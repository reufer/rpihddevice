/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */
//#pragma pack(1)

#include "omxdevice.h"

#include <vdr/remux.h>
#include <vdr/tools.h>

#include <mpg123.h>

#include <string.h>

extern "C"
{
#include "ilclient.h"
}

#include "bcm_host.h"

class cOmx
{

public:

	static const char* errStr(int err)
	{
		return 	err == OMX_ErrorNone                               ? "None"                               :
				err == OMX_ErrorInsufficientResources              ? "InsufficientResources"              :
				err == OMX_ErrorUndefined                          ? "Undefined"                          :
				err == OMX_ErrorInvalidComponentName               ? "InvalidComponentName"               :
				err == OMX_ErrorComponentNotFound                  ? "ComponentNotFound"                  :
				err == OMX_ErrorInvalidComponent                   ? "InvalidComponent"                   :
				err == OMX_ErrorBadParameter                       ? "BadParameter"                       :
				err == OMX_ErrorNotImplemented                     ? "NotImplemented"                     :
				err == OMX_ErrorUnderflow                          ? "Underflow"                          :
				err == OMX_ErrorOverflow                           ? "Overflow"                           :
				err == OMX_ErrorHardware                           ? "Hardware"                           :
				err == OMX_ErrorInvalidState                       ? "InvalidState"                       :
				err == OMX_ErrorStreamCorrupt                      ? "StreamCorrupt"                      :
				err == OMX_ErrorPortsNotCompatible                 ? "PortsNotCompatible"                 :
				err == OMX_ErrorResourcesLost                      ? "ResourcesLost"                      :
				err == OMX_ErrorNoMore                             ? "NoMore"                             :
				err == OMX_ErrorVersionMismatch                    ? "VersionMismatch"                    :
				err == OMX_ErrorNotReady                           ? "NotReady"                           :
				err == OMX_ErrorTimeout                            ? "Timeout"                            :
				err == OMX_ErrorSameState                          ? "SameState"                          :
				err == OMX_ErrorResourcesPreempted                 ? "ResourcesPreempted"                 :
				err == OMX_ErrorPortUnresponsiveDuringAllocation   ? "PortUnresponsiveDuringAllocation"   :
				err == OMX_ErrorPortUnresponsiveDuringDeallocation ? "PortUnresponsiveDuringDeallocation" :
				err == OMX_ErrorPortUnresponsiveDuringStop         ? "PortUnresponsiveDuringStop"         :
				err == OMX_ErrorIncorrectStateTransition           ? "IncorrectStateTransition"           :
				err == OMX_ErrorIncorrectStateOperation            ? "IncorrectStateOperation"            :
				err == OMX_ErrorUnsupportedSetting                 ? "UnsupportedSetting"                 :
				err == OMX_ErrorUnsupportedIndex                   ? "UnsupportedIndex"                   :
				err == OMX_ErrorBadPortIndex                       ? "BadPortIndex"                       :
				err == OMX_ErrorPortUnpopulated                    ? "PortUnpopulated"                    :
				err == OMX_ErrorComponentSuspended                 ? "ComponentSuspended"                 :
				err == OMX_ErrorDynamicResourcesUnavailable        ? "DynamicResourcesUnavailable"        :
				err == OMX_ErrorMbErrorsInFrame                    ? "MbErrorsInFrame"                    :
				err == OMX_ErrorFormatNotDetected                  ? "FormatNotDetected"                  :
				err == OMX_ErrorContentPipeOpenFailed              ? "ContentPipeOpenFailed"              :
				err == OMX_ErrorContentPipeCreationFailed          ? "ContentPipeCreationFailed"          :
				err == OMX_ErrorSeperateTablesUsed                 ? "SeperateTablesUsed"                 :
				err == OMX_ErrorTunnelingUnsupported               ? "TunnelingUnsupported"               :
				err == OMX_ErrorKhronosExtensions                  ? "KhronosExtensions"                  :
				err == OMX_ErrorVendorStartUnused                  ? "VendorStartUnused"                  :
				err == OMX_ErrorDiskFull                           ? "DiskFull"                           :
				err == OMX_ErrorMaxFileSize                        ? "MaxFileSize"                        :
				err == OMX_ErrorDrmUnauthorised                    ? "DrmUnauthorised"                    :
				err == OMX_ErrorDrmExpired                         ? "DrmExpired"                         :
				err == OMX_ErrorDrmGeneral                         ? "DrmGeneral"                         :
				"unknown";
	};

