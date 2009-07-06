/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the GNU L-GPL license.
 */

#include "AVFormatReader.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <new>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <ByteOrder.h>
#include <DataIO.h>
#include <MediaDefs.h>
#include <MediaFormats.h>

extern "C" {
	#include "avformat.h"
}

#include "DemuxerTable.h"
#include "gfx_util.h"
//#include "RawFormats.h"


#define TRACE_AVFORMAT_READER
#ifdef TRACE_AVFORMAT_READER
#	define TRACE printf
#	define TRACE_IO(a...)
#	define TRACE_PACKET(a...)
#else
#	define TRACE(a...)
#	define TRACE_IO(a...)
#	define TRACE_PACKET(a...)
#endif

#define ERROR(a...) fprintf(stderr, a)


static const size_t kIOBufferSize = 64 * 1024;
	// TODO: This could depend on the BMediaFile creation flags, IIRC,
	// the allow to specify a buffering mode.


class AVFormatReader::StreamCookie {
public:
								StreamCookie(BPositionIO* source,
									BLocker* streamLock);
	virtual						~StreamCookie();

	// Init an indivual AVFormatContext
			status_t			Open();

	// Setup this stream to point to the AVStream at the given streamIndex.
	// This will also initialize the media_format.
			status_t			Init(int32 streamIndex);

	inline	const AVFormatContext* Context() const
									{ return fContext; }
			int32				Index() const;

	inline	const media_format&	Format() const
									{ return fFormat; }

			double				FrameRate() const;

	// Support for AVFormatContext
			status_t			GetStreamInfo(int64* frameCount,
									bigtime_t* duration, media_format* format,
									const void** infoBuffer,
									size_t* infoSize) const;

			status_t			Seek(uint32 flags, int64* frame,
									bigtime_t* time);
			status_t			FindKeyFrame(uint32 flags, int64* frame,
									bigtime_t* time) const;

			status_t			GetNextChunk(const void** chunkBuffer,
									size_t* chunkSize,
									media_header* mediaHeader);

private:
	// I/O hooks for libavformat, cookie will be a StreamCookie instance.
	// Since multiple StreamCookies use the same BPositionIO source, they
	// maintain the position individually, and may need to seek the source
	// if it does not match anymore in _Read().
	static	int					_Read(void* cookie, uint8* buffer,
									int bufferSize);
	static	off_t				_Seek(void* cookie, off_t offset, int whence);


			status_t			_NextPacket(bool reuse);
private:
			BPositionIO*		fSource;
			off_t				fPosition;
			// Since different threads may read from the source,
			// we need to protect the file position and I/O by a lock.
			BLocker*			fStreamLock;

			AVFormatContext*	fContext;
			AVStream*			fStream;

			ByteIOContext		fIOContext;
			uint8				fIOBuffer[kIOBufferSize];

			AVPacket			fPacket;
			bool				fReusePacket;

			media_format		fFormat;
};



AVFormatReader::StreamCookie::StreamCookie(BPositionIO* source,
		BLocker* streamLock)
	:
	fSource(source),
	fPosition(0),
	fStreamLock(streamLock),

	fContext(NULL),
	fStream(NULL),

	fReusePacket(false)
{
	memset(&fIOBuffer, 0, sizeof(fIOBuffer));
	memset(&fFormat, 0, sizeof(media_format));
	av_new_packet(&fPacket, 0);
}


AVFormatReader::StreamCookie::~StreamCookie()
{
	av_free_packet(&fPacket);
	av_free(fContext);
}


