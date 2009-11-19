/*
 * Playlist.cpp - Media Player for the Haiku Operating System
 *
 * Copyright (C) 2006 Marcus Overhagen <marcus@overhagen.de>
 * Copyright (C) 2007-2009 Stephan Aßmus <superstippi@gmx.de> (MIT ok)
 * Copyright (C) 2008-2009 Fredrik Modéen <[FirstName]@[LastName].se> (MIT ok)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "Playlist.h"

#include <debugger.h>
#include <new>
#include <stdio.h>

#include <AppFileInfo.h>
#include <Application.h>
#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include "FilePlaylistItem.h"
#include "FileReadWrite.h"

using std::nothrow;

// TODO: using BList for objects is bad, replace it with a template

Playlist::Listener::Listener() {}
Playlist::Listener::~Listener() {}
void Playlist::Listener::ItemAdded(PlaylistItem* item, int32 index) {}
void Playlist::Listener::ItemRemoved(int32 index) {}
void Playlist::Listener::ItemsSorted() {}
void Playlist::Listener::CurrentItemChanged(int32 newIndex) {}


// #pragma mark -


static void
make_item_compare_string(const PlaylistItem* item, char* buffer,
	size_t bufferSize)
{
	// TODO: Maybe "location" would be useful here as well.
//	snprintf(buffer, bufferSize, "%s - %s - %0*ld - %s",
//		item->Author().String(),
//		item->Album().String(),
//		3, item->TrackNumber(),
//		item->Title().String());
	snprintf(buffer, bufferSize, "%s", item->LocationURI().String());
}


static int
playlist_item_compare(const void* _item1, const void* _item2)
{
	// compare complete path
	const PlaylistItem* item1 = *(const PlaylistItem**)_item1;
	const PlaylistItem* item2 = *(const PlaylistItem**)_item2;

	static const size_t bufferSize = 1024;
	char string1[bufferSize];
	make_item_compare_string(item1, string1, bufferSize);
	char string2[bufferSize];
	make_item_compare_string(item2, string2, bufferSize);

	return strcmp(string1, string2);
}


// #pragma mark -


Playlist::Playlist()
	:
	BLocker("playlist lock"),
	fItems(),
 	fCurrentIndex(-1)
{
}


Playlist::~Playlist()
{
	MakeEmpty();

	if (fListeners.CountItems() > 0)
		debugger("Playlist::~Playlist() - there are still listeners attached!");
}


// #pragma mark - archiving


static const char* kItemArchiveKey = "item";


status_t
Playlist::Unarchive(const BMessage* archive)
{
	if (archive == NULL)
		return B_BAD_VALUE;

	MakeEmpty();

	BMessage itemArchive;
	for (int32 i = 0;
		archive->FindMessage(kItemArchiveKey, i, &itemArchive) == B_OK; i++) {

		BArchivable* archivable = instantiate_object(&itemArchive);
		PlaylistItem* item = dynamic_cast<PlaylistItem*>(archivable);
		if (!item) {
			delete archivable;
			continue;
		}

		if (!AddItem(item)) {
			delete item;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
Playlist::Archive(BMessage* into) const
{
	if (into == NULL)
		return B_BAD_VALUE;

	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		const PlaylistItem* item = ItemAtFast(i);
		BMessage itemArchive;
		status_t ret = item->Archive(&itemArchive);
		if (ret != B_OK)
			return ret;
		ret = into->AddMessage(kItemArchiveKey, &itemArchive);
		if (ret != B_OK)
			return ret;
	}

	return B_OK;
}


const uint32 kPlaylistMagicBytes = 'MPPL';
const char* kTextPlaylistMimeString = "text/x-playlist";
const char* kBinaryPlaylistMimeString = "application/x-vnd.haiku-playlist";

status_t
Playlist::Unflatten(BDataIO* stream)
{
	if (stream == NULL)
		return B_BAD_VALUE;

	uint32 magicBytes;
	ssize_t read = stream->Read(&magicBytes, 4);
	if (read != 4) {
		if (read < 0)
			return (status_t)read;
		return B_IO_ERROR;
	}

	if (B_LENDIAN_TO_HOST_INT32(magicBytes) != kPlaylistMagicBytes)
		return B_BAD_VALUE;

	BMessage archive;
	status_t ret = archive.Unflatten(stream);
	if (ret != B_OK)
		return ret;

	return Unarchive(&archive);
}


status_t
Playlist::Flatten(BDataIO* stream) const
{
	if (stream == NULL)
		return B_BAD_VALUE;

	BMessage archive;
	status_t ret = Archive(&archive);
	if (ret != B_OK)
		return ret;

	uint32 magicBytes = B_HOST_TO_LENDIAN_INT32(kPlaylistMagicBytes);
	ssize_t written = stream->Write(&magicBytes, 4);
	if (written != 4) {
		if (written < 0)
			return (status_t)written;
		return B_IO_ERROR;
	}

	return archive.Flatten(stream);
}


// #pragma mark - list access


void
Playlist::MakeEmpty(bool deleteItems)
{
	int32 count = CountItems();
	for (int32 i = count - 1; i >= 0; i--) {
		PlaylistItem* item = RemoveItem(i, false);
		_NotifyItemRemoved(i);
		if (deleteItems)
			item->RemoveReference();
	}
	SetCurrentItemIndex(-1);
}


int32
Playlist::CountItems() const
{
	return fItems.CountItems();
}


void
Playlist::Sort()
{
	fItems.SortItems(playlist_item_compare);
	_NotifyItemsSorted();
}


bool
Playlist::AddItem(PlaylistItem* item)
{
	return AddItem(item, CountItems());
}


bool
Playlist::AddItem(PlaylistItem* item, int32 index)
{
	if (!fItems.AddItem(item, index))
		return false;

	if (index <= fCurrentIndex)
		SetCurrentItemIndex(fCurrentIndex + 1);

	_NotifyItemAdded(item, index);

	return true;
}


bool
Playlist::AdoptPlaylist(Playlist& other)
{
	return AdoptPlaylist(other, CountItems());
}


bool
Playlist::AdoptPlaylist(Playlist& other, int32 index)
{
	if (&other == this)
		return false;
	// NOTE: this is not intended to merge two "equal" playlists
	// the given playlist is assumed to be a temporary "dummy"
	if (fItems.AddList(&other.fItems, index)) {
		// take care of the notifications
		int32 count = other.CountItems();
		for (int32 i = index; i < index + count; i++) {
			PlaylistItem* item = ItemAtFast(i);
			_NotifyItemAdded(item, i);
		}
		if (index <= fCurrentIndex)
			SetCurrentItemIndex(fCurrentIndex + count);
		// empty the other list, so that the PlaylistItems are now ours
		other.fItems.MakeEmpty();
		return true;
	}
	return false;
}


PlaylistItem*
Playlist::RemoveItem(int32 index, bool careAboutCurrentIndex)
{
	PlaylistItem* item = (PlaylistItem*)fItems.RemoveItem(index);
	if (!item)
		return NULL;
	_NotifyItemRemoved(index);

	if (careAboutCurrentIndex) {
		if (index == fCurrentIndex && index >= CountItems())
			SetCurrentItemIndex(CountItems() - 1);
		else if (index < fCurrentIndex)
			SetCurrentItemIndex(fCurrentIndex - 1);
	}

	return item;
}


int32
Playlist::IndexOf(PlaylistItem* item) const
{
	return fItems.IndexOf(item);
}


PlaylistItem*
Playlist::ItemAt(int32 index) const
{
	return (PlaylistItem*)fItems.ItemAt(index);
}


PlaylistItem*
Playlist::ItemAtFast(int32 index) const
{
	return (PlaylistItem*)fItems.ItemAtFast(index);
}


// #pragma mark - navigation


bool
Playlist::SetCurrentItemIndex(int32 index)
{
	bool result = true;
	if (index >= CountItems() || index < 0) {
		index = -1;
		result = false;
	}

	if (index == fCurrentIndex)
		return result;

	fCurrentIndex = index;
	_NotifyCurrentItemChanged(fCurrentIndex);
	return result;
}


int32
Playlist::CurrentItemIndex() const
{
	return fCurrentIndex;
}


void
Playlist::GetSkipInfo(bool* canSkipPrevious, bool* canSkipNext) const
{
	if (canSkipPrevious)
		*canSkipPrevious = fCurrentIndex > 0;
	if (canSkipNext)
		*canSkipNext = fCurrentIndex < CountItems() - 1;
}


// pragma mark -


bool
Playlist::AddListener(Listener* listener)
{
	BAutolock _(this);
	if (listener && !fListeners.HasItem(listener))
		return fListeners.AddItem(listener);
	return false;
}


void
Playlist::RemoveListener(Listener* listener)
{
	BAutolock _(this);
	fListeners.RemoveItem(listener);
}


// #pragma mark - support


void
Playlist::AppendRefs(const BMessage* refsReceivedMessage, int32 appendIndex)
{
	// the playlist ist replaced by the refs in the message
	// or the refs are appended at the appendIndex
	// in the existing playlist
	bool add = appendIndex >= 0;

	if (!add)
		MakeEmpty();

	bool startPlaying = CountItems() == 0;

	Playlist temporaryPlaylist;
	Playlist* playlist = add ? &temporaryPlaylist : this;
	bool sortPlaylist = true;

	entry_ref ref;
	int32 subAppendIndex = CountItems();
	for (int i = 0; refsReceivedMessage->FindRef("refs", i, &ref) == B_OK;
			i++) {
		Playlist subPlaylist;
		if (_IsPlaylist(_MIMEString(&ref))) {
			AppendPlaylistToPlaylist(ref, &subPlaylist);
			// Do not sort the whole playlist anymore, as that
			// will screw up the ordering in the saved playlist.
			sortPlaylist = false;
		} else {
			AppendToPlaylistRecursive(ref, &subPlaylist);
			// At least sort this subsection of the playlist
			// if the whole playlist is not sorted anymore.
			if (!sortPlaylist)
				subPlaylist.Sort();
		}
		int32 subPlaylistCount = subPlaylist.CountItems();
		AdoptPlaylist(subPlaylist, subAppendIndex);
		subAppendIndex += subPlaylistCount;
	}
	if (sortPlaylist)
		playlist->Sort();

	if (add)
		AdoptPlaylist(temporaryPlaylist, appendIndex);

	if (startPlaying) {
		// open first file
		SetCurrentItemIndex(0);
	}
}


/*static*/ void
Playlist::AppendToPlaylistRecursive(const entry_ref& ref, Playlist* playlist)
{
	// recursively append the ref (dive into folders)
	BEntry entry(&ref, true);
	if (entry.InitCheck() != B_OK || !entry.Exists())
		return;

	if (entry.IsDirectory()) {
		BDirectory dir(&entry);
		if (dir.InitCheck() != B_OK)
			return;

		entry.Unset();

		entry_ref subRef;
		while (dir.GetNextRef(&subRef) == B_OK) {
			AppendToPlaylistRecursive(subRef, playlist);
		}
	} else if (entry.IsFile()) {
		BString mimeString = _MIMEString(&ref);
		if (_IsMediaFile(mimeString)) {
			PlaylistItem* item = new (std::nothrow) FilePlaylistItem(ref);
			if (item != NULL && !playlist->AddItem(item))
				delete item;
		} else
			printf("MIME Type = %s\n", mimeString.String());
	}
}


