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

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
}

#include <queue>
#include <string.h>

#define AVPKT_BUFFER_SIZE (KILOBYTE(256))

class cRpiAudioDecoder::cParser
{

public:

	cParser() :
		m_mutex(new cMutex()),
		m_codec(cAudioCodec::eInvalid),
		m_channels(0),
		m_samplingRate(0),
		m_size(0),
		m_parsed(true)
	{
	}

	~cParser()
	{
		delete m_mutex;
	}

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

	unsigned int GetSamplingRate(void)
	{
		if (!m_parsed)
			Parse();
		return m_samplingRate;
	}

	uint64_t GetPts(void)
	{
		uint64_t pts = 0;
		m_mutex->Lock();

		if (!m_ptsQueue.empty())
			pts = m_ptsQueue.front()->pts;

		m_mutex->Unlock();
		return pts;
	}

	unsigned int GetFreeSpace(void)
	{
		return AVPKT_BUFFER_SIZE - m_size - FF_INPUT_BUFFER_PADDING_SIZE;
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
		m_mutex->Lock();
		m_codec = cAudioCodec::eInvalid;
		m_channels = 0;
		m_samplingRate = 0;
		m_packet.size = 0;
		m_size = 0;
		m_parsed = true; // parser is empty, no need for parsing
		memset(m_packet.data, 0, FF_INPUT_BUFFER_PADDING_SIZE);

		while (!m_ptsQueue.empty())
		{
			delete m_ptsQueue.front();
			m_ptsQueue.pop();
		}
		m_mutex->Unlock();
	}

