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
		ePlay
	};

	enum eClockState {
		eClockStateRunning,
		eClockStateStopped,
		eClockStateWaiting
	};

	cOmxDevice(void (*onPrimaryDevice)(void));
	virtual ~cOmxDevice();

	virtual bool HasDecoder(void) const { return true; };

	virtual bool SetPlayMode(ePlayMode PlayMode);

	virtual bool CanReplay(void) const;

	virtual int PlayVideo(const uchar *Data, int Length);
	virtual int PlayAudio(const uchar *Data, int Length, uchar Id);

	virtual int64_t GetSTC(void);

	virtual void Play(void);

	virtual bool Flush(int TimeoutMs = 0);
	
	virtual void HandlePortSettingsChanged(unsigned int portId);
	virtual void HandleEndOfStream(unsigned int portId);

	virtual int OmxInit();
	virtual int OmxDeInit();

	virtual void GetOsdSize(int &Width, int &Height, double &PixelAspect)
		{ cOmxDevice::GetDisplaySize(Width, Height, PixelAspect); }

	static void GetDisplaySize(int &width, int &height, double &aspect);

protected:

	virtual void MakePrimaryDevice(bool On);

private:

	void (*m_onPrimaryDevice)(void);

	virtual void Start(void);
	virtual void Stop(void);

	virtual void SetClockScale(int scale);
	virtual void SetClockState(eClockState clockState);

	virtual void OmxSetVideoCodec(const uchar *data);
	virtual void OmxSetAudioCodec(const uchar *data);

	cOmx 	  *m_omx;
	cAudio 	  *m_audio;
	cCondWait *m_eosEvent;
	cMutex	  *m_mutex;

	eState 	   m_state;

	bool m_firstVideoPacket;
	bool m_firstAudioPacket;

};

#endif
