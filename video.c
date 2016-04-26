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

const unsigned char cRpiVideoDecoder::s_mpeg2EndOfSequence[4] = {
		0x00, 0x00, 0x01, 0xb7
};

const unsigned char cRpiVideoDecoder::s_h264EndOfSequence[8] = {
		0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x01, 0x0b
};

const unsigned char cRpiVideoDecoder::s_h265EndOfSequence[5] = {
		0x00, 0x00, 0x01, 0x48, 0x01
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

extern "C" {
#include <libavcodec/avcodec.h>
}

#define FFMPEG_VIDEO_BUFFERS 32
#define NUM_INPUT_AVPACKETS 64

class cYuv422Geometry
{

public:

	void Set(int width, int height)
	{
		strideY = ALIGN_UP(width, 32);
		strideC = strideY / 2;
		heightY = ALIGN_UP(height, 16);
		heightC = heightY / 2;

		sizeY = strideY * heightY;
		sizeC = strideC * heightC;
	}

	int Size(void) { return sizeY + sizeC * 2; }

	int strideY;
	int heightY;
	int strideC;
	int heightC;
	int sizeY;
	int sizeC;
};

cRpiFfmpegVideoDecoder::cRpiFfmpegVideoDecoder(
		cVideoCodec::eCodec codec, cOmx *omx,
		void (*onStreamStart)(void*, const cVideoFrameFormat *format),
		void (*onEndOfStream)(void*), void* callbackData) :
	cRpiVideoDecoder(codec, onStreamStart, onEndOfStream, callbackData),
	cThread("video decoder"),
	m_omx(omx),
	m_bufferMutex(),
	m_newData(new cCondWait()),
	m_newBuffer(new cCondWait()),
	m_reset(false),
	m_setStartTime(true),
	m_spareBuffers(0),
	m_avCodec(0),
	m_avContext(0),
	m_packet(0),
	m_geometry(new cYuv422Geometry())
{
	m_avCodec = avcodec_find_decoder(
			codec == cVideoCodec::eMPEG2 ? AV_CODEC_ID_MPEG2VIDEO :
			codec == cVideoCodec::eH264  ? AV_CODEC_ID_H264 :
			codec == cVideoCodec::eH265  ? AV_CODEC_ID_HEVC :
					AV_CODEC_ID_NONE);
	if (!m_avCodec)
		ELOG("failed to get %s ffmpeg codec!", cVideoCodec::Str(codec));

	avcodec_register(m_avCodec);
	m_avContext = avcodec_alloc_context3(m_avCodec);
	if (!m_avContext)
		ELOG("failed to allocate %s ffmpeg context!", cVideoCodec::Str(codec));

	m_avContext->get_buffer2 = &GetBuffer2;
	m_avContext->opaque = this;

	m_avContext->thread_count = 4;
	m_avContext->thread_safe_callbacks = 1;

	if (!avcodec_open2(m_avContext, m_avCodec, NULL) < 0)
		ELOG("failed to open %s ffmpeg codec!", cVideoCodec::Str(codec));

	// create image_fx
	if (!m_omx->CreateComponent(cOmx::eVideoFx, true))
		ELOG("failed creating video fx!");

	m_omx->SetTunnel(cOmx::eVideoFxToVideoScheduler,
			cOmx::eVideoFx, 191, cOmx::eVideoScheduler, 10);

	if (!m_omx->ChangeComponentState(cOmx::eVideoFx, OMX_StateIdle))
		ELOG("failed to set video fx to idle state!");

	// setup clock tunnels first
	if (!m_omx->SetupTunnel(cOmx::eClockToVideoScheduler))
		ELOG("failed to setup up tunnel from clock to video scheduler!");

	m_omx->AddEventHandler(this);
	Start();
}

cRpiFfmpegVideoDecoder::~cRpiFfmpegVideoDecoder()
{
	Clear();
	Cancel(-1);

	while (Active())
		cCondWait::SleepMs(5);

	if (m_packet)
		av_packet_free(&m_packet);

	avcodec_close(m_avContext);
	avcodec_free_context(&m_avContext);

	if (m_format.width)
		m_omx->DisablePortBuffers(cOmx::eVideoFx, 190, m_spareBuffers);
	m_spareBuffers = 0;

	// put video fx into idle
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

	// clean up image_fx
	m_omx->CleanupComponent(cOmx::eVideoFx);

	m_omx->RemoveEventHandler(this);

	delete m_geometry;
	delete m_newBuffer;
	delete m_newData;
}

int cRpiFfmpegVideoDecoder::GetBuffer2(AVCodecContext *ctx,
		AVFrame *frame,	int flags)
{
	return (static_cast <cRpiFfmpegVideoDecoder*> (ctx->opaque))->GetAvBuffer(
			frame, flags);
}

void cRpiFfmpegVideoDecoder::FreeBuffer(void *opaque, uint8_t *data)
{
	OMX_BUFFERHEADERTYPE *buf = static_cast <OMX_BUFFERHEADERTYPE*> (opaque);
	static_cast <cRpiFfmpegVideoDecoder*> (buf->pAppPrivate)->EmptyBuffer(buf);
}

int cRpiFfmpegVideoDecoder::GetAvBuffer(AVFrame *frame, int flags)
{
	// we got the first decoded frame, so setup image fx to provide buffers
	if (!m_format.width)
	{
		m_format.width = frame->width;
		m_format.height = frame->height;
		m_format.pixelWidth = frame->sample_aspect_ratio.num;
		m_format.pixelHeight = frame->sample_aspect_ratio.den;
		m_format.scanMode = frame->interlaced_frame ?
				(frame->top_field_first ? cScanMode::eTopFieldFirst :
						cScanMode::eBottomFieldFirst) : cScanMode::eProgressive;
		m_format.frameRate =
				m_avContext->framerate.num / m_avContext->framerate.den;

		m_geometry->Set(frame->width, frame->height);

		// forward to device instanceà
		NotifyStreamStart();

		// configure video fx
		OMX_IMAGE_PARAM_PORTFORMATTYPE imageFormat;
		OMX_INIT_STRUCT(imageFormat);
		imageFormat.nPortIndex = 190;
		imageFormat.eCompressionFormat = OMX_IMAGE_CodingUnused;
		imageFormat.eColorFormat = OMX_COLOR_FormatYUV422PackedPlanar;

		if (!m_omx->SetParameter(cOmx::eVideoFx,
				OMX_IndexParamImagePortFormat, &imageFormat))
			ELOG("failed to set video fx parameters!");

		// configure video fx input parameters
		OMX_PARAM_PORTDEFINITIONTYPE portdef;
		OMX_INIT_STRUCT(portdef);
		portdef.nPortIndex = 190;
		if (!m_omx->GetParameter(cOmx::eVideoFx,
				OMX_IndexParamPortDefinition, &portdef))
			ELOG("failed to get video fx port definition!");

		portdef.nBufferSize = m_geometry->Size();
		portdef.nBufferCountActual = FFMPEG_VIDEO_BUFFERS;
		portdef.format.image.nFrameWidth = frame->width;
		portdef.format.image.nFrameHeight = frame->height;
		portdef.format.image.nStride = m_geometry->strideY;
		portdef.format.image.nSliceHeight = m_geometry->heightY;
		portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
		portdef.format.image.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

		if (!m_omx->SetParameter(cOmx::eVideoFx,
				OMX_IndexParamPortDefinition, &portdef))
			ELOG("failed to set video fx port definition!");

		// if necessary, configure deinterlacer
		cDeinterlacerMode::eMode mode =
				cRpiDisplay::IsProgressive() && m_format.Interlaced() ? (
						m_format.width * m_format.height > 576 * 720 ?
								cDeinterlacerMode::eFast :
								cDeinterlacerMode::eAdvanced) :
						cDeinterlacerMode::eDisabled;

		OMX_CONFIG_IMAGEFILTERPARAMSTYPE filterparam;
		OMX_INIT_STRUCT(filterparam);
		filterparam.nPortIndex = 191;
		filterparam.eImageFilter = OMX_ImageFilterNone;

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
		}
		if (!m_omx->SetConfig(cOmx::eVideoFx,
				OMX_IndexConfigCommonImageFilterParameters, &filterparam))
			ELOG("failed to set deinterlacing paramaters!");

		if (!m_omx->ChangeComponentState(cOmx::eVideoFx, OMX_StateExecuting))
			ELOG("failed to enable video fx!");

		if (!m_omx->EnablePortBuffers(cOmx::eVideoFx, 190))
			ELOG("failed to enable port buffer on video fx!");

		// set video render's pixel aspect ratio, allowed values are:
		// 10:11 (4:3 NTSC), 40:33 (16:9 NTSC),
		// 59:54 (4:3 PAL),  16:11 (16:9 PAL)
		OMX_CONFIG_POINTTYPE aspect;
		OMX_INIT_STRUCT(aspect);
		aspect.nPortIndex = 90;
		if (m_format.pixelWidth == 16 && m_format.pixelHeight == 15)
		{
			// PAL 4:3
			aspect.nX = 59;
			aspect.nY = 54;
		}
		else if (m_format.pixelWidth == 64 && m_format.pixelHeight == 45)
		{
			// PAL 16:9
			aspect.nX = 16;
			aspect.nY = 11;
		}
		if (!m_omx->SetParameter(cOmx::eVideoRender,
				OMX_IndexParamBrcmPixelAspectRatio, &aspect))
			ELOG("failed to set video render pixel aspect ratio!");
	}

	if (OMX_BUFFERHEADERTYPE *buf = GetBuffer())
	{
		for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
		{
			frame->buf[i] = NULL;
			frame->data[i] = NULL;
			frame->linesize[i] = 0;
		}

		// store information to free buffer
		frame->opaque = buf;
		buf->pAppPrivate = this;
		AVBufferRef *avBuf = av_buffer_create(buf->pBuffer, buf->nAllocLen,
				&FreeBuffer, buf, 0);

		if (!avBuf)
		{
			// to do: return buffer!
			ELOG("failed to allocate av buffer reference!");
			return -1;
		}
		frame->buf[0] = avBuf;
		frame->linesize[0] = m_geometry->strideY;
		frame->linesize[1] = m_geometry->strideC;
		frame->linesize[2] = m_geometry->strideC;
		frame->data[0] = avBuf->data;
		frame->data[1] = frame->data[0] + m_geometry->sizeY;
		frame->data[2] = frame->data[1] + m_geometry->sizeC;
		frame->extended_data = frame->data;
		return 0;
	}

	frame->opaque = 0;
	ELOG("failed to get image buffer!");
	return -1;
}

