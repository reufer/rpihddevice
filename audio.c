/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "audio.h"
#include "setup.h"
#include "omx.h"

#include <vdr/tools.h>
#include <vdr/remux.h>

#include <string.h>

class cAudioParser
{

public:

	cAudioParser() { }
	~cAudioParser() { }

	inline AVPacket*      Packet(void) { return &m_packet;             }
	inline unsigned int   Size(void)   { return m_packet.stream_index; }
	inline unsigned char *Data(void)   { return m_packet.data;         }

	int Init(void)
	{
		return av_new_packet(&m_packet, 64 * 1024 /* 1 PES packet */);
	}

	int DeInit(void)
	{
		av_free_packet(&m_packet);
		return 0;
	}

	void Reset(void)
	{
		m_packet.stream_index = 0;
		memset(m_packet.data, 0, FF_INPUT_BUFFER_PADDING_SIZE);
	}

	bool Append(const unsigned char *data, unsigned int length)
	{
		if (m_packet.stream_index + length + FF_INPUT_BUFFER_PADDING_SIZE > m_packet.size)
			return false;

		memcpy(m_packet.data + m_packet.stream_index, data, length);
		m_packet.stream_index += length;
		memset(m_packet.data + m_packet.stream_index, 0, FF_INPUT_BUFFER_PADDING_SIZE);
		return true;
	}

	void Shrink(unsigned int length)
	{
		if (length < m_packet.stream_index)
		{
			memmove(m_packet.data, m_packet.data + length, m_packet.stream_index - length);
			m_packet.stream_index -= length;
			memset(m_packet.data + m_packet.stream_index, 0, FF_INPUT_BUFFER_PADDING_SIZE);
		}
		else
			Reset();
	}
	
	cAudioCodec::eCodec Parse(unsigned int &offset)
	{
		cAudioCodec::eCodec codec = cAudioCodec::eInvalid;
		
		while (Size() - offset >= 5)
		{
			const uint8_t *p = Data() + offset;
			int n = Size() - offset;
			int r = 0;

			// 4 bytes 0xFFExxxxx MPEG audio
			// 3 bytes 0x56Exxx AAC LATM audio
			// 5 bytes 0x0B77xxxxxx AC-3 audio
			// 6 bytes 0x0B77xxxxxxxx E-AC-3 audio
			// 7/9 bytes 0xFFFxxxxxxxxxxx ADTS audio
			// PCM audio can't be found

			if (FastMpegCheck(p))
			{
				r = MpegCheck(p, n);
				codec = cAudioCodec::eMPG;
			}
			else if (FastAc3Check(p))
			{
				r = Ac3Check(p, n);
				codec = cAudioCodec::eAC3;

				if (r > 0 && p[5] > (10 << 3))
					codec = cAudioCodec::eEAC3;
			}
			else if (FastLatmCheck(p))
			{
				r = LatmCheck(p, n);
				codec = cAudioCodec::eAAC;
			}
			else if (FastAdtsCheck(p))
			{
				r = AdtsCheck(p, n);
				codec = cAudioCodec::eDTS;
			}

			if (r < 0)	// need more bytes
				break;

			if (r > 0)
				return codec;

			++offset;
		}
		return cAudioCodec::eInvalid;
	}

private:

	AVPacket m_packet;

	/* ------------------------------------------------------------------------- */
	/*     audio codec parser helper functions, taken from vdr-softhddevice      */
	/* ------------------------------------------------------------------------- */

	static const uint16_t BitRateTable[2][3][16];
	static const uint16_t SampleRateTable[4];
	static const uint16_t Ac3FrameSizeTable[38][3];

	///
	///	Fast check for MPEG audio.
	///
	///	4 bytes 0xFFExxxxx MPEG audio
	///
	static bool FastMpegCheck(const uint8_t *p)
	{
		if (p[0] != 0xFF)			// 11bit frame sync
			return false;
		if ((p[1] & 0xE0) != 0xE0)
			return false;
		if ((p[1] & 0x18) == 0x08)	// version ID - 01 reserved
			return false;
		if (!(p[1] & 0x06))			// layer description - 00 reserved
			return false;
		if ((p[2] & 0xF0) == 0xF0)	// bit rate index - 1111 reserved
			return false;
		if ((p[2] & 0x0C) == 0x0C)	// sampling rate index - 11 reserved
			return false;
		return true;
	}

