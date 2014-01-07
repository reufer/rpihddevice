/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <vdr/plugin.h>
#include <vdr/config.h>

#include "ovgosd.h"
#include "omxdevice.h"

#include "bcm_host.h"

static const char *VERSION        = "0.0.3";
static const char *DESCRIPTION    = "HD output device for Raspberry Pi";

class cDummyDevice : cDevice
{

public:

	cDummyDevice() { }
	virtual ~cDummyDevice() { }
    virtual bool HasDecoder(void) const { return true; }
    virtual bool SetPlayMode(ePlayMode PlayMode) { return true; }
    virtual int  PlayVideo(const uchar *Data, int Length) { return Length; }
    virtual int  PlayAudio(const uchar *Data, int Length, uchar Id) { return Length; }
    virtual bool Poll(cPoller &Poller, int TimeoutMs = 0) { return true; }
    virtual bool Flush(int TimeoutMs = 0) { return true; }
	virtual void GetOsdSize(int &Width, int &Height, double &PixelAspect)
		{ cOmxDevice::GetDisplaySize(Width, Height, PixelAspect); }
    bool Start(void) {return true;}

protected:
	virtual void MakePrimaryDevice(bool On) { if (On) new cRpiOsdProvider(); }

};

class cPluginRpiHdDevice : public cPlugin
{
private:

	cOmxDevice *m_device;

	static void OnPrimaryDevice() { new cRpiOsdProvider(); }

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
	virtual cMenuSetupPage *SetupMenu(void) { return NULL; }
	virtual bool SetupParse(const char *Name, const char *Value) { return false; };
};

cPluginRpiHdDevice::cPluginRpiHdDevice(void) : 
	m_device(0)
{
	m_device = new cOmxDevice(&cPluginRpiHdDevice::OnPrimaryDevice);
	//new cDummyDevice();
}

cPluginRpiHdDevice::~cPluginRpiHdDevice()
{
	delete m_device;
}

bool cPluginRpiHdDevice::Initialize(void)
{
	bcm_host_init();

	if (m_device)
		return (m_device->Init() == 0);

	return true;
}

bool cPluginRpiHdDevice::Start(void)
{
	return true;
}

void cPluginRpiHdDevice::Stop(void)
{
	if (m_device)
		m_device->DeInit();
}

VDRPLUGINCREATOR(cPluginRpiHdDevice); // Don't touch this!
