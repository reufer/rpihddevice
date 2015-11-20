/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "setup.h"
#include "display.h"
#include "ovgosd.h"

#include <vdr/tools.h>
#include <vdr/menuitems.h>

#include <getopt.h>

#include <bcm_host.h>
#include "interface/vchiq_arm/vchiq_if.h"
#include "interface/vmcs_host/vc_tvservice.h"

/* ------------------------------------------------------------------------- */

class cRpiSetupPage : public cMenuSetupPage
{

public:

	cRpiSetupPage(
			cRpiSetup::AudioParameters audio,
			cRpiSetup::VideoParameters video,
			cRpiSetup::OsdParameters osd) :

		m_audio(audio),
		m_video(video),
		m_osd(osd)
	{
		m_audioPort[0] = tr("analog");
		m_audioPort[1] = tr("HDMI");

		m_audioFormat[0] = tr("pass through");
		m_audioFormat[1] = tr("multi channel PCM");
		m_audioFormat[2] = tr("stereo PCM");

		m_videoFraming[0] = tr("box");
		m_videoFraming[1] = tr("crop");
		m_videoFraming[2] = tr("stretch");

		m_videoResolution[0] = tr("default");
		m_videoResolution[1] = tr("follow video");
		m_videoResolution[2] = "720x480";
		m_videoResolution[3] = "720x576";
		m_videoResolution[4] = "1280x720";
		m_videoResolution[5] = "1920x1080";

		m_videoFrameRate[0] = tr("default");
		m_videoFrameRate[1] = tr("follow video");
		m_videoFrameRate[2] = "24p";
		m_videoFrameRate[3] = "25p";
		m_videoFrameRate[4] = "30p";
		m_videoFrameRate[5] = "50i";
		m_videoFrameRate[6] = "50p";
		m_videoFrameRate[7] = "60i";
		m_videoFrameRate[8] = "60p";

		Setup();
	}

	eOSState ProcessKey(eKeys Key)
	{
		int newAudioPort = m_audio.port;
		eOSState state = cMenuSetupPage::ProcessKey(Key);

		if (Key != kNone)
		{
			if (newAudioPort != m_audio.port)
				Setup();
		}

		return state;
	}

protected:

	virtual void Store(void)
	{
		SetupStore("AudioPort", m_audio.port);
		SetupStore("AudioFormat", m_audio.format);

		SetupStore("VideoFraming", m_video.framing);
		SetupStore("Resolution", m_video.resolution);
		SetupStore("FrameRate", m_video.frameRate);

		SetupStore("AcceleratedOsd", m_osd.accelerated);

		cRpiSetup::GetInstance()->Set(m_audio, m_video, m_osd);
}

private:

	void Setup(void)
	{
		int current = Current();
		Clear();

		if (!cRpiDisplay::IsFixedMode())
		{
			Add(new cMenuEditStraItem(
				tr("Resolution"), &m_video.resolution, 6, m_videoResolution));

			Add(new cMenuEditStraItem(
				tr("Frame Rate"), &m_video.frameRate, 9, m_videoFrameRate));
		}

		Add(new cMenuEditStraItem(
				tr("Video Framing"), &m_video.framing, 3, m_videoFraming));

		Add(new cMenuEditStraItem(
				tr("Audio Port"), &m_audio.port, 2, m_audioPort));

		if (m_audio.port == 1)
		{
			Add(new cMenuEditStraItem(tr("Digital Audio Format"),
					&m_audio.format, 3, m_audioFormat));
		}

		Add(new cMenuEditBoolItem(
				tr("Use GPU accelerated OSD"), &m_osd.accelerated));

		SetCurrent(Get(current));
		Display();
	}

	cRpiSetup::AudioParameters m_audio;
	cRpiSetup::VideoParameters m_video;
	cRpiSetup::OsdParameters   m_osd;

	const char *m_audioPort[2];
	const char *m_audioFormat[3];
	const char *m_videoFraming[3];
	const char *m_videoResolution[6];
	const char *m_videoFrameRate[9];
};

/* ------------------------------------------------------------------------- */

cRpiSetup* cRpiSetup::s_instance = 0;

