/*
 * Copyright 2004-2009, The Haiku Project. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Axel Dörfler
 *		Marcus Overhagen
 */


#include "DataExchange.h"
#include "MetaFormat.h"
#include "debug.h"

#include <MediaFormats.h>
#include <ObjectList.h>
#include <Message.h>
#include <Autolock.h>

#include <string.h>

using namespace BPrivate::media;


static BLocker sLock;
static BObjectList<meta_format> sFormats;
static bigtime_t sLastFormatsUpdate;


status_t
get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, media_format* _outputFormat,
	media_codec_info* _codecInfo)
{
	// TODO: If fileFormat is provided (existing apps also pass NULL),
	// we could at least check fileFormat->capabilities against
	// outputFormat->type without even contacting the server.

	if (cookie == NULL || inputFormat == NULL || _codecInfo == NULL)
		return B_BAD_VALUE;

	while (true) {
		server_get_codec_info_request request;
		request.cookie = *cookie;
		server_get_codec_info_reply reply;
		status_t ret = QueryServer(SERVER_GET_CODEC_INFO_FOR_COOKIE, &request,
			sizeof(request), &reply, sizeof(reply));
		if (ret != B_OK)
			return ret;

		*cookie = *cookie + 1;

		if (fileFormat != NULL && reply.format_family != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != reply.format_family) {
			continue;
		}

		if (!reply.input_format.Matches(inputFormat))
			continue;

		if (_outputFormat != NULL)
			*_outputFormat = reply.output_format;

		*_codecInfo = reply.codec_info;
		break;
	}

	return B_OK;
}


status_t
get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, const media_format* outputFormat,
	media_codec_info* _codecInfo, media_format* _acceptedInputFormat,
	media_format* _acceptedOutputFormat)
{
	// TODO: If fileFormat is provided (existing apps also pass NULL),
	// we could at least check fileFormat->capabilities against
	// outputFormat->type without even contacting the server.

	if (cookie == NULL || inputFormat == NULL || outputFormat == NULL
		|| _codecInfo == NULL) {
		return B_BAD_VALUE;
	}

	while (true) {
		server_get_codec_info_request request;
		request.cookie = *cookie;
		server_get_codec_info_reply reply;
		status_t ret = QueryServer(SERVER_GET_CODEC_INFO_FOR_COOKIE, &request,
			sizeof(request), &reply, sizeof(reply));
		if (ret != B_OK)
			return ret;

		*cookie = *cookie + 1;

		if (fileFormat != NULL && reply.format_family != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != reply.format_family) {
			continue;
		}

		if (!reply.input_format.Matches(inputFormat)
			|| !reply.output_format.Matches(outputFormat)) {
			continue;
		}

		// TODO: These formats are currently way too generic. For example,
		// an encoder may want to adjust video width to a multiple of 16,
		// or overwrite the intput and or output color space. To make this
		// possible, we actually have to instantiate an Encoder here and
		// ask it to specifiy the format.
		if (_acceptedInputFormat != NULL)
			*_acceptedInputFormat = reply.input_format;
		if (_acceptedOutputFormat != NULL)
			*_acceptedOutputFormat = reply.output_format;

		*_codecInfo = reply.codec_info;
		break;
	}

	return B_OK;
}


status_t
get_next_encoder(int32* cookie, media_codec_info* _codecInfo)
{
	if (cookie == NULL || _codecInfo == NULL)
		return B_BAD_VALUE;

	server_get_codec_info_request request;
	request.cookie = *cookie;
	server_get_codec_info_reply reply;
	status_t ret = QueryServer(SERVER_GET_CODEC_INFO_FOR_COOKIE, &request,
		sizeof(request), &reply, sizeof(reply));
	if (ret != B_OK)
		return ret;

	*cookie = *cookie + 1;
	*_codecInfo = reply.codec_info;

	return B_OK;
}


bool
does_file_accept_format(const media_file_format* _fileFormat,
	media_format* format, uint32 flags)
{
	UNIMPLEMENTED();
	return false;
}


//	#pragma mark -


_media_format_description::_media_format_description()
{
	memset(this, 0, sizeof(*this));
}


_media_format_description::~_media_format_description()
{
}


