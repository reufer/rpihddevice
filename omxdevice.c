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

private:

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

	// to do: make this private!

	ILCLIENT_T 	*m_client;
	COMPONENT_T	*m_comp[cOmx::eNumComponents + 1];
	TUNNEL_T 	 m_tun[cOmx::eNumTunnels + 1];

	cMutex	*m_mutex;

	bool m_firstVideoBuffer;
	bool m_firstAudioBuffer;

	int m_freeAudioBuffers;
	int m_freeVideoBuffers;

	void HandleEndOfStream(unsigned int portId)
	{
		dsyslog("rpihddevice: HandleEndOfStream(%d)", portId);

		switch (portId)
		{
		case 131:
			break;

		case 11:
			break;

		case 90:
			break;
		}
	}

	void HandlePortSettingsChanged(unsigned int portId)
	{
		dsyslog("rpihddevice: HandlePortSettingsChanged(%d)", portId);

		switch (portId)
		{
		case 131:
			if (ilclient_setup_tunnel(&m_tun[eVideoDecoderToVideoScheduler], 0, 0) != 0)
				esyslog("rpihddevice: failed to setup up tunnel from video decoder to scheduler!");
			if (ilclient_change_component_state(m_comp[eVideoScheduler], OMX_StateExecuting) != 0)
				esyslog("rpihddevice: failed to enable video scheduler!");
			break;

		case 11:
			if (ilclient_setup_tunnel(&m_tun[eVideoSchedulerToVideoRender], 0, 1000) != 0)
				esyslog("rpihddevice: failed to setup up tunnel from scheduler to render!");
			if (ilclient_change_component_state(m_comp[eVideoRender], OMX_StateExecuting) != 0)
				esyslog("rpihddevice: failed to enable video render!");
			break;
		}
	}

	void HandleBufferEmpty(COMPONENT_T *comp)
	{
		if (comp == m_comp[eVideoDecoder])
		{
			m_mutex->Lock();
			m_freeVideoBuffers++;
			m_mutex->Unlock();
		}
		else if (comp == m_comp[eAudioRender])
		{
			m_mutex->Lock();
			m_freeAudioBuffers++;
			m_mutex->Unlock();
		}
	}

	static void OnBufferEmpty(void *instance, COMPONENT_T *comp)
	{
		cOmx* omx = static_cast <cOmx*> (instance);
		omx->HandleBufferEmpty(comp);
	}

	static void OnPortSettingsChanged(void *instance, COMPONENT_T *comp, unsigned int data)
	{
		cOmx* omx = static_cast <cOmx*> (instance);
		omx->HandlePortSettingsChanged(data);
	}

	static void OnEndOfStream(void *instance, COMPONENT_T *comp, unsigned int data)
	{
		cOmx* omx = static_cast <cOmx*> (instance);
		omx->HandleEndOfStream(data);
	}

	static void OnError(void *instance, COMPONENT_T *comp, unsigned int data)
	{
		if (data != OMX_ErrorSameState)
			esyslog("rpihddevice: OmxError(%s)", errStr((int)data));
	}

