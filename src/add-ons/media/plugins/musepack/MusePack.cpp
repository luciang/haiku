/* 
** Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include "MusePack.h"
#include "MusePackReader.h"
#include "MusePackDecoder.h"


Reader *
MusePackPlugin::NewReader()
{
	return new MusePackReader();
}

Decoder *
MusePackPlugin::NewDecoder()
{
	return new MusePackDecoder();
}

status_t 
MusePackPlugin::RegisterPlugin()
{
	PublishDecoder("audiocodec/musepack", "musepack", "musepack decoder");
	return B_OK;
}


//	#pragma mark -


MediaPlugin *
instantiate_plugin()
{
	return new MusePackPlugin();
}