status_t
AVFormatReader::StreamCookie::Open()
{
	// Init probing data
	size_t probeSize = 1024;
	AVProbeData probeData;
	probeData.filename = "";
	probeData.buf = fIOBuffer;
	probeData.buf_size = probeSize;

	// Read a bit of the input...
	// NOTE: Even if other streams have already read from the source,
	// it is ok to not seek first, since our fPosition is 0, so the necessary
	// seek will happen automatically in _Read().
	if (_Read(this, fIOBuffer, probeSize) != (ssize_t)probeSize)
		return B_IO_ERROR;
	// ...and seek back to the beginning of the file. This is important
	// since libavformat will assume the stream to be at offset 0, the
	// probe data is not reused.
	_Seek(this, 0, SEEK_SET);

	// Probe the input format
	AVInputFormat* inputFormat = av_probe_input_format(&probeData, 1);

	if (inputFormat == NULL) {
		TRACE("AVFormatReader::StreamCookie::Init() - "
			"av_probe_input_format() failed!\n");
		return B_NOT_SUPPORTED;
	}

	TRACE("AVFormatReader::StreamCookie::Init() - "
		"av_probe_input_format(): %s\n", inputFormat->name);

	const DemuxerFormat* demuxerFormat = demuxer_format_for(inputFormat);
	if (demuxerFormat == NULL) {
		// We could support this format, but we don't want to. Bail out.
		ERROR("AVFormatReader::StreamCookie::Init() - "
			"support for demuxer '%s' is not enabled. "
			"See DemuxerTable.cpp\n", inputFormat->name);
		return B_NOT_SUPPORTED;
	}

	// Init I/O context with buffer and hook functions, pass ourself as
	// cookie.
	if (init_put_byte(&fIOContext, fIOBuffer, kIOBufferSize, 0, this,
			_Read, 0, _Seek) != 0) {
		TRACE("AVFormatReader::StreamCookie::Init() - "
			"init_put_byte() failed!\n");
		return B_ERROR;
	}

	// Initialize our context.
	if (av_open_input_stream(&fContext, &fIOContext, "", inputFormat,
			NULL) < 0) {
		TRACE("AVFormatReader::StreamCookie::Init() - "
			"av_open_input_stream() failed!\n");
		return B_NOT_SUPPORTED;
	}

	// Retrieve stream information
	if (av_find_stream_info(fContext) < 0) {
		TRACE("AVFormatReader::StreamCookie::Init() - "
			"av_find_stream_info() failed!\n");
		return B_NOT_SUPPORTED;
	}

	TRACE("AVFormatReader::StreamCookie::Init() - "
		"av_find_stream_info() success!\n");

	return B_OK;
}