public:

	cOmx() :
		m_mutex(new cMutex()),
		m_firstVideoBuffer(true),
		m_firstAudioBuffer(true),
		m_freeAudioBuffers(0),
		m_freeVideoBuffers(0)
	{ }

	virtual ~cOmx()
	{
		delete m_mutex;
	}

	int Init(void)
	{
		m_client = ilclient_init();
		if (m_client == NULL)
			esyslog("rpihddevice: ilclient_init() failed!");

		if (OMX_Init() != OMX_ErrorNone)
			esyslog("rpihddevice: OMX_Init() failed!");

		ilclient_set_error_callback(m_client, OnError, this);
		ilclient_set_empty_buffer_done_callback(m_client, OnBufferEmpty, this);
		ilclient_set_port_settings_callback(m_client, OnPortSettingsChanged, this);
		ilclient_set_eos_callback(m_client, OnEndOfStream, this);

		// create video_decode
		if (ilclient_create_component(m_client, &m_comp[eVideoDecoder],
			"video_decode",	(ILCLIENT_CREATE_FLAGS_T)
			(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0)
			esyslog("rpihddevice: failed creating video decoder!");

		// create video_render
		if (ilclient_create_component(m_client, &m_comp[eVideoRender],
			"video_render",	ILCLIENT_DISABLE_ALL_PORTS) != 0)
			esyslog("rpihddevice: failed creating video render!");

		//create clock
		if (ilclient_create_component(m_client, &m_comp[eClock],
			"clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
			esyslog("rpihddevice: failed creating clock!");

		// create audio_render
		if (ilclient_create_component(m_client, &m_comp[eAudioRender],
			"audio_render",	(ILCLIENT_CREATE_FLAGS_T)
			(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0)
			esyslog("rpihddevice: failed creating audio render!");

		//create video_scheduler
		if (ilclient_create_component(m_client, &m_comp[eVideoScheduler],
			"video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
			esyslog("rpihddevice: failed creating video scheduler!");

		// setup tunnels
		set_tunnel(&m_tun[eVideoDecoderToVideoScheduler],	m_comp[eVideoDecoder], 131, m_comp[eVideoScheduler], 10);
		set_tunnel(&m_tun[eVideoSchedulerToVideoRender], m_comp[eVideoScheduler], 11, m_comp[eVideoRender], 90);
		set_tunnel(&m_tun[eClockToVideoScheduler], m_comp[eClock], 80, m_comp[eVideoScheduler], 12);
		set_tunnel(&m_tun[eClockToAudioRender], m_comp[eClock], 81, m_comp[eAudioRender], 101);

		// setup clock tunnels first
		if (ilclient_setup_tunnel(&m_tun[eClockToVideoScheduler], 0, 0) != 0)
			esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

		if (ilclient_setup_tunnel(&m_tun[eClockToAudioRender], 0, 0) != 0)
			esyslog("rpihddevice: failed to setup up tunnel from clock to audio render!");

		OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refclock;
		memset(&refclock, 0, sizeof(refclock));
		refclock.nSize = sizeof(refclock);
		refclock.nVersion.nVersion = OMX_VERSION;
		refclock.eClock = OMX_TIME_RefClockAudio;
	//	refclock.eClock = OMX_TIME_RefClockVideo;

		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeActiveRefClock, &refclock) != OMX_ErrorNone)
			esyslog("rpihddevice: failed set active clock reference!");

		ilclient_change_component_state(m_comp[eClock], OMX_StateExecuting);
		ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle);

		// set up the number and size of buffers for audio render
		m_freeAudioBuffers = 64;
		OMX_PARAM_PORTDEFINITIONTYPE param;
		memset(&param, 0, sizeof(param));
		param.nSize = sizeof(param);
		param.nVersion.nVersion = OMX_VERSION;
		param.nPortIndex = 100;
		if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to get audio render port parameters!");
		param.nBufferSize = 65536;
		param.nBufferCountActual = m_freeAudioBuffers;
		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render port parameters!");

		memset(&param, 0, sizeof(param));
		param.nSize = sizeof(param);
		param.nVersion.nVersion = OMX_VERSION;
		param.nPortIndex = 130;
		if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
				OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to get video decoder port parameters!");

		m_freeVideoBuffers = param.nBufferCountActual;
		dsyslog("rpihddevice: started with %d video and %d audio buffers",
				m_freeVideoBuffers, m_freeAudioBuffers);

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

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioPcm, &pcmMode) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render parameters!");

		OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
		memset(&audioDest, 0, sizeof(audioDest));
		audioDest.nSize = sizeof(audioDest);
		audioDest.nVersion.nVersion = OMX_VERSION;
		strcpy((char *)audioDest.sName, "local");

		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio destination!");

/*		// configure video decoder stall callback
		OMX_CONFIG_BUFFERSTALLTYPE stallConf;
		memset(&stallConf, 0, sizeof(stallConf));
		stallConf.nSize = sizeof(stallConf);
		stallConf.nVersion.nVersion = OMX_VERSION;
		stallConf.nPortIndex = 131;
		stallConf.nDelay = 1500 * 1000;
		if (OMX_SetConfig(m_comp[eVideoDecoder], OMX_IndexConfigBufferStall,
				&stallConf) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set video decoder stall config!");

		OMX_CONFIG_REQUESTCALLBACKTYPE reqCallback;
		memset(&reqCallback, 0, sizeof(reqCallback));
		reqCallback.nSize = sizeof(reqCallback);
		reqCallback.nVersion.nVersion = OMX_VERSION;
		reqCallback.nPortIndex = 131;
		reqCallback.nIndex = OMX_IndexConfigBufferStall;
		reqCallback.bEnable = OMX_TRUE;
		if (OMX_SetConfig(m_comp[eVideoDecoder], OMX_IndexConfigRequestCallback,
				&reqCallback) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set video decoder stall callback!");
*/
	//	if (ilclient_enable_port_buffers(comp[eAudioRender], 100, NULL, NULL, NULL) != 0)
	//		esyslog("rpihddevice: failed to enable port buffer on audio render!");

		return 0;
	}

	int DeInit(void)
	{
		ilclient_teardown_tunnels(m_tun);
		ilclient_state_transition(m_comp, OMX_StateIdle);
		ilclient_state_transition(m_comp, OMX_StateLoaded);
		ilclient_cleanup_components(m_comp);

		OMX_Deinit();
		ilclient_destroy(m_client);

		return 0;
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

	int64_t GetSTC(void)
	{
		int64_t stc = -1;

		OMX_TIME_CONFIG_TIMESTAMPTYPE timestamp;
		memset(&timestamp, 0, sizeof(timestamp));
		timestamp.nSize = sizeof(timestamp);
		timestamp.nPortIndex = OMX_ALL;
		timestamp.nVersion.nVersion = OMX_VERSION;

		if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeCurrentMediaTime, &timestamp) != OMX_ErrorNone)
			esyslog("rpihddevice: failed get current clock reference!");
		else
			stc = TicksToPts(timestamp.nTimestamp);

		return stc;
	}

	bool IsClockRunning(void)
	{
		OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
		memset(&cstate, 0, sizeof(cstate));
		cstate.nSize = sizeof(cstate);
		cstate.nVersion.nVersion = OMX_VERSION;

		if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
			esyslog("rpihddevice: failed get clock state!");

		if (cstate.eState == OMX_TIME_ClockStateRunning)
			return true;
		else
			return false;
	}

	enum eClockState {
		eClockStateRun,
		eClockStateStop,
		eClockStateWaitForVideo,
		eClockStateWaitForAudio,
		eClockStateWaitForAudioVideo
	};

	void SetClockState(eClockState clockState)
	{
		dsyslog("rpihddevice: SetClockState(%s)",
			clockState == eClockStateRun ? "eClockStateRun" :
			clockState == eClockStateStop ? "eClockStateStop" :
			clockState == eClockStateWaitForVideo ? "eClockStateWaitForVideo" :
			clockState == eClockStateWaitForAudio ? "eClockStateWaitForAudio" :
			clockState == eClockStateWaitForAudioVideo ? "eClockStateWaitForAudioVideo" : "unknown");

		OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
		memset(&cstate, 0, sizeof(cstate));
		cstate.nSize = sizeof(cstate);
		cstate.nVersion.nVersion = OMX_VERSION;

		if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
			esyslog("rpihddevice: failed get clock state!");

		if ((cstate.eState == OMX_TIME_ClockStateRunning) &&
				(clockState == eClockStateWaitForVideo ||
				 clockState == eClockStateWaitForAudio ||
				 clockState == eClockStateWaitForAudioVideo))
		{
			// clock already running, need to stop it first
			cstate.eState = OMX_TIME_ClockStateStopped;
			if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
					OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
				esyslog("rpihddevice: failed set clock state!");
		}

		switch (clockState)
		{
		case eClockStateRun:
			cstate.eState = OMX_TIME_ClockStateRunning;
			break;

		case eClockStateStop:
			cstate.eState = OMX_TIME_ClockStateStopped;
			break;

		case eClockStateWaitForVideo:
			cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
			cstate.nWaitMask = OMX_CLOCKPORT0;
			m_firstVideoBuffer = true;
			break;

		case eClockStateWaitForAudio:
			cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
			cstate.nWaitMask = OMX_CLOCKPORT1;
			m_firstAudioBuffer = true;
			break;

		case eClockStateWaitForAudioVideo:
			cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
			cstate.nWaitMask = OMX_CLOCKPORT0 | OMX_CLOCKPORT1;
			m_firstVideoBuffer = true;
			m_firstAudioBuffer = true;
			break;
		}

		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
			esyslog("rpihddevice: failed set clock state!");
	}

	void SetClockScale(float scale)
	{
		OMX_TIME_CONFIG_SCALETYPE scaleType;
		memset(&scaleType, 0, sizeof(scaleType));
		scaleType.nSize = sizeof(scaleType);
		scaleType.nVersion.nVersion = OMX_VERSION;
		scaleType.xScale = floor(scale * pow(2, 16));
		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeScale, &scaleType) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set clock scale (%d)!", scaleType.xScale);
		else
			dsyslog("rpihddevice: set clock scale to %.2f (%d)", scale, scaleType.xScale);
	}

	void SetVolume(int vol)
	{
		dsyslog("rpihddevice: SetVolumeDevice(%d)", vol);

		OMX_AUDIO_CONFIG_VOLUMETYPE volume;
		memset(&volume, 0, sizeof(volume));
		volume.nSize = sizeof(volume);
		volume.nVersion.nVersion = OMX_VERSION;
		volume.nPortIndex = 100;
		volume.bLinear = OMX_TRUE;
		volume.sVolume.nValue = vol * 100 / 255;

		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexConfigAudioVolume, &volume) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set volume!");
	}

	void SendEos(void)
	{
#if 0
		OMX_BUFFERHEADERTYPE *buf = ilclient_get_input_buffer(m_comp[eVideoDecoder], 130, 1);
		if (buf == NULL)
			return;

		buf->nFilledLen = 0;
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

		if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eVideoDecoder]), buf) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to send empty packet to video decoder!");

		if (!m_eosEvent->Wait(10000))
			esyslog("rpihddevice: time out waiting for EOS event!");
