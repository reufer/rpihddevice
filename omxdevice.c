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

#include <string.h>

cOmxDevice::cOmxDevice(void (*onPrimaryDevice)(void)) :
	cDevice(),
	m_onPrimaryDevice(onPrimaryDevice),
	m_omx(new cOmx()),
	m_audio(new cAudioDecoder(m_omx)),
	m_mutex(new cMutex()),
	m_state(eNone),
	m_audioId(0)
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

bool cOmxDevice::CanReplay(void) const
{
	dsyslog("rpihddevice: CanReplay");
	// video codec de-initialization done
	return (m_state == eNone);
}

bool cOmxDevice::SetPlayMode(ePlayMode PlayMode)
{
	// in case we were in some trick mode
	m_omx->SetClockScale(1.0f);

	dsyslog("rpihddevice: SetPlayMode(%s)",
		PlayMode == pmNone			 ? "none" 			   :
		PlayMode == pmAudioVideo	 ? "Audio/Video" 	   :
		PlayMode == pmAudioOnly		 ? "Audio only" 	   :
		PlayMode == pmAudioOnlyBlack ? "Audio only, black" :
		PlayMode == pmVideoOnly		 ? "Video only" 	   : 
									   "unsupported");
	switch (PlayMode)
	{
	case pmNone:
		m_mutex->Lock();
		if (HasVideo())
			m_omx->FlushVideo(true);
		if (HasAudio())
		{
			m_audio->Reset();
			m_omx->FlushAudio();
		}
		m_omx->Stop();
		SetState(eNone);
		m_mutex->Unlock();
		break;

	case pmAudioVideo:
	case pmAudioOnly:
	case pmAudioOnlyBlack:
	case pmVideoOnly:
		break;
	}

	return true;
}

int cOmxDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
	m_mutex->Lock();
	int ret = Length;

	// if first packet is audio, we assume audio only
	if (State() == eNone)
		SetState(eAudioOnly);

	if (State() == eAudioOnly || State() == eAudioVideo)
	{
		if (m_audio->Poll())
		{
			if (m_audioId != Id)
			{
				m_audioId = Id;
				m_audio->Reset();
			}

			//dsyslog("A %llu", PesGetPts(Data));
			if (!m_audio->WriteData(Data + PesPayloadOffset(Data),
					PesLength(Data) - PesPayloadOffset(Data),
					PesHasPts(Data) ? PesGetPts(Data) : 0))
				esyslog("rpihddevice: failed to write data to audio decoder!");
		}
		else
			ret = 0;
	}

	m_mutex->Unlock();
	return ret;
}

int cOmxDevice::PlayVideo(const uchar *Data, int Length)
{
	m_mutex->Lock();
	int ret = Length;

	if (State() == eNone)
		SetState(eStartingVideo);

	if (State() == eStartingVideo)
	{
		cVideoCodec::eCodec codec = ParseVideoCodec(Data, Length);
		if (codec != cVideoCodec::eInvalid)
		{
			if (cRpiSetup::IsVideoCodecSupported(codec))
			{
				m_omx->SetVideoCodec(codec);
				SetState(eAudioVideo);
				dsyslog("rpihddevice: set video codec to %s",
						cVideoCodec::Str(codec));
			}
			else
			{
				SetState(eAudioOnly);
				esyslog("rpihddevice: %s video codec not supported!",
						cVideoCodec::Str(codec));
			}
		}
	}

	if (State() == eVideoOnly || State() == eAudioVideo)
	{
		int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;
		OMX_BUFFERHEADERTYPE *buf = m_omx->GetVideoBuffer(pts);
		if (buf)
		{
			//dsyslog("V %llu", PesGetPts(Data));
			const uchar *payload = Data + PesPayloadOffset(Data);
			int length = PesLength(Data) - PesPayloadOffset(Data);
			if (length <= buf->nAllocLen)
			{
				memcpy(buf->pBuffer, payload, length);
				buf->nFilledLen = length;
			}
			else
				esyslog("rpihddevice: video packet too long for video buffer!");

			if (!m_omx->EmptyVideoBuffer(buf))
			{
				ret = 0;
				esyslog("rpihddevice: failed to pass buffer to video decoder!");
			}
		}
	}

	m_mutex->Unlock();
	return ret;
}

void cOmxDevice::SetState(eState state)
{
	switch (state)
	{
	case eNone:
		m_omx->SetClockState(cOmx::eClockStateStop);
		break;

	case eStartingVideo:
		break;

	case eAudioOnly:
		m_omx->SetClockState(cOmx::eClockStateWaitForAudio);
		break;

	case eVideoOnly:
		m_omx->SetClockState(cOmx::eClockStateWaitForVideo);
		break;

	case eAudioVideo:
		m_omx->SetClockState(cOmx::eClockStateWaitForAudioVideo);
		break;
	}
	m_state = state;
}

int64_t cOmxDevice::GetSTC(void)
{
	//dsyslog("S %llu", m_omx->GetSTC());
	return m_omx->GetSTC();
}

void cOmxDevice::Play(void)
{
	dsyslog("rpihddevice: Play()");
	m_omx->SetClockScale(1.0f);
	cDevice::Play();
}

void cOmxDevice::Freeze(void)
{
	dsyslog("rpihddevice: Freeze()");
	m_omx->SetClockScale(0.0f);
	cDevice::Freeze();
}

void cOmxDevice::TrickSpeed(int Speed)
{
	dsyslog("rpihddevice: TrickSpeed(%d)", Speed);
}

bool cOmxDevice::Flush(int TimeoutMs)
{
	dsyslog("rpihddevice: Flush()");

	return true;
}

void cOmxDevice::Clear(void)
{
	m_mutex->Lock();
	dsyslog("rpihddevice: Clear()");

	m_omx->SetClockScale(1.0f);
	m_omx->SetMediaTime(0);

	if (HasAudio())
	{
		m_audio->Reset();
		m_omx->FlushAudio();
	}

	if (HasVideo())
		m_omx->FlushVideo(false);

	m_mutex->Unlock();
	cDevice::Clear();
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
		if (time.Elapsed() >= TimeoutMs)
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

			else if (p[i + 3] == 0x09)	// slice
			{
				switch (p[i + 4] >> 5)
				{
				case 0: case 3: case 5: // I frame
					return cVideoCodec::eH264;

				case 1: case 4: case 6: // P frame
				case 2: case 7:         // B frame
				default:
					return cVideoCodec::eInvalid;
				}
			}
			return cVideoCodec::eInvalid;
		}
	}
	return cVideoCodec::eInvalid;
}