status_t
AVFormatReader::StreamCookie::Init(int32 streamIndex)
{
	TRACE("AVFormatReader::StreamCookie::Init(%ld)\n", streamIndex);

	if (fContext == NULL)
		return B_NO_INIT;

	if (streamIndex < 0 || streamIndex >= (int32)fContext->nb_streams)
		return B_BAD_INDEX;

	const DemuxerFormat* demuxerFormat = demuxer_format_for(fContext->iformat);
	if (demuxerFormat == NULL) {
		TRACE("  unknown AVInputFormat!\n");
		return B_NOT_SUPPORTED;
	}

	// Make us point to the AVStream at streamIndex
	fStream = fContext->streams[streamIndex];

	// Get a pointer to the AVCodecContext for the stream at streamIndex.
	AVCodecContext* codecContext = fStream->codec;
	AVStream* stream = fStream;

	// initialize the media_format for this stream
	media_format* format = &fFormat;
	memset(format, 0, sizeof(media_format));

	media_format_description description;

	// Set format family and type depending on codec_type of the stream.
	switch (codecContext->codec_type) {
		case CODEC_TYPE_AUDIO:
			if ((codecContext->codec_id >= CODEC_ID_PCM_S16LE)
				&& (codecContext->codec_id <= CODEC_ID_PCM_U8)) {
				TRACE("  raw audio\n");
				format->type = B_MEDIA_RAW_AUDIO;
				description.family = B_ANY_FORMAT_FAMILY;
			} else {
				TRACE("  encoded audio\n");
				format->type = B_MEDIA_ENCODED_AUDIO;
				description.family = demuxerFormat->audio_family;
			}
			break;
		case CODEC_TYPE_VIDEO:
			TRACE("  encoded video\n");
			format->type = B_MEDIA_ENCODED_VIDEO;
			description.family = demuxerFormat->video_family;
			break;
		default:
			TRACE("  unknown type\n");
			format->type = B_MEDIA_UNKNOWN_TYPE;
			break;
	}

	if (format->type == B_MEDIA_RAW_AUDIO) {
		switch (codecContext->codec_id) {
			case CODEC_ID_PCM_S16LE:
				format->u.raw_audio.format
					= media_raw_audio_format::B_AUDIO_SHORT;
				format->u.raw_audio.byte_order
					= B_MEDIA_LITTLE_ENDIAN;
				break;
			case CODEC_ID_PCM_S16BE:
				format->u.raw_audio.format
					= media_raw_audio_format::B_AUDIO_SHORT;
				format->u.raw_audio.byte_order
					= B_MEDIA_BIG_ENDIAN;
				break;
			case CODEC_ID_PCM_U16LE:
//				format->u.raw_audio.format
//					= media_raw_audio_format::B_AUDIO_USHORT;
//				format->u.raw_audio.byte_order
//					= B_MEDIA_LITTLE_ENDIAN;
				return B_NOT_SUPPORTED;
				break;
			case CODEC_ID_PCM_U16BE:
//				format->u.raw_audio.format
//					= media_raw_audio_format::B_AUDIO_USHORT;
//				format->u.raw_audio.byte_order
//					= B_MEDIA_BIG_ENDIAN;
				return B_NOT_SUPPORTED;
				break;
			case CODEC_ID_PCM_S8:
				format->u.raw_audio.format
					= media_raw_audio_format::B_AUDIO_CHAR;
				break;
			case CODEC_ID_PCM_U8:
				format->u.raw_audio.format
					= media_raw_audio_format::B_AUDIO_UCHAR;
				break;
			default:
				return B_NOT_SUPPORTED;
				break;
		}
	} else {
		switch (description.family) {
			case B_AIFF_FORMAT_FAMILY:
				TRACE("  B_AIFF_FORMAT_FAMILY\n");
				description.u.aiff.codec = codecContext->codec_tag;
				break;
			case B_ASF_FORMAT_FAMILY:
				TRACE("  B_ASF_FORMAT_FAMILY\n");
//				description.u.asf.guid = GUID(codecContext->codec_tag);
				return B_NOT_SUPPORTED;
				break;
			case B_AVI_FORMAT_FAMILY:
				TRACE("  B_AVI_FORMAT_FAMILY\n");
				description.u.avi.codec = codecContext->codec_tag;
				break;
			case B_AVR_FORMAT_FAMILY:
				TRACE("  B_AVR_FORMAT_FAMILY\n");
				description.u.avr.id = codecContext->codec_tag;
				break;
			case B_MPEG_FORMAT_FAMILY:
				TRACE("  B_MPEG_FORMAT_FAMILY\n");
				if (codecContext->codec_id == CODEC_ID_MPEG1VIDEO)
					description.u.mpeg.id = B_MPEG_1_VIDEO;
				else if (codecContext->codec_id == CODEC_ID_MPEG2VIDEO)
					description.u.mpeg.id = B_MPEG_2_VIDEO;
				else if (codecContext->codec_id == CODEC_ID_MP2)
					description.u.mpeg.id = B_MPEG_2_AUDIO_LAYER_2;
				else if (codecContext->codec_id == CODEC_ID_MP3)
					description.u.mpeg.id = B_MPEG_1_AUDIO_LAYER_3;
				// TODO: Add some more...
				else
					description.u.mpeg.id = B_MPEG_ANY;
				break;
			case B_QUICKTIME_FORMAT_FAMILY:
				TRACE("  B_QUICKTIME_FORMAT_FAMILY\n");
				description.u.quicktime.codec
					= B_HOST_TO_BENDIAN_INT32(codecContext->codec_tag);
				break;
			case B_WAV_FORMAT_FAMILY:
				TRACE("  B_WAV_FORMAT_FAMILY\n");
				description.u.wav.codec = codecContext->codec_tag;
				break;
			case B_MISC_FORMAT_FAMILY:
				TRACE("  B_MISC_FORMAT_FAMILY\n");
				description.u.misc.codec = codecContext->codec_tag;
				break;

			default:
				break;
		}
		TRACE("  fourcc '%.4s'\n", (char*)&codecContext->codec_tag);

		BMediaFormats formats;
		status_t status = formats.GetFormatFor(description, format);
		if (status < B_OK)
			TRACE("  formats.GetFormatFor() error: %s\n", strerror(status));

		format->user_data_type = B_CODEC_TYPE_INFO;
		*(uint32*)format->user_data = codecContext->codec_tag;
		format->user_data[4] = 0;
	}

//	format->require_flags = 0;
	format->deny_flags = B_MEDIA_MAUI_UNDEFINED_FLAGS;

	switch (format->type) {
		case B_MEDIA_RAW_AUDIO:
			format->u.raw_audio.frame_rate = (float)codecContext->sample_rate;
			format->u.raw_audio.channel_count = codecContext->channels;
			format->u.raw_audio.buffer_size = 0;

			// Read one packet and mark it for later re-use. (So our first
			// GetNextChunk() call does not read another packet.)
			if ( _NextPacket(true) == B_OK) {
				TRACE("  successfully determined audio buffer size: %d\n",
					fPacket.size);
				format->u.raw_audio.buffer_size = fPacket.size;
			}
			break;

		case B_MEDIA_ENCODED_AUDIO:
			format->u.encoded_audio.bit_rate = codecContext->bit_rate;
			format->u.encoded_audio.frame_size = codecContext->frame_size;
			// Fill in some info about possible output format
			format->u.encoded_audio.output
				= media_multi_audio_format::wildcard;
			format->u.encoded_audio.output.frame_rate
				= (float)codecContext->sample_rate;
			format->u.encoded_audio.output.channel_count
				= codecContext->channels;
			break;

		case B_MEDIA_ENCODED_VIDEO:
// TODO: Specifying any of these seems to throw off the format matching
// later on.
//			format->u.encoded_video.avg_bit_rate = codecContext->bit_rate; 
//			format->u.encoded_video.max_bit_rate = codecContext->bit_rate
//				+ codecContext->bit_rate_tolerance;
	
//			format->u.encoded_video.encoding
//				= media_encoded_video_format::B_ANY;
	
//			format->u.encoded_video.frame_size = 1;
//			format->u.encoded_video.forward_history = 0;
//			format->u.encoded_video.backward_history = 0;
	
			format->u.encoded_video.output.field_rate
				= av_q2d(stream->r_frame_rate);
			format->u.encoded_video.output.interlace = 1;
				// TODO: Fix up for interlaced video
			format->u.encoded_video.output.first_active = 0;
			format->u.encoded_video.output.last_active
				= codecContext->height - 1;
				// TODO: Maybe libavformat actually provides that info
				// somewhere...
			format->u.encoded_video.output.orientation
				= B_VIDEO_TOP_LEFT_RIGHT;
	
			// TODO: Implement aspect ratio for real
			format->u.encoded_video.output.pixel_width_aspect
				= 1;//stream->sample_aspect_ratio.num;
			format->u.encoded_video.output.pixel_height_aspect
				= 1;//stream->sample_aspect_ratio.den;
	
			TRACE("  pixel width/height aspect: %d/%d or %.4f\n",
				stream->sample_aspect_ratio.num,
				stream->sample_aspect_ratio.den,
				av_q2d(stream->sample_aspect_ratio));

			format->u.encoded_video.output.display.format
				= pixfmt_to_colorspace(codecContext->pix_fmt);
			format->u.encoded_video.output.display.line_width
				= codecContext->width;
			format->u.encoded_video.output.display.line_count
				= codecContext->height;
			format->u.encoded_video.output.display.bytes_per_row = 0;
			format->u.encoded_video.output.display.pixel_offset = 0;
			format->u.encoded_video.output.display.line_offset = 0;
			format->u.encoded_video.output.display.flags = 0; // TODO

			break;

		default:
			// This is an unknown format to us.
			break;
	}

	// Add the meta data, if any
	if (codecContext->extradata_size > 0) {
		format->SetMetaData(codecContext->extradata,
			codecContext->extradata_size);
	}

#ifdef TRACE_AVFORMAT_READER
	char formatString[512];
	if (string_for_format(*format, formatString, sizeof(formatString)))
		TRACE("  format: %s\n", formatString);

	uint32 encoding = format->Encoding();
	TRACE("  encoding '%.4s'\n", (char*)&encoding);
#endif

	return B_OK;
}


