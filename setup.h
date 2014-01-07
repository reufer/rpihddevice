/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef SETUP_H
#define SETUP_H

#include "audio.h"
#include "omxdevice.h"

class cMenuSetupPage;

class cRpiSetup
{

public:

	static bool HwInit(void);

	static cAudioDecoder::ePort GetAudioPort(void) {
		return (GetInstance()->m_audioPort) ? cAudioDecoder::eHDMI : cAudioDecoder::eLocal; }
	static bool IsAudioPassthrough(void) { return GetInstance()->m_passthrough; }
	static bool HasAudioSetupChanged(void);

	static bool IsAudioFormatSupported(cAudioDecoder::eCodec codec, int channels, int samplingRate);

	static bool IsVideoCodecSupported(cOmxDevice::eVideoCodec codec) {
		return codec == cOmxDevice::eMPEG2 ? GetInstance()->m_mpeg2Enabled :
				codec == cOmxDevice::eH264 ? true : false;
	}

	static int GetDisplaySize(int &width, int &height, double &aspect);

	static cRpiSetup* GetInstance(void);
	static void DropInstance(void);

	cMenuSetupPage* GetSetupPage(void);
	bool Parse(const char *name, const char *value);

private:

	cRpiSetup() : m_audioSetupChanged(false), m_mpeg2Enabled(false) { }
	virtual ~cRpiSetup() { }

	static cRpiSetup* s_instance;

	int m_audioPort;
	int m_passthrough;

	bool m_audioSetupChanged;

	bool m_mpeg2Enabled;
};

#endif
