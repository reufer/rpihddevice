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

#ifndef OMX_H
#define OMX_H

#include <vdr/thread.h>
#include <vdr/tools.h>
#include "tools.h"

extern "C" {
#include "ilclient.h"
}

#define OMX_INVALID_PTS -1

#define OMX_INIT_STRUCT(a) \
	memset(&(a), 0, sizeof(a)); \
	(a).nSize = sizeof(a); \
	(a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
	(a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
	(a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
	(a).nVersion.s.nStep = OMX_VERSION_STEP

class cOmxEvents;
class cOmxEventHandler;

/* ------------------------------------------------------------------------- */

class cOmx : public cThread
{

public:

	cOmx();
	virtual ~cOmx();
	int Init(int display, int layer);
	int DeInit(void);

	void AddEventHandler(cOmxEventHandler *handler);
	void RemoveEventHandler(cOmxEventHandler *handler);

	enum eOmxComponent {
		eClock = 0,
		eVideoDecoder,
		eVideoFx,
		eVideoScheduler,
		eVideoRender,
		eAudioRender,
		eNumComponents,
		eInvalidComponent
	};

	bool CreateComponent(eOmxComponent comp, bool enableInputBuffers = false);
	void CleanupComponent(eOmxComponent comp);
	bool ChangeComponentState(eOmxComponent comp, OMX_STATETYPE state);
	bool FlushComponent(eOmxComponent comp, int port);

	bool EnablePortBuffers(eOmxComponent comp, int port);
	void DisablePortBuffers(eOmxComponent comp, int port,
			OMX_BUFFERHEADERTYPE *buffers = 0);

	OMX_BUFFERHEADERTYPE* GetBuffer(eOmxComponent comp, int port);
	bool EmptyBuffer(eOmxComponent comp, OMX_BUFFERHEADERTYPE *buf);

	bool GetParameter(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR param);
	bool SetParameter(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR param);

	bool GetConfig(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR config);
	bool SetConfig(eOmxComponent comp, OMX_INDEXTYPE index, OMX_PTR config);

	enum eOmxTunnel {
		eVideoDecoderToVideoFx = 0,
		eVideoFxToVideoScheduler,
		eVideoSchedulerToVideoRender,
		eClockToVideoScheduler,
		eClockToAudioRender,
		eNumTunnels
	};

	void SetTunnel(eOmxTunnel tunnel, eOmxComponent srcComp, int srcPort,
			eOmxComponent dstComp, int dstPort);

	bool SetupTunnel(eOmxTunnel tunnel, int timeout = 0);
	void DisableTunnel(eOmxTunnel tunnel);
	void TeardownTunnel(eOmxTunnel tunnel);
	void FlushTunnel(eOmxTunnel tunnel);

	static OMX_TICKS ToOmxTicks(int64_t val);
	static int64_t FromOmxTicks(OMX_TICKS &ticks);
	static void PtsToTicks(int64_t pts, OMX_TICKS &ticks);
	static int64_t TicksToPts(OMX_TICKS &ticks);

	int64_t GetSTC(void);
	bool IsClockRunning(void);

	enum eClockState {
		eClockStateRun,
		eClockStateStop,
		eClockStateWaitForVideo,
		eClockStateWaitForAudio,
		eClockStateWaitForAudioVideo
	};

	void StartClock(bool waitForVideo = false, bool waitForAudio = false);
	void StopClock(void);
	void ResetClock(void);

	void SetClockScale(OMX_S32 scale);
	bool IsClockFreezed(void) { return m_clockScale == 0; }
	unsigned int GetAudioLatency(void);

	enum eClockReference {
		eClockRefAudio,
		eClockRefVideo,
		eClockRefNone
	};

	void SetClockReference(eClockReference clockReference);
	void SetClockLatencyTarget(void);
	void SetVolume(int vol);
	void SetMute(bool mute);
	void StopAudio(void);

	void FlushAudio(void);

	int SetupAudioRender(cAudioCodec::eCodec outputFormat,
			int channels, cRpiAudioPort::ePort audioPort,
			int samplingRate = 0, int frameSize = 0);

	void SetDisplayMode(bool letterbox, bool noaspect);
	void SetPixelAspectRatio(int width, int height);
	void SetDisplayRegion(int x, int y, int width, int height);
	void SetDisplay(int display, int layer);

	OMX_BUFFERHEADERTYPE* GetAudioBuffer(int64_t pts = OMX_INVALID_PTS);

	bool EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf);

	void GetBufferUsage(int &audio, int &video);

private:

	virtual void Action(void);

	static const char* errStr(int err);

#ifdef DEBUG_BUFFERS
	static void DumpBuffer(OMX_BUFFERHEADERTYPE *buf, const char *prefix = "");
#endif

	ILCLIENT_T 	*m_client;
	COMPONENT_T	*m_comp[cOmx::eNumComponents + 1];
	TUNNEL_T 	 m_tun[cOmx::eNumTunnels + 1];

	bool m_setAudioStartTime;

#define BUFFERSTAT_FILTER_SIZE 64

	int m_usedAudioBuffers[BUFFERSTAT_FILTER_SIZE];
	int m_usedVideoBuffers[BUFFERSTAT_FILTER_SIZE];

	OMX_BUFFERHEADERTYPE* m_spareAudioBuffers;

	eClockReference	m_clockReference;
	OMX_S32 m_clockScale;

	cOmxEvents *m_portEvents;

	cList<cOmxEventHandler> *m_eventHandlers;

	void HandlePortBufferEmptied(eOmxComponent component);
	void HandlePortSettingsChanged(unsigned int portId);
	bool IsBufferStall(void);

	static void OnBufferEmpty(void *instance, COMPONENT_T *comp);
	static void OnPortSettingsChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnEndOfStream(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnError(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnConfigChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);

};

/* ------------------------------------------------------------------------- */

class cOmxEventHandler : public cListObject
{

public:

	virtual void Tick(void) { };
	virtual void PortSettingsChanged(int port) { };
	virtual void EndOfStreamReceived(int port) { };
	virtual void BufferEmptied(cOmx::eOmxComponent comp) { };
	virtual void BufferStalled(void) { };
};

#endif
