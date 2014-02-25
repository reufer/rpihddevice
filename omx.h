/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef OMX_H
#define OMX_H

#include <queue>
#include <vdr/thread.h>
#include "tools.h"

extern "C"
{
#include "ilclient.h"
}

class cOmx : public cThread
{

public:

	cOmx();
	virtual ~cOmx();
	int Init(void);
	int DeInit(void);

	void SetBufferStallCallback(void (*onBufferStall)(void*), void* data);

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
	void GetClockScale(OMX_S32 &scale);
	void SetStartTime(uint64_t pts);
	void SetCurrentReferenceTime(uint64_t pts);
	unsigned int GetMediaTime(void);

	enum eClockReference {
		eClockRefAudio,
		eClockRefVideo,
		eClockRefNone
	};

	void SetClockReference(eClockReference clockReference);
	void SetClockLatencyTarget(void);
	void SetVolume(int vol);
	void SetMute(bool mute);
	void SendEos(void);
	void StopVideo(void);
	void StopAudio(void);

	enum eDataUnitType {
		eCodedPicture,
		eArbitraryStreamSection
	};

	void SetVideoDataUnitType(eDataUnitType dataUnitType);
	void SetVideoErrorConcealment(bool startWithValidFrame);
	void SetVideoDecoderExtraBuffers(int extraBuffers);

	void FlushAudio(void);
	void FlushVideo(bool flushRender = false);

	int SetVideoCodec(cVideoCodec::eCodec codec,
			eDataUnitType dataUnit = eArbitraryStreamSection);
	int SetupAudioRender(cAudioCodec::eCodec outputFormat,
			int channels, cAudioPort::ePort audioPort, int samplingRate = 0);
	void GetVideoSize(int &width, int &height, bool &interlaced);

	void SetDisplayMode(bool letterbox, bool noaspect);
	void SetDisplayRegion(int x, int y, int width, int height);

	OMX_BUFFERHEADERTYPE* GetAudioBuffer(uint64_t pts = 0);
	OMX_BUFFERHEADERTYPE* GetVideoBuffer(uint64_t pts = 0);
	bool PollVideoBuffers(int minBuffers = 0);
	bool PollAudioBuffers(int minBuffers = 0);

	bool EmptyAudioBuffer(OMX_BUFFERHEADERTYPE *buf);
	bool EmptyVideoBuffer(OMX_BUFFERHEADERTYPE *buf);

private:

	virtual void Action(void);

	static const char* errStr(int err);

	enum eOmxComponent {
		eClock = 0,
		eVideoDecoder,
		eVideoFx,
		eVideoScheduler,
		eVideoRender,
		eAudioRender,
		eNumComponents
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

	int m_videoWidth;
	int m_videoHeight;
	bool m_videoInterlaced;

	bool m_setAudioStartTime;
	bool m_setVideoStartTime;
	bool m_setVideoDiscontinuity;

	int m_freeAudioBuffers;
	int m_freeVideoBuffers;

	eClockReference	m_clockReference;
	OMX_S32 m_clockScale;

	cCondWait *m_eventReady;
	std::queue<unsigned int> *m_portEvents;
	bool m_stallEvent;
	void (*m_onBufferStall)(void*);
	void *m_onBufferStallData;

	void HandlePortSettingsChanged(unsigned int portId);
	void SetBufferStall(int delayMs);
	bool IsBufferStall(void);

	static void OnBufferEmpty(void *instance, COMPONENT_T *comp);
	static void OnPortSettingsChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnEndOfStream(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnError(void *instance, COMPONENT_T *comp, OMX_U32 data);
	static void OnConfigChanged(void *instance, COMPONENT_T *comp, OMX_U32 data);

};

#endif