cRpiSetup* cRpiSetup::GetInstance(void)
{
	if (!s_instance)
		s_instance = new cRpiSetup();

	return s_instance;
}

void cRpiSetup::DropInstance(void)
{
	delete s_instance;
	s_instance = 0;

	bcm_host_deinit();
}

bool cRpiSetup::HwInit(void)
{
	cRpiSetup* instance = GetInstance();
	if (!instance)
		return false;

	bcm_host_init();

	if (!vc_gencmd_send("codec_enabled MPG2"))
	{
		char buffer[1024];
		if (!vc_gencmd_read_response(buffer, sizeof(buffer)))
		{
			if (!strcasecmp(buffer,"MPG2=enabled"))
				GetInstance()->m_mpeg2Enabled = true;
		}
	}

	int width, height;
	if (!cRpiDisplay::GetSize(width, height))
	{
		ILOG("HwInit() done, display size is %dx%d", width, height);
	}
	else
		ELOG("failed to get video port information!");

	return true;
}

void cRpiSetup::SetAudioSetupChangedCallback(void (*callback)(void*), void* data)
{
	GetInstance()->m_onAudioSetupChanged = callback;
	GetInstance()->m_onAudioSetupChangedData = data;
}

void cRpiSetup::SetVideoSetupChangedCallback(void (*callback)(void*), void* data)
{
	GetInstance()->m_onVideoSetupChanged = callback;
	GetInstance()->m_onVideoSetupChangedData = data;
}

bool cRpiSetup::IsAudioFormatSupported(cAudioCodec::eCodec codec,
		int channels, int samplingRate)
{
	// MPEG-1 layer 2 audio pass-through not supported by audio render
	// and AAC audio pass-through not yet working
	if (codec == cAudioCodec::eMPG || codec == cAudioCodec::eAAC)
		return false;

	switch (GetAudioFormat())
	{
	case cAudioFormat::ePassThrough:
		return (vc_tv_hdmi_audio_supported(
					codec == cAudioCodec::eMPG  ? EDID_AudioFormat_eMPEG1 :
					codec == cAudioCodec::eAC3  ? EDID_AudioFormat_eAC3   :
					codec == cAudioCodec::eEAC3 ? EDID_AudioFormat_eEAC3  :
					codec == cAudioCodec::eAAC  ? EDID_AudioFormat_eAAC   :
					codec == cAudioCodec::eDTS  ? EDID_AudioFormat_eDTS   :
							EDID_AudioFormat_ePCM, channels,
					samplingRate ==  32000 ? EDID_AudioSampleRate_e32KHz  :
					samplingRate ==  44100 ? EDID_AudioSampleRate_e44KHz  :
					samplingRate ==  88200 ? EDID_AudioSampleRate_e88KHz  :
					samplingRate ==  96000 ? EDID_AudioSampleRate_e96KHz  :
					samplingRate == 176000 ? EDID_AudioSampleRate_e176KHz :
					samplingRate == 192000 ? EDID_AudioSampleRate_e192KHz :
							EDID_AudioSampleRate_e48KHz,
							EDID_AudioSampleSize_16bit) == 0);

	case cAudioFormat::eMultiChannelPCM:
		return codec == cAudioCodec::ePCM;

	default:
	case cAudioFormat::eStereoPCM:
		return codec == cAudioCodec::ePCM && channels == 2;
	}
}

