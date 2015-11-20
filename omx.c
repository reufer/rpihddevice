/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <queue>

#include "omx.h"
#include "display.h"

#include <vdr/tools.h>
#include <vdr/thread.h>

extern "C" {
#include "ilclient.h"
}

#include "bcm_host.h"

#define OMX_PRE_ROLL 0

// default: 20x 81920 bytes, now 128x 64k (8M)
#define OMX_VIDEO_BUFFERS 128
#define OMX_VIDEO_BUFFERSIZE KILOBYTE(64);

// default: 16x 4096 bytes, now 128x 16k (2M)
#define OMX_AUDIO_BUFFERS 128
#define OMX_AUDIO_BUFFERSIZE KILOBYTE(16);

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

class cOmxEvents
{

public:

	enum eEvent {
		ePortSettingsChanged,
		eConfigChanged,
		eEndOfStream,
		eBufferEmptied
	};

	struct Event
	{
		Event(eEvent _event, int _data)
			: event(_event), data(_data) { };
		eEvent 	event;
		int		data;
	};

	cOmxEvents() :
		m_signal(new cCondWait()),
		m_mutex(new cMutex())
	{ }

	virtual ~cOmxEvents()
	{
		while (!m_events.empty())
		{
			delete m_events.front();
			m_events.pop();
		}
		delete m_signal;
		delete m_mutex;
	}

	Event* Get(void)
	{
		Event* event = 0;
		if (!m_events.empty())
		{
			m_mutex->Lock();
			event = m_events.front();
			m_events.pop();
			m_mutex->Unlock();
		}
		return event;
	}

	void Add(Event* event)
	{
		m_mutex->Lock();
		m_events.push(event);
		m_mutex->Unlock();
		m_signal->Signal();
	}

private:

	cOmxEvents(const cOmxEvents&);
	cOmxEvents& operator= (const cOmxEvents&);

	cCondWait*	m_signal;
	cMutex*		m_mutex;
	std::queue<Event*> m_events;
};

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

void cOmx::Action(void)
{
	cTimeMs timer;
	while (Running())
	{
		while (cOmxEvents::Event* event = m_portEvents->Get())
		{
			switch (event->event)
			{
			case cOmxEvents::ePortSettingsChanged:
				if (m_handlePortEvents)
					HandlePortSettingsChanged(event->data);
				break;

			case cOmxEvents::eConfigChanged:
				if (event->data == OMX_IndexConfigBufferStall)
					if (IsBufferStall() && !IsClockFreezed() && m_onBufferStall)
						m_onBufferStall(m_onBufferStallData);
				break;

			case cOmxEvents::eEndOfStream:
				if (event->data == 90 && m_onEndOfStream)
					m_onEndOfStream(m_onEndOfStreamData);
				break;

			case cOmxEvents::eBufferEmptied:
				HandlePortBufferEmptied((eOmxComponent)event->data);
				break;

			default:
				break;
			}

			delete event;
		}
		cCondWait::SleepMs(10);

		if (timer.TimedOut())
		{
			timer.Set(100);
			Lock();
			for (int i = BUFFERSTAT_FILTER_SIZE - 1; i > 0; i--)
			{
				m_usedAudioBuffers[i] = m_usedAudioBuffers[i - 1];
				m_usedVideoBuffers[i] = m_usedVideoBuffers[i - 1];
			}
			Unlock();
		}
	}
}

bool cOmx::PollVideo(void)
{
	return (m_usedVideoBuffers[0] * 100 / OMX_VIDEO_BUFFERS) < 90;
}

void cOmx::GetBufferUsage(int &audio, int &video)
{
	audio = 0;
	video = 0;
	for (int i = 0; i < BUFFERSTAT_FILTER_SIZE; i++)
	{
		audio += m_usedAudioBuffers[i];
		video += m_usedVideoBuffers[i];
	}
	audio = audio * 100 / BUFFERSTAT_FILTER_SIZE / OMX_AUDIO_BUFFERS;
	video = video * 100 / BUFFERSTAT_FILTER_SIZE / OMX_VIDEO_BUFFERS;
}

void cOmx::HandlePortBufferEmptied(eOmxComponent component)
{
	Lock();

	switch (component)
	{
	case eVideoDecoder:
		m_usedVideoBuffers[0]--;
		break;

	case eAudioRender:
		m_usedAudioBuffers[0]--;
		break;

	default:
		ELOG("HandlePortBufferEmptied: invalid component!");
		break;
	}
	Unlock();
}