	enum eOmxComponent {
		eClock = 0,
		eVideoDecoder,
		eVideoScheduler,
		eVideoRender,
		eAudioRender,
		eNumComponents
	};

	enum eOmxTunnel {
		eVideoDecoderToVideoScheduler = 0,
		eVideoSchedulerToVideoRender,
		eClockToVideoScheduler,
		eClockToAudioRender,
		eNumTunnels
	};

	ILCLIENT_T 	*client;
	COMPONENT_T	*comp[cOmx::eNumComponents + 1];
	TUNNEL_T 	 tun[cOmx::eNumTunnels + 1];

	static void OmxError(void *omxDevice, COMPONENT_T *comp, unsigned int data)
	{
		if (data != OMX_ErrorSameState)
			esyslog("rpihddevice: OmxError(%s)", errStr((int)data));
	}

	static void OmxBufferEmpty(void *omxDevice, COMPONENT_T *comp)
	{
//		dsyslog("rpihddevice: OmxBufferEmpty()");
	}

	static void OmxPortSettingsChanged(void *omxDevice, COMPONENT_T *comp, unsigned int data)
	{
		cOmxDevice* dev = static_cast <cOmxDevice*> (omxDevice);
		dev->HandlePortSettingsChanged(data);
	}

	static void OmxEndOfStream(void *omxDevice, COMPONENT_T *comp, unsigned int data)
	{
		cOmxDevice* dev = static_cast <cOmxDevice*> (omxDevice);
		dev->HandleEndOfStream(data);
	}

	static void PtsToTicks(uint64_t pts, OMX_TICKS &ticks)
	{
		// ticks = pts * OMX_TICKS_PER_SECOND / PTSTICKS
		pts = pts * 100 / 9;
		ticks.nLowPart = (OMX_U32)pts;
		ticks.nHighPart = (OMX_U32)(pts >> 32);
	}

	static uint64_t TicksToPts(OMX_TICKS &ticks)
	{
		// pts = ticks * PTSTICKS / OMX_TICKS_PER_SECOND
		uint64_t pts = ticks.nHighPart;
		pts = (pts << 32) + ticks.nLowPart;
		pts = pts * 9 / 100;
		return pts;
	}

};

class cAudio
{

public:

	cAudio() :
		sampleRate(0),
		bitDepth(0),
		nChannels(0),
		encoding(0),
		m_handle(0)
	{
		int ret;
		mpg123_init();
		m_handle = mpg123_new(NULL, &ret);
		if (m_handle == NULL)
			esyslog("rpihddevice: failed to create mpg123 handle!");

		if (mpg123_open_feed(m_handle) == MPG123_ERR)
			esyslog("rpihddevice: failed to open mpg123 feed!");

		dsyslog("rpihddevice: new cAudio()");
	}

	~cAudio()
	{
		mpg123_delete(m_handle);
		dsyslog("rpihddevice: delete cAudio()");
	}

	bool writeData(const unsigned char *buf, unsigned int length)
	{
		return (mpg123_feed(m_handle, buf, length) != MPG123_ERR);
	}

	unsigned int readSamples(unsigned char *buf, unsigned length, bool &done)
	{
		unsigned int read = 0;
		done = (mpg123_read(m_handle, buf, length, &read) == MPG123_NEED_MORE);
		mpg123_getformat(m_handle, &sampleRate, &nChannels, &encoding);
		return read;
	}

	long sampleRate;
	int bitDepth;
	int nChannels;
	int encoding;
        
	mpg123_handle *m_handle;
};

cOmxDevice::cOmxDevice(void (*onPrimaryDevice)(void)) :
	cDevice(),
	m_onPrimaryDevice(onPrimaryDevice),
	m_omx(new cOmx()),
	m_audio(new cAudio()),
	m_eosEvent(0),
	m_state(eStop),
	m_firstVideoPacket(false),
	m_firstAudioPacket(false)
{
	bcm_host_init();

	m_eosEvent = new cCondWait();
	m_mutex = new cMutex();
}

