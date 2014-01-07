/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "omxdevice.h"
#include "omx.h"
#include "audio.h"
#include "setup.h"

#include <vdr/remux.h>
#include <vdr/tools.h>
#include <vdr/skins.h>

#include <string.h>

cOmxDevice::cOmxDevice(void (*onPrimaryDevice)(void)) :
	cDevice(),
	m_onPrimaryDevice(onPrimaryDevice),
	m_omx(new cOmx()),
	m_audio(new cAudioDecoder(m_omx)),
	m_mutex(new cMutex()),
	m_videoCodec(cVideoCodec::eInvalid),
	m_hasVideo(false),
	m_hasAudio(false),
	m_skipAudio(false),
	m_playDirection(0),
	m_trickRequest(0),
	m_audioPts(0),
	m_videoPts(0)
{
}

cOmxDevice::~cOmxDevice()
{
	delete m_omx;
	delete m_audio;
	delete m_mutex;
}

int cOmxDevice::Init(void)
{
	if (m_omx->Init() < 0)
	{
		esyslog("rpihddevice: failed to initialize OMX!");
		return -1;
	}
	if (m_audio->Init() < 0)
	{
		esyslog("rpihddevice: failed to initialize audio!");
		return -1;
	}
	return 0;
}

int cOmxDevice::DeInit(void)
{
	if (m_audio->DeInit() < 0)
	{
		esyslog("rpihddevice: failed to deinitialize audio!");
		return -1;
	}
	if (m_omx->DeInit() < 0)
	{
		esyslog("rpihddevice: failed to deinitialize OMX!");
		return -1;
	}
	return 0;
}

void cOmxDevice::GetOsdSize(int &Width, int &Height, double &PixelAspect)
{
	cRpiSetup::GetDisplaySize(Width, Height, PixelAspect);
}

bool cOmxDevice::SetPlayMode(ePlayMode PlayMode)
{
	m_mutex->Lock();
	dsyslog("rpihddevice: SetPlayMode(%s)",
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
		ResetAudioVideo(true);
		m_omx->StopVideo();
		m_videoCodec = cVideoCodec::eInvalid;
		break;

	case pmAudioVideo:
	case pmAudioOnly:
	case pmAudioOnlyBlack:
	case pmVideoOnly:
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
		// to get a picture displayed, PlayVideo() needs to be called
		// twice for MPEG2 and 6x for H264... ?
		for (int i = 0; i < (m_videoCodec == cVideoCodec::eMPEG2 ? 2 : 6); i++)
			PlayVideo(Data, Length, true);
	}
}

int cOmxDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
	if (m_skipAudio)
		return Length;

	m_mutex->Lock();
	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

	if (!m_hasAudio)
	{
		m_omx->SetClockReference(cOmx::eClockRefAudio);
		m_hasAudio = true;
	}

	// keep track of direction in case of trick speed
	if (m_trickRequest && pts)
	{
		if (m_audioPts)
			PtsTracker(PtsDiff(m_audioPts, pts));

		m_audioPts = pts;
	}

	int ret = m_audio->WriteData(Data + PesPayloadOffset(Data),
			Length - PesPayloadOffset(Data), pts);

	m_mutex->Unlock();
	return ret;
}

int cOmxDevice::PlayVideo(const uchar *Data, int Length, bool singleFrame)
{
	m_mutex->Lock();
	int ret = Length;

	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;
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
			dsyslog("rpihddevice: set video codec to %s",
					cVideoCodec::Str(codec));
		}
		else
			Skins.QueueMessage(mtError, tr("video format not supported!"));
	}

	if (videoRestart)
	{
		m_hasVideo = true;

		if (!m_hasAudio)
		{
			m_omx->SetClockReference(cOmx::eClockRefVideo);
			m_omx->SetCurrentReferenceTime(0);
			m_omx->SetClockState(cOmx::eClockStateWaitForVideo);
		}
	}

	// keep track of direction in case of trick speed
	if (m_trickRequest && pts)
	{
		if (m_videoPts)
			PtsTracker(PtsDiff(m_videoPts, pts));

		m_videoPts = pts;
	}

	if (m_hasVideo)
	{
		while (Length)
		{
			const uchar *payload = Data + PesPayloadOffset(Data);
			unsigned int length = PesLength(Data) - PesPayloadOffset(Data);

			OMX_BUFFERHEADERTYPE *buf = m_omx->GetVideoBuffer(pts);
			if (buf)
			{
				buf->nFilledLen = length;
				memcpy(buf->pBuffer, payload, length);

				if (singleFrame && Length == PesLength(Data))
					buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

				if (!m_omx->EmptyVideoBuffer(buf))
				{
					ret = 0;
					esyslog("rpihddevice: failed to pass buffer to video decoder!");
					break;
				}
			}
			else
			{
				ret = 0;
				break;
			}
			Length -= PesLength(Data);
			Data += PesLength(Data);
		}
	}

	m_mutex->Unlock();
	return ret;
}