#endif
	}

	void Stop(void)
	{
		// put video decoder into idle
		ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle);

		// put video scheduler into idle
		ilclient_flush_tunnels(&m_tun[eVideoDecoderToVideoScheduler], 1);
		ilclient_disable_tunnel(&m_tun[eVideoDecoderToVideoScheduler]);
		ilclient_flush_tunnels(&m_tun[eClockToVideoScheduler], 1);
		ilclient_disable_tunnel(&m_tun[eClockToVideoScheduler]);
		ilclient_change_component_state(m_comp[eVideoScheduler], OMX_StateIdle);

		// put video render into idle
		ilclient_flush_tunnels(&m_tun[eVideoSchedulerToVideoRender], 1);
		ilclient_disable_tunnel(&m_tun[eVideoSchedulerToVideoRender]);
		ilclient_change_component_state(m_comp[eVideoRender], OMX_StateIdle);

		// put audio render onto idle
		ilclient_flush_tunnels(&m_tun[eClockToAudioRender], 1);
		ilclient_disable_tunnel(&m_tun[eClockToAudioRender]);
		ilclient_change_component_state(m_comp[eAudioRender], OMX_StateIdle);

		// disable port buffers and allow video decoder and audio render to reconfig
		ilclient_disable_port_buffers(m_comp[eVideoDecoder], 130, NULL, NULL, NULL);
		ilclient_disable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL);

		SetClockState(eClockStateStop);
	}

	int SetVideoCodec(OMX_VIDEO_CODINGTYPE coding)
	{
		// configure video decoder
		OMX_VIDEO_PARAM_PORTFORMATTYPE videoFormat;
		memset(&videoFormat, 0, sizeof(videoFormat));
		videoFormat.nSize = sizeof(videoFormat);
		videoFormat.nVersion.nVersion = OMX_VERSION;
		videoFormat.nPortIndex = 130;
		videoFormat.eCompressionFormat = coding;
		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
				OMX_IndexParamVideoPortFormat, &videoFormat) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set video decoder parameters!");

		OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ectype;
		memset (&ectype, 0, sizeof(ectype));
		ectype.nSize = sizeof(ectype);
		ectype.nVersion.nVersion = OMX_VERSION;
		ectype.bStartWithValidFrame = OMX_FALSE;
		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
				OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ectype) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set video decode error concealment failed\n");

		if (ilclient_enable_port_buffers(m_comp[eVideoDecoder], 130, NULL, NULL, NULL) != 0)
			esyslog("rpihddevice: failed to enable port buffer on video decoder!");

		if (ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateExecuting) != 0)
			esyslog("rpihddevice: failed to set video decoder to executing state!");

		// setup clock tunnels first
		if (ilclient_setup_tunnel(&m_tun[eClockToVideoScheduler], 0, 0) != 0)
			esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

		return 0;
	}

	int SetAudioCodec(OMX_AUDIO_CODINGTYPE coding)
	{
		if (ilclient_enable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL) != 0)
			esyslog("rpihddevice: failed to enable port buffer on audio render!");

		ilclient_change_component_state(m_comp[eAudioRender], OMX_StateExecuting);

		if (ilclient_setup_tunnel(&m_tun[eClockToAudioRender], 0, 0) != 0)
			esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

		return 0;
	}

	OMX_BUFFERHEADERTYPE* GetAudioBuffer(uint64_t pts = 0)
	{
		m_mutex->Lock();
		OMX_BUFFERHEADERTYPE* buf = NULL;

		if (m_freeAudioBuffers > 0)
		{
			buf = ilclient_get_input_buffer(m_comp[eAudioRender], 100, 0);

			if (buf != NULL)
			{
				cOmx::PtsToTicks(pts, buf->nTimeStamp);
				buf->nFlags = m_firstAudioBuffer ? OMX_BUFFERFLAG_STARTTIME : OMX_BUFFERFLAG_TIME_UNKNOWN;
				m_firstAudioBuffer = false;
				m_freeAudioBuffers--;
			}
		}
		m_mutex->Unlock();
		return buf;
	}

	OMX_BUFFERHEADERTYPE* GetVideoBuffer(uint64_t pts = 0)
	{
		m_mutex->Lock();
		OMX_BUFFERHEADERTYPE* buf = NULL;

		if (m_freeVideoBuffers > 0)
		{
			buf = ilclient_get_input_buffer(m_comp[eVideoDecoder], 130, 0);

			if (buf != NULL)
			{
				cOmx::PtsToTicks(pts, buf->nTimeStamp);
				buf->nFlags = m_firstVideoBuffer ? OMX_BUFFERFLAG_STARTTIME : OMX_BUFFERFLAG_TIME_UNKNOWN;
				m_firstVideoBuffer = false;
				m_freeVideoBuffers--;
			}
		}
		m_mutex->Unlock();
		return buf;
	}

	bool VideoBuffersAvailable(void)
	{
		return (m_freeVideoBuffers > 0);
	}

	bool EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf)
	{
		if (!buf)
			return false;

		return (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eAudioRender]), buf) != OMX_ErrorNone);
	}

	bool EmptyVideoBuffer(OMX_BUFFERHEADERTYPE *buf)
	{
		if (!buf)
			return false;

		return (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eVideoDecoder]), buf) != OMX_ErrorNone);
	}
};