_media_format_description::_media_format_description(
	const _media_format_description& other)
{
	memcpy(this, &other, sizeof(*this));
}


_media_format_description& 
_media_format_description::operator=(const _media_format_description& other)
{
	memcpy(this, &other, sizeof(*this));
	return *this;
}


bool
operator==(const media_format_description& a,
	const media_format_description& b)
{
	if (a.family != b.family)
		return false;

	switch (a.family) {
		case B_BEOS_FORMAT_FAMILY:
			return a.u.beos.format == b.u.beos.format;
		case B_QUICKTIME_FORMAT_FAMILY:
			return a.u.quicktime.codec == b.u.quicktime.codec
				&& a.u.quicktime.vendor == b.u.quicktime.vendor;
		case B_AVI_FORMAT_FAMILY:
			return a.u.avi.codec == b.u.avi.codec;
		case B_ASF_FORMAT_FAMILY:
			return a.u.asf.guid == b.u.asf.guid;
		case B_MPEG_FORMAT_FAMILY:
			return a.u.mpeg.id == b.u.mpeg.id;
		case B_WAV_FORMAT_FAMILY:
			return a.u.wav.codec == b.u.wav.codec;
		case B_AIFF_FORMAT_FAMILY:
			return a.u.aiff.codec == b.u.aiff.codec;
		case B_AVR_FORMAT_FAMILY:
			return a.u.avr.id == b.u.avr.id;
		case B_MISC_FORMAT_FAMILY:
			return a.u.misc.file_format == b.u.misc.file_format
				&& a.u.misc.codec == b.u.misc.codec;

		default:
			return false;
	}
}


bool
operator<(const media_format_description& a, const media_format_description& b)
{
	if (a.family != b.family)
		return a.family < b.family;

	switch (a.family) {
		case B_BEOS_FORMAT_FAMILY:
			return a.u.beos.format < b.u.beos.format;
		case B_QUICKTIME_FORMAT_FAMILY:
			if (a.u.quicktime.vendor == b.u.quicktime.vendor)
				return a.u.quicktime.codec < b.u.quicktime.codec;
			return a.u.quicktime.vendor < b.u.quicktime.vendor;
		case B_AVI_FORMAT_FAMILY:
			return a.u.avi.codec < b.u.avi.codec;
		case B_ASF_FORMAT_FAMILY:
			return a.u.asf.guid < b.u.asf.guid;
		case B_MPEG_FORMAT_FAMILY:
			return a.u.mpeg.id < b.u.mpeg.id;
		case B_WAV_FORMAT_FAMILY:
			return a.u.wav.codec < b.u.wav.codec;
		case B_AIFF_FORMAT_FAMILY:
			return a.u.aiff.codec < b.u.aiff.codec;
		case B_AVR_FORMAT_FAMILY:
			return a.u.avr.id < b.u.avr.id;
		case B_MISC_FORMAT_FAMILY:
			if (a.u.misc.file_format == b.u.misc.file_format)
				return a.u.misc.codec < b.u.misc.codec;
			return a.u.misc.file_format < b.u.misc.file_format;

		default:
			return true;
	}
}


bool
operator==(const GUID& a, const GUID& b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}


bool
operator<(const GUID& a, const GUID& b)
{
	return memcmp(&a, &b, sizeof(a)) < 0;
}


//	#pragma mark -
//
//	Some (meta) formats supply functions


meta_format::meta_format()
	:
	id(0)
{
}



meta_format::meta_format(const media_format_description& description,
	const media_format& format, int32 id)
	:
	description(description),
	format(format),
	id(id)
{
}


meta_format::meta_format(const media_format_description& description)
	:
	description(description),
	id(0)
{
}


meta_format::meta_format(const meta_format& other)
	:
	description(other.description),
	format(other.format)
{
}


bool 
meta_format::Matches(const media_format& otherFormat,
	media_format_family family)
{
	if (family != description.family)
		return false;

	return format.Matches(&otherFormat);
}


int 
meta_format::CompareDescriptions(const meta_format* a, const meta_format* b)
{
	if (a->description == b->description)
		return 0;

	if (a->description < b->description)
		return -1;

	return 1;
}


int 
meta_format::Compare(const meta_format* a, const meta_format* b)
{
	int compare = CompareDescriptions(a, b);
	if (compare != 0)
		return compare;

	return a->id - b->id;
}


