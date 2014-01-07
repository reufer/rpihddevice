/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "audio.h"
#include "setup.h"

#include <vdr/tools.h>
#include <vdr/remux.h>

#include <string.h>

cAudioDecoder::cAudioDecoder() :
	m_codec(ePCM),
	m_outputFormat(ePCM),
	m_outputPort(eLocal),
	m_channels(0),
	m_samplingRate(0),
	m_passthrough(false),
	m_frame(0),
	m_mutex(new cMutex()) { }

cAudioDecoder::~cAudioDecoder()
{
	delete m_mutex;
}

int cAudioDecoder::Init(void)
{
	int ret = 0;

	avcodec_register_all();

	m_codecs[ePCM ].codec = NULL;
	m_codecs[eMPG ].codec = avcodec_find_decoder(CODEC_ID_MP3);
	m_codecs[eAC3 ].codec = avcodec_find_decoder(CODEC_ID_AC3);
	m_codecs[eEAC3].codec = avcodec_find_decoder(CODEC_ID_EAC3);
	m_codecs[eAAC ].codec = avcodec_find_decoder(CODEC_ID_AAC_LATM);
	m_codecs[eDTS ].codec = avcodec_find_decoder(CODEC_ID_DTS);

	for (int i = 0; i < eNumCodecs; i++)
	{
		eCodec codec = static_cast<eCodec>(i);
		if (m_codecs[codec].codec)
		{
			m_codecs[codec].context = avcodec_alloc_context3(m_codecs[codec].codec);
			if (!m_codecs[codec].context)
			{
				esyslog("rpihddevice: failed to allocate %s context!", CodecStr(codec));
				ret = -1;
				break;
			}
			if (avcodec_open2(m_codecs[codec].context, m_codecs[codec].codec, NULL) < 0)
			{
				esyslog("rpihddevice: failed to open %s decoder!", CodecStr(codec));
				ret = -1;
				break;
			}
		}
	}

	m_frame = avcodec_alloc_frame();
	if (!m_frame)
	{
		esyslog("rpihddevice: failed to allocate audio frame!");
		ret = -1;
	}

	if (ret < 0)
		DeInit();

	return ret;
}

int cAudioDecoder::DeInit(void)
{
	for (int i = 0; i < eNumCodecs; i++)
	{
		eCodec codec = static_cast<eCodec>(i);
		avcodec_close(m_codecs[codec].context);
		av_free(m_codecs[codec].context);
	}

	av_free(m_frame);
	return 0;
}

bool cAudioDecoder::SetupAudioCodec(const unsigned char *data, int length)
{
	m_mutex->Lock();

	bool ret = false;

	// try to decode audio sample
	AVPacket avpkt;
	av_init_packet(&avpkt);
	avpkt.data = (unsigned char *)(data + PesPayloadOffset(data));
	avpkt.size = PesLength(data) - PesPayloadOffset(data);

	for (int i = 0; i < eNumCodecs; i++)
	{
		eCodec codec = static_cast<eCodec>(i);
		if (m_codecs[codec].codec)
		{
			int frame = 0;
			avcodec_get_frame_defaults(m_frame);

			m_codecs[codec].context->request_channel_layout = AV_CH_LAYOUT_NATIVE;
			m_codecs[codec].context->request_channels = 0;

			int len = avcodec_decode_audio4(m_codecs[codec].context, m_frame, &frame, &avpkt);

			if (len > 0 && frame)
			{
				m_codec = codec;
				m_channels =  m_codecs[m_codec].context->channels;
				m_samplingRate = m_codecs[m_codec].context->sample_rate;
				dsyslog("rpihddevice: set audio codec to %s with %d channels, %dHz",
						CodecStr(m_codec), m_channels, m_samplingRate);

				m_passthrough = false;
				m_outputFormat = ePCM;
				m_outputPort = eLocal;

				if (cRpiSetup::GetAudioPort() == eHDMI &&
					cRpiSetup::IsAudioFormatSupported(ePCM, 2, 48000))
				{
					m_outputPort = eHDMI;

					if (cRpiSetup::IsAudioPassthrough() &&
						cRpiSetup::IsAudioFormatSupported(m_codec, m_channels, m_samplingRate))
					{
						m_passthrough = true;
						m_outputFormat = m_codec;
					}
					dsyslog("rpihddevice: set HDMI audio output format to %s%s",
							CodecStr(m_outputFormat), m_passthrough ? " (pass-through)" : "");
				}
				else
				{
					m_codecs[m_codec].context->request_channel_layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
					m_codecs[m_codec].context->request_channels = 2;
					m_channels = 2;
					dsyslog("rpihddevice: set analog audio output format to PCM stereo");
				}

				ret = true;
				break;
			}
		}
	}
	m_mutex->Unlock();
	return ret;
}

unsigned int cAudioDecoder::DecodeAudio(const unsigned char *data, int length, unsigned char *outbuf, int bufsize)
{
	m_mutex->Lock();

	if (m_passthrough)
	{
		if (length > bufsize)
			esyslog("rpihddevice: pass-through audio frame is bigger than output buffer!");
		else
			memcpy(outbuf, data, length);

		return length;
	}

	AVPacket avpkt;
	av_init_packet(&avpkt);

	avpkt.data = (unsigned char *)data;
	avpkt.size = length;

	unsigned int outsize = 0;

	while (avpkt.size > 0)
	{
		int frame = 0;
		avcodec_get_frame_defaults(m_frame);

		int len = avcodec_decode_audio4(m_codecs[m_codec].context, m_frame, &frame, &avpkt);
		if (len < 0)
			break;

		if (frame)
		{
			unsigned int framesize = av_samples_get_buffer_size(NULL,
					m_channels == 6 ? 8 : m_channels, m_frame->nb_samples,
					m_codecs[m_codec].context->sample_fmt, 1);

			if (outsize + framesize <= bufsize)
			{
				if (m_channels == 6)
				{
					// interleaved copy to fit 5.1 data into 8 channels
					int32_t* src = (int32_t*)m_frame->data[0];
					int32_t* dst = (int32_t*)outbuf;

					for (int i = 0; i < m_frame->nb_samples; i++)
					{
						*dst++ = *src++; // LF & RF
						*dst++ = *src++; // CF & LFE
						*dst++ = *src++; // LR & RR
						*dst++ = 0;      // empty channels
					}
				}
				else
					memcpy(outbuf, m_frame->data[0], framesize);

				outsize += framesize;
				outbuf  += framesize;
			}
			else
			{
				esyslog("rpihddevice: decoded audio frame is bigger than output buffer!");
				break;
			}
		}
		avpkt.size -= len;
		avpkt.data += len;
	}

	m_mutex->Unlock();
	return outsize;
}
