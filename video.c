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

// default: 20x 81920 bytes, now 128x 64k (8M)
#define OMX_VIDEO_BUFFERS 128
#define OMX_VIDEO_BUFFERSIZE KILOBYTE(64);

#define OMX_VIDEO_BUFFERSTALL_TIMEOUT_MS 20000

cRpiOmxVideoDecoder::cRpiOmxVideoDecoder(cVideoCodec::eCodec codec, cOmx *omx,
		void (*onStreamStart)(void*, const cVideoFrameFormat *format),
		void (*onEndOfStream)(void*), void* callbackData) :
	cRpiVideoDecoder(codec, onStreamStart, onEndOfStream, callbackData),
	cOmxEventHandler(),
	m_omx(omx),
	m_setDiscontinuity(true),
	m_setStartTime(true),
	m_spareBuffers(0)
{
	ILOG("new OMX %s video codec", cVideoCodec::Str(codec));

	// create video_decode
	if (!m_omx->CreateComponent(cOmx::eVideoDecoder, true))
		ELOG("failed creating video decoder!");

	// create image_fx
	if (!m_omx->CreateComponent(cOmx::eVideoFx))
		ELOG("failed creating video fx!");

	m_omx->SetTunnel(cOmx::eVideoDecoderToVideoFx,
			cOmx::eVideoDecoder, 131, cOmx::eVideoFx, 190);

	m_omx->SetTunnel(cOmx::eVideoFxToVideoScheduler,
			cOmx::eVideoFx, 191, cOmx::eVideoScheduler, 10);

	if (!m_omx->ChangeComponentState(cOmx::eVideoDecoder, OMX_StateIdle))
		ELOG("failed to set video decoder to idle state!");

	if (!m_omx->ChangeComponentState(cOmx::eVideoFx, OMX_StateIdle))
		ELOG("failed to set video fx to idle state!");

	OMX_CONFIG_BUFFERSTALLTYPE stallConf;
	OMX_INIT_STRUCT(stallConf);
	stallConf.nPortIndex = 131;
	stallConf.nDelay = OMX_VIDEO_BUFFERSTALL_TIMEOUT_MS * 1000;
	if (!m_omx->SetConfig(cOmx::eVideoDecoder,
			OMX_IndexConfigBufferStall, &stallConf))
		ELOG("failed to set video decoder stall config!");

	// set buffer stall call back
	OMX_CONFIG_REQUESTCALLBACKTYPE reqCallback;
	OMX_INIT_STRUCT(reqCallback);
	reqCallback.nPortIndex = 131;
	reqCallback.nIndex = OMX_IndexConfigBufferStall;
	reqCallback.bEnable = OMX_TRUE;
	if (!m_omx->SetConfig(cOmx::eVideoDecoder,
			OMX_IndexConfigRequestCallback, &reqCallback))
		ELOG("failed to set video decoder stall call back!");

	// configure video decoder
	OMX_VIDEO_PARAM_PORTFORMATTYPE videoFormat;
	OMX_INIT_STRUCT(videoFormat);
	videoFormat.nPortIndex = 130;
	videoFormat.eCompressionFormat =
			codec == cVideoCodec::eMPEG2 ? OMX_VIDEO_CodingMPEG2 :
			codec == cVideoCodec::eH264  ? OMX_VIDEO_CodingAVC   :
					OMX_VIDEO_CodingAutoDetect;

	if (!m_omx->SetParameter(cOmx::eVideoDecoder,
			OMX_IndexParamVideoPortFormat, &videoFormat))
		ELOG("failed to set video decoder parameters!");

	OMX_PARAM_PORTDEFINITIONTYPE param;
	OMX_INIT_STRUCT(param);
	param.nPortIndex = 130;
	if (!m_omx->GetParameter(cOmx::eVideoDecoder,
			OMX_IndexParamPortDefinition, &param))
		ELOG("failed to get video decoder port parameters!");

	param.nBufferSize = OMX_VIDEO_BUFFERSIZE;
	param.nBufferCountActual = OMX_VIDEO_BUFFERS;
	for (int i = 0; i < BUFFERSTAT_FILTER_SIZE; i++)
		m_usedBuffers[i] = 0;

	if (!m_omx->SetParameter(cOmx::eVideoDecoder,
			OMX_IndexParamPortDefinition, &param))
		ELOG("failed to set video decoder port parameters!");

	OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ectype;
	OMX_INIT_STRUCT(ectype);
	ectype.bStartWithValidFrame = OMX_TRUE;
	if (!m_omx->SetParameter(cOmx::eVideoDecoder,
			OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ectype))
		ELOG("failed to set video decode error concealment failed!");

	if (!m_omx->EnablePortBuffers(cOmx::eVideoDecoder, 130))
		ELOG("failed to enable port buffer on video decoder!");

	if (!m_omx->ChangeComponentState(cOmx::eVideoDecoder, OMX_StateExecuting))
		ELOG("failed to set video decoder to executing state!");

	// setup clock tunnels first
	if (!m_omx->SetupTunnel(cOmx::eClockToVideoScheduler))
		ELOG("failed to setup up tunnel from clock to video scheduler!");

	m_omx->AddEventHandler(this);
}

