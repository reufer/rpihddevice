/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef AUDIO_H
#define AUDIO_H

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <vdr/thread.h>

#include "tools.h"
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
	void SetCodec(cAudioCodec::eCodec codec, unsigned int &channels, unsigned int samplingRate);

	struct Codec
	{
		AVCodec 		*codec;
	    AVCodecContext 	*context;
	};

private:

	Codec		  m_codecs[cAudioCodec::eNumCodecs];
	bool		  m_passthrough;
	bool		  m_reset;
	bool		  m_ready;
	uint64_t 	  m_pts;

	cCondWait	 *m_wait;
	cAudioParser *m_parser;
	cOmx		 *m_omx;
};

#endif