void cOmx::HandlePortSettingsChanged(unsigned int portId)
{
	Lock();
	DBG("HandlePortSettingsChanged(%d)", portId);

	switch (portId)
	{
	case 191:
		if (ilclient_setup_tunnel(&m_tun[eVideoFxToVideoScheduler], 0, 0) != 0)
			ELOG("failed to setup up tunnel from video fx to scheduler!");
		if (ilclient_change_component_state(m_comp[eVideoScheduler], OMX_StateExecuting) != 0)
			ELOG("failed to enable video scheduler!");
		break;

	case 131:
		OMX_PARAM_PORTDEFINITIONTYPE portdef;
		OMX_INIT_STRUCT(portdef);
		portdef.nPortIndex = 131;
		if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]), OMX_IndexParamPortDefinition,
				&portdef) != OMX_ErrorNone)
			ELOG("failed to get video decoder port format!");

		OMX_CONFIG_INTERLACETYPE interlace;
		OMX_INIT_STRUCT(interlace);
		interlace.nPortIndex = 131;
		if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eVideoDecoder]), OMX_IndexConfigCommonInterlace,
				&interlace) != OMX_ErrorNone)
			ELOG("failed to get video decoder interlace config!");

		m_videoFrameFormat.width = portdef.format.video.nFrameWidth;
		m_videoFrameFormat.height = portdef.format.video.nFrameHeight;
		m_videoFrameFormat.scanMode =
				interlace.eMode == OMX_InterlaceProgressive ? cScanMode::eProgressive :
				interlace.eMode == OMX_InterlaceFieldSingleUpperFirst ? cScanMode::eTopFieldFirst :
				interlace.eMode == OMX_InterlaceFieldSingleLowerFirst ? cScanMode::eBottomFieldFirst :
				interlace.eMode == OMX_InterlaceFieldsInterleavedUpperFirst ? cScanMode::eTopFieldFirst :
				interlace.eMode == OMX_InterlaceFieldsInterleavedLowerFirst ? cScanMode::eBottomFieldFirst :
						cScanMode::eProgressive;

		// discard 4 least significant bits, since there might be some deviation
		// due to jitter in time stamps
		m_videoFrameFormat.frameRate = ALIGN_UP(
				portdef.format.video.xFramerate & 0xfffffff0, 1 << 16) >> 16;

		// workaround for progressive streams detected as interlaced video by
		// the decoder due to missing SEI parsing
		// see: https://github.com/raspberrypi/firmware/issues/283
		// update: with FW from 2015/01/18 this is not necessary anymore
		if (m_videoFrameFormat.Interlaced() && m_videoFrameFormat.frameRate >= 50)
		{
			DLOG("%di looks implausible, you should use a recent firmware...",
					m_videoFrameFormat.frameRate * 2);
			//m_videoFormat.interlaced = false;
		}

		if (m_videoFrameFormat.Interlaced())
			m_videoFrameFormat.frameRate = m_videoFrameFormat.frameRate * 2;

		if (m_onStreamStart)
			m_onStreamStart(m_onStreamStartData);

		OMX_CONFIG_IMAGEFILTERPARAMSTYPE filterparam;
		OMX_INIT_STRUCT(filterparam);
		filterparam.nPortIndex = 191;
		filterparam.eImageFilter = OMX_ImageFilterNone;

		OMX_PARAM_U32TYPE extraBuffers;
		OMX_INIT_STRUCT(extraBuffers);
		extraBuffers.nPortIndex = 130;

		if (cRpiDisplay::IsProgressive() && m_videoFrameFormat.Interlaced())
		{
			bool fastDeinterlace = portdef.format.video.nFrameWidth *
					portdef.format.video.nFrameHeight > 576 * 720;
			fastDeinterlace = true;

			filterparam.nNumParams = 4;
			filterparam.nParams[0] = 3;
			filterparam.nParams[1] = 0; // default frame interval
			filterparam.nParams[2] = 0; // half framerate
			filterparam.nParams[3] = 1; // use qpus

			filterparam.eImageFilter = fastDeinterlace ?
					OMX_ImageFilterDeInterlaceFast :
					OMX_ImageFilterDeInterlaceAdvanced;

			if (fastDeinterlace)
				extraBuffers.nU32 = -2;
		}
		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoFx]),
				OMX_IndexConfigCommonImageFilterParameters, &filterparam) != OMX_ErrorNone)
			ELOG("failed to set deinterlacing paramaters!");

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoFx]),
				OMX_IndexParamBrcmExtraBuffers, &extraBuffers) != OMX_ErrorNone)
			ELOG("failed to set video fx extra buffers!");

		if (ilclient_setup_tunnel(&m_tun[eVideoDecoderToVideoFx], 0, 0) != 0)
			ELOG("failed to setup up tunnel from video decoder to fx!");
		if (ilclient_change_component_state(m_comp[eVideoFx], OMX_StateExecuting) != 0)
			ELOG("failed to enable video fx!");

		break;

	case 11:
		if (ilclient_setup_tunnel(&m_tun[eVideoSchedulerToVideoRender], 0, 0) != 0)
			ELOG("failed to setup up tunnel from scheduler to render!");
		if (ilclient_change_component_state(m_comp[eVideoRender], OMX_StateExecuting) != 0)
			ELOG("failed to enable video render!");
		break;
	}

	Unlock();
}

void cOmx::OnBufferEmpty(void *instance, COMPONENT_T *comp)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->m_portEvents->Add(
			new cOmxEvents::Event(cOmxEvents::eBufferEmptied,
					comp == omx->m_comp[eVideoDecoder] ? eVideoDecoder :
					comp == omx->m_comp[eAudioRender] ? eAudioRender :
							eInvalidComponent));
}

void cOmx::OnPortSettingsChanged(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->m_portEvents->Add(
			new cOmxEvents::Event(cOmxEvents::ePortSettingsChanged, data));
}

void cOmx::OnConfigChanged(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->m_portEvents->Add(
			new cOmxEvents::Event(cOmxEvents::eConfigChanged, data));
}

void cOmx::OnEndOfStream(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	cOmx* omx = static_cast <cOmx*> (instance);
	omx->m_portEvents->Add(
			new cOmxEvents::Event(cOmxEvents::eEndOfStream, data));
}

void cOmx::OnError(void *instance, COMPONENT_T *comp, OMX_U32 data)
{
	if ((OMX_S32)data != OMX_ErrorSameState)
		ELOG("OmxError(%s)", errStr((int)data));
}

