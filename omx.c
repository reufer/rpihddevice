/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "omx.h"

#include <vdr/tools.h>
#include <vdr/thread.h>

extern "C" {
#include "ilclient.h"
}

#include "bcm_host.h"

#define OMX_INIT_STRUCT(a) \
  memset(&(a), 0, sizeof(a)); \
  (a).nSize = sizeof(a); \
  (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (a).nVersion.s.nStep = OMX_VERSION_STEP

#define OMX_AUDIO_CHANNEL_MAPPING(s, c) \
switch (c) { \
case 4: \
	(s).eChannelMapping[0] = OMX_AUDIO_ChannelLF; \
	(s).eChannelMapping[1] = OMX_AUDIO_ChannelRF; \
	(s).eChannelMapping[2] = OMX_AUDIO_ChannelLR; \
	(s).eChannelMapping[3] = OMX_AUDIO_ChannelRR; \
	break; \
case 1: \
	(s).eChannelMapping[0] = OMX_AUDIO_ChannelCF; \
	break; \
case 8: \
	(s).eChannelMapping[6] = OMX_AUDIO_ChannelLS; \
	(s).eChannelMapping[7] = OMX_AUDIO_ChannelRS; \
case 6: \
	(s).eChannelMapping[2] = OMX_AUDIO_ChannelCF; \
	(s).eChannelMapping[3] = OMX_AUDIO_ChannelLFE; \
	(s).eChannelMapping[4] = OMX_AUDIO_ChannelLR; \
	(s).eChannelMapping[5] = OMX_AUDIO_ChannelRR; \
case 2: \
default: \
	(s).eChannelMapping[0] = OMX_AUDIO_ChannelLF; \
	(s).eChannelMapping[1] = OMX_AUDIO_ChannelRF; \
	break; }


const char* cOmx::errStr(int err)
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
}

void cOmx::HandleEndOfStream(unsigned int portId)
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

void cOmx::HandlePortSettingsChanged(unsigned int portId)
{
	//dsyslog("rpihddevice: HandlePortSettingsChanged(%d)", portId);

	switch (portId)
	{
	case 131:

		OMX_PARAM_PORTDEFINITIONTYPE portdef;
		OMX_INIT_STRUCT(portdef);
		portdef.nPortIndex = 131;
		if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]), OMX_IndexParamPortDefinition,
				&portdef) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to get video decoder port format!");

		OMX_CONFIG_INTERLACETYPE interlace;
		OMX_INIT_STRUCT(interlace);
		interlace.nPortIndex = 131;
		if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eVideoDecoder]), OMX_IndexConfigCommonInterlace,
				&interlace) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to get video decoder interlace config!");

		dsyslog("rpihddevice: decoding video %dx%d%s",
				portdef.format.video.nFrameWidth,
				portdef.format.video.nFrameHeight,
				interlace.eMode == OMX_InterlaceProgressive ? "p" : "i");

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

void cOmx::HandleBufferEmpty(COMPONENT_T *comp)
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

void cOmx::OnBufferEmpty(void *instance, COMPONENT_T *comp)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->HandleBufferEmpty(comp);
}

void cOmx::OnPortSettingsChanged(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->HandlePortSettingsChanged(data);
}

void cOmx::OnEndOfStream(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->HandleEndOfStream(data);
}

void cOmx::OnError(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	if ((OMX_S32)data != OMX_ErrorSameState)
		esyslog("rpihddevice: OmxError(%s)", errStr((int)data));
}

cOmx::cOmx() :
	m_mutex(new cMutex()),
	m_setAudioStartTime(false),
	m_setVideoStartTime(false),
	m_setVideoDiscontinuity(false),
	m_freeAudioBuffers(0),
	m_freeVideoBuffers(0),
	m_clockReference(eClockRefAudio)
{
}

cOmx::~cOmx()
{
	delete m_mutex;
}

