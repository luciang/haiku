/* 
** Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/
#ifndef MUSEPACK_PLUGIN_H
#define MUSEPACK_PLUGIN_H


#include "ReaderPlugin.h"
#include "DecoderPlugin.h"


class MusePackPlugin : public ReaderPlugin, public DecoderPlugin {
	public:
		Reader *NewReader();

		Decoder *NewDecoder();
		status_t RegisterDecoder();
};

#endif	/* MUSEPACK_PLUGIN_H */
