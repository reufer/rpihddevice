/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef TYPES_H
#define TYPES_H

class cAudioCodec
{
public:

	enum eCodec {
		ePCM,
		eMPG,
		eAC3,
		eEAC3,
		eAAC,
		eADTS,
		eNumCodecs,
		eInvalid
	};

	static const char* Str(eCodec codec) {
		return  (codec == ePCM)  ? "PCM"   :
				(codec == eMPG)  ? "MPEG"  :
				(codec == eAC3)  ? "AC3"   :
				(codec == eEAC3) ? "E-AC3" :
				(codec == eAAC)  ? "AAC"   :
				(codec == eADTS) ? "ADTS"  : "unknown";
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

class cAudioPort
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

#endif
