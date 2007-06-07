/*
 * Copyright (c) 2004-2007, Marcus Overhagen
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
#include <string.h>
#include "OpenDMLParser.h"
#include "avi.h"

//#define TRACE_ODML_PARSER
#ifdef TRACE_ODML_PARSER
  #define TRACE printf
#else
  #define TRACE(a...)
#endif

#define ERROR(a...) fprintf(stderr, a)

#if B_HOST_IS_BENDIAN
	#define DO_SWAP_INT32(x)	x = B_SWAP_INT32(x)
	#define DO_SWAP_INT16(x)	x = B_SWAP_INT16(x)
#endif

struct movie_chunk
{
	movie_chunk *	next;
	int64			start;
	uint32			size;
};

OpenDMLParser::OpenDMLParser(BPositionIO *source)
 :	fSource(source),
 	fSize(source->Seek(0, SEEK_END)),
 	fMovieListStart(0),
 	fStandardIndexStart(0),
 	fStandardIndexSize(0),
 	fStreamCount(0),
 	fMovieChunkCount(0),
	fAviMainHeaderValid(false),
	fOdmlExtendedHeaderValid(false),
	fStreams(0),
	fCurrentStream(0)
{
}

OpenDMLParser::~OpenDMLParser()
{
	// XXX free memory here!
}

int
OpenDMLParser::StreamCount()
{
	return fStreamCount;
}

const stream_info *
OpenDMLParser::StreamInfo(int index)
{
	if (index < 0 || index >= fStreamCount)
		return NULL;
	
	stream_info *info = fStreams;
	while (index--)
		info = info->next;
	return info;
}

int64
OpenDMLParser::StandardIndexStart()
{
	return fStandardIndexStart;
}

uint32
OpenDMLParser::StandardIndexSize()
{
	return fStandardIndexSize;
}

int64
OpenDMLParser::MovieListStart()
{
	return fMovieListStart;
}

const avi_main_header *
OpenDMLParser::AviMainHeader()
{
	return fAviMainHeaderValid ? &fAviMainHeader : 0;
}

const odml_extended_header *
OpenDMLParser::OdmlExtendedHeader()
{
	return fOdmlExtendedHeaderValid ? &fOdmlExtendedHeader : 0;
}

void
OpenDMLParser::CreateNewStreamInfo()
{
	stream_info *info = new stream_info;
	info->next = 0;
	info->is_audio = false;
	info->is_video = false;
	info->stream_header_valid = false;
	info->audio_format = 0;
	info->video_format_valid = false;
	info->odml_index_start = 0;
	info->odml_index_size = 0;
	
	// append the new stream_info to the fStreams list and point fCurrentStream to it
	if (fStreams) {
		stream_info *cur = fStreams;
		while (cur->next)
			cur = cur->next;
		cur->next = info;
	} else {
		fStreams = info;
	}
	fCurrentStream = info;
}

status_t
OpenDMLParser::Parse()
{
	TRACE("OpenDMLParser::Parse\n");

	if (!fSource) {
		ERROR("OpenDMLParser::Parse: no source\n");
		return B_ERROR;
	}
	if (fSize < 32) {
		ERROR("OpenDMLParser::Parse: file to small\n");
		return B_ERROR;
	}
		
	uint64 pos = 0;
	int riff_chunk_number = 0;
	while (pos < (uint64)fSize) {
		uint32 temp;
		uint32 fourcc;
		uint32 size;
		uint64 maxsize;
		
		maxsize = fSize - pos;
		
		if (maxsize < 13) {
			ERROR("OpenDMLParser::Parse: remaining size too small for RIFF AVI chunk data at pos %lld\n", pos);
			return B_ERROR;
		}

		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::Parse: read error at pos %llu\n", pos);
			return B_ERROR;
		}
		pos += 4;
		maxsize -= 4;
		fourcc = AVI_UINT32(temp);

		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::Parse: read error at pos %llu\n", pos);
			return B_ERROR;
		}
		pos += 4;
		maxsize -= 4;
		size = AVI_UINT32(temp);

		TRACE("OpenDMLParser::Parse: chunk '"FOURCC_FORMAT"', size = %lu, maxsize %Ld\n", FOURCC_PARAM(fourcc), size, maxsize);

		if (size == 0) {
			ERROR("OpenDMLParser::Parse: Error: chunk of size 0 found\n");
			return B_ERROR;
		}
	
		if (size > maxsize) {
			ERROR("OpenDMLParser::Parse: Warning chunk '"FOURCC_FORMAT"', size = %lu extends beyond end of file\n", FOURCC_PARAM(fourcc), size);
			ERROR("OpenDMLParser::Parse: Chunk at filepos %Ld truncated to %Ld, filesize %Ld\n", pos - 8, maxsize, fSize);
			size = maxsize;
		}
		
		if (fourcc == FOURCC('J','U','N','K')) {
			ERROR("OpenDMLParser::Parse: JUNK chunk ignored, size: %lu bytes\n", size);
			goto cont;
		}

		if (fourcc != FOURCC('R','I','F','F')) {
			if (riff_chunk_number == 0) {
				ERROR("OpenDMLParser::Parse: not a RIFF file\n");
				return B_ERROR;
			} else {
				TRACE("OpenDMLParser::Parse: unknown chunk '"FOURCC_FORMAT"' (expected 'RIFF'), size = %lu ignored\n", FOURCC_PARAM(fourcc), size);
				goto cont;
			}
			
		}

		TRACE("OpenDMLParser::Parse: it's a RIFF chunk!\n");

		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::Parse: read error at pos %llu\n", pos);
			return B_ERROR;
		}
		fourcc = AVI_UINT32(temp);

		if (riff_chunk_number == 0 && fourcc != FOURCC('A','V','I',' ')) {
			ERROR("OpenDMLParser::Parse: not a AVI file\n");
			return B_ERROR;
		}

		if (fourcc != FOURCC('A','V','I',' ') && fourcc != FOURCC('A','V','I','X')) {
			TRACE("OpenDMLParser::Parse: unknown RIFF subchunk '"FOURCC_FORMAT"' , size = %lu ignored, filepos %Ld, filesize %Ld\n",
				FOURCC_PARAM(fourcc), size, pos - 8, fSize);
			goto cont;
		}
		
		if (ParseChunk_AVI(riff_chunk_number, pos + 4, size - 4) < B_OK)
			return B_ERROR;

cont:
		pos += (size) + (size & 1);
		riff_chunk_number++;
	}
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_AVI(int number, uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_AVI\n");
	uint64 pos = start;
	uint64 end = start + size;

	if (size < 9) {
		ERROR("OpenDMLParser::ParseChunk_AVI: chunk is to small at pos %llu\n", start);
		return B_ERROR;
	}

	while (pos < end) {
		uint32 temp;
		uint32 Chunkfcc;
		uint32 Chunksize;

		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::ParseChunk_AVI: read error at pos %llu\n",pos);
			return B_ERROR;
		}
		pos += 4;
		Chunkfcc = AVI_UINT32(temp);
		
		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::ParseChunk_AVI: read error at pos %llu\n",pos);
			return B_ERROR;
		}
		pos += 4;
		Chunksize = AVI_UINT32(temp);

		uint32 maxsize = end - pos;

		TRACE("OpenDMLParser::ParseChunk_AVI: chunk '"FOURCC_FORMAT"', size = %lu, maxsize %lu\n", FOURCC_PARAM(Chunkfcc), Chunksize, maxsize);

		if (Chunksize == 0) {
			ERROR("OpenDMLParser::ParseChunk_AVI: chunk '"FOURCC_FORMAT"' has size 0\n", FOURCC_PARAM(Chunkfcc));
			return B_ERROR;
		}

		if (Chunksize > maxsize) {
			TRACE("OpenDMLParser::ParseChunk_AVI: chunk '"FOURCC_FORMAT"', size = %lu too big, truncated to %lu\n", FOURCC_PARAM(Chunkfcc), Chunksize, maxsize);
			Chunksize = maxsize;
		}

		if (Chunkfcc == FOURCC('L','I','S','T')) {
			if (ParseChunk_LIST(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('i','d','x','1')) {
			if (ParseChunk_idx1(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('J','U','N','K')) {
			; // do nothing
		} else {
			TRACE("OpenDMLParser::ParseChunk_AVI: unknown chunk ignored\n");
		}

		pos += (Chunksize) + (Chunksize & 1);
	}
	
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_LIST(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_LIST\n");
	uint32 temp;
	uint32 fourcc;
	
	if (size < 5) {
		ERROR("OpenDMLParser::ParseChunk_LIST: chunk is to small at pos %llu\n", start);
		return B_ERROR;
	}

	if (sizeof(temp) != fSource->ReadAt(start, &temp, sizeof(temp))) {
		ERROR("OpenDMLParser::ParseChunk_LIST: read error at pos %llu\n", start);
		return B_ERROR;
	}
	fourcc = AVI_UINT32(temp);

	TRACE("OpenDMLParser::ParseChunk_LIST: type '"FOURCC_FORMAT"'\n", FOURCC_PARAM(fourcc));
	
	if (fourcc == FOURCC('m','o','v','i')) {
		return ParseList_movi(start + 4, size - 4);
	} else if (fourcc == FOURCC('r','e','c',' ')) {
		// XXX parse rec simliar to movi?
		return ParseList_movi(start + 4, size - 4);
	} else if (fourcc == FOURCC('h','d','r','l')) {
		return ParseList_generic(start + 4, size - 4);
	} else if (fourcc == FOURCC('s','t','r','l')) {
		return ParseList_strl(start + 4, size - 4);
	} else if (fourcc == FOURCC('o','d','m','l')) {
		return ParseList_generic(start + 4, size - 4);
	} else {
		TRACE("OpenDMLParser::ParseChunk_LIST: unknown list type ignored\n");
		return B_OK;
	}
}

status_t
OpenDMLParser::ParseChunk_idx1(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_idx1\n");
	
	if (fStandardIndexSize != 0) {
		TRACE("OpenDMLParser::ParseChunk_idx1: found a second chunk\n");
		return B_OK; // just ignore, no error
	}

	fStandardIndexStart = start;
	fStandardIndexSize = size;
	
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_avih(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_avih\n");

	if (fAviMainHeaderValid) {
		TRACE("OpenDMLParser::ParseChunk_avih: found a second chunk\n");
		return B_OK; // just ignore, no error
	}

	if (size < sizeof(fAviMainHeader)) {
		TRACE("OpenDMLParser::ParseChunk_avih: warning, avi header chunk too small\n");
	}

	memset(&fAviMainHeader, 0, sizeof(fAviMainHeader));
	size = min_c(size, sizeof(fAviMainHeader));
	if ((ssize_t)size != fSource->ReadAt(start, &fAviMainHeader, size)) {
		ERROR("OpenDMLParser::ParseChunk_avih: read error at pos %llu\n", start);
		return B_ERROR;
	}
	
	#if B_HOST_IS_BENDIAN
		DO_SWAP_INT32(fAviMainHeader.micro_sec_per_frame);
		DO_SWAP_INT32(fAviMainHeader.max_bytes_per_sec);
		DO_SWAP_INT32(fAviMainHeader.padding_granularity);
		DO_SWAP_INT32(fAviMainHeader.flags);
		DO_SWAP_INT32(fAviMainHeader.total_frames);
		DO_SWAP_INT32(fAviMainHeader.initial_frames);
		DO_SWAP_INT32(fAviMainHeader.streams);
		DO_SWAP_INT32(fAviMainHeader.suggested_buffer_size);
		DO_SWAP_INT32(fAviMainHeader.width);
		DO_SWAP_INT32(fAviMainHeader.height);
	#endif

	fAviMainHeaderValid = true;
	
	TRACE("fAviMainHeader:\n");
	TRACE("micro_sec_per_frame   = %lu\n", fAviMainHeader.micro_sec_per_frame);
	TRACE("max_bytes_per_sec     = %lu\n", fAviMainHeader.max_bytes_per_sec);
	TRACE("padding_granularity   = %lu\n", fAviMainHeader.padding_granularity);
	TRACE("flags                 = 0x%lx\n", fAviMainHeader.flags);
	TRACE("total_frames          = %lu\n", fAviMainHeader.total_frames);
	TRACE("initial_frames        = %lu\n", fAviMainHeader.initial_frames);
	TRACE("streams               = %lu\n", fAviMainHeader.streams);
	TRACE("suggested_buffer_size = %lu\n", fAviMainHeader.suggested_buffer_size);
	TRACE("width                 = %lu\n", fAviMainHeader.width);
	TRACE("height                = %lu\n", fAviMainHeader.height);
	
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_strh(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_strh\n");
	
	if (fCurrentStream == 0) {
		ERROR("OpenDMLParser::ParseChunk_strh: error, no Stream info\n");
		return B_ERROR;
	}

	if (fCurrentStream->stream_header_valid) {
		TRACE("OpenDMLParser::ParseChunk_strh: error, already have stream header\n");
		return B_OK; // just ignore, no error
	}

	if (size < sizeof(fCurrentStream->stream_header)) {
		TRACE("OpenDMLParser::ParseChunk_strh: warning, avi stream header chunk too small\n");
	}

	memset(&fCurrentStream->stream_header, 0, sizeof(fCurrentStream->stream_header));

	size = min_c(size, sizeof(fCurrentStream->stream_header));
	if ((ssize_t)size != fSource->ReadAt(start, &fCurrentStream->stream_header, size)) {
		ERROR("OpenDMLParser::ParseChunk_strh: read error at pos %llu\n", start);
		return B_ERROR;
	}

	#if B_HOST_IS_BENDIAN
		DO_SWAP_INT32(fCurrentStream->stream_header.fourcc_type);
		DO_SWAP_INT32(fCurrentStream->stream_header.fourcc_handler);
		DO_SWAP_INT32(fCurrentStream->stream_header.flags);
		DO_SWAP_INT16(fCurrentStream->stream_header.priority);
		DO_SWAP_INT16(fCurrentStream->stream_header.language);
		DO_SWAP_INT32(fCurrentStream->stream_header.initial_frames);
		DO_SWAP_INT32(fCurrentStream->stream_header.scale);
		DO_SWAP_INT32(fCurrentStream->stream_header.rate);
		DO_SWAP_INT32(fCurrentStream->stream_header.start);
		DO_SWAP_INT32(fCurrentStream->stream_header.length);
		DO_SWAP_INT32(fCurrentStream->stream_header.suggested_buffer_size);
		DO_SWAP_INT32(fCurrentStream->stream_header.quality);
		DO_SWAP_INT32(fCurrentStream->stream_header.sample_size);
		DO_SWAP_INT16(fCurrentStream->stream_header.rect_left);
		DO_SWAP_INT16(fCurrentStream->stream_header.rect_top);
		DO_SWAP_INT16(fCurrentStream->stream_header.rect_right);
		DO_SWAP_INT16(fCurrentStream->stream_header.rect_bottom);
	#endif

	fCurrentStream->stream_header_valid = true;
	fCurrentStream->is_audio = fCurrentStream->stream_header.fourcc_type == FOURCC('a','u','d','s');
	fCurrentStream->is_video = fCurrentStream->stream_header.fourcc_type == FOURCC('v','i','d','s');

	TRACE("stream_header, Stream %d, is_audio %d, is_video %d:\n", fStreamCount - 1, fCurrentStream->is_audio, fCurrentStream->is_video);
	TRACE("fourcc_type           = '"FOURCC_FORMAT"'\n",FOURCC_PARAM(fCurrentStream->stream_header.fourcc_type));
	TRACE("fourcc_handler        = '"FOURCC_FORMAT"'\n",FOURCC_PARAM(fCurrentStream->stream_header.fourcc_handler));
	TRACE("flags                 = 0x%lx\n", fCurrentStream->stream_header.flags);
	TRACE("priority              = %u\n", fCurrentStream->stream_header.priority);
	TRACE("language              = %u\n", fCurrentStream->stream_header.language);
	TRACE("initial_frames        = %lu\n", fCurrentStream->stream_header.initial_frames);
	TRACE("scale                 = %lu\n", fCurrentStream->stream_header.scale);
	TRACE("rate                  = %lu\n", fCurrentStream->stream_header.rate);
	TRACE("frames/sec            = %.3f\n", fCurrentStream->stream_header.rate / (float)fCurrentStream->stream_header.scale);
	TRACE("start                 = %lu\n", fCurrentStream->stream_header.start);
	TRACE("length                = %lu\n", fCurrentStream->stream_header.length);
	TRACE("suggested_buffer_size = %lu\n", fCurrentStream->stream_header.suggested_buffer_size);
	TRACE("quality               = %lu\n", fCurrentStream->stream_header.quality);
	TRACE("sample_size           = %lu\n", fCurrentStream->stream_header.sample_size);
	TRACE("rect_left             = %d\n", fCurrentStream->stream_header.rect_left);
	TRACE("rect_top              = %d\n", fCurrentStream->stream_header.rect_top);
	TRACE("rect_right            = %d\n", fCurrentStream->stream_header.rect_right );
	TRACE("rect_bottom           = %d\n", fCurrentStream->stream_header.rect_bottom);
	
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_strf(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_strf, size %lu\n", size);

	if (fCurrentStream == 0) {
		ERROR("OpenDMLParser::ParseChunk_strf: error, no Stream info\n");
		return B_ERROR;
	}
	
	if (fCurrentStream->is_audio) {

		if (fCurrentStream->audio_format) {
			TRACE("OpenDMLParser::ParseChunk_strf: error, already have audio format header\n");
			return B_OK; // just ignore, no error
		}

//		if (size < sizeof(fCurrentStream->audio_format)) {
//			TRACE("OpenDMLParser::ParseChunk_strf: warning, avi audio header chunk too small\n");
//		}

		// if size to read is less then	sizeof(wave_format_ex), allocate
		// a full wave_format_ex and fill reminder with zero bytes
		fCurrentStream->audio_format_size = max_c(sizeof(wave_format_ex), size);
		fCurrentStream->audio_format = (wave_format_ex *) new char[fCurrentStream->audio_format_size];
		memset(size + (char *)fCurrentStream->audio_format, 0, fCurrentStream->audio_format_size - size);

		if ((ssize_t)size != fSource->ReadAt(start, fCurrentStream->audio_format, size)) {
			ERROR("OpenDMLParser::ParseChunk_strf: read error at pos %llu\n", start);
			delete [] fCurrentStream->audio_format;
			fCurrentStream->audio_format_size = 0;
			fCurrentStream->audio_format = 0;
			return B_ERROR;
		}
		
		#if B_HOST_IS_BENDIAN
			DO_SWAP_INT16(fCurrentStream->audio_format->format_tag);
			DO_SWAP_INT16(fCurrentStream->audio_format->channels);
			DO_SWAP_INT32(fCurrentStream->audio_format->frames_per_sec);
			DO_SWAP_INT32(fCurrentStream->audio_format->avg_bytes_per_sec);
			DO_SWAP_INT32(fCurrentStream->audio_format->block_align);
			DO_SWAP_INT32(fCurrentStream->audio_format->bits_per_sample);
			DO_SWAP_INT32(fCurrentStream->audio_format->extra_size);
		#endif

		TRACE("audio_format:\n");
		TRACE("format_tag        = 0x%x\n", fCurrentStream->audio_format->format_tag);
		TRACE("channels          = %u\n", fCurrentStream->audio_format->channels);
		TRACE("frames_per_sec    = %lu\n", fCurrentStream->audio_format->frames_per_sec);
		TRACE("avg_bytes_per_sec = %lu\n", fCurrentStream->audio_format->avg_bytes_per_sec);
		TRACE("block_align       = %u\n", fCurrentStream->audio_format->block_align);
		TRACE("bits_per_sample   = %u\n", fCurrentStream->audio_format->bits_per_sample);
		TRACE("extra_size        = %u\n", fCurrentStream->audio_format->extra_size);
		
		// XXX read extra data
	
	} else if (fCurrentStream->is_video) {

		if (fCurrentStream->video_format_valid) {
			TRACE("OpenDMLParser::ParseChunk_strf: error, already have video format header\n");
			return B_OK; // just ignore, no error
		}
	
//		if (size < sizeof(fCurrentStream->video_format)) {
//			TRACE("OpenDMLParser::ParseChunk_strf: warning, avi video header chunk too small\n");
//		}
	
		memset(&fCurrentStream->video_format, 0, sizeof(fCurrentStream->video_format));
	
		size = min_c(size, sizeof(fCurrentStream->video_format));
		if ((ssize_t)size != fSource->ReadAt(start, &fCurrentStream->video_format, size)) {
			ERROR("OpenDMLParser::ParseChunk_strf: read error at pos %llu\n", start);
			return B_ERROR;
		}
		
		#if B_HOST_IS_BENDIAN
			DO_SWAP_INT32(fCurrentStream->video_format.size);
			DO_SWAP_INT32(fCurrentStream->video_format.width);
			DO_SWAP_INT32(fCurrentStream->video_format.height);
			DO_SWAP_INT16(fCurrentStream->video_format.planes);
			DO_SWAP_INT16(fCurrentStream->video_format.bit_count);
			DO_SWAP_INT32(fCurrentStream->video_format.compression);
			DO_SWAP_INT32(fCurrentStream->video_format.image_size);
			DO_SWAP_INT32(fCurrentStream->video_format.x_pels_per_meter);
			DO_SWAP_INT32(fCurrentStream->video_format.y_pels_per_meter);
			DO_SWAP_INT32(fCurrentStream->video_format.clr_used);
			DO_SWAP_INT32(fCurrentStream->video_format.clr_important);
		#endif

		fCurrentStream->video_format_valid = true;

		TRACE("video_format:\n");
		TRACE("size             = %lu\n", fCurrentStream->video_format.size);
		TRACE("width            = %lu\n", fCurrentStream->video_format.width);
		TRACE("height           = %lu\n", fCurrentStream->video_format.height);
		TRACE("planes           = %u\n", fCurrentStream->video_format.planes);
		TRACE("bit_count        = %u\n", fCurrentStream->video_format.bit_count);
		TRACE("compression      = 0x%08lx\n", fCurrentStream->video_format.compression);
		TRACE("image_size       = %lu\n", fCurrentStream->video_format.image_size);
		TRACE("x_pels_per_meter = %lu\n", fCurrentStream->video_format.x_pels_per_meter);
		TRACE("y_pels_per_meter = %lu\n", fCurrentStream->video_format.y_pels_per_meter);
		TRACE("clr_used         = %lu\n", fCurrentStream->video_format.clr_used);
		TRACE("clr_important    = %lu\n", fCurrentStream->video_format.clr_important);

	} else {
		ERROR("OpenDMLParser::ParseChunk_strf: error, unknown Stream type\n");
	}
	
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_indx(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_indx\n");

	if (fCurrentStream == 0) {
		ERROR("OpenDMLParser::ParseChunk_indx: error, no stream info\n");
		return B_ERROR;
	}
	// XXX
	fCurrentStream->odml_index_start = start;
	fCurrentStream->odml_index_size = size;
	
	return B_OK;
}

status_t
OpenDMLParser::ParseChunk_dmlh(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseChunk_dmlh\n");

	if (fOdmlExtendedHeaderValid) {
		TRACE("OpenDMLParser::ParseChunk_dmlh: found a second chunk\n");
		return B_OK; // just ignore it, no error
	}

	if (size < sizeof(fOdmlExtendedHeader)) {
		TRACE("OpenDMLParser::ParseChunk_dmlh: warning, avi header chunk too small\n");
	}

	memset(&fOdmlExtendedHeader, 0, sizeof(fOdmlExtendedHeader));
	size = min_c(size, sizeof(fOdmlExtendedHeader));
	if ((ssize_t)size != fSource->ReadAt(start, &fOdmlExtendedHeader, size)) {
		ERROR("OpenDMLParser::ParseChunk_dmlh: read error at pos %llu\n", start);
		return B_ERROR;
	}
	
	#if B_HOST_IS_BENDIAN
		DO_SWAP_INT32(fOdmlExtendedHeader.total_frames);
	#endif

	fOdmlExtendedHeaderValid = true;
	
	TRACE("fOdmlExtendedHeader:\n");
	TRACE("total_frames   = %lu\n", fOdmlExtendedHeader.total_frames);

	return B_OK;
}

status_t
OpenDMLParser::ParseList_strl(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseList_strl\n");

	CreateNewStreamInfo();
	fStreamCount++;

	return ParseList_generic(start, size);
}

status_t
OpenDMLParser::ParseList_generic(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseList_generic\n");
	uint64 pos = start;
	uint64 end = start + size;

	if (size < 9) {
		ERROR("OpenDMLParser::ParseList_generic: list too small at pos %llu\n",pos);
		return B_ERROR;
	}

	while (pos < end) {
		uint32 temp;
		uint32 Chunkfcc;
		uint32 Chunksize;

		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::ParseList_generic: read error at pos %llu\n",pos);
			return B_ERROR;
		}
		pos += 4;
		Chunkfcc = AVI_UINT32(temp);
		
		if (sizeof(temp) != fSource->ReadAt(pos, &temp, sizeof(temp))) {
			ERROR("OpenDMLParser::ParseList_generic: read error at pos %llu\n",pos);
			return B_ERROR;
		}
		pos += 4;
		Chunksize = AVI_UINT32(temp);
		
		uint32 maxsize = end - pos;

		TRACE("OpenDMLParser::ParseList_generic: chunk '"FOURCC_FORMAT"', size = %lu, maxsize = %lu\n", FOURCC_PARAM(Chunkfcc), Chunksize, maxsize);

		if (Chunksize == 0) {
			ERROR("OpenDMLParser::ParseList_generic: chunk '"FOURCC_FORMAT"' has size 0\n", FOURCC_PARAM(Chunkfcc));
			return B_ERROR;
		}

		if (Chunksize > maxsize) {
			TRACE("OpenDMLParser::ParseList_generic: chunk '"FOURCC_FORMAT"', size = %lu too big, truncated to %lu\n", FOURCC_PARAM(Chunkfcc), Chunksize, maxsize);
			Chunksize = maxsize;
		}

		if (Chunkfcc == FOURCC('a','v','i','h')) {
			if (ParseChunk_avih(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('L','I','S','T')) {
			if (ParseChunk_LIST(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('s','t','r','h')) {
			if (ParseChunk_strh(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('s','t','r','f')) {
			if (ParseChunk_strf(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('i','n','d','x')) {
			if (ParseChunk_indx(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('d','m','l','h')) {
			if (ParseChunk_dmlh(pos, Chunksize) < B_OK)
				return B_ERROR;
		} else if (Chunkfcc == FOURCC('J','U','N','K')) {
			; // do nothing
		} else {
			TRACE("OpenDMLParser::ParseList_generic: unknown chunk ignored\n");
		}

		pos += Chunksize + (Chunksize & 1);
	}
	return B_OK;
}

status_t
OpenDMLParser::ParseList_movi(uint64 start, uint32 size)
{
	TRACE("OpenDMLParser::ParseList_movi\n");

	if (fMovieListStart == 0)
		fMovieListStart = start;
	
	fMovieChunkCount++;
	return B_OK;
}

