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

#include "video.h"
#include "tools.h"
#include "display.h"
#include "omx.h"

#include <vdr/tools.h>

const unsigned char cRpiVideoDecoder::s_mpeg2EndOfSequence[4]  = {
		0x00, 0x00, 0x01, 0xb7
};

const unsigned char cRpiVideoDecoder::s_h264EndOfSequence[8] = {
		0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x01, 0x0b
};

cRpiVideoDecoder::cRpiVideoDecoder(cVideoCodec::eCodec codec,
		void (*onStreamStart)(void*, const cVideoFrameFormat *format),
		void* onStreamStartData) :
	m_codec(codec),
	m_format(),
	m_onStreamStart(onStreamStart),
	m_onStreamStartData(onStreamStartData)
{
	m_format.width = 0;
	m_format.height = 0;
	m_format.frameRate = 0;
	m_format.scanMode = cScanMode::eProgressive;
	m_format.pixelWidth = 0;
	m_format.pixelHeight = 0;
}

cRpiVideoDecoder::~cRpiVideoDecoder()
{ }

/* ------------------------------------------------------------------------- */

cRpiOmxVideoDecoder::cRpiOmxVideoDecoder(cVideoCodec::eCodec codec, cOmx *omx,
		void (*onStreamStart)(void*, const cVideoFrameFormat *format),
		void* onStreamStartData) :
	cRpiVideoDecoder(codec, onStreamStart, onStreamStartData),
	m_omx(omx)
{
	DLOG("new OMX %s video codec", cVideoCodec::Str(codec));
	m_omx->SetVideoCodec(codec);
	m_omx->SetStreamStartCallback(OnStreamStart, this);
	m_omx->SetBufferStallCallback(OnBufferStall, this);
}

cRpiOmxVideoDecoder::~cRpiOmxVideoDecoder()
{
	Clear(true);
	m_omx->SetBufferStallCallback(0, 0);
	m_omx->SetStreamStartCallback(0, 0);
	m_omx->StopVideo();
}

bool cRpiOmxVideoDecoder::WriteData(const unsigned char *data,
		unsigned int length, int64_t pts, bool eof)
{
	while (length > 0)
	{
		if (OMX_BUFFERHEADERTYPE *buf =	m_omx->GetVideoBuffer(pts))
		{
			buf->nFilledLen = buf->nAllocLen < length ?	buf->nAllocLen : length;

			memcpy(buf->pBuffer, data, buf->nFilledLen);
			length -= buf->nFilledLen;
			data += buf->nFilledLen;

			if (eof && !length)
				buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

			if (!m_omx->EmptyVideoBuffer(buf))
			{
				ELOG("failed to pass buffer to video decoder!");
				return false;
			}
		}
		else
			return false;

		pts = OMX_INVALID_PTS;
	}
	return true;
}

bool cRpiOmxVideoDecoder::Poll(void)
{
	return m_omx->PollVideo();
}

void cRpiOmxVideoDecoder::Flush(void)
{
	DBG("SubmitEOS()");
	OMX_BUFFERHEADERTYPE *buf = m_omx->GetVideoBuffer(0);
	if (buf)
	{
		buf->nFlags = OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_ENDOFFRAME;
		buf->nFilledLen = m_codec == cVideoCodec::eMPEG2 ?
				sizeof(s_mpeg2EndOfSequence) : sizeof(s_h264EndOfSequence);
		memcpy(buf->pBuffer, m_codec == cVideoCodec::eMPEG2 ?
				s_mpeg2EndOfSequence : s_h264EndOfSequence, buf->nFilledLen);
	}
	if (!m_omx->EmptyVideoBuffer(buf))
		ELOG("failed to submit EOS packet!");
}

void cRpiOmxVideoDecoder::Clear(bool flushVideoRender)
{
	m_omx->FlushVideo(flushVideoRender);
}

void cRpiOmxVideoDecoder::HandleStreamStart(int width, int height,
		int frameRate, cScanMode::eMode scanMode,
		int pixelWidth, int pixelHeight)
{
	m_format.width = width;
	m_format.height = height;
	m_format.frameRate = frameRate;
	m_format.scanMode = scanMode;
	m_format.pixelWidth = pixelWidth;
	m_format.pixelHeight = pixelHeight;

	// forward to device instance
	if (m_onStreamStart)
		m_onStreamStart(m_onStreamStartData, &m_format);

	// if necessary, setup deinterlacer
	m_omx->SetupDeinterlacer(
			cRpiDisplay::IsProgressive() && m_format.Interlaced() ? (
					m_format.width * m_format.height > 576 * 720 ?
							cDeinterlacerMode::eFast :
							cDeinterlacerMode::eAdvanced) :
					cDeinterlacerMode::eDisabled);
}

void cRpiOmxVideoDecoder::HandleBufferStall(void)
{
	ILOG("Buffer stall!");
	Clear();
}

/* ------------------------------------------------------------------------- */