int64_t cOmxDevice::GetSTC(void)
{
	return m_omx->GetSTC();
}

void cOmxDevice::Play(void)
{
	dsyslog("rpihddevice: Play()");

	Clear();
	return;

	m_mutex->Lock();

	m_skipAudio = false;
	m_omx->SetClockScale(1.0f);
	m_omx->SetClockReference(m_hasAudio ?
			cOmx::eClockRefAudio : cOmx::eClockRefVideo);

	m_mutex->Unlock();
	cDevice::Play();
}

void cOmxDevice::Freeze(void)
{
	m_mutex->Lock();
	m_omx->SetClockScale(0.0f);
	m_mutex->Unlock();
	cDevice::Freeze();
}

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
		ApplyTrickSpeed(Speed);

	m_mutex->Unlock();
}

void cOmxDevice::ApplyTrickSpeed(int trickSpeed, bool reverse)
{
	float scale =
		// slow forward
		trickSpeed ==  8 ?  0.125f :
		trickSpeed ==  4 ?  0.25f  :
		trickSpeed ==  2 ?  0.5f   :

		// fast for-/backward
		trickSpeed ==  6 ? (reverse ?  -2.0f :  2.0f) :
		trickSpeed ==  3 ? (reverse ?  -4.0f :  4.0f) :
		trickSpeed ==  1 ? (reverse ? -12.0f : 12.0f) :

		// slow backward
		trickSpeed == 63 ? -0.125f :
		trickSpeed == 48 ? -0.25f  :
		trickSpeed == 24 ? -0.5f   : 1.0f;

	m_omx->SetClockScale(scale);
	m_omx->SetClockReference(cOmx::eClockRefVideo);

	m_trickRequest = 0;
	m_skipAudio = true;

	dsyslog("rpihddevice: ApplyTrickSpeed(%.3f, %sward)",
			scale, reverse ? "back" : "for");

}

void cOmxDevice::PtsTracker(int64_t ptsDiff)
{
	if (ptsDiff < 0)
		--m_playDirection;
	else if (ptsDiff > 0)
		m_playDirection += 2;

	if (m_playDirection < -2 || m_playDirection > 3)
		ApplyTrickSpeed(m_trickRequest, m_playDirection < 0);
}

bool cOmxDevice::Flush(int TimeoutMs)
{
	dsyslog("rpihddevice: Flush()");
	return true;
}

void cOmxDevice::Clear(void)
{
	dsyslog("rpihddevice: Clear()");
	m_mutex->Lock();

	ResetAudioVideo();
	m_omx->SetStartTime(0);

	m_mutex->Unlock();
	cDevice::Clear();
}

void cOmxDevice::ResetAudioVideo(bool flushVideoRender)
{
	m_omx->SetClockScale(1.0f);
	m_skipAudio = false;
	m_trickRequest = 0;
	m_audioPts = 0;
	m_videoPts = 0;

	if (m_hasAudio)
	{
		m_audio->Reset();
		m_omx->FlushAudio();
	}

	if (m_hasVideo)
		m_omx->FlushVideo(flushVideoRender);

	m_hasAudio = false;
	m_hasVideo = false;
}


void cOmxDevice::SetVolumeDevice(int Volume)
{
	m_omx->SetVolume(Volume);
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
