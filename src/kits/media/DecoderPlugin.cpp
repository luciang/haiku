/* 
** Copyright 2004, Marcus Overhagen. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include "DecoderPlugin.h"

#include <MediaFormats.h>
#include <stdio.h>
#include <string.h>


Decoder::Decoder()
 :	fChunkProvider(NULL)
{
}


Decoder::~Decoder()
{
	delete fChunkProvider;
}

	
status_t
Decoder::GetNextChunk(const void **chunkBuffer, size_t *chunkSize,
					  media_header *mediaHeader)
{
	return fChunkProvider->GetNextChunk(chunkBuffer, chunkSize, mediaHeader);
}


void
Decoder::SetChunkProvider(ChunkProvider *provider)
{
	delete fChunkProvider;
	fChunkProvider = provider;
}


//	#pragma mark -


DecoderPlugin::DecoderPlugin()
{
}