/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */

cOmxDevice::cOmxDevice(void (*onPrimaryDevice)(void)) :
	cDevice(),
	m_onPrimaryDevice(onPrimaryDevice),
	m_omx(new cOmx()),
	m_audio(new cAudio()),
	m_mutex(new cMutex()),
	m_state(eStop),
	m_audioCodecReady(false),
	m_videoCodecReady(false)
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
	return m_omx->Init();
}

int cOmxDevice::DeInit(void)
{
	return m_omx->DeInit();
}

bool cOmxDevice::CanReplay(void) const
{
	dsyslog("rpihddevice: CanReplay");
	// video codec de-initialization done
	return (m_state == eStop);
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
		m_state = eStop;
		m_omx->Stop();
		m_audioCodecReady = false;
		m_videoCodecReady = false;
		m_mutex->Unlock();
		break;

	case pmAudioVideo:
		m_mutex->Lock();
		m_state = eStarting;
		m_mutex->Unlock();
		break;

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

	if (m_state == eStarting)
	{
		m_omx->SetClockState(cOmx::eClockStateWaitForAudio);
		m_state = ePlay;
	}
	else if (m_state != ePlay)
	{
		m_mutex->Unlock();
		dsyslog("rpihddevice: PlayAudio() not replaying!");
		return 0;
	}

	if (!PesHasLength(Data))
	{
		esyslog("rpihddevice: empty audio packet dropped!");
		m_mutex->Unlock();
		return Length;
	}

	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

	const uchar *payload = Data + PesPayloadOffset(Data);
	int length = PesLength(Data) - PesPayloadOffset(Data);

	// try to init codec if PTS is valid
	if (!m_audioCodecReady && pts != 0)
		m_audioCodecReady = OmxSetAudioCodec(payload);

	if (!m_audioCodecReady)
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
		OMX_BUFFERHEADERTYPE *buf = m_omx->GetAudioBuffer(pts);
		if (buf == NULL)
		{
			//esyslog("rpihddevice: failed to get audio buffer!");
			m_mutex->Unlock();
			return 0;
		}

		// decode and write audio packet
		buf->nFilledLen = m_audio->readSamples(buf->pBuffer, buf->nAllocLen, done);

		if (m_omx->EmptyAudioBuffer(buf))
			break;
	}

	m_mutex->Unlock();
	return Length;
}