cOmx::cOmx() :
	cThread(),
	m_client(NULL),
	m_setAudioStartTime(false),
	m_setVideoStartTime(false),
	m_setVideoDiscontinuity(false),
	m_spareAudioBuffers(0),
	m_spareVideoBuffers(0),
	m_clockReference(eClockRefNone),
	m_clockScale(0),
	m_portEvents(new cOmxEvents()),
	m_handlePortEvents(false),
	m_onBufferStall(0),
	m_onBufferStallData(0),
	m_onEndOfStream(0),
	m_onEndOfStreamData(0),
	m_onStreamStart(0),
	m_onStreamStartData(0)
{
	memset(m_tun, 0, sizeof(m_tun));
	memset(m_comp, 0, sizeof(m_comp));

	m_videoFrameFormat.width = 0;
	m_videoFrameFormat.height = 0;
	m_videoFrameFormat.frameRate = 0;
	m_videoFrameFormat.scanMode = cScanMode::eProgressive;
}

cOmx::~cOmx()
{
	delete m_portEvents;
}

int cOmx::Init(int display, int layer)
{
	m_client = ilclient_init();
	if (m_client == NULL)
		ELOG("ilclient_init() failed!");

	if (OMX_Init() != OMX_ErrorNone)
		ELOG("OMX_Init() failed!");

	ilclient_set_error_callback(m_client, OnError, this);
	ilclient_set_empty_buffer_done_callback(m_client, OnBufferEmpty, this);
	ilclient_set_port_settings_callback(m_client, OnPortSettingsChanged, this);
	ilclient_set_eos_callback(m_client, OnEndOfStream, this);
	ilclient_set_configchanged_callback(m_client, OnConfigChanged, this);

	// create video_decode
	if (ilclient_create_component(m_client, &m_comp[eVideoDecoder],
		"video_decode",	(ILCLIENT_CREATE_FLAGS_T)
		(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0)
		ELOG("failed creating video decoder!");

	// create image_fx
	if (ilclient_create_component(m_client, &m_comp[eVideoFx],
		"image_fx",	ILCLIENT_DISABLE_ALL_PORTS) != 0)
		ELOG("failed creating video fx!");

	// create video_render
	if (ilclient_create_component(m_client, &m_comp[eVideoRender],
		"video_render",	ILCLIENT_DISABLE_ALL_PORTS) != 0)
		ELOG("failed creating video render!");

	//create clock
	if (ilclient_create_component(m_client, &m_comp[eClock],
		"clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		ELOG("failed creating clock!");

	// create audio_render
	if (ilclient_create_component(m_client, &m_comp[eAudioRender],
		"audio_render",	(ILCLIENT_CREATE_FLAGS_T)
		(ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS)) != 0)
		ELOG("failed creating audio render!");

	//create video_scheduler
	if (ilclient_create_component(m_client, &m_comp[eVideoScheduler],
		"video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		ELOG("failed creating video scheduler!");

	// setup tunnels
	set_tunnel(&m_tun[eVideoDecoderToVideoFx],
		m_comp[eVideoDecoder], 131, m_comp[eVideoFx], 190);

	set_tunnel(&m_tun[eVideoFxToVideoScheduler],
		m_comp[eVideoFx], 191, m_comp[eVideoScheduler], 10);

	set_tunnel(&m_tun[eVideoSchedulerToVideoRender],
		m_comp[eVideoScheduler], 11, m_comp[eVideoRender], 90);

	set_tunnel(&m_tun[eClockToVideoScheduler],
		m_comp[eClock], 80, m_comp[eVideoScheduler], 12);

	set_tunnel(&m_tun[eClockToAudioRender],
		m_comp[eClock], 81, m_comp[eAudioRender], 101);

	// setup clock tunnels first
	if (ilclient_setup_tunnel(&m_tun[eClockToVideoScheduler], 0, 0) != 0)
		ELOG("failed to setup up tunnel from clock to video scheduler!");

	if (ilclient_setup_tunnel(&m_tun[eClockToAudioRender], 0, 0) != 0)
		ELOG("failed to setup up tunnel from clock to audio render!");

	ilclient_change_component_state(m_comp[eClock], OMX_StateExecuting);
	ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle);
	ilclient_change_component_state(m_comp[eVideoFx], OMX_StateIdle);
	ilclient_change_component_state(m_comp[eAudioRender], OMX_StateIdle);

	SetDisplay(display, layer);
	SetClockLatencyTarget();
	SetBufferStallThreshold(20000);
	SetClockReference(cOmx::eClockRefVideo);

	FlushVideo();
	FlushAudio();

	Start();

	return 0;
}

int cOmx::DeInit(void)
{
	Cancel(-1);
	m_portEvents->Add(0);

	for (int i = 0; i < eNumTunnels; i++)
		ilclient_disable_tunnel(&m_tun[i]);

	ilclient_teardown_tunnels(m_tun);
	ilclient_state_transition(m_comp, OMX_StateIdle);
	ilclient_state_transition(m_comp, OMX_StateLoaded);
	ilclient_cleanup_components(m_comp);

	OMX_Deinit();

	ilclient_destroy(m_client);

	return 0;
}

void cOmx::SetBufferStallCallback(void (*onBufferStall)(void*), void* data)
{
	m_onBufferStall = onBufferStall;
	m_onBufferStallData = data;
}

void cOmx::SetEndOfStreamCallback(void (*onEndOfStream)(void*), void* data)
{
	m_onEndOfStream = onEndOfStream;
	m_onEndOfStreamData = data;
}

void cOmx::SetStreamStartCallback(void (*onStreamStart)(void*), void* data)
{
	m_onStreamStart = onStreamStart;
	m_onStreamStartData = data;
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

void cOmx::PtsToTicks(int64_t pts, OMX_TICKS &ticks)
{
	// ticks = pts * OMX_TICKS_PER_SECOND / PTSTICKS
	pts = pts * 100 / 9;
	ticks.nLowPart = pts;
	ticks.nHighPart = pts >> 32;
}

int64_t cOmx::TicksToPts(OMX_TICKS &ticks)
{
	// pts = ticks * PTSTICKS / OMX_TICKS_PER_SECOND
	int64_t pts = ticks.nHighPart;
	pts = (pts << 32) + ticks.nLowPart;
	pts = pts * 9 / 100;
	return pts;
}

int64_t cOmx::GetSTC(void)
{
	int64_t stc = OMX_INVALID_PTS;
	OMX_TIME_CONFIG_TIMESTAMPTYPE timestamp;
	OMX_INIT_STRUCT(timestamp);
	timestamp.nPortIndex = OMX_ALL;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
		OMX_IndexConfigTimeCurrentMediaTime, &timestamp) != OMX_ErrorNone)
		ELOG("failed get current clock reference!");
	else
		stc = TicksToPts(timestamp.nTimestamp);

	return stc;
}

bool cOmx::IsClockRunning(void)
{
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	OMX_INIT_STRUCT(cstate);

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		ELOG("failed get clock state!");

	if (cstate.eState == OMX_TIME_ClockStateRunning)
		return true;
	else
		return false;
}

void cOmx::StartClock(bool waitForVideo, bool waitForAudio)
{
	DBG("StartClock(%svideo, %saudio)",
			waitForVideo ? "" : "no ",
			waitForAudio ? "" : "no ");

	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	OMX_INIT_STRUCT(cstate);

	cstate.eState = OMX_TIME_ClockStateRunning;
	cstate.nOffset = ToOmxTicks(-1000LL * OMX_PRE_ROLL);

	if (waitForVideo)
	{
		cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
		m_setVideoStartTime = true;
		cstate.nWaitMask |= OMX_CLOCKPORT0;
	}
	if (waitForAudio)
	{
		cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
		m_setAudioStartTime = true;
		cstate.nWaitMask |= OMX_CLOCKPORT1;
	}

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		ELOG("failed to start clock!");
}

void cOmx::StopClock(void)
{
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	OMX_INIT_STRUCT(cstate);

	cstate.eState = OMX_TIME_ClockStateStopped;
	cstate.nOffset = ToOmxTicks(-1000LL * OMX_PRE_ROLL);

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		ELOG("failed to stop clock!");
}

void cOmx::SetClockScale(OMX_S32 scale)
{
	if (scale != m_clockScale)
	{
		OMX_TIME_CONFIG_SCALETYPE scaleType;
		OMX_INIT_STRUCT(scaleType);
		scaleType.xScale = scale;

		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeScale, &scaleType) != OMX_ErrorNone)
			ELOG("failed to set clock scale (%d)!", scale);
		else
			m_clockScale = scale;
	}
}

void cOmx::ResetClock(void)
{
	OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
	OMX_INIT_STRUCT(timeStamp);

	if (m_clockReference == eClockRefAudio || m_clockReference == eClockRefNone)
	{
		timeStamp.nPortIndex = 81;
		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeCurrentAudioReference, &timeStamp)
				!= OMX_ErrorNone)
			ELOG("failed to set current audio reference time!");
	}

	if (m_clockReference == eClockRefVideo || m_clockReference == eClockRefNone)
	{
		timeStamp.nPortIndex = 80;
		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigTimeCurrentVideoReference, &timeStamp)
				!= OMX_ErrorNone)
			ELOG("failed to set current video reference time!");
	}
}

