/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the GNU L-GPL license.
 */

#include "AVFormatWriter.h"

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


#define TRACE_AVFORMAT_WRITER
#ifdef TRACE_AVFORMAT_WRITER
#	define TRACE printf
#	define TRACE_IO(a...)
#	define TRACE_PACKET printf
#else
#	define TRACE(a...)
#	define TRACE_IO(a...)
#	define TRACE_PACKET(a...)
#endif

#define ERROR(a...) fprintf(stderr, a)


static const size_t kIOBufferSize = 64 * 1024;
	// TODO: This could depend on the BMediaFile creation flags, IIRC,
	// they allow to specify a buffering mode.


// #pragma mark - URLProtocol
// TODO: Do we need to write an URLProtocol?


// #pragma mark - AVFormatWriter::StreamCookie


class AVFormatWriter::StreamCookie {
public:
								StreamCookie(AVFormatContext* context,
									BLocker* streamLock);
	virtual						~StreamCookie();

			status_t			Init(const media_format* format,
									const media_codec_info* codecInfo);

			status_t			WriteChunk(const void* chunkBuffer,
									size_t chunkSize,
									media_encode_info* encodeInfo);

			status_t			AddTrackInfo(uint32 code, const void* data,
									size_t size, uint32 flags);

private:
			AVFormatContext*	fContext;
			AVStream*			fStream;
			AVPacket			fPacket;
			// Since different threads may write to the target,
			// we need to protect the file position and I/O by a lock.
			BLocker*			fStreamLock;
			int64				fChunksWritten;
};



AVFormatWriter::StreamCookie::StreamCookie(AVFormatContext* context,
		BLocker* streamLock)
	:
	fContext(context),
	fStream(NULL),
	fStreamLock(streamLock),
	fChunksWritten(0)
{
	av_new_packet(&fPacket, 0);
}


AVFormatWriter::StreamCookie::~StreamCookie()
{
	av_free_packet(&fPacket);
}


