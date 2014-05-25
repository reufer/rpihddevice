/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <vdr/thread.h>

#include "tools.h"
#include "omx.h"

class cRpiAudioDecoder : public cThread
{

public:

    cRpiAudioDecoder(cOmx *omx);
	virtual ~cRpiAudioDecoder();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool WriteData(const unsigned char *buf, unsigned int length,
			uint64_t pts = 0);

	virtual bool Poll(void);
	virtual void Reset(void);

protected:

	virtual void Action(void);
	void SetCodec(cAudioCodec::eCodec codec, unsigned int &channels,
			unsigned int samplingRate);

	static void Log(void* ptr, int level, const char* fmt, va_list vl);
	static int s_printPrefix;

	struct Codec
	{
		class AVCodec		 *codec;
	    class AVCodecContext *context;
	};

private:

	class cParser;

	Codec		  m_codecs[cAudioCodec::eNumCodecs];
	bool		  m_passthrough;
	bool		  m_reset;

	cCondWait	 *m_wait;
	cParser		 *m_parser;
	cOmx		 *m_omx;
};

#endif
