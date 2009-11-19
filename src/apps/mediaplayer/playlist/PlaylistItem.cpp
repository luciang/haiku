/*
 * Copyright 2009 Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "PlaylistItem.h"

#include <stdio.h>


PlaylistItem::Listener::Listener()
{
}

PlaylistItem::Listener::~Listener()
{
}

void PlaylistItem::Listener::ItemChanged(const PlaylistItem* item)
{
}


// #pragma mark -


//#define DEBUG_INSTANCE_COUNT
#ifdef DEBUG_INSTANCE_COUNT
static vint32 sInstanceCount = 0;
#endif


PlaylistItem::PlaylistItem()
	:
	fPlaybackFailed(false)
{
#ifdef DEBUG_INSTANCE_COUNT
	atomic_add(&sInstanceCount, 1);
	printf("%p->PlaylistItem::PlaylistItem() (%ld)\n", this, sInstanceCount);
#endif
}


PlaylistItem::~PlaylistItem()
{
#ifdef DEBUG_INSTANCE_COUNT
	atomic_add(&sInstanceCount, -1);
	printf("%p->PlaylistItem::~PlaylistItem() (%ld)\n", this, sInstanceCount);
#endif
}


BString
PlaylistItem::Name() const
{
	BString name;
	if (GetAttribute(ATTR_STRING_NAME, name) != B_OK)
		name = "<unnamed>";
	return name;
}


BString
PlaylistItem::Author() const
{
	BString author;
	if (GetAttribute(ATTR_STRING_AUTHOR, author) != B_OK)
		author = "<unknown>";
	return author;
}


BString
PlaylistItem::Album() const
{
	BString album;
	if (GetAttribute(ATTR_STRING_ALBUM, album) != B_OK)
		album = "<unknown>";
	return album;
}


BString
PlaylistItem::Title() const
{
	BString title;
	if (GetAttribute(ATTR_STRING_TITLE, title) != B_OK)
		title = "<untitled>";
	return title;
}


int32
PlaylistItem::TrackNumber() const
{
	int32 trackNumber;
	if (GetAttribute(ATTR_INT32_TRACK, trackNumber) != B_OK)
		trackNumber = 0;
	return trackNumber;
}


int32
PlaylistItem::BitRate() const
{
	int32 bitrate;
	if (GetAttribute(ATTR_INT32_BIT_RATE, bitrate) != B_OK)
		bitrate = 0;
	return bitrate;
}


bigtime_t
PlaylistItem::Duration() const
{
	bigtime_t duration;
	if (GetAttribute(ATTR_INT64_DURATION, duration) != B_OK)
		duration = 0;
	return duration;
}


void
PlaylistItem::SetPlaybackFailed()
{
	fPlaybackFailed = true;
}


//! You must hold the Playlist lock.
bool
PlaylistItem::AddListener(Listener* listener)
{
	if (listener && !fListeners.HasItem(listener))
		return fListeners.AddItem(listener);
	return false;
}


//! You must hold the Playlist lock.
void
PlaylistItem::RemoveListener(Listener* listener)
{
	fListeners.RemoveItem(listener);
}


void
PlaylistItem::_NotifyListeners() const
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		Listener* listener = (Listener*)listeners.ItemAtFast(i);
		listener->ItemChanged(this);
	}
}

