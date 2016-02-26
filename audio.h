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
