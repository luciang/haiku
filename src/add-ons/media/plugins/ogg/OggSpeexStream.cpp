#include "OggSpeexFormats.h"
#include "OggSpeexStream.h"
#include <stdio.h>

#define TRACE_THIS 1
#if TRACE_THIS
  #define TRACE printf
#else
  #define TRACE(a...) ((void)0)
#endif

inline size_t
AudioBufferSize(media_raw_audio_format * raf, bigtime_t buffer_duration = 50000 /* 50 ms */)
{
	return (raf->format & 0xf) * (raf->channel_count)
         * (size_t)((raf->frame_rate * buffer_duration) / 1000000.0);
}

/*
 * speex header from libspeex/speex_header.h
 * also documented at http://www.speex.org/manual/node7.html#SECTION00073000000000000000
 */

typedef struct SpeexHeader {
   char speex_string[8];       /**< Identifies a Speex bit-stream, always set to "Speex   " */
   char speex_version[20];     /**< Speex version */
   int speex_version_id;       /**< Version for Speex (for checking compatibility) */
   int header_size;            /**< Total size of the header ( sizeof(SpeexHeader) ) */
   int rate;                   /**< Sampling rate used */
   int mode;                   /**< Mode used (0 for narrowband, 1 for wideband) */
   int mode_bitstream_version; /**< Version ID of the bit-stream */
   int nb_channels;            /**< Number of channels encoded */
   int bitrate;                /**< Bit-rate used */
   int frame_size;             /**< Size of frames */
   int vbr;                    /**< 1 for a VBR encoding, 0 otherwise */
   int frames_per_packet;      /**< Number of frames stored per Ogg packet */
   int extra_headers;          /**< Number of additional headers after the comments */
   int reserved1;              /**< Reserved for future use, must be zero */
   int reserved2;              /**< Reserved for future use, must be zero */
} SpeexHeader;


/*
 * OggSpeexStream implementations
 */

/* static */ bool
OggSpeexStream::IsValidHeader(const ogg_packet & packet)
{
	return findIdentifier(packet,"Speex   ",0);
}

OggSpeexStream::OggSpeexStream(long serialno)
	: OggStream(serialno)
{
	TRACE("OggSpeexStream::OggSpeexStream\n");
}

OggSpeexStream::~OggSpeexStream()
{

}

status_t
OggSpeexStream::GetStreamInfo(int64 *frameCount, bigtime_t *duration,
                               media_format *format)
{
	TRACE("OggSpeexStream::GetStreamInfo\n");
	status_t result = B_OK;
	ogg_packet packet;

	// get header packet
	if (GetHeaderPackets().size() < 1) {
		result = GetPacket(&packet);
		if (result != B_OK) {
			return result;
		}
		SaveHeaderPacket(packet);
	}
	packet = GetHeaderPackets()[0];
	if (!packet.b_o_s) {
		return B_ERROR; // first packet was not beginning of stream
	}

	// parse header packet, check size against struct minus optional fields
	if (packet.bytes < 1 + (signed)sizeof(SpeexHeader) - 2*(signed)sizeof(int)) {
		return B_ERROR;
	}
	void * data = &(packet.packet[0]);
	SpeexHeader * header = (SpeexHeader *)data;

	// get the format for the description
	media_format_description description = speex_description();
	BMediaFormats formats;
	result = formats.InitCheck();
	if (result == B_OK) {
		result = formats.GetFormatFor(description, format);
	}
	if (result != B_OK) {
		*format = speex_encoded_media_format();
		// ignore error, allow user to use ReadChunk interface
	}

	// fill out format from header packet
	if (header->bitrate > 0) {
		format->u.encoded_audio.bit_rate = header->bitrate;
	} else {
		// TODO: manually compute it where possible
	}
	if (header->nb_channels == 1) {
		format->u.encoded_audio.multi_info.channel_mask = B_CHANNEL_LEFT;
	} else {
		format->u.encoded_audio.multi_info.channel_mask = B_CHANNEL_LEFT | B_CHANNEL_RIGHT;
	}
	format->u.encoded_audio.output.frame_rate = header->rate;
	format->u.encoded_audio.output.channel_count = header->nb_channels;
	// allocate buffer, round up to nearest speex output_length size
	int buffer_size = AudioBufferSize(&format->u.encoded_audio.output);
	int output_length = header->frame_size * header->nb_channels *
	                    (format->u.encoded_audio.output.format & 0xf);
	buffer_size = ((buffer_size - 1) / output_length + 1) * output_length;
	format->u.encoded_audio.output.buffer_size = buffer_size;

	// get comment packet
	if (GetHeaderPackets().size() < 2) {
		result = GetPacket(&packet);
		if (result != B_OK) {
			return result;
		}
		SaveHeaderPacket(packet);
	}

	// get extra headers
	while ((signed)GetHeaderPackets().size() < header->extra_headers) {
		result = GetPacket(&packet);
		if (result != B_OK) {
			return result;
		}
		SaveHeaderPacket(packet);
	}

	format->SetMetaData((void*)&GetHeaderPackets(),sizeof(GetHeaderPackets()));
	*duration = 100000000;
	*frameCount = 60000;
	return B_OK;
}


status_t
OggSpeexStream::GetNextChunk(const void **chunkBuffer, size_t *chunkSize,
                             media_header *mediaHeader)
{
	status_t result = GetPacket(&fChunkPacket);
	if (result != B_OK) {
		TRACE("OggSpeexStream::GetNextChunk failed: GetPacket = %s\n", strerror(result));
		return result;
	}
	*chunkBuffer = fChunkPacket.packet;
	*chunkSize = fChunkPacket.bytes;
	return B_OK;
}