/*static*/ void
Playlist::AppendPlaylistToPlaylist(const entry_ref& ref, Playlist* playlist)
{
	BEntry entry(&ref, true);
	if (entry.InitCheck() != B_OK || !entry.Exists())
		return;

	BString mimeString = _MIMEString(&ref);
	if (_IsTextPlaylist(mimeString)) {
		//printf("RunPlaylist thing\n");
		BFile file(&ref, B_READ_ONLY);
		FileReadWrite lineReader(&file);

		BString str;
		entry_ref refPath;
		status_t err;
		BPath path;
		while (lineReader.Next(str)) {
			str = str.RemoveFirst("file://");
			str = str.RemoveLast("..");
			path = BPath(str.String());
			printf("Line %s\n", path.Path());
			if (path.Path() != NULL) {
				if ((err = get_ref_for_path(path.Path(), &refPath)) == B_OK) {
					PlaylistItem* item
						= new (std::nothrow) FilePlaylistItem(refPath);
					if (item == NULL || !playlist->AddItem(item))
						delete item;
				} else
					printf("Error - %s: [%lx]\n", strerror(err), (int32) err);
			} else
				printf("Error - No File Found in playlist\n");
		}
	} else if (_IsBinaryPlaylist(mimeString)) {
		BFile file(&ref, B_READ_ONLY);
		Playlist temp;
		if (temp.Unflatten(&file) == B_OK)
			playlist->AdoptPlaylist(temp, playlist->CountItems());
	}
}


