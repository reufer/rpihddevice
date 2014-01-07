/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef OMX_DEVICE_H
#define OMX_DEVICE_H

#include <vdr/device.h>

#include "types.h"

class cOmx;
class cAudioDecoder;
class cMutex;

class cOmxDevice : cDevice
{

public:

	cOmxDevice(void (*onPrimaryDevice)(void));
	virtual ~cOmxDevice();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool HasDecoder(void) const { return true; };
	virtual bool CanReplay(void)  const { return true; };

	virtual void GetOsdSize(int &Width, int &Height, double &PixelAspect);

	virtual bool SetPlayMode(ePlayMode PlayMode);

	virtual void StillPicture(const uchar *Data, int Length);

	virtual int PlayAudio(const uchar *Data, int Length, uchar Id);
	virtual int PlayVideo(const uchar *Data, int Length)
		{ return PlayVideo(Data, Length, false); }

	virtual int PlayVideo(const uchar *Data, int Length, bool singleFrame);

	virtual int64_t GetSTC(void);

	virtual bool HasIBPTrickSpeed(void) { return true; }

#if APIVERSNUM >= 20103
	virtual void TrickSpeed(int Speed, bool Forward);
#else
	virtual void TrickSpeed(int Speed);
#endif

	virtual void Clear(void);
	virtual void Play(void);
	virtual void Freeze(void);

	virtual void SetVolumeDevice(int Volume);

	virtual bool Flush(int TimeoutMs = 0);
	virtual bool Poll(cPoller &Poller, int TimeoutMs = 0);

protected:

	virtual void MakePrimaryDevice(bool On);

private:

	void (*m_onPrimaryDevice)(void);
	virtual cVideoCodec::eCodec ParseVideoCodec(const uchar *data, int length);

	void ResetAudioVideo(bool flushVideoRender = false);

	void ApplyTrickSpeed(int trickSpeed, bool forward = true);
	void PtsTracker(int64_t ptsDiff);

	cOmx			*m_omx;
	cAudioDecoder	*m_audio;
	cMutex			*m_mutex;

	cVideoCodec::eCodec	m_videoCodec;

	bool	m_hasVideo;
	bool	m_hasAudio;

	bool	m_skipAudio;
	int		m_playDirection;
	int		m_trickRequest;

	int64_t	m_audioPts;
	int64_t	m_videoPts;
};

#endif