int32
AVFormatReader::StreamCookie::Index() const
{
	if (fStream != NULL)
		return fStream->index;
	return -1;
}


double
AVFormatReader::StreamCookie::FrameRate() const
{
	switch (fStream->codec->codec_type) {
		case CODEC_TYPE_AUDIO:
			return (double)fStream->codec->sample_rate;
		case CODEC_TYPE_VIDEO:
			return av_q2d(fStream->r_frame_rate);
		default:
			return 0.0;
	}
}


status_t
AVFormatReader::StreamCookie::GetStreamInfo(int64* frameCount,
	bigtime_t* duration, media_format* format, const void** infoBuffer,
	size_t* infoSize) const
{
	TRACE("AVFormatReader::StreamCookie::GetStreamInfo()\n");

	double frameRate = FrameRate();
	TRACE("  frameRate: %.4f\n", frameRate);

	// TODO: This is obviously not working correctly for all stream types...
	// It seems that the calculations here are correct, because they work
	// for a couple of streams and are in line with the documentation, but
	// unfortunately, libavformat itself seems to set the time_base and
	// duration wrongly sometimes. :-(
	TRACE("  stream duration: %lld, time_base %.4f (%d/%d)\n",
		fStream->duration,
		av_q2d(fStream->time_base),
		fStream->time_base.num, fStream->time_base.den);
	*duration = (bigtime_t)(1000000LL * fStream->duration
		* av_q2d(fStream->time_base));

	TRACE("  duration: %lld or %.2fs\n", *duration, *duration / 1000000.0);

	*frameCount = fStream->nb_frames;
	if (*frameCount == 0) {
		// Calculate from duration and frame rate
		*frameCount = (int64)(*duration * frameRate / 1000000LL);
		TRACE("  frameCount (calculated): %lld\n", *frameCount);
	} else
		TRACE("  frameCount: %lld\n", *frameCount);

	*format = fFormat;

	// TODO: Possibly use fStream->metadata for this?
	*infoBuffer = 0;
	*infoSize = 0;

	return B_OK;
}


