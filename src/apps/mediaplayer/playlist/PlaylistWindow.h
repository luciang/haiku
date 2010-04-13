/*
 * Copyright 2007-2010, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus 	<superstippi@gmx.de>
 *		Fredrik Modéen 	<fredrik@modeen.se>
 */
#ifndef PLAYLIST_WINDOW_H
#define PLAYLIST_WINDOW_H


#include <Entry.h>
#include <Window.h>

#include "ListenerAdapter.h"


class BMenuBar;
class BMenuItem;
class CommandStack;
class Controller;
class Notifier;
class Playlist;
class PlaylistListView;
class RWLocker;
class BButton;
class BFilePanel;


enum {
	M_PLAYLIST_REMOVE_AND_PUT_INTO_TRASH	= 'rmtr'
};


class PlaylistWindow : public BWindow {
public:
								PlaylistWindow(BRect frame,
									Playlist* playlist,
									Controller* controller);
	virtual						~PlaylistWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_CreateMenu(BRect& frame);
			void				_ObjectChanged(const Notifier* object);
			void				_SavePlaylist(const BMessage* filePanelMessage);
			void				_SavePlaylist(const entry_ref& ref);
			void				_SavePlaylist(BEntry& origEntry,
									BEntry& tempEntry, const char* finalName);

			Playlist*			fPlaylist;
			PlaylistListView*	fListView;

			BView*				fTopView;
			BMenuItem*			fUndoMI;
			BMenuItem*			fRedoMI;

			RWLocker*			fLocker;
			CommandStack*		fCommandStack;
			ListenerAdapter		fCommandStackListener;

			entry_ref			fSavedPlaylistRef;
};


#endif // PLAYLIST_WINDOW_H