unsigned int cOmx::GetAudioLatency(void)
{
	unsigned int ret = 0;

	OMX_PARAM_U32TYPE u32;
	OMX_INIT_STRUCT(u32);
	u32.nPortIndex = 100;

	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
		OMX_IndexConfigAudioRenderingLatency, &u32) != OMX_ErrorNone)
		ELOG("failed get audio render latency!");
	else
		ret = u32.nU32;

	return ret;
}

void cOmx::SetClockReference(eClockReference clockReference)
{
	if (m_clockReference != clockReference)
	{
		OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
		OMX_INIT_STRUCT(refClock);
		refClock.eClock =
			(clockReference == eClockRefAudio) ? OMX_TIME_RefClockAudio :
			(clockReference == eClockRefVideo) ? OMX_TIME_RefClockVideo :
				OMX_TIME_RefClockNone;

		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
				OMX_IndexConfigTimeActiveRefClock, &refClock) != OMX_ErrorNone)
			ELOG("failed set active clock reference!");
		else
			DBG("set active clock reference to %s",
					clockReference == eClockRefAudio ? "audio" :
					clockReference == eClockRefVideo ? "video" : "none");

		m_clockReference = clockReference;
	}
}

void cOmx::SetClockLatencyTarget(void)
{
	OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
	OMX_INIT_STRUCT(latencyTarget);

	// latency target for clock
	// values set according reference implementation in omxplayer
	latencyTarget.nPortIndex = OMX_ALL;
	latencyTarget.bEnabled = OMX_TRUE;
	latencyTarget.nFilter = 10;
	latencyTarget.nTarget = 0;
	latencyTarget.nShift = 3;
	latencyTarget.nSpeedFactor = -60;
	latencyTarget.nInterFactor = 100;
	latencyTarget.nAdjCap = 100;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eClock]),
			OMX_IndexConfigLatencyTarget, &latencyTarget) != OMX_ErrorNone)
		ELOG("failed set clock latency target!");

	// latency target for video render
	// values set according reference implementation in omxplayer
	latencyTarget.nPortIndex = 90;
	latencyTarget.bEnabled = OMX_TRUE;
	latencyTarget.nFilter = 2;
	latencyTarget.nTarget = 4000;
	latencyTarget.nShift = 3;
	latencyTarget.nSpeedFactor = -135;
	latencyTarget.nInterFactor = 500;
	latencyTarget.nAdjCap = 20;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoRender]),
			OMX_IndexConfigLatencyTarget, &latencyTarget) != OMX_ErrorNone)
		ELOG("failed set video render latency target!");
}

