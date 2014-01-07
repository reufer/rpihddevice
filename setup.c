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
}

class cRpiSetupPage : public cMenuSetupPage
{

public:

	cRpiSetupPage(int *audioPort, int *passthrough, bool *audioSetupChanged) :
		m_audioPort(audioPort),
		m_passthrough(passthrough),
		m_audioSetupChanged(audioSetupChanged)
	{
	    static const char *const audioport[] = { tr("analog"), tr("HDMI") };

		m_newAudioPort = *m_audioPort;
		m_newPassthrough = *m_passthrough;

		Add(new cMenuEditStraItem(
				tr("Audio Port"), &m_newAudioPort, 2, audioport));
		Add(new cMenuEditBoolItem(
				tr("Digital Audio Pass-Through"), &m_newPassthrough));
	}

protected:

	virtual void Store(void)
	{
		*m_audioSetupChanged =
			(*m_audioPort != m_newAudioPort) ||
			(*m_passthrough != m_newPassthrough);

		SetupStore("AudioPort", *m_audioPort = m_newAudioPort);
		SetupStore("PassThrough", *m_passthrough = m_newPassthrough);
	}

private:

	int m_newAudioPort;
	int m_newPassthrough;

	int *m_audioPort;
	int *m_passthrough;

	bool *m_audioSetupChanged;

};

bool cRpiSetup::HwInit(void)
{
	bcm_host_init();
	vcos_init();

	VCHI_INSTANCE_T vchiInstance;
	VCHI_CONNECTION_T *vchiConnections;
	if (vchi_initialise(&vchiInstance) != VCHIQ_SUCCESS)
	{
		esyslog("rpihddevice: failed to open vchiq instance!");
		return false;
	}
	if (vchi_connect(NULL, 0, vchiInstance) != 0)
	{
		esyslog("rpihddevice: failed to connect to vchi!");
		return false;
	}
	if (vc_vchi_tv_init(vchiInstance, &vchiConnections, 1) != 0)
	{
		esyslog("rpihddevice: failed to connect to tvservice!");
		return false;
	}

	if (!vc_gencmd_send("codec_enabled MPG2"))
	{
		char buffer[1024];
		if (!vc_gencmd_read_response(buffer, sizeof(buffer)))
		{
			if (!strcasecmp(buffer,"MPG2=enabled"))
				GetInstance()->m_mpeg2Enabled = true;
		}
	}

	return true;
}

bool cRpiSetup::IsAudioFormatSupported(cAudioCodec::eCodec codec,
		int channels, int samplingRate)
{
	// AAC and DTS pass-through currently not supported
	if (codec == cAudioCodec::eAAC ||
		codec == cAudioCodec::eADTS)
		return false;

	if (vc_tv_hdmi_audio_supported(
			codec == cAudioCodec::eMPG  ? EDID_AudioFormat_eMPEG1 :
			codec == cAudioCodec::eAC3  ? EDID_AudioFormat_eAC3   :
			codec == cAudioCodec::eEAC3 ? EDID_AudioFormat_eEAC3  :
			codec == cAudioCodec::eAAC  ? EDID_AudioFormat_eAAC   :
					EDID_AudioFormat_ePCM, channels,
			samplingRate ==  32000 ? EDID_AudioSampleRate_e32KHz  :
			samplingRate ==  44000 ? EDID_AudioSampleRate_e44KHz  :
			samplingRate ==  88000 ? EDID_AudioSampleRate_e88KHz  :
			samplingRate ==  96000 ? EDID_AudioSampleRate_e96KHz  :
			samplingRate == 176000 ? EDID_AudioSampleRate_e176KHz :
			samplingRate == 192000 ? EDID_AudioSampleRate_e192KHz :
					EDID_AudioSampleRate_e48KHz,
					EDID_AudioSampleSize_16bit) == 0)
		return true;

	return false;
}

int cRpiSetup::GetDisplaySize(int &width, int &height, double &aspect)
{
	uint32_t screenWidth;
	uint32_t screenHeight;

	if (graphics_get_display_size(0 /* LCD */, &screenWidth, &screenHeight) < 0)
			esyslog("rpihddevice: failed to get display size!");
	else
	{
		width = (int)screenWidth;
		height = (int)screenHeight;
		aspect = 1;
		return 0;
	}
	return -1;
}

bool cRpiSetup::HasAudioSetupChanged(void)
{
	if (!GetInstance()->m_audioSetupChanged)
		return false;

	GetInstance()->m_audioSetupChanged = false;
	return true;
}

cMenuSetupPage* cRpiSetup::GetSetupPage(void)
{
	return new cRpiSetupPage(&m_audioPort, &m_passthrough, &m_audioSetupChanged);
}

bool cRpiSetup::Parse(const char *name, const char *value)
{
	if (!strcasecmp(name, "AudioPort")) m_audioPort = atoi(value);
	else if (!strcasecmp(name, "PassThrough")) m_passthrough = atoi(value);
	else return false;

	return true;
}
