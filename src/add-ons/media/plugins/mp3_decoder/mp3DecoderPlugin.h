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
#ifndef _MP3_DECODER_PLUGIN_H
#define _MP3_DECODER_PLUGIN_H
 
#include "DecoderPlugin.h"
#include "mpglib/mpglib.h"

class mp3Decoder : public Decoder
{
public:
				mp3Decoder();
				~mp3Decoder();
	
	void		GetCodecInfo(media_codec_info *codecInfo);

	status_t	Setup(media_format *ioEncodedFormat,
					  const void *infoBuffer, int32 infoSize);

	status_t	NegotiateOutputFormat(media_format *ioDecodedFormat);

	status_t	Seek(uint32 seekTo,
					 int64 seekFrame, int64 *frame,
					 bigtime_t seekTime, bigtime_t *time);

							 
	status_t	Decode(void *buffer, int64 *frameCount,
					   media_header *mediaHeader, media_decode_info *info);
					   
private:
	status_t	DecodeNextChunk();
	bool		IsValidStream(uint8 *buffer, int size);
	int			GetFrameLength(void *header);	
	
private:
	struct mpstr	fMpgLibPrivate;
	int32			fResidualBytes;
	uint8 *			fResidualBuffer;
	uint8 *			fDecodeBuffer;
	bigtime_t		fStartTime;
	int				fFrameSize;
	int				fFrameRate;
	int				fBitRate;
	int				fChannelCount;
	int				fOutputBufferSize;
	bool			fNeedSync;
	bool			fDecodingError;
};


class mp3DecoderPlugin : public DecoderPlugin
{
public:
	Decoder *	NewDecoder(uint index);
	status_t	GetSupportedFormats(media_format ** formats, size_t * count);
};

#endif