status_t
AVFormatReader::StreamCookie::Seek(uint32 flags, int64* frame,
	bigtime_t* time)
{
	if (fContext == NULL || fStream == NULL)
		return B_NO_INIT;

	if ((flags & B_MEDIA_SEEK_CLOSEST_FORWARD) != 0) {
		TRACE("  AVFormatReader::Seek() - B_MEDIA_SEEK_CLOSEST_FORWARD "
			"not supported.\n");
		return B_NOT_SUPPORTED;
	}

	TRACE("AVFormatReader::StreamCookie::Seek(%ld, %s %s %s %s, %lld, "
		"%lld)\n", Index(),
		(flags & B_MEDIA_SEEK_TO_FRAME) ? "B_MEDIA_SEEK_TO_FRAME" : "",
		(flags & B_MEDIA_SEEK_TO_TIME) ? "B_MEDIA_SEEK_TO_TIME" : "",
		(flags & B_MEDIA_SEEK_CLOSEST_BACKWARD) ? "B_MEDIA_SEEK_CLOSEST_BACKWARD" : "",
		(flags & B_MEDIA_SEEK_CLOSEST_FORWARD) ? "B_MEDIA_SEEK_CLOSEST_FORWARD" : "",
		*frame, *time);

	if ((flags & B_MEDIA_SEEK_TO_FRAME) != 0)
		*time = *frame * 1000000LL / FrameRate();

	double timeBase = av_q2d(fStream->time_base);
	int64_t timeStamp;
	if ((flags & B_MEDIA_SEEK_TO_FRAME) != 0) {
		// Can use frame, because stream timeStamp is actually in frame
		// units.
		timeStamp = *frame;
	} else
		timeStamp = (int64_t)(*time / timeBase / 1000000.0);

	TRACE("  time: %.2fs -> %lld, current DTS: %lld (time_base: %f)\n",
		*time / 1000000.0, timeStamp, fStream->cur_dts, timeBase);

	if (av_seek_frame(fContext, Index(), timeStamp, 0) < 0) {
		TRACE("  av_seek_frame() failed.\n");
		return B_ERROR;
	}

	// Our last packet is toast in any case.
	av_free_packet(&fPacket);
	fReusePacket = false;

	return B_OK;	
}


status_t
AVFormatReader::StreamCookie::FindKeyFrame(uint32 flags, int64* frame,
	bigtime_t* time) const
{
	if (fContext == NULL || fStream == NULL)
		return B_NO_INIT;

	TRACE("AVFormatReader::StreamCookie::FindKeyFrame(%ld, %s %s %s %s, %lld, "
		"%lld)\n", Index(),
		(flags & B_MEDIA_SEEK_TO_FRAME) ? "B_MEDIA_SEEK_TO_FRAME" : "",
		(flags & B_MEDIA_SEEK_TO_TIME) ? "B_MEDIA_SEEK_TO_TIME" : "",
		(flags & B_MEDIA_SEEK_CLOSEST_BACKWARD) ? "B_MEDIA_SEEK_CLOSEST_BACKWARD" : "",
		(flags & B_MEDIA_SEEK_CLOSEST_FORWARD) ? "B_MEDIA_SEEK_CLOSEST_FORWARD" : "",
		*frame, *time);

	double frameRate = FrameRate();
	if ((flags & B_MEDIA_SEEK_TO_FRAME) != 0)
		*time = *frame * 1000000LL / frameRate;

	double timeBase = av_q2d(fStream->time_base);
	int64_t timeStamp;
	if ((flags & B_MEDIA_SEEK_TO_FRAME) != 0) {
		// Can use frame, because stream timeStamp is actually in frame
		// units.
		timeStamp = *frame;
	} else
		timeStamp = (int64_t)(*time / timeBase / 1000000.0);

	TRACE("  time: %.2fs -> %lld (time_base: %f)\n", *time / 1000000.0,
		timeStamp, timeBase);

	int searchFlags = AVSEEK_FLAG_BACKWARD;
	if ((flags & B_MEDIA_SEEK_CLOSEST_FORWARD) != 0)
		searchFlags = 0;

	int index = av_index_search_timestamp(fStream, timeStamp, searchFlags);
	if (index < 0) {
		TRACE("  av_index_search_timestamp() failed.\n");
		// Just seek to the beginning of the stream and assume it is a
		// keyframe...
		*frame = 0;
		*time = 0;
		return B_OK;
	}

	const AVIndexEntry& entry = fStream->index_entries[index];
	timeStamp = entry.timestamp;
	*time = (bigtime_t)(timeStamp * 1000000.0 * timeBase);

	TRACE("  seeked time: %.2fs (%lld)\n", *time / 1000000.0, timeStamp);
	if ((flags & B_MEDIA_SEEK_TO_FRAME) != 0) {
		*frame = timeStamp;//*time * frameRate / 1000000LL;
		TRACE("  seeked frame: %lld\n", *frame);
	}

	return B_OK;
}