status_t
AVFormatWriter::StreamCookie::Init(const media_format* format,
	const media_codec_info* codecInfo)
{
	TRACE("AVFormatWriter::StreamCookie::Init()\n");

	BAutolock _(fStreamLock);

	fPacket.stream_index = fContext->nb_streams;
	fStream = av_new_stream(fContext, fPacket.stream_index);

	if (fStream == NULL) {
		TRACE("  failed to add new stream\n");
		return B_ERROR;
	}

	// Setup the stream according to the media format...
	if (format->type == B_MEDIA_RAW_VIDEO) {
		avcodec_get_context_defaults2(fStream->codec, CODEC_TYPE_VIDEO);
		// frame rate
		fStream->codec->time_base.den = (int)format->u.raw_video.field_rate;
		fStream->codec->time_base.num = 1;
		fStream->r_frame_rate.den = (int)format->u.raw_video.field_rate;
		fStream->r_frame_rate.num = 1;
		fStream->time_base.den = (int)format->u.raw_video.field_rate;
		fStream->time_base.num = 1;
		// video size
		fStream->codec->width = format->u.raw_video.display.line_width;
		fStream->codec->height = format->u.raw_video.display.line_count;
		// pixel aspect ratio
		fStream->sample_aspect_ratio.num
			= format->u.raw_video.pixel_width_aspect;
		fStream->sample_aspect_ratio.den
			= format->u.raw_video.pixel_height_aspect;
		if (fStream->sample_aspect_ratio.num == 0
			|| fStream->sample_aspect_ratio.den == 0) {
			av_reduce(&fStream->sample_aspect_ratio.num,
				&fStream->sample_aspect_ratio.den, fStream->codec->width,
				fStream->codec->height, 255);
		}

		fStream->codec->sample_aspect_ratio = fStream->sample_aspect_ratio;
		// TODO: Don't hard code this...
		fStream->codec->pix_fmt = PIX_FMT_YUV420P;
	} else if (format->type == B_MEDIA_RAW_AUDIO) {
		avcodec_get_context_defaults2(fStream->codec, CODEC_TYPE_AUDIO);
		// channels
		fStream->codec->channels = format->u.raw_audio.channel_count;
		switch (format->u.raw_audio.format) {
			case media_raw_audio_format::B_AUDIO_FLOAT:
				fStream->codec->sample_fmt = SAMPLE_FMT_FLT;
				break;
			case media_raw_audio_format::B_AUDIO_DOUBLE:
				fStream->codec->sample_fmt = SAMPLE_FMT_DBL;
				break;
			case media_raw_audio_format::B_AUDIO_INT:
				fStream->codec->sample_fmt = SAMPLE_FMT_S32;
				break;
			case media_raw_audio_format::B_AUDIO_SHORT:
				fStream->codec->sample_fmt = SAMPLE_FMT_S16;
				break;
			case media_raw_audio_format::B_AUDIO_UCHAR:
				fStream->codec->sample_fmt = SAMPLE_FMT_U8;
				break;

			case media_raw_audio_format::B_AUDIO_CHAR:
			default:
				return B_MEDIA_BAD_FORMAT;
				break;
		}
		if (format->u.raw_audio.channel_mask == 0) {
			// guess the channel mask...
			switch (format->u.raw_audio.channel_count) {
				default:
				case 2:
					fStream->codec->channel_layout = CH_LAYOUT_STEREO;
					break;
				case 1:
					fStream->codec->channel_layout = CH_LAYOUT_MONO;
					break;
				case 3:
					fStream->codec->channel_layout = CH_LAYOUT_SURROUND;
					break;
				case 4:
					fStream->codec->channel_layout = CH_LAYOUT_QUAD;
					break;
				case 5:
					fStream->codec->channel_layout = CH_LAYOUT_5POINT0;
					break;
				case 6:
					fStream->codec->channel_layout = CH_LAYOUT_5POINT1;
					break;
				case 8:
					fStream->codec->channel_layout = CH_LAYOUT_7POINT1;
					break;
				case 10:
					fStream->codec->channel_layout = CH_LAYOUT_7POINT1_WIDE;
					break;
			}
		} else {
			// The bits match 1:1 for media_multi_channels and FFmpeg defines.
			fStream->codec->channel_layout = format->u.raw_audio.channel_mask;
		}
		// frame rate
		fStream->codec->sample_rate = (int)format->u.raw_audio.frame_rate;
		fStream->codec->time_base.den = (int)format->u.raw_audio.frame_rate;
		fStream->codec->time_base.num = 1;
		fStream->time_base.den = (int)format->u.raw_audio.frame_rate;
		fStream->time_base.num = 1;
	}

	TRACE("  stream->time_base: (%d/%d), codec->time_base: (%d/%d))\n",
		fStream->time_base.num, fStream->time_base.den,
		fStream->codec->time_base.num, fStream->codec->time_base.den);

	// TODO: This is a hack for now! Use avcodec_find_encoder_by_name()
	// or something similar...
	fStream->codec->codec_id = (CodecID)codecInfo->sub_id;

	return B_OK;
}


status_t
AVFormatWriter::StreamCookie::WriteChunk(const void* chunkBuffer,
	size_t chunkSize, media_encode_info* encodeInfo)
{
	TRACE_PACKET("AVFormatWriter::StreamCookie::WriteChunk(%p, %ld, "
		"start_time: %lld)\n", chunkBuffer, chunkSize, encodeInfo->start_time);

	BAutolock _(fStreamLock);

	// TODO: Probably the AVCodecEncoder needs to pass packet data
	// in encodeInfo...

	fPacket.data = const_cast<uint8_t*>((const uint8_t*)chunkBuffer);
	fPacket.size = chunkSize;

	fPacket.pts = (encodeInfo->start_time
		* fStream->time_base.den / fStream->time_base.num) / 1000000;
	TRACE_PACKET("  PTS: %lld  (stream->time_base: (%d/%d), "
		"codec->time_base: (%d/%d))\n", fPacket.pts,
		fStream->time_base.num, fStream->time_base.den,
		fStream->codec->time_base.num, fStream->codec->time_base.den);

// From ffmpeg.c::do_audio_out():
//	if (enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
//		fPacket.pts = av_rescale_q(enc->coded_frame->pts,
//		enc->time_base, ost->st->time_base);


#if 0
	// TODO: Eventually, we need to write interleaved packets, but
	// maybe we are only supposed to use this if we have actually
	// more than one stream. For the moment, this crashes in AVPacket
	// shuffling inside libavformat. Maybe if we want to use this, we
	// need to allocate a separate AVPacket and copy the chunk buffer.
	int result = av_interleaved_write_frame(fContext, &fPacket);
	if (result < 0)
		TRACE("  av_interleaved_write_frame(): %d\n", result);
#else
	int result = av_write_frame(fContext, &fPacket);
	if (result < 0)
		TRACE("  av_write_frame(): %d\n", result);
#endif

	return result == 0 ? B_OK : B_ERROR;
}


