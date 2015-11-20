/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "omxdevice.h"
#include "omx.h"
#include "audio.h"
#include "display.h"
#include "setup.h"

#include <vdr/thread.h>
#include <vdr/remux.h>
#include <vdr/tools.h>
#include <vdr/skins.h>

#include <string.h>

#define S(x) ((int)(floor(x * pow(2, 16))))
#define PTS_START_OFFSET (32 * (MAX33BIT + 1))

// trick speeds as defined in vdr/dvbplayer.c
const int cOmxDevice::s_playbackSpeeds[eNumDirections][eNumPlaybackSpeeds] = {
	{ S(0.0f), S( 0.125f), S( 0.25f), S( 0.5f), S( 1.0f), S( 2.0f), S( 4.0f), S( 12.0f) },
	{ S(0.0f), S(-0.125f), S(-0.25f), S(-0.5f), S(-1.0f), S(-2.0f), S(-4.0f), S(-12.0f) }
};

// speed correction factors for live mode
// HDMI specification allows a tolerance of 1000ppm, however on the Raspberry Pi
// it's limited to 175ppm to avoid audio drops one some A/V receivers
const int cOmxDevice::s_liveSpeeds[eNumLiveSpeeds] = {
	S(0.999f), S(0.99985f), S(1.000f), S(1.00015), S(1.001)
};

const uchar cOmxDevice::PesVideoHeader[14] = {
	0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00
};

cOmxDevice::cOmxDevice(void (*onPrimaryDevice)(void), int display, int layer) :
	cDevice(),
	m_onPrimaryDevice(onPrimaryDevice),
	m_omx(new cOmx()),
	m_audio(new cRpiAudioDecoder(m_omx)),
	m_mutex(new cMutex()),
	m_timer(new cTimeMs()),
	m_videoCodec(cVideoCodec::eInvalid),
	m_playMode(pmNone),
	m_liveSpeed(eNoCorrection),
	m_playbackSpeed(eNormal),
	m_direction(eForward),
	m_hasVideo(false),
	m_hasAudio(false),
	m_skipAudio(false),
	m_playDirection(0),
	m_trickRequest(0),
	m_audioPts(0),
	m_videoPts(0),
	m_lastStc(0),
	m_display(display),
	m_layer(layer)
{
}

cOmxDevice::~cOmxDevice()
{
	DeInit();

	delete m_omx;
	delete m_audio;
	delete m_mutex;
	delete m_timer;
}

int cOmxDevice::Init(void)
{
	if (m_omx->Init(m_display, m_layer) < 0)
	{
		ELOG("failed to initialize OMX!");
		return -1;
	}
	if (m_audio->Init() < 0)
	{
		ELOG("failed to initialize audio!");
		return -1;
	}
	m_omx->SetBufferStallCallback(&OnBufferStall, this);
	m_omx->SetEndOfStreamCallback(&OnEndOfStream, this);
	m_omx->SetStreamStartCallback(&OnStreamStart, this);

	cRpiSetup::SetVideoSetupChangedCallback(&OnVideoSetupChanged, this);

	return 0;
}

int cOmxDevice::DeInit(void)
{
	cRpiSetup::SetVideoSetupChangedCallback(0);
	if (m_audio->DeInit() < 0)
	{
		ELOG("failed to deinitialize audio!");
		return -1;
	}
	if (m_omx->DeInit() < 0)
	{
		ELOG("failed to deinitialize OMX!");
		return -1;
	}
	return 0;
}

bool cOmxDevice::Start(void)
{
	HandleVideoSetupChanged();
	return true;
}

void cOmxDevice::GetOsdSize(int &Width, int &Height, double &PixelAspect)
{
	cRpiDisplay::GetSize(Width, Height, PixelAspect);
}

void cOmxDevice::GetVideoSize(int &Width, int &Height, double &VideoAspect)
{
	Height = m_omx->GetVideoFrameFormat()->height;
	Width = m_omx->GetVideoFrameFormat()->width;

	if (Height)
		VideoAspect = (double)Width / Height;
	else
		VideoAspect = 1.0;
}

