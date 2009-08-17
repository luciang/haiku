/*
 * Copyright 2001-2007, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*!
	Whenever a ServerBitmap associated with a client-side BBitmap needs to be
	created or destroyed, the BitmapManager needs to handle it. It takes care of
	all memory management related to them.
*/


#include "BitmapManager.h"

#include "ClientMemoryAllocator.h"
#include "HWInterface.h"
#include "Overlay.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerProtocol.h"
#include "ServerTokenSpace.h"

#include <BitmapPrivate.h>
#include <ObjectList.h>
#include <video_overlay.h>

#include <AppDefs.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Message.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using std::nothrow;


//! The one and only bitmap manager for the server, created by the AppServer
BitmapManager *gBitmapManager = NULL;


int
compare_app_pointer(const ServerApp* a, const ServerApp* b)
{
	return (addr_t)b - (addr_t)b;
}


//	#pragma mark -


//! Sets up stuff to be ready to allocate space for bitmaps
BitmapManager::BitmapManager()
	:
	fBitmapList(1024),
	fLock("BitmapManager Lock")
{
}


//! Deallocates everything associated with the manager
BitmapManager::~BitmapManager()
{
	int32 count = fBitmapList.CountItems();
	for (int32 i = 0; i < count; i++) {
		if (ServerBitmap* bitmap = (ServerBitmap*)fBitmapList.ItemAt(i)) {
			if (bitmap->AllocationCookie() != NULL)
				debugger("We're not supposed to keep our cookies...");

			delete bitmap;
		}
	}
}


/*!
	\brief Allocates a new ServerBitmap.
	\param bounds Size of the bitmap
	\param space Color space of the bitmap
	\param flags Bitmap flags as defined in Bitmap.h
	\param bytesPerRow Number of bytes per row.
	\param screen Screen id of the screen associated with it. Unused.
	\return A new ServerBitmap or NULL if unable to allocate one.
*/
ServerBitmap*
BitmapManager::CreateBitmap(ClientMemoryAllocator* allocator,
	HWInterface& hwInterface, BRect bounds, color_space space, uint32 flags,
	int32 bytesPerRow, screen_id screen, uint8* _allocationFlags)
{
	BAutolock locker(fLock);

	if (!locker.IsLocked())
		return NULL;

	overlay_token overlayToken = NULL;

	if (flags & B_BITMAP_WILL_OVERLAY) {
		if (!hwInterface.CheckOverlayRestrictions(bounds.IntegerWidth() + 1,
				bounds.IntegerHeight() + 1, space))
			return NULL;

		if (flags & B_BITMAP_RESERVE_OVERLAY_CHANNEL) {
			overlayToken = hwInterface.AcquireOverlayChannel();
			if (overlayToken == NULL)
				return NULL;
		}
	}

	ServerBitmap* bitmap = new(nothrow) ServerBitmap(bounds, space, flags,
		bytesPerRow);
	if (bitmap == NULL) {
		if (overlayToken != NULL)
			hwInterface.ReleaseOverlayChannel(overlayToken);

		return NULL;
	}

	void* cookie = NULL;
	uint8* buffer = NULL;

	if (flags & B_BITMAP_WILL_OVERLAY) {
		Overlay* overlay = new (std::nothrow) Overlay(hwInterface, bitmap,
			overlayToken);

		overlay_client_data* clientData = NULL;
		bool newArea = false;

		if (overlay != NULL && overlay->InitCheck() == B_OK) {
			// allocate client memory to communicate the overlay semaphore
			// and buffer location to the BBitmap
			cookie = allocator->Allocate(sizeof(overlay_client_data),
				(void**)&clientData, newArea);
		}

		if (cookie != NULL) {
			overlay->SetClientData(clientData);

			bitmap->fAllocator = allocator;
			bitmap->fAllocationCookie = cookie;
			bitmap->SetOverlay(overlay);
			bitmap->fBytesPerRow = overlay->OverlayBuffer()->bytes_per_row;

			buffer = (uint8*)overlay->OverlayBuffer()->buffer;
			if (_allocationFlags)
				*_allocationFlags = kFramebuffer | (newArea ? kNewAllocatorArea : 0);
		} else {
			delete overlay;
			allocator->Free(cookie);
		}
	} else if (allocator != NULL) {
		// standard bitmaps
		bool newArea;
		cookie = allocator->Allocate(bitmap->BitsLength(), (void**)&buffer, newArea);
		if (cookie != NULL) {
			bitmap->fAllocator = allocator;
			bitmap->fAllocationCookie = cookie;

			if (_allocationFlags)
				*_allocationFlags = kAllocator | (newArea ? kNewAllocatorArea : 0);
		}
	} else {
		// server side only bitmaps
		buffer = (uint8*)malloc(bitmap->BitsLength());
		if (buffer != NULL) {
			bitmap->fAllocator = NULL;
			bitmap->fAllocationCookie = NULL;

			if (_allocationFlags)
				*_allocationFlags = kHeap;
		}
	}

	bool success = false;
	if (buffer != NULL) {
		success = fBitmapList.AddItem(bitmap);
		if (success && bitmap->Overlay() != NULL) {
			success = fOverlays.AddItem(bitmap);
			if (!success)
				fBitmapList.RemoveItem(bitmap);
		}
	}

	if (success) {
		bitmap->fBuffer = buffer;
		bitmap->fToken = gTokenSpace.NewToken(kBitmapToken, bitmap);
		// NOTE: the client handles clearing to white in case the flags
		// indicate this is needed
	} else {
		// Allocation failed for buffer or bitmap list
		delete bitmap;
		bitmap = NULL;
	}

	return bitmap;
}


/*!
	\brief Deletes a ServerBitmap.
	\param bitmap The bitmap to delete
*/
void
BitmapManager::DeleteBitmap(ServerBitmap* bitmap)
{
	if (bitmap == NULL || !bitmap->_Release()) {
		// there are other references to this bitmap, we don't have to delete
		// it yet
		return;
	}

	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	if (bitmap->Overlay() != NULL)
		fOverlays.RemoveItem(bitmap);

	if (fBitmapList.RemoveItem(bitmap))
		delete bitmap;
}


void
BitmapManager::SuspendOverlays()
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	// first, tell all applications owning an overlay to release their locks

	BObjectList<ServerApp> apps;
	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		apps.BinaryInsert(bitmap->Owner(), &compare_app_pointer);
	}
	for (int32 i = 0; i < apps.CountItems(); i++) {
		BMessage notify(B_RELEASE_OVERLAY_LOCK);
		apps.ItemAt(i)->SendMessageToClient(&notify);
	}

	// suspend overlays

	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		bitmap->Overlay()->Suspend(bitmap, false);
	}
}


void
BitmapManager::ResumeOverlays()
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	// first, tell all applications owning an overlay that
	// they can reacquire their locks

	BObjectList<ServerApp> apps;
	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		apps.BinaryInsert(bitmap->Owner(), &compare_app_pointer);
	}
	for (int32 i = 0; i < apps.CountItems(); i++) {
		BMessage notify(B_RELEASE_OVERLAY_LOCK);
		apps.ItemAt(i)->SendMessageToClient(&notify);
	}

	// resume overlays

	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);

		bitmap->Overlay()->Resume(bitmap);
	}
}

