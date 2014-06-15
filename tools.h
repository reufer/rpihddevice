/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef TOOLS_H
#define TOOLS_H

#define ELOG(a...) esyslog("rpihddevice: " a)
#define ILOG(a...) isyslog("rpihddevice: " a)
#define DLOG(a...) dsyslog("rpihddevice: " a)

#ifdef DEBUG
#define DBG(a...)  dsyslog("rpihddevice: " a)
#else
#define DBG(a...)  void()
#endif

class cVideoFraming
{
public:

	enum eFraming {
		eFrame,
		eCut,
		eStretch
	};

	static const char* Str(eFraming framing) {
		return  (framing == eFrame)   ? "frame"   :
				(framing == eCut)     ? "cut"     :
				(framing == eStretch) ? "stretch" : "unknown";
	}
};

class cAudioCodec
{
public:

	enum eCodec {
		ePCM,
		eMPG,
		eAC3,
		eEAC3,
		eAAC,
		eNumCodecs,
		eInvalid
	};

	static const char* Str(eCodec codec) {
		return  (codec == ePCM)  ? "PCM"   :
				(codec == eMPG)  ? "MPEG"  :
				(codec == eAC3)  ? "AC3"   :
				(codec == eEAC3) ? "E-AC3" :
				(codec == eAAC)  ? "AAC"   : "unknown";
	}
};

class cVideoCodec
{
public:

	enum eCodec {
		eMPEG2,
		eH264,
		eNumCodecs,
		eInvalid
	};

	static const char* Str(eCodec codec) {
		return  (codec == eMPEG2) ? "MPEG2" :
				(codec == eH264)  ? "H264"  : "unknown";
	}
};

class cRpiAudioPort
{
public:

	enum ePort {
		eLocal,
		eHDMI
	};

	static const char* Str(ePort port) {
		return 	(port == eLocal) ? "local" :
				(port == eHDMI)  ? "HDMI"  : "unknown";
	}
};

class cRpiVideoPort
{
public:

	enum ePort {
		eComposite,
		eHDMI
	};

	static const char* Str(ePort port) {
		return 	(port == eComposite) ? "composite" :
				(port == eHDMI)      ? "HDMI"      : "unknown";
	}
};

#endif
