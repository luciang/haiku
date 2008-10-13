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
#include <string.h>
#include <malloc.h>
#include <DataIO.h>
#include <ByteOrder.h>
#include <InterfaceDefs.h>
#include "WavReaderPlugin.h"
#include "RawFormats.h"

//#define TRACE_WAVE_READER
#ifdef TRACE_WAVE_READER
  #define TRACE printf
#else
  #define TRACE(a...)
#endif

#define BUFFER_SIZE	16384 // must be > 5200 for mp3 decoder to work

#define FOURCC(a,b,c,d)	((((uint32)(d)) << 24) | (((uint32)(c)) << 16) | (((uint32)(b)) << 8) | ((uint32)(a)))
#define UINT16(a) 		((uint16)B_LENDIAN_TO_HOST_INT16((a)))
#define UINT32(a) 		((uint32)B_LENDIAN_TO_HOST_INT32((a)))

struct wavdata
{
	int64 position;
	int64 datasize;

	uint32 bitrate;
	uint32 fps;

	void *buffer;
	uint32 buffersize;
	
	uint64 framecount;
	bigtime_t duration;
	
	bool raw;
	
	media_format format;
};

static bigtime_t FrameToTime(uint64 frame, uint32 fps) {
	return frame * 1000000LL / fps;
}

static uint64 TimeToFrame(bigtime_t time, uint32 fps) {
	return (time * fps) / 1000000LL;
}

static int64 TimeToPosition(bigtime_t time, uint32 bitrate) {
	return (time * bitrate) / 8000000LL;
}

static bigtime_t PositionToTime(int64 position, uint32 bitrate) {
	return (position * 8000000LL) / bitrate;
}

static int64 FrameToPosition(uint64 frame, uint32 bitrate, uint32 fps) {
	return TimeToPosition(FrameToTime(frame,fps),bitrate);
}

static uint64 PositionToFrame(int64 position, uint32 bitrate, uint32 fps) {
	return TimeToFrame(PositionToTime(position,bitrate),fps);
}

WavReader::WavReader()
{
	TRACE("WavReader::WavReader\n");
}

WavReader::~WavReader()
{
}
      