void cRpiFfmpegVideoDecoder::EmptyBuffer(OMX_BUFFERHEADERTYPE *buf)
{
	if (buf)
	{
#ifdef DEBUG_BUFFERS
		cOmx::DumpBuffer(buf, "V");
#endif
		if (!m_omx->EmptyBuffer(cOmx::eVideoFx, buf))
		{
			ELOG("failed to empty OMX video buffer");
			cMutexLock MutexLock(&m_bufferMutex);

			if (buf->nFlags & OMX_BUFFERFLAG_STARTTIME)
				m_setStartTime = true;

			buf->nFilledLen = 0;
			buf->pAppPrivate = m_spareBuffers;
			m_spareBuffers = buf;

			m_newBuffer->Signal();
		}
	}
}

bool cRpiFfmpegVideoDecoder::WriteData(const unsigned char *data,
		unsigned int length, int64_t pts, bool eof)
{
	bool ret = false;
	if (m_packets.size() < NUM_INPUT_AVPACKETS)
	{
		if (m_packet && m_packet->size > 0 && pts != OMX_INVALID_PTS)
		{
			Lock();
			m_packets.push(m_packet);
			m_newData->Signal();
			m_packet = 0;
			Unlock();
		}

		if (!m_packet)
			m_packet = av_packet_alloc();

		if (m_packet)
		{
			if (!av_grow_packet(m_packet, length))
			{
				if (pts != OMX_INVALID_PTS)
					m_packet->pts = pts;

				memcpy(m_packet->data + m_packet->size - length, data, length);
				ret = true;
			}
			else
				ELOG("failed to allocate video packet buffer!");
		}
		else
			ELOG("failed to allocate video packet!");
	}
	return ret;
}

