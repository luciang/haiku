/*
 * Copyright 2006-2007, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


#include "IconUtils.h"

#include <new>
#include <fs_attr.h>
#include <stdio.h>
#include <string.h>

#include <Bitmap.h>
#include <Node.h>
#include <TypeConstants.h>

#include "Icon.h"
#include "IconRenderer.h"
#include "FlatIconImporter.h"

#ifndef HAIKU_TARGET_PLATFORM_HAIKU
#	define B_MINI_ICON_TYPE		'MICN'
#	define B_LARGE_ICON_TYPE	'ICON'
#endif

using namespace BPrivate::Icon;
using std::nothrow;


// GetIcon
status_t
BIconUtils::GetIcon(BNode* node,
					const char* vectorIconAttrName,
					const char* smallIconAttrName,
					const char* largeIconAttrName,
					icon_size size,
					BBitmap* result)
{
	if (!result || result->InitCheck())
		return B_BAD_VALUE;

	status_t ret = B_ERROR;

	switch (result->ColorSpace()) {
		case B_RGBA32:
		case B_RGB32:
			// prefer vector icon
			ret = GetVectorIcon(node, vectorIconAttrName, result);
			if (ret < B_OK) {
				// try to fallback to B_CMAP8 icons
				// (converting to B_RGBA32 is handled)

				// override size
				if (result->Bounds().IntegerWidth() + 1 >= 32)
					size = B_LARGE_ICON;
				else
					size = B_MINI_ICON;

				ret = GetCMAP8Icon(node,
								   smallIconAttrName,
								   largeIconAttrName,
								   size, result);
			}
			break;

		case B_CMAP8:
			// prefer old B_CMAP8 icons
			ret = GetCMAP8Icon(node,
							   smallIconAttrName,
							   largeIconAttrName,
							   size, result);
			if (ret < B_OK) {
				// try to fallback to vector icon
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
				BBitmap temp(result->Bounds(),
							 B_BITMAP_NO_SERVER_LINK, B_RGBA32);
#else
				BBitmap temp(result->Bounds(), B_RGBA32);
#endif
				ret = temp.InitCheck();
				if (ret < B_OK)
					break;
				ret = GetVectorIcon(node, vectorIconAttrName, &temp);
				if (ret < B_OK)
					break;
				uint32 width = temp.Bounds().IntegerWidth() + 1;
				uint32 height = temp.Bounds().IntegerHeight() + 1;
				uint32 bytesPerRow = temp.BytesPerRow();
				ret = ConvertToCMAP8((uint8*)temp.Bits(),
									 width, height, bytesPerRow, result);
			}
			break;
		default:
			printf("BIconUtils::GetIcon() - unsupported colorspace\n");
			break;
	}

	return ret;
}

// #pragma mark -

// GetVectorIcon
status_t
BIconUtils::GetVectorIcon(BNode* node, const char* attrName,
						  BBitmap* result)
{
	if (!node || node->InitCheck() < B_OK || !attrName)
		return B_BAD_VALUE;

#if TIME_VECTOR_ICONS
bigtime_t startTime = system_time();
#endif

	// get the attribute info and check type and size of the attr contents
	attr_info attrInfo;
	status_t ret = node->GetAttrInfo(attrName, &attrInfo);
	if (ret < B_OK)
		return ret;

	type_code attrType = B_VECTOR_ICON_TYPE;

	if (attrInfo.type != attrType)
		return B_BAD_TYPE;

	// chicken out on unrealisticly large attributes
	if (attrInfo.size > 16 * 1024)
		return B_BAD_VALUE;

	uint8 buffer[attrInfo.size];
	ssize_t read = node->ReadAttr(attrName, attrType, 0, buffer, attrInfo.size);
	if (read != attrInfo.size)
		return B_ERROR;

#if TIME_VECTOR_ICONS
bigtime_t importTime = system_time();
#endif

	ret = GetVectorIcon(buffer, attrInfo.size, result);
	if (ret < B_OK)
		return ret;

#if TIME_VECTOR_ICONS
bigtime_t finishTime = system_time();
printf("read: %lld, import: %lld\n", importTime - startTime, finishTime - importTime);
#endif

	return B_OK;
}

// GetVectorIcon
status_t
BIconUtils::GetVectorIcon(const uint8* buffer, size_t size,
						  BBitmap* result)
{
	if (!result)
		return B_BAD_VALUE;

	status_t ret = result->InitCheck();
	if (ret < B_OK)
		return ret;

	if (result->ColorSpace() != B_RGBA32 && result->ColorSpace() != B_RGB32)
		return B_BAD_VALUE;

	Icon icon;
	ret = icon.InitCheck();
	if (ret < B_OK)
		return ret;

	FlatIconImporter importer;
	ret = importer.Import(&icon, const_cast<uint8*>(buffer), size);
	if (ret < B_OK)
		return ret;

	IconRenderer renderer(result);
	renderer.SetIcon(&icon);
	renderer.SetScale((result->Bounds().Width() + 1.0) / 64.0);
	renderer.Render();

	// TODO: would be nice to get rid of this
	// (B_RGBA32_PREMULTIPLIED or better yet, new blending_mode)
	// NOTE: probably not necessary only because
	// transparent colors are "black" in all existing icons
	// lighter transparent colors should be too dark if
	// app_server uses correct blending
//	renderer.Demultiply();

	return B_OK;
}

// #pragma mark -

status_t
BIconUtils::GetCMAP8Icon(BNode* node,
						 const char* smallIconAttrName,
						 const char* largeIconAttrName,
						 icon_size size,
						 BBitmap* icon)
{
	// check parameters and initialization
	if (!icon || icon->InitCheck() != B_OK
		|| !node || node->InitCheck() != B_OK
		|| !smallIconAttrName || !largeIconAttrName)
		return B_BAD_VALUE;

	status_t ret = B_OK;

	// NOTE: this might be changed if other icon
	// sizes are supported in B_CMAP8 attributes,
	// but this is currently not the case, so we
	// relax the requirement to pass an icon
	// of just the right size
	if (size < B_LARGE_ICON)
		size = B_MINI_ICON;
	else
		size = B_LARGE_ICON;

	// set some icon size related variables
	const char *attribute = NULL;
	BRect bounds;
	uint32 attrType = 0;
	size_t attrSize = 0;
	switch (size) {
		case B_MINI_ICON:
			attribute = smallIconAttrName;
			bounds.Set(0, 0, 15, 15);
			attrType = B_MINI_ICON_TYPE;
			attrSize = 16 * 16;
			break;
		case B_LARGE_ICON:
			attribute = largeIconAttrName;
			bounds.Set(0, 0, 31, 31);
			attrType = B_LARGE_ICON_TYPE;
			attrSize = 32 * 32;
			break;
		default:
			// can not happen, see above
			ret = B_BAD_VALUE;
			break;
	}

	// get the attribute info and check type and size of the attr contents
	attr_info attrInfo;
	if (ret == B_OK)
		ret = node->GetAttrInfo(attribute, &attrInfo);
	if (ret == B_OK && attrInfo.type != attrType)
		ret = B_BAD_TYPE;
	if (ret == B_OK && attrInfo.size != attrSize)
		ret = B_BAD_DATA;

	// check parameters
	// currently, scaling B_CMAP8 icons is not supported
	if (icon->ColorSpace() == B_CMAP8 && icon->Bounds() != bounds)
		return B_BAD_VALUE;

	// read the attribute
	if (ret == B_OK) {
		bool tempBuffer = (icon->ColorSpace() != B_CMAP8
						   || icon->Bounds() != bounds);
		uint8* buffer = NULL;
		ssize_t read;
		if (tempBuffer) {
			// other color space or bitmap size than stored in attribute
			buffer = new(nothrow) uint8[attrSize];
			if (!buffer) {
				ret = B_NO_MEMORY;
			} else {
				read = node->ReadAttr(attribute, attrType, 0, buffer,
									  attrSize);
			}
		} else {
			read = node->ReadAttr(attribute, attrType, 0, icon->Bits(), 
								  attrSize);
		}
		if (ret == B_OK) {
			if (read < 0)
				ret = read;
			else if (read != (ssize_t)attrSize)
				ret = B_ERROR;
		}
		if (tempBuffer) {
			// other color space than stored in attribute
			if (ret == B_OK) {
				ret = ConvertFromCMAP8(buffer,
									   (uint32)size, (uint32)size,
									   (uint32)size, icon);
			}
			delete[] buffer;
		}
	}
	return ret;
}

// #pragma mark -

// ConvertFromCMAP8
status_t
BIconUtils::ConvertFromCMAP8(BBitmap* source, BBitmap* result)
{
	if (!source)
		return B_BAD_VALUE;

	status_t ret = source->InitCheck();
	if (ret < B_OK)
		return ret;

	if (source->ColorSpace() != B_CMAP8)
		return B_BAD_VALUE;

	uint8* src = (uint8*)source->Bits();
	uint32 srcBPR = source->BytesPerRow();
	uint32 width = source->Bounds().IntegerWidth() + 1;
	uint32 height = source->Bounds().IntegerHeight() + 1;

	return ConvertFromCMAP8(src, width, height, srcBPR, result);
}

// scale_bilinear
static void
scale_bilinear(uint8* bits, int32 srcWidth, int32 srcHeight,
			   int32 dstWidth, int32 dstHeight, uint32 bpr)
{
	// first pass: scale bottom to top

	uint8* dst = bits + (dstHeight - 1) * bpr;
		// offset to bottom left pixel in target size
	for (int32 x = 0; x < srcWidth; x++) {
		uint8* d = dst;
		for (int32 y = dstHeight - 1; y >= 0; y--) {
			int32 lineF = y * 256 * (srcHeight - 1) / (dstHeight - 1);
			int32 lineI = lineF >> 8;
			uint8 weight = (uint8)(lineF & 0xff);
			uint8* s1 = bits + lineI * bpr + 4 * x;
			if (weight == 0) {
				d[0] = s1[0];
				d[1] = s1[1];
				d[2] = s1[2];
				d[3] = s1[3];
			} else {
				uint8* s2 = s1 + bpr;
				
				d[0] = (((s2[0] - s1[0]) * weight) + (s1[0] << 8)) >> 8;
				d[1] = (((s2[1] - s1[1]) * weight) + (s1[1] << 8)) >> 8;
				d[2] = (((s2[2] - s1[2]) * weight) + (s1[2] << 8)) >> 8;
				d[3] = (((s2[3] - s1[3]) * weight) + (s1[3] << 8)) >> 8;
			}

			d -= bpr;
		}
		dst += 4;
	}

	// second pass: scale right to left

	dst = bits + (dstWidth - 1) * 4;
		// offset to top left pixel in target size
	for (int32 y = 0; y < dstWidth; y++) {
		uint8* d = dst;
		for (int32 x = dstWidth - 1; x >= 0; x--) {
			int32 columnF = x * 256 * (srcWidth - 1) / (dstWidth - 1);
			int32 columnI = columnF >> 8;
			uint8 weight = (uint8)(columnF & 0xff);
			uint8* s1 = bits + y * bpr + 4 * columnI;
			if (weight == 0) {
				d[0] = s1[0];
				d[1] = s1[1];
				d[2] = s1[2];
				d[3] = s1[3];
			} else {
				uint8* s2 = s1 + 4;
				
				d[0] = (((s2[0] - s1[0]) * weight) + (s1[0] << 8)) >> 8;
				d[1] = (((s2[1] - s1[1]) * weight) + (s1[1] << 8)) >> 8;
				d[2] = (((s2[2] - s1[2]) * weight) + (s1[2] << 8)) >> 8;
				d[3] = (((s2[3] - s1[3]) * weight) + (s1[3] << 8)) >> 8;
			}

			d -= 4;
		}
		dst += bpr;
	}
}


// ConvertFromCMAP8
status_t
BIconUtils::ConvertFromCMAP8(const uint8* src,
							 uint32 width, uint32 height, uint32 srcBPR,
							 BBitmap* result)
{
	if (!src || !result || srcBPR == 0)
		return B_BAD_VALUE;

	status_t ret = result->InitCheck();
	if (ret < B_OK)
		return ret;

	uint32 dstWidth = result->Bounds().IntegerWidth() + 1;
	uint32 dstHeight = result->Bounds().IntegerHeight() + 1;

	if (dstWidth < width || dstHeight < height) {
		// TODO: down scaling
		return B_ERROR;
	}

//#if __HAIKU__
//
//	return result->ImportBits(src, height * srcBPR, srcBPR, 0, B_CMAP8);
//
//#else

	if (result->ColorSpace() != B_RGBA32 && result->ColorSpace() != B_RGB32) {
		// TODO: support other color spaces
		return B_BAD_VALUE;
	}

	uint8* dst = (uint8*)result->Bits();
	uint32 dstBPR = result->BytesPerRow();

	const rgb_color* colorMap = system_colors()->color_list;

	for (uint32 y = 0; y < height; y++) {
		uint32* d = (uint32*)dst;
		const uint8* s = src;
		for (uint32 x = 0; x < width; x++) {
			const rgb_color c = colorMap[*s];
			uint8 alpha = 255;
			if (*s == B_TRANSPARENT_MAGIC_CMAP8)
				alpha = 0;
			*d = (alpha << 24) | (c.red << 16) | (c.green << 8) | (c.blue);
			s++;
			d++;
		}
		src += srcBPR;
		dst += dstBPR;
	}

	if (dstWidth > width || dstHeight > height) {
		// up scaling
		scale_bilinear((uint8*)result->Bits(), width, height,
					   dstWidth, dstHeight, dstBPR);
	}

	return B_OK;

//#endif // __HAIKU__
}

// ConvertToCMAP8
status_t
BIconUtils::ConvertToCMAP8(const uint8* src,
						   uint32 width, uint32 height, uint32 srcBPR,
						   BBitmap* result)
{
	if (!src || !result || srcBPR == 0)
		return B_BAD_VALUE;

	status_t ret = result->InitCheck();
	if (ret < B_OK)
		return ret;

	uint32 dstWidth = result->Bounds().IntegerWidth() + 1;
	uint32 dstHeight = result->Bounds().IntegerHeight() + 1;

	if (dstWidth < width || dstHeight < height) {
		// TODO: down scaling
		return B_ERROR;
	} else if (dstWidth > width || dstHeight > height) {
		// TODO: up scaling
		// (currently copies bitmap into result at left-top)
memset(result->Bits(), 255, result->BitsLength());
	}

//#if __HAIKU__
//
//	return result->ImportBits(src, height * srcBPR, srcBPR, 0, B_RGBA32);
//
//#else

	if (result->ColorSpace() != B_CMAP8)
		return B_BAD_VALUE;

	uint8* dst = (uint8*)result->Bits();
	uint32 dstBPR = result->BytesPerRow();

	const color_map* colorMap = system_colors();
	uint16 index;

	for (uint32 y = 0; y < height; y++) {
		uint8* d = dst;
		const uint8* s = src;
		for (uint32 x = 0; x < width; x++) {
			if (s[3] < 128) {
				*d = B_TRANSPARENT_MAGIC_CMAP8;
			} else {
				index = ((s[2] & 0xf8) << 7) | ((s[1] & 0xf8) << 2)
						| (s[0] >> 3);
				*d = colorMap->index_map[index];
			}
			s += 4;
			d += 1;
		}
		src += srcBPR;
		dst += dstBPR;
	}

	return B_OK;

//#endif // __HAIKU__
}

// #pragma mark - forbidden

BIconUtils::BIconUtils() {}
BIconUtils::~BIconUtils() {}
BIconUtils::BIconUtils(const BIconUtils&) {}
BIconUtils& BIconUtils::operator=(const BIconUtils&) { return *this; }