cOmxDevice::~cOmxDevice()
{
	OmxDeInit();
	delete m_omx;
	delete m_audio;
	delete m_mutex;
	delete m_eosEvent;
}

bool cOmxDevice::CanReplay(void) const
{
	// video codec de-initialization done
	return (m_state == eStop);
}

bool cOmxDevice::SetPlayMode(ePlayMode PlayMode)
{
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
		Stop();
		break;

	case pmAudioVideo:
		Start(eAudioVideo);
		break;

	case pmAudioOnly:
	case pmAudioOnlyBlack:
		Start(eAudioOnly);
		break;

	case pmVideoOnly:
		Start(eVideoOnly);
		break;
	}

	return true;
}

int cOmxDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
	m_mutex->Lock();

	if (m_state != ePlay)
	{
		m_mutex->Unlock();
		dsyslog("rpihddevice: PlayAudio() not replaying!");
		return 0;
	}

	if (!PesHasLength(Data))
	{
		esyslog("rpihddevice: audio packet dropped!");
		m_mutex->Unlock();
		return Length;
	}

	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

	const uchar *payload = Data + PesPayloadOffset(Data);
	int length = PesLength(Data) - PesPayloadOffset(Data);

	// first packet of a new stream needs valid PTS
	if (m_firstAudioPacket && pts == 0)
	{
		m_mutex->Unlock();
		return Length;
	}

	if (m_firstAudioPacket && !OmxSetAudioCodec(payload))
	{
		m_mutex->Unlock();
		return Length;
	}

	if (!m_audio->writeData(payload, length))
	{
		esyslog("rpihddevice: failed to pass buffer to audio decoder!");
		m_mutex->Unlock();
		return 0;
	}

	bool done = false;
	while (!done)
	{
		OMX_BUFFERHEADERTYPE *buf = ilclient_get_input_buffer(m_omx->comp[cOmx::eAudioRender], 100, 0);
		if (buf == NULL)
		{
			esyslog("rpihddevice: failed to get audio buffer!");
			m_mutex->Unlock();
			return Length;
		}

		// decode audio packet
		buf->nFilledLen = m_audio->readSamples(buf->pBuffer, buf->nAllocLen, done);
		cOmx::PtsToTicks(pts, buf->nTimeStamp);
		buf->nFlags = m_firstAudioPacket ? OMX_BUFFERFLAG_STARTTIME : 0; //OMX_BUFFERFLAG_TIME_UNKNOWN;
		m_firstAudioPacket = false;

//		dsyslog("A: %u.%u - f:%d %lld", buf->nTimeStamp.nHighPart, buf->nTimeStamp.nLowPart, buf->nFlags, pts);

		if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_omx->comp[cOmx::eAudioRender]), buf) != OMX_ErrorNone)
		{
			esyslog("rpihddevice: failed to pass buffer to audio render!");
			break;
		}
	}

	m_mutex->Unlock();
	return Length;
}

int cOmxDevice::PlayVideo(const uchar *Data, int Length)
{
	m_mutex->Lock();

	if (m_state != ePlay)
	{
		m_mutex->Unlock();
		dsyslog("rpihddevice: PlayVideo() not replaying!");
		return 0;
	}

	if (!PesHasLength(Data))
	{
		esyslog("rpihddevice: video packet dropped!");
		m_mutex->Unlock();
		return Length;
	}

	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

	const uchar *payload = Data + PesPayloadOffset(Data);
	int length = PesLength(Data) - PesPayloadOffset(Data);

	// first packet of a new stream needs valid PTS
	if (m_firstVideoPacket && pts == 0)
	{
		esyslog("rpihddevice: PTS missing for first video packet!");
		m_mutex->Unlock();
		return Length;
	}

	if (m_firstVideoPacket && !OmxSetVideoCodec(payload))
	{
		m_mutex->Unlock();
		return Length;
	}

	OMX_BUFFERHEADERTYPE *buf = ilclient_get_input_buffer(m_omx->comp[cOmx::eVideoDecoder], 130, 0);
	if (buf == NULL)
	{
		esyslog("rpihddevice: failed to get video buffer!");
		m_mutex->Unlock();
		return Length;
	}

	cOmx::PtsToTicks(pts, buf->nTimeStamp);
	buf->nFlags = m_firstVideoPacket ? OMX_BUFFERFLAG_STARTTIME : 0; //OMX_BUFFERFLAG_TIME_UNKNOWN;

	if (length <= buf->nAllocLen)
	{
		memcpy(buf->pBuffer, payload, length);
		buf->nFilledLen = length;
	}
	else
		esyslog("rpihddevice: video packet too long for video buffer!");

//	dsyslog("V: %u.%u -          f:%d %lld", buf->nTimeStamp.nHighPart, buf->nTimeStamp.nLowPart, buf->nFlags, pts);
//	dsyslog("rpihddevice: PlayVideo(%u.%u, %02x %02x %02x %02x %02x %02x %02x %02x, %d)", buf->nTimeStamp.nHighPart, buf->nTimeStamp.nLowPart,
//		buf->pBuffer[0], buf->pBuffer[1], buf->pBuffer[2], buf->pBuffer[3],
//		buf->pBuffer[4], buf->pBuffer[5], buf->pBuffer[6], buf->pBuffer[7], buf->nFilledLen);

	if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_omx->comp[cOmx::eVideoDecoder]), buf) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to pass buffer to video decoder!");

	m_firstVideoPacket = false;

	m_mutex->Unlock();
	return Length;
}