void cOmxDevice::ScaleVideo(const cRect &Rect)
{
	DBG("ScaleVideo(%d, %d, %d, %d)",
		Rect.X(), Rect.Y(), Rect.Width(), Rect.Height());

	m_omx->SetDisplayRegion(Rect.X(), Rect.Y(), Rect.Width(), Rect.Height());
}

bool cOmxDevice::SetPlayMode(ePlayMode PlayMode)
{
	m_mutex->Lock();
	DBG("SetPlayMode(%s)",
		PlayMode == pmNone			 ? "none" 			   :
		PlayMode == pmAudioVideo	 ? "Audio/Video" 	   :
		PlayMode == pmAudioOnly		 ? "Audio only" 	   :
		PlayMode == pmAudioOnlyBlack ? "Audio only, black" :
		PlayMode == pmVideoOnly		 ? "Video only" 	   : 
									   "unsupported");

	// Stop audio / video if play mode is set to pmNone. Start
	// is triggered once a packet is going to be played, since
	// we don't know what kind of stream we'll get (audio-only,
	// video-only or both) after SetPlayMode() - VDR will always
	// pass pmAudioVideo as argument.

	switch (PlayMode)
	{
	case pmNone:
		FlushStreams(true);
		m_omx->StopVideo();
		m_hasAudio = false;
		m_hasVideo = false;
		m_videoCodec = cVideoCodec::eInvalid;
		m_playMode = pmNone;
		break;

	case pmAudioVideo:
	case pmAudioOnly:
	case pmAudioOnlyBlack:
	case pmVideoOnly:
		m_playbackSpeed = eNormal;
		m_direction = eForward;
		break;

	default:
		break;
	}

	m_mutex->Unlock();
	return true;
}

void cOmxDevice::StillPicture(const uchar *Data, int Length)
{
	if (Data[0] == 0x47)
		cDevice::StillPicture(Data, Length);
	else
	{
		DBG("StillPicture()");
		int pesLength = 0;
		uchar *pesPacket = 0;

		cVideoCodec::eCodec codec = ParseVideoCodec(Data, Length);
		if (codec != cVideoCodec::eInvalid)
		{
			// some plugins deliver raw MPEG data, but PlayVideo() needs a
			// complete PES packet with valid header
			pesLength = Length + sizeof(PesVideoHeader);
			pesPacket = MALLOC(uchar, pesLength);
			if (!pesPacket)
				return;

			memcpy(pesPacket, PesVideoHeader, sizeof(PesVideoHeader));
			memcpy(pesPacket + sizeof(PesVideoHeader), Data, Length);
		}
		else
			codec = ParseVideoCodec(Data + PesPayloadOffset(Data),
					Length - PesPayloadOffset(Data));

		if (codec == cVideoCodec::eInvalid)
			return;

		m_mutex->Lock();
		m_playbackSpeed = eNormal;
		m_direction = eForward;
		m_hasVideo = false;
		m_omx->StopClock();

		// to get a picture displayed, PlayVideo() needs to be called
		// 4x for MPEG2 and 10x for H264... ?
		int repeat = codec == cVideoCodec::eMPEG2 ? 4 : 10;
		while (repeat--)
		{
			int length = pesPacket ? pesLength : Length;
			const uchar *data = pesPacket ? pesPacket : Data;

			// play every single PES packet, rise ENDOFFRAME flag on last
			while (PesLongEnough(length))
			{
				int pktLen = PesHasLength(data) ? PesLength(data) : length;

				// skip non-video packets as they may occur in PES recordings
				if ((data[3] & 0xf0) == 0xe0)
					PlayVideo(data, pktLen, pktLen == length);

				data += pktLen;
				length -= pktLen;
			}
		}
		SubmitEOS();
		m_mutex->Unlock();

		if (pesPacket)
			free(pesPacket);
	}
}

int cOmxDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
	// ignore audio packets during trick speeds for non-radio recordings
	if (m_playbackSpeed != eNormal && m_playMode != pmAudioOnly)
	{
		DLOG("audio packet ignored!");
		return Length;
	}

	m_mutex->Lock();
	int ret = Length;
	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : OMX_INVALID_PTS;

	if (pts != OMX_INVALID_PTS)
	{
		if (!m_hasAudio)
		{
			m_hasAudio = true;
			m_omx->SetClockReference(cOmx::eClockRefAudio);

			if (!m_hasVideo)
			{
				DBG("audio first");
				m_omx->SetClockScale(
						s_playbackSpeeds[m_direction][m_playbackSpeed]);
				m_omx->StartClock(m_hasVideo, m_hasAudio);
				m_audioPts = PTS_START_OFFSET + pts;
				m_playMode = pmAudioOnly;
			}
			else
			{
				m_audioPts = m_videoPts + PtsDiff(m_videoPts & MAX33BIT, pts);
				m_playMode = pmAudioVideo;
			}
		}

		int64_t ptsDiff = PtsDiff(m_audioPts & MAX33BIT, pts);

		if ((m_audioPts & ~MAX33BIT) != (m_audioPts + ptsDiff & ~MAX33BIT))
			DBG("audio PTS wrap around");

		m_audioPts += ptsDiff;

		// keep track of direction in case of trick speed
		if (m_trickRequest && ptsDiff)
			PtsTracker(ptsDiff);
	}

	int length = Length - PesPayloadOffset(Data);

	// ignore packets with invalid payload offset
	if (length > 0)
	{
		const uchar *data = Data + PesPayloadOffset(Data);

		// remove audio substream header as seen in PES recordings with AC3
		// audio track (0x80: AC3, 0x88: DTS, 0xA0: LPCM)
		if ((data[0] == 0x80 || data[0] == 0x88 || data[0] == 0xa0)
				&& data[0] == Id)
		{
			data += 4;
			length -= 4;
		}
		if (!m_audio->WriteData(data, length,
				pts != OMX_INVALID_PTS ? m_audioPts : OMX_INVALID_PTS))
			ret = 0;
	}
	m_mutex->Unlock();

	if (Transferring() && !ret)
		DBG("failed to write %d bytes of audio packet!", Length);

	if (ret && Transferring())
		AdjustLiveSpeed();

	return ret;
}

int cOmxDevice::PlayVideo(const uchar *Data, int Length, bool EndOfFrame)
{
	// prevent writing incomplete frames
	if (m_hasVideo && !m_omx->PollVideo())
		return 0;

	m_mutex->Lock();
	int ret = Length;
	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : OMX_INVALID_PTS;

	if (!m_hasVideo && pts != OMX_INVALID_PTS &&
			m_videoCodec == cVideoCodec::eInvalid)
	{
		m_videoCodec = ParseVideoCodec(Data + PesPayloadOffset(Data),
				Length - PesPayloadOffset(Data));

		if (m_videoCodec != cVideoCodec::eInvalid)
		{
			if (cRpiSetup::IsVideoCodecSupported(m_videoCodec))
			{
				m_omx->SetVideoCodec(m_videoCodec);
				DLOG("set video codec to %s", cVideoCodec::Str(m_videoCodec));
			}
			else
				Skins.QueueMessage(mtError, tr("video format not supported!"));
		}
	}

	if (!m_hasVideo && pts != OMX_INVALID_PTS &&
			cRpiSetup::IsVideoCodecSupported(m_videoCodec))
	{
		m_hasVideo = true;
		if (!m_hasAudio)
		{
			DBG("video first");
			m_omx->SetClockReference(cOmx::eClockRefVideo);
			m_omx->SetClockScale(s_playbackSpeeds[m_direction][m_playbackSpeed]);
			m_omx->StartClock(m_hasVideo, m_hasAudio);
			m_videoPts = PTS_START_OFFSET + pts;
			m_playMode = pmVideoOnly;
		}
		else
		{
			m_videoPts = m_audioPts + PtsDiff(m_audioPts & MAX33BIT, pts);
			m_playMode = pmAudioVideo;
		}
	}

	if (m_hasVideo)
	{
		if (pts != OMX_INVALID_PTS)
		{
			int64_t ptsDiff = PtsDiff(m_videoPts & MAX33BIT, pts);
			m_videoPts += ptsDiff;

			// keep track of direction in case of trick speed
			if (m_trickRequest && ptsDiff)
				PtsTracker(ptsDiff);
		}

		// skip PES header, proceed with payload towards OMX
		Length -= PesPayloadOffset(Data);
		Data += PesPayloadOffset(Data);

		while (Length > 0)
		{
			if (OMX_BUFFERHEADERTYPE *buf =	m_omx->GetVideoBuffer(
					pts != OMX_INVALID_PTS ? m_videoPts : OMX_INVALID_PTS))
			{
				buf->nFilledLen = buf->nAllocLen < (unsigned)Length ?
						buf->nAllocLen : Length;

				memcpy(buf->pBuffer, Data, buf->nFilledLen);
				Length -= buf->nFilledLen;
				Data += buf->nFilledLen;

				if (EndOfFrame && !Length)
					buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

				if (!m_omx->EmptyVideoBuffer(buf))
				{
					ret = 0;
					ELOG("failed to pass buffer to video decoder!");
					break;
				}
			}
			else
			{
				ret = 0;
				break;
			}
			pts = OMX_INVALID_PTS;
		}
	}
	m_mutex->Unlock();

	if (Transferring() && !ret)
		DBG("failed to write %d bytes of video packet!", Length);

	if (ret && Transferring())
		AdjustLiveSpeed();

	return ret;
}

