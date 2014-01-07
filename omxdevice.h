/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef OMX_DEVICE_H
#define OMX_DEVICE_H

#include <vdr/device.h>
#include <vdr/thread.h>

class cOmx;
class cAudio;

class cOmxDevice : cDevice
{

public:

	enum eState {
		eStop,
		eStarting,
		ePlay
	};

	cOmxDevice(void (*onPrimaryDevice)(void));
	virtual ~cOmxDevice();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool HasDecoder(void) const { return true; };

	virtual bool SetPlayMode(ePlayMode PlayMode);
	virtual bool CanReplay(void) const;

	virtual int PlayVideo(const uchar *Data, int Length);
	virtual int PlayAudio(const uchar *Data, int Length, uchar Id);

	virtual int64_t GetSTC(void);

	virtual bool Flush(int TimeoutMs = 0);

	virtual bool HasIBPTrickSpeed(void) { return false; }
	virtual void TrickSpeed(int Speed);
	virtual void Clear(void);
	virtual void Play(void);
	virtual void Freeze(void);

	virtual void SetVolumeDevice(int Volume);

	virtual bool Poll(cPoller &Poller, int TimeoutMs = 0);

	virtual void GetOsdSize(int &Width, int &Height, double &PixelAspect)
		{ cOmxDevice::GetDisplaySize(Width, Height, PixelAspect); }

	static void GetDisplaySize(int &width, int &height, double &aspect);

protected:

	virtual void MakePrimaryDevice(bool On);

private:

	void (*m_onPrimaryDevice)(void);

	virtual bool OmxSetVideoCodec(const uchar *data);
	virtual bool OmxSetAudioCodec(const uchar *data);

	cOmx	*m_omx;
	cAudio	*m_audio;
	cMutex	*m_mutex;

	eState	 m_state;

	bool	 m_audioCodecReady;
	bool	 m_videoCodecReady;
};

#endif
