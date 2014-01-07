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

#include <vdr/thread.h>

#include "types.h"
#include "omx.h"

class cAudioParser;

class cAudioDecoder : public cThread
{

public:

    cAudioDecoder(cOmx *omx);
	virtual ~cAudioDecoder();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool WriteData(const unsigned char *buf, unsigned int length, uint64_t pts = 0);

	virtual bool Poll(void);
	virtual void Reset(void);

protected:

	virtual void Action(void);

	virtual unsigned int DecodeFrame();
	virtual unsigned int ReadFrame(unsigned char *buf, unsigned int bufsize);

	virtual bool ProbeCodec(void);
	void SetCodec(cAudioCodec::eCodec codec);

	struct Codec
	{
		AVCodec 		*codec;
	    AVCodecContext 	*context;
	};

private:

	Codec				 m_codecs[cAudioCodec::eNumCodecs];
	cAudioCodec::eCodec	 m_codec;
	cAudioCodec::eCodec	 m_outputFormat;
	cAudioPort::ePort	 m_outputPort;
	int					 m_channels;
	int					 m_samplingRate;
	bool				 m_passthrough;
	bool				 m_outputFormatChanged;
	uint64_t 			 m_pts;

    AVFrame 	 		*m_frame;
    cMutex		 		*m_mutex;
    cCondWait	 		*m_newData;
    cAudioParser 		*m_parser;
	cOmx		 		*m_omx;
};

#endif