int cOmx::Init(void)
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
	set_tunnel(&m_tun[eVideoDecoderToVideoScheduler],
		m_comp[eVideoDecoder], 131, m_comp[eVideoScheduler], 10);

	set_tunnel(&m_tun[eVideoSchedulerToVideoRender],
		m_comp[eVideoScheduler], 11, m_comp[eVideoRender], 90);

	set_tunnel(&m_tun[eClockToVideoScheduler],
		m_comp[eClock], 80, m_comp[eVideoScheduler], 12);

	set_tunnel(&m_tun[eClockToAudioRender],
		m_comp[eClock], 81, m_comp[eAudioRender], 101);

	// setup clock tunnels first
	if (ilclient_setup_tunnel(&m_tun[eClockToVideoScheduler], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

	if (ilclient_setup_tunnel(&m_tun[eClockToAudioRender], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to audio render!");

	ilclient_change_component_state(m_comp[eClock], OMX_StateExecuting);
	ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle);

	// set up the number and size of buffers for audio render
	m_freeAudioBuffers = 2; //64;

	OMX_PARAM_PORTDEFINITIONTYPE param;
	OMX_INIT_STRUCT(param);
	param.nPortIndex = 100;
	if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to get audio render port parameters!");
	param.nBufferSize = 160 * 1024;
	param.nBufferCountActual = m_freeAudioBuffers;
	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set audio render port parameters!");

	OMX_INIT_STRUCT(param);
	param.nPortIndex = 130;
	if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to get video decoder port parameters!");
	m_freeVideoBuffers = param.nBufferCountActual;

	dsyslog("rpihddevice: started with %d video and %d audio buffers",
		m_freeVideoBuffers, m_freeAudioBuffers);

/*	// configure video decoder stall callback
	OMX_CONFIG_BUFFERSTALLTYPE stallConf;
	OMX_INIT_STRUCT(stallConf);
	stallConf.nPortIndex = 131;
	stallConf.nDelay = 1500 * 1000;
	if (OMX_SetConfig(m_comp[eVideoDecoder], OMX_IndexConfigBufferStall,
			&stallConf) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decoder stall config!");

	OMX_CONFIG_REQUESTCALLBACKTYPE reqCallback;
	OMX_INIT_STRUCT(reqCallback);
	reqCallback.nPortIndex = 131;
	reqCallback.nIndex = OMX_IndexConfigBufferStall;
	reqCallback.bEnable = OMX_TRUE;
	if (OMX_SetConfig(m_comp[eVideoDecoder], OMX_IndexConfigRequestCallback,
			&reqCallback) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decoder stall callback!");
*/
//	if (ilclient_enable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL) != 0)
//		esyslog("rpihddevice: failed to enable port buffer on audio render!");

	SetClockState(cOmx::eClockStateRun);

	return 0;
}

int cOmx::DeInit(void)
{
	ilclient_teardown_tunnels(m_tun);
	ilclient_state_transition(m_comp, OMX_StateIdle);
	ilclient_state_transition(m_comp, OMX_StateLoaded);
	ilclient_cleanup_components(m_comp);

	OMX_Deinit();
	ilclient_destroy(m_client);

	return 0;
}

OMX_TICKS cOmx::ToOmxTicks(int64_t val)
{
	OMX_TICKS ticks;
	ticks.nLowPart = val;
	ticks.nHighPart = val >> 32;
	return ticks;
}

int64_t cOmx::FromOmxTicks(OMX_TICKS &ticks)
{
	int64_t ret = ticks.nLowPart | ((int64_t)(ticks.nHighPart) << 32);
	return ret;
}

void cOmx::PtsToTicks(uint64_t pts, OMX_TICKS &ticks)
{
	// ticks = pts * OMX_TICKS_PER_SECOND / PTSTICKS
	pts = pts * 100 / 9;
	ticks.nLowPart = pts;
	ticks.nHighPart = pts >> 32;
}

uint64_t cOmx::TicksToPts(OMX_TICKS &ticks)
{
	// pts = ticks * PTSTICKS / OMX_TICKS_PER_SECOND
	uint64_t pts = ticks.nHighPart;
	pts = (pts << 32) + ticks.nLowPart;
	pts = pts * 9 / 100;
	return pts;
}

int64_t cOmx::GetSTC(void)
{
	int64_t stc = -1;
//	return stc;

	OMX_TIME_CONFIG_TIMESTAMPTYPE timestamp;
	OMX_INIT_STRUCT(timestamp);
	timestamp.nPortIndex = OMX_ALL;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
		OMX_IndexConfigTimeCurrentMediaTime, &timestamp) != OMX_ErrorNone)
		esyslog("rpihddevice: failed get current clock reference!");
	else
		stc = TicksToPts(timestamp.nTimestamp);

//	dsyslog("S %u", timestamp.nTimestamp.nLowPart);

	return stc;
}