void cOmx::SetBufferStallThreshold(int delayMs)
{
	if (delayMs > 0)
	{
		OMX_CONFIG_BUFFERSTALLTYPE stallConf;
		OMX_INIT_STRUCT(stallConf);
		stallConf.nPortIndex = 131;
		stallConf.nDelay = delayMs * 1000;
		if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
				OMX_IndexConfigBufferStall, &stallConf) != OMX_ErrorNone)
			ELOG("failed to set video decoder stall config!");
	}

	// set buffer stall call back
	OMX_CONFIG_REQUESTCALLBACKTYPE reqCallback;
	OMX_INIT_STRUCT(reqCallback);
	reqCallback.nPortIndex = 131;
	reqCallback.nIndex = OMX_IndexConfigBufferStall;
	reqCallback.bEnable = delayMs > 0 ? OMX_TRUE : OMX_FALSE;
	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexConfigRequestCallback, &reqCallback) != OMX_ErrorNone)
		ELOG("failed to set video decoder stall call back!");
}

bool cOmx::IsBufferStall(void)
{
	OMX_CONFIG_BUFFERSTALLTYPE stallConf;
	OMX_INIT_STRUCT(stallConf);
	stallConf.nPortIndex = 131;
	if (OMX_GetConfig(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexConfigBufferStall, &stallConf) != OMX_ErrorNone)
		ELOG("failed to get video decoder stall config!");

	return stallConf.bStalled == OMX_TRUE;
}

void cOmx::SetVolume(int vol)
{
	OMX_AUDIO_CONFIG_VOLUMETYPE volume;
	OMX_INIT_STRUCT(volume);
	volume.nPortIndex = 100;
	volume.bLinear = OMX_TRUE;
	volume.sVolume.nValue = vol * 100 / 255;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexConfigAudioVolume, &volume) != OMX_ErrorNone)
		ELOG("failed to set volume!");
}

void cOmx::SetMute(bool mute)
{
	OMX_AUDIO_CONFIG_MUTETYPE amute;
	OMX_INIT_STRUCT(amute);
	amute.nPortIndex = 100;
	amute.bMute = mute ? OMX_TRUE : OMX_FALSE;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexConfigAudioMute, &amute) != OMX_ErrorNone)
		ELOG("failed to set mute state!");
}

void cOmx::StopVideo(void)
{
	Lock();

	// disable port buffers and allow video decoder to reconfig
	ilclient_disable_port_buffers(m_comp[eVideoDecoder], 130,
			m_spareVideoBuffers, NULL, NULL);

	m_spareVideoBuffers = 0;
	m_handlePortEvents = false;

	m_videoFrameFormat.width = 0;
	m_videoFrameFormat.height = 0;
	m_videoFrameFormat.frameRate = 0;
	m_videoFrameFormat.scanMode = cScanMode::eProgressive;

	// put video decoder into idle
	ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle);

	// put video fx into idle
	ilclient_flush_tunnels(&m_tun[eVideoDecoderToVideoFx], 1);
	ilclient_disable_tunnel(&m_tun[eVideoDecoderToVideoFx]);
	ilclient_change_component_state(m_comp[eVideoFx], OMX_StateIdle);

	// put video scheduler into idle
	ilclient_flush_tunnels(&m_tun[eVideoFxToVideoScheduler], 1);
	ilclient_disable_tunnel(&m_tun[eVideoFxToVideoScheduler]);
	ilclient_flush_tunnels(&m_tun[eClockToVideoScheduler], 1);
	ilclient_disable_tunnel(&m_tun[eClockToVideoScheduler]);
	ilclient_change_component_state(m_comp[eVideoScheduler], OMX_StateIdle);

	// put video render into idle
	ilclient_flush_tunnels(&m_tun[eVideoSchedulerToVideoRender], 1);
	ilclient_disable_tunnel(&m_tun[eVideoSchedulerToVideoRender]);
	ilclient_change_component_state(m_comp[eVideoRender], OMX_StateIdle);

	Unlock();
}

void cOmx::StopAudio(void)
{
	Lock();

	// put audio render onto idle
	ilclient_flush_tunnels(&m_tun[eClockToAudioRender], 1);
	ilclient_disable_tunnel(&m_tun[eClockToAudioRender]);
	ilclient_change_component_state(m_comp[eAudioRender], OMX_StateIdle);
	ilclient_disable_port_buffers(m_comp[eAudioRender], 100,
			m_spareAudioBuffers, NULL, NULL);

	m_spareAudioBuffers = 0;
	Unlock();
}

void cOmx::SetVideoErrorConcealment(bool startWithValidFrame)
{
	OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ectype;
	OMX_INIT_STRUCT(ectype);
	ectype.bStartWithValidFrame = startWithValidFrame ? OMX_TRUE : OMX_FALSE;
	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ectype) != OMX_ErrorNone)
		ELOG("failed to set video decode error concealment failed\n");
}