void cRpiSetup::SetHDMIChannelMapping(bool passthrough, int channels)
{
	char command[80], response[80];

	sprintf(command, "hdmi_stream_channels %d", passthrough ? 1 : 0);
	vc_gencmd(response, sizeof response, command);

	uint32_t channel_map = 0;

	if (!passthrough && channels > 0 && channels <= 6)
	{
		const unsigned char ch_mapping[6][8] =
		{
			{ 0, 0, 0, 0, 0, 0, 0, 0 }, // not supported
			{ 1, 2, 0, 0, 0, 0, 0, 0 }, // 2.0
			{ 1, 2, 4, 0, 0, 0, 0, 0 }, // 2.1
			{ 0, 0, 0, 0, 0, 0, 0, 0 }, // not supported
			{ 0, 0, 0, 0, 0, 0, 0, 0 }, // not supported
			{ 1, 2, 4, 3, 5, 6, 0, 0 }, // 5.1
		};

		// speaker layout according CEA 861, Table 28: Audio InfoFrame, byte 4
		const unsigned char cea_map[] =
		{
			0xff,	// not supported
			0x00,	// 2.0
			0x01,	// 2.1
			0xff,	// not supported
			0xff,	// not supported
			0x0b	// 5.1
		};

		for (int ch = 0; ch < channels; ch++)
			if (ch_mapping[channels - 1][ch])
				channel_map |= (ch_mapping[channels - 1][ch] - 1) << (3 * ch);

		channel_map |= cea_map[channels - 1] << 24;
	}

	sprintf(command, "hdmi_channel_map 0x%08x", channel_map);
	vc_gencmd(response, sizeof response, command);
}

cMenuSetupPage* cRpiSetup::GetSetupPage(void)
{
	return new cRpiSetupPage(m_audio, m_video, m_osd);
}

bool cRpiSetup::Parse(const char *name, const char *value)
{
	if (!strcasecmp(name, "AudioPort"))
		m_audio.port = atoi(value);
	else if (!strcasecmp(name, "AudioFormat"))
		m_audio.format = atoi(value);
	else if (!strcasecmp(name, "VideoFraming"))
		m_video.framing = atoi(value);
	else if (!strcasecmp(name, "Resolution"))
		m_video.resolution = atoi(value);
	else if (!strcasecmp(name, "FrameRate"))
		m_video.frameRate = atoi(value);
	else if (!strcasecmp(name, "AcceleratedOsd"))
		m_osd.accelerated = atoi(value);
	else return false;

	return true;
}

void cRpiSetup::Set(AudioParameters audio, VideoParameters video,
		OsdParameters osd)
{
	if (audio != m_audio)
	{
		m_audio = audio;
		if (m_onAudioSetupChanged)
			m_onAudioSetupChanged(m_onAudioSetupChangedData);
	}

	if (video != m_video)
	{
		m_video = video;
		if (m_onVideoSetupChanged)
			m_onVideoSetupChanged(m_onVideoSetupChangedData);
	}

	if (osd != m_osd)
	{
		m_osd = osd;
		cRpiOsdProvider::ResetOsd(false);
	}
}

bool cRpiSetup::ProcessArgs(int argc, char *argv[])
{
	const int cDisplayOpt = 0x100;
	static struct option long_options[] = {
			{ "disable-osd", no_argument,       NULL, 'd'         },
			{ "display",     required_argument, NULL, cDisplayOpt },
			{ "video-layer", required_argument, NULL, 'v'         },
			{ "osd-layer",   required_argument, NULL, 'o'         },
			{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long(argc, argv, "do:v:", long_options, NULL)) != -1)
	{
		switch (c)
		{
		case 'd':
			m_plugin.hasOsd = false;
			break;
		case 'o':
			m_plugin.osdLayer = atoi(optarg);
			break;
		case 'v':
			m_plugin.videoLayer = atoi(optarg);
			break;
		case cDisplayOpt:
		{
			int d = atoi(optarg);
			switch (d)
			{
			case 0:
			case 4:
			case 5:
			case 6:
				m_plugin.display = d;
				break;
			default:
				ELOG("invalid device id (%d), using default display!", d);
				break;
			}
		}
			break;
		default:
			return false;
		}
	}
	DBG("dispmanx layers: video=%d, osd=%d (%s), display=%d",
			m_plugin.videoLayer, m_plugin.osdLayer,
			m_plugin.hasOsd ? "enabled" : "disabled", m_plugin.display);

	return true;
}

const char *cRpiSetup::CommandLineHelp(void)
{
	return	"  -d,       --disable-osd  disable OSD\n"
			"  -v,       --video-layer  dispmanx layer for video (default 0)\n"
			"  -o,       --osd-layer    dispmanx layer for OSD (default 2)\n"
			"            --display      display used for output:\n"
			"                           0: default display (default)\n"
			"                           4: LCD\n"
			"                           5: TV/HDMI\n"
			"                           6: non-default display\n";
}