/** We share one global list for all BMediaFormats in the team - since the
 *	format data can change at any time, we have to ask the server to update
 *	the list to ensure that we are working on the latest data set.
 *	The list we get from the server is always sorted by description.
 *	The formats lock has to be hold when you call this function.
 */

static status_t
update_media_formats()
{
	if (!sLock.IsLocked())
		return B_NOT_ALLOWED;

	BMessage request(MEDIA_SERVER_GET_FORMATS);
	request.AddInt64("last_timestamp", sLastFormatsUpdate);
	
	BMessage reply;
	status_t status = QueryServer(request, reply);
	if (status < B_OK) {
		ERROR("BMediaFormats: Could not update formats: %s\n",
			strerror(status));
		return status;
	}

	// do we need an update at all?
	bool needUpdate;
	if (reply.FindBool("need_update", &needUpdate) < B_OK)
		return B_ERROR;
	if (!needUpdate)
		return B_OK;

	// update timestamp and check if the message is okay
	type_code code;
	int32 count;
	if (reply.FindInt64("timestamp", &sLastFormatsUpdate) < B_OK
		|| reply.GetInfo("formats", &code, &count) < B_OK)
		return B_ERROR;

	// overwrite already existing formats

	int32 index = 0;
	for (; index < sFormats.CountItems() && index < count; index++) {
		meta_format* item = sFormats.ItemAt(index);

		const meta_format* newItem;
		ssize_t size;
		if (reply.FindData("formats", MEDIA_META_FORMAT_TYPE, index,
				(const void**)&newItem, &size) == B_OK)
			*item = *newItem;
	}

	// allocate additional formats

	for (; index < count; index++) {
		const meta_format* newItem;
		ssize_t size;
		if (reply.FindData("formats", MEDIA_META_FORMAT_TYPE, index,
				(const void**)&newItem, &size) == B_OK)
			sFormats.AddItem(new meta_format(*newItem));
	}

	// remove no longer used formats

	while (count < sFormats.CountItems())
		delete sFormats.RemoveItemAt(count);

	return B_OK;
}


//	#pragma mark -


BMediaFormats::BMediaFormats()
	:
	fIteratorIndex(0)	
{
}


BMediaFormats::~BMediaFormats()
{
}


status_t 
BMediaFormats::InitCheck()
{
	return sLock.Sem() >= B_OK ? B_OK : sLock.Sem();
}


status_t 
BMediaFormats::GetCodeFor(const media_format& format,
	media_format_family family,
	media_format_description* _description)
{
	BAutolock locker(sLock);

	status_t status = update_media_formats();
	if (status < B_OK)
		return status;

	// search for a matching format

	for (int32 index = sFormats.CountItems(); index-- > 0;) {
		meta_format* metaFormat = sFormats.ItemAt(index);

		if (metaFormat->Matches(format, family)) {
			*_description = metaFormat->description;
			return B_OK;
		}
	}

	return B_MEDIA_BAD_FORMAT;
}


status_t 
BMediaFormats::GetFormatFor(const media_format_description& description,
	media_format* _format)
{
	BAutolock locker(sLock);

	status_t status = update_media_formats();
	if (status < B_OK) {
		ERROR("BMediaFormats: updating formats from server failed: %s!\n",
			strerror(status));
		return status;
	}
	TRACE("search for description family = %d, a = 0x%lx, b = 0x%lx\n",
		description.family, description.u.misc.file_format,
		description.u.misc.codec);

	// search for a matching format description

	meta_format other(description);
	const meta_format* metaFormat = sFormats.BinarySearch(other,
		meta_format::CompareDescriptions);
	TRACE("meta format == %p\n", metaFormat);
	if (metaFormat == NULL) {
		memset(_format, 0, sizeof(*_format)); // clear to widlcard
		return B_MEDIA_BAD_FORMAT;
	}

	// found it!
	*_format = metaFormat->format;
	return B_OK;
}


