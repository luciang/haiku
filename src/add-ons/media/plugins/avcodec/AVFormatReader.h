/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the GNU L-GPL license.
 */
#ifndef AV_FORMAT_READER_H
#define AV_FORMAT_READER_H


#include "ReaderPlugin.h"


struct AVFormatContext;


class AVFormatReader : public Reader {
public:
								AVFormatReader();
								~AVFormatReader();
	
	virtual	const char*			Copyright();
	
	virtual	status_t			Sniff(int32* streamCount);

	virtual	void				GetFileFormatInfo(media_file_format* mff);

	virtual	status_t			AllocateCookie(int32 streamNumber,
									void** cookie);
	virtual	status_t			FreeCookie(void* cookie);
	
	virtual	status_t			GetStreamInfo(void* cookie, int64* frameCount,
									bigtime_t* duration, media_format* format,
									const void** infoBuffer,
									size_t* infoSize);

	virtual	status_t			Seek(void* cookie, uint32 flags, int64* frame,
									bigtime_t* time);
	virtual	status_t			FindKeyFrame(void* cookie, uint32 flags,
									int64* frame, bigtime_t* time);

	virtual	status_t			GetNextChunk(void* cookie,
									const void** chunkBuffer, size_t* chunkSize,
									media_header* mediaHeader);

private:
			AVFormatContext*	fContext;
};


class AVFormatPlugin : public ReaderPlugin {
public:
			Reader*				NewReader();
};

MediaPlugin *instantiate_plugin();

#endif // AV_FORMAT_READER_H
