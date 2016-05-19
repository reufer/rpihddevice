/*
 * rpihddevice - VDR HD output device for Raspberry Pi
 * Copyright (C) 2014, 2015, 2016 Thomas Reufer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

class cVideoResolution
{
public:

	enum eResolution {
		eDontChange = 0,
		eFollowVideo,
		e480,
		e480w,
		e576,
		e576w,
		e720,
		e1080
	};

	static const char* Str(eResolution resolution) {
		return	(resolution == eDontChange)  ? "don't change" :
				(resolution == eFollowVideo) ? "follow video" :
				(resolution == e480)         ? "480"          :
				(resolution == e480w)        ? "480w"         :
				(resolution == e576)         ? "576"          :
				(resolution == e576w)        ? "576w"         :
				(resolution == e720)         ? "720"          :
				(resolution == e1080)        ? "1080"         :	"unknown";
	}
};

class cVideoFrameRate
{
public:

	enum eFrameRate {
		eDontChange = 0,
		eFollowVideo,
		e24p,
		e25p,
		e30p,
		e50i,
		e50p,
		e60i,
		e60p
	};

	static const char* Str(eFrameRate frameRate) {
		return	(frameRate == eDontChange)  ? "don't change" :
				(frameRate == eFollowVideo) ? "follow video" :
				(frameRate == e24p)         ? "p24"          :
				(frameRate == e25p)         ? "p25"          :
				(frameRate == e30p)         ? "p30"          :
				(frameRate == e50i)         ? "i50"          :
				(frameRate == e50p)         ? "p50"          :
				(frameRate == e60i)         ? "i60"          :
				(frameRate == e60p)         ? "p60"          : "unknown";
	}
};

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
		eAAC_LATM,
		eDTS,
		eNumCodecs,
		eInvalid
	};

	static const char* Str(eCodec codec) {
		return  (codec == ePCM)      ? "PCM"      :
				(codec == eMPG)      ? "MPEG"     :
				(codec == eAC3)      ? "AC3"      :
				(codec == eEAC3)     ? "E-AC3"    :
				(codec == eAAC)      ? "AAC"      :
				(codec == eAAC_LATM) ? "AAC-LATM" :
				(codec == eDTS)      ? "DTS"      : "unknown";
	}
};

class cAudioFormat
{
public:

	enum eFormat {
		ePassThrough,
		eMultiChannelPCM,
		eStereoPCM
	};

	static const char* Str(eFormat format) {
		return  (format == ePassThrough)     ? "pass through"      :
				(format == eMultiChannelPCM) ? "multi channel PCM" :
				(format == eStereoPCM)       ? "stereo PCM"        : "unknown";
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

class cScanMode
{
public:

	enum eMode {
		eProgressive,
		eTopFieldFirst,
		eBottomFieldFirst
	};

	static const char* Str(eMode mode) {
		return 	(mode == eProgressive)      ? "progressive"      :
				(mode == eTopFieldFirst)    ? "interlaced (tff)" :
				(mode == eBottomFieldFirst) ? "interlaced (bff)" : "unknown";
	}

	static const bool Interlaced(eMode mode) {
		return mode != eProgressive;
	}
};

class cVideoFrameFormat
{
public:

	cVideoFrameFormat() : width(0), height(0), frameRate(0),
		scanMode(cScanMode::eProgressive), pixelWidth(0), pixelHeight(0) { };

	int width;
	int height;
	int frameRate;
	cScanMode::eMode scanMode;
	int pixelWidth;
	int pixelHeight;

	bool Interlaced(void) const {
		return cScanMode::Interlaced(scanMode);
	}
};

class cRational
{
public:

	cRational(double d);
	cRational(int _num, int _den) : num(_num), den(_den) { }

	bool Reduce(int max);

	int num;
	int den;

private:

	cRational();
	static int Gcd(int u, int v);
};

#endif