const char *
WavReader::Copyright()
{
	return "WAV reader, " B_UTF8_COPYRIGHT " by Marcus Overhagen";
}

	
status_t
WavReader::Sniff(int32 *streamCount)
{
	TRACE("WavReader::Sniff\n");

	fSource = dynamic_cast<BPositionIO *>(Reader::Source());

	fFileSize = Source()->Seek(0, SEEK_END);
	if (fFileSize < 44) {
		TRACE("WavReader::Sniff: File too small\n");
		return B_ERROR;
	}
	
	int64 pos = 0;
	
	riff_struct riff;

	if (sizeof(riff) != Source()->ReadAt(pos, &riff, sizeof(riff))) {
		TRACE("WavReader::Sniff: RIFF WAVE header reading failed\n");
		return B_ERROR;
	}
	pos += sizeof(riff);

	if (UINT32(riff.riff_id) != FOURCC('R','I','F','F') || UINT32(riff.wave_id) != FOURCC('W','A','V','E')) {
		TRACE("WavReader::Sniff: RIFF WAVE header not recognized\n");
		return B_ERROR;
	}
	
	format_struct format;
	format_struct_extensible format_ext;
	fact_struct fact;
	
	// read all chunks and search for "fact", "fmt" (normal or extensible) and "data"
	// everything else is ignored;
	bool foundFact = false;
	bool foundFmt = false;
	bool foundFmtExt = false;
	bool foundData = false;
	while (pos + sizeof(chunk_struct) <= fFileSize) {
		chunk_struct chunk;
		if (sizeof(chunk) != Source()->ReadAt(pos, &chunk, sizeof(chunk))) {
			TRACE("WavReader::Sniff: chunk header reading failed\n");
			return B_ERROR;
		}
		pos += sizeof(chunk);
		if (UINT32(chunk.len) == 0) {
			TRACE("WavReader::Sniff: Error: chunk of size 0 found\n");
			return B_ERROR;
		}
		switch (UINT32(chunk.fourcc)) {
			case FOURCC('f','m','t',' '):
				if (UINT32(chunk.len) >= sizeof(format)) {
					if (sizeof(format) != Source()->ReadAt(pos, &format, sizeof(format))) {
						TRACE("WavReader::Sniff: format chunk reading failed\n");
						break;
					}
					foundFmt = true;
					if (UINT32(chunk.len) >= sizeof(format_ext) && UINT16(format.format_tag) == 0xfffe) {
						if (sizeof(format_ext) != Source()->ReadAt(pos, &format_ext, sizeof(format_ext))) {
							TRACE("WavReader::Sniff: format extensible chunk reading failed\n");
							break;
						}
						foundFmtExt = true;
					}
				}
				break;
			case FOURCC('f','a','c','t'):
				if (UINT32(chunk.len) >= sizeof(fact)) {
					if (sizeof(fact) != Source()->ReadAt(pos, &fact, sizeof(fact))) {
						TRACE("WavReader::Sniff: fact chunk reading failed\n");
						break;
					}
					foundFact = true;
				}
				break;
			case FOURCC('d','a','t','a'):
				fDataStart = pos;
				fDataSize = UINT32(chunk.len);
				foundData = true;
				// If file size is >= 2GB and we already found a format chunk,
				// assume the rest of the file is data and get out of here.
				// This should allow reading wav files much bigger than 2 or 4 GB.
				if (fFileSize >= 0x7fffffff && foundFmt) {
					pos += fFileSize;
					fDataSize = fFileSize - fDataStart;
					TRACE("WavReader::Sniff: big file size %Ld, indicated data size %lu, assuming data size is %Ld\n",
						fFileSize, UINT32(chunk.len), fDataSize);
				}
				break;
			default:
				TRACE("WavReader::Sniff: ignoring chunk 0x%08lx of %lu bytes\n", UINT32(chunk.fourcc), UINT32(chunk.len));
				break;
		}
		pos += UINT32(chunk.len);
		pos += (pos & 1);
	}

	if (!foundFmt) {
		TRACE("WavReader::Sniff: couldn't find format chunk\n");
		return B_ERROR;
	}
	if (!foundData) {
		TRACE("WavReader::Sniff: couldn't find data chunk\n");
		return B_ERROR;
	}
	
	TRACE("WavReader::Sniff: we found something that looks like:\n");
	
	TRACE("  format_tag            0x%04x\n", UINT16(format.format_tag));
	TRACE("  channels              %d\n", UINT16(format.channels));
	TRACE("  samples_per_sec       %ld\n", UINT32(format.samples_per_sec));
	TRACE("  avg_bytes_per_sec     %ld\n", UINT32(format.avg_bytes_per_sec));
	TRACE("  block_align           %d\n", UINT16(format.block_align));
	TRACE("  bits_per_sample       %d\n", UINT16(format.bits_per_sample));
	if (foundFmtExt) {
		TRACE("  ext_size              %d\n", UINT16(format_ext.ext_size));
		TRACE("  valid_bits_per_sample %d\n", UINT16(format_ext.valid_bits_per_sample));
		TRACE("  channel_mask          %ld\n", UINT32(format_ext.channel_mask));
		TRACE("  guid[0-1] format      0x%04x\n", (format_ext.guid[1] << 8) | format_ext.guid[0]);
	}
	if (foundFact) {
		TRACE("  sample_length         %ld\n", UINT32(fact.sample_length));
	}

	fChannelCount = UINT16(format.channels);
	fSampleRate = UINT32(format.samples_per_sec);
	fBlockAlign = UINT16(format.block_align);
	fBitsPerSample = UINT16(format.bits_per_sample);
	if (fBitsPerSample == 0) {
		printf("WavReader::Sniff: Error, bits_per_sample = 0 calculating\n");
		fBitsPerSample = fBlockAlign * 8 / fChannelCount;
	}
	fFrameRate = fSampleRate * fChannelCount;
	
	fAvgBytesPerSec = format.avg_bytes_per_sec;
	
	// fact.sample_length is really no of samples for all channels
	fFrameCount = foundFact ? UINT32(fact.sample_length) / fChannelCount : 0;
	fFormatCode = UINT16(format.format_tag);
	if (fFormatCode == 0xfffe && foundFmtExt)
		fFormatCode = (format_ext.guid[1] << 8) | format_ext.guid[0];

	int min_align = (fFormatCode == 0x0001) ? (fBitsPerSample * fChannelCount + 7) / 8 : 1;
	if (fBlockAlign < min_align)
		fBlockAlign = min_align;

	TRACE("  fDataStart     %Ld\n", fDataStart);
	TRACE("  fDataSize      %Ld\n", fDataSize);
	TRACE("  fChannelCount  %ld\n", fChannelCount);
	TRACE("  fSampleRate    %ld\n", fSampleRate);
	TRACE("  fFrameRate     %ld\n", fFrameRate);
	TRACE("  fFrameCount    %Ld\n", fFrameCount);
	TRACE("  fBitsPerSample %d\n", fBitsPerSample);
	TRACE("  fBlockAlign    %d\n", fBlockAlign);
	TRACE("  min_align      %d\n", min_align);
	TRACE("  fFormatCode    0x%04x\n", fFormatCode);
	
	// XXX fact.sample_length contains duration of encoded files?

	*streamCount = 1;
	return B_OK;
}


