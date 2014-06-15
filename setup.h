/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef SETUP_H
#define SETUP_H

#include "omx.h"
#include "tools.h"

class cRpiSetup
{

public:

	struct AudioParameters
	{
		AudioParameters() :
			port(0),
			passthrough(0),
			ignoreEDID(0) { }

		int port;
		int passthrough;
		int ignoreEDID;

		bool operator!=(const AudioParameters& a) {
			return (a.port != port) || (a.passthrough != passthrough) ||
					(a.ignoreEDID != ignoreEDID);
		}
	};

	struct VideoParameters
	{
		VideoParameters() :
			framing(0) { }

		int framing;

		bool operator!=(const VideoParameters& a) {
			return (a.framing != framing);
		}
	};

	static bool HwInit(void);

	static cRpiAudioPort::ePort GetAudioPort(void) {
		return (GetInstance()->m_audio.port) ?
				cRpiAudioPort::eHDMI : cRpiAudioPort::eLocal; }

	static bool IsAudioPassthrough(void) {
		return GetInstance()->m_audio.passthrough; }

	static bool IgnoreAudioEDID(void) {
		return GetInstance()->m_audio.ignoreEDID; }

	static cVideoFraming::eFraming GetVideoFraming(void) {
		return GetInstance()->m_video.framing == 0 ? cVideoFraming::eFrame :
			   GetInstance()->m_video.framing == 1 ? cVideoFraming::eCut :
					   cVideoFraming::eStretch;
	}

	static bool IsAudioFormatSupported(cAudioCodec::eCodec codec,
			int channels, int samplingRate);

	static bool IsVideoCodecSupported(cVideoCodec::eCodec codec) {
		return codec == cVideoCodec::eMPEG2 ? GetInstance()->m_mpeg2Enabled :
			   codec == cVideoCodec::eH264 ? true : false;
	}

	static int GetDisplaySize(int &width, int &height, double &aspect);

	static bool IsDisplayProgressive(void) {
		return GetInstance()->m_isProgressive; }

	static cRpiSetup* GetInstance(void);
	static void DropInstance(void);

	class cMenuSetupPage* GetSetupPage(void);
	bool Parse(const char *name, const char *value);

	void Set(AudioParameters audio, VideoParameters video);

	static void SetAudioSetupChangedCallback(void (*callback)(void*), void* data = 0);
	static void SetVideoSetupChangedCallback(void (*callback)(void*), void* data = 0);

private:

	cRpiSetup() :
		m_mpeg2Enabled(false),
		m_isProgressive(false),
		m_displayHeight(0),
		m_displayWidth(0),
		m_onAudioSetupChanged(0),
		m_onAudioSetupChangedData(0),
		m_onVideoSetupChanged(0),
		m_onVideoSetupChangedData(0)
	{ }

	virtual ~cRpiSetup() { }

	static cRpiSetup* s_instance;

	AudioParameters m_audio;
	VideoParameters m_video;

	bool m_mpeg2Enabled;
	bool m_isProgressive;

	int m_displayHeight;
	int m_displayWidth;

	void (*m_onAudioSetupChanged)(void*);
	void *m_onAudioSetupChangedData;

	void (*m_onVideoSetupChanged)(void*);
	void *m_onVideoSetupChangedData;
};

#endif
