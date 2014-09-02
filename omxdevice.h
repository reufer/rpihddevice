/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef OMX_DEVICE_H
#define OMX_DEVICE_H

#include <vdr/device.h>

#include "tools.h"

class cOmx;
class cRpiAudioDecoder;
class cMutex;

class cOmxDevice : cDevice
{

public:

	cOmxDevice(void (*onPrimaryDevice)(void));
	virtual ~cOmxDevice();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool HasDecoder(void) const { return true; }
	virtual bool CanReplay(void)  const { return true; }
	virtual bool HasIBPTrickSpeed(void);

	virtual void GetOsdSize(int &Width, int &Height, double &PixelAspect);
	virtual void GetVideoSize(int &Width, int &Height, double &VideoAspect);

	virtual cRect CanScaleVideo(const cRect &Rect, int Alignment = taCenter)
		{ return Rect; }
	virtual void ScaleVideo(const cRect &Rect = cRect::Null);

	virtual bool SetPlayMode(ePlayMode PlayMode);

	virtual void StillPicture(const uchar *Data, int Length);

	virtual int PlayAudio(const uchar *Data, int Length, uchar Id);
	virtual int PlayVideo(const uchar *Data, int Length)
		{ return PlayVideo(Data, Length, false); }

	virtual int PlayVideo(const uchar *Data, int Length, bool EndOfStream);

	virtual int64_t GetSTC(void);

	virtual uchar *GrabImage(int &Size, bool Jpeg = true, int Quality = -1,
			int SizeX = -1, int SizeY = -1);

#if APIVERSNUM >= 20103
	virtual void TrickSpeed(int Speed, bool Forward);
#else
	virtual void TrickSpeed(int Speed);
#endif

	virtual void Clear(void);
	virtual void Play(void);
	virtual void Freeze(void);

	virtual void SetVolumeDevice(int Volume);

	virtual bool Poll(cPoller &Poller, int TimeoutMs = 0);

protected:

	virtual void MakePrimaryDevice(bool On);

	enum eDirection {
		eForward,
		eBackward
	};

	static const char* DirectionStr(eDirection dir) {
		return 	dir == eForward ? "forward" :
				dir == eBackward ? "backward" : "unknown";
	}

	enum eSpeed {
		ePause,
		eSlowest,
		eSlower,
		eSlow,
		eNormal,
		eFast,
		eFaster,
		eFastest
	};

	static const char* SpeedStr(eSpeed speed) {
		return 	speed == ePause   ? "pause"   :
				speed == eSlowest ? "slowest" :
				speed == eSlower  ? "slower"  :
				speed == eSlow    ? "slow"    :
				speed == eNormal  ? "normal"  :
				speed == eFast    ? "fast"    :
				speed == eFaster  ? "faster"  :
				speed == eFastest ? "fastest" : "unknown";
	}

	enum eSpeedCorrection {
		eNegMaxCorrection,
		eNegCorrection,
		eNoCorrection,
		ePosCorrection,
		ePosMaxCorrection,
	};

	static const char* SpeedCorrectionStr(eSpeedCorrection corr) {
		return	corr == eNegMaxCorrection ? "max negative" :
				corr == eNegCorrection    ? "negative"     :
				corr == eNoCorrection     ? "no"           :
				corr == ePosCorrection    ? "positive"     :
				corr == ePosMaxCorrection ? "max positive" : "unknown";
	}

	static int s_speeds[2][8];
	static int s_speedCorrections[5];

private:

	void (*m_onPrimaryDevice)(void);
	virtual cVideoCodec::eCodec ParseVideoCodec(const uchar *data, int length);

	static void OnBufferStall(void *data)
		{ (static_cast <cOmxDevice*> (data))->HandleBufferStall(); }

	static void OnEndOfStream(void *data)
		{ (static_cast <cOmxDevice*> (data))->HandleEndOfStream(); }

	static void OnVideoSetupChanged(void *data)
		{ (static_cast <cOmxDevice*> (data))->HandleVideoSetupChanged(); }

	void HandleBufferStall();
	void HandleEndOfStream();
	void HandleVideoSetupChanged();

	void FlushStreams(bool flushVideoRender = false);

	void ApplyTrickSpeed(int trickSpeed, bool forward);
	void PtsTracker(int64_t ptsDiff);

	void UpdateLatency(int64_t pts);

	cOmx			 *m_omx;
	cRpiAudioDecoder *m_audio;
	cMutex			 *m_mutex;

	cVideoCodec::eCodec	m_videoCodec;

	eSpeed		m_speed;
	eDirection	m_direction;

	bool	m_hasVideo;
	bool	m_hasAudio;

	bool	m_skipAudio;
	int		m_playDirection;
	int		m_trickRequest;

	int64_t	m_audioPts;
	int64_t	m_videoPts;
	int64_t m_latency;
};

#endif