status_t
AVFormatReader::StreamCookie::GetNextChunk(const void** chunkBuffer,
	size_t* chunkSize, media_header* mediaHeader)
{
	TRACE_PACKET("AVFormatReader::StreamCookie::GetNextChunk()\n");

	status_t ret = _NextPacket(false);
	if (ret != B_OK)
		return ret;

	// According to libavformat documentation, fPacket is valid until the
	// next call to av_read_frame(). This is what we want and we can share
	// the memory with the least overhead.
	*chunkBuffer = fPacket.data;
	*chunkSize = fPacket.size;

	if (mediaHeader != NULL) {
		mediaHeader->type = fFormat.type;
		mediaHeader->buffer = 0;
		mediaHeader->destination = -1;
		mediaHeader->time_source = -1;
		mediaHeader->size_used = fPacket.size;
		mediaHeader->start_time = (bigtime_t)(1000000.0 * fPacket.pts
			/ av_q2d(fStream->time_base));
		mediaHeader->file_pos = fPacket.pos;
		mediaHeader->data_offset = 0;
		switch (mediaHeader->type) {
			case B_MEDIA_RAW_AUDIO:
				break;
			case B_MEDIA_ENCODED_AUDIO:
				mediaHeader->u.encoded_audio.buffer_flags
					= (fPacket.flags & PKT_FLAG_KEY) ? B_MEDIA_KEY_FRAME : 0;
				break;
			case B_MEDIA_RAW_VIDEO:
				mediaHeader->u.raw_video.line_count
					= fFormat.u.raw_video.display.line_count;
				break;
			case B_MEDIA_ENCODED_VIDEO:
				mediaHeader->u.encoded_video.field_flags
					= (fPacket.flags & PKT_FLAG_KEY) ? B_MEDIA_KEY_FRAME : 0;
				mediaHeader->u.encoded_video.line_count
					= fFormat.u.encoded_video.output.display.line_count;
				break;
			default:
				break;
		}
	}

	return B_OK;
}


// #pragma mark -


/*static*/ int
AVFormatReader::StreamCookie::_Read(void* cookie, uint8* buffer,
	int bufferSize)
{
	TRACE_IO("AVFormatReader::StreamCookie::_Read(%p, %p, %d)\n",
		cookie, buffer, whence);

	StreamCookie* stream = reinterpret_cast<StreamCookie*>(cookie);

	BAutolock _(stream->fStreamLock);

	if (stream->fPosition != stream->fSource->Position()) {
		off_t position
			= stream->fSource->Seek(stream->fPosition, SEEK_SET);
		if (position != stream->fPosition)
			return -1;
	}

	ssize_t read = stream->fSource->Read(buffer, bufferSize);
	if (read > 0)
		stream->fPosition += read;

	TRACE_IO("  read: %ld\n", read);
	return (int)read;

}


/*static*/ off_t
AVFormatReader::StreamCookie::_Seek(void* cookie, off_t offset, int whence)
{
	TRACE_IO("AVFormatReader::StreamCookie::_Seek(%p, %lld, %d)\n",
		cookie, offset, whence);

	StreamCookie* stream = reinterpret_cast<StreamCookie*>(cookie);

	BAutolock _(stream->fStreamLock);

	// Support for special file size retrieval API without seeking
	// anywhere:
	if (whence == AVSEEK_SIZE) {
		off_t size;
		if (stream->fSource->GetSize(&size) == B_OK)
			return size;
		return -1;
	}

	off_t position = stream->fSource->Seek(offset, whence);
	stream->fPosition = position;

	TRACE_IO("  position: %lld\n", position);
	return position;
}


