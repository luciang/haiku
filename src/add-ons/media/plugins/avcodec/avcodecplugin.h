/*
 * Copyright (C) 2001 Carlos Hasan. All Rights Reserved.
 * Copyright (C) 2001 François Revol. All Rights Reserved.
 * Copyright (C) 2001 Axel Dörfler. All Rights Reserved.
 * Copyright (C) 2004 Marcus Overhagen. All Rights Reserved.
 *
 * Distributed under the terms of the MIT License.
 */

//! libavcodec based decoder for Haiku

#ifndef __AVCODEC_PLUGIN_H__
#define __AVCODEC_PLUGIN_H__

#include <MediaFormats.h>
#include "ReaderPlugin.h"
#include "DecoderPlugin.h"

//#define DO_PROFILING

#include "gfx_util.h"

struct codec_table {
	CodecID					id;
	media_type				type;
	media_format_family		family;
	uint64					fourcc;
	const char*				prettyname;
};

extern const struct codec_table gCodecTable[];
extern const int num_codecs;
extern media_format avcodec_formats[];

class avCodec : public Decoder {
public:
								avCodec();
		
	virtual						~avCodec();
		
	virtual	void				GetCodecInfo(media_codec_info* mci);
	
	virtual	status_t			Setup(media_format* ioEncodedFormat,
								   const void* infoBuffer, size_t infoSize);
   
	virtual	status_t			NegotiateOutputFormat(
									media_format* outputFormat);
	
	virtual	status_t			Decode(void* outBuffer, int64* outFrameCount,
						    		media_header* mediaHeader,
						    		media_decode_info* info);

	virtual	status_t			Seek(uint32 seekTo, int64 seekFrame,
									int64* frame, bigtime_t seekTime,
									bigtime_t* time);
	
	
protected:
			media_header		fHeader;
			media_decode_info	fInfo;
	
	friend class avCodecInputStream;
		
private:
			media_format		fInputFormat;
			media_raw_video_format fOutputVideoFormat;

			int64				fFrame;
			bool				isAudio;
	
			int					ffcodec_index_in_table;
									// helps to find codecpretty
		
			// ffmpeg related datas
			AVCodec*			fCodec;
			AVCodecContext*		ffc;
			AVFrame*			ffpicture;
			AVFrame*			opicture;
		
			bool 				fCodecInitDone;

			gfx_convert_func	conv_func; // colorspace convert func

			char*				fExtraData;
			int					fExtraDataSize;
			int					fBlockAlign;
		
			bigtime_t			fStartTime;
			int32				fOutputFrameCount;
			float				fOutputFrameRate;
			int					fOutputFrameSize; // sample size * channel count
		
			const void*			fChunkBuffer;
			int32				fChunkBufferOffset;
			size_t				fChunkBufferSize;

			char*				fOutputBuffer;
			int32				fOutputBufferOffset;
			int32				fOutputBufferSize;

};

class avCodecPlugin : public DecoderPlugin {
public:
			Decoder*			NewDecoder(uint index);
			status_t			GetSupportedFormats(media_format** formats,
									size_t* count);
};


#endif
