/*
 * Copyright 2004-2008, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Marcus Overhagen
 *		Axel Dörfler
 */


#include "AddOnManager.h"

#include <stdio.h>
#include <string.h>

#include <FindDirectory.h>
#include <Path.h>
#include <Entry.h>
#include <Directory.h>
#include <Autolock.h>
#include <image.h>

#include <safemode.h>
#include <syscalls.h>

#include "FormatManager.h"
#include "MetaFormat.h"
#include "media_server.h"
#include "debug.h"


//	#pragma mark ImageLoader

/*!	The ImageLoader class is a convenience class to temporarily load
	an image file, and unload it on deconstruction automatically.
*/
class ImageLoader {
	public:
		ImageLoader(BPath &path)
		{
			fImage = load_add_on(path.Path());
		}

		~ImageLoader()
		{
			if (fImage >= B_OK)
				unload_add_on(fImage);
		}

		status_t InitCheck() const { return fImage; }
		image_id Image() const { return fImage; }

	private:
		image_id	fImage;
};


//	#pragma mark -


AddOnManager::AddOnManager()
	:
 	fLock("add-on manager")
{
}


AddOnManager::~AddOnManager()
{
}


void
AddOnManager::LoadState()
{
	RegisterAddOns();
}


void
AddOnManager::SaveState()
{
}


