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

	static bool HwInit(void);

	static cAudioPort::ePort GetAudioPort(void) {
		return (GetInstance()->m_audioPort) ? cAudioPort::eHDMI : cAudioPort::eLocal; }

	static bool IsAudioPassthrough(void) {
		return GetInstance()->m_passthrough; }

	static bool IgnoreAudioEDID(void) {
		return GetInstance()->m_ignoreAudioEDID; }

	static bool HasAudioSetupChanged(void);

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

private:

	cRpiSetup() :
		m_audioPort(0),
		m_passthrough(0),
		m_ignoreAudioEDID(0),
		m_audioSetupChanged(false),
		m_mpeg2Enabled(false),
		m_isProgressive(false),
		m_displayHeight(0),
		m_displayWidth(0) { }

	virtual ~cRpiSetup() { }

	static cRpiSetup* s_instance;

	int m_audioPort;
	int m_passthrough;
	int m_ignoreAudioEDID;

	bool m_audioSetupChanged;

	bool m_mpeg2Enabled;
	bool m_isProgressive;

	int m_displayHeight;
	int m_displayWidth;
};

#endif
