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

/* taken from vcinclude/common.h */
#define ALIGN_DOWN(p, n) ((uintptr_t)(p) - ((uintptr_t)(p) % (uintptr_t)(n)))
#define ALIGN_UP(p, n)   ALIGN_DOWN((uintptr_t)(p) + (uintptr_t)(n) - 1, (n))

const unsigned char cRpiVideoDecoder::s_mpeg2EndOfSequence[4]  = {
		0x00, 0x00, 0x01, 0xb7
};

const unsigned char cRpiVideoDecoder::s_h264EndOfSequence[8] = {
		0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x01, 0x0b
};

cRpiVideoDecoder::cRpiVideoDecoder(cVideoCodec::eCodec codec,
		void (*onStreamStart)(void*, const cVideoFrameFormat *format),
		void (*onEndOfStream)(void*), void* callbackData) :
	m_codec(codec),
	m_format(),
	m_onStreamStart(onStreamStart),
	m_onEndOfStream(onEndOfStream),
	m_callbackData(callbackData)
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

void cRpiVideoDecoder::NotifyStreamStart(void)
{
	if (m_onStreamStart)
		m_onStreamStart(m_callbackData, &m_format);
}

void cRpiVideoDecoder::NotifyEndOfStream(void)
{
	if (m_onEndOfStream)
		m_onEndOfStream(m_callbackData);
}

/* ------------------------------------------------------------------------- */

cRpiOmxVideoDecoder::cRpiOmxVideoDecoder(cVideoCodec::eCodec codec, cOmx *omx,
		void (*onStreamStart)(void*, const cVideoFrameFormat *format),
		void (*onEndOfStream)(void*), void* callbackData) :
	cRpiVideoDecoder(codec, onStreamStart, onEndOfStream, callbackData),
	cOmxEventHandler(),
	m_omx(omx)
{
	ILOG("new OMX %s video codec", cVideoCodec::Str(codec));
	m_omx->AddEventHandler(this);
	m_omx->SetVideoCodec(codec);
}

cRpiOmxVideoDecoder::~cRpiOmxVideoDecoder()
{
	Clear(true);
	m_omx->StopVideo();
	m_omx->RemoveEventHandler(this);
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

void cRpiOmxVideoDecoder::PortSettingsChanged(int port)
{
	switch (port)
	{
	case 131:
		OMX_PARAM_PORTDEFINITIONTYPE portdef;
		OMX_INIT_STRUCT(portdef);
		portdef.nPortIndex = 131;
		if (!m_omx->GetParameter(cOmx::eVideoDecoder, OMX_IndexParamPortDefinition, &portdef))
			ELOG("failed to get video decoder port format!");

		OMX_CONFIG_POINTTYPE pixel;
		OMX_INIT_STRUCT(pixel);
		pixel.nPortIndex = 131;
		if (!m_omx->GetParameter(cOmx::eVideoDecoder, OMX_IndexParamBrcmPixelAspectRatio, &pixel))
			ELOG("failed to get pixel aspect ratio!");

		OMX_CONFIG_INTERLACETYPE interlace;
		OMX_INIT_STRUCT(interlace);
		interlace.nPortIndex = 131;
		if (!m_omx->GetConfig(cOmx::eVideoDecoder, OMX_IndexConfigCommonInterlace, &interlace))
			ELOG("failed to get video decoder interlace config!");
		{
			cScanMode::eMode scanMode =
					interlace.eMode == OMX_InterlaceProgressive ? cScanMode::eProgressive :
					interlace.eMode == OMX_InterlaceFieldSingleUpperFirst ? cScanMode::eTopFieldFirst :
					interlace.eMode == OMX_InterlaceFieldSingleLowerFirst ? cScanMode::eBottomFieldFirst :
					interlace.eMode == OMX_InterlaceFieldsInterleavedUpperFirst ? cScanMode::eTopFieldFirst :
					interlace.eMode == OMX_InterlaceFieldsInterleavedLowerFirst ? cScanMode::eBottomFieldFirst :
							cScanMode::eProgressive;

			// discard 4 least significant bits, since there might be some
			// deviation due to jitter in time stamps
			int frameRate = ALIGN_UP(portdef.format.video.xFramerate & 0xfffffff0, 1 << 16) >> 16;

			if (cScanMode::Interlaced(scanMode))
				frameRate = frameRate * 2;

			m_format.width = portdef.format.video.nFrameWidth;
			m_format.height = portdef.format.video.nFrameHeight;
			m_format.frameRate = frameRate;
			m_format.scanMode = scanMode;
			m_format.pixelWidth = pixel.nX;
			m_format.pixelHeight = pixel.nY;

			// forward to device instance
			NotifyStreamStart();

			// if necessary, setup deinterlacer
			m_omx->SetupDeinterlacer(
					cRpiDisplay::IsProgressive() && m_format.Interlaced() ? (
							m_format.width * m_format.height > 576 * 720 ?
									cDeinterlacerMode::eFast :
									cDeinterlacerMode::eAdvanced) :
							cDeinterlacerMode::eDisabled);
		}

		if (!m_omx->SetupTunnel(cOmx::eVideoDecoderToVideoFx, 0))
			ELOG("failed to setup up tunnel from video decoder to fx!");
		if (!m_omx->ChangeState(cOmx::eVideoFx, OMX_StateExecuting))
			ELOG("failed to enable video fx!");

		break;
	}
}

void cRpiOmxVideoDecoder::EndOfStreamReceived(int port)
{
	if (port == 90) // input port of OMX video render
		NotifyEndOfStream();
}

void cRpiOmxVideoDecoder::BufferEmptied(cOmx::eOmxComponent comp)
{

}

void cRpiOmxVideoDecoder::BufferStalled(void)
{
	ILOG("Buffer stall!");
	Clear();
}

/* ------------------------------------------------------------------------- */