status_t
AVFormatReader::StreamCookie::_NextPacket(bool reuse)
{
	TRACE_PACKET("AVFormatReader::StreamCookie::_NextPacket(%d)\n", reuse);

	if (fReusePacket) {
		// The last packet was marked for reuse, so we keep using it.
		TRACE_PACKET("  re-using last packet\n");
		fReusePacket = reuse;
		return B_OK;
	}

	av_free_packet(&fPacket);

	while (true) {
		if (av_read_frame(fContext, &fPacket) < 0) {
			fReusePacket = false;
			return B_LAST_BUFFER_ERROR;
		}

		if (fPacket.stream_index == Index())
			break;

		// This is a packet from another stream, ignore it.
		av_free_packet(&fPacket);
	}

	// Mark this packet with the new reuse flag.
	fReusePacket = reuse;
	return B_OK;
}


// #pragma mark - AVFormatReader


AVFormatReader::AVFormatReader()
	:
	fStreams(NULL),
	fStreamLock("stream lock")
{
	TRACE("AVFormatReader::AVFormatReader\n");
}


AVFormatReader::~AVFormatReader()
{
	TRACE("AVFormatReader::~AVFormatReader\n");
	if (fStreams != NULL) {
		delete fStreams[0];
		delete[] fStreams;
	}
}


// #pragma mark -


const char*
AVFormatReader::Copyright()
{
// TODO: Could not find the equivalent in libavformat >= version 53.
//	if (fStreams != NULL && fStreams[0] != NULL)
//		return fStreams[0]->Context()->copyright;
	// TODO: Return copyright of the file instead!
	return "Copyright 2009, Stephan Aßmus";
}

	
status_t
AVFormatReader::Sniff(int32* _streamCount)
{
	TRACE("AVFormatReader::Sniff\n");

	BPositionIO* source = dynamic_cast<BPositionIO*>(Source());
	if (source == NULL) {
		TRACE("  not a BPositionIO, but we need it to be one.\n");
		return B_NOT_SUPPORTED;
	}

	StreamCookie* stream = new(std::nothrow) StreamCookie(source,
		&fStreamLock);
	if (stream == NULL) {
		ERROR("AVFormatReader::Sniff() - failed to allocate StreamCookie\n");
		return B_NO_MEMORY;
	}

	ObjectDeleter<StreamCookie> streamDeleter(stream);

	status_t ret = stream->Open();
	if (ret != B_OK) {
		TRACE("  failed to detect stream: %s\n", strerror(ret));
		return ret;
	}

	delete[] fStreams;
	fStreams = NULL;

	int32 streamCount = stream->Context()->nb_streams;
	if (streamCount == 0) {
		TRACE("  failed to detect any streams: %s\n", strerror(ret));
		return B_ERROR;
	}

	fStreams = new(std::nothrow) StreamCookie*[streamCount];
	if (fStreams == NULL) {
		ERROR("AVFormatReader::Sniff() - failed to allocate streams\n");
		return B_NO_MEMORY;
	}

	memset(fStreams, 0, sizeof(StreamCookie*) * streamCount);
	fStreams[0] = stream;
	streamDeleter.Detach();

	if (_streamCount != NULL)
		*_streamCount = streamCount;

	return B_OK;
}


void
AVFormatReader::GetFileFormatInfo(media_file_format* mff)
{
	TRACE("AVFormatReader::GetFileFormatInfo\n");

	if (fStreams == NULL)
		return;

	// The first cookie is always there!
	const AVFormatContext* context = fStreams[0]->Context();

	if (context == NULL || context->iformat == NULL) {
		TRACE("  no AVFormatContext or AVInputFormat!\n");
		return;
	}

	const DemuxerFormat* format = demuxer_format_for(context->iformat);

	mff->capabilities = media_file_format::B_READABLE
		| media_file_format::B_KNOWS_ENCODED_VIDEO
		| media_file_format::B_KNOWS_ENCODED_AUDIO
		| media_file_format::B_IMPERFECTLY_SEEKABLE;

	if (format != NULL) {
		// TODO: Check if AVInputFormat has audio only and then use
		// format->audio_family!
		mff->family = format->video_family;
	} else {
		TRACE("  no DemuxerFormat for AVInputFormat!\n");
		mff->family = B_MISC_FORMAT_FAMILY;
	}

	mff->version = 100;

	if (format != NULL) {
		strcpy(mff->mime_type, format->mime_type);
	} else {
		// TODO: Would be nice to be able to provide this from AVInputFormat,
		// maybe by extending the FFmpeg code itself (all demuxers).
		strcpy(mff->mime_type, "");
	}

	if (context->iformat->extensions != NULL)
		strcpy(mff->file_extension, context->iformat->extensions);
	else {
		TRACE("  no file extensions for AVInputFormat.\n");
		strcpy(mff->file_extension, "");
	}

	if (context->iformat->name != NULL)
		strcpy(mff->short_name,  context->iformat->name);
	else {
		TRACE("  no short name for AVInputFormat.\n");
		strcpy(mff->short_name, "");
	}

	if (context->iformat->long_name != NULL)
		strcpy(mff->pretty_name, context->iformat->long_name);
	else {
		if (format != NULL)
			strcpy(mff->pretty_name, format->pretty_name);
		else
			strcpy(mff->pretty_name, "");
	}
}