status_t
AddOnManager::GetDecoderForFormat(xfer_entry_ref *_decoderRef,
	const media_format &format)
{
	if ((format.type == B_MEDIA_ENCODED_VIDEO
			|| format.type == B_MEDIA_ENCODED_AUDIO
			|| format.type == B_MEDIA_MULTISTREAM)
		&& format.Encoding() == 0)
		return B_MEDIA_BAD_FORMAT;
	if (format.type == B_MEDIA_NO_TYPE || format.type == B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;

	BAutolock locker(fLock);

	printf("AddOnManager::GetDecoderForFormat: searching decoder for encoding "
		"%ld\n", format.Encoding());

	decoder_info *info;
	for (fDecoderList.Rewind(); fDecoderList.GetNext(&info);) {
		media_format *decoderFormat;
		for (info->formats.Rewind(); info->formats.GetNext(&decoderFormat);) {
			// check if the decoder matches the supplied format
			if (!decoderFormat->Matches(&format))
				continue;

			printf("AddOnManager::GetDecoderForFormat: found decoder %s for "
				"encoding %ld\n", info->ref.name, decoderFormat->Encoding());

			*_decoderRef = info->ref;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;	
}
									

status_t
AddOnManager::GetReaders(xfer_entry_ref *outRefs, int32 *outCount,
	int32 maxCount)
{
	BAutolock locker(fLock);

	fReaderList.Rewind();
	reader_info *info;
	for (*outCount = 0; fReaderList.GetNext(&info) && *outCount < maxCount;
			(*outCount)++) {
		outRefs[*outCount] = info->ref;
	}

	return B_OK;
}


status_t
AddOnManager::RegisterAddOn(BEntry &entry)
{
	BPath path(&entry);

	entry_ref ref;
	status_t status = entry.GetRef(&ref);
	if (status < B_OK)
		return status;

	printf("AddOnManager::RegisterAddOn(): trying to load \"%s\"\n",
		path.Path());

	ImageLoader loader(path);
	if ((status = loader.InitCheck()) < B_OK)
		return status;

	MediaPlugin *(*instantiate_plugin_func)();

	if (get_image_symbol(loader.Image(), "instantiate_plugin",
			B_SYMBOL_TYPE_TEXT, (void **)&instantiate_plugin_func) < B_OK) {
		printf("AddOnManager::RegisterAddOn(): can't find instantiate_plugin "
			"in \"%s\"\n", path.Path());
		return B_BAD_TYPE;
	}

	MediaPlugin *plugin = (*instantiate_plugin_func)();
	if (plugin == NULL) {
		printf("AddOnManager::RegisterAddOn(): instantiate_plugin in \"%s\" "
			"returned NULL\n", path.Path());
		return B_ERROR;
	}

	// ToDo: remove any old formats describing this add-on!!

	ReaderPlugin *reader = dynamic_cast<ReaderPlugin *>(plugin);
	if (reader != NULL)
		RegisterReader(reader, ref);

	DecoderPlugin *decoder = dynamic_cast<DecoderPlugin *>(plugin);
	if (decoder != NULL)
		RegisterDecoder(decoder, ref);

	delete plugin;
	
	return B_OK;
}


void
AddOnManager::RegisterAddOns()
{
	class CodecHandler : public AddOnMonitorHandler {
	private:
		AddOnManager * fManager;

	public:
		CodecHandler(AddOnManager *manager)
		{
			fManager = manager;
		}

		virtual void AddOnCreated(const add_on_entry_info *entryInfo)
		{
		}

		virtual void AddOnEnabled(const add_on_entry_info *entryInfo)
		{
			entry_ref ref;
			make_entry_ref(entryInfo->dir_nref.device,
				entryInfo->dir_nref.node, entryInfo->name, &ref);
			BEntry entry(&ref, false);
			if (entry.InitCheck() == B_OK)
				fManager->RegisterAddOn(entry);
		}

		virtual void AddOnDisabled(const add_on_entry_info *entryInfo)
		{
		}

		virtual void AddOnRemoved(const add_on_entry_info *entryInfo)
		{
		}
	};

	const directory_which directories[] = {
		B_USER_ADDONS_DIRECTORY,
		B_COMMON_ADDONS_DIRECTORY,
		B_BEOS_ADDONS_DIRECTORY,
	};

	fHandler = new CodecHandler(this);
	fAddOnMonitor = new AddOnMonitor(fHandler);

	// get safemode option for disabling user add-ons

	char buffer[16];
	size_t size = sizeof(buffer);

	bool disableUserAddOns = _kern_get_safemode_option(B_SAFEMODE_SAFE_MODE,
			buffer, &size) == B_OK
		&& (!strcasecmp(buffer, "true")
			|| !strcasecmp(buffer, "yes")
			|| !strcasecmp(buffer, "on")
			|| !strcasecmp(buffer, "enabled")
			|| !strcmp(buffer, "1"));

	node_ref nref;
	BDirectory directory;
	BPath path;
	for (uint i = 0 ; i < sizeof(directories) / sizeof(directory_which) ; i++) {
		if (disableUserAddOns && i <= 1)
			continue;

		if (find_directory(directories[i], &path) == B_OK
			&& path.Append("media/plugins") == B_OK
			&& directory.SetTo(path.Path()) == B_OK 
			&& directory.GetNodeRef(&nref) == B_OK) {
			fHandler->AddDirectory(&nref);
		}
	}
}


void
AddOnManager::RegisterReader(ReaderPlugin *reader, const entry_ref &ref)
{
	BAutolock locker(fLock);

	reader_info *pinfo;
	for (fReaderList.Rewind(); fReaderList.GetNext(&pinfo);) {
		if (!strcmp(pinfo->ref.name, ref.name)) {
			// we already know this reader
			return;
		}
	}

	printf("AddOnManager::RegisterReader, name %s\n", ref.name);

	reader_info info;
	info.ref = ref;

	fReaderList.Insert(info);
}


void
AddOnManager::RegisterDecoder(DecoderPlugin *plugin, const entry_ref &ref)
{
	BAutolock locker(fLock);

	decoder_info *pinfo;
	for (fDecoderList.Rewind(); fDecoderList.GetNext(&pinfo);) {
		if (!strcmp(pinfo->ref.name, ref.name)) {
			// we already know this decoder
			return;
		}
	}

	printf("AddOnManager::RegisterDecoder, name %s\n", ref.name);

	decoder_info info;
	info.ref = ref;

	media_format * formats = 0;
	size_t count = 0;
	if (plugin->GetSupportedFormats(&formats,&count) != B_OK) {
		printf("AddOnManager::RegisterDecoder(): plugin->GetSupportedFormats"
			"(...) failed!\n");
		return;
	}
	for (uint i = 0 ; i < count ; i++) {
		info.formats.Insert(formats[i]);
	}
	fDecoderList.Insert(info);
}