// #pragma mark - private


/*static*/ bool
Playlist::_IsMediaFile(const BString& mimeString)
{
	BMimeType superType;
	BMimeType fileType(mimeString.String());

	if (fileType.GetSupertype(&superType) != B_OK)
		return false;

	// try a shortcut first
	if (superType == "audio" || superType == "video")
		return true;

	// Look through our supported types
	app_info appInfo;
	if (be_app->GetAppInfo(&appInfo) != B_OK)
		return false;
	BFile appFile(&appInfo.ref, B_READ_ONLY);
	if (appFile.InitCheck() != B_OK)
		return false;
	BMessage types;
	BAppFileInfo appFileInfo(&appFile);
	if (appFileInfo.GetSupportedTypes(&types) != B_OK)
		return false;

	const char* type;
	for (int32 i = 0; types.FindString("types", i, &type) == B_OK; i++) {
		if (strcasecmp(mimeString.String(), type) == 0)
			return true;
	}

	return false;
}


/*static*/ bool
Playlist::_IsTextPlaylist(const BString& mimeString)
{
	return mimeString.Compare(kTextPlaylistMimeString) == 0;
}


/*static*/ bool
Playlist::_IsBinaryPlaylist(const BString& mimeString)
{
	return mimeString.Compare(kBinaryPlaylistMimeString) == 0;
}


