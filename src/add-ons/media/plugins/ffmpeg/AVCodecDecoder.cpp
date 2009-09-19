/*
 * Copyright (C) 2001 Carlos Hasan
 * Copyright (C) 2001 François Revol
 * Copyright (C) 2001 Axel Dörfler
 * Copyright (C) 2004 Marcus Overhagen
 * Copyright (C) 2009 Stephan Amßus <superstippi@gmx.de>
 *
 * All rights reserved. Distributed under the terms of the MIT License.
 */

//! libavcodec based decoder for Haiku

#include "AVCodecDecoder.h"

#include <new>

#include <string.h>

#include <Bitmap.h>
#include <Debug.h>


#undef TRACE
//#define TRACE_AV_CODEC
#ifdef TRACE_AV_CODEC
#	define TRACE(x...)	printf(x)
#else
#	define TRACE(x...)
#endif

//#define LOG_STREAM_TO_FILE
#ifdef LOG_STREAM_TO_FILE
#	include <File.h>
	static BFile sStreamLogFile("/boot/home/Desktop/AVCodecDebugStream.raw",
		B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	static int sDumpedPackets = 0;
#endif


struct wave_format_ex {
	uint16 format_tag;
	uint16 channels;
	uint32 frames_per_sec;
	uint32 avg_bytes_per_sec;
	uint16 block_align;
	uint16 bits_per_sample;
	uint16 extra_size;
	// extra_data[extra_size]
} _PACKED;


// profiling related globals
#define DO_PROFILING 0

static bigtime_t decodingTime = 0;
static bigtime_t conversionTime = 0;
static long profileCounter = 0;


AVCodecDecoder::AVCodecDecoder()
	:
	fHeader(),
	fInputFormat(),
	fOutputVideoFormat(),
	fFrame(0),
	fIsAudio(false),
	fCodecIndexInTable(-1),
	fCodec(NULL),
	fContext(avcodec_alloc_context()),
	fInputPicture(avcodec_alloc_frame()),
	fOutputPicture(avcodec_alloc_frame()),

	fCodecInitDone(false),

	fFormatConversionFunc(NULL),

	fExtraData(NULL),
	fExtraDataSize(0),
	fBlockAlign(0),

	fStartTime(0),
	fOutputFrameCount(0),
	fOutputFrameRate(1.0),
	fOutputFrameSize(0),

	fOutputBuffer(NULL),
	fOutputBufferOffset(0),
	fOutputBufferSize(0)
{
	TRACE("AVCodecDecoder::AVCodecDecoder()\n");
}


AVCodecDecoder::~AVCodecDecoder()
{
	TRACE("[%c] AVCodecDecoder::~AVCodecDecoder()\n", fIsAudio?('a'):('v'));

#ifdef DO_PROFILING
	if (profileCounter > 0) {
			printf("[%c] profile: d1 = %lld, d2 = %lld (%Ld)\n",
				fIsAudio?('a'):('v'), decodingTime / profileCounter, conversionTime / profileCounter,
				fFrame);
	}
#endif

	if (fCodecInitDone)
		avcodec_close(fContext);

	free(fOutputPicture);
	free(fInputPicture);
	free(fContext);

	delete[] fExtraData;
	delete[] fOutputBuffer;
}


void
AVCodecDecoder::GetCodecInfo(media_codec_info* mci)
{
	sprintf(mci->short_name, "ff:%s", fCodec->name);
	sprintf(mci->pretty_name, "%s (libavcodec %s)",
		gCodecTable[fCodecIndexInTable].prettyname, fCodec->name);
}


status_t
AVCodecDecoder::Setup(media_format* ioEncodedFormat, const void* infoBuffer,
	size_t infoSize)
{
	if (ioEncodedFormat->type != B_MEDIA_ENCODED_AUDIO
		&& ioEncodedFormat->type != B_MEDIA_ENCODED_VIDEO)
		return B_ERROR;

	fIsAudio = (ioEncodedFormat->type == B_MEDIA_ENCODED_AUDIO);
	TRACE("[%c] AVCodecDecoder::Setup()\n", fIsAudio?('a'):('v'));

	if (fIsAudio && !fOutputBuffer)
		fOutputBuffer = new char[AVCODEC_MAX_AUDIO_FRAME_SIZE];

#ifdef TRACE_AV_CODEC
	char buffer[1024];
	string_for_format(*ioEncodedFormat, buffer, sizeof(buffer));
	TRACE("[%c]   input_format = %s\n", fIsAudio?('a'):('v'), buffer);
	TRACE("[%c]   infoSize = %ld\n", fIsAudio?('a'):('v'), infoSize);
	TRACE("[%c]   user_data_type = %08lx\n", fIsAudio?('a'):('v'),
		ioEncodedFormat->user_data_type);
	TRACE("[%c]   meta_data_size = %ld\n", fIsAudio?('a'):('v'),
		ioEncodedFormat->MetaDataSize());
	TRACE("[%c]   info_size = %ld\n", fIsAudio?('a'):('v'), infoSize);
#endif

	media_format_description descr;
	for (int32 i = 0; gCodecTable[i].id; i++) {
		fCodecIndexInTable = i;
		uint64 cid;

		if (BMediaFormats().GetCodeFor(*ioEncodedFormat,
				gCodecTable[i].family, &descr) == B_OK
		    && gCodecTable[i].type == ioEncodedFormat->type) {
			switch(gCodecTable[i].family) {
				case B_WAV_FORMAT_FAMILY:
					cid = descr.u.wav.codec;
					break;
				case B_AIFF_FORMAT_FAMILY:
					cid = descr.u.aiff.codec;
					break;
				case B_AVI_FORMAT_FAMILY:
					cid = descr.u.avi.codec;
					break;
				case B_MPEG_FORMAT_FAMILY:
					cid = descr.u.mpeg.id;
					break;
				case B_QUICKTIME_FORMAT_FAMILY:
					cid = descr.u.quicktime.codec;
					break;
				case B_MISC_FORMAT_FAMILY:
					cid = (((uint64)descr.u.misc.file_format) << 32)
						| descr.u.misc.codec;
					break;
				default:
					puts("ERR family");
					return B_ERROR;
			}
			TRACE("  0x%04lx codec id = \"%c%c%c%c\"\n", uint32(cid),
				(char)((cid >> 24) & 0xff), (char)((cid >> 16) & 0xff),
				(char)((cid >> 8) & 0xff), (char)(cid & 0xff));

			if (gCodecTable[i].family == descr.family
				&& gCodecTable[i].fourcc == cid) {
				fCodec = avcodec_find_decoder(gCodecTable[i].id);
				if (fCodec == NULL) {
					TRACE("AVCodecDecoder: unable to find the correct FFmpeg "
						"decoder (id = %d)\n", gCodecTable[i].id);
					return B_ERROR;
				}
				TRACE("AVCodecDecoder: found decoder %s\n",fCodec->name);

				const void* extraData = infoBuffer;
				fExtraDataSize = infoSize;
				if (gCodecTable[i].family == B_WAV_FORMAT_FAMILY
						&& infoSize >= sizeof(wave_format_ex)) {
					// Special case extra data in B_WAV_FORMAT_FAMILY
					const wave_format_ex* waveFormatData
						= (const wave_format_ex*)infoBuffer;

					size_t waveFormatSize = infoSize;
					if (waveFormatData != NULL && waveFormatSize > 0) {
						fBlockAlign = waveFormatData->block_align;
						fExtraDataSize = waveFormatData->extra_size;
						// skip the wave_format_ex from the extra data.
						extraData = waveFormatData + 1;
					}
				} else {
					fBlockAlign
						= ioEncodedFormat->u.encoded_audio.output.buffer_size;
				}
				if (extraData != NULL && fExtraDataSize > 0) {
					TRACE("AVCodecDecoder: extra data size %ld\n", infoSize);
					fExtraData = new(std::nothrow) char[fExtraDataSize];
					if (fExtraData != NULL)
						memcpy(fExtraData, infoBuffer, fExtraDataSize);
					else
						fExtraDataSize = 0;
				}

				fInputFormat = *ioEncodedFormat;
				return B_OK;
			}
		}
	}
	printf("AVCodecDecoder::Setup failed!\n");
	return B_ERROR;
}


status_t
AVCodecDecoder::Seek(uint32 seekTo, int64 seekFrame, int64* frame,
	bigtime_t seekTime, bigtime_t* time)
{
	// Reset the FFmpeg codec to flush buffers, so we keep the sync
#if 1
	if (fCodecInitDone) {
		fCodecInitDone = false;
		avcodec_close(fContext);
		fCodecInitDone = (avcodec_open(fContext, fCodec) >= 0);
	}
#else
	// For example, this doesn't work on the H.264 codec. :-/
	if (fCodecInitDone)
		avcodec_flush_buffers(fContext);
#endif

	if (seekTo == B_MEDIA_SEEK_TO_TIME) {
		TRACE("AVCodecDecoder::Seek by time ");
		TRACE("from frame %Ld and time %.6f TO Required Time %.6f. ",
			fFrame, fStartTime / 1000000.0, seekTime / 1000000.0);

		*frame = (int64)(seekTime * fOutputFrameRate / 1000000LL);
		*time = seekTime;
	} else if (seekTo == B_MEDIA_SEEK_TO_FRAME) {
		TRACE("AVCodecDecoder::Seek by Frame ");
		TRACE("from time %.6f and frame %Ld TO Required Frame %Ld. ",
			fStartTime / 1000000.0, fFrame, seekFrame);

		*time = (bigtime_t)(seekFrame * 1000000LL / fOutputFrameRate);
		*frame = seekFrame;
	} else
		return B_BAD_VALUE;

	fFrame = *frame;
	fStartTime = *time;
	TRACE("so new frame is %Ld at time %.6f\n", *frame, *time / 1000000.0);
	return B_OK;
}


status_t
AVCodecDecoder::NegotiateOutputFormat(media_format* inOutFormat)
{
	TRACE("AVCodecDecoder::NegotiateOutputFormat() [%c] \n",
		fIsAudio?('a'):('v'));

#ifdef TRACE_AV_CODEC
	char buffer[1024];
	string_for_format(*inOutFormat, buffer, sizeof(buffer));
	TRACE("  [%c]  requested format = %s\n", fIsAudio?('a'):('v'), buffer);
#endif

	if (fIsAudio)
		return _NegotiateAudioOutputFormat(inOutFormat);
	else
		return _NegotiateVideoOutputFormat(inOutFormat);
}


status_t
AVCodecDecoder::Decode(void* outBuffer, int64* outFrameCount,
	media_header* mediaHeader, media_decode_info* info)
{
	if (!fCodecInitDone)
		return B_NO_INIT;

//	TRACE("[%c] AVCodecDecoder::Decode() for time %Ld\n", fIsAudio?('a'):('v'),
//		fStartTime);

	mediaHeader->start_time = fStartTime;

	status_t ret;
	if (fIsAudio)
		ret = _DecodeAudio(outBuffer, outFrameCount, mediaHeader, info);
	else
		ret = _DecodeVideo(outBuffer, outFrameCount, mediaHeader, info);

	fStartTime = (bigtime_t)(1000000LL * fFrame / fOutputFrameRate);

	return ret;
}


// #pragma mark -


status_t
AVCodecDecoder::_NegotiateAudioOutputFormat(media_format* inOutFormat)
{
	TRACE("AVCodecDecoder::_NegotiateAudioOutputFormat()\n");

	media_multi_audio_format outputAudioFormat;
	outputAudioFormat = media_raw_audio_format::wildcard;
	outputAudioFormat.byte_order = B_MEDIA_HOST_ENDIAN;
	outputAudioFormat.frame_rate
		= fInputFormat.u.encoded_audio.output.frame_rate;
	outputAudioFormat.channel_count
		= fInputFormat.u.encoded_audio.output.channel_count;
	outputAudioFormat.format = fInputFormat.u.encoded_audio.output.format;
	// Check that format is not still a wild card!
	if (outputAudioFormat.format == 0)
		outputAudioFormat.format = media_raw_audio_format::B_AUDIO_SHORT;

	outputAudioFormat.buffer_size
		= 1024 * fInputFormat.u.encoded_audio.output.channel_count;
	inOutFormat->type = B_MEDIA_RAW_AUDIO;
	inOutFormat->u.raw_audio = outputAudioFormat;

	fContext->bit_rate = (int)fInputFormat.u.encoded_audio.bit_rate;
	fContext->sample_rate
		= (int)fInputFormat.u.encoded_audio.output.frame_rate;
	fContext->channels = fInputFormat.u.encoded_audio.output.channel_count;
	fContext->block_align = fBlockAlign;
	fContext->extradata = (uint8_t*)fExtraData;
	fContext->extradata_size = fExtraDataSize;
	
	if (fInputFormat.MetaDataSize() > 0) {
		fContext->extradata = (uint8_t*)fInputFormat.MetaData();
		fContext->extradata_size = fInputFormat.MetaDataSize();
	}

	TRACE("bit_rate %d, sample_rate %d, channels %d, block_align %d, "
		"extradata_size %d\n", fContext->bit_rate, fContext->sample_rate,
		fContext->channels, fContext->block_align, fContext->extradata_size);

	// close any previous instance
	if (fCodecInitDone) {
		fCodecInitDone = false;
		avcodec_close(fContext);
	}

	// open new
	int result = avcodec_open(fContext, fCodec);
	fCodecInitDone = (result >= 0);

	TRACE("audio: bit_rate = %d, sample_rate = %d, chans = %d Init = %d\n",
		fContext->bit_rate, fContext->sample_rate, fContext->channels,
		result);

	fStartTime = 0;
	size_t sampleSize = outputAudioFormat.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK;
	fOutputFrameSize = sampleSize * outputAudioFormat.channel_count;
	fOutputFrameCount = outputAudioFormat.buffer_size / fOutputFrameSize;
	fOutputFrameRate = outputAudioFormat.frame_rate;
	fChunkBuffer = 0;
	fChunkBufferOffset = 0;
	fChunkBufferSize = 0;
	fOutputBufferOffset = 0;
	fOutputBufferSize = 0;

	inOutFormat->require_flags = 0;
	inOutFormat->deny_flags = B_MEDIA_MAUI_UNDEFINED_FLAGS;

	if (!fCodecInitDone) {
		TRACE("avcodec_open() failed!\n");
		return B_ERROR;
	}

	return B_OK;
}


status_t
AVCodecDecoder::_NegotiateVideoOutputFormat(media_format* inOutFormat)
{
	TRACE("AVCodecDecoder::_NegotiateVideoOutputFormat()\n");

	fOutputVideoFormat = fInputFormat.u.encoded_video.output;

	fContext->width = fOutputVideoFormat.display.line_width;
	fContext->height = fOutputVideoFormat.display.line_count;
//	fContext->frame_rate = (int)(fOutputVideoFormat.field_rate
//		* fContext->frame_rate_base);

	fOutputFrameRate = fOutputVideoFormat.field_rate;

	fContext->extradata = (uint8_t*)fExtraData;
	fContext->extradata_size = fExtraDataSize;

//	if (fInputFormat.MetaDataSize() > 0) {
//		fContext->extradata = (uint8_t*)fInputFormat.MetaData();
//		fContext->extradata_size = fInputFormat.MetaDataSize();
//	}

	TRACE("  requested video format 0x%x\n",
		inOutFormat->u.raw_video.display.format);

	// Make MediaPlayer happy (if not in rgb32 screen depth and no overlay,
	// it will only ask for YCbCr, which DrawBitmap doesn't handle, so the
	// default colordepth is RGB32).
	if (inOutFormat->u.raw_video.display.format == B_YCbCr422)
		fOutputVideoFormat.display.format = B_YCbCr422;
	else
		fOutputVideoFormat.display.format = B_RGB32;

	// Search for a pixel-format the codec handles
	// TODO: We should try this a couple of times until it succeeds, each
	// time using another pixel-format that is supported by the decoder.
	// But libavcodec doesn't seem to offer any way to tell the decoder
	// which format it should use.
	fFormatConversionFunc = 0;
	// Iterate over supported codec formats
	for (int i = 0; i < 1; i++) {
		// close any previous instance
		if (fCodecInitDone) {
			fCodecInitDone = false;
			avcodec_close(fContext);
		}
		// TODO: Set n-th fContext->pix_fmt here
		if (avcodec_open(fContext, fCodec) >= 0) {
			fCodecInitDone = true;

			fFormatConversionFunc = resolve_colorspace(
				fOutputVideoFormat.display.format, fContext->pix_fmt);
		}
		if (fFormatConversionFunc != NULL)
			break;
	}

	if (!fCodecInitDone) {
		TRACE("avcodec_open() failed to init codec!\n");
		return B_ERROR;
	}

	if (fFormatConversionFunc == NULL) {
		TRACE("no pixel format conversion function found or decoder has "
			"not set the pixel format yet!\n");
	}

	if (fOutputVideoFormat.display.format == B_YCbCr422) {
		fOutputVideoFormat.display.bytes_per_row
			= 2 * fOutputVideoFormat.display.line_width;
	} else {
		fOutputVideoFormat.display.bytes_per_row
			= 4 * fOutputVideoFormat.display.line_width;
	}

	inOutFormat->type = B_MEDIA_RAW_VIDEO;
	inOutFormat->u.raw_video = fOutputVideoFormat;

	inOutFormat->require_flags = 0;
	inOutFormat->deny_flags = B_MEDIA_MAUI_UNDEFINED_FLAGS;

#ifdef TRACE_AV_CODEC
	char buffer[1024];
	string_for_format(*inOutFormat, buffer, sizeof(buffer));
	TRACE("[v]  outFormat = %s\n", buffer);
	TRACE("  returned  video format 0x%x\n",
		inOutFormat->u.raw_video.display.format);
#endif

	return B_OK;
}


status_t
AVCodecDecoder::_DecodeAudio(void* outBuffer, int64* outFrameCount,
	media_header* mediaHeader, media_decode_info* info)
{
//	TRACE("audio start_time %.6f\n", mediaHeader->start_time / 1000000.0);

	char* output_buffer = (char*)outBuffer;
	*outFrameCount = 0;
	while (*outFrameCount < fOutputFrameCount) {
		if (fOutputBufferSize < 0) {
			TRACE("############ fOutputBufferSize %ld\n",
				fOutputBufferSize);
			fOutputBufferSize = 0;
		}
		if (fChunkBufferSize < 0) {
			TRACE("############ fChunkBufferSize %ld\n",
				fChunkBufferSize);
			fChunkBufferSize = 0;
		}

		if (fOutputBufferSize > 0) {
			int32 frames = min_c(fOutputFrameCount - *outFrameCount,
				fOutputBufferSize / fOutputFrameSize);
			memcpy(output_buffer, fOutputBuffer + fOutputBufferOffset,
				frames * fOutputFrameSize);
			fOutputBufferOffset += frames * fOutputFrameSize;
			fOutputBufferSize -= frames * fOutputFrameSize;
			output_buffer += frames * fOutputFrameSize;
			*outFrameCount += frames;
			fStartTime += (bigtime_t)((1000000LL * frames) / fOutputFrameRate);
			continue;
		}
		if (fChunkBufferSize == 0) {
			media_header chunkMediaHeader;
			status_t err;
			err = GetNextChunk(&fChunkBuffer, &fChunkBufferSize, &chunkMediaHeader);
			if (err == B_LAST_BUFFER_ERROR) {
				TRACE("Last Chunk with chunk size %ld\n",fChunkBufferSize);
				fChunkBufferSize = 0;
				return err;
			}
			if (err != B_OK || fChunkBufferSize < 0) {
				printf("GetNextChunk error %ld\n",fChunkBufferSize);
				fChunkBufferSize = 0;
				break;
			}
			fChunkBufferOffset = 0;
			fStartTime = chunkMediaHeader.start_time;
			if (*outFrameCount == 0)
				mediaHeader->start_time = chunkMediaHeader.start_time;
			continue;
		}
		if (fOutputBufferSize == 0) {
			int len;
			int out_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
			len = avcodec_decode_audio2(fContext, (short *)fOutputBuffer,
				&out_size, (uint8_t*)fChunkBuffer + fChunkBufferOffset,
				fChunkBufferSize);
			if (len < 0) {
				TRACE("########### audio decode error, "
					"fChunkBufferSize %ld, fChunkBufferOffset %ld\n",
					fChunkBufferSize, fChunkBufferOffset);
				out_size = 0;
				len = 0;
				fChunkBufferOffset = 0;
				fChunkBufferSize = 0;
//				} else {
//					TRACE("audio decode: len %d, out_size %d\n", len, out_size);
			}
			fChunkBufferOffset += len;
			fChunkBufferSize -= len;
			fOutputBufferOffset = 0;
			fOutputBufferSize = out_size;
		}
	}
	fFrame += *outFrameCount;

//	TRACE("Played %Ld frames at time %Ld\n",*outFrameCount, mediaHeader->start_time);
	return B_OK;
}


status_t
AVCodecDecoder::_DecodeVideo(void* outBuffer, int64* outFrameCount,
	media_header* mediaHeader, media_decode_info* info)
{
	bool firstRun = true;
	while (true) {
		const void* data;
		size_t size;
		media_header chunkMediaHeader;
		status_t err = GetNextChunk(&data, &size, &chunkMediaHeader);
		if (err != B_OK) {
			TRACE("AVCodecDecoder::_DecodeVideo(): error from "
				"GetNextChunk(): %s\n", strerror(err));
			return err;
		}
#ifdef LOG_STREAM_TO_FILE
		if (sDumpedPackets < 100) {
			sStreamLogFile.Write(data, size);
			printf("wrote %ld bytes\n", size);
			sDumpedPackets++;
		} else if (sDumpedPackets == 100)
			sStreamLogFile.Unset();
#endif

		if (firstRun) {
			firstRun = false;

			mediaHeader->type = B_MEDIA_RAW_VIDEO;
//			mediaHeader->start_time = chunkMediaHeader.start_time;
			mediaHeader->file_pos = 0;
			mediaHeader->orig_size = 0;
			mediaHeader->u.raw_video.field_gamma = 1.0;
			mediaHeader->u.raw_video.field_sequence = fFrame;
			mediaHeader->u.raw_video.field_number = 0;
			mediaHeader->u.raw_video.pulldown_number = 0;
			mediaHeader->u.raw_video.first_active_line = 1;
			mediaHeader->u.raw_video.line_count
				= fOutputVideoFormat.display.line_count;

			TRACE("[v] start_time=%02d:%02d.%02d field_sequence=%lu\n",
				int((mediaHeader->start_time / 60000000) % 60),
				int((mediaHeader->start_time / 1000000) % 60),
				int((mediaHeader->start_time / 10000) % 100),
				mediaHeader->u.raw_video.field_sequence);
		}

#if DO_PROFILING
		bigtime_t startTime = system_time();
#endif

		// NOTE: In the FFmpeg code example I've read, the length returned by
		// avcodec_decode_video() is completely ignored. Furthermore, the
		// packet buffers are supposed to contain complete frames only so we
		// don't seem to be required to buffer any packets because not the
		// complete packet has been read.
		int gotPicture = 0;
		int len = avcodec_decode_video(fContext, fInputPicture, &gotPicture,
			(uint8_t*)data, size);
		if (len < 0) {
			TRACE("[v] AVCodecDecoder: error in decoding frame %lld: %d\n",
				fFrame, len);
			// NOTE: An error from avcodec_decode_video() seems to be ignored
			// in the ffplay sample code.
//			return B_ERROR;
		}


//TRACE("FFDEC: PTS = %d:%d:%d.%d - fContext->frame_number = %ld "
//	"fContext->frame_rate = %ld\n", (int)(fContext->pts / (60*60*1000000)),
//	(int)(fContext->pts / (60*1000000)), (int)(fContext->pts / (1000000)),
//	(int)(fContext->pts % 1000000), fContext->frame_number,
//	fContext->frame_rate);
//TRACE("FFDEC: PTS = %d:%d:%d.%d - fContext->frame_number = %ld "
//	"fContext->frame_rate = %ld\n",
//	(int)(fInputPicture->pts / (60*60*1000000)),
//	(int)(fInputPicture->pts / (60*1000000)),
//	(int)(fInputPicture->pts / (1000000)),
//	(int)(fInputPicture->pts % 1000000), fContext->frame_number,
//	fContext->frame_rate);

		if (gotPicture) {
			int width = fOutputVideoFormat.display.line_width;
			int height = fOutputVideoFormat.display.line_count;
			AVPicture deinterlacedPicture;
			bool useDeinterlacedPicture = false;

			if (fInputPicture->interlaced_frame) {
				AVPicture source;
				source.data[0] = fInputPicture->data[0];
				source.data[1] = fInputPicture->data[1];
				source.data[2] = fInputPicture->data[2];
				source.data[3] = fInputPicture->data[3];
				source.linesize[0] = fInputPicture->linesize[0];
				source.linesize[1] = fInputPicture->linesize[1];
				source.linesize[2] = fInputPicture->linesize[2];
				source.linesize[3] = fInputPicture->linesize[3];

				avpicture_alloc(&deinterlacedPicture,
					fContext->pix_fmt, width, height);

				if (avpicture_deinterlace(&deinterlacedPicture, &source,
						fContext->pix_fmt, width, height) < 0) {
					TRACE("[v] avpicture_deinterlace() - error\n");
				} else
					useDeinterlacedPicture = true;
			}

#if DO_PROFILING
			bigtime_t formatConversionStart = system_time();
#endif
//			TRACE("ONE FRAME OUT !! len=%d size=%ld (%s)\n", len, size,
//				pixfmt_to_string(fContext->pix_fmt));

			// Some decoders do not set pix_fmt until they have decoded 1 frame
			if (fFormatConversionFunc == NULL) {
				fFormatConversionFunc = resolve_colorspace(
					fOutputVideoFormat.display.format, fContext->pix_fmt);
			}
			fOutputPicture->data[0] = (uint8_t*)outBuffer;
			fOutputPicture->linesize[0]
				= fOutputVideoFormat.display.bytes_per_row;

			if (fFormatConversionFunc != NULL) {
				if (useDeinterlacedPicture) {
					AVFrame inputFrame;
					inputFrame.data[0] = deinterlacedPicture.data[0];
					inputFrame.data[1] = deinterlacedPicture.data[1];
					inputFrame.data[2] = deinterlacedPicture.data[2];
					inputFrame.data[3] = deinterlacedPicture.data[3];
					inputFrame.linesize[0] = deinterlacedPicture.linesize[0];
					inputFrame.linesize[1] = deinterlacedPicture.linesize[1];
					inputFrame.linesize[2] = deinterlacedPicture.linesize[2];
					inputFrame.linesize[3] = deinterlacedPicture.linesize[3];

					(*fFormatConversionFunc)(&inputFrame,
						fOutputPicture, width, height);
				} else {
					(*fFormatConversionFunc)(fInputPicture, fOutputPicture,
						width, height);
				}
			}
			if (fInputPicture->interlaced_frame)
				avpicture_free(&deinterlacedPicture);
#ifdef DEBUG
			dump_ffframe(fInputPicture, "ffpict");
//			dump_ffframe(fOutputPicture, "opict");
#endif
			*outFrameCount = 1;
			fFrame++;

#if DO_PROFILING
			bigtime_t doneTime = system_time();
			decodingTime += formatConversionStart - startTime;
			conversionTime += doneTime - formatConversionStart;
			profileCounter++;
			if (!(fFrame % 10)) {
				if (info) {
					printf("[v] profile: d1 = %lld, d2 = %lld (%Ld) required "
						"%Ld\n",
						decodingTime / profileCounter,
						conversionTime / profileCounter,
						fFrame, info->time_to_decode);
				} else {
					printf("[v] profile: d1 = %lld, d2 = %lld (%Ld) required "
						"%Ld\n",
						decodingTime / profileCounter,
						conversionTime / profileCounter,
						fFrame, bigtime_t(1000000LL / fOutputFrameRate));
				}
			}
#endif
			return B_OK;
		} else {
			TRACE("frame %lld - no picture yet, len: %d, chunk size: %ld\n",
				fFrame, len, size);
		}
	}
}


