/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "setup.h"

#include <vdr/tools.h>
#include <vdr/menuitems.h>

#include <bcm_host.h>
#include "interface/vchiq_arm/vchiq_if.h"
#include "interface/vmcs_host/vc_tvservice.h"

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

class cRpiSetupPage : public cMenuSetupPage
{

public:

	cRpiSetupPage(
			cRpiSetup::AudioParameters audio,
			cRpiSetup::VideoParameters video) :

		m_audio(audio),
		m_video(video)
	{
		Setup();
	}

	eOSState ProcessKey(eKeys Key)
	{
		int newAudioPort = m_audio.port;
		int newPassthrough = m_audio.passthrough;

		eOSState state = cMenuSetupPage::ProcessKey(Key);

		if (Key != kNone)
		{
			if ((newAudioPort != m_audio.port) ||
					(newPassthrough != m_audio.passthrough))
				Setup();
		}

		return state;
	}

protected:

	virtual void Store(void)
	{
		SetupStore("AudioPort", m_audio.port);
		SetupStore("PassThrough", m_audio.passthrough);
		SetupStore("IgnoreAudioEDID", m_audio.ignoreEDID);

		SetupStore("VideoFraming", m_video.framing);

		cRpiSetup::GetInstance()->Set(m_audio, m_video);
}

private:

	void Setup(void)
	{
		int current = Current();
		Clear();

		Add(new cMenuEditStraItem(
				tr("Video Framing"), &m_video.framing, 3, s_videoframing));

		Add(new cMenuEditStraItem(
				tr("Audio Port"), &m_audio.port, 2, s_audioport));

		if (m_audio.port == 1)
		{
			Add(new cMenuEditBoolItem(
					tr("Digital Audio Pass-Through"), &m_audio.passthrough));

			if (m_audio.passthrough)
				Add(new cMenuEditBoolItem(
						tr("Ignore Audio EDID"), &m_audio.ignoreEDID));
		}

		SetCurrent(Get(current));
		Display();
	}

	cRpiSetup::AudioParameters m_audio;
	cRpiSetup::VideoParameters m_video;

	static const char *const s_audioport[2];
	static const char *const s_videoframing[3];
};

const char *const cRpiSetupPage::s_audioport[] =
		{ tr("analog"), tr("HDMI") };

const char *const cRpiSetupPage::s_videoframing[] =
		{ tr("box"), tr("crop"), tr("stretch") };

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

	TV_DISPLAY_STATE_T tvstate;
	memset(&tvstate, 0, sizeof(TV_DISPLAY_STATE_T));
	if (!vc_tv_get_display_state(&tvstate))
	{
		int height = 0, width = 0;
		bool progressive = false;
		cRpiVideoPort::ePort port = cRpiVideoPort::eComposite;

		// HDMI
		if ((tvstate.state & (VC_HDMI_HDMI | VC_HDMI_DVI)))
		{
			progressive = tvstate.display.hdmi.scan_mode == 0;
			height = tvstate.display.hdmi.height;
			width = tvstate.display.hdmi.width;
			port = cRpiVideoPort::eHDMI;
		}
		else
		{
			height = tvstate.display.sdtv.height;
			width = tvstate.display.sdtv.width;
		}

		ILOG("using %s video output at %dx%d%s",
				cRpiVideoPort::Str(port), width, height, progressive ? "p" : "i");

		instance->m_isProgressive = progressive;
		instance->m_displayHeight = height;
		instance->m_displayWidth = width;
	}
	else
	{
		ELOG("failed to get display parameters!");
		return false;
	}

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

	if (GetInstance()->m_audio.ignoreEDID)
		return true;

	if (vc_tv_hdmi_audio_supported(
			codec == cAudioCodec::eMPG  ? EDID_AudioFormat_eMPEG1 :
			codec == cAudioCodec::eAC3  ? EDID_AudioFormat_eAC3   :
			codec == cAudioCodec::eEAC3 ? EDID_AudioFormat_eEAC3  :
			codec == cAudioCodec::eAAC  ? EDID_AudioFormat_eAAC   :
					EDID_AudioFormat_ePCM, channels,
			samplingRate ==  32000 ? EDID_AudioSampleRate_e32KHz  :
			samplingRate ==  44100 ? EDID_AudioSampleRate_e44KHz  :
			samplingRate ==  88200 ? EDID_AudioSampleRate_e88KHz  :
			samplingRate ==  96000 ? EDID_AudioSampleRate_e96KHz  :
			samplingRate == 176000 ? EDID_AudioSampleRate_e176KHz :
			samplingRate == 192000 ? EDID_AudioSampleRate_e192KHz :
					EDID_AudioSampleRate_e48KHz,
					EDID_AudioSampleSize_16bit) == 0)
		return true;

	DLOG("%dch %s, %d.%dkHz not supported by HDMI device",
			channels, cAudioCodec::Str(codec),
			samplingRate / 1000, (samplingRate % 1000) / 100);

	return false;
}

int cRpiSetup::GetDisplaySize(int &width, int &height, double &aspect)
{
	height = GetInstance()->m_displayHeight;
	width = GetInstance()->m_displayWidth;
	aspect = (double)width / height;
	return 0;
}

cMenuSetupPage* cRpiSetup::GetSetupPage(void)
{
	return new cRpiSetupPage(m_audio, m_video);
}

bool cRpiSetup::Parse(const char *name, const char *value)
{
	if (!strcasecmp(name, "AudioPort"))
		m_audio.port = atoi(value);
	else if (!strcasecmp(name, "PassThrough"))
		m_audio.passthrough = atoi(value);
	else if (!strcasecmp(name, "IgnoreAudioEDID"))
		m_audio.ignoreEDID = atoi(value);
	else if (!strcasecmp(name, "VideoFraming"))
		m_video.framing = atoi(value);
	else return false;

	return true;
}

void cRpiSetup::Set(AudioParameters audio, VideoParameters video)
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
}
