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
	virtual ~cOmx();
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

	void FlushAudio(void);
	void FlushVideo(bool flushRender = false);

	int SetVideoCodec(cVideoCodec::eCodec codec);
	int SetupAudioRender(cAudioCodec::eCodec outputFormat,
			int channels, cRpiAudioPort::ePort audioPort,
			int samplingRate = 0, int frameSize = 0);

	const cVideoFrameFormat *GetVideoFrameFormat(void) {
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

	ILCLIENT_T 	*m_client;
	COMPONENT_T	*m_comp[cOmx::eNumComponents + 1];
	TUNNEL_T 	 m_tun[cOmx::eNumTunnels + 1];

	cVideoFrameFormat m_videoFrameFormat;

	bool m_setAudioStartTime;
	bool m_setVideoStartTime;
	bool m_setVideoDiscontinuity;

#define BUFFERSTAT_FILTER_SIZE 64

	int m_usedAudioBuffers[BUFFERSTAT_FILTER_SIZE];
	int m_usedVideoBuffers[BUFFERSTAT_FILTER_SIZE];

	OMX_BUFFERHEADERTYPE* m_spareAudioBuffers;
	OMX_BUFFERHEADERTYPE* m_spareVideoBuffers;

	eClockReference	m_clockReference;
	OMX_S32 m_clockScale;

	cOmxEvents *m_portEvents;
	bool m_handlePortEvents;

	void (*m_onBufferStall)(void*);
	void *m_onBufferStallData;

	void (*m_onEndOfStream)(void*);
	void *m_onEndOfStreamData;

	void (*m_onStreamStart)(void*);
	void *m_onStreamStartData;

	void HandlePortBufferEmptied(eOmxComponent component);
	void HandlePortSettingsChanged(unsigned int portId);
	void SetPARChangeCallback(bool enable);
	void SetBufferStallThreshold(int delayMs);
	bool IsBufferStall(void);

	static void OnBufferEmpty(void *instance, COMPONENT_T *comp);
	static void OnPortSettingsChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnEndOfStream(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnError(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnConfigChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);

};

#endif