bool cOmxDevice::SubmitEOS(void)
{
	DBG("SubmitEOS()");
	OMX_BUFFERHEADERTYPE *buf = m_omx->GetVideoBuffer(0);
	if (buf)
		buf->nFlags = /*OMX_BUFFERFLAG_ENDOFFRAME | */ OMX_BUFFERFLAG_EOS;

	return m_omx->EmptyVideoBuffer(buf);
}

int64_t cOmxDevice::GetSTC(void)
{
	int64_t stc = m_omx->GetSTC();
	if (stc != OMX_INVALID_PTS)
		m_lastStc = stc;
	return m_lastStc & MAX33BIT;
}

uchar *cOmxDevice::GrabImage(int &Size, bool Jpeg, int Quality,
		int SizeX, int SizeY)
{
	DBG("GrabImage(%s, %dx%d)", Jpeg ? "JPEG" : "PNM", SizeX, SizeY);

	uint8_t* ret = NULL;
	int width, height;
	cRpiDisplay::GetSize(width, height);

	SizeX = (SizeX > 0) ? SizeX : width;
	SizeY = (SizeY > 0) ? SizeY : height;
	Quality = (Quality >= 0) ? Quality : 100;

	// bigger than needed, but uint32_t ensures proper alignment
	uint8_t* frame = (uint8_t*)(MALLOC(uint32_t, SizeX * SizeY));

	if (!frame)
	{
		ELOG("failed to allocate image buffer!");
		return ret;
	}

	if (cRpiDisplay::Snapshot(frame, SizeX, SizeY))
	{
		ELOG("failed to grab image!");
		free(frame);
		return ret;
	}

	if (Jpeg)
		ret = RgbToJpeg(frame, SizeX, SizeY, Size, Quality);
	else
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "P6\n%d\n%d\n255\n", SizeX, SizeY);
		int l = strlen(buf);
		Size = l + SizeX * SizeY * 3;
		ret = MALLOC(uint8_t, Size);
		if (ret)
		{
			memcpy(ret, buf, l);
			memcpy(ret + l, frame, SizeX * SizeY * 3);
		}
	}
	free(frame);
	return ret;
}

void cOmxDevice::Clear(void)
{
	DBG("Clear()");
	m_mutex->Lock();

	FlushStreams();
	m_hasAudio = false;
	m_hasVideo = false;

	m_mutex->Unlock();
	cDevice::Clear();
}

void cOmxDevice::Play(void)
{
	DBG("Play()");
	m_mutex->Lock();

	m_playbackSpeed = eNormal;
	m_direction = eForward;
	m_omx->SetClockScale(s_playbackSpeeds[m_direction][m_playbackSpeed]);

	m_mutex->Unlock();
	cDevice::Play();
}