	///	Check for MPEG audio.
	///
	///	0xFFEx already checked.
	///
	///	@param data	incomplete PES packet
	///	@param size	number of bytes
	///
	///	@retval <0	possible MPEG audio, but need more data
	///	@retval 0	no valid MPEG audio
	///	@retval >0	valid MPEG audio
	///
	///	From: http://www.mpgedit.org/mpgedit/mpeg_format/mpeghdr.htm
	///
	///	AAAAAAAA AAABBCCD EEEEFFGH IIJJKLMM
	///
	///	o a 11x Frame sync
	///	o b 2x	MPEG audio version (2.5, reserved, 2, 1)
	///	o c 2x	Layer (reserved, III, II, I)
	///	o e 2x	BitRate index
	///	o f 2x	SampleRate index (4100, 48000, 32000, 0)
	///	o g 1x	Padding bit
	///	o ..	Doesn't care
	///
	///	frame length:
	///	Layer I:
	///		FrameLengthInBytes = (12 * BitRate / SampleRate + Padding) * 4
	///	Layer II & III:
	///		FrameLengthInBytes = 144 * BitRate / SampleRate + Padding
	///
	static int MpegCheck(const uint8_t *data, int size)
	{
		int frame_size;
		int mpeg2 = !(data[1] & 0x08) && (data[1] & 0x10);
		int mpeg25 = !(data[1] & 0x08) && !(data[1] & 0x10);
		int layer = 4 - ((data[1] >> 1) & 0x03);
		int padding = (data[2] >> 1) & 0x01;

		int sample_rate = SampleRateTable[(data[2] >> 2) & 0x03];
		if (!sample_rate)
			return 0;

		sample_rate >>= mpeg2;		// MPEG 2 half rate
		sample_rate >>= mpeg25;		// MPEG 2.5 quarter rate

		int bit_rate = BitRateTable[mpeg2 | mpeg25][layer - 1][(data[2] >> 4) & 0x0F];
		if (!bit_rate)
			return 0;

		switch (layer)
		{
		case 1:
			frame_size = (12000 * bit_rate) / sample_rate;
			frame_size = (frame_size + padding) * 4;
			break;
		case 2:
		case 3:
		default:
			frame_size = (144000 * bit_rate) / sample_rate;
			frame_size = frame_size + padding;
			break;
		}
		if (frame_size + 4 > size)
			return -frame_size - 4;

		// check if after this frame a new MPEG frame starts
		if (cAudioParser::FastMpegCheck(data + frame_size))
			return frame_size;

		return 0;
	}

	///
	///	Fast check for (E-)AC-3 audio.
	///
	///	5 bytes 0x0B77xxxxxx AC-3 audio
	///
	static bool FastAc3Check(const uint8_t *p)
	{
		if (p[0] != 0x0B)			// 16bit sync
			return false;
		if (p[1] != 0x77)
			return false;
		return true;
	}

