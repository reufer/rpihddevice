/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef OMX_H
#define OMX_H

#include "types.h"

extern "C"
{
#include "ilclient.h"
}

class cMutex;

class cOmx
{

public:

	cOmx();
	virtual ~cOmx();
	int Init(void);
	int DeInit(void);

	static OMX_TICKS ToOmxTicks(int64_t val);
	static int64_t FromOmxTicks(OMX_TICKS &ticks);
	static void PtsToTicks(uint64_t pts, OMX_TICKS &ticks);
	static uint64_t TicksToPts(OMX_TICKS &ticks);

	int64_t GetSTC(void);
	bool IsClockRunning(void);

	enum eClockState {
		eClockStateRun,
		eClockStateStop,
		eClockStateWaitForVideo,
		eClockStateWaitForAudio,
		eClockStateWaitForAudioVideo
	};

	void SetClockState(eClockState clockState);
	void SetClockScale(float scale);
	void SetMediaTime(uint64_t pts);
	unsigned int GetMediaTime(void);

	enum eClockReference {
		eClockRefAudio,
		eClockRefVideo
	};

	void SetClockReference(eClockReference clockReference);
	void SetVolume(int vol);
	void SendEos(void);
	void Stop(void);

	void FlushAudio(void);
	void FlushVideo(bool flushRender = false);

	int SetVideoCodec(cVideoCodec::eCodec codec);
	int SetupAudioRender(cAudioCodec::eCodec outputFormat,
			int channels, int samplingRate, cAudioPort::ePort audioPort);

	OMX_BUFFERHEADERTYPE* GetAudioBuffer(uint64_t pts = 0);
	OMX_BUFFERHEADERTYPE* GetVideoBuffer(uint64_t pts = 0);
	bool PollVideoBuffers(int minBuffers = 0);
	bool PollAudioBuffers(int minBuffers = 0);

	bool EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf);
	bool EmptyVideoBuffer(OMX_BUFFERHEADERTYPE *buf);

private:

	static const char* errStr(int err);

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

	ILCLIENT_T 	*m_client;
	COMPONENT_T	*m_comp[cOmx::eNumComponents + 1];
	TUNNEL_T 	 m_tun[cOmx::eNumTunnels + 1];

	cMutex	*m_mutex;

	bool m_setVideoStartTime;
	bool m_setAudioStartTime;
	bool m_setVideoDiscontinuity;

	int m_freeAudioBuffers;
	int m_freeVideoBuffers;

	eClockReference	m_clockReference;

	void HandleEndOfStream(unsigned int portId);
	void HandlePortSettingsChanged(unsigned int portId);
	void HandleBufferEmpty(COMPONENT_T *comp);

	static void OnBufferEmpty(void *instance, COMPONENT_T *comp);
	static void OnPortSettingsChanged(void *instance, COMPONENT_T *comp, unsigned int data);
	static void OnEndOfStream(void *instance, COMPONENT_T *comp, unsigned int data);
	static void OnError(void *instance, COMPONENT_T *comp, unsigned int data);

};

#endif
