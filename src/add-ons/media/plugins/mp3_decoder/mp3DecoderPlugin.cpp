/*
 * Copyright (c) 2003-2004, Marcus Overhagen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <DataIO.h>
#include <Locker.h>
#include <MediaFormats.h>
#include <MediaRoster.h>
#include "mp3DecoderPlugin.h"

//#define TRACE_MP3_DECODER
#ifdef TRACE_MP3_DECODER
  #define TRACE printf
#else
  #define TRACE(a...)
#endif

#define DECODE_BUFFER_SIZE	(32 * 1024)

// reference:
// http://www.mp3-tech.org/programmer/frame_header.html

inline size_t
AudioBufferSize(const media_raw_audio_format &raf, bigtime_t buffer_duration = 50000 /* 50 ms */)
{
	return (raf.format & 0xf) * (raf.channel_count)
         * (size_t)((raf.frame_rate * buffer_duration) / 1000000.0);
}

// bit_rate_table[mpeg_version_index][layer_index][bitrate_index]
static const int bit_rate_table[4][4][16] =
{
	{ // mpeg version 2.5
		{ }, // undefined layer
		{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 }, // layer 3
		{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 }, // layer 2
		{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 } // layer 1
	},
	{ // undefined version
		{ },
		{ },
		{ },
		{ }
	},
	{ // mpeg version 2
		{ },
		{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 },
		{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 },
		{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }
	},
	{ // mpeg version 1
		{ },
		{ 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 },
		{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },
		{ 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}
	}
};

// frame_rate_table[mpeg_version_index][sampling_rate_index]
static const int frame_rate_table[4][4] =
{
	{ 11025, 12000, 8000, 0},	// mpeg version 2.5
	{ 0, 0, 0, 0 },
	{ 22050, 24000, 16000, 0},	// mpeg version 2
	{ 44100, 48000, 32000, 0}	// mpeg version 1
};


mp3Decoder::mp3Decoder()
{
	InitMP3(&fMpgLibPrivate);
	fResidualBytes = 0;
	fResidualBuffer = 0;
	fDecodeBuffer = new uint8 [DECODE_BUFFER_SIZE];
	fStartTime = 0;
	fFrameSize = 0;
	fFrameRate = 0;
	fBitRate = 0;
	fChannelCount = 0;
	fOutputBufferSize = 0;
	fNeedSync = true; // some files start with garbage
	fDecodingError = false;
}


mp3Decoder::~mp3Decoder()
{
	ExitMP3(&fMpgLibPrivate);
	delete [] fDecodeBuffer;
}


void 
mp3Decoder::GetCodecInfo(media_codec_info *info)
{
	strcpy(info->short_name, "mp3");
	strcpy(info->pretty_name, "MPEG audio decoder (mpeg123 mpglib)");
		// ToDo: could alter the above string depending on the real format
}


status_t
mp3Decoder::Setup(media_format *ioEncodedFormat,
				  const void *infoBuffer, size_t infoSize)
{
	// decode first chunk to initialize mpeg library
	if (B_OK != DecodeNextChunk()) {
		printf("mp3Decoder::Setup failed, can't decode first chunk\n");
		return B_ERROR;
	}

	// initialize fBitRate, fFrameRate and fChannelCount from mpg decode library values of first header
	extern int tabsel_123[2][3][16];
	extern long freqs[9];
	fBitRate = tabsel_123[fMpgLibPrivate.fr.lsf][fMpgLibPrivate.fr.lay-1][fMpgLibPrivate.fr.bitrate_index] * 1000;
	fFrameRate = freqs[fMpgLibPrivate.fr.sampling_frequency];
	fChannelCount = fMpgLibPrivate.fr.stereo;
	
	printf("mp3Decoder::Setup: channels %d, bitrate %d, framerate %d\n", fChannelCount, fBitRate, fFrameRate);
	
	// put some more useful info into the media_format describing our input format
	ioEncodedFormat->u.encoded_audio.bit_rate = fBitRate;
	ioEncodedFormat->u.encoded_audio.output.frame_rate = fFrameRate;
	ioEncodedFormat->u.encoded_audio.output.channel_count = fChannelCount;
	return B_OK;
}