bool cOmx::IsClockRunning(void)
{
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	OMX_INIT_STRUCT(cstate);

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		esyslog("rpihddevice: failed get clock state!");

	if (cstate.eState == OMX_TIME_ClockStateRunning)
		return true;
	else
		return false;
}

void cOmx::SetClockState(eClockState clockState)
{
	m_mutex->Lock();

	dsyslog("rpihddevice: SetClockState(%s)",
		clockState == eClockStateRun ? "eClockStateRun" :
		clockState == eClockStateStop ? "eClockStateStop" :
		clockState == eClockStateWaitForVideo ? "eClockStateWaitForVideo" :
		clockState == eClockStateWaitForAudio ? "eClockStateWaitForAudio" :
		clockState == eClockStateWaitForAudioVideo ? "eClockStateWaitForAudioVideo" :
				"unknown");

	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	OMX_INIT_STRUCT(cstate);

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to get clock state!");

	// if clock is already running, we need to stop it first
	if ((cstate.eState == OMX_TIME_ClockStateRunning) &&
			(clockState == eClockStateWaitForVideo ||
			 clockState == eClockStateWaitForAudio ||
			 clockState == eClockStateWaitForAudioVideo))
	{
		cstate.eState = OMX_TIME_ClockStateStopped;
		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to stop clock!");
	}

	cstate.nWaitMask = 0;

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
		m_setVideoStartTime = true;
		cstate.nWaitMask = OMX_CLOCKPORT0;
		break;

	case eClockStateWaitForAudio:
		cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
		m_setAudioStartTime = true;
		cstate.nWaitMask = OMX_CLOCKPORT1;
		break;

	case eClockStateWaitForAudioVideo:
		cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
		m_setAudioStartTime = true;
		m_setVideoStartTime = true;
		cstate.nWaitMask = OMX_CLOCKPORT0 | OMX_CLOCKPORT1;
		break;
	}

	if (cstate.eState == OMX_TIME_ClockStateWaitingForStartTime)
		// 200ms pre roll, value taken from omxplayer
		cstate.nOffset = ToOmxTicks(-1000LL * 400);

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set clock state!");

	m_mutex->Unlock();
}

void cOmx::SetClockScale(float scale)
{
	OMX_TIME_CONFIG_SCALETYPE scaleType;
	OMX_INIT_STRUCT(scaleType);
	scaleType.xScale = floor(scale * pow(2, 16));

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeScale, &scaleType) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set clock scale (%d)!", scaleType.xScale);
}

void cOmx::SetStartTime(uint64_t pts)
{
	OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
	OMX_INIT_STRUCT(timeStamp);
	timeStamp.nPortIndex = 80; //m_clockReference == eClockRefAudio ? 81 : 80;
	cOmx::PtsToTicks(pts, timeStamp.nTimestamp);

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClientStartTime, &timeStamp) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set current start time!");
}

void cOmx::SetCurrentReferenceTime(uint64_t pts)
{
	OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
	OMX_INIT_STRUCT(timeStamp);
	timeStamp.nPortIndex = 80; //OMX_ALL; //m_clockReference == eClockRefAudio ? 81 : 80;
	cOmx::PtsToTicks(pts, timeStamp.nTimestamp);

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			m_clockReference == eClockRefAudio ?
			OMX_IndexConfigTimeCurrentAudioReference :
			OMX_IndexConfigTimeCurrentVideoReference, &timeStamp) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set current %s reference time!",
				m_clockReference == eClockRefAudio ? "audio" : "video");
}

unsigned int cOmx::GetMediaTime(void)
{
	unsigned int ret = 0;

	OMX_TIME_CONFIG_TIMESTAMPTYPE timestamp;
	OMX_INIT_STRUCT(timestamp);
	timestamp.nPortIndex = OMX_ALL;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
		OMX_IndexConfigTimeCurrentMediaTime, &timestamp) != OMX_ErrorNone)
		esyslog("rpihddevice: failed get current clock reference!");
	else
		ret = timestamp.nTimestamp.nLowPart;

	return ret;
}