void cOmxDevice::Freeze(void)
{
	DBG("Freeze()");
	m_mutex->Lock();

	m_omx->SetClockScale(s_playbackSpeeds[eForward][ePause]);

	m_mutex->Unlock();
	cDevice::Freeze();
}

#if APIVERSNUM >= 20103
void cOmxDevice::TrickSpeed(int Speed, bool Forward)
{
	m_mutex->Lock();
	ApplyTrickSpeed(Speed, Forward);
	m_mutex->Unlock();
}
#else
void cOmxDevice::TrickSpeed(int Speed)
{
	m_mutex->Lock();
	m_audioPts = 0;
	m_videoPts = 0;
	m_playDirection = 0;

	// play direction is ambiguous for fast modes, start PTS tracking
	if (Speed == 1 || Speed == 3 || Speed == 6)
		m_trickRequest = Speed;
	else
		ApplyTrickSpeed(Speed, (Speed == 8 || Speed == 4 || Speed == 2));

	m_mutex->Unlock();
}
#endif

void cOmxDevice::ApplyTrickSpeed(int trickSpeed, bool forward)
{
	m_direction = forward ? eForward : eBackward;
	m_playbackSpeed =

		// slow forward
		trickSpeed ==  8 ? eSlowest :
		trickSpeed ==  4 ? eSlower  :
		trickSpeed ==  2 ? eSlow    :

		// fast for-/backward
		trickSpeed ==  6 ? eFast    :
		trickSpeed ==  3 ? eFaster  :
		trickSpeed ==  1 ? eFastest :

		// slow backward
		trickSpeed == 63 ? eSlowest :
		trickSpeed == 48 ? eSlower  :
		trickSpeed == 24 ? eSlow    : eNormal;

	m_omx->SetClockScale(s_playbackSpeeds[m_direction][m_playbackSpeed]);

	DBG("ApplyTrickSpeed(%s, %s)",
			PlaybackSpeedStr(m_playbackSpeed), DirectionStr(m_direction));
	return;
}

void cOmxDevice::PtsTracker(int64_t ptsDiff)
{
	DBG("PtsTracker(%lld)", ptsDiff);

	if (ptsDiff < 0)
		--m_playDirection;
	else if (ptsDiff > 0)
		m_playDirection += 2;

	if (m_playDirection < -2 || m_playDirection > 3)
	{
		ApplyTrickSpeed(m_trickRequest, m_playDirection > 0);
		m_trickRequest = 0;
	}
}

bool cOmxDevice::HasIBPTrickSpeed(void)
{
	return !m_hasVideo;
}

void cOmxDevice::AdjustLiveSpeed(void)
{
	if (m_timer->TimedOut())
	{
		int usedBuffers, usedAudioBuffers, usedVideoBuffers;
		m_omx->GetBufferUsage(usedAudioBuffers, usedVideoBuffers);
		usedBuffers = m_hasAudio ? usedAudioBuffers : usedVideoBuffers;

		if (usedBuffers < 5)
			m_liveSpeed = eNegCorrection;

		else if (usedBuffers > 15)
			m_liveSpeed = ePosCorrection;

		else if ((usedBuffers > 10 && m_liveSpeed == eNegCorrection) ||
				(usedBuffers < 10 && m_liveSpeed == ePosCorrection))
			m_liveSpeed = eNoCorrection;

#ifdef DEBUG_BUFFERSTAT
		DLOG("buffer usage: A=%3d%%, V=%3d%%, Corr=%d",
				usedAudioBuffers, usedVideoBuffers,
				m_liveSpeed == eNegMaxCorrection ? -2 :
				m_liveSpeed == eNegCorrection    ? -1 :
				m_liveSpeed == eNoCorrection     ?  0 :
				m_liveSpeed == ePosCorrection    ?  1 :
				m_liveSpeed == ePosMaxCorrection ?  2 : 0);
#endif
		m_omx->SetClockScale(s_liveSpeeds[m_liveSpeed]);
		m_timer->Set(1000);
	}
}