status_t
mp3Decoder::NegotiateOutputFormat(media_format *ioDecodedFormat)
{
	// fFrameRate and fChannelCount are already valid here
	
	// BeBook says: The codec will find and return in ioFormat its best matching format
	// => This means, we never return an error, and always change the format values
	//    that we don't support to something more applicable

	ioDecodedFormat->type = B_MEDIA_RAW_AUDIO;
	ioDecodedFormat->u.raw_audio.frame_rate = fFrameRate;
	ioDecodedFormat->u.raw_audio.channel_count = fChannelCount;
	ioDecodedFormat->u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	ioDecodedFormat->u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	
	int frame_size = (ioDecodedFormat->u.raw_audio.format & 0xf) * ioDecodedFormat->u.raw_audio.channel_count;
	if (ioDecodedFormat->u.raw_audio.buffer_size == 0)
		ioDecodedFormat->u.raw_audio.buffer_size = AudioBufferSize(ioDecodedFormat->u.raw_audio);
	else
		ioDecodedFormat->u.raw_audio.buffer_size = (ioDecodedFormat->u.raw_audio.buffer_size / frame_size) * frame_size;

	if (ioDecodedFormat->u.raw_audio.channel_mask == 0)
		ioDecodedFormat->u.raw_audio.channel_mask = (fChannelCount == 1) ? B_CHANNEL_LEFT : B_CHANNEL_LEFT | B_CHANNEL_RIGHT;

	// setup rest of the needed variables
	fFrameSize = frame_size;
	fOutputBufferSize = ioDecodedFormat->u.raw_audio.buffer_size;

	return B_OK;
}


status_t
mp3Decoder::Seek(uint32 seekTo,
				 int64 seekFrame, int64 *frame,
				 bigtime_t seekTime, bigtime_t *time)
{
	fNeedSync = true;
	return B_OK;
}


status_t
mp3Decoder::Decode(void *buffer, int64 *frameCount,
				   media_header *mediaHeader, media_decode_info *info /* = 0 */)
{
	status_t last_err = B_LAST_BUFFER_ERROR;
	uint8 * out_buffer = static_cast<uint8 *>(buffer);
	int32	out_bytes_needed = fOutputBufferSize;
	int32	out_bytes = 0;

	mediaHeader->start_time = fStartTime;
	//TRACE("mp3Decoder: Decoding start time %.6f\n", fStartTime / 1000000.0);

	while (out_bytes_needed > 0) {

		if (fNeedSync) {
			TRACE("mp3Decoder::Decode: syncing...\n");
			ExitMP3(&fMpgLibPrivate);
			InitMP3(&fMpgLibPrivate);
			fDecodingError = false;
			fResidualBytes = 0;
			mediaHeader->start_time = -1;
			// fNeedSync is reset in DecodeNextChunk
		}

		if (fDecodingError)
			return B_ERROR;

		if (fResidualBytes) {
			int32 bytes = min_c(fResidualBytes, out_bytes_needed);
			memcpy(out_buffer, fResidualBuffer, bytes);
			fResidualBuffer += bytes;
			fResidualBytes -= bytes;
			out_buffer += bytes;
			out_bytes += bytes;
			out_bytes_needed -= bytes;
			
			fStartTime += (1000000LL * (bytes / fFrameSize)) / fFrameRate;

			//TRACE("mp3Decoder: fStartTime inc'd to %.6f\n", fStartTime / 1000000.0);
			continue;
		}
		
		last_err = DecodeNextChunk();
		if (last_err != B_OK) {
			fDecodingError = true;
			break;
		}
		if (mediaHeader->start_time == -1)
			mediaHeader->start_time = fStartTime;
	}

	*frameCount = out_bytes / fFrameSize;

	TRACE("framecount %Ld, time %Ld\n",*frameCount, mediaHeader->start_time);
	return (out_bytes > 0) ? B_OK : last_err;
}