int cOmxDevice::PlayVideo(const uchar *Data, int Length)
{
	m_mutex->Lock();

	if (m_state == eStarting)
	{
		m_omx->SetClockState(cOmx::eClockStateWaitForAudioVideo);
		m_state = ePlay;
	}
	else if (m_state != ePlay)
	{
		m_mutex->Unlock();
		dsyslog("rpihddevice: PlayVideo() not replaying!");
		return 0;
	}

	if (!PesHasLength(Data))
	{
		esyslog("rpihddevice: empty video packet dropped!");
		m_mutex->Unlock();
		return Length;
	}

	int64_t pts = PesHasPts(Data) ? PesGetPts(Data) : 0;

	const uchar *payload = Data + PesPayloadOffset(Data);
	int length = PesLength(Data) - PesPayloadOffset(Data);

	// try to init codec if PTS is valid
	if (!m_videoCodecReady && pts != 0)
		m_videoCodecReady = OmxSetVideoCodec(payload);

	if (!m_videoCodecReady)
	{
		m_mutex->Unlock();
		return Length;
	}
	OMX_BUFFERHEADERTYPE *buf = m_omx->GetVideoBuffer(pts);
	if (buf == NULL)
	{
		//esyslog("rpihddevice: failed to get video buffer!");
		m_mutex->Unlock();
		return 0;
	}

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
	if (m_omx->EmptyVideoBuffer(buf))
		esyslog("rpihddevice: failed to pass buffer to video decoder!");

	m_mutex->Unlock();
	return Length;
}

