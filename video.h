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

#ifndef VIDEO_H
#define VIDEO_H

#include <vdr/thread.h>

#include "tools.h"
#include "omx.h"

class cRpiVideoDecoder
{

public:

	cRpiVideoDecoder(cVideoCodec::eCodec codec,
			void (*onStreamStart)(void*, const cVideoFrameFormat *format),
			void (*onEndOfStream)(void*), void* callbackData);
	virtual ~cRpiVideoDecoder();

	cVideoCodec::eCodec GetCodec(void) { return m_codec; };

	virtual bool WriteData(const unsigned char *data,
			unsigned int length, int64_t pts, bool eof) = 0;

	virtual bool Poll(void) { return true; };
	virtual void Clear(bool flushVideoRender = false) { };
	virtual void Flush(void) { };

	const cVideoFrameFormat *GetFrameFormat(void) { return &m_format; }

protected:

	static const unsigned char s_mpeg2EndOfSequence[4];
	static const unsigned char s_h264EndOfSequence[8];

	cVideoCodec::eCodec m_codec;
	cVideoFrameFormat   m_format;

	void NotifyStreamStart(void);
	void NotifyEndOfStream(void);

	void (*m_onStreamStart)(void*, const cVideoFrameFormat *format);
	void (*m_onEndOfStream)(void*);
	void *m_callbackData;

};

/* ------------------------------------------------------------------------- */

class cRpiOmxVideoDecoder : public cRpiVideoDecoder, protected cOmxEventHandler
{

public:

	cRpiOmxVideoDecoder(cVideoCodec::eCodec codec, cOmx *omx,
			void (*onStreamStart)(void*, const cVideoFrameFormat *format),
			void (*onEndOfStream)(void*), void* callbackData);
	virtual ~cRpiOmxVideoDecoder();

	virtual bool WriteData(const unsigned char *data,
			unsigned int length, int64_t pts, bool eof);

	virtual bool Poll(void);
	virtual void Clear(bool flushVideoRender = false);
	virtual void Flush(void);

protected:

	virtual void PortSettingsChanged(int port);
	virtual void EndOfStreamReceived(int port);
	virtual void BufferEmptied(cOmx::eOmxComponent comp);
	virtual void BufferStalled(void);

	cOmx *m_omx;

private:

};

/*
class cRpiFfmpegVideoDecoder : public cRpiVideoDecoder, public cThread
{

public:

	cRpiFfmpegVideoDecoder();
	virtual ~cRpiFfmpegVideoDecoder();

protected:

	static void Log(void* ptr, int level, const char* fmt, va_list vl);

	struct Codec
	{
		class AVCodec		 *codec;
	    class AVCodecContext *context;
	};

	Codec m_codec;

private:

};
*/
#endif
