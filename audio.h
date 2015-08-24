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

class cRpiAudioRender;

class cRpiAudioDecoder : public cThread
{

public:

    cRpiAudioDecoder(cOmx *omx);
	virtual ~cRpiAudioDecoder();

	virtual int Init(void);
	virtual int DeInit(void);

	virtual bool WriteData(const unsigned char *buf, unsigned int length,
			int64_t pts = 0);

	virtual bool Poll(void);
	virtual void Reset(void);

protected:

	virtual void Action(void);

	static void OnAudioSetupChanged(void *data)
		{ (static_cast <cRpiAudioDecoder*> (data))->HandleAudioSetupChanged(); }

	void HandleAudioSetupChanged();

	static void Log(void* ptr, int level, const char* fmt, va_list vl);

	struct Codec
	{
		class AVCodec		 *codec;
	    class AVCodecContext *context;
	};

private:

	class cParser;

	Codec		  	m_codecs[cAudioCodec::eNumCodecs];
	bool		  	m_passthrough;
	bool		  	m_reset;
	bool		  	m_setupChanged;

	cCondWait	 	*m_wait;
	cParser		 	*m_parser;
	cRpiAudioRender	*m_render;
};

#endif
