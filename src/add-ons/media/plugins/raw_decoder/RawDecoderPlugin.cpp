#include <stdio.h>
#include <string.h>
#include <DataIO.h>
#include "RawDecoderPlugin.h"

#define TRACE_THIS 1
#if TRACE_THIS
  #define TRACE printf
#else
  #define TRACE TRACE(a...)
#endif


status_t
RawDecoder::Setup(media_format *ioEncodedFormat,
				  const void *infoBuffer, int32 infoSize)
{
	if (ioEncodedFormat->type != B_MEDIA_RAW_AUDIO && ioEncodedFormat->type != B_MEDIA_RAW_VIDEO)
		return B_ERROR;
		
	fInputFormat = *ioEncodedFormat;
		
	if (ioEncodedFormat->type == B_MEDIA_RAW_VIDEO)
		fFrameSize = ioEncodedFormat->u.raw_video.display.line_count * ioEncodedFormat->u.raw_video.display.bytes_per_row;
	else
		fFrameSize = (ioEncodedFormat->u.raw_audio.format & 0xf) * ioEncodedFormat->u.raw_audio.channel_count;

	return B_OK;
}


status_t
RawDecoder::NegotiateOutputFormat(media_format *ioDecodedFormat)
{
	// BeBook says: The codec will find and return in ioFormat its best matching format
	// => This means, we never return an error, and always change the format values
	//    that we don't support to something more applicable
	
	char s[1024];

	string_for_format(*ioDecodedFormat, s, sizeof(s));
	printf("RawDecoder::NegotiateOutputFormat enter: %s\n", s);

	*ioDecodedFormat = fInputFormat;

	string_for_format(*ioDecodedFormat, s, sizeof(s));
	printf("RawDecoder::NegotiateOutputFormat leave: %s\n", s);

	return B_OK;
}


status_t
RawDecoder::Seek(uint32 seekTo,
				 int64 seekFrame, int64 *frame,
				 bigtime_t seekTime, bigtime_t *time)
{
	return B_OK;
}


status_t
RawDecoder::Decode(void *buffer, int64 *frameCount,
				   media_header *mediaHeader, media_decode_info *info /* = 0 */)
{
	void *chunkBuffer;
	int32 chunkSize;
	if (B_OK != GetNextChunk(&chunkBuffer, &chunkSize, mediaHeader))
		return B_ERROR;
	
	memcpy(buffer, chunkBuffer, chunkSize);
	*frameCount = chunkSize / fFrameSize;

	return B_OK;
}


Decoder *
RawDecoderPlugin::NewDecoder()
{
	return new RawDecoder;
}

status_t
RawDecoderPlugin::RegisterPlugin()
{
	PublishDecoder("audiocodec/raw", "raw", "RAW audio decoder", "{ WAV : 0x1 }, { QT : 0x20776172, 0x736f7774, 0x74776f73 }");
	PublishDecoder("videocodec/raw", "raw", "RAW video decoder");
	return B_OK;
}

MediaPlugin *instantiate_plugin()
{
	return new RawDecoderPlugin;
}
