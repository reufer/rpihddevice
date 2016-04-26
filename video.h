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

#include <queue>

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

	virtual const char* Description(void) = 0;
	cVideoCodec::eCodec GetCodec(void) { return m_codec; };

	virtual bool WriteData(const unsigned char *data,
			unsigned int length, int64_t pts, bool eof) = 0;

	virtual bool Poll(void) { return true; };
	virtual void Clear(bool flushRender = false) { };
	virtual void Flush(void) { };

	const cVideoFrameFormat *GetFrameFormat(void) { return &m_format; }

	virtual int GetBufferUsage(void) { return 0; };

protected:

	static const unsigned char s_mpeg2EndOfSequence[4];
	static const unsigned char s_h264EndOfSequence[8];
	static const unsigned char s_h265EndOfSequence[5];

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

	virtual const char* Description(void) { return "OpenMax"; };

	virtual bool WriteData(const unsigned char *data,
			unsigned int length, int64_t pts, bool eof);

	virtual bool Poll(void);
	virtual void Clear(bool flushRender = false);
	virtual void Flush(void);

	virtual int GetBufferUsage(void);

protected:

	virtual void PortSettingsChanged(int port);
	virtual void EndOfStreamReceived(int port);
	virtual void BufferEmptied(cOmx::eOmxComponent comp);
	virtual void BufferStalled(void);
	virtual void Tick(void);

private:

	OMX_BUFFERHEADERTYPE* GetBuffer(void);
	bool EmptyBuffer(OMX_BUFFERHEADERTYPE *buf);

	void SetupDeinterlacer(cDeinterlacerMode::eMode mode);

	cMutex m_mutex;
	cOmx  *m_omx;
	int    m_usedBuffers[BUFFERSTAT_FILTER_SIZE];
	bool   m_setDiscontinuity;
	bool   m_setStartTime;

	OMX_BUFFERHEADERTYPE *m_spareBuffers;
};

/* ------------------------------------------------------------------------- */

struct AVFrame;
struct AVPacket;
struct AVCodec;
struct AVCodecContext;

class cYuv422Geometry;

class cRpiFfmpegVideoDecoder : public cRpiVideoDecoder, public cThread,
	protected cOmxEventHandler
{

public:

	cRpiFfmpegVideoDecoder(cVideoCodec::eCodec codec, cOmx *omx,
			void (*onStreamStart)(void*, const cVideoFrameFormat *format),
			void (*onEndOfStream)(void*), void* callbackData);
	virtual ~cRpiFfmpegVideoDecoder();

	virtual const char* Description(void) { return "FFmpeg"; };

	virtual bool WriteData(const unsigned char *data,
			unsigned int length, int64_t pts, bool eof);

	virtual bool Poll(void);
	virtual void Clear(bool flushRender = false);
	virtual void Flush(void);

protected:

	virtual void Action(void);
	virtual void BufferEmptied(cOmx::eOmxComponent comp);

private:

	static int GetBuffer2(AVCodecContext *ctx, AVFrame *frame, int flags);
	static void FreeBuffer(void *opaque, uint8_t *data);

	int GetAvBuffer(AVFrame *frame, int flags);

	OMX_BUFFERHEADERTYPE* GetBuffer(void);
	void EmptyBuffer(OMX_BUFFERHEADERTYPE *buf);

	cOmx                 *m_omx;
	cMutex                m_bufferMutex;
	cCondWait            *m_newData;
	cCondWait            *m_newBuffer;
	bool                  m_reset;
	bool                  m_setStartTime;
	OMX_BUFFERHEADERTYPE *m_spareBuffers;
	AVCodec              *m_avCodec;
	AVCodecContext       *m_avContext;
	AVPacket*             m_packet;
	std::queue<AVPacket*> m_packets;
	cYuv422Geometry      *m_geometry;
};

#endif