void cOmxDevice::HandleBufferStall()
{
	ELOG("buffer stall!");
	m_mutex->Lock();

	FlushStreams(true);
	m_omx->StopVideo();

	m_hasAudio = false;
	m_hasVideo = false;
	m_videoCodec = cVideoCodec::eInvalid;

	m_mutex->Unlock();
}

void cOmxDevice::HandleEndOfStream()
{
	DBG("HandleEndOfStream()");
	m_mutex->Lock();

	// flush pipes and restart clock after still image
	FlushStreams();
	m_omx->SetClockScale(s_playbackSpeeds[m_direction][m_playbackSpeed]);
	m_omx->StartClock(m_hasVideo, m_hasAudio);

	m_mutex->Unlock();
}

void cOmxDevice::HandleStreamStart()
{
	DBG("HandleStreamStart()");

	const cVideoFrameFormat *format = m_omx->GetVideoFrameFormat();
	DLOG("video stream started %dx%d@%d%s",	format->width, format->height,
			format->frameRate, format->Interlaced() ? "i" : "p");

	cRpiDisplay::SetVideoFormat(format);
}

void cOmxDevice::HandleVideoSetupChanged()
{
	DBG("HandleVideoSetupChanged()");

	switch (cRpiSetup::GetVideoFraming())
	{
	default:
	case cVideoFraming::eFrame:
		m_omx->SetDisplayMode(false, false);
		break;

	case cVideoFraming::eCut:
		m_omx->SetDisplayMode(true, false);
		break;

	case cVideoFraming::eStretch:
		m_omx->SetDisplayMode(true, true);
		break;
	}

	cRpiDisplay::SetVideoFormat(m_omx->GetVideoFrameFormat());
}

void cOmxDevice::FlushStreams(bool flushVideoRender)
{
	DBG("FlushStreams(%s)", flushVideoRender ? "flushVideoRender" : "");
	m_omx->StopClock();

	if (m_hasVideo)
		m_omx->FlushVideo(flushVideoRender);

	if (m_hasAudio)
		m_audio->Reset();

	m_omx->ResetClock();
}

void cOmxDevice::SetVolumeDevice(int Volume)
{
	DBG("SetVolume(%d)", Volume);
	if (Volume)
	{
		m_omx->SetVolume(Volume);
		m_omx->SetMute(false);
	}
	else
		m_omx->SetMute(true);
}

bool cOmxDevice::Poll(cPoller &Poller, int TimeoutMs)
{
	cTimeMs timer(TimeoutMs);
	while (!m_omx->PollVideo() || !m_audio->Poll())
	{
		if (timer.TimedOut())
			return false;
		cCondWait::SleepMs(5);
	}
	return true;
}

void cOmxDevice::MakePrimaryDevice(bool On)
{
	if (On && m_onPrimaryDevice)
		m_onPrimaryDevice();
	cDevice::MakePrimaryDevice(On);
}

cVideoCodec::eCodec cOmxDevice::ParseVideoCodec(const uchar *data, int length)
{
	const uchar *p = data;

	for (int i = 0; (i < 5) && (i + 4 < length); i++)
	{
		// find start code prefix - should be right at the beginning of payload
		if ((!p[i] && !p[i + 1] && p[i + 2] == 0x01))
		{
			if (p[i + 3] == 0xb3)		// sequence header
				return cVideoCodec::eMPEG2;

			//p[i + 3] = 0xf0
			else if (p[i + 3] == 0x09)	// slice
			{
				// quick hack for converted mkvs
				if (p[i + 4] == 0xf0)
					return cVideoCodec::eH264;

				switch (p[i + 4] >> 5)
				{
				case 0: case 3: case 5: // I frame
					return cVideoCodec::eH264;

				case 2: case 7:			// B frame
				case 1: case 4: case 6:	// P frame
				default:
//					return cVideoCodec::eInvalid;
					return cVideoCodec::eH264;
				}
			}
			return cVideoCodec::eInvalid;
		}
	}
	return cVideoCodec::eInvalid;
}