/*static*/ bool
Playlist::_IsPlaylist(const BString& mimeString)
{
	return _IsTextPlaylist(mimeString) || _IsBinaryPlaylist(mimeString);
}


/*static*/ BString
Playlist::_MIMEString(const entry_ref* ref)
{
	BFile file(ref, B_READ_ONLY);
	BNodeInfo nodeInfo(&file);
	char mimeString[B_MIME_TYPE_LENGTH];
	if (nodeInfo.GetType(mimeString) != B_OK) {
		BMimeType type;
		if (BMimeType::GuessMimeType(ref, &type) != B_OK)
			return BString();

		strlcpy(mimeString, type.Type(), B_MIME_TYPE_LENGTH);
		nodeInfo.SetType(type.Type());
	}
	return BString(mimeString);
}


// #pragma mark - notifications


void
Playlist::_NotifyItemAdded(PlaylistItem* item, int32 index) const
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		Listener* listener = (Listener*)listeners.ItemAtFast(i);
		listener->ItemAdded(item, index);
	}
}


void
Playlist::_NotifyItemRemoved(int32 index) const
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		Listener* listener = (Listener*)listeners.ItemAtFast(i);
		listener->ItemRemoved(index);
	}
}


void
Playlist::_NotifyItemsSorted() const
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		Listener* listener = (Listener*)listeners.ItemAtFast(i);
		listener->ItemsSorted();
	}
}


void
Playlist::_NotifyCurrentItemChanged(int32 newIndex) const
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		Listener* listener = (Listener*)listeners.ItemAtFast(i);
		listener->CurrentItemChanged(newIndex);
	}
}

