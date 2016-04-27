/*
 * rpihddevice - VDR HD output device for Raspberry Pi
 * Copyright (C) 2014, 2015, 2016 Thomas Reufer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <queue>

#include "omx.h"
#include "display.h"
#include "setup.h"

#include <vdr/tools.h>
#include <vdr/thread.h>

extern "C" {
#include "ilclient.h"
}

#include "bcm_host.h"

#define OMX_PRE_ROLL 0

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
			Lock();
			switch (event->event)
			{
			case cOmxEvents::ePortSettingsChanged:
				HandlePortSettingsChanged(event->data);

				for (cOmxEventHandler *handler = m_eventHandlers->First();
						handler; handler = m_eventHandlers->Next(handler))
					handler->PortSettingsChanged(event->data);
				break;

			case cOmxEvents::eConfigChanged:
				if (event->data == OMX_IndexConfigBufferStall
						&& IsBufferStall() && !IsClockFreezed())
					for (cOmxEventHandler *handler = m_eventHandlers->First();
							handler; handler = m_eventHandlers->Next(handler))
						handler->BufferStalled();
				break;

			case cOmxEvents::eEndOfStream:
				for (cOmxEventHandler *handler = m_eventHandlers->First();
						handler; handler = m_eventHandlers->Next(handler))
					handler->EndOfStreamReceived(event->data);
				break;

			case cOmxEvents::eBufferEmptied:
				for (cOmxEventHandler *handler = m_eventHandlers->First();
						handler; handler = m_eventHandlers->Next(handler))
					handler->BufferEmptied((eOmxComponent)event->data);
				break;

			default:
				break;
			}
			Unlock();
			delete event;
		}
		cCondWait::SleepMs(10);

		if (timer.TimedOut())
		{
			timer.Set(100);
			for (cOmxEventHandler *handler = m_eventHandlers->First(); handler;
					handler = m_eventHandlers->Next(handler))
				handler->Tick();
		}
	}
}

void cOmx::HandlePortSettingsChanged(unsigned int portId)
{
	Lock();
	DBG("HandlePortSettingsChanged(%d)", portId);

	switch (portId)
	{
	case 191:
		if (!SetupTunnel(eVideoFxToVideoScheduler))
			ELOG("failed to setup up tunnel from video fx to scheduler!");
		if (!ChangeComponentState(eVideoScheduler, OMX_StateExecuting))
			ELOG("failed to enable video scheduler!");
		break;

	case 11:
		if (!SetupTunnel(eVideoSchedulerToVideoRender))
			ELOG("failed to setup up tunnel from scheduler to render!");
		if (!ChangeComponentState(eVideoRender, OMX_StateExecuting))
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
	m_clockReference(eClockRefNone),
	m_clockScale(0),
	m_portEvents(new cOmxEvents()),
	m_eventHandlers(new cList<cOmxEventHandler>)
{
	memset(m_tun, 0, sizeof(m_tun));
	memset(m_comp, 0, sizeof(m_comp));
}

cOmx::~cOmx()
{
	delete m_eventHandlers;
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

	// create video_render
	if (!CreateComponent(eVideoRender))
		ELOG("failed creating video render!");

	//create clock
	if (!CreateComponent(eClock))
		ELOG("failed creating clock!");

	//create video_scheduler
	if (!CreateComponent(eVideoScheduler))
		ELOG("failed creating video scheduler!");

	// set tunnels
	SetTunnel(eVideoSchedulerToVideoRender, eVideoScheduler, 11, eVideoRender, 90);
	SetTunnel(eClockToVideoScheduler, eClock, 80, eVideoScheduler, 12);

	// setup clock tunnels first
	if (!SetupTunnel(eClockToVideoScheduler))
		ELOG("failed to setup up tunnel from clock to video scheduler!");

	ChangeComponentState(eClock, OMX_StateExecuting);

	SetDisplay(display, layer);
	SetClockLatencyTarget();
	SetClockReference(cOmx::eClockRefVideo);

	Start();
	return 0;
}

int cOmx::DeInit(void)
{
	Cancel(-1);
	m_portEvents->Add(0);

	ChangeComponentState(eClock, OMX_StateIdle);

	CleanupComponent(eClock);
	CleanupComponent(eVideoScheduler);
	CleanupComponent(eVideoRender);

	OMX_Deinit();
	ilclient_destroy(m_client);
	return 0;
}

bool cOmx::CreateComponent(eOmxComponent comp, bool enableInputBuffers)
{
	return (comp >= 0 && comp < eNumComponents &&
			!ilclient_create_component(m_client, &m_comp[comp],
					comp == eClock          ? "clock"           :
					comp == eVideoDecoder   ? "video_decode"    :
					comp == eVideoFx        ? "image_fx"        :
					comp == eVideoScheduler ? "video_scheduler" :
					comp == eVideoRender    ? "video_render"    :
					comp == eAudioRender    ? "audio_render"    : "",
					(ILCLIENT_CREATE_FLAGS_T)(ILCLIENT_DISABLE_ALL_PORTS |
					(enableInputBuffers ? ILCLIENT_ENABLE_INPUT_BUFFERS : 0))));
}

void cOmx::CleanupComponent(eOmxComponent comp)
{
	if (comp >= 0 && comp < eNumComponents)
	{
		COMPONENT_T *c[2] = { m_comp[comp], 0 };
		ilclient_cleanup_components(c);
	}
}

bool cOmx::ChangeComponentState(eOmxComponent comp, OMX_STATETYPE state)
{
	return comp >= 0 && comp < eNumComponents &&
			!ilclient_change_component_state(m_comp[comp], state);
}

bool cOmx::FlushComponent(eOmxComponent comp, int port)
{
	if (comp >= 0 && comp < eNumComponents)
	{
		if (OMX_SendCommand(ILC_GET_HANDLE(m_comp[comp]), OMX_CommandFlush,
				port, NULL) == OMX_ErrorNone)
		{
			ilclient_wait_for_event(m_comp[comp], OMX_EventCmdComplete,
				OMX_CommandFlush, 0, port, 0, ILCLIENT_PORT_FLUSH,
				VCOS_EVENT_FLAGS_SUSPEND);

			return true;
		}
	}
	return false;
}

bool cOmx::EnablePortBuffers(eOmxComponent comp, int port)
{
	return comp >= 0 && comp < eNumComponents &&
			!ilclient_enable_port_buffers(m_comp[comp], port, NULL, NULL, NULL);
}

void cOmx::DisablePortBuffers(eOmxComponent comp, int port,
		OMX_BUFFERHEADERTYPE *buffers)
{
	if (comp >= 0 && comp < eNumComponents)
		ilclient_disable_port_buffers(m_comp[comp], port, buffers, NULL, NULL);
}

OMX_BUFFERHEADERTYPE* cOmx::GetBuffer(eOmxComponent comp, int port)
{
	return (comp >= 0 && comp < eNumComponents) ?
			ilclient_get_input_buffer(m_comp[comp], port, 0) : 0;
}

bool cOmx::EmptyBuffer(eOmxComponent comp, OMX_BUFFERHEADERTYPE *buf)
{
	return buf && comp >= 0 && comp < eNumComponents &&
			(OMX_EmptyThisBuffer(ILC_GET_HANDLE(m_comp[comp]), buf)
					== OMX_ErrorNone);
}

bool cOmx::GetParameter(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR param)
{
	return param && comp >= 0 && comp < eNumComponents &&
			(OMX_GetParameter(ILC_GET_HANDLE(m_comp[comp]), index, param)
					== OMX_ErrorNone);
}

bool cOmx::SetParameter(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR param)
{
	return param && comp >= 0 && comp < eNumComponents &&
			(OMX_SetParameter(ILC_GET_HANDLE(m_comp[comp]), index, param)
					== OMX_ErrorNone);
}

bool cOmx::GetConfig(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR config)
{
	return config && comp >= 0 && comp < eNumComponents &&
			(OMX_GetConfig(ILC_GET_HANDLE(m_comp[comp]), index, config)
					== OMX_ErrorNone);
}

bool cOmx::SetConfig(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR config)
{
	return config && comp >= 0 && comp < eNumComponents &&
			(OMX_SetConfig(ILC_GET_HANDLE(m_comp[comp]), index, config)
					== OMX_ErrorNone);
}

void cOmx::SetTunnel(eOmxTunnel tunnel, eOmxComponent srcComp, int srcPort,
		eOmxComponent dstComp, int dstPort)
{
	if (tunnel >= 0 && tunnel < eNumTunnels && srcComp >= 0 && srcComp
			< eNumComponents && dstComp >= 0 && dstComp < eNumComponents)
		set_tunnel(&m_tun[tunnel],
			m_comp[srcComp], srcPort, m_comp[dstComp], dstPort);
}

bool cOmx::SetupTunnel(eOmxTunnel tunnel, int timeout)
{
	return tunnel >= 0 && tunnel < eNumTunnels &&
			!ilclient_setup_tunnel(&m_tun[tunnel], 0, timeout);
}

void cOmx::DisableTunnel(eOmxTunnel tunnel)
{
	if (tunnel >= 0 && tunnel < eNumTunnels)
		ilclient_disable_tunnel(&m_tun[tunnel]);
}

void cOmx::TeardownTunnel(eOmxTunnel tunnel)
{
	if (tunnel >= 0 && tunnel < eNumTunnels)
	{
		TUNNEL_T t[2] = { m_tun[tunnel], { 0, 0, 0, 0 } };
		ilclient_teardown_tunnels(t);
	}
}

void cOmx::FlushTunnel(eOmxTunnel tunnel)
{
	if (tunnel >= 0 && tunnel < eNumTunnels)
		ilclient_flush_tunnels(&m_tun[tunnel], 1);
}

void cOmx::AddEventHandler(cOmxEventHandler *handler)
{
	Lock();
	m_eventHandlers->Add(handler);
	Unlock();
}

void cOmx::RemoveEventHandler(cOmxEventHandler *handler)
{
	Lock();
	m_eventHandlers->Del(handler, false);
	Unlock();
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
		cstate.nWaitMask |= OMX_CLOCKPORT0;
	}
	if (waitForAudio)
	{
		cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
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

void cOmx::SetPixelAspectRatio(int width, int height)
{
	OMX_CONFIG_DISPLAYREGIONTYPE region;
	OMX_INIT_STRUCT(region);
	region.nPortIndex = 90;
	region.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_PIXEL);

	region.pixel_x = width;
	region.pixel_y = height;

	if (OMX_SetConfig(ILC_GET_HANDLE(m_comp[eVideoRender]),
			OMX_IndexConfigDisplayRegion, &region) != OMX_ErrorNone)
		ELOG("failed to set pixel apect ratio!");
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