status_t
AVFormatWriter::StreamCookie::AddTrackInfo(uint32 code,
	const void* data, size_t size, uint32 flags)
{
	TRACE("AVFormatWriter::StreamCookie::AddTrackInfo(%lu, %p, %ld, %lu)\n",
		code, data, size, flags);

	BAutolock _(fStreamLock);

	return B_NOT_SUPPORTED;
}


// #pragma mark - AVFormatWriter


AVFormatWriter::AVFormatWriter()
	:
	fContext(avformat_alloc_context()),
	fHeaderWritten(false),
	fIOBuffer(NULL),
	fStreamLock("stream lock")
{
	TRACE("AVFormatWriter::AVFormatWriter\n");
}


AVFormatWriter::~AVFormatWriter()
{
	TRACE("AVFormatWriter::~AVFormatWriter\n");

	av_free(fContext);

	delete[] fIOBuffer;
}


// #pragma mark -


status_t
AVFormatWriter::Init(const media_file_format* fileFormat)
{
	TRACE("AVFormatWriter::Init()\n");

	delete[] fIOBuffer;
	fIOBuffer = new(std::nothrow) uint8[kIOBufferSize];
	if (fIOBuffer == NULL)
		return B_NO_MEMORY;

	// Init I/O context with buffer and hook functions, pass ourself as
	// cookie.
	if (init_put_byte(&fIOContext, fIOBuffer, kIOBufferSize, 0, this,
			0, _Write, _Seek) != 0) {
		TRACE("  init_put_byte() failed!\n");
		return B_ERROR;
	}

	// Setup I/O hooks. This seems to be enough.
	fContext->pb = &fIOContext;

	// Set the AVOutputFormat according to fileFormat...
	fContext->oformat = guess_format(fileFormat->short_name,
		fileFormat->file_extension, fileFormat->mime_type);
	if (fContext->oformat == NULL) {
		TRACE("  failed to find AVOuputFormat for %s\n",
			fileFormat->short_name);
		return B_NOT_SUPPORTED;
	}

	TRACE("  found AVOuputFormat for %s: %s\n", fileFormat->short_name,
		fContext->oformat->name);

	return B_OK;
}


status_t
AVFormatWriter::SetCopyright(const char* copyright)
{
	TRACE("AVFormatWriter::SetCopyright(%s)\n", copyright);

	return B_NOT_SUPPORTED;
}


status_t
AVFormatWriter::CommitHeader()
{
	TRACE("AVFormatWriter::CommitHeader\n");

	if (fContext == NULL)
		return B_NO_INIT;

	if (fHeaderWritten)
		return B_NOT_ALLOWED;

	for (unsigned i = 0; i < fContext->nb_streams; i++) {
		AVStream* stream = fContext->streams[i];
		TRACE("  stream[%u] time_base: (%d/%d), codec->time_base: (%d/%d)\n",
			i, stream->time_base.num, stream->time_base.den,
			stream->codec->time_base.num, stream->codec->time_base.den);
	}

	int result = av_write_header(fContext);
	if (result < 0)
		TRACE("  av_write_header(): %d\n", result);
	else
		fHeaderWritten = true;

	TRACE("  wrote header\n");
	for (unsigned i = 0; i < fContext->nb_streams; i++) {
		AVStream* stream = fContext->streams[i];
		TRACE("  stream[%u] time_base: (%d/%d), codec->time_base: (%d/%d)\n",
			i, stream->time_base.num, stream->time_base.den,
			stream->codec->time_base.num, stream->codec->time_base.den);
	}

	return result == 0 ? B_OK : B_ERROR;
}


