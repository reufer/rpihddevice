/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <vdr/plugin.h>
#include <vdr/config.h>

#include "ovgosd.h"
#include "omxdevice.h"
#include "setup.h"
#include "tools.h"

static const char *VERSION        = "0.0.8";
static const char *DESCRIPTION    = tr("HD output device for Raspberry Pi");

class cPluginRpiHdDevice : public cPlugin
{
private:

	cOmxDevice *m_device;

	static void OnPrimaryDevice(void) { new cRpiOsdProvider(); }

public:
	cPluginRpiHdDevice(void);
	virtual ~cPluginRpiHdDevice();
	virtual const char *Version(void) { return VERSION; }
	virtual const char *Description(void) { return DESCRIPTION; }
	virtual const char *CommandLineHelp(void) { return NULL; }
	virtual bool ProcessArgs(int argc, char *argv[]) { return true; }
	virtual bool Initialize(void);
	virtual bool Start(void);
	virtual void Stop(void);
	virtual void Housekeeping(void) {}
	virtual const char *MainMenuEntry(void) { return NULL; }
	virtual cOsdObject *MainMenuAction(void) { return NULL; }
	virtual cMenuSetupPage *SetupMenu(void);
	virtual bool SetupParse(const char *Name, const char *Value);
};

cPluginRpiHdDevice::cPluginRpiHdDevice(void) : 
	m_device(0)
{
}

cPluginRpiHdDevice::~cPluginRpiHdDevice()
{
	cRpiSetup::DropInstance();
}

bool cPluginRpiHdDevice::Initialize(void)
{
	if (!cRpiSetup::HwInit())
		return false;

	// test whether MPEG2 license is available
	if (!cRpiSetup::IsVideoCodecSupported(cVideoCodec::eMPEG2))
		DLOG("MPEG2 video decoder not enabled!");

	m_device = new cOmxDevice(&OnPrimaryDevice);

	if (m_device)
		return !m_device->Init();

	return false;
}

bool cPluginRpiHdDevice::Start(void)
{
	return true;
}

void cPluginRpiHdDevice::Stop(void)
{
}

cMenuSetupPage* cPluginRpiHdDevice::SetupMenu(void)
{
	return cRpiSetup::GetInstance()->GetSetupPage();
}

bool cPluginRpiHdDevice::SetupParse(const char *Name, const char *Value)
{
	return cRpiSetup::GetInstance()->Parse(Name, Value);
}

VDRPLUGINCREATOR(cPluginRpiHdDevice); // Don't touch this! okay.
