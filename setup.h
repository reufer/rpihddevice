/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef SETUP_H
#define SETUP_H

#include "omx.h"
#include "tools.h"

#define VC_DISPLAY_DEFAULT     0
#define VC_DISPLAY_LCD         4
#define VC_DISPLAY_TV_HDMI     5
#define VC_DISPLAY_NON_DEFAULT 6

class cRpiSetup
{

public:

	struct AudioParameters
	{
		AudioParameters() :
			port(0),
			format(0) { }

		int port;
		int format;

		bool operator!=(const AudioParameters& a) {
			return (a.port != port) || (a.format != format);
		}
	};

	struct VideoParameters
	{
		VideoParameters() :
			framing(0),
			resolution(0),
			frameRate(0) { }

		int framing;
		int resolution;
		int frameRate;

		bool operator!=(const VideoParameters& a) {
			return (a.framing != framing) || (a.resolution != resolution) ||
					(a.frameRate != frameRate);
		}
	};

	struct OsdParameters
	{
		OsdParameters() :
			accelerated(1) { }

		int accelerated;

		bool operator!=(const OsdParameters& a) {
			return (a.accelerated != accelerated);
		}
	};

	struct PluginParameters
	{
		PluginParameters() :
			hasOsd(true), display(0), videoLayer(0), osdLayer(2) { }

		bool hasOsd;
		int display;
		int videoLayer;
		int osdLayer;
	};

	static bool HwInit(void);

	static cRpiAudioPort::ePort GetAudioPort(void) {
		return (GetInstance()->m_audio.port) ?
				cRpiAudioPort::eHDMI : cRpiAudioPort::eLocal; }

	static cAudioFormat::eFormat GetAudioFormat(void) {
		return  GetInstance()->m_audio.format == 0 ? cAudioFormat::ePassThrough :
				GetInstance()->m_audio.format == 1 ? cAudioFormat::eMultiChannelPCM :
						cAudioFormat::eStereoPCM;
	}

	static cVideoFraming::eFraming GetVideoFraming(void) {
		return GetInstance()->m_video.framing == 0 ? cVideoFraming::eFrame :
			   GetInstance()->m_video.framing == 1 ? cVideoFraming::eCut :
					   cVideoFraming::eStretch;
	}

	static cVideoResolution::eResolution GetVideoResolution(void) {
		return	GetInstance()->m_video.resolution == 1 ? cVideoResolution::eFollowVideo :
				GetInstance()->m_video.resolution == 2 ? cVideoResolution::e480 :
				GetInstance()->m_video.resolution == 3 ? cVideoResolution::e576 :
				GetInstance()->m_video.resolution == 4 ? cVideoResolution::e720 :
				GetInstance()->m_video.resolution == 5 ? cVideoResolution::e1080 :
						cVideoResolution::eDontChange;
	}

	static cVideoFrameRate::eFrameRate GetVideoFrameRate(void) {
		return 	GetInstance()->m_video.frameRate == 1 ? cVideoFrameRate::eFollowVideo :
				GetInstance()->m_video.frameRate == 2 ? cVideoFrameRate::e24p :
				GetInstance()->m_video.frameRate == 3 ? cVideoFrameRate::e25p :
				GetInstance()->m_video.frameRate == 4 ? cVideoFrameRate::e30p :
				GetInstance()->m_video.frameRate == 5 ? cVideoFrameRate::e50i :
				GetInstance()->m_video.frameRate == 6 ? cVideoFrameRate::e50p :
				GetInstance()->m_video.frameRate == 7 ? cVideoFrameRate::e60i :
				GetInstance()->m_video.frameRate == 8 ? cVideoFrameRate::e60p :
						cVideoFrameRate::eDontChange;
	}

	static bool IsAudioFormatSupported(cAudioCodec::eCodec codec,
			int channels, int samplingRate);

	static bool IsVideoCodecSupported(cVideoCodec::eCodec codec) {
		return codec == cVideoCodec::eMPEG2 ? GetInstance()->m_mpeg2Enabled :
			   codec == cVideoCodec::eH264 ? true : false;
	}

	static bool IsHighLevelOsd(void) {
		return GetInstance()->m_osd.accelerated != 0;
	}

	static bool HasOsd(void) {
		return GetInstance()->m_plugin.hasOsd;
	}

	static int Display(void) {
		return GetInstance()->m_plugin.display;
	}

	static int VideoLayer(void) {
		return GetInstance()->m_plugin.videoLayer;
	}

	static int OsdLayer(void) {
		return GetInstance()->m_plugin.osdLayer;
	}

	static void SetHDMIChannelMapping(bool passthrough, int channels);

	static cRpiSetup* GetInstance(void);
	static void DropInstance(void);

	class cMenuSetupPage* GetSetupPage(void);
	bool Parse(const char *name, const char *value);

	void Set(AudioParameters audio, VideoParameters video, OsdParameters osd);

	static void SetAudioSetupChangedCallback(void (*callback)(void*), void* data = 0);
	static void SetVideoSetupChangedCallback(void (*callback)(void*), void* data = 0);

	bool ProcessArgs(int argc, char *argv[]);
	const char *CommandLineHelp(void);

private:

	cRpiSetup() :
		m_mpeg2Enabled(false),
		m_onAudioSetupChanged(0),
		m_onAudioSetupChangedData(0),
		m_onVideoSetupChanged(0),
		m_onVideoSetupChangedData(0)
	{ }

	virtual ~cRpiSetup() { };

	static cRpiSetup* s_instance;

	AudioParameters  m_audio;
	VideoParameters  m_video;
	OsdParameters    m_osd;
	PluginParameters m_plugin;

	bool m_mpeg2Enabled;

	void (*m_onAudioSetupChanged)(void*);
	void *m_onAudioSetupChangedData;

	void (*m_onVideoSetupChanged)(void*);
	void *m_onVideoSetupChangedData;
};

#endif