void cOmx::SetClockReference(eClockReference clockReference)
{
	m_mutex->Lock();

	m_clockReference = clockReference;

	OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
	OMX_INIT_STRUCT(refClock);
	refClock.eClock = (m_clockReference == eClockRefAudio) ?
			OMX_TIME_RefClockAudio : OMX_TIME_RefClockVideo;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeActiveRefClock, &refClock) != OMX_ErrorNone)
		esyslog("rpihddevice: failed set active clock reference!");
	else
		dsyslog("rpihddevice: set active clock reference to %s",
				m_clockReference == eClockRefAudio ? "audio" : "video");

	m_mutex->Unlock();
}

void cOmx::SetVolume(int vol)
{
	dsyslog("rpihddevice: SetVolumeDevice(%d)", vol);

	OMX_AUDIO_CONFIG_VOLUMETYPE volume;
	OMX_INIT_STRUCT(volume);
	volume.nPortIndex = 100;
	volume.bLinear = OMX_TRUE;
	volume.sVolume.nValue = vol * 100 / 255;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexConfigAudioVolume, &volume) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set volume!");
}

void cOmx::SendEos(void)
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

void cOmx::StopVideo(void)
{
	dsyslog("rpihddevice: StopVideo()");

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

	// disable port buffers and allow video decoder to reconfig
	ilclient_disable_port_buffers(m_comp[eVideoDecoder], 130, NULL, NULL, NULL);
}

void cOmx::StopAudio(void)
{
	// put audio render onto idle
	ilclient_flush_tunnels(&m_tun[eClockToAudioRender], 1);
//	ilclient_disable_tunnel(&m_tun[eClockToAudioRender]);
	ilclient_change_component_state(m_comp[eAudioRender], OMX_StateIdle);
	ilclient_disable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL);
}

void cOmx::SetVideoDataUnitType(eDataUnitType dataUnitType)
{
	OMX_PARAM_DATAUNITTYPE dataUnit;
	OMX_INIT_STRUCT(dataUnit);
	dataUnit.nPortIndex = 130;

	dataUnit.eEncapsulationType = OMX_DataEncapsulationElementaryStream;
	dataUnit.eUnitType = dataUnitType == eCodedPicture ?
				OMX_DataUnitCodedPicture : OMX_DataUnitArbitraryStreamSection;

	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamBrcmDataUnit, &dataUnit) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decoder data unit type!");

}

void cOmx::SetVideoErrorConcealment(bool startWithValidFrame)
{
	OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ectype;
	OMX_INIT_STRUCT(ectype);
	ectype.bStartWithValidFrame = startWithValidFrame ? OMX_TRUE : OMX_FALSE;
	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ectype) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decode error concealment failed\n");
}

void cOmx::FlushAudio(void)
{
	ilclient_flush_tunnels(&m_tun[eClockToAudioRender], 1);

	if (OMX_SendCommand(ILC_GET_HANDLE(m_comp[eAudioRender]), OMX_CommandFlush, 100, NULL) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to flush audio render!");
}

void cOmx::FlushVideo(bool flushRender)
{
	ilclient_flush_tunnels(&m_tun[eClockToVideoScheduler], 1);

	if (OMX_SendCommand(ILC_GET_HANDLE(m_comp[eVideoDecoder]), OMX_CommandFlush, 130, NULL) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to flush video decoder!");

	ilclient_flush_tunnels(&m_tun[eVideoDecoderToVideoScheduler], 1);

	if (flushRender)
		ilclient_flush_tunnels(&m_tun[eVideoSchedulerToVideoRender], 1);

	m_setVideoDiscontinuity = true;
}

int cOmx::SetVideoCodec(cVideoCodec::eCodec codec, eDataUnitType dataUnit)
{
	dsyslog("rpihddevice: SetVideoCodec()");

	if (ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle) != 0)
		esyslog("rpihddevice: failed to set video decoder to idle state!");

	// configure video decoder
	OMX_VIDEO_PARAM_PORTFORMATTYPE videoFormat;
	OMX_INIT_STRUCT(videoFormat);
	videoFormat.nPortIndex = 130;
	videoFormat.eCompressionFormat =
			codec == cVideoCodec::eMPEG2 ? OMX_VIDEO_CodingMPEG2 :
			codec == cVideoCodec::eH264  ? OMX_VIDEO_CodingAVC   :
					OMX_VIDEO_CodingAutoDetect;

	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamVideoPortFormat, &videoFormat) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decoder parameters!");

	// start with valid frames only if codec is MPEG2
	SetVideoErrorConcealment(codec == cVideoCodec::eMPEG2);
	SetVideoDataUnitType(dataUnit);
	//SetVideoDecoderExtraBuffers(3);

	if (ilclient_enable_port_buffers(m_comp[eVideoDecoder], 130, NULL, NULL, NULL) != 0)
		esyslog("rpihddevice: failed to enable port buffer on video decoder!");

	if (ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateExecuting) != 0)
		esyslog("rpihddevice: failed to set video decoder to executing state!");

	// setup clock tunnels first
	if (ilclient_setup_tunnel(&m_tun[eClockToVideoScheduler], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

	return 0;
}

void cOmx::SetVideoDecoderExtraBuffers(int extraBuffers)
{
	OMX_PARAM_U32TYPE u32;
	OMX_INIT_STRUCT(u32);
	u32.nPortIndex = 130;
	u32.nU32 = extraBuffers;
	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamBrcmExtraBuffers, &u32) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set video decoder extra buffers!");
}

