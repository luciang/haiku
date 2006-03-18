/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*!
	Note, this class don't provide any locking whatsoever - you are
	supposed to have a BPrivate::AppServerLink object around which
	does the necessary locking.
	However, this is not enforced in the methods here, you have to
	take care for yourself!
*/

#include "ServerMemoryAllocator.h"

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include <syscalls.h>
#endif

#include <new>


namespace BPrivate {

struct area_mapping {
	area_id	server_area;
	area_id local_area;
	uint8*	local_base;
};


ServerMemoryAllocator::ServerMemoryAllocator()
	:
	fAreas(4)
{
}


ServerMemoryAllocator::~ServerMemoryAllocator()
{
	for (int32 i = fAreas.CountItems(); i-- > 0;) {
		area_mapping* mapping = (area_mapping*)fAreas.ItemAt(i);

		delete_area(mapping->local_area);
		delete mapping;
	}
}


status_t
ServerMemoryAllocator::InitCheck()
{
	return B_OK;
}


status_t
ServerMemoryAllocator::AddArea(area_id serverArea, area_id& _area, uint8*& _base)
{
	area_mapping* mapping = new (std::nothrow) area_mapping;
	if (mapping == NULL || !fAreas.AddItem(mapping)) {
		delete mapping;
		return B_NO_MEMORY;
	}

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	// reserve 128 MB of space for the area
	void* base = (void*)0x60000000;
	status_t status = _kern_reserve_heap_address_range((addr_t*)&base,
		B_BASE_ADDRESS, 128 * 1024 * 1024);
#else
	void* base;
	status_t status = B_ERROR;
#endif

	mapping->local_area = clone_area("server_memory", &base,
		status == B_OK ? B_EXACT_ADDRESS : B_BASE_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, serverArea);
	if (mapping->local_area < B_OK) {
		status = mapping->local_area;
		delete mapping;
		return status;
	}

	mapping->server_area = serverArea;
	mapping->local_base = (uint8*)base;

	_area = mapping->local_area;
	_base = mapping->local_base;

	return B_OK;
}


void
ServerMemoryAllocator::RemoveArea(area_id serverArea)
{
	for (int32 i = fAreas.CountItems(); i-- > 0;) {
		area_mapping* mapping = (area_mapping*)fAreas.ItemAt(i);

		if (mapping->server_area == serverArea) {
			// we found the area we should remove
			delete_area(mapping->local_area);
			delete mapping;
			break;
		}
	}
}


status_t
ServerMemoryAllocator::AreaAndBaseFor(area_id serverArea, area_id& _area, uint8*& _base)
{
	for (int32 i = fAreas.CountItems(); i-- > 0;) {
		area_mapping* mapping = (area_mapping*)fAreas.ItemAt(i);

		if (mapping->server_area == serverArea) {
			_area = mapping->local_area;
			_base = mapping->local_base;
			return B_OK;
		}
	}

	return B_ERROR;
}

}	// namespace BPrivate
