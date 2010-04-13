/*
 * Copyright 2007-2009 Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "ImportPLItemsCommand.h"

#include <new>
#include <stdio.h>

#include <Alert.h>
#include <Autolock.h>

#include "Playlist.h"
#include "PlaylistItem.h"


using std::nothrow;


ImportPLItemsCommand::ImportPLItemsCommand(Playlist* playlist,
		 const BMessage* refsMessage, int32 toIndex)
	:
	PLItemsCommand(),
	fPlaylist(playlist),

	fOldItems(NULL),
	fOldCount(0),

	fNewItems(NULL),
	fNewCount(0),

	fToIndex(toIndex),
	fPlaylingIndex(0),

	fItemsAdded(false)
{
	if (!fPlaylist)
		return;

	Playlist temp;
	temp.AppendRefs(refsMessage);

	fNewCount = temp.CountItems();
	if (fNewCount <= 0) {
		BAlert* alert = new BAlert("Nothing to Play", "None of the files "
			"you wanted to play appear to be media files.", "OK");
		alert->Go(NULL);
		return;
	}

	fNewItems = new (nothrow) PlaylistItem*[fNewCount];
	if (!fNewItems)
		return;
	memset(fNewItems, 0, fNewCount * sizeof(PlaylistItem*));

	// init new entries
	for (int32 i = 0; i < fNewCount; i++) {
		fNewItems[i] = temp.ItemAtFast(i)->Clone();
		if (fNewItems[i] == NULL) {
			// indicate bad object init
			_CleanUp(fNewItems, fNewCount, true);
			return;
		}
	}

	fPlaylingIndex = fPlaylist->CurrentItemIndex();

	if (fToIndex == APPEND_INDEX_REPLACE_PLAYLIST) {
		fOldCount = fPlaylist->CountItems();
		if (fOldCount > 0) {
			fOldItems = new (nothrow) PlaylistItem*[fOldCount];
			if (!fOldItems) {
				// indicate bad object init
				_CleanUp(fNewItems, fNewCount, true);
			}
			memset(fOldItems, 0, fOldCount * sizeof(PlaylistItem*));
		}
	}

	for (int32 i = 0; i < fOldCount; i++) {
		fOldItems[i] = fPlaylist->ItemAtFast(i)->Clone();
		if (fOldItems[i] == NULL) {
			// indicate bad object init
			_CleanUp(fNewItems, fNewCount, true);
			return;
		}
	}
}


ImportPLItemsCommand::~ImportPLItemsCommand()
{
	_CleanUp(fOldItems, fOldCount, fItemsAdded);
	_CleanUp(fNewItems, fNewCount, !fItemsAdded);
}


status_t
ImportPLItemsCommand::InitCheck()
{
	if (!fPlaylist || !fNewItems)
		return B_NO_INIT;
	return B_OK;
}


status_t
ImportPLItemsCommand::Perform()
{
	BAutolock _(fPlaylist);

	fItemsAdded = true;

	if (fToIndex == APPEND_INDEX_APPEND_LAST)
		fToIndex = fPlaylist->CountItems();

	int32 index = fToIndex;
	if (fToIndex == APPEND_INDEX_REPLACE_PLAYLIST) {
		fPlaylist->MakeEmpty(false);
		index = 0;
	}

	bool startPlaying = fPlaylist->CountItems() == 0;

	// add refs to playlist at the insertion index
	for (int32 i = 0; i < fNewCount; i++) {
		if (!fPlaylist->AddItem(fNewItems[i], index++))
			return B_NO_MEMORY;
	}

	if (startPlaying) {
		// open first file
		fPlaylist->SetCurrentItemIndex(0);
	}

	return B_OK;
}


status_t
ImportPLItemsCommand::Undo()
{
	BAutolock _(fPlaylist);

	fItemsAdded = false;

	if (fToIndex == APPEND_INDEX_REPLACE_PLAYLIST) {
		// remove new items from playlist and restore old refs
		fPlaylist->MakeEmpty(false);
		for (int32 i = 0; i < fOldCount; i++) {
			if (!fPlaylist->AddItem(fOldItems[i], i))
				return B_NO_MEMORY;
		}
		// Restore previously playing item
		if (fPlaylingIndex >= 0)
			fPlaylist->SetCurrentItemIndex(fPlaylingIndex);
	} else {
		// remove new items from playlist
		for (int32 i = 0; i < fNewCount; i++) {
			fPlaylist->RemoveItem(fToIndex);
		}
	}

	return B_OK;
}


void
ImportPLItemsCommand::GetName(BString& name)
{
	if (fNewCount > 1)
		name << "Import Entries";
	else
		name << "Import Entry";
}

