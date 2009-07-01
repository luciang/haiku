/*
 * Copyright (C) 2001 Carlos Hasan. All Rights Reserved.
 * Copyright (C) 2001 François Revol. All Rights Reserved.
 * Copyright (C) 2001 Axel Dörfler. All Rights Reserved.
 * Copyright (C) 2004 Marcus Overhagen. All Rights Reserved.
 *
 * Distributed under the terms of the MIT License.
 */

//! libavcodec based decoder for Haiku

#ifndef FFMPEG_PLUGIN_H
#define FFMPEG_PLUGIN_H

#include <MediaFormats.h>

#include "DecoderPlugin.h"
#include "ReaderPlugin.h"

class FFmpegPlugin : public ReaderPlugin, public DecoderPlugin {
public:
	virtual	Reader*				NewReader();

	virtual	Decoder*			NewDecoder(uint index);
	virtual	status_t			GetSupportedFormats(media_format** formats,
									size_t* count);
};


#endif // FFMPEG_PLUGIN_H