void
WavReader::GetFileFormatInfo(media_file_format *mff)
{
	mff->capabilities =   media_file_format::B_READABLE
						| media_file_format::B_KNOWS_ENCODED_AUDIO
						| media_file_format::B_IMPERFECTLY_SEEKABLE;
	mff->family = B_WAV_FORMAT_FAMILY;
	mff->version = 100;
	strcpy(mff->mime_type, "audio/x-wav");
	strcpy(mff->file_extension, "wav");
	strcpy(mff->short_name,  "RIFF WAV audio");
	strcpy(mff->pretty_name, "RIFF WAV audio");
}


status_t
WavReader::AllocateCookie(int32 streamNumber, void **cookie)
{
	TRACE("WavReader::AllocateCookie\n");

	wavdata *data = new wavdata;

	data->position = 0;
	data->datasize = fDataSize;
	data->fps = fSampleRate;
	data->buffersize = (BUFFER_SIZE / fBlockAlign) * fBlockAlign;
	data->buffer = malloc(data->buffersize);
	data->framecount = fFrameCount ? fFrameCount : (8 * fDataSize) / (fChannelCount * fBitsPerSample);
	data->raw = fFormatCode == 0x0001;

	if (!fAvgBytesPerSec) {
		fAvgBytesPerSec = fSampleRate * fBlockAlign;
	}

	data->duration = (data->datasize * 1000000LL) / fAvgBytesPerSec;
	data->bitrate = fAvgBytesPerSec * 8;

	TRACE(" raw        %s\n", data->raw ? "true" : "false");
	TRACE(" framecount %Ld\n", data->framecount);
	TRACE(" duration   %Ld\n", data->duration);
	TRACE(" bitrate    %ld\n", data->bitrate);
	TRACE(" fps        %ld\n", data->fps);
	TRACE(" buffersize %ld\n", data->buffersize);
	
	BMediaFormats formats;
	if (fFormatCode == 0x0001) {
		// a raw PCM format
		media_format_description description;
		description.family = B_BEOS_FORMAT_FAMILY;
		description.u.beos.format = B_BEOS_FORMAT_RAW_AUDIO;
		formats.GetFormatFor(description, &data->format);
		// Really SampleRate
		data->format.u.raw_audio.frame_rate = fSampleRate;
		data->format.u.raw_audio.channel_count = fChannelCount;
		switch (fBitsPerSample) {
			case 8:
				data->format.u.raw_audio.format = B_AUDIO_FORMAT_UINT8;
				break;
			case 16:
				data->format.u.raw_audio.format = B_AUDIO_FORMAT_INT16;
				break;
			case 24:
				data->format.u.raw_audio.format = B_AUDIO_FORMAT_INT24;
				break;
			case 32:
				data->format.u.raw_audio.format = B_AUDIO_FORMAT_INT32;
				break;
			default:
				TRACE("WavReader::AllocateCookie: unhandled bits per sample %d\n", fBitsPerSample);
				return B_ERROR;
		}
		data->format.u.raw_audio.format |= B_AUDIO_FORMAT_CHANNEL_ORDER_WAVE;
		data->format.u.raw_audio.byte_order = B_MEDIA_LITTLE_ENDIAN;
		data->format.u.raw_audio.buffer_size = data->buffersize;
	} else {
		// some encoded format
		media_format_description description;
		description.family = B_WAV_FORMAT_FAMILY;
		description.u.wav.codec = fFormatCode;
		formats.GetFormatFor(description, &data->format);
		// Really SampleRate
		data->format.u.encoded_audio.output.frame_rate = fSampleRate;
		data->format.u.encoded_audio.output.channel_count = fChannelCount;
	}
	
	// store the cookie
	*cookie = data;
	return B_OK;
}


status_t
WavReader::FreeCookie(void *cookie)
{
	TRACE("WavReader::FreeCookie\n");
	wavdata *data = reinterpret_cast<wavdata *>(cookie);

	free(data->buffer);
	delete data;
	
	return B_OK;
}