void cOmx::FlushAudio(void)
{
	Lock();

	if (OMX_SendCommand(ILC_GET_HANDLE(m_comp[eAudioRender]), OMX_CommandFlush, 100, NULL) != OMX_ErrorNone)
		ELOG("failed to flush audio render!");

	ilclient_wait_for_event(m_comp[eAudioRender], OMX_EventCmdComplete,
		OMX_CommandFlush, 0, 100, 0, ILCLIENT_PORT_FLUSH,
		VCOS_EVENT_FLAGS_SUSPEND);

	ilclient_flush_tunnels(&m_tun[eClockToAudioRender], 1);
	Unlock();
}

void cOmx::FlushVideo(bool flushRender)
{
	Lock();

	if (OMX_SendCommand(ILC_GET_HANDLE(m_comp[eVideoDecoder]), OMX_CommandFlush, 130, NULL) != OMX_ErrorNone)
		ELOG("failed to flush video decoder!");

	ilclient_wait_for_event(m_comp[eVideoDecoder], OMX_EventCmdComplete,
		OMX_CommandFlush, 0, 130, 0, ILCLIENT_PORT_FLUSH,
		VCOS_EVENT_FLAGS_SUSPEND);

	ilclient_flush_tunnels(&m_tun[eVideoDecoderToVideoFx], 1);
	ilclient_flush_tunnels(&m_tun[eVideoFxToVideoScheduler], 1);

	if (flushRender)
		ilclient_flush_tunnels(&m_tun[eVideoSchedulerToVideoRender], 1);

	ilclient_flush_tunnels(&m_tun[eClockToVideoScheduler], 1);

	m_setVideoDiscontinuity = true;
	Unlock();
}

int cOmx::SetVideoCodec(cVideoCodec::eCodec codec)
{
	Lock();

	if (ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateIdle) != 0)
		ELOG("failed to set video decoder to idle state!");

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
		ELOG("failed to set video decoder parameters!");

	OMX_PARAM_PORTDEFINITIONTYPE param;
	OMX_INIT_STRUCT(param);
	param.nPortIndex = 130;
	if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		ELOG("failed to get video decoder port parameters!");

	param.nBufferSize = OMX_VIDEO_BUFFERSIZE;
	param.nBufferCountActual = OMX_VIDEO_BUFFERS;
	for (int i = 0; i < BUFFERSTAT_FILTER_SIZE; i++)
		m_usedVideoBuffers[i] = 0;

	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eVideoDecoder]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		ELOG("failed to set video decoder port parameters!");

	// start with valid frames only if codec is MPEG2
	// update: with FW from 2015/01/18 this is not necessary anymore
	SetVideoErrorConcealment(true /*codec == cVideoCodec::eMPEG2*/);

	// update: with FW from 2014/02/04 this is not necessary anymore
	//SetVideoDecoderExtraBuffers(3);

	if (ilclient_enable_port_buffers(m_comp[eVideoDecoder], 130, NULL, NULL, NULL) != 0)
		ELOG("failed to enable port buffer on video decoder!");

	if (ilclient_change_component_state(m_comp[eVideoDecoder], OMX_StateExecuting) != 0)
		ELOG("failed to set video decoder to executing state!");

	// setup clock tunnels first
	if (ilclient_setup_tunnel(&m_tun[eClockToVideoScheduler], 0, 0) != 0)
		ELOG("failed to setup up tunnel from clock to video scheduler!");

	m_handlePortEvents = true;

	Unlock();
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
		ELOG("failed to set video decoder extra buffers!");
}