int64_t cOmxDevice::GetSTC(void)
{
	int64_t stc = -1;

	OMX_TIME_CONFIG_TIMESTAMPTYPE timestamp;
	memset(&timestamp, 0, sizeof(timestamp));
	timestamp.nSize = sizeof(timestamp);
	timestamp.nPortIndex = OMX_ALL;
	timestamp.nVersion.nVersion = OMX_VERSION;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
		OMX_IndexConfigTimeCurrentMediaTime, &timestamp) != OMX_ErrorNone)
		esyslog("rpihddevice: failed get current clock reference!");
	else
		stc = cOmx::TicksToPts(timestamp.nTimestamp);

	dsyslog("-: %d.%d - %llu", timestamp.nTimestamp.nHighPart, timestamp.nTimestamp.nLowPart, stc);

	return stc;
}

void cOmxDevice::Play(void)
{
	dsyslog("rpihddevice: Play()");
	SetClockScale(1.0f);
}

void cOmxDevice::Freeze(void)
{
	dsyslog("rpihddevice: Freeze()");
	SetClockScale(0.0f);
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
	dsyslog("rpihddevice: Clear()");
	cDevice::Clear();
}

void cOmxDevice::SetVolumeDevice(int Volume)
{
	dsyslog("rpihddevice: SetVolumeDevice(%d)", Volume);

	OMX_AUDIO_CONFIG_VOLUMETYPE volume;
	memset(&volume, 0, sizeof(volume));
	volume.nSize = sizeof(volume);
	volume.nVersion.nVersion = OMX_VERSION;
	volume.nPortIndex = 100;
	volume.bLinear = OMX_TRUE;
	volume.sVolume.nValue = Volume * 100 / 255;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eAudioRender]),
			OMX_IndexConfigAudioVolume, &volume) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set volume!");
}

bool cOmxDevice::Poll(cPoller &Poller, int TimeoutMs)
{
	dsyslog("rpihddevice: Poll()");

	return false;
}

void cOmxDevice::MakePrimaryDevice(bool On)
{
	if (On && m_onPrimaryDevice)
		m_onPrimaryDevice();
	cDevice::MakePrimaryDevice(On);
}

void cOmxDevice::HandleEndOfStream(unsigned int portId)
{
	dsyslog("rpihddevice: HandleEndOfStream(%d)", portId);

	switch (portId)
	{
	case 131:
		break;

	case 11:
		break;

	case 90:
		m_eosEvent->Signal();
		break;
	}
}