cRpiOmxVideoDecoder::~cRpiOmxVideoDecoder()
{
	Clear(true);

	// disable port buffers
	m_omx->DisablePortBuffers(cOmx::eVideoDecoder, 130, m_spareBuffers);
	m_spareBuffers = 0;

	// put video decoder into idle
	m_omx->ChangeComponentState(cOmx::eVideoDecoder, OMX_StateIdle);

	// put video fx into idle
	m_omx->FlushTunnel(cOmx::eVideoDecoderToVideoFx);
	m_omx->DisableTunnel(cOmx::eVideoDecoderToVideoFx);
	m_omx->ChangeComponentState(cOmx::eVideoFx, OMX_StateIdle);

	// put video scheduler into idle
	m_omx->FlushTunnel(cOmx::eVideoFxToVideoScheduler);
	m_omx->DisableTunnel(cOmx::eVideoFxToVideoScheduler);
	m_omx->FlushTunnel(cOmx::eClockToVideoScheduler);
	m_omx->DisableTunnel(cOmx::eClockToVideoScheduler);
	m_omx->ChangeComponentState(cOmx::eVideoScheduler, OMX_StateIdle);

	// put video render into idle
	m_omx->FlushTunnel(cOmx::eVideoSchedulerToVideoRender);
	m_omx->DisableTunnel(cOmx::eVideoSchedulerToVideoRender);
	m_omx->ChangeComponentState(cOmx::eVideoRender, OMX_StateIdle);

	// clean up image_fx & decoder
	m_omx->CleanupComponent(cOmx::eVideoFx);
	m_omx->CleanupComponent(cOmx::eVideoDecoder);

	m_omx->RemoveEventHandler(this);
}

