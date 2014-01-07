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

#define AVPKT_BUFFER_SIZE (64 * 1024) /* 1 PES packet */

class cAudioParser
{

public:

	cAudioParser() { }
	~cAudioParser() { }

	AVPacket* Packet(void)
	{
		return &m_packet;
	}

	cAudioCodec::eCodec GetCodec(void)
	{
		if (!m_parsed)
			Parse();
		return m_codec;
	}

	unsigned int GetChannels(void)
	{
		if (!m_parsed)
			Parse();
		return m_channels;
	}

	bool Empty(void)
	{
		if (!m_parsed)
			Parse();
		return m_packet.size == 0;
	}

	int Init(void)
	{
		if (!av_new_packet(&m_packet, AVPKT_BUFFER_SIZE))
		{
			Reset();
			return 0;
		}
		return -1;
	}

	int DeInit(void)
	{
		av_free_packet(&m_packet);
		return 0;
	}

	void Reset(void)
	{
		m_codec = cAudioCodec::eInvalid;
		m_channels = 0;
		m_packet.size = 0;
		m_size = 0;
		m_parsed = false;
		memset(m_packet.data, 0, FF_INPUT_BUFFER_PADDING_SIZE);
	}

	bool Append(const unsigned char *data, unsigned int length)
	{
		if (m_size + length + FF_INPUT_BUFFER_PADDING_SIZE > AVPKT_BUFFER_SIZE)
			return false;

		memcpy(m_packet.data + m_size, data, length);
		m_size += length;
		memset(m_packet.data + m_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

		m_parsed = false;
		return true;
	}

	void Shrink(unsigned int length)
	{
		if (length < m_size)
		{
			memmove(m_packet.data, m_packet.data + length, m_size - length);
			m_size -= length;
			memset(m_packet.data + m_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

			m_parsed = false;
		}
		else
			Reset();
	}
	
private:

	// Check format of first audio packet in buffer. If format has been
	// guessed, but packet is not yet complete, codec is set with a length
	// of 0. Once the buffer contains either the exact amount of expected
	// data or another valid packet start after the first frame, packet
	// size is set to the first frame length.
	// Valid packets are always moved to the buffer start, if no valid
	// audio frame has been found, packet gets cleared.
	//
	// To do:
	// - parse sampling rate to allow non-48kHz audio
	// - consider codec change for next frame check

	void Parse()
	{
		cAudioCodec::eCodec codec = cAudioCodec::eInvalid;
		int channels = 0;
		int offset = 0;
		int frameSize = 0;

		while (m_size - offset >= 3)
		{
			const uint8_t *p = m_packet.data + offset;
			int n = m_size - offset;

			// 4 bytes 0xFFExxxxx MPEG audio
			// 3 bytes 0x56Exxx AAC LATM audio
			// 5 bytes 0x0B77xxxxxx AC-3 audio
			// 6 bytes 0x0B77xxxxxxxx E-AC-3 audio
			// 7/9 bytes 0xFFFxxxxxxxxxxx ADTS audio
			// PCM audio can't be found

			if (FastMpegCheck(p))
			{
				if (MpegCheck(p, n, frameSize))
				{
					codec = cAudioCodec::eMPG;
					channels = 2;
				}
				break;
			}
			else if (FastAc3Check(p))
			{
				if (Ac3Check(p, n, frameSize, channels))
				{
					codec = cAudioCodec::eAC3;
					if (n > 5 && p[5] > (10 << 3))
						codec = cAudioCodec::eEAC3;
				}
				break;
			}
			else if (FastLatmCheck(p))
			{
				if (LatmCheck(p, n, frameSize))
				{
					codec = cAudioCodec::eAAC;
					channels = 2;
				}
				break;
			}
			else if (FastAdtsCheck(p))
			{
				if (AdtsCheck(p, n, frameSize, channels))
					codec = cAudioCodec::eADTS;
				break;
			}

			++offset;
		}

		if (codec != cAudioCodec::eInvalid)
		{
			if (offset)
			{
				dsyslog("rpihddevice: audio packet shrinked by %d bytes", offset);
				Shrink(offset);
			}

			m_codec = codec;
			m_channels = channels;
			m_packet.size = frameSize;
		}
		else
			Reset();

		m_parsed = true;
	}

	AVPacket 			m_packet;
	cAudioCodec::eCodec m_codec;
	unsigned int		m_channels;
	unsigned int		m_size;
	bool				m_parsed;

	/* ------------------------------------------------------------------------- */
	/*     audio codec parser helper functions, taken from vdr-softhddevice      */
	/* ------------------------------------------------------------------------- */

	static const uint16_t BitRateTable[2][3][16];
	static const uint16_t MpegSampleRateTable[4];
	static const uint16_t Ac3SampleRateTable[4];
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
	///	From: http://www.mpgedit.org/mpgedit/mpeg_format/mpeghdr.htm
	///
	///	AAAAAAAA AAABBCCD EEEEFFGH IIJJKLMM
	///
	///	o a 11x Frame sync
	///	o b 2x	MPEG audio version (2.5, reserved, 2, 1)
	///	o c 2x	Layer (reserved, III, II, I)
	///	o e 2x	BitRate index
	///	o f 2x	SampleRate index (41000, 48000, 32000, 0)
	///	o g 1x	Padding bit
	///	o ..	Doesn't care
	///
	///	frame length:
	///	Layer I:
	///		FrameLengthInBytes = (12 * BitRate / SampleRate + Padding) * 4
	///	Layer II & III:
	///		FrameLengthInBytes = 144 * BitRate / SampleRate + Padding
	///
	static bool MpegCheck(const uint8_t *data, int size, int &frame_size)
	{
		frame_size = 0;
		if (size < 3)
			return true;

		int mpeg2 = !(data[1] & 0x08) && (data[1] & 0x10);
		int mpeg25 = !(data[1] & 0x08) && !(data[1] & 0x10);
		int layer = 4 - ((data[1] >> 1) & 0x03);
		int padding = (data[2] >> 1) & 0x01;

		int sample_rate = MpegSampleRateTable[(data[2] >> 2) & 0x03];
		if (!sample_rate)
			return false;

		sample_rate >>= mpeg2;		// MPEG 2 half rate
		sample_rate >>= mpeg25;		// MPEG 2.5 quarter rate

		int bit_rate = BitRateTable[mpeg2 | mpeg25][layer - 1][(data[2] >> 4) & 0x0F];
		if (!bit_rate)
			return false;

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

		if (size >= frame_size + 3 && !FastMpegCheck(data + frame_size))
			return false;

		if (frame_size > size)
			frame_size = 0;

		return true;
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
	///	o AC-3 Header
	///	AAAAAAAA AAAAAAAA BBBBBBBB BBBBBBBB CCDDDDDD EEEEEFFF GGGxxxxx
	///
	///	o a 16x Frame sync, always 0x0B77
	///	o b 16x CRC 16
	///	o c 2x	Sample rate ( 48000, 44100, 32000, reserved )
	///	o d 6x	Frame size code
	///	o e 5x	Bit stream ID
	///	o f 3x	Bit stream mode
	/// o g 3x  Audio coding mode
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
	/// o g 3x  Channel mode
	/// 0 h 1x  LFE on
	///
	static bool Ac3Check(const uint8_t *p, int size, int &frame_size, int &channels)
	{
		frame_size = 0;
		if (size < 7)
			return true;

		int acmod;
		bool lfe;
		int sample_rate;			// for future use, E-AC3 t.b.d.

		if (p[5] > (10 << 3))		// E-AC-3
		{
			if ((p[4] & 0xF0) == 0xF0)	// invalid fscod fscod2
				return false;

			acmod = (p[4] & 0x0E) >> 1;	// number of channels, LFE excluded
			lfe = p[4] & 0x01;

			frame_size = ((p[2] & 0x03) << 8) + p[3] + 1;
			frame_size *= 2;
		}
		else						// AC-3
		{
			sample_rate = Ac3SampleRateTable[(p[4] >> 6) & 0x03];

			int fscod = p[4] >> 6;
			if (fscod == 0x03)		// invalid sample rate
				return false;

			int frmsizcod = p[4] & 0x3F;
			if (frmsizcod > 37)		// invalid frame size
				return false;

			acmod = p[6] >> 5;		// number of channels, LFE excluded

			int lfe_bptr = 51;		// position of LFE bit in header for 2.0
			if ((acmod & 0x01) && (acmod != 0x01))
				lfe_bptr += 2;		// skip center mix level
			if (acmod & 0x04)
				lfe_bptr += 2;		// skip surround mix level
			if (acmod == 0x02)
				lfe_bptr += 2;		// skip surround mode
			lfe = (p[lfe_bptr / 8] & (1 << (7 - (lfe_bptr % 8))));

			// invalid is checked above
			frame_size = Ac3FrameSizeTable[frmsizcod][fscod] * 2;
		}

		channels =
			acmod == 0x00 ? 2 : 	// Ch1, Ch2
			acmod == 0x01 ? 1 : 	// C
			acmod == 0x02 ? 2 : 	// L, R
			acmod == 0x03 ? 3 : 	// L, C, R
			acmod == 0x04 ? 3 : 	// L, R, S
			acmod == 0x05 ? 4 : 	// L, C, R, S
			acmod == 0x06 ? 4 : 	// L, R, RL, RR
			acmod == 0x07 ? 5 : 0;	// L, C, R, RL, RR

		if (lfe) channels++;

		if (size >= frame_size + 2 && !FastAc3Check(p + frame_size))
			return false;

		if (frame_size > size)
			frame_size = 0;

		return true;
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
	static bool LatmCheck(const uint8_t *p, int size, int &frame_size)
	{
		frame_size = 0;
		if (size < 3)
			return true;

		// 13 bit frame size without header
		frame_size = ((p[1] & 0x1F) << 8) + p[2];
		frame_size += 3;

		if (size >= frame_size + 3 && !FastLatmCheck(p + frame_size))
			return false;

		if (frame_size > size)
			frame_size = 0;

		return true;
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
	///	AAAAAAAA AAAABCCD EEFFFFGH HHIJKLMM MMMMMMMM MMMOOOOO OOOOOOPP
	///	(QQQQQQQQ QQQQQQQ)
	///
	///	o A*12	sync word 0xFFF
	///	o B*1	MPEG Version: 0 for MPEG-4, 1 for MPEG-2
	///	o C*2	layer: always 0
	///	o ..
	///	o F*4	sampling frequency index (15 is invalid)
	///	o ..
	/// o H*3	MPEG-4 channel configuration
	/// o ...
	///	o M*13	frame length
	///
	static bool AdtsCheck(const uint8_t *p, int size, int &frame_size, int &channels)
	{
		frame_size = 0;
		if (size < 6)
			return true;

	    frame_size = (p[3] & 0x03) << 11;
	    frame_size |= (p[4] & 0xFF) << 3;
	    frame_size |= (p[5] & 0xE0) >> 5;

	    int ch_config = (p[2] & 0x01) << 7;
	    ch_config |= (p[3] & 0xC0) >> 6;
	    channels =
	    	ch_config == 0x00 ? 0 : // defined in AOT specific config
			ch_config == 0x01 ? 1 : // C
	    	ch_config == 0x02 ? 2 : // L, R
	    	ch_config == 0x03 ? 3 : // C, L, R
	    	ch_config == 0x04 ? 4 : // C, L, R, RC
	    	ch_config == 0x05 ? 5 : // C, L, R, RL, RR
	    	ch_config == 0x06 ? 6 : // C, L, R, RL, RR, LFE
	    	ch_config == 0x07 ? 8 : // C, L, R, SL, SR, RL, RR, LFE
				0;

		if (size >= frame_size + 3 && !FastAdtsCheck(p + frame_size))
			return false;

		if (frame_size > size)
			frame_size = 0;

		return true;
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
const uint16_t cAudioParser::MpegSampleRateTable[4] = { 44100, 48000, 32000, 0 };

///
///	AC-3 sample rate table.
///
const uint16_t cAudioParser::Ac3SampleRateTable[4] = { 48000, 44100, 32000, 0 };

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
	m_passthrough(false),
	m_reset(false),
	m_ready(false),
	m_pts(0),
	m_mutex(new cMutex()),
	m_wait(new cCondWait()),
	m_parser(new cAudioParser()),
	m_omx(omx)
{
}

cAudioDecoder::~cAudioDecoder()
{
	delete m_parser;
	delete m_wait;
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
	m_codecs[cAudioCodec::eADTS].codec = avcodec_find_decoder(CODEC_ID_AAC);

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

	m_parser->DeInit();
	return 0;
}

int cAudioDecoder::WriteData(const unsigned char *buf, unsigned int length, uint64_t pts)
{
	int ret = 0;
	if (m_ready)
	{
		m_mutex->Lock();
		if (m_parser->Append(buf, length))
		{
			m_pts = pts;
			m_ready = false;
			m_wait->Signal();
			ret = length;
		}
		m_mutex->Unlock();
	}
	return ret;
}

void cAudioDecoder::Reset(void)
{
	m_mutex->Lock();
	m_reset = true;
	m_wait->Signal();
	m_mutex->Unlock();
}

bool cAudioDecoder::Poll(void)
{
	return m_ready && m_omx->PollAudioBuffers();
}

void cAudioDecoder::Action(void)
{
	dsyslog("rpihddevice: cAudioDecoder() thread started");

	while (Running())
	{
		unsigned int channels = 0;
		unsigned int outputChannels = 0;

		bool bufferFull = false;

		cAudioCodec::eCodec codec = cAudioCodec::eInvalid;
		OMX_BUFFERHEADERTYPE *buf = 0;

	    AVFrame *frame = avcodec_alloc_frame();
		if (!frame)
		{
			esyslog("rpihddevice: failed to allocate audio frame!");
			return;
		}

		m_reset = false;
		m_ready = true;

		while (!m_reset)
		{
			// check for data if no decoded samples are pending
			if (!m_parser->Empty() && !frame->nb_samples)
			{
				if (codec != m_parser->GetCodec() ||
					channels != m_parser->GetChannels())
				{
					// to change codec config, we need to empty buffer first
					if (buf)
						bufferFull = true;
					else
					{
						codec = m_parser->GetCodec();
						channels = m_parser->GetChannels();

						outputChannels = channels;
						SetCodec(codec, outputChannels);
					}
				}
			}

			// if codec has been configured but we don't have a buffer, get one
			while (codec != cAudioCodec::eInvalid && !buf && !m_reset)
			{
				buf = m_omx->GetAudioBuffer(m_pts);
				if (buf)
					m_pts = 0;
				else
					m_wait->Wait(10);
			}

			// we have a non-full buffer and data to encode / copy
			if (buf && !bufferFull && !m_parser->Empty())
			{
				int copied = 0;
				if (m_passthrough)
				{
					// for pass-through directly copy AV packet to buffer
					if (m_parser->Packet()->size <= buf->nAllocLen - buf->nFilledLen)
					{
						m_mutex->Lock();

						memcpy(buf->pBuffer + buf->nFilledLen,
								m_parser->Packet()->data, m_parser->Packet()->size);
						buf->nFilledLen += m_parser->Packet()->size;
						m_parser->Shrink(m_parser->Packet()->size);

						m_mutex->Unlock();
					}
					else
						if (m_parser->Packet()->size > buf->nAllocLen)
						{
							esyslog("rpihddevice: encoded audio frame too big!");
							m_reset = true;
							break;
						}
						else
							bufferFull = true;
				}
				else
				{
					// decode frame if we do not pass-through
					m_mutex->Lock();

					int gotFrame = 0;
					int len = avcodec_decode_audio4(m_codecs[codec].context,
							frame, &gotFrame, m_parser->Packet());

					if (len > 0)
						m_parser->Shrink(len);

					m_mutex->Unlock();

					if (len < 0)
					{
						esyslog("rpihddevice: failed to decode audio frame!");
						m_reset = true;
						break;
					}
				}
			}

			// we have decoded samples we need to copy to buffer
			if (buf && !bufferFull && frame->nb_samples > 0)
			{
				int length = av_samples_get_buffer_size(NULL,
						outputChannels == 6 ? 8 : outputChannels, frame->nb_samples,
						m_codecs[codec].context->sample_fmt, 1);

				if (length <= buf->nAllocLen - buf->nFilledLen)
				{
					if (outputChannels == 6)
					{
						// interleaved copy to fit 5.1 data into 8 channels
						int32_t* src = (int32_t*)frame->data[0];
						int32_t* dst = (int32_t*)buf->pBuffer + buf->nFilledLen;

						for (int i = 0; i < frame->nb_samples; i++)
						{
							*dst++ = *src++; // LF & RF
							*dst++ = *src++; // CF & LFE
							*dst++ = *src++; // LR & RR
							*dst++ = 0;      // empty channels
						}
					}
					else
						memcpy(buf->pBuffer + buf->nFilledLen, frame->data[0], length);

					buf->nFilledLen += length;
					frame->nb_samples = 0;
				}
				else
				{
					if (length > buf->nAllocLen)
					{
						esyslog("rpihddevice: decoded audio frame too big!");
						m_reset = true;
						break;
					}
					else
						bufferFull = true;
				}
			}

			// check if no decoded samples are pending and parser is empty
			if (!frame->nb_samples && m_parser->Empty())
			{
				// if no more data but buffer with data -> end of PES packet
				if (buf && buf->nFilledLen > 0)
					bufferFull = true;
				else
				{
					m_ready = true;
					m_wait->Wait(50);
				}
			}

			// we have a buffer to empty
			if (buf && bufferFull)
			{
				if (m_omx->EmptyAudioBuffer(buf))
				{
					bufferFull = false;
					buf = 0;

					// if parser is empty, get new data
					if (m_parser->Empty())
						m_ready = true;
				}
				else
				{
					esyslog("rpihddevice: failed to empty audio buffer!");
					m_reset = true;
					break;
				}
			}
		}

		dsyslog("reset");
		if (buf && m_omx->EmptyAudioBuffer(buf))
			buf = 0;

		av_free(frame);
		m_parser->Reset();
	}
	dsyslog("rpihddevice: cAudioDecoder() thread ended");
}

void cAudioDecoder::SetCodec(cAudioCodec::eCodec codec, unsigned int &channels)
{
	if (codec != cAudioCodec::eInvalid && channels > 0)
	{
		dsyslog("rpihddevice: set audio codec to %dch %s",
				channels, cAudioCodec::Str(codec));

		m_codecs[codec].context->request_channel_layout = AV_CH_LAYOUT_NATIVE;
		m_codecs[codec].context->request_channels = 0;

		m_passthrough = false;
		cAudioCodec::eCodec outputFormat = cAudioCodec::ePCM;
		cAudioPort::ePort outputPort = cAudioPort::eLocal;

		if (cRpiSetup::GetAudioPort() == cAudioPort::eHDMI &&
			cRpiSetup::IsAudioFormatSupported(cAudioCodec::ePCM, channels, 48000))
		{
			outputPort = cAudioPort::eHDMI;

			if (cRpiSetup::IsAudioPassthrough() &&
				cRpiSetup::IsAudioFormatSupported(codec, channels, 48000))
			{
				m_passthrough = true;
				outputFormat = codec;
			}
		}
		else
		{
			m_codecs[codec].context->request_channel_layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
			m_codecs[codec].context->request_channels = 2;
			channels = 2;

			// if 2ch PCM audio on HDMI is supported
			if (cRpiSetup::GetAudioPort() == cAudioPort::eHDMI &&
				cRpiSetup::IsAudioFormatSupported(cAudioCodec::ePCM, 2, 48000))
				outputPort = cAudioPort::eHDMI;
		}

		m_omx->SetupAudioRender(outputFormat, channels,	outputPort, 48000);
		dsyslog("rpihddevice: set %s audio output format to %dch %s%s",
				cAudioPort::Str(outputPort), channels, cAudioCodec::Str(outputFormat),
				m_passthrough ? " (pass-through)" : "");
	}
}
