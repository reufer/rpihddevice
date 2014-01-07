/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef AUDIO_H
#define AUDIO_H

extern "C"
{
#include <libavcodec/avcodec.h>
}

class cMutex;

class cAudioDecoder
{

public:

	enum eCodec {
		ePCM,
		eMPG,
		eAC3,
		eEAC3,
		eAAC,
		eDTS,
		eNumCodecs
	};

	enum ePort {
		eLocal,
		eHDMI
	};

	static const char* CodecStr(eCodec codec)
	{
		return  (codec == ePCM)  ? "PCM"   :
				(codec == eMPG)  ? "MPG"   :
				(codec == eAC3)  ? "AC3"   :
				(codec == eEAC3) ? "E-AC3" :
				(codec == eAAC)  ? "AAC"   :
				(codec == eDTS)  ? "DTS"   : "unknown";
	}

    cAudioDecoder();
	virtual ~cAudioDecoder();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool SetupAudioCodec(const unsigned char *data, int length);

	virtual eCodec GetCodec(void) { return m_codec; }
	virtual eCodec GetOutputFormat(void) { return m_outputFormat; }
	virtual ePort GetOutputPort(void) { return m_outputPort; }
	virtual int GetChannels(void) { return m_channels; }
	virtual int GetSamplingrate(void) { return m_samplingRate; }

	virtual unsigned int DecodeAudio(const unsigned char *data, int length,
			unsigned char *outbuf, int bufsize);

private:

	struct Codec
	{
		AVCodec 		*codec;
	    AVCodecContext 	*context;
	};

	Codec	 m_codecs[eNumCodecs];
	eCodec	 m_codec;
	eCodec	 m_outputFormat;
	ePort	 m_outputPort;
	int		 m_channels;
	int		 m_samplingRate;

	bool	 m_passthrough;

    AVFrame *m_frame;
    cMutex	*m_mutex;
};

#endif