	///
	///	Check for (E-)AC-3 audio.
	///
	///	0x0B77xxxxxx already checked.
	///
	///	@param data	incomplete PES packet
	///	@param size	number of bytes
	///
	///	@retval <0	possible AC-3 audio, but need more data
	///	@retval 0	no valid AC-3 audio
	///	@retval >0	valid AC-3 audio
	///
	///	o AC-3 Header
	///	AAAAAAAA AAAAAAAA BBBBBBBB BBBBBBBB CCDDDDDD EEEEEFFF
	///
	///	o a 16x Frame sync, always 0x0B77
	///	o b 16x CRC 16
	///	o c 2x	Sample rate
	///	o d 6x	Frame size code
	///	o e 5x	Bit stream ID
	///	o f 3x	Bit stream mode
	///
	///	o E-AC-3 Header
	///	AAAAAAAA AAAAAAAA BBCCCDDD DDDDDDDD EEFFGGGH IIIII...
	///
	///	o a 16x Frame sync, always 0x0B77
	///	o b 2x	Frame type
	///	o c 3x	Sub stream ID
	///	o d 10x Frame size - 1 in words
	///	o e 2x	Frame size code
	///	o f 2x	Frame size code 2
	///
	static int Ac3Check(const uint8_t *p, int size)
	{
		int frame_size;

		if (size < 5)				// need 5 bytes to see if AC-3/E-AC-3
			return -5;

		if (p[5] > (10 << 3))	// E-AC-3
		{
			if ((p[4] & 0xF0) == 0xF0)	// invalid fscod fscod2
				return 0;

			frame_size = ((p[2] & 0x03) << 8) + p[3] + 1;
			frame_size *= 2;
		}
		else						// AC-3
		{
			int fscod = p[4] >> 6;
			if (fscod == 0x03)		// invalid sample rate
				return 0;

			int frmsizcod = p[4] & 0x3F;
			if (frmsizcod > 37)		// invalid frame size
				return 0;

			// invalid is checked above
			frame_size = Ac3FrameSizeTable[frmsizcod][fscod] * 2;
		}
		if (frame_size + 5 > size)
			return -frame_size - 5;

		// check if after this frame a new AC-3 frame starts
		if (FastAc3Check(p + frame_size))
			return frame_size;

		return 0;
	}

	///
	///	Fast check for AAC LATM audio.
	///
	///	3 bytes 0x56Exxx AAC LATM audio
	///
	static bool FastLatmCheck(const uint8_t *p)
	{
		if (p[0] != 0x56)			// 11bit sync
			return false;
		if ((p[1] & 0xE0) != 0xE0)
			return false;
		return true;
	}

	///
	///	Check for AAC LATM audio.
	///
	///	0x56Exxx already checked.
	///
	///	@param data	incomplete PES packet
	///	@param size	number of bytes
	///
	///	@retval <0	possible AAC LATM audio, but need more data
	///	@retval 0	no valid AAC LATM audio
	///	@retval >0	valid AAC LATM audio
	///
	static int LatmCheck(const uint8_t *p, int size)
	{
		// 13 bit frame size without header
		int frame_size = ((p[1] & 0x1F) << 8) + p[2];
		frame_size += 3;

		if (frame_size + 2 > size)
			return -frame_size - 2;

		// check if after this frame a new AAC LATM frame starts
		if (FastLatmCheck(p + frame_size))
			return frame_size;

	    return 0;
	}
	
	///
	///	Fast check for ADTS Audio Data Transport Stream.
	///
	///	7/9 bytes 0xFFFxxxxxxxxxxx(xxxx)  ADTS audio
	///
	static bool FastAdtsCheck(const uint8_t *p)
	{
		if (p[0] != 0xFF)			// 12bit sync
			return false;
		if ((p[1] & 0xF6) != 0xF0)	// sync + layer must be 0
			return false;
		if ((p[2] & 0x3C) == 0x3C)	// sampling frequency index != 15
			return false;
		return true;
	}

	///
	///	Check for ADTS Audio Data Transport Stream.
	///
	///	0xFFF already checked.
	///
	///	@param data	incomplete PES packet
	///	@param size	number of bytes
	///
	///	@retval <0	possible ADTS audio, but need more data
	///	@retval 0	no valid ADTS audio
	///	@retval >0	valid AC-3 audio
	///
	///	AAAAAAAA AAAABCCD EEFFFFGH HHIJKLMM MMMMMMMM MMMOOOOO OOOOOOPP
	///	(QQQQQQQQ QQQQQQQ)
	///
	///	o A*12	sync word 0xFFF
	///	o B*1	MPEG Version: 0 for MPEG-4, 1 for MPEG-2
	///	o C*2	layer: always 0
	///	o ..
	///	o F*4	sampling frequency index (15 is invalid)
	///	o ..
	///	o M*13	frame length
	///
	static int AdtsCheck(const uint8_t *p, int size)
	{
	    if (size < 6)
	    	return -6;

	    int frame_size = (p[3] & 0x03) << 11;
	    frame_size |= (p[4] & 0xFF) << 3;
	    frame_size |= (p[5] & 0xE0) >> 5;

	    if (frame_size + 3 > size)
	    	return -frame_size - 3;

	    // check if after this frame a new ADTS frame starts
	    if (FastAdtsCheck(p + frame_size))
	    	return frame_size;

	    return 0;
	}
};