int cOmx::SetupAudioRender(cAudioCodec::eCodec outputFormat, int channels,
		cAudioPort::ePort audioPort, int samplingRate)
{
	// put audio render onto idle
	ilclient_flush_tunnels(&m_tun[eClockToAudioRender], 1);
	ilclient_disable_tunnel(&m_tun[eClockToAudioRender]);
	ilclient_change_component_state(m_comp[eAudioRender], OMX_StateIdle);
	ilclient_disable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL);

	if (OMX_SendCommand(ILC_GET_HANDLE(m_comp[eAudioRender]), OMX_CommandFlush, 100, NULL) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to flush audio render!");

	OMX_AUDIO_PARAM_PORTFORMATTYPE format;
	OMX_INIT_STRUCT(format);
	format.nPortIndex = 100;
	if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamAudioPortFormat, &format) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to get audio port format parameters!");

	format.eEncoding =
		outputFormat == cAudioCodec::ePCM  ? OMX_AUDIO_CodingPCM :
		outputFormat == cAudioCodec::eMPG  ? OMX_AUDIO_CodingMP3 :
		outputFormat == cAudioCodec::eAC3  ? OMX_AUDIO_CodingDDP :
		outputFormat == cAudioCodec::eEAC3 ? OMX_AUDIO_CodingDDP :
		outputFormat == cAudioCodec::eAAC  ? OMX_AUDIO_CodingAAC :
		outputFormat == cAudioCodec::eADTS ? OMX_AUDIO_CodingDTS :
				OMX_AUDIO_CodingAutoDetect;

	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamAudioPortFormat, &format) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set audio port format parameters!");

	switch (outputFormat)
	{
	case cAudioCodec::eMPG:
		OMX_AUDIO_PARAM_MP3TYPE mp3;
		OMX_INIT_STRUCT(mp3);
		mp3.nPortIndex = 100;
		mp3.nChannels = channels;
		mp3.nSampleRate = samplingRate;
		mp3.eChannelMode = OMX_AUDIO_ChannelModeStereo; // ?
		mp3.eFormat = OMX_AUDIO_MP3StreamFormatMP1Layer3; // should be MPEG-1 layer 2

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioMp3, &mp3) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render mp3 parameters!");
		break;

	case cAudioCodec::eAC3:
	case cAudioCodec::eEAC3:
		OMX_AUDIO_PARAM_DDPTYPE ddp;
		OMX_INIT_STRUCT(ddp);
		ddp.nPortIndex = 100;
		ddp.nChannels = channels;
		ddp.nSampleRate = samplingRate;
		OMX_AUDIO_CHANNEL_MAPPING(ddp, channels);

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioDdp, &ddp) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render ddp parameters!");
		break;

	case cAudioCodec::eAAC:
		OMX_AUDIO_PARAM_AACPROFILETYPE aac;
		OMX_INIT_STRUCT(aac);
		aac.nPortIndex = 100;
		aac.nChannels = channels;
		aac.nSampleRate = samplingRate;
		aac.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LATM;

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioAac, &aac) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render aac parameters!");
		break;

	case cAudioCodec::eADTS:
		OMX_AUDIO_PARAM_DTSTYPE dts;
		OMX_INIT_STRUCT(aac);
		dts.nPortIndex = 100;
		dts.nChannels = channels;
		dts.nSampleRate = samplingRate;
		dts.nDtsType = 1; // ??
		dts.nFormat = 0; // ??
		dts.nDtsFrameSizeBytes = 0; // ?
			OMX_AUDIO_CHANNEL_MAPPING(dts, channels);

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioDts, &dts) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render dts parameters!");
		break;

	case cAudioCodec::ePCM:
		OMX_AUDIO_PARAM_PCMMODETYPE pcm;
		OMX_INIT_STRUCT(pcm);
		pcm.nPortIndex = 100;
		pcm.nChannels = channels == 6 ? 8 : channels;
		pcm.eNumData = OMX_NumericalDataSigned;
		pcm.eEndian = OMX_EndianLittle;
		pcm.bInterleaved = OMX_TRUE;
		pcm.nBitPerSample = 16;
		pcm.nSamplingRate = samplingRate;
		pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;
		OMX_AUDIO_CHANNEL_MAPPING(pcm, channels);

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioPcm, &pcm) != OMX_ErrorNone)
			esyslog("rpihddevice: failed to set audio render pcm parameters!");
		break;

	default:
		esyslog("rpihddevice: output codec not supported: %s!",
				cAudioCodec::Str(outputFormat));
		break;
	}

	OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
	OMX_INIT_STRUCT(audioDest);
	strcpy((char *)audioDest.sName,
			audioPort == cAudioPort::eLocal ? "local" : "hdmi");

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone)
		esyslog("rpihddevice: failed to set audio destination!");

	if (ilclient_enable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL) != 0)
		esyslog("rpihddevice: failed to enable port buffer on audio render!");

	ilclient_change_component_state(m_comp[eAudioRender], OMX_StateExecuting);

	if (ilclient_setup_tunnel(&m_tun[eClockToAudioRender], 0, 0) != 0)
		esyslog("rpihddevice: failed to setup up tunnel from clock to video scheduler!");

	return 0;
}

