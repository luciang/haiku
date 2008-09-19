/*
 * Copyright (c) 2004, Marcus Overhagen
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
#ifndef _MATROSKA_READER_H
#define _MATROSKA_READER_H

#include "ReaderPlugin.h"
#include "libMatroskaParser/StreamIO.h"

struct mkv_cookie;

class mkvReader : public Reader
{
public:
				mkvReader();
				~mkvReader();
	
	virtual	const char *Copyright();
	
	virtual	status_t	Sniff(int32 *streamCount);

	virtual	void		GetFileFormatInfo(media_file_format *mff);

	virtual	status_t	AllocateCookie(int32 streamNumber, void **cookie);

	virtual	status_t	FreeCookie(void *cookie);
	
	virtual	status_t	GetStreamInfo(void *cookie, int64 *frameCount, bigtime_t *duration,
							  media_format *format, const void **infoBuffer, size_t *infoSize);

	virtual	status_t	Seek(void *cookie, uint32 flags,
							int64 *frame, bigtime_t *time);

	virtual	status_t	FindKeyFrame(void* cookie, uint32 flags,
							int64* frame, bigtime_t* time);

	virtual	status_t	GetNextChunk(void *cookie,
							 const void **chunkBuffer, size_t *chunkSize,
							 media_header *mediaHeader);
private:
	status_t	SetupVideoCookie(mkv_cookie *cookie);
	status_t	SetupAudioCookie(mkv_cookie *cookie);
	status_t	SetupTextCookie(mkv_cookie *cookie);
	int			CreateFakeAACDecoderConfig(const TrackInfo *track, uint8 **fakeExtraData);

private:
	InputStream *		fInputStream;
	MatroskaFile *		fFile;
	const SegmentInfo *	fFileInfo;
};


class mkvReaderPlugin : public ReaderPlugin
{
public:
	Reader *NewReader();
};

MediaPlugin *instantiate_plugin();

#endif