status_t
AVFormatWriter::Flush()
{
	TRACE("AVFormatWriter::Flush\n");

	return B_NOT_SUPPORTED;
}


status_t
AVFormatWriter::Close()
{
	TRACE("AVFormatWriter::Close\n");

	if (fContext == NULL)
		return B_NO_INIT;

	if (!fHeaderWritten)
		return B_NOT_ALLOWED;

	int result = av_write_trailer(fContext);
	if (result < 0)
		TRACE("  av_write_trailer(): %d\n", result);

	return result == 0 ? B_OK : B_ERROR;
}


status_t
AVFormatWriter::AllocateCookie(void** _cookie, const media_format* format,
	const media_codec_info* codecInfo)
{
	TRACE("AVFormatWriter::AllocateCookie()\n");

	BAutolock _(fStreamLock);

	if (_cookie == NULL)
		return B_BAD_VALUE;

	StreamCookie* cookie = new(std::nothrow) StreamCookie(fContext,
		&fStreamLock);

	status_t ret = cookie->Init(format, codecInfo);
	if (ret != B_OK) {
		delete cookie;
		return ret;
	}

	*_cookie = cookie;
	return B_OK;
}


status_t
AVFormatWriter::FreeCookie(void* _cookie)
{
	BAutolock _(fStreamLock);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	delete cookie;

	return B_OK;
}


// #pragma mark -


status_t
AVFormatWriter::SetCopyright(void* cookie, const char* copyright)
{
	TRACE("AVFormatWriter::SetCopyright(%p, %s)\n", cookie, copyright);

	return B_NOT_SUPPORTED;
}


status_t
AVFormatWriter::AddTrackInfo(void* _cookie, uint32 code,
	const void* data, size_t size, uint32 flags)
{
	TRACE("AVFormatWriter::AddTrackInfo(%lu, %p, %ld, %lu)\n",
		code, data, size, flags);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	return cookie->AddTrackInfo(code, data, size, flags);
}


status_t
AVFormatWriter::WriteChunk(void* _cookie, const void* chunkBuffer,
	size_t chunkSize, media_encode_info* encodeInfo)
{
	TRACE_PACKET("AVFormatWriter::WriteChunk(%p, %ld, %p)\n", chunkBuffer,
		chunkSize, encodeInfo);

	StreamCookie* cookie = reinterpret_cast<StreamCookie*>(_cookie);
	return cookie->WriteChunk(chunkBuffer, chunkSize, encodeInfo);
}


// #pragma mark - I/O hooks


/*static*/ int
AVFormatWriter::_Write(void* cookie, uint8* buffer, int bufferSize)
{
	TRACE_IO("AVFormatWriter::_Write(%p, %p, %d)\n",
		cookie, buffer, bufferSize);

	AVFormatWriter* writer = reinterpret_cast<AVFormatWriter*>(cookie);

	ssize_t written = writer->fTarget->Write(buffer, bufferSize);

	TRACE_IO("  written: %ld\n", written);
	return (int)written;

}


/*static*/ off_t
AVFormatWriter::_Seek(void* cookie, off_t offset, int whence)
{
	TRACE_IO("AVFormatWriter::_Seek(%p, %lld, %d)\n",
		cookie, offset, whence);

	AVFormatWriter* writer = reinterpret_cast<AVFormatWriter*>(cookie);

	BPositionIO* positionIO = dynamic_cast<BPositionIO*>(writer->fTarget);
	if (positionIO == NULL)
		return -1;

	// Support for special file size retrieval API without seeking anywhere:
	if (whence == AVSEEK_SIZE) {
		off_t size;
		if (positionIO->GetSize(&size) == B_OK)
			return size;
		return -1;
	}

	off_t position = positionIO->Seek(offset, whence);
	TRACE_IO("  position: %lld\n", position);
	if (position < 0)
		return -1;

	return position;
}