bool cRpiOmxVideoDecoder::WriteData(const unsigned char *data,
		unsigned int length, int64_t pts, bool eof)
{
	while (length > 0)
	{
		if (OMX_BUFFERHEADERTYPE *buf =	GetBuffer())
		{
			if (pts == OMX_INVALID_PTS)
				buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
			else if (m_setStartTime)
			{
				buf->nFlags |= OMX_BUFFERFLAG_STARTTIME;
				m_setStartTime = false;
			}
			if (m_setDiscontinuity)
			{
				buf->nFlags |= OMX_BUFFERFLAG_DISCONTINUITY;
				m_setDiscontinuity = false;
			}
			cOmx::PtsToTicks(pts, buf->nTimeStamp);

			buf->nFilledLen = buf->nAllocLen < length ?	buf->nAllocLen : length;

			memcpy(buf->pBuffer, data, buf->nFilledLen);
			length -= buf->nFilledLen;
			data += buf->nFilledLen;

			if (eof && !length)
				buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

			if (!EmptyBuffer(buf))
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
	return (m_usedBuffers[0] * 100 / OMX_VIDEO_BUFFERS) < 90;
}

void cRpiOmxVideoDecoder::Flush(void)
{
	DBG("SubmitEOS()");
	OMX_BUFFERHEADERTYPE *buf = GetBuffer();
	if (buf)
	{
		cOmx::PtsToTicks(0, buf->nTimeStamp);
		buf->nFlags = OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_ENDOFFRAME |
				OMX_BUFFERFLAG_TIME_UNKNOWN;
		buf->nFilledLen = m_codec == cVideoCodec::eMPEG2 ?
				sizeof(s_mpeg2EndOfSequence) : sizeof(s_h264EndOfSequence);
		memcpy(buf->pBuffer, m_codec == cVideoCodec::eMPEG2 ?
				s_mpeg2EndOfSequence : s_h264EndOfSequence, buf->nFilledLen);
	}
	if (!EmptyBuffer(buf))
		ELOG("failed to submit EOS packet!");
}

int cRpiOmxVideoDecoder::GetBufferUsage(void)
{
	int usage = 0;
	for (int i = 0; i < BUFFERSTAT_FILTER_SIZE; i++)
		usage += m_usedBuffers[i];

	return usage * 100 / BUFFERSTAT_FILTER_SIZE / OMX_VIDEO_BUFFERS;
}

void cRpiOmxVideoDecoder::Clear(bool flushRender)
{
	if (!m_omx->FlushComponent(cOmx::eVideoDecoder, 130))
		ELOG("failed to flush video decoder!");

	m_omx->FlushTunnel(cOmx::eVideoDecoderToVideoFx);

	if (!m_omx->FlushComponent(cOmx::eVideoFx, 190))
		ELOG("failed to flush video fx!");

	m_omx->FlushTunnel(cOmx::eVideoFxToVideoScheduler);

	if (flushRender)
		m_omx->FlushTunnel(cOmx::eVideoSchedulerToVideoRender);

	m_omx->FlushTunnel(cOmx::eClockToVideoScheduler);

	m_setDiscontinuity = true;
	m_setStartTime = true;
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
			SetupDeinterlacer(
					cRpiDisplay::IsProgressive() && m_format.Interlaced() ? (
							m_format.width * m_format.height > 576 * 720 ?
									cDeinterlacerMode::eFast :
									cDeinterlacerMode::eAdvanced) :
							cDeinterlacerMode::eDisabled);
		}

		if (!m_omx->SetupTunnel(cOmx::eVideoDecoderToVideoFx, 0))
			ELOG("failed to setup up tunnel from video decoder to fx!");
		if (!m_omx->ChangeComponentState(cOmx::eVideoFx, OMX_StateExecuting))
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
	if (comp == cOmx::eVideoDecoder)
	{
		cMutexLock MutexLock(&m_mutex);
		m_usedBuffers[0]--;
	}
}

void cRpiOmxVideoDecoder::BufferStalled(void)
{
	ILOG("Buffer stall!");
	Clear();
}

void cRpiOmxVideoDecoder::Tick(void)
{
	for (int i = BUFFERSTAT_FILTER_SIZE - 1; i > 0; i--)
		m_usedBuffers[i] = m_usedBuffers[i - 1];
}

OMX_BUFFERHEADERTYPE* cRpiOmxVideoDecoder::GetBuffer(void)
{
	OMX_BUFFERHEADERTYPE* buf = 0;
	cMutexLock MutexLock(&m_mutex);

	if (m_spareBuffers)
	{
		buf = m_spareBuffers;
		m_spareBuffers = static_cast <OMX_BUFFERHEADERTYPE*>(buf->pAppPrivate);
		buf->pAppPrivate = 0;
	}
	else
	{
		buf = m_omx->GetBuffer(cOmx::eVideoDecoder, 130);
		if (buf)
			m_usedBuffers[0]++;
	}

	if (buf)
	{
		buf->nFilledLen = 0;
		buf->nOffset = 0;
		buf->nFlags = 0;
	}
	return buf;
}

bool cRpiOmxVideoDecoder::EmptyBuffer(OMX_BUFFERHEADERTYPE *buf)
{
	bool ret = true;

#ifdef DEBUG_BUFFERS
	cOmx::DumpBuffer(buf, "V");
#endif

	if (!m_omx->EmptyBuffer(cOmx::eVideoDecoder, buf))
	{
		ELOG("failed to empty OMX video buffer");
		cMutexLock MutexLock(&m_mutex);

		if (buf->nFlags & OMX_BUFFERFLAG_STARTTIME)
			m_setStartTime = true;

		buf->nFilledLen = 0;
		buf->pAppPrivate = m_spareBuffers;
		m_spareBuffers = buf;
		ret = false;
	}
	return ret;
}

void cRpiOmxVideoDecoder::SetupDeinterlacer(cDeinterlacerMode::eMode mode)
{
	DBG("SetupDeinterlacer(%s)", cDeinterlacerMode::Str(mode));

	OMX_CONFIG_IMAGEFILTERPARAMSTYPE filterparam;
	OMX_INIT_STRUCT(filterparam);
	filterparam.nPortIndex = 191;
	filterparam.eImageFilter = OMX_ImageFilterNone;

	OMX_PARAM_U32TYPE extraBuffers;
	OMX_INIT_STRUCT(extraBuffers);
	extraBuffers.nPortIndex = 130;

	if (cDeinterlacerMode::Active(mode))
	{
		filterparam.nNumParams = 4;
		filterparam.nParams[0] = 3;
		filterparam.nParams[1] = 0; // default frame interval
		filterparam.nParams[2] = 0; // half framerate
		filterparam.nParams[3] = 1; // use qpus

		filterparam.eImageFilter = mode == cDeinterlacerMode::eFast ?
				OMX_ImageFilterDeInterlaceFast :
				OMX_ImageFilterDeInterlaceAdvanced;

		if (mode == cDeinterlacerMode::eFast)
			extraBuffers.nU32 = -2;
	}
	if (!m_omx->SetConfig(cOmx::eVideoFx,
			OMX_IndexConfigCommonImageFilterParameters, &filterparam))
		ELOG("failed to set deinterlacing paramaters!");

	if (!m_omx->SetParameter(cOmx::eVideoFx,
			OMX_IndexParamBrcmExtraBuffers, &extraBuffers))
		ELOG("failed to set video fx extra buffers!");
}

/* ------------------------------------------------------------------------- */