///
///	MPEG bit rate table.
///
///	BitRateTable[Version][Layer][Index]
///
const uint16_t cAudioParser::BitRateTable[2][3][16] =
{
	{	// MPEG Version 1
		{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
		{0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
		{0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0}
	},
	{	// MPEG Version 2 & 2.5
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
		{0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0},
		{0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0}
	}
};

///
///	MPEG sample rate table.
///
const uint16_t cAudioParser::SampleRateTable[4] = { 44100, 48000, 32000, 0 };

///
///	Possible AC-3 frame sizes.
///
///	from ATSC A/52 table 5.18 frame size code table.
///
const uint16_t cAudioParser::Ac3FrameSizeTable[38][3] =
{
	{  64,   69,   96}, {  64,   70,   96}, {  80,   87,  120}, { 80,  88,  120},
	{  96,  104,  144}, {  96,  105,  144}, { 112,  121,  168}, {112, 122,  168},
	{ 128,  139,  192}, { 128,  140,  192}, { 160,  174,  240}, {160, 175,  240},
	{ 192,  208,  288}, { 192,  209,  288}, { 224,  243,  336}, {224, 244,  336},
	{ 256,  278,  384}, { 256,  279,  384}, { 320,  348,  480}, {320, 349,  480},
	{ 384,  417,  576}, { 384,  418,  576}, { 448,  487,  672}, {448, 488,  672},
	{ 512,  557,  768}, { 512,  558,  768}, { 640,  696,  960}, {640, 697,  960},
	{ 768,  835, 1152}, { 768,  836, 1152}, { 896,  975, 1344}, {896, 976, 1344},
	{1024, 1114, 1536}, {1024, 1115, 1536}, {1152, 1253, 1728},
	{1152, 1254, 1728}, {1280, 1393, 1920}, {1280, 1394, 1920},
};

/* ------------------------------------------------------------------------- */

cAudioDecoder::cAudioDecoder(cOmx *omx) :
	cThread(),
	m_codec(cAudioCodec::eInvalid),
	m_outputFormat(cAudioCodec::ePCM),
	m_outputPort(cAudioPort::eLocal),
	m_channels(0),
	m_samplingRate(0),
	m_passthrough(false),
	m_outputFormatChanged(true),
	m_pts(0),
	m_frame(0),
	m_mutex(new cMutex()),
	m_newData(new cCondWait()),
	m_parser(new cAudioParser()),
	m_omx(omx)
{
}

cAudioDecoder::~cAudioDecoder()
{
	delete m_parser;
	delete m_newData;
	delete m_mutex;
}

int cAudioDecoder::Init(void)
{
	int ret = m_parser->Init();
	if (ret)
		return ret;

	avcodec_register_all();

	m_codecs[cAudioCodec::ePCM ].codec = NULL;
	m_codecs[cAudioCodec::eMPG ].codec = avcodec_find_decoder(CODEC_ID_MP3);
	m_codecs[cAudioCodec::eAC3 ].codec = avcodec_find_decoder(CODEC_ID_AC3);
	m_codecs[cAudioCodec::eEAC3].codec = avcodec_find_decoder(CODEC_ID_EAC3);
	m_codecs[cAudioCodec::eAAC ].codec = avcodec_find_decoder(CODEC_ID_AAC_LATM);
	m_codecs[cAudioCodec::eDTS ].codec = avcodec_find_decoder(CODEC_ID_DTS);

	for (int i = 0; i < cAudioCodec::eNumCodecs; i++)
	{
		cAudioCodec::eCodec codec = static_cast<cAudioCodec::eCodec>(i);
		if (m_codecs[codec].codec)
		{
			m_codecs[codec].context = avcodec_alloc_context3(m_codecs[codec].codec);
			if (!m_codecs[codec].context)
			{
				esyslog("rpihddevice: failed to allocate %s context!", cAudioCodec::Str(codec));
				ret = -1;
				break;
			}
			if (avcodec_open2(m_codecs[codec].context, m_codecs[codec].codec, NULL) < 0)
			{
				esyslog("rpihddevice: failed to open %s decoder!", cAudioCodec::Str(codec));
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
	avcodec_get_frame_defaults(m_frame);

	if (ret < 0)
		DeInit();

	Start();

	return ret;
}

int cAudioDecoder::DeInit(void)
{
	Cancel();

	for (int i = 0; i < cAudioCodec::eNumCodecs; i++)
	{
		cAudioCodec::eCodec codec = static_cast<cAudioCodec::eCodec>(i);
		avcodec_close(m_codecs[codec].context);
		av_free(m_codecs[codec].context);
	}

	av_free(m_frame);
	m_parser->DeInit();
	return 0;
}

bool cAudioDecoder::WriteData(const unsigned char *buf, unsigned int length, uint64_t pts)
{
	m_mutex->Lock();

	bool ret = m_parser->Append(buf, length);
	if (ret)
	{
		// set current pts as reference
		m_pts = pts;

		ProbeCodec();
		m_newData->Signal();
	}

	m_mutex->Unlock();
	return ret;
}

void cAudioDecoder::Reset(void)
{
	m_mutex->Lock();
	m_parser->Reset();
	m_codec = cAudioCodec::eInvalid;
	avcodec_get_frame_defaults(m_frame);
	m_mutex->Unlock();
}

bool cAudioDecoder::Poll(void)
{
	return !m_parser->Size() && m_omx->PollAudioBuffers();
}

void cAudioDecoder::Action(void)
{
	dsyslog("rpihddevice: cAudioDecoder() thread started");

	while (Running())
	{
		if (m_parser->Size())
		{
			m_mutex->Lock();
			if (m_outputFormatChanged)
			{
				m_outputFormatChanged = false;
				m_omx->SetupAudioRender(
					m_outputFormat, m_channels,
					m_samplingRate, m_outputPort);
			}

			OMX_BUFFERHEADERTYPE *buf = m_omx->GetAudioBuffer(m_pts);
			if (buf)
			{
				while (DecodeFrame())
					buf->nFilledLen += ReadFrame(
							buf->pBuffer + buf->nFilledLen,
							buf->nAllocLen - buf->nFilledLen);

				if (!m_omx->EmptyAudioBuffer(buf))
					esyslog("rpihddevice: failed to empty audio buffer!");
			}
			else
			{
				esyslog("rpihddevice: failed to get audio buffer!");
				cCondWait::SleepMs(5);
			}
			m_mutex->Unlock();
		}
		else
			m_newData->Wait(50);
	}
	dsyslog("rpihddevice: cAudioDecoder() thread ended");
}

unsigned int cAudioDecoder::DecodeFrame()
{
	unsigned int ret = 0;

	if (m_passthrough)
		ret = m_parser->Size();

	else if (m_parser->Size())
	{
		int frame = 0;
		int len = avcodec_decode_audio4(m_codecs[m_codec].context,
				m_frame, &frame, m_parser->Packet());

		// decoding error or number of channels changed ?
		if (len < 0 || m_channels != m_codecs[m_codec].context->channels)
		{
			m_parser->Reset();
			m_codec = cAudioCodec::eInvalid;
		}
		else
		{
			m_parser->Shrink(len);
			ret = frame ? len : 0;
		}
	}
	return ret;
}

unsigned int cAudioDecoder::ReadFrame(unsigned char *buf, unsigned int bufsize)
{
	unsigned int ret = 0;

	if (m_passthrough)
	{
		// for pass-through directly read from AV packet
		if (m_parser->Size() > bufsize)
			ret = bufsize;
		else
			ret = m_parser->Size();

		memcpy(buf, m_parser->Data(), ret);
		m_parser->Shrink(ret);
	}
	else
	{
		if (m_frame->nb_samples > 0)
		{
			ret = av_samples_get_buffer_size(NULL,
					m_channels == 6 ? 8 : m_channels, m_frame->nb_samples,
					m_codecs[m_codec].context->sample_fmt, 1);

			if (ret > bufsize)
			{
				esyslog("rpihddevice: decoded audio frame too big!");
				ret = 0;
			}
			else
			{
				if (m_channels == 6)
				{
					// interleaved copy to fit 5.1 data into 8 channels
					int32_t* src = (int32_t*)m_frame->data[0];
					int32_t* dst = (int32_t*)buf;

					for (int i = 0; i < m_frame->nb_samples; i++)
					{
						*dst++ = *src++; // LF & RF
						*dst++ = *src++; // CF & LFE
						*dst++ = *src++; // LR & RR
						*dst++ = 0;      // empty channels
					}
				}
				else
					memcpy(buf, m_frame->data[0], ret);
			}
		}
	}
	return ret;
}

bool cAudioDecoder::ProbeCodec(void)
{
	bool ret = false;

	unsigned int offset = 0;
	cAudioCodec::eCodec codec = m_parser->Parse(offset);

	if (codec != cAudioCodec::eInvalid)
	{
		if (offset)
			m_parser->Shrink(offset);

		// if new codec has been found, decode one packet to determine number of
		// channels, since they are needed to properly set audio output format
		if (codec != m_codec || cRpiSetup::HasAudioSetupChanged())
		{
			m_codecs[codec].context->flags |= CODEC_FLAG_TRUNCATED;
			m_codecs[codec].context->request_channel_layout = AV_CH_LAYOUT_NATIVE;
			m_codecs[codec].context->request_channels = 0;

			int frame = 0;
			avcodec_get_frame_defaults(m_frame);
			int len = avcodec_decode_audio4(m_codecs[codec].context, m_frame,
					&frame, m_parser->Packet());

			if (len > 0 && frame)
			{
				SetCodec(codec);
				ret = true;
			}
		}
	}
	return ret;
}

void cAudioDecoder::SetCodec(cAudioCodec::eCodec codec)
{
	if (codec != cAudioCodec::eInvalid)
	{
		if (m_codec == cAudioCodec::eInvalid)
			m_outputFormatChanged = true;

		m_codec = codec;
		m_codecs[m_codec].context->request_channel_layout = AV_CH_LAYOUT_NATIVE;
		m_codecs[m_codec].context->request_channels = 0;

		m_passthrough = false;
		cAudioCodec::eCodec outputFormat = cAudioCodec::ePCM;
		cAudioPort::ePort outputPort = cAudioPort::eLocal;

		int channels = m_codecs[m_codec].context->channels;
		int samplingRate = m_codecs[m_codec].context->sample_rate;

		dsyslog("rpihddevice: set audio codec to %s with %d channels, %dHz",
			cAudioCodec::Str(m_codec), channels, samplingRate);

		if (cRpiSetup::GetAudioPort() == cAudioPort::eHDMI &&
			cRpiSetup::IsAudioFormatSupported(cAudioCodec::ePCM,
					m_codecs[m_codec].context->channels,
					m_codecs[m_codec].context->sample_rate))
		{
			outputPort = cAudioPort::eHDMI;

			if (cRpiSetup::IsAudioPassthrough() &&
				cRpiSetup::IsAudioFormatSupported(m_codec,
						m_codecs[m_codec].context->channels,
						m_codecs[m_codec].context->sample_rate))
			{
				m_passthrough = true;
				outputFormat = m_codec;
			}
		}
		else
		{
			m_codecs[m_codec].context->request_channel_layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
			m_codecs[m_codec].context->request_channels = 2;
			channels = 2;
		}

		if ((outputFormat != m_outputFormat) ||
			(outputPort   != m_outputPort  ) ||
			(channels     != m_channels    ) ||
			(samplingRate != m_samplingRate))
		{
			m_outputFormat = outputFormat;
			m_outputPort = outputPort;
			m_channels = channels;
			m_samplingRate = samplingRate;
			m_outputFormatChanged = true;

			dsyslog("rpihddevice: set %s audio output format to %s%s",
					cAudioPort::Str(m_outputPort), cAudioCodec::Str(m_outputFormat),
					m_passthrough ? " (pass-through)" : "");
		}
	}
}