int64_t cOmxDevice::GetSTC(void)
{
	return m_omx->GetSTC();
}

void cOmxDevice::Play(void)
{
	dsyslog("rpihddevice: Play()");
	m_omx->SetClockScale(1.0f);
}

void cOmxDevice::Freeze(void)
{
	dsyslog("rpihddevice: Freeze()");
	m_omx->SetClockScale(0.0f);
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
	m_omx->SetVolume(Volume);
}

bool cOmxDevice::Poll(cPoller &Poller, int TimeoutMs)
{
	cTimeMs time;
	time.Set();
	while (!m_omx->VideoBuffersAvailable())
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

bool cOmxDevice::OmxSetVideoCodec(const uchar *data)
{
	if (data[0] != 0x00 || data[1] != 0x00)
		return false;

	if (data[2] == 0x01 && data[3] == 0xb3)
	{
		dsyslog("rpihddevice: set video codec to MPEG2");
		return (m_omx->SetVideoCodec(OMX_VIDEO_CodingMPEG2) == 0);
	}

	else if ((data[2] == 0x01 && data[3] == 0x09 && data[4] == 0x10) ||
			(data[2] == 0x00 && data[3] == 0x01 && data[4] == 0x09 && data[5] == 0x10))
	{
		dsyslog("rpihddevice: set video codec to H264");
		return (m_omx->SetVideoCodec(OMX_VIDEO_CodingAVC) == 0);
	}

	esyslog("rpihddevice: invalid start sequence: %02x %02x %02x %02x %02x %02x %02x %02x",
			data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

	return false;
}

bool cOmxDevice::OmxSetAudioCodec(const uchar *data)
{
	return (m_omx->SetAudioCodec(OMX_AUDIO_CodingPCM) == 0);
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