void cOmxDevice::HandlePortSettingsChanged(unsigned int portId)
{
	dsyslog("rpihddevice: HandlePortSettingsChanged(%d)", portId);

	switch (portId)
	{
	case 131:
		if (m_state == ePlay)
		{
			if (ilclient_setup_tunnel(&m_omx->tun[cOmx::eVideoDecoderToVideoScheduler], 0, 0) != 0)
				esyslog("rpihddevice: failed to setup up tunnel from video decoder to video scheduler!");
			if (ilclient_change_component_state(m_omx->comp[cOmx::eVideoScheduler], OMX_StateExecuting) != 0)
				esyslog("rpihddevice: failed to enable video scheduler!");
		}
		else
			esyslog("HandlePortSettingsChanged: a");
		break;

	case 11:
		if (m_state == ePlay)
		{
			if (ilclient_setup_tunnel(&m_omx->tun[cOmx::eVideoSchedulerToVideoRender], 0, 1000) != 0)
				esyslog("rpihddevice: failed to setup up tunnel from scheduler to render!");
			if (ilclient_change_component_state(m_omx->comp[cOmx::eVideoRender], OMX_StateExecuting) != 0)
				esyslog("rpihddevice: failed to enable video render!");
		}
		else
			esyslog("HandlePortSettingsChanged: b");
		break;
	}
}

bool cOmxDevice::IsClockRunning()
{
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	memset(&cstate, 0, sizeof(cstate));
	cstate.nSize = sizeof(cstate);
	cstate.nVersion.nVersion = OMX_VERSION;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		esyslog("rpihddevice: failed get clock state!");

	if (cstate.eState == OMX_TIME_ClockStateRunning)
		return true;
	else
		return false;
}