int cOmx::SetupAudioRender(cAudioCodec::eCodec outputFormat, int channels,
		cRpiAudioPort::ePort audioPort, int samplingRate, int frameSize)
{
	Lock();

	OMX_AUDIO_PARAM_PORTFORMATTYPE format;
	OMX_INIT_STRUCT(format);
	format.nPortIndex = 100;
	if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamAudioPortFormat, &format) != OMX_ErrorNone)
		ELOG("failed to get audio port format parameters!");

	format.eEncoding =
		outputFormat == cAudioCodec::ePCM  ? OMX_AUDIO_CodingPCM :
		outputFormat == cAudioCodec::eMPG  ? OMX_AUDIO_CodingMP3 :
		outputFormat == cAudioCodec::eAC3  ? OMX_AUDIO_CodingDDP :
		outputFormat == cAudioCodec::eEAC3 ? OMX_AUDIO_CodingDDP :
		outputFormat == cAudioCodec::eAAC  ? OMX_AUDIO_CodingAAC :
		outputFormat == cAudioCodec::eDTS  ? OMX_AUDIO_CodingDTS :
				OMX_AUDIO_CodingAutoDetect;

	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamAudioPortFormat, &format) != OMX_ErrorNone)
		ELOG("failed to set audio port format parameters!");

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
			ELOG("failed to set audio render mp3 parameters!");
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
			ELOG("failed to set audio render ddp parameters!");
		break;

	case cAudioCodec::eAAC:
		OMX_AUDIO_PARAM_AACPROFILETYPE aac;
		OMX_INIT_STRUCT(aac);
		aac.nPortIndex = 100;
		aac.nChannels = channels;
		aac.nSampleRate = samplingRate;
		aac.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioAac, &aac) != OMX_ErrorNone)
			ELOG("failed to set audio render aac parameters!");
		break;

	case cAudioCodec::eDTS:
		OMX_AUDIO_PARAM_DTSTYPE dts;
		OMX_INIT_STRUCT(dts);
		dts.nPortIndex = 100;
		dts.nChannels = channels;
		dts.nSampleRate = samplingRate;
		dts.nDtsType = 1;
		dts.nFormat = 3; /* 16bit, LE */
		dts.nDtsFrameSizeBytes = frameSize;
		OMX_AUDIO_CHANNEL_MAPPING(dts, channels);

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioDts, &dts) != OMX_ErrorNone)
			ELOG("failed to set audio render dts parameters!");
		break;

	case cAudioCodec::ePCM:
		OMX_AUDIO_PARAM_PCMMODETYPE pcm;
		OMX_INIT_STRUCT(pcm);
		pcm.nPortIndex = 100;
		pcm.nChannels = channels;
		pcm.eNumData = OMX_NumericalDataSigned;
		pcm.eEndian = OMX_EndianLittle;
		pcm.bInterleaved = OMX_TRUE;
		pcm.nBitPerSample = 16;
		pcm.nSamplingRate = samplingRate;
		pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;
		OMX_AUDIO_CHANNEL_MAPPING(pcm, channels);

		if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
				OMX_IndexParamAudioPcm, &pcm) != OMX_ErrorNone)
			ELOG("failed to set audio render pcm parameters!");
		break;

	default:
		ELOG("output codec not supported: %s!",
				cAudioCodec::Str(outputFormat));
		break;
	}

	OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
	OMX_INIT_STRUCT(audioDest);
	strcpy((char *)audioDest.sName,
			audioPort == cRpiAudioPort::eLocal ? "local" : "hdmi");

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone)
		ELOG("failed to set audio destination!");

	// set up the number and size of buffers for audio render
	OMX_PARAM_PORTDEFINITIONTYPE param;
	OMX_INIT_STRUCT(param);
	param.nPortIndex = 100;
	if (OMX_GetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		ELOG("failed to get audio render port parameters!");

	param.nBufferSize = OMX_AUDIO_BUFFERSIZE;
	param.nBufferCountActual = OMX_AUDIO_BUFFERS;
	for (int i = 0; i < BUFFERSTAT_FILTER_SIZE; i++)
		m_usedAudioBuffers[i] = 0;

	if (OMX_SetParameter(ILC_GET_HANDLE(m_comp[eAudioRender]),
			OMX_IndexParamPortDefinition, &param) != OMX_ErrorNone)
		ELOG("failed to set audio render port parameters!");

	if (ilclient_enable_port_buffers(m_comp[eAudioRender], 100, NULL, NULL, NULL) != 0)
		ELOG("failed to enable port buffer on audio render!");

	ilclient_change_component_state(m_comp[eAudioRender], OMX_StateExecuting);

	if (ilclient_setup_tunnel(&m_tun[eClockToAudioRender], 0, 0) != 0)
		ELOG("failed to setup up tunnel from clock to audio render!");

	Unlock();
	return 0;
}

void cOmx::SetDisplayMode(bool fill, bool noaspect)
{
	OMX_CONFIG_DISPLAYREGIONTYPE region;
	OMX_INIT_STRUCT(region);
	region.nPortIndex = 90;
	region.set = (OMX_DISPLAYSETTYPE)
			(OMX_DISPLAY_SET_MODE | OMX_DISPLAY_SET_NOASPECT);

	region.noaspect = noaspect ? OMX_TRUE : OMX_FALSE;
	region.mode = fill ? OMX_DISPLAY_MODE_FILL : OMX_DISPLAY_MODE_LETTERBOX;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoRender]),
			OMX_IndexConfigDisplayRegion, &region) != OMX_ErrorNone)
		ELOG("failed to set display region!");
}

void cOmx::SetDisplayRegion(int x, int y, int width, int height)
{
	OMX_CONFIG_DISPLAYREGIONTYPE region;
	OMX_INIT_STRUCT(region);
	region.nPortIndex = 90;
	region.set = (OMX_DISPLAYSETTYPE)
			(OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_DEST_RECT);

	region.fullscreen = (!x && !y && !width && !height) ? OMX_TRUE : OMX_FALSE;
	region.dest_rect.x_offset = x;
	region.dest_rect.y_offset = y;
	region.dest_rect.width = width;
	region.dest_rect.height = height;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoRender]),
			OMX_IndexConfigDisplayRegion, &region) != OMX_ErrorNone)
		ELOG("failed to set display region!");
}

void cOmx::SetDisplay(int display, int layer)
{
	OMX_CONFIG_DISPLAYREGIONTYPE region;
	OMX_INIT_STRUCT(region);
	region.nPortIndex = 90;
	region.layer = layer;
	region.num = display;
	region.set = (OMX_DISPLAYSETTYPE)
			(OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoRender]),
			OMX_IndexConfigDisplayRegion, &region) != OMX_ErrorNone)
		ELOG("failed to set display number and layer!");
}

OMX_BUFFERHEADERTYPE* cOmx::GetAudioBuffer(int64_t pts)
{
	Lock();
	OMX_BUFFERHEADERTYPE* buf = 0;
	if (m_spareAudioBuffers)
	{
		buf = m_spareAudioBuffers;
		m_spareAudioBuffers =
				static_cast <OMX_BUFFERHEADERTYPE*>(buf->pAppPrivate);
		buf->pAppPrivate = 0;
	}
	else
	{
		buf = ilclient_get_input_buffer(m_comp[eAudioRender], 100, 0);
		if (buf)
			m_usedAudioBuffers[0]++;
	}

	if (buf)
	{
		buf->nFilledLen = 0;
		buf->nOffset = 0;
		buf->nFlags = 0;

		if (pts == OMX_INVALID_PTS)
			buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
		else if (m_setAudioStartTime)
		{
			buf->nFlags |= OMX_BUFFERFLAG_STARTTIME;
			m_setAudioStartTime = false;
		}
		cOmx::PtsToTicks(pts, buf->nTimeStamp);
	}
	Unlock();
	return buf;
}