	bool Append(const unsigned char *data, uint64_t pts, unsigned int length)
	{
		m_mutex->Lock();
		bool ret = true;

		if (m_size + length + FF_INPUT_BUFFER_PADDING_SIZE > AVPKT_BUFFER_SIZE)
			ret = false;
		else
		{
			memcpy(m_packet.data + m_size, data, length);
			m_size += length;
			memset(m_packet.data + m_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

			Pts* entry = new Pts(pts, length);
			m_ptsQueue.push(entry);

			m_parsed = false;
		}
		m_mutex->Unlock();
		return ret;
	}

	void Shrink(unsigned int length)
	{
		m_mutex->Lock();

		if (length < m_size)
		{
			memmove(m_packet.data, m_packet.data + length, m_size - length);
			m_size -= length;
			memset(m_packet.data + m_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

			// clear current PTS since it's not valid anymore after
			// shrinking the packet
			if (!m_ptsQueue.empty())
				m_ptsQueue.front()->pts = 0;

			while (!m_ptsQueue.empty() && length)
			{
				if (m_ptsQueue.front()->length <= length)
				{
					length -= m_ptsQueue.front()->length;
					delete m_ptsQueue.front();
					m_ptsQueue.pop();
				}
				else
				{
					length -= m_ptsQueue.front()->length -= length;
					length = 0;
				}
			}

			m_parsed = false;
		}
		else
			Reset();

		m_mutex->Unlock();
	}
	
private:

	cParser(const cParser&);
	cParser& operator= (const cParser&);

	// Check format of first audio packet in buffer. If format has been
	// guessed, but packet is not yet complete, codec is set with a length
	// of 0. Once the buffer contains either the exact amount of expected
	// data or another valid packet start after the first frame, packet
	// size is set to the first frame length.
	// Valid packets are always moved to the buffer start, if no valid
	// audio frame has been found, packet gets cleared.

	void Parse()
	{
		m_mutex->Lock();
		cAudioCodec::eCodec codec = cAudioCodec::eInvalid;
		unsigned int channels = 0;
		unsigned int offset = 0;
		unsigned int frameSize = 0;
		unsigned int samplingRate = 0;

		while (m_size - offset >= 3)
		{
			// 4 bytes 0xFFExxxxx MPEG audio
			// 5 bytes 0x0B77xxxxxx AC-3 audio
			// 6 bytes 0x0B77xxxxxxxx E-AC-3 audio
			// 7/9 bytes 0xFFFxxxxxxxxxxx AAC audio
			// PCM audio can't be found

			const uint8_t *p = m_packet.data + offset;
			unsigned int n = m_size - offset;

			switch (FastCheck(p))
			{
			case cAudioCodec::eMPG:
				if (MpegCheck(p, n, frameSize, channels, samplingRate))
					codec = cAudioCodec::eMPG;
				break;

			case cAudioCodec::eAC3:
				if (Ac3Check(p, n, frameSize, channels, samplingRate))
				{
					codec = cAudioCodec::eAC3;
					if (n > 5 && p[5] > (10 << 3))
						codec = cAudioCodec::eEAC3;
				}
				break;
			case cAudioCodec::eAAC:
				if (AdtsCheck(p, n, frameSize, channels, samplingRate))
					codec = cAudioCodec::eAAC;
				break;

			default:
				break;
			}

			if (codec != cAudioCodec::eInvalid)
			{
				// if there is enough data in buffer, check if predicted next
				// frame start is valid
				if (n < frameSize + 3 ||
						FastCheck(p + frameSize) != cAudioCodec::eInvalid)
				{
					// if codec has been detected but buffer does not yet
					// contains a complete frame, set size to zero to prevent
					// frame from being decoded
					if (frameSize > n)
						frameSize = 0;

					break;
				}
			}

			++offset;
		}

		if (offset)
		{
			DLOG("audio parser skipped %u of %u bytes", offset, m_size);
			Shrink(offset);
		}

		if (codec != cAudioCodec::eInvalid)
		{
			m_codec = codec;
			m_channels = channels;
			m_samplingRate = samplingRate;
			m_packet.size = frameSize;
		}
		else
			m_packet.size = 0;

		m_parsed = true;
		m_mutex->Unlock();
	}

	struct Pts
	{
		Pts(uint64_t _pts, unsigned int _length)
			: pts(_pts), length(_length) { };

		uint64_t 		pts;
		unsigned int 	length;
	};

	cMutex*				m_mutex;
	AVPacket 			m_packet;
	cAudioCodec::eCodec m_codec;
	unsigned int		m_channels;
	unsigned int		m_samplingRate;
	unsigned int		m_size;
	std::queue<Pts*> 	m_ptsQueue;
	bool				m_parsed;

	/* ------------------------------------------------------------------------- */
	/*     audio codec parser helper functions, based on vdr-softhddevice        */
	/* ------------------------------------------------------------------------- */

	static const uint16_t BitRateTable[2][3][16];
	static const uint16_t MpegSampleRateTable[4];
	static const uint32_t Mpeg4SampleRateTable[16];
	static const uint16_t Ac3SampleRateTable[4];
	static const uint16_t Ac3FrameSizeTable[38][3];

	static cAudioCodec::eCodec FastCheck(const uint8_t *p)
	{
		return 	FastMpegCheck(p)  ? cAudioCodec::eMPG :
				FastAc3Check (p)  ? cAudioCodec::eAC3 :
				FastAdtsCheck(p)  ? cAudioCodec::eAAC :
									cAudioCodec::eInvalid;
	}

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
	/// o h 1x  Private bit
	/// o i 2x  Channel mode
	///	o ..	Doesn't care
	///
	///	frame length:
	///	Layer I:
	///		FrameLengthInBytes = (12 * BitRate / SampleRate + Padding) * 4
	///	Layer II & III:
	///		FrameLengthInBytes = 144 * BitRate / SampleRate + Padding
	///
	static bool MpegCheck(const uint8_t *p, unsigned int size,
			unsigned int &frameSize, unsigned int &channels,
			unsigned int &samplingRate)
	{
		frameSize = size;
		if (size < 4)
			return true;

		int cmode = (p[3] >> 6) & 0x03;
		int mpeg2 = !(p[1] & 0x08) && (p[1] & 0x10);
		int mpeg25 = !(p[1] & 0x08) && !(p[1] & 0x10);
		int layer = 4 - ((p[1] >> 1) & 0x03);
		int padding = (p[2] >> 1) & 0x01;

		// channel mode = [ stereo, joint stereo, dual channel, mono]
		channels = cmode == 0x03 ? 1 : 2;

		samplingRate = MpegSampleRateTable[(p[2] >> 2) & 0x03];
		if (!samplingRate)
			return false;

		samplingRate >>= mpeg2;		// MPEG 2 half rate
		samplingRate >>= mpeg25;	// MPEG 2.5 quarter rate

		int bit_rate = BitRateTable[mpeg2 | mpeg25][layer - 1][(p[2] >> 4) & 0x0F];
		if (!bit_rate)
			return false;

		switch (layer)
		{
		case 1:
			frameSize = (12000 * bit_rate) / samplingRate;
			frameSize = (frameSize + padding) * 4;
			break;
		case 2:
		case 3:
		default:
			frameSize = (144000 * bit_rate) / samplingRate;
			frameSize = frameSize + padding;
			break;
		}
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
	///	o d 11x Frame size - 1 in words
	///	o e 2x	Frame size code
	///	o f 2x	Frame size code 2
	/// o g 3x  Channel mode
	/// 0 h 1x  LFE on
	///
	static bool Ac3Check(const uint8_t *p, unsigned int size,
			unsigned int &frameSize, unsigned int &channels,
			unsigned int &samplingRate)
	{
		frameSize = size;
		if (size < 7)
			return true;

		int acmod;
		bool lfe;
		int fscod = (p[4] & 0xC0) >> 6;

		samplingRate = Ac3SampleRateTable[fscod];

		if (p[5] > (10 << 3))		// E-AC-3
		{
			if (fscod == 0x03)
			{
				int fscod2 = (p[4] & 0x30) >> 4;
				if (fscod2 == 0x03)
					return false;		// invalid fscod & fscod2

				samplingRate = Ac3SampleRateTable[fscod2] / 2;
			}

			acmod = (p[4] & 0x0E) >> 1;	// number of channels, LFE excluded
			lfe = p[4] & 0x01;

			frameSize = ((p[2] & 0x07) << 8) + p[3] + 1;
			frameSize *= 2;
		}
		else						// AC-3
		{
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
			frameSize = Ac3FrameSizeTable[frmsizcod][fscod] * 2;
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
		return true;
	}

#if 0
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
	static bool LatmCheck(const uint8_t *p, unsigned int size,
			unsigned int &frameSize, unsigned int &channels,
			unsigned int &samplingRate)
	{
		frameSize = size;
		if (size < 3)
			return true;

		// to do: determine channels
		channels = 2;

		// to do: determine sampling rate
		samplingRate = 48000;

		// 13 bit frame size without header
		frameSize = ((p[1] & 0x1F) << 8) + p[2];
		frameSize += 3;
		return true;
	}
#endif
	
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
	static bool AdtsCheck(const uint8_t *p, unsigned int size,
			unsigned int &frameSize, unsigned int &channels,
			unsigned int &samplingRate)
	{
		frameSize = size;
		if (size < 6)
			return true;

		samplingRate = Mpeg4SampleRateTable[(p[2] >> 2) & 0x0F];

		frameSize = (p[3] & 0x03) << 11;
		frameSize |= (p[4] & 0xFF) << 3;
		frameSize |= (p[5] & 0xE0) >> 5;

	    int cConf = (p[2] & 0x01) << 7;
	    cConf |= (p[3] & 0xC0) >> 6;
	    channels =
	    	cConf == 0x00 ? 0 : // defined in AOT specific config
			cConf == 0x01 ? 1 : // C
	    	cConf == 0x02 ? 2 : // L, R
	    	cConf == 0x03 ? 3 : // C, L, R
	    	cConf == 0x04 ? 4 : // C, L, R, RC
	    	cConf == 0x05 ? 5 : // C, L, R, RL, RR
	    	cConf == 0x06 ? 6 : // C, L, R, RL, RR, LFE
	    	cConf == 0x07 ? 8 : // C, L, R, SL, SR, RL, RR, LFE
				0;

		if (!samplingRate || !channels)
			return false;

	    return true;
	}
};

///
///	MPEG bit rate table.
///
///	BitRateTable[Version][Layer][Index]
///
const uint16_t cRpiAudioDecoder::cParser::BitRateTable[2][3][16] =
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
const uint16_t cRpiAudioDecoder::cParser::MpegSampleRateTable[4] =
	{ 44100, 48000, 32000, 0 };

///
///	MPEG-4 sample rate table.
///
const uint32_t cRpiAudioDecoder::cParser::Mpeg4SampleRateTable[16] = {
		96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
		16000, 12000, 11025,  8000,  7350,     0,     0,     0
};

///
///	AC-3 sample rate table.
///
const uint16_t cRpiAudioDecoder::cParser::Ac3SampleRateTable[4] =
	{ 48000, 44100, 32000, 0 };

///
///	Possible AC-3 frame sizes.
///
///	from ATSC A/52 table 5.18 frame size code table.
///
const uint16_t cRpiAudioDecoder::cParser::Ac3FrameSizeTable[38][3] =
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

cRpiAudioDecoder::cRpiAudioDecoder(cOmx *omx) :
	cThread(),
	m_passthrough(false),
	m_reset(false),
	m_setupChanged(true),
	m_wait(new cCondWait()),
	m_parser(new cParser()),
	m_omx(omx)
{
	memset(m_codecs, 0, sizeof(m_codecs));
}

cRpiAudioDecoder::~cRpiAudioDecoder()
{
	delete m_parser;
	delete m_wait;
}

extern int SysLogLevel;

int cRpiAudioDecoder::Init(void)
{
	int ret = m_parser->Init();
	if (ret)
		return ret;

	avcodec_register_all();

	av_log_set_level(
			SysLogLevel > 2 ? AV_LOG_VERBOSE :
			SysLogLevel > 1 ? AV_LOG_INFO : AV_LOG_ERROR);
	av_log_set_callback(&Log);

	m_codecs[cAudioCodec::ePCM ].codec = NULL;
	m_codecs[cAudioCodec::eMPG ].codec = avcodec_find_decoder(CODEC_ID_MP3);
	m_codecs[cAudioCodec::eAC3 ].codec = avcodec_find_decoder(CODEC_ID_AC3);
	m_codecs[cAudioCodec::eEAC3].codec = avcodec_find_decoder(CODEC_ID_EAC3);
	m_codecs[cAudioCodec::eAAC ].codec = avcodec_find_decoder(CODEC_ID_AAC);

	for (int i = 0; i < cAudioCodec::eNumCodecs; i++)
	{
		cAudioCodec::eCodec codec = static_cast<cAudioCodec::eCodec>(i);
		if (m_codecs[codec].codec)
		{
			m_codecs[codec].context = avcodec_alloc_context3(m_codecs[codec].codec);
			if (!m_codecs[codec].context)
			{
				ELOG("failed to allocate %s context!", cAudioCodec::Str(codec));
				ret = -1;
				break;
			}
			if (avcodec_open2(m_codecs[codec].context, m_codecs[codec].codec, NULL) < 0)
			{
				ELOG("failed to open %s decoder!", cAudioCodec::Str(codec));
				ret = -1;
				break;
			}
		}
	}

	if (ret < 0)
		DeInit();

	cRpiSetup::SetAudioSetupChangedCallback(&OnAudioSetupChanged, this);

	Start();

	return ret;
}

int cRpiAudioDecoder::DeInit(void)
{
	Lock();

	Reset();
	Cancel(-1);
	m_wait->Signal();

	while (Active())
		cCondWait::SleepMs(50);

	cRpiSetup::SetAudioSetupChangedCallback(0);

	for (int i = 0; i < cAudioCodec::eNumCodecs; i++)
	{
		cAudioCodec::eCodec codec = static_cast<cAudioCodec::eCodec>(i);
		if (m_codecs[codec].codec)
		{
			avcodec_close(m_codecs[codec].context);
			av_free(m_codecs[codec].context);
		}
	}

	av_log_set_callback(&av_log_default_callback);
	m_parser->DeInit();

	Unlock();
	return 0;
}

bool cRpiAudioDecoder::WriteData(const unsigned char *buf, unsigned int length,
		uint64_t pts)
{
	Lock();

	bool ret = m_parser->Append(buf, pts, length);
	if (ret)
		m_wait->Signal();

	Unlock();
	return ret;
}

void cRpiAudioDecoder::Reset(void)
{
	Lock();

	m_reset = true;
	m_wait->Signal();
	while (m_reset)
		cCondWait::SleepMs(1);

	Unlock();
}

bool cRpiAudioDecoder::Poll(void)
{
	return m_parser->GetFreeSpace() > KILOBYTE(16);
}

void cRpiAudioDecoder::HandleAudioSetupChanged()
{
	DBG("HandleAudioSetupChanged()");
	m_setupChanged = true;
}

void cRpiAudioDecoder::Action(void)
{
	DLOG("cAudioDecoder() thread started");

	unsigned int channels = 0;
	unsigned int outputChannels = 0;
	unsigned int samplingRate = 0;

	cAudioCodec::eCodec codec = cAudioCodec::eInvalid;
	OMX_BUFFERHEADERTYPE *buf = 0;

    AVFrame *frame = avcodec_alloc_frame();
	if (!frame)
	{
		ELOG("failed to allocate audio frame!");
		return;
	}

	while (Running())
	{
		// test for codec change if there is data in parser and no left over
		if (!m_parser->Empty() && !frame->nb_samples)
			m_setupChanged |= codec != m_parser->GetCodec() ||
				channels != m_parser->GetChannels() ||
				samplingRate != m_parser->GetSamplingRate();

		// if necessary, set up audio codec
		if (m_setupChanged)
		{
			codec = m_parser->GetCodec();
			channels = m_parser->GetChannels();
			samplingRate = m_parser->GetSamplingRate();

			outputChannels = channels;
			SetCodec(codec, outputChannels, samplingRate);

			avcodec_get_frame_defaults(frame);
			m_setupChanged = false;

			if (codec == cAudioCodec::eInvalid)
				m_reset = true;
		}

		// get free buffer
		while ((!m_parser->Empty() || frame->nb_samples) && !buf && !m_reset)
		{
			buf = m_omx->GetAudioBuffer(m_parser->GetPts());
			if (!buf)
				m_wait->Wait(10);
			else
				buf->nFilledLen = 0;
		}

		// decoding loop
		while ((!m_parser->Empty() || frame->nb_samples) && buf && !m_reset)
		{
			if (m_setupChanged |= (codec != m_parser->GetCodec() ||
					channels != m_parser->GetChannels() ||
					samplingRate != m_parser->GetSamplingRate()))
				break;

			// -- decode frame --
			if (!frame->nb_samples && !m_passthrough)
			{
				// if frame has been emptied, decode new data, rise reset
				// flag if something goes wrong
				int gotFrame = 0;
				int len = avcodec_decode_audio4(m_codecs[codec].context,
						frame, &gotFrame, m_parser->Packet());
				if (len > 0)
					m_parser->Shrink(len);

				if (len < 0)
				{
					ELOG("failed to decode audio frame!");
					m_reset = true;
					break;
				}
			}

			// -- get length --
			int len = m_passthrough ? m_parser->Packet()->size :
				av_samples_get_buffer_size(NULL,
					outputChannels == 6 ? 8 : outputChannels, frame->nb_samples,
					m_codecs[codec].context->sample_fmt, 1);

			if (len > (signed)(buf->nAllocLen - buf->nFilledLen) || len < 0)
			{
				// rise reset flag if packet is even bigger than allocated buffer
				m_reset = len > (signed)(buf->nAllocLen) || len < 0;
				if (m_reset)
					ELOG("encoded audio frame too big!");
				break;
			}

			// -- copy frame --
			if (m_passthrough)
			{
				// for pass-through directly copy AV packet to buffer
				memcpy(buf->pBuffer + buf->nFilledLen,
						m_parser->Packet()->data, len);

				buf->nFilledLen += len;
				m_parser->Shrink(len);
			}
			else if (frame->nb_samples)
			{
				if (outputChannels == 6)
				{
					int32_t* src = (int32_t*)frame->data[0];
					int32_t* dst = (int32_t*)(buf->pBuffer + buf->nFilledLen);

					// interleaved copy to fit 5.1 data into 8 channels
					for (int i = 0; i < frame->nb_samples; i++)
					{
						*dst++ = *src++; // LF & RF
						*dst++ = *src++; // CF & LFE
						*dst++ = *src++; // LR & RR
						*dst++ = 0;      // empty channels
					}
				}
				else
					memcpy(buf->pBuffer + buf->nFilledLen, frame->data[0], len);

				buf->nFilledLen += len;
				avcodec_get_frame_defaults(frame);
			}
			// if there's a valid PTS after shrinking, a complete PES packet
			// has been handled and is ready to play
			if (m_parser->GetPts())
				break;
		}

		// -- reset --
		if (m_reset)
		{
			m_parser->Reset();
			avcodec_get_frame_defaults(frame);
			if (buf)
			{
				cOmx::PtsToTicks(0, buf->nTimeStamp);
				buf->nFilledLen = 0;
				buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
			}
		}

		// -- empty buffer --
		if (buf && m_omx->EmptyAudioBuffer(buf))
			buf = 0;

		m_reset = false;

		// wait for new audio packets
		if (m_parser->Empty() && !frame->nb_samples)
			m_wait->Wait(50);
	}

	av_free(frame);
	SetCodec(cAudioCodec::eInvalid, outputChannels, samplingRate);
	DLOG("cAudioDecoder() thread ended");
}

void cRpiAudioDecoder::SetCodec(cAudioCodec::eCodec codec, unsigned int &channels, unsigned int samplingRate)
{
	m_omx->StopAudio();

	if (codec != cAudioCodec::eInvalid && channels > 0)
	{
		DLOG("set audio codec to %dch %s", channels, cAudioCodec::Str(codec));

		avcodec_flush_buffers(m_codecs[codec].context);
		m_codecs[codec].context->request_channel_layout = AV_CH_LAYOUT_NATIVE;
		m_codecs[codec].context->request_channels = 0;

		m_passthrough = false;
		cAudioCodec::eCodec outputFormat = cAudioCodec::ePCM;
		cRpiAudioPort::ePort outputPort = cRpiAudioPort::eLocal;

		if (cRpiSetup::GetAudioPort() == cRpiAudioPort::eHDMI &&
			cRpiSetup::IsAudioFormatSupported(cAudioCodec::ePCM, channels, samplingRate))
		{
			outputPort = cRpiAudioPort::eHDMI;

			if (cRpiSetup::IsAudioPassthrough() &&
				cRpiSetup::IsAudioFormatSupported(codec, channels, samplingRate))
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
			if (cRpiSetup::GetAudioPort() == cRpiAudioPort::eHDMI &&
				cRpiSetup::IsAudioFormatSupported(cAudioCodec::ePCM, 2, samplingRate))
				outputPort = cRpiAudioPort::eHDMI;
		}

		m_omx->SetupAudioRender(outputFormat, channels,	outputPort, samplingRate);
		ILOG("set %s audio output format to %dch %s, %d.%dkHz%s",
				cRpiAudioPort::Str(outputPort), channels, cAudioCodec::Str(outputFormat),
				samplingRate / 1000, (samplingRate % 1000) / 100,
				m_passthrough ? " (pass-through)" : "");
	}
}

int cRpiAudioDecoder::s_printPrefix = 1;

void cRpiAudioDecoder::Log(void* ptr, int level, const char* fmt, va_list vl)
{
	if (level == AV_LOG_QUIET)
		return;

	char line[128];
	av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &s_printPrefix);

	if (level <= AV_LOG_ERROR)
		ELOG("%s", line);
	else if (level <= AV_LOG_INFO)
		ILOG("%s", line);
	else if (level <= AV_LOG_VERBOSE)
		DLOG("%s", line);
}