void cOmxDevice::SetClockState(eClockState clockState, bool armVideo, bool armAudio)
{
	dsyslog("rpihddevice: SetClockState(%s, %s, %s)",
		clockState == eClockStateRunning ? "eClockStateRunning" :
		clockState == eClockStateStopped ? "eClockStateStopped" :
		clockState == eClockStateWaiting ? "eClockStateWaiting" : "unknown",
		armVideo ? "true" : "false", armAudio ? "true" : "false");

	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	memset(&cstate, 0, sizeof(cstate));
	cstate.nSize = sizeof(cstate);
	cstate.nVersion.nVersion = OMX_VERSION;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		esyslog("rpihddevice: failed get clock state!");

	if ((clockState == eClockStateWaiting) && (cstate.eState == OMX_TIME_ClockStateRunning))
	{
		esyslog("rpihddevice: need to disable clock first!");
		cstate.eState = OMX_TIME_ClockStateStopped;
		if (OMX_SetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
				OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
			esyslog("rpihddevice: failed set clock state!");
	}

	switch (clockState)
	{
	case eClockStateRunning:
		cstate.eState = OMX_TIME_ClockStateRunning;
		break;

	case eClockStateStopped:
		cstate.eState = OMX_TIME_ClockStateStopped;
		break;

	case eClockStateWaiting:
		cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
		cstate.nWaitMask = (armVideo ? OMX_CLOCKPORT0 : 0) | (armAudio ? OMX_CLOCKPORT1 : 0);
		break;
	}

	esyslog("rpihddevice: wait mask: %d", cstate.nWaitMask);

	if (OMX_SetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		esyslog("rpihddevice: failed set clock state!");
}

void cOmxDevice::SetClockScale(float scale)
{
	OMX_TIME_CONFIG_SCALETYPE scaleType;
	memset(&scaleType, 0, sizeof(scaleType));
	scaleType.xScale = floor(scale * pow(2, 16));
	if (OMX_SetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
			OMX_IndexConfigTimeScale, &scaleType) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set clock scale (%d)!", scaleType.xScale);
	else
		dsyslog("rpihddevice: set clock scale to %.2f (%d)", scale, scaleType.xScale);
}

int cOmxDevice::OmxInit()
{
	dsyslog("OmxInit()");

	m_omx->client = ilclient_init();
	if (m_omx->client == NULL)
		esyslog("rpihddevice: ilclient_init() failed!");

	if (OMX_Init() != OMX_ErrorNone)
		esyslog("rpihddevice: OMX_Init() failed!");

	ilclient_set_error_callback(m_omx->client, cOmx::OmxError, this);
	ilclient_set_empty_buffer_done_callback(m_omx->client, cOmx::OmxBufferEmpty, this);
	ilclient_set_port_settings_callback(m_omx->client, cOmx::OmxPortSettingsChanged, this);
	ilclient_set_eos_callback(m_omx->client, cOmx::OmxEndOfStream, this);

	// create video_decode
	if (ilclient_create_component(m_omx->client, &m_omx->comp[cOmx::eVideoDecoder],
		"video_decode",	(ILCLIENT_CREATE_FLAGS_T)
		(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0)
		esyslog("rpihddevice: failed creating video decoder!");

	// create video_render
	if (ilclient_create_component(m_omx->client, &m_omx->comp[cOmx::eVideoRender],
		"video_render",	ILCLIENT_DISABLE_ALL_PORTS) != 0)
		esyslog("rpihddevice: failed creating video render!");

	//create clock
	if (ilclient_create_component(m_omx->client, &m_omx->comp[cOmx::eClock],
		"clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		esyslog("rpihddevice: failed creating clock!");

	// create audio_render
	if (ilclient_create_component(m_omx->client, &m_omx->comp[cOmx::eAudioRender],
		"audio_render",	(ILCLIENT_CREATE_FLAGS_T)
		(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0)
		esyslog("rpihddevice: failed creating audio render!");

	//create video_scheduler
	if (ilclient_create_component(m_omx->client, &m_omx->comp[cOmx::eVideoScheduler],
		"video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		esyslog("rpihddevice: failed creating video scheduler!");

	// setup tunnels
	set_tunnel(&m_omx->tun[cOmx::eVideoDecoderToVideoScheduler],
		m_omx->comp[cOmx::eVideoDecoder], 131, m_omx->comp[cOmx::eVideoScheduler], 10);

	set_tunnel(&m_omx->tun[cOmx::eVideoSchedulerToVideoRender],
		m_omx->comp[cOmx::eVideoScheduler], 11, m_omx->comp[cOmx::eVideoRender], 90);

	set_tunnel(&m_omx->tun[cOmx::eClockToVideoScheduler],
		m_omx->comp[cOmx::eClock], 80, m_omx->comp[cOmx::eVideoScheduler], 12);

	set_tunnel(&m_omx->tun[cOmx::eClockToAudioRender],
		m_omx->comp[cOmx::eClock], 81, m_omx->comp[cOmx::eAudioRender], 101);

	// setup clock tunnels first
	if (ilclient_setup_tunnel(&m_omx->tun[cOmx::eClockToVideoScheduler], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

	if (ilclient_setup_tunnel(&m_omx->tun[cOmx::eClockToAudioRender], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to audio render!");
	
	OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refclock;
	memset(&refclock, 0, sizeof(refclock));
	refclock.nSize = sizeof(refclock);
	refclock.nVersion.nVersion = OMX_VERSION;
	refclock.eClock = OMX_TIME_RefClockAudio;
//	refclock.eClock = OMX_TIME_RefClockVideo;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eClock]),
			OMX_IndexConfigTimeActiveRefClock, &refclock) != OMX_ErrorNone)
		esyslog("rpihddevice: failed set active clock reference!");

	ilclient_change_component_state(m_omx->comp[cOmx::eClock], OMX_StateExecuting);
	ilclient_change_component_state(m_omx->comp[cOmx::eVideoDecoder], OMX_StateIdle);

	// set up the number and size of buffers for audio render
	OMX_PARAM_PORTDEFINITIONTYPE param;
	memset(&param, 0, sizeof(param));
	param.nSize = sizeof(param);
	param.nVersion.nVersion = OMX_VERSION;
	param.nPortIndex = 100;		
	if (OMX_GetParameter(ILC_GET_HANDLE(m_omx->comp[cOmx::eAudioRender]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to get audio render port parameters!");
	param.nBufferSize = 65536;
	param.nBufferCountActual = 64;
	if (OMX_SetParameter(ILC_GET_HANDLE(m_omx->comp[cOmx::eAudioRender]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set audio render port parameters!");

	// configure audio render
	OMX_AUDIO_PARAM_PCMMODETYPE pcmMode;
	memset(&pcmMode, 0, sizeof(pcmMode));
	pcmMode.nSize = sizeof(pcmMode);
	pcmMode.nVersion.nVersion = OMX_VERSION;
	pcmMode.nPortIndex = 100;
	pcmMode.nChannels = 2;
	pcmMode.eNumData = OMX_NumericalDataSigned;
	pcmMode.eEndian = OMX_EndianLittle;
	pcmMode.bInterleaved = OMX_TRUE;
	pcmMode.nBitPerSample = 16;
	pcmMode.nSamplingRate = 48000;
	pcmMode.ePCMMode = OMX_AUDIO_PCMModeLinear;
	pcmMode.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
	pcmMode.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
	
	if (OMX_SetParameter(ILC_GET_HANDLE(m_omx->comp[cOmx::eAudioRender]),
			OMX_IndexParamAudioPcm, &pcmMode) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set audio render parameters!");

	OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
	memset(&audioDest, 0, sizeof(audioDest));
	audioDest.nSize = sizeof(audioDest);
	audioDest.nVersion.nVersion = OMX_VERSION;
	strcpy((char *)audioDest.sName, "local");

	if (OMX_SetConfig(ILC_GET_HANDLE(m_omx->comp[cOmx::eAudioRender]),
			OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set audio destination!");

//	if (ilclient_enable_port_buffers(m_omx->comp[cOmx::eAudioRender], 100, NULL, NULL, NULL) != 0)
//		esyslog("rpihddevice: failed to enable port buffer on audio render!");

	return 0;
}

void cOmxDevice::Start(eMode mode)
{
	dsyslog("rpihddevice: Start()");
	m_mutex->Lock();

	m_state = ePlay;
	m_firstVideoPacket = true;
	m_firstAudioPacket = true;

	SetClockState(eClockStateWaiting,
			(mode == eAudioVideo || mode == eVideoOnly),
			(mode == eAudioVideo || mode == eAudioOnly));

	m_mutex->Unlock();
	dsyslog("rpihddevice: Start() end");
}

void cOmxDevice::Stop()
{
	dsyslog("rpihddevice: Stop()");
	m_mutex->Lock();

	m_state = eStop;

	// really necessary?
	if (false && IsClockRunning())
	{
		OMX_BUFFERHEADERTYPE *buf = ilclient_get_input_buffer(m_omx->comp[cOmx::eVideoDecoder], 130, 1);
		if (buf == NULL)
			return;

		buf->nFilledLen = 0;
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

		if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_omx->comp[cOmx::eVideoDecoder]), buf) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to send empty packet to video decoder!");

		if (!m_eosEvent->Wait(10000))
			esyslog("rpihddevice: time out waiting for EOS event!");
	}

	// need to flush the renderer to allow video_decode to disable its input port

	// put video decoder into idle
	ilclient_change_component_state(m_omx->comp[cOmx::eVideoDecoder], OMX_StateIdle);

	// put video scheduler into idle
	ilclient_flush_tunnels(&m_omx->tun[cOmx::eVideoDecoderToVideoScheduler], 1);
	ilclient_disable_tunnel(&m_omx->tun[cOmx::eVideoDecoderToVideoScheduler]);
	ilclient_flush_tunnels(&m_omx->tun[cOmx::eClockToVideoScheduler], 1);
	ilclient_disable_tunnel(&m_omx->tun[cOmx::eClockToVideoScheduler]);
	ilclient_change_component_state(m_omx->comp[cOmx::eVideoScheduler], OMX_StateIdle);

	// put video render into idle
	ilclient_flush_tunnels(&m_omx->tun[cOmx::eVideoSchedulerToVideoRender], 1);
	ilclient_disable_tunnel(&m_omx->tun[cOmx::eVideoSchedulerToVideoRender]);
	ilclient_change_component_state(m_omx->comp[cOmx::eVideoRender], OMX_StateIdle);

	// put audio render onto idle
	ilclient_flush_tunnels(&m_omx->tun[cOmx::eClockToAudioRender], 1);
	ilclient_disable_tunnel(&m_omx->tun[cOmx::eClockToAudioRender]);
	ilclient_change_component_state(m_omx->comp[cOmx::eAudioRender], OMX_StateIdle);

	// disable port buffers and allow video decoder and audio render to reconfig
	ilclient_disable_port_buffers(m_omx->comp[cOmx::eVideoDecoder], 130, NULL, NULL, NULL);
	ilclient_disable_port_buffers(m_omx->comp[cOmx::eAudioRender], 100, NULL, NULL, NULL);

	SetClockState(eClockStateStopped);

	m_mutex->Unlock();
	dsyslog("rpihddevice: Stop() end");
}

bool cOmxDevice::OmxSetVideoCodec(const uchar *data)
{
	// configure video decoder
	OMX_VIDEO_PARAM_PORTFORMATTYPE videoFormat;
	memset(&videoFormat, 0, sizeof(videoFormat));
	videoFormat.nSize = sizeof(videoFormat);
	videoFormat.nVersion.nVersion = OMX_VERSION;
	videoFormat.nPortIndex = 130;

	if (data[0] == 0x00 && data[1] == 0x00 &&
		data[2] == 0x01 && data[3] == 0xb3)
	{
		dsyslog("rpihddevice: set video codec to MPEG2");
		videoFormat.eCompressionFormat = OMX_VIDEO_CodingMPEG2;
	}

	else if (data[0] == 0x00 && data[1] == 0x00 &&
		data[2] == 0x00 && data[3] == 0x01 && data[4] == 0x09 && data[5] == 0x10)
	{
		dsyslog("rpihddevice: set video codec to H264");
		videoFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
	}
	else
	{
		esyslog("rpihddevice: wrong start sequence: %02x %02x %02x %02x %02x %02x %02x %02x",
				data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
		return false;
	}

	if (OMX_SetParameter(ILC_GET_HANDLE(m_omx->comp[cOmx::eVideoDecoder]),
			OMX_IndexParamVideoPortFormat, &videoFormat) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decoder parameters!");

	OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ectype;
	memset (&ectype, 0, sizeof(ectype));
	ectype.nSize = sizeof(ectype);
	ectype.nVersion.nVersion = OMX_VERSION;
	ectype.bStartWithValidFrame = OMX_FALSE;
	if (OMX_SetParameter(ILC_GET_HANDLE(m_omx->comp[cOmx::eVideoDecoder]),
			OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ectype) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decode error concealment failed\n");

	if (ilclient_enable_port_buffers(m_omx->comp[cOmx::eVideoDecoder], 130, NULL, NULL, NULL) != 0)
		esyslog("rpihddevice: failed to enable port buffer on video decoder!");

	if (ilclient_change_component_state(m_omx->comp[cOmx::eVideoDecoder], OMX_StateExecuting) != 0)
		esyslog("rpihddevice: failed to set video decoder to executing state!");

	// setup clock tunnels first
	if (ilclient_setup_tunnel(&m_omx->tun[cOmx::eClockToVideoScheduler], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

	return true;
}

bool cOmxDevice::OmxSetAudioCodec(const uchar *data)
{
	if (ilclient_enable_port_buffers(m_omx->comp[cOmx::eAudioRender], 100, NULL, NULL, NULL) != 0)
		esyslog("rpihddevice: failed to enable port buffer on audio render!");

	ilclient_change_component_state(m_omx->comp[cOmx::eAudioRender], OMX_StateExecuting);

	if (ilclient_setup_tunnel(&m_omx->tun[cOmx::eClockToAudioRender], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

	return true;
}

int cOmxDevice::OmxDeInit()
{
	dsyslog("rpihddevice: OmxDeInit()");

	// need to flush the renderer to allow video_decode to disable its input port
//	ilclient_flush_tunnels(m_omx->tun, 0);

//	ilclient_disable_tunnel(&m_omx->tun[cOmx::eVideoDecoderToVideoScheduler]);
//	ilclient_disable_tunnel(&m_omx->tun[cOmx::eVideoSchedulerToVideoRender]);
//	ilclient_disable_tunnel(&m_omx->tun[cOmx::eClockToVideoScheduler]);
//	ilclient_disable_tunnel(&m_omx->tun[cOmx::eClockToAudioRender]);

	ilclient_teardown_tunnels(m_omx->tun);

	ilclient_state_transition(m_omx->comp, OMX_StateIdle);
	ilclient_state_transition(m_omx->comp, OMX_StateLoaded);

	ilclient_cleanup_components(m_omx->comp);

	OMX_Deinit();
	ilclient_destroy(m_omx->client);

	return 0;
}

void cOmxDevice::GetDisplaySize(int &width, int &height, double &aspect)
{
	uint32_t screenWidth;
	uint32_t screenHeight;

	if (graphics_get_display_size(0 /* LCD */, &screenWidth, &screenHeight) < 0)
			esyslog("rpihddevice: failed to get display size!");
	else
	{
		width = (int)screenWidth;
		height = (int)screenHeight;
		aspect = 1;
	}
}