status_t
WavReader::GetStreamInfo(void *cookie, int64 *frameCount, bigtime_t *duration,
						 media_format *format, const void **infoBuffer, size_t *infoSize)
{
	wavdata *data = reinterpret_cast<wavdata *>(cookie);
	
	*frameCount = data->framecount;
	*duration = data->duration;
	*format = data->format;
	*infoBuffer = 0;
	*infoSize = 0;
	return B_OK;
}

status_t
WavReader::CalculateNewPosition(void *cookie,
				uint32 flags,
				int64 *frame, bigtime_t *time, int64 *position)
{
	wavdata *data = reinterpret_cast<wavdata *>(cookie);

	if (flags & B_MEDIA_SEEK_TO_FRAME) {
		TRACE(" to frame %Ld",*frame);
		*position = FrameToPosition(*frame, data->bitrate, data->fps);

	} else if (flags & B_MEDIA_SEEK_TO_TIME) {
		TRACE(" to time %Ld", *time);
		*position = TimeToPosition(*time, data->bitrate);
	} else {
		printf("WavReader::CalculateNewPosition invalid flag passed %ld\n", flags);
		return B_ERROR;
	}

	*position = (*position / fBlockAlign) * fBlockAlign; // round down to a block start

	TRACE(", position %Ld ", *position);

	*frame = PositionToFrame(*position, data->bitrate, data->fps);
	*time = FrameToTime(*frame,data->fps);

	TRACE("newtime %Ld ", *time);
	TRACE("newframe %Ld\n", *frame);
	
	if (*position < 0 || *position > data->datasize) {
		printf("WavReader::CalculateNewPosition invalid position %Ld\n", *position);
		return B_ERROR;
	}
	
	return B_OK;
}

status_t
WavReader::Seek(void *cookie,
				uint32 flags,
				int64 *frame, bigtime_t *time)
{
	// Seek to the given position
	wavdata *data = reinterpret_cast<wavdata *>(cookie);
	status_t status;
	int64 pos;
	
	TRACE("WavReader::Seek");
	status = CalculateNewPosition(cookie, flags, frame, time, &pos);
	
	if (status == B_OK) {
		// set the new position so next GetNextChunk will read from new seek pos
		data->position = pos;
	}
	
	return status;
}

status_t
WavReader::FindKeyFrame(void* cookie, uint32 flags,
							int64* frame, bigtime_t* time)
{
	// Find a seek position without actually seeking
	int64 pos;
	TRACE("WavReader::FindKeyFrame");
	
	return CalculateNewPosition(cookie, flags, frame, time, &pos);
}

status_t
WavReader::GetNextChunk(void *cookie,
						const void **chunkBuffer, size_t *chunkSize,
						media_header *mediaHeader)
{
	wavdata *data = reinterpret_cast<wavdata *>(cookie);

	// XXX it might be much better to not return any start_time information for encoded formats here,
	// XXX and instead use the last time returned from seek and count forward after decoding.
	mediaHeader->start_time = PositionToTime(data->position,data->bitrate);
	mediaHeader->file_pos = fDataStart + data->position;

	TRACE("(%s) position   = %9Ld ", data->raw ? "raw" : "encoded", data->position);
	TRACE("frame      = %9Ld ", PositionToFrame(data->position,data->bitrate,data->fps));
	TRACE("fDataSize  = %9Ld ", fDataSize);
	TRACE("start_time = %9Ld\n", mediaHeader->start_time);

	int64 maxreadsize = data->datasize - data->position;
	int32 readsize = data->buffersize;
	if (maxreadsize < readsize)
		readsize = maxreadsize;
	if (readsize == 0) {
		printf("WavReader::GetNextChunk: LAST BUFFER ERROR at time %9Ld\n",mediaHeader->start_time);
		return B_LAST_BUFFER_ERROR;
	}
			
	if (readsize != Source()->ReadAt(fDataStart + data->position, data->buffer, readsize)) {
		printf("WavReader::GetNextChunk: unexpected read error at position %9Ld\n",fDataStart + data->position);
		return B_ERROR;
	}
	
	// XXX if the stream has more than two channels, we need to reorder channel data here
	
	data->position += readsize;
	*chunkBuffer = data->buffer;
	*chunkSize = readsize;
	return B_OK;
}


Reader *
WavReaderPlugin::NewReader()
{
	return new WavReader;
}


MediaPlugin *instantiate_plugin()
{
	return new WavReaderPlugin;
}