bool cRpiFfmpegVideoDecoder::Poll(void)
{
	return m_packets.size() < NUM_INPUT_AVPACKETS * 100 / 90;
}

void cRpiFfmpegVideoDecoder::Clear(bool flushRender)
{
	Lock();
	av_packet_free(&m_packet);
	while (!m_packets.empty())
	{
		av_packet_free(&m_packets.front());
		m_packets.pop();
	}
	m_reset = true;
	m_newData->Signal();
	m_newBuffer->Signal();

	while (m_reset)
		cCondWait::SleepMs(5);

	if (!m_omx->FlushComponent(cOmx::eVideoFx, 190))
		ELOG("failed to flush video fx!");

	m_omx->FlushTunnel(cOmx::eVideoFxToVideoScheduler);

	if (flushRender)
		m_omx->FlushTunnel(cOmx::eVideoSchedulerToVideoRender);

	m_omx->FlushTunnel(cOmx::eClockToVideoScheduler);

	m_setStartTime = true;
	Unlock();
}

void cRpiFfmpegVideoDecoder::Flush(void)
{
	Lock();
	if (m_packet && m_packet->size > 0)
	{
		m_packets.push(m_packet);
		m_packet = 0;
	}

	AVPacket* packet = av_packet_alloc();
	if (packet)
	{
		if (!av_new_packet(packet,
				m_codec == cVideoCodec::eH264 ? sizeof(s_h264EndOfSequence) :
				m_codec == cVideoCodec::eH265 ? sizeof(s_h265EndOfSequence) :
						sizeof(s_mpeg2EndOfSequence)))
		{
			memcpy(packet->data,
					m_codec == cVideoCodec::eH264 ? s_h264EndOfSequence :
					m_codec == cVideoCodec::eH265 ? s_h265EndOfSequence :
							s_mpeg2EndOfSequence, packet->size);

			packet->pts = AV_NOPTS_VALUE;
			m_packets.push(packet);
		}
		else
			ELOG("failed to allocate EOS video packet buffer!");
	}
	else
		ELOG("failed to allocate EOS video packet!");

	m_newData->Signal();
	Unlock();
}