OMX_BUFFERHEADERTYPE* cOmx::GetAudioBuffer(uint64_t pts)
{
	m_mutex->Lock();
	OMX_BUFFERHEADERTYPE* buf = NULL;

	if (m_freeAudioBuffers > 0)
	{
		buf = ilclient_get_input_buffer(m_comp[eAudioRender], 100, 0);
		if (buf)
		{
			cOmx::PtsToTicks(pts, buf->nTimeStamp);
			buf->nFlags = pts ? 0 : OMX_BUFFERFLAG_TIME_UNKNOWN;
			buf->nFlags |= m_setAudioStartTime ? OMX_BUFFERFLAG_STARTTIME : 0;

			m_setAudioStartTime = false;
			m_freeAudioBuffers--;
		}
	}
	m_mutex->Unlock();
	return buf;
}

OMX_BUFFERHEADERTYPE* cOmx::GetVideoBuffer(uint64_t pts)
{
	m_mutex->Lock();
	OMX_BUFFERHEADERTYPE* buf = NULL;

	if (m_freeVideoBuffers > 0)
	{
		buf = ilclient_get_input_buffer(m_comp[eVideoDecoder], 130, 0);
		if (buf)
		{
			cOmx::PtsToTicks(pts, buf->nTimeStamp);
			buf->nFlags = pts ? 0 : OMX_BUFFERFLAG_TIME_UNKNOWN;
			buf->nFlags |= m_setVideoStartTime ? OMX_BUFFERFLAG_STARTTIME : 0;
			buf->nFlags |= m_setVideoDiscontinuity ? OMX_BUFFERFLAG_DISCONTINUITY : 0;

			m_setVideoStartTime = false;
			m_setVideoDiscontinuity = false;
			m_freeVideoBuffers--;
		}
	}
	m_mutex->Unlock();
	return buf;
}

bool cOmx::PollVideoBuffers(int minBuffers)
{
	return (m_freeVideoBuffers > minBuffers);
}

bool cOmx::PollAudioBuffers(int minBuffers)
{
	return (m_freeAudioBuffers > minBuffers);
}

bool cOmx::EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf)
{
	if (!buf)
		return false;

	return (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eAudioRender]), buf)
			== OMX_ErrorNone);
}

bool cOmx::EmptyVideoBuffer(OMX_BUFFERHEADERTYPE *buf)
{
	if (!buf)
		return false;

	return (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eVideoDecoder]), buf)
			== OMX_ErrorNone);
}