status_t 
BMediaFormats::GetBeOSFormatFor(uint32 format, 
	media_format* _format, media_type type)
{
	BMediaFormats formats;

	media_format_description description;
	description.family = B_BEOS_FORMAT_FAMILY;
	description.u.beos.format = format;

	status_t status = formats.GetFormatFor(description, _format);
	if (status < B_OK)
		return status;

	if (type != B_MEDIA_UNKNOWN_TYPE && type != _format->type)
		return B_BAD_TYPE;

	return B_OK;
}


status_t 
BMediaFormats::GetAVIFormatFor(uint32 codec,
	media_format* _format, media_type type)
{
	UNIMPLEMENTED();
	BMediaFormats formats;

	media_format_description description;
	description.family = B_AVI_FORMAT_FAMILY;
	description.u.avi.codec = codec;

	status_t status = formats.GetFormatFor(description, _format);
	if (status < B_OK)
		return status;

	if (type != B_MEDIA_UNKNOWN_TYPE && type != _format->type)
		return B_BAD_TYPE;

	return B_OK;
}


status_t 
BMediaFormats::GetQuicktimeFormatFor(uint32 vendor, uint32 codec, 
	media_format* _format, media_type type)
{
	BMediaFormats formats;

	media_format_description description;
	description.family = B_QUICKTIME_FORMAT_FAMILY;
	description.u.quicktime.vendor = vendor;
	description.u.quicktime.codec = codec;

	status_t status = formats.GetFormatFor(description, _format);
	if (status < B_OK)
		return status;

	if (type != B_MEDIA_UNKNOWN_TYPE && type != _format->type)
		return B_BAD_TYPE;

	return B_OK;
}


status_t 
BMediaFormats::RewindFormats()
{
	if (!sLock.IsLocked() || sLock.LockingThread() != find_thread(NULL)) {
		// TODO: Shouldn't we simply drop into the debugger in this case?
		return B_NOT_ALLOWED;
	}

	fIteratorIndex = 0;
	return B_OK;
}


status_t 
BMediaFormats::GetNextFormat(media_format* _format,
	media_format_description* _description)
{
	if (!sLock.IsLocked() || sLock.LockingThread() != find_thread(NULL)) {
		// TODO: Shouldn't we simply drop into the debugger in this case?
		return B_NOT_ALLOWED;
	}

	if (fIteratorIndex == 0) {
		// This is the first call, so let's make sure we have current data to
		// operate on.
		status_t status = update_media_formats();
		if (status < B_OK)
			return status;
	}

	meta_format* format = sFormats.ItemAt(fIteratorIndex++);
	if (format == NULL)
		return B_BAD_INDEX;

	return B_OK;
}


bool
BMediaFormats::Lock()
{
	return sLock.Lock();
}


void 
BMediaFormats::Unlock()
{
	sLock.Unlock();
}


status_t 
BMediaFormats::MakeFormatFor(const media_format_description* descriptions,
	int32 descriptionCount, media_format* format, uint32 flags,
	void* _reserved)
{
	BMessage request(MEDIA_SERVER_MAKE_FORMAT_FOR);
	for (int32 i = 0 ; i < descriptionCount ; i++) {
		request.AddData("description", B_RAW_TYPE, &descriptions[i],
			sizeof(descriptions[i]));
	}
	request.AddData("format", B_RAW_TYPE, format, sizeof(*format));
	request.AddData("flags", B_UINT32_TYPE, &flags, sizeof(flags));
	request.AddPointer("_reserved", _reserved);
	
	BMessage reply;
	status_t status = QueryServer(request, reply);
	if (status != B_OK) {
		ERROR("BMediaFormats: Could not make a format: %s\n", strerror(status));
		return status;
	}

	// check the status
	if (reply.FindInt32("result", &status) < B_OK)
		return B_ERROR;
	if (status != B_OK)
		return status;

	// get the format
	const void* data;
	ssize_t size;
	if (reply.FindData("format", B_RAW_TYPE, 0, &data, &size) != B_OK)
		return B_ERROR;
	if (size != sizeof(*format))
		return B_ERROR;

	// copy the BMessage's data into our format
	*format = *(media_format*)data;

	return B_OK;
}


// #pragma mark - deprecated API


status_t 
BMediaFormats::MakeFormatFor(const media_format_description& description,
	const media_format& inFormat, media_format* _outFormat)
{
	*_outFormat = inFormat;
	return MakeFormatFor(&description, 1, _outFormat);
}

