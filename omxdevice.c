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

// latency target for transfer mode in PTS ticks (90kHz) -> 200ms
#define LATENCY_TARGET 18000LL
// latency window for validation where closed loop will be active (+/- 4s)
#define LATENCY_WINDOW 360000LL

#define S(x) ((int)(floor(x * pow(2, 16))))

// trick speeds as defined in vdr/dvbplayer.c
int cOmxDevice::s_speeds[2][8] = {
	{ S(0.0f), S( 0.125f), S( 0.25f), S( 0.5f), S( 1.0f), S( 2.0f), S( 4.0f), S( 12.0f) },
	{ S(0.0f), S(-0.125f), S(-0.25f), S(-0.5f), S(-1.0f), S(-2.0f), S(-4.0f), S(-12.0f) }
};

// speed correction factors for live mode, taken from omxplayer
int cOmxDevice::s_speedCorrections[5] = {
	S(0.990f), S(0.999f), S(1.000f), S(1.001), S(1.010)
};

cOmxDevice::cOmxDevice(void (*onPrimaryDevice)(void)) :
	cDevice(),
	m_onPrimaryDevice(onPrimaryDevice),
	m_omx(new cOmx()),
	m_audio(new cRpiAudioDecoder(m_omx)),
	m_mutex(new cMutex()),
	m_videoCodec(cVideoCodec::eInvalid),
	m_speed(eNormal),
	m_direction(eForward),
	m_hasVideo(false),
	m_hasAudio(false),
	m_skipAudio(false),
	m_playDirection(0),
	m_trickRequest(0),
	m_audioPts(0),
	m_videoPts(0),
	m_latency(0)
{
}

cOmxDevice::~cOmxDevice()
{
	DeInit();

	delete m_omx;
	delete m_audio;
	delete m_mutex;
}

int cOmxDevice::Init(void)
{
	if (m_omx->Init() < 0)
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

	cRpiSetup::SetVideoSetupChangedCallback(&OnVideoSetupChanged, this);
	HandleVideoSetupChanged();

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

void cOmxDevice::GetOsdSize(int &Width, int &Height, double &PixelAspect)
{
	cRpiSetup::GetDisplaySize(Width, Height, PixelAspect);
}

void cOmxDevice::GetVideoSize(int &Width, int &Height, double &VideoAspect)
{
	bool interlaced;
	m_omx->GetVideoSize(Width, Height, interlaced);

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
		m_omx->SetClockReference(cOmx::eClockRefVideo);
		m_videoCodec = cVideoCodec::eInvalid;
		m_hasAudio = false;
		m_hasVideo = false;
		break;

	case pmAudioVideo:
	case pmAudioOnly:
	case pmAudioOnlyBlack:
	case pmVideoOnly:
		m_speed = eNormal;
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
		m_mutex->Lock();

		// manually restart clock and wait for video only
		m_omx->StopClock();
		m_omx->SetClockScale(s_speeds[eForward][eNormal]);
		m_omx->StartClock(true, false);

		// to get a picture displayed, PlayVideo() needs to be called
		// 4x for MPEG2 and 13x for H264... ?
		int repeat =
			ParseVideoCodec(Data, Length) == cVideoCodec::eMPEG2 ? 4 : 13;

		while (repeat--)
		{
			const uchar *data = Data;
			int length = Length;

			// play every single PES packet, rise EOS flag on last
			while (length)
			{
				int pktLen = PesHasLength(data) ? PesLength(data) : length;
				PlayVideo(data, pktLen, !repeat && (pktLen == length));
				data += pktLen;
				length -= pktLen;
			}
		}

		m_mutex->Unlock();
	}
}

int cOmxDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
	if (m_skipAudio)
		return Length;

	m_mutex->Lock();

	if (!m_hasAudio)
	{
		m_hasAudio = true;
		m_omx->SetClockReference(cOmx::eClockRefAudio);

		// actually, clock should be restarted anyway, but if video is already
		// present, decoder will get stuck after clock restart and raises a
		// buffer stall
		if (!m_hasVideo)
		{
			FlushStreams();
			m_omx->SetClockScale(s_speeds[m_direction][m_speed]);
			m_omx->StartClock(m_hasVideo, m_hasAudio);
		}
	}

	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

	// keep track of direction in case of trick speed
	if (m_trickRequest && pts)
	{
		if (m_audioPts)
			PtsTracker(PtsDiff(m_audioPts, pts));

		m_audioPts = pts;
	}

	if (Transferring() && pts)
		UpdateLatency(pts);

	// ignore packets with invalid payload offset
	int ret = (Length - PesPayloadOffset(Data) < 0) ||
			m_audio->WriteData(Data + PesPayloadOffset(Data),
			Length - PesPayloadOffset(Data), pts) ? Length : 0;

	m_mutex->Unlock();
	return ret;
}

int cOmxDevice::PlayVideo(const uchar *Data, int Length, bool EndOfStream)
{
	m_mutex->Lock();
	int ret = Length;

	cVideoCodec::eCodec codec = ParseVideoCodec(Data, Length);

	// video restart after Clear() with same codec
	bool videoRestart = (!m_hasVideo && codec == m_videoCodec &&
			cRpiSetup::IsVideoCodecSupported(codec));

	// video restart after SetPlayMode() or codec changed
	if (codec != cVideoCodec::eInvalid && codec != m_videoCodec)
	{
		m_videoCodec = codec;

		if (m_hasVideo)
		{
			m_omx->StopVideo();
			m_hasVideo = false;
		}

		if (cRpiSetup::IsVideoCodecSupported(codec))
		{
			videoRestart = true;
			m_omx->SetVideoCodec(codec, cOmx::eArbitraryStreamSection);
			DLOG("set video codec to %s", cVideoCodec::Str(codec));
		}
		else
			Skins.QueueMessage(mtError, tr("video format not supported!"));
	}

	if (videoRestart)
	{
		m_hasVideo = true;
		FlushStreams();
		m_omx->SetClockScale(s_speeds[m_direction][m_speed]);
		m_omx->StartClock(m_hasVideo, m_hasAudio);
	}

	if (m_hasVideo)
	{
		int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

		// keep track of direction in case of trick speed
		if (m_trickRequest && pts)
		{
			if (m_videoPts)
				PtsTracker(PtsDiff(m_videoPts, pts));

			m_videoPts = pts;
		}

		if (!m_hasAudio && Transferring() && pts)
			UpdateLatency(pts);

		// skip PES header, proceed with payload towards OMX
		Length -= PesPayloadOffset(Data);
		Data += PesPayloadOffset(Data);

		while (Length > 0)
		{
			OMX_BUFFERHEADERTYPE *buf = m_omx->GetVideoBuffer(pts);
			if (buf)
			{
				buf->nFilledLen = buf->nAllocLen < (unsigned)Length ?
						buf->nAllocLen : Length;

				memcpy(buf->pBuffer, Data, buf->nFilledLen);
				Length -= buf->nFilledLen;
				Data += buf->nFilledLen;

				if (EndOfStream && !Length)
					buf->nFlags |= OMX_BUFFERFLAG_EOS;

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
			pts = 0;
		}
	}

	m_mutex->Unlock();
	return ret;
}

int64_t cOmxDevice::GetSTC(void)
{
	return m_omx->GetSTC();
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
		ret = (uint8_t *)malloc(Size);
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
	m_omx->SetClockScale(s_speeds[m_direction][m_speed]);
	m_omx->StartClock(m_hasVideo, m_hasAudio);

	m_mutex->Unlock();
	cDevice::Clear();
}

void cOmxDevice::Play(void)
{
	DBG("Play()");
	m_mutex->Lock();

	m_speed = eNormal;
	m_direction = eForward;
	m_omx->SetClockScale(s_speeds[m_direction][m_speed]);
	m_skipAudio = false;

	m_mutex->Unlock();
	cDevice::Play();
}

void cOmxDevice::Freeze(void)
{
	DBG("Freeze()");
	m_mutex->Lock();

	m_omx->SetClockScale(s_speeds[eForward][ePause]);

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
	bool flush = HasIBPTrickSpeed();

	m_speed =
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

	m_direction = forward ? eForward : eBackward;

	// we only need to flush when IBP trick mode has changed,
	// for the other transitions VDR will call Clear() if necessary
	flush ^= HasIBPTrickSpeed();

	// if there is video to play, we're going to skip audio
	// but first, we need to flush audio
	if (!m_skipAudio && m_hasVideo && !HasIBPTrickSpeed())
	{
		m_audio->Reset();
		m_omx->FlushAudio();
		m_skipAudio = true;
	}

	if (flush)
		FlushStreams();

	m_omx->SetClockScale(s_speeds[m_direction][m_speed]);

	if (flush)
		m_omx->StartClock(m_hasVideo, !m_skipAudio);

	DBG("ApplyTrickSpeed(%s, %s)",
			SpeedStr(m_speed), DirectionStr(m_direction));
}

bool cOmxDevice::HasIBPTrickSpeed(void)
{
	// IBP trick speed only supported at first fast forward speed or
	// for audio only recordings at every speed
	return m_direction == eForward && (m_speed <= eFast || !m_hasVideo);
}

void cOmxDevice::PtsTracker(int64_t ptsDiff)
{
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

void cOmxDevice::UpdateLatency(int64_t pts)
{
	// calculate and validate latency
	uint64_t latency = pts - m_omx->GetSTC();
	if (abs(latency > LATENCY_WINDOW))
		return;

	m_latency = (7 * m_latency + latency) >> 3;
	eSpeedCorrection corr = eNoCorrection;

	if (m_latency < 0.5f * LATENCY_TARGET)
		corr = eNegMaxCorrection;
	else if (m_latency < 0.9f * LATENCY_TARGET)
		corr = eNegCorrection;
	else if (m_latency > 2.0f * LATENCY_TARGET)
		corr = ePosMaxCorrection;
	else if (m_latency > 1.1f * LATENCY_TARGET)
		corr = ePosCorrection;

	m_omx->SetClockScale(s_speedCorrections[corr]);
}

void cOmxDevice::HandleBufferStall()
{
	ELOG("buffer stall!");
	m_mutex->Lock();

	FlushStreams();
	m_omx->SetClockScale(s_speeds[m_direction][m_speed]);
	m_omx->StartClock(m_hasVideo, m_hasAudio);

	m_mutex->Unlock();
}

void cOmxDevice::HandleEndOfStream()
{
	DBG("HandleEndOfStream()");

	// flush pipes and restart clock after still image
	FlushStreams();
	m_omx->SetClockScale(s_speeds[eForward][ePause]);
	m_omx->StartClock(m_hasVideo, m_hasAudio);
}

void cOmxDevice::HandleVideoSetupChanged()
{
	DBG("HandleVideoSettingsChanged()");

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
}

void cOmxDevice::FlushStreams(bool flushVideoRender)
{
	DBG("FlushStreams(%s)", flushVideoRender ? "flushVideoRender" : "");

	m_omx->StopClock();
	m_omx->SetClockScale(0.0f);

	if (m_hasVideo)
		m_omx->FlushVideo(flushVideoRender);

	if (m_hasAudio)
	{
		m_audio->Reset();
		m_omx->FlushAudio();
	}

	m_omx->SetCurrentReferenceTime(0);
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
	cTimeMs time;
	time.Set();
	while (!m_omx->PollVideoBuffers() || !m_audio->Poll())
	{
		if (time.Elapsed() >= (unsigned)TimeoutMs)
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
	if (!PesHasPts(data))
		return cVideoCodec::eInvalid;

	if (PesLength(data) - PesPayloadOffset(data) < 6)
		return cVideoCodec::eInvalid;

	const uchar *p = data + PesPayloadOffset(data);

	for (int i = 0; i < 5; i++)
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