status_t
mp3Decoder::DecodeNextChunk()
{
	const void *chunkBuffer;
	size_t chunkSize;
	media_header mh;
	int outsize;
	int result;
	status_t err;

	// decode residual data that is still in the decoder first
	result = decodeMP3(&fMpgLibPrivate, 0, 0, (char *)fDecodeBuffer, DECODE_BUFFER_SIZE, &outsize);
	if (result == MP3_OK) {
		fResidualBuffer = fDecodeBuffer;
		fResidualBytes = outsize;
		return B_OK;
	}
	
	// get another chunk and push it to the decoder
	err = GetNextChunk(&chunkBuffer, &chunkSize, &mh);
	if (err != B_OK)
		return err;

	fStartTime = mh.start_time;
//	TRACE("mp3Decoder: fStartTime reset to %.6f\n", fStartTime / 1000000.0);

	// resync after a seek		
	if (fNeedSync) {
		TRACE("mp3Decoder::DecodeNextChunk: Syncing...\n");
		if (chunkSize < 4) {
			TRACE("mp3Decoder::DecodeNextChunk: Sync failed, frame too small\n");
			return B_ERROR;
		}
		int len = GetFrameLength(chunkBuffer);
		TRACE("mp3Decoder::DecodeNextChunk: chunkSize %ld, first frame length %d\n", chunkSize, len);
		// len == -1 when not at frame start
		// len == chunkSize for mp3 reader (delivers single frames)
		if (len < (int)chunkSize) {
//			FIXME: join a few chunks to create a larger (2k) buffer, and use:
//			while (chunkSize > 100) {
//				if (IsValidStream((uint8 *)chunkBuffer, chunkSize))
			while (chunkSize >= 4) {
				if (GetFrameLength(chunkBuffer) > 0)
					break;
				chunkBuffer = (uint8 *)chunkBuffer + 1;
				chunkSize--;
			}
//			if (chunkSize <= 100) {
			if (chunkSize == 3) {	
				TRACE("mp3Decoder::DecodeNextChunk: Sync failed\n");
				return B_ERROR;
			}
			TRACE("mp3Decoder::DecodeNextChunk: new chunkSize %ld, first frame length %d\n", chunkSize, GetFrameLength(chunkBuffer));
		}
		fNeedSync = false;
	}
	
	result = decodeMP3(&fMpgLibPrivate, (char *)chunkBuffer, chunkSize, (char *)fDecodeBuffer, DECODE_BUFFER_SIZE, &outsize);
	if (result == MP3_NEED_MORE) {
		TRACE("mp3Decoder::DecodeNextChunk: decodeMP3 returned MP3_NEED_MORE\n");
		fResidualBuffer = NULL;
		fResidualBytes = 0;
		return B_OK;
	} else if (result != MP3_OK) {
		TRACE("mp3Decoder::DecodeNextChunk: decodeMP3 returned error %d\n", result);
		return B_ERROR;
//		fNeedSync = true;
//		fResidualBuffer = NULL;
//		fResidualBytes = 0;
//		return B_OK;
	}
		
	//printf("mp3Decoder::Decode: decoded %d bytes into %d bytes\n",chunkSize, outsize);
		
	fResidualBuffer = fDecodeBuffer;
	fResidualBytes = outsize;
	
	return B_OK;
}

bool
mp3Decoder::IsValidStream(const uint8 *buffer, int size)
{
	// check 3 consecutive frame headers to make sure
	// that the length encoded in the header is correct,
	// and also that mpeg version and layer do not change
	int length1 = GetFrameLength(buffer);
	if (length1 < 0 || (length1 + 4) > size)
		return false;
	int version_index1 = (buffer[1] >> 3) & 0x03;
	int layer_index1 = (buffer[1] >> 1) & 0x03;
	int length2 = GetFrameLength(buffer + length1);
	if (length2 < 0 || (length1 + length2 + 4) > size)
		return false;
	int version_index2 = (buffer[length1 + 1] >> 3) & 0x03;
	int layer_index2 = (buffer[length1 + 1] >> 1) & 0x03;
	if (version_index1 != version_index2 || layer_index1 != layer_index1)
		return false;
	int length3 = GetFrameLength(buffer + length1 + length2);
	if (length3 < 0)
		return false;
	int version_index3 = (buffer[length1 + length2 + 1] >> 3) & 0x03;
	int layer_index3 = (buffer[length1 + length2 + 1] >> 1) & 0x03;
	if (version_index2 != version_index3 || layer_index2 != layer_index3)
		return false;
	return true;
}