// #pragma mark -


status_t
AVFormatReader::AllocateCookie(int32 streamIndex, void** _cookie)
{
	TRACE("AVFormatReader::AllocateCookie(%ld)\n", streamIndex);

	BAutolock _(fStreamLock);

	if (fStreams == NULL)
		return B_NO_INIT;

	const AVFormatContext* context = fStreams[0]->Context();

	if (streamIndex < 0 || streamIndex >= (int32)context->nb_streams)
		return B_BAD_INDEX;

	if (_cookie == NULL)
		return B_BAD_VALUE;

	StreamCookie* cookie = fStreams[streamIndex];
	if (cookie == NULL) {
		// Allocate the cookie
		BPositionIO* source = dynamic_cast<BPositionIO*>(Source());
		if (source == NULL) {
			TRACE("  not a BPositionIO, but we need it to be one.\n");
			return B_NOT_SUPPORTED;
		}

		cookie = new(std::nothrow) StreamCookie(source, &fStreamLock);
		if (cookie == NULL) {
			ERROR("AVFormatReader::Sniff() - failed to allocate "
				"StreamCookie\n");
			return B_NO_MEMORY;
		}

		status_t ret = cookie->Open();
		if (ret != B_OK) {
			TRACE("  stream failed to open: %s\n", strerror(ret));
			delete cookie;
			return ret;
		}
	}

	status_t ret = cookie->Init(streamIndex);
	if (ret != B_OK) {
		TRACE("  stream failed to initialize: %s\n", strerror(ret));
		// NOTE: Never delete the first stream!
		if (streamIndex != 0)
			delete cookie;
		return ret;
	}

	fStreams[streamIndex] = cookie;
	*_cookie = cookie;

	return B_OK;
}


status_t
AVFormatReader::FreeCookie(void *_cookie)
{
	BAutolock _(fStreamLock);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);

	// NOTE: Never delete the first cookie!
	if (cookie != NULL && cookie->Index() != 0) {
		if (fStreams != NULL)
			fStreams[cookie->Index()] = NULL;
		delete cookie;
	}

	return B_OK;
}


// #pragma mark -


status_t
AVFormatReader::GetStreamInfo(void* _cookie, int64* frameCount,
	bigtime_t* duration, media_format* format, const void** infoBuffer,
	size_t* infoSize)
{
	TRACE("AVFormatReader::GetStreamInfo()\n");

	BAutolock _(fStreamLock);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	return cookie->GetStreamInfo(frameCount, duration, format, infoBuffer,
		infoSize);
}


status_t
AVFormatReader::Seek(void* _cookie, uint32 seekTo, int64* frame,
	bigtime_t* time)
{
	TRACE("AVFormatReader::Seek()\n");

	BAutolock _(fStreamLock);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	return cookie->Seek(seekTo, frame, time);
}


status_t
AVFormatReader::FindKeyFrame(void* _cookie, uint32 flags, int64* frame,
	bigtime_t* time)
{
	TRACE("AVFormatReader::FindKeyFrame()\n");

	BAutolock _(fStreamLock);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	return cookie->FindKeyFrame(flags, frame, time);
}


status_t
AVFormatReader::GetNextChunk(void* _cookie, const void** chunkBuffer,
	size_t* chunkSize, media_header* mediaHeader)
{
	TRACE_PACKET("AVFormatReader::GetNextChunk()\n");

	BAutolock _(fStreamLock);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	return cookie->GetNextChunk(chunkBuffer, chunkSize, mediaHeader);
}