void cRpiFfmpegVideoDecoder::Action(void)
{
	AVPacket *packet = 0;
	AVFrame *frame = av_frame_alloc();

	while (frame && Running())
	{
		if (m_reset)
		{
			if (packet)
				av_packet_free(&packet);

			av_frame_unref(frame);
			m_reset = false;
		}

		else if (!packet && !m_packets.empty())
		{
			Lock();
			packet = m_packets.front();
			m_packets.pop();
			Unlock();
		}

		else if (packet)
		{
			int frameComplete = 0;
			int read = avcodec_decode_video2(m_avContext, frame, &frameComplete,
					packet);

			if (read < 0)
				m_reset = true;
			else
			{
				av_packet_free(&packet);

				if (frameComplete)
				{
					OMX_BUFFERHEADERTYPE *buf =
							static_cast <OMX_BUFFERHEADERTYPE*> (frame->opaque);

					int64_t pts = av_frame_get_best_effort_timestamp(frame);

					if (!m_setStartTime || pts != AV_NOPTS_VALUE)
						buf->nFilledLen = buf->nAllocLen;

					if (pts == AV_NOPTS_VALUE)
						buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
					else
					{
						cOmx::PtsToTicks(pts, buf->nTimeStamp);
						if (m_setStartTime)
						{
							buf->nFlags |= OMX_BUFFERFLAG_STARTTIME;
							m_setStartTime = false;
						}
					}
					buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
				}
			}
		}
		else
			// nothing to be done...
			m_newData->Wait(20);
	}

	if (packet)
		av_packet_free(&packet);
	if (frame)
		av_frame_free(&frame);
}

void cRpiFfmpegVideoDecoder::BufferEmptied(cOmx::eOmxComponent comp)
{
	if (comp == cOmx::eVideoFx)
		m_newBuffer->Signal();
}

OMX_BUFFERHEADERTYPE* cRpiFfmpegVideoDecoder::GetBuffer(void)
{
	OMX_BUFFERHEADERTYPE* buf = 0;
	while (!buf && Active() && !m_reset)
	{
		{
			cMutexLock MutexLock(&m_bufferMutex);
			if (m_spareBuffers)
			{
				buf = m_spareBuffers;
				m_spareBuffers =
						static_cast <OMX_BUFFERHEADERTYPE*> (buf->pAppPrivate);
				buf->pAppPrivate = 0;
			}
			else
				buf = m_omx->GetBuffer(cOmx::eVideoFx, 190);
		}
		if (buf)
		{
			buf->nFilledLen = 0;
			buf->nOffset = 0;
			buf->nFlags = 0;
		}
		else
			m_newBuffer->Wait(10);
	}
	return buf;
}