int
mp3Decoder::GetFrameLength(const void *header)
{
	const uint8 *h = static_cast<const uint8 *>(header);

	if (h[0] != 0xff)
		return -1;
	if ((h[1] & 0xe0) != 0xe0)
		return -1;
	
	int mpeg_version_index = (h[1] >> 3) & 0x03;
	int layer_index = (h[1] >> 1) & 0x03;
	int bitrate_index = (h[2] >> 4) & 0x0f;
	int sampling_rate_index = (h[2] >> 2) & 0x03;
	int padding = (h[2] >> 1) & 0x01;
	/* not interested in the other bits */
	
	int bitrate = bit_rate_table[mpeg_version_index][layer_index][bitrate_index];
	int framerate = frame_rate_table[mpeg_version_index][sampling_rate_index];
	
	if (!bitrate || !framerate)
		return -1;

	int length;	
	if (layer_index == 3) // layer 1
		length = ((144 * 1000 * bitrate) / framerate) + (padding * 4);
	else // layer 2 & 3
		length = ((144 * 1000 * bitrate) / framerate) + padding;

#if 0
	TRACE("%s %s, %s crc, bit rate %d, frame rate %d, padding %d, frame length %d\n",
		mpeg_version_index == 0 ? "mpeg 2.5" : (mpeg_version_index == 2 ? "mpeg 2" : "mpeg 1"),
		layer_index == 3 ? "layer 1" : (layer_index == 2 ? "layer 2" : "layer 3"),
		(h[1] & 0x01) ? "no" : "has",
		bitrate, framerate, padding, length);
#endif

	return length;
}


Decoder *
mp3DecoderPlugin::NewDecoder(uint index)
{
	static BLocker locker;
	static bool initdone = false;
	locker.Lock();
	if (!initdone) {
		InitMpgLib();
		initdone = true;
	}
	locker.Unlock();
	return new mp3Decoder;
}


static media_format mp3_formats[1];

status_t
mp3DecoderPlugin::GetSupportedFormats(media_format ** formats, size_t * count)
{
	const mpeg_id ids[] = {
		B_MPEG_1_AUDIO_LAYER_1,
		B_MPEG_1_AUDIO_LAYER_2,
		B_MPEG_1_AUDIO_LAYER_3,		//	"MP3"
		B_MPEG_2_AUDIO_LAYER_1,
		B_MPEG_2_AUDIO_LAYER_2,
		B_MPEG_2_AUDIO_LAYER_3,
		B_MPEG_2_5_AUDIO_LAYER_1,
		B_MPEG_2_5_AUDIO_LAYER_2,
		B_MPEG_2_5_AUDIO_LAYER_3,
	};
	const size_t otherIDs = 6;
	const size_t numIDs = otherIDs + sizeof(ids) / sizeof(mpeg_id);
	media_format_description descriptions[numIDs];
	descriptions[0].family = B_WAV_FORMAT_FAMILY;
	descriptions[0].u.wav.codec = 0x0050;
	descriptions[1].family = B_WAV_FORMAT_FAMILY;
	descriptions[1].u.wav.codec = 0x0055;
	descriptions[2].family = B_QUICKTIME_FORMAT_FAMILY;
	descriptions[2].u.quicktime.codec = '.mp3';
	descriptions[3].family = B_QUICKTIME_FORMAT_FAMILY;
	descriptions[3].u.quicktime.codec = '3pm.';
	descriptions[4].family = B_AVI_FORMAT_FAMILY;
	descriptions[4].u.avi.codec = '.mp3';
	descriptions[5].family = B_AVI_FORMAT_FAMILY;
	descriptions[5].u.avi.codec = '3pm.';
	for (size_t i = otherIDs; i < numIDs; i++) {
		descriptions[i].family = B_MPEG_FORMAT_FAMILY;
		descriptions[i].u.mpeg.id = ids[i-otherIDs];
	}

	media_format format;
	format.type = B_MEDIA_ENCODED_AUDIO;
	format.u.encoded_audio = media_encoded_audio_format::wildcard;
	format.u.encoded_audio.output.format = media_raw_audio_format::B_AUDIO_SHORT;
	format.u.encoded_audio.output.byte_order = B_MEDIA_HOST_ENDIAN;

	BMediaFormats mediaFormats;
	status_t result = mediaFormats.InitCheck();
	if (result != B_OK) {
		return result;
	}
	result = mediaFormats.MakeFormatFor(descriptions, numIDs, &format);
	if (result != B_OK) {
		return result;
	}
	mp3_formats[0] = format;

	*formats = mp3_formats;
	*count = 1;

	return result;
}


MediaPlugin *instantiate_plugin()
{
	return new mp3DecoderPlugin;
}
