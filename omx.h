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
#include "tools.h"
#include <queue>

extern "C"
{
#include "ilclient.h"
}

#define OMX_INVALID_PTS -1

class cOmxEvents;

class cOmx : public cThread
{

public:

	cOmx();
	int Init(int display, int layer);
	int DeInit(void);

	void SetBufferStallCallback(void (*onBufferStall)(void*), void* data);
	void SetEndOfStreamCallback(void (*onEndOfStream)(void*), void* data);
	void SetStreamStartCallback(void (*onStreamStart)(void*), void* data);

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

	void StartClock(bool waitForVideo = false, bool waitForAudio = false,
			int preRollMs = 0);
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
	void StopVideo(void);
	void StopAudio(void);

	void SetVideoErrorConcealment(bool startWithValidFrame);
	void SetVideoDecoderExtraBuffers(int extraBuffers);

private:
	inline void FlushAudio(void);
public:
	void FlushVideo(bool flushRender = false);

	int SetVideoCodec(cVideoCodec::eCodec codec);
	int SetupAudioRender(cAudioCodec::eCodec outputFormat,
			int channels, cRpiAudioPort::ePort audioPort,
			int samplingRate = 0, int frameSize = 0);

	const cVideoFrameFormat *GetVideoFrameFormat(void) {
		// FIXME: This is being accessed without holding any mutex!
		return &m_videoFrameFormat;
	}

	void SetDisplayMode(bool letterbox, bool noaspect);
	void SetPixelAspectRatio(int width, int height);
	void SetDisplayRegion(int x, int y, int width, int height);
	void SetDisplay(int display, int layer);

	OMX_BUFFERHEADERTYPE* GetAudioBuffer(int64_t pts = OMX_INVALID_PTS);
	OMX_BUFFERHEADERTYPE* GetVideoBuffer(int64_t pts = OMX_INVALID_PTS);

	bool PollVideo(void);

	bool EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf);
	bool EmptyVideoBuffer(OMX_BUFFERHEADERTYPE *buf);

	void GetBufferUsage(int &audio, int &video);

private:
	struct Event
	{
		enum eEvent {
			eShutdown,
			ePortSettingsChanged,
			eConfigChanged,
			eEndOfStream,
			eBufferEmptied
		};
		Event(eEvent _event, int _data)
			: event(_event), data(_data) { };
		eEvent event;
		int data;
	};

	void Add(const Event& event);

	virtual void Action(void);

	static const char* errStr(int err);

#ifdef DEBUG_BUFFERS
	static void DumpBuffer(OMX_BUFFERHEADERTYPE *buf, const char *prefix = "");
#endif

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

	enum eOmxTunnel {
		eVideoDecoderToVideoFx = 0,
		eVideoFxToVideoScheduler,
		eVideoSchedulerToVideoRender,
		eClockToVideoScheduler,
		eClockToAudioRender,
		eNumTunnels
	};

	ILCLIENT_T 	*m_client = nullptr;
	COMPONENT_T	*m_comp[cOmx::eNumComponents + 1];
	TUNNEL_T 	 m_tun[cOmx::eNumTunnels + 1];

	/* Updated by Action() thread;
	read by callers of GetVideoFrameFormat() (without holding a mutex!) */
	cVideoFrameFormat m_videoFrameFormat;

	/* The following fields are protected by cThread::mutex */
	bool m_setAudioStartTime = false;
	bool m_setVideoStartTime = false;
	bool m_setVideoDiscontinuity = false;
	static constexpr size_t BUFFERSTAT_FILTER_SIZE = 64;
	int m_usedAudioBuffers[BUFFERSTAT_FILTER_SIZE];
	int m_usedVideoBuffers[BUFFERSTAT_FILTER_SIZE];

	OMX_BUFFERHEADERTYPE* m_spareAudioBuffers = nullptr;
	OMX_BUFFERHEADERTYPE* m_spareVideoBuffers = nullptr;
	eClockReference	m_clockReference = eClockRefNone;
	OMX_S32 m_clockScale = 0;
	bool m_handlePortEvents = false;

	cMutex m_mutex;

	cCondVar m_portEventsAdded;
	std::queue<Event> m_portEvents;

	/** pointer to cOmxDevice::OnBufferStall(); constant after Init() */
	void (*m_onBufferStall)(void*) = nullptr;
	void *m_onBufferStallData = nullptr;

	/** pointer to cOmxDevice::OnEndOfStream(); constant after Init() */
	void (*m_onEndOfStream)(void*) = nullptr;
	void *m_onEndOfStreamData = nullptr;

	/** pointer to cOmxDevice::OnStreamStart(); constant after Init() */
	void (*m_onStreamStart)(void*) = nullptr;
	void *m_onStreamStartData = nullptr;

	void HandlePortBufferEmptied(eOmxComponent component);
	void HandlePortSettingsChanged(unsigned int portId);
	void SetPARChangeCallback(bool enable);
	void SetBufferStallThreshold(int delayMs);
	bool IsBufferStall(void);

	static void OnBufferEmpty(void *instance, COMPONENT_T *comp);
	static void OnPortSettingsChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnEndOfStream(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnError(void *, COMPONENT_T *, OMX_U32 data);
	static void OnConfigChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);

};

#endif
