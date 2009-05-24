/*
 * Copyright 2007-2009 Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "PlaylistObserver.h"

#include <Message.h>


PlaylistObserver::PlaylistObserver(BHandler* target)
	: Playlist::Listener()
	, AbstractLOAdapter(target)
{
}


PlaylistObserver::~PlaylistObserver()
{
}


void
PlaylistObserver::ItemAdded(PlaylistItem* item, int32 index)
{
	BMessage message(MSG_PLAYLIST_ITEM_ADDED);
	message.AddPointer("item", item);
	message.AddInt32("index", index);

	DeliverMessage(message);
}


void
PlaylistObserver::ItemRemoved(int32 index)
{
	BMessage message(MSG_PLAYLIST_ITEM_REMOVED);
	message.AddInt32("index", index);

	DeliverMessage(message);
}


void
PlaylistObserver::ItemsSorted()
{
	BMessage message(MSG_PLAYLIST_ITEMS_SORTED);

	DeliverMessage(message);
}


void
PlaylistObserver::CurrentItemChanged(int32 newIndex)
{
	BMessage message(MSG_PLAYLIST_CURRENT_ITEM_CHANGED);
	message.AddInt32("index", newIndex);

	DeliverMessage(message);
}