OMX_BUFFERHEADERTYPE* cOmx::GetVideoBuffer(int64_t pts)
{
	Lock();
	OMX_BUFFERHEADERTYPE* buf = 0;
	if (m_spareVideoBuffers)
	{
		buf = m_spareVideoBuffers;
		m_spareVideoBuffers =
				static_cast <OMX_BUFFERHEADERTYPE*>(buf->pAppPrivate);
		buf->pAppPrivate = 0;
	}
	else
	{
		buf = ilclient_get_input_buffer(m_comp[eVideoDecoder], 130, 0);
		if (buf)
			m_usedVideoBuffers[0]++;
	}

	if (buf)
	{
		buf->nFilledLen = 0;
		buf->nOffset = 0;
		buf->nFlags = 0;

		if (pts == OMX_INVALID_PTS)
			buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
		else if (m_setVideoStartTime)
		{
			buf->nFlags |= OMX_BUFFERFLAG_STARTTIME;
			m_setVideoStartTime = false;
		}
		if (m_setVideoDiscontinuity)
		{
			buf->nFlags |= OMX_BUFFERFLAG_DISCONTINUITY;
			m_setVideoDiscontinuity = false;
		}
		cOmx::PtsToTicks(pts, buf->nTimeStamp);
	}
	Unlock();
	return buf;
}

#ifdef DEBUG_BUFFERS
void cOmx::DumpBuffer(OMX_BUFFERHEADERTYPE *buf, const char *prefix)
{
	DLOG("%s: TS=%8x%08x, LEN=%5d/%5d: %02x %02x %02x %02x... "
			"FLAGS: %s%s%s%s%s%s%s%s%s%s%s%s%s%s",
		prefix,
		buf->nTimeStamp.nHighPart, buf->nTimeStamp.nLowPart,
		buf->nFilledLen, buf->nAllocLen,
		buf->pBuffer[0], buf->pBuffer[1], buf->pBuffer[2], buf->pBuffer[3],
		buf->nFlags & OMX_BUFFERFLAG_EOS             ? "EOS "           : "",
		buf->nFlags & OMX_BUFFERFLAG_STARTTIME       ? "STARTTIME "     : "",
		buf->nFlags & OMX_BUFFERFLAG_DECODEONLY      ? "DECODEONLY "    : "",
		buf->nFlags & OMX_BUFFERFLAG_DATACORRUPT     ? "DATACORRUPT "   : "",
		buf->nFlags & OMX_BUFFERFLAG_ENDOFFRAME      ? "ENDOFFRAME "    : "",
		buf->nFlags & OMX_BUFFERFLAG_SYNCFRAME       ? "SYNCFRAME "     : "",
		buf->nFlags & OMX_BUFFERFLAG_EXTRADATA       ? "EXTRADATA "     : "",
		buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG     ? "CODECCONFIG "   : "",
		buf->nFlags & OMX_BUFFERFLAG_TIME_UNKNOWN    ? "TIME_UNKNOWN "  : "",
		buf->nFlags & OMX_BUFFERFLAG_CAPTURE_PREVIEW ? "CAPTURE_PREV "  : "",
		buf->nFlags & OMX_BUFFERFLAG_ENDOFNAL        ? "ENDOFNAL "      : "",
		buf->nFlags & OMX_BUFFERFLAG_FRAGMENTLIST    ? "FRAGMENTLIST "  : "",
		buf->nFlags & OMX_BUFFERFLAG_DISCONTINUITY   ? "DISCONTINUITY " : "",
		buf->nFlags & OMX_BUFFERFLAG_CODECSIDEINFO   ? "CODECSIDEINFO " : ""
	);
}
#endif

bool cOmx::EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf)
{
	if (!buf)
		return false;

	Lock();
	bool ret = true;
#ifdef DEBUG_BUFFERS
	DumpBuffer(buf, "A");
#endif

	if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eAudioRender]), buf)
			!= OMX_ErrorNone)
	{
		ELOG("failed to empty OMX audio buffer");

		if (buf->nFlags & OMX_BUFFERFLAG_STARTTIME)
			m_setAudioStartTime = true;

		if (buf->nFlags & OMX_BUFFERFLAG_DISCONTINUITY)
			m_setVideoDiscontinuity = true;

		buf->nFilledLen = 0;
		buf->pAppPrivate = m_spareAudioBuffers;
		m_spareAudioBuffers = buf;
		ret = false;
	}
	Unlock();
	return ret;
}

bool cOmx::EmptyVideoBuffer(OMX_BUFFERHEADERTYPE *buf)
{
	if (!buf)
		return false;

	Lock();
	bool ret = true;
#ifdef DEBUG_BUFFERS
	DumpBuffer(buf, "V");
#endif

	if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[eVideoDecoder]), buf)
			!= OMX_ErrorNone)
	{
		ELOG("failed to empty OMX video buffer");

		if (buf->nFlags & OMX_BUFFERFLAG_STARTTIME)
			m_setVideoStartTime = true;

		buf->nFilledLen = 0;
		buf->pAppPrivate = m_spareVideoBuffers;
		m_spareVideoBuffers = buf;
		ret = false;
	}
	Unlock();
	return ret;
}
