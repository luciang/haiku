/*
 * MainWin.cpp - Media Player for the Haiku Operating System
 *
 * Copyright (C) 2006 Marcus Overhagen <marcus@overhagen.de>
 * Copyright (C) 2007-2009 Stephan Aßmus <superstippi@gmx.de> (GPL->MIT ok)
 * Copyright (C) 2007-2009 Fredrik Modéen <[FirstName]@[LastName].se> (MIT ok)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */


#include "MainWin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Debug.h>
#include <fs_attr.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <PropertyInfo.h>
#include <RecentItems.h>
#include <Roster.h>
#include <Screen.h>
#include <String.h>
#include <View.h>

#include "AudioProducer.h"
#include "ControllerObserver.h"
#include "FilePlaylistItem.h"
#include "MainApp.h"
#include "PeakView.h"
#include "PlaylistItem.h"
#include "PlaylistObserver.h"
#include "PlaylistWindow.h"
#include "Settings.h"

#define MIN_WIDTH 250


int
MainWin::sNoVideoWidth = MIN_WIDTH;


// XXX TODO: why is lround not defined?
#define lround(a) ((int)(0.99999 + (a)))

enum {
	M_DUMMY = 0x100,
	M_FILE_OPEN = 0x1000,
	M_FILE_NEWPLAYER,
	M_FILE_INFO,
	M_FILE_PLAYLIST,
	M_FILE_CLOSE,
	M_FILE_QUIT,
	M_VIEW_SIZE,
	M_TOGGLE_FULLSCREEN,
	M_TOGGLE_ALWAYS_ON_TOP,
	M_TOGGLE_NO_INTERFACE,
	M_VOLUME_UP,
	M_VOLUME_DOWN,
	M_SKIP_NEXT,
	M_SKIP_PREV,

	// The common display aspect ratios
	M_ASPECT_SAME_AS_SOURCE,
	M_ASPECT_NO_DISTORTION,
	M_ASPECT_4_3,
	M_ASPECT_16_9,
	M_ASPECT_83_50,
	M_ASPECT_7_4,
	M_ASPECT_37_20,
	M_ASPECT_47_20,

	M_SELECT_AUDIO_TRACK		= 0x00000800,
	M_SELECT_AUDIO_TRACK_END	= 0x00000fff,
	M_SELECT_VIDEO_TRACK		= 0x00010000,
	M_SELECT_VIDEO_TRACK_END	= 0x000fffff,

	M_SET_PLAYLIST_POSITION,

	M_FILE_DELETE,

	M_SHOW_IF_NEEDED
};


static property_info sPropertyInfo[] = {
	{ "Next", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Skip to the next track.", 0
	},
	{ "Prev", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Skip to the previous track.", 0
	},
	{ "Play", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Start playing.", 0
	},
	{ "Stop", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Stop playing.", 0
	},
	{ "Pause", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Pause playback.", 0
	},
	{ "TogglePlaying", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Toggle pause/play.", 0
	},
	{ "Mute", { B_EXECUTE_PROPERTY },
		{ B_DIRECT_SPECIFIER, 0 }, "Toggle mute.", 0
	},
	{ "Volume", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Gets/sets the volume (0.0-2.0).", 0,
		{ B_FLOAT_TYPE }
	},
	{ "URI", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Gets the URI of the currently playing item.", 0, { B_STRING_TYPE }
	},
	{ 0, { 0 }, { 0 }, 0, 0 }
};


//#define printf(a...)


MainWin::MainWin(bool isFirstWindow, BMessage* message)
	:
	BWindow(BRect(100, 100, 400, 300), NAME, B_TITLED_WINDOW,
 		B_ASYNCHRONOUS_CONTROLS /* | B_WILL_ACCEPT_FIRST_CLICK */),
 	fCreationTime(system_time()),
	fInfoWin(NULL),
	fPlaylistWindow(NULL),
	fHasFile(false),
	fHasVideo(false),
	fHasAudio(false),
	fPlaylist(new Playlist),
	fPlaylistObserver(new PlaylistObserver(this)),
	fController(new Controller),
	fControllerObserver(new ControllerObserver(this,
		OBSERVE_FILE_CHANGES | OBSERVE_TRACK_CHANGES
			| OBSERVE_PLAYBACK_STATE_CHANGES | OBSERVE_POSITION_CHANGES
			| OBSERVE_VOLUME_CHANGES)),
	fIsFullscreen(false),
	fAlwaysOnTop(false),
	fNoInterface(false),
	fSourceWidth(-1),
	fSourceHeight(-1),
	fWidthAspect(0),
	fHeightAspect(0),
	fSavedFrame(),
	fNoVideoFrame(),
	fMouseDownTracking(false),
	fGlobalSettingsListener(this),
	fInitialSeekPosition(0)
{
	// Handle window position and size depending on whether this is the
	// first window or not. Use the window size from the window that was
	// last resized by the user.
	static int pos = 0;
	MoveBy(pos * 25, pos * 25);
	pos = (pos + 1) % 15;

	BRect frame = Settings::Default()->CurrentSettings()
		.audioPlayerWindowFrame;
	if (frame.IsValid()) {
		if (isFirstWindow) {
			if (message == NULL) {
				MoveTo(frame.LeftTop());
				ResizeTo(frame.Width(), frame.Height());
			} else {
				// Delay moving to the initial position, since we don't
				// know if we will be playing audio at all.
				message->AddRect("window frame", frame);
			}
		}
		if (sNoVideoWidth == MIN_WIDTH)
			sNoVideoWidth = frame.IntegerWidth();
	} else if (sNoVideoWidth > MIN_WIDTH) {
		ResizeTo(sNoVideoWidth, Bounds().Height());
	}
	fNoVideoWidth = sNoVideoWidth;

	BRect rect = Bounds();

	// background
	fBackground = new BView(rect, "background", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	fBackground->SetViewColor(0, 0, 0);
	AddChild(fBackground);

	// menu
	fMenuBar = new BMenuBar(fBackground->Bounds(), "menu");
	_CreateMenu();
	fBackground->AddChild(fMenuBar);
	fMenuBar->SetResizingMode(B_FOLLOW_NONE);
	fMenuBar->ResizeToPreferred();
	fMenuBarWidth = (int)fMenuBar->Frame().Width() + 1;
	fMenuBarHeight = (int)fMenuBar->Frame().Height() + 1;

	// video view
	rect = BRect(0, fMenuBarHeight, fBackground->Bounds().right,
		fMenuBarHeight + 10);
	fVideoView = new VideoView(rect, "video display", B_FOLLOW_NONE);
	fBackground->AddChild(fVideoView);

	// controls
	rect = BRect(0, fMenuBarHeight + 11, fBackground->Bounds().right,
		fBackground->Bounds().bottom);
	fControls = new ControllerView(rect, fController, fPlaylist);
	fBackground->AddChild(fControls);
	fControls->ResizeToPreferred();
	fControlsHeight = (int)fControls->Frame().Height() + 1;
	fControlsWidth = (int)fControls->Frame().Width() + 1;
	fControls->SetResizingMode(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT_RIGHT);

	fPlaylist->AddListener(fPlaylistObserver);
	fController->SetVideoView(fVideoView);
	fController->AddListener(fControllerObserver);
	PeakView* peakView = fControls->GetPeakView();
	peakView->SetPeakNotificationWhat(MSG_PEAK_NOTIFICATION);
	fController->SetPeakListener(peakView);

	_SetupWindow();

	// setup the playlist window now, we need to have it
	// running for the undo/redo playlist editing
	fPlaylistWindow = new PlaylistWindow(BRect(150, 150, 500, 600), fPlaylist,
		fController);
	fPlaylistWindow->Hide();
	fPlaylistWindow->Show();
		// this makes sure the window thread is running without
		// showing the window just yet

	Settings::Default()->AddListener(&fGlobalSettingsListener);
	_AdoptGlobalSettings();

	AddShortcut('z', B_COMMAND_KEY, new BMessage(B_UNDO));
	AddShortcut('y', B_COMMAND_KEY, new BMessage(B_UNDO));
	AddShortcut('z', B_COMMAND_KEY | B_SHIFT_KEY, new BMessage(B_REDO));
	AddShortcut('y', B_COMMAND_KEY | B_SHIFT_KEY, new BMessage(B_REDO));

	Hide();
	Show();

	if (message != NULL)
		PostMessage(message);
}


MainWin::~MainWin()
{
//	printf("MainWin::~MainWin\n");

	Settings::Default()->RemoveListener(&fGlobalSettingsListener);
	fPlaylist->RemoveListener(fPlaylistObserver);
	fController->Lock();
	fController->RemoveListener(fControllerObserver);
	fController->SetPeakListener(NULL);
	fController->SetVideoTarget(NULL);
	fController->Unlock();

	// give the views a chance to detach from any notifiers
	// before we delete them
	fBackground->RemoveSelf();
	delete fBackground;

	if (fInfoWin && fInfoWin->Lock())
		fInfoWin->Quit();

	if (fPlaylistWindow && fPlaylistWindow->Lock())
		fPlaylistWindow->Quit();

	delete fPlaylist;
	fPlaylist = NULL;

	// quit the Controller looper thread
	thread_id controllerThread = fController->Thread();
	fController->PostMessage(B_QUIT_REQUESTED);
	status_t exitValue;
	wait_for_thread(controllerThread, &exitValue);
}


// #pragma mark -


void
MainWin::FrameResized(float newWidth, float newHeight)
{
	if (newWidth != Bounds().Width() || newHeight != Bounds().Height()) {
		debugger("size wrong\n");
	}

	bool noMenu = fNoInterface || fIsFullscreen;
	bool noControls = fNoInterface || fIsFullscreen;

//	printf("FrameResized enter: newWidth %.0f, newHeight %.0f\n",
//		newWidth, newHeight);

	if (!fHasVideo)
		sNoVideoWidth = fNoVideoWidth = (int)newWidth;

	int maxVideoWidth  = int(newWidth) + 1;
	int maxVideoHeight = int(newHeight) + 1
		- (noMenu  ? 0 : fMenuBarHeight)
		- (noControls ? 0 : fControlsHeight);

	ASSERT(maxVideoHeight >= 0);

	int y = 0;

	if (noMenu) {
		if (!fMenuBar->IsHidden(fMenuBar))
			fMenuBar->Hide();
	} else {
		fMenuBar->MoveTo(0, y);
		fMenuBar->ResizeTo(newWidth, fMenuBarHeight - 1);
		if (fMenuBar->IsHidden(fMenuBar))
			fMenuBar->Show();
		y += fMenuBarHeight;
	}

	if (maxVideoHeight == 0) {
		if (!fVideoView->IsHidden(fVideoView))
			fVideoView->Hide();
	} else {
		_ResizeVideoView(0, y, maxVideoWidth, maxVideoHeight);
		if (fVideoView->IsHidden(fVideoView))
			fVideoView->Show();
		y += maxVideoHeight;
	}

	if (noControls) {
		if (!fControls->IsHidden(fControls))
			fControls->Hide();
	} else {
		fControls->MoveTo(0, y);
		fControls->ResizeTo(newWidth, fControlsHeight - 1);
		if (fControls->IsHidden(fControls))
			fControls->Show();
//		y += fControlsHeight;
	}

//	printf("FrameResized leave\n");
}


void
MainWin::Zoom(BPoint rec_position, float rec_width, float rec_height)
{
	PostMessage(M_TOGGLE_FULLSCREEN);
}


void
MainWin::DispatchMessage(BMessage *msg, BHandler *handler)
{
	if ((msg->what == B_MOUSE_DOWN)
		&& (handler == fBackground || handler == fVideoView
				|| handler == fControls))
		_MouseDown(msg, dynamic_cast<BView*>(handler));

	if ((msg->what == B_MOUSE_MOVED)
		&& (handler == fBackground || handler == fVideoView
				|| handler == fControls))
		_MouseMoved(msg, dynamic_cast<BView*>(handler));

	if ((msg->what == B_MOUSE_UP)
		&& (handler == fBackground || handler == fVideoView))
		_MouseUp(msg);

	if ((msg->what == B_KEY_DOWN)
		&& (handler == fBackground || handler == fVideoView)) {

		// special case for PrintScreen key
		if (msg->FindInt32("key") == B_PRINT_KEY) {
			fVideoView->OverlayScreenshotPrepare();
			BWindow::DispatchMessage(msg, handler);
			fVideoView->OverlayScreenshotCleanup();
			return;
		}

		// every other key gets dispatched to our _KeyDown first
		if (_KeyDown(msg)) {
			// it got handled, don't pass it on
			return;
		}
	}

	BWindow::DispatchMessage(msg, handler);
}


void
MainWin::MessageReceived(BMessage* msg)
{
//	msg->PrintToStream();
	switch (msg->what) {
		case B_EXECUTE_PROPERTY:
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		{
			BMessage reply(B_REPLY);
			status_t result = B_BAD_SCRIPT_SYNTAX;
			int32 index;
			BMessage specifier;
			int32 what;
			const char* property;

			if (msg->GetCurrentSpecifier(&index, &specifier, &what,
					&property) != B_OK) {
				return BWindow::MessageReceived(msg);
			}

			BPropertyInfo propertyInfo(sPropertyInfo);
			switch (propertyInfo.FindMatch(msg, index, &specifier, what,
					property)) {
				case 0:
					fControls->SkipForward();
					result = B_OK;
					break;

				case 1:
					fControls->SkipBackward();
					result = B_OK;
					break;

				case 2:
					fController->Play();
					result = B_OK;
					break;

				case 3:
					fController->Stop();
					result = B_OK;
					break;

				case 4:
					fController->Pause();
					result = B_OK;
					break;

				case 5:
					fController->TogglePlaying();
					result = B_OK;
					break;

				case 6:
					fController->ToggleMute();
					result = B_OK;
					break;

				case 7:
				{
					if (msg->what == B_GET_PROPERTY) {
						result = reply.AddFloat("result",
							fController->Volume());
					} else if (msg->what == B_SET_PROPERTY) {
						float newVolume;
						result = msg->FindFloat("data", &newVolume);
						if (result == B_OK)
							fController->SetVolume(newVolume);
					}

					break;
				}

				case 8:
				{
					if (msg->what == B_GET_PROPERTY) {
						BAutolock _(fPlaylist);
						const PlaylistItem* item = fController->Item();
						if (item == NULL) {
							result = B_NO_INIT;
							break;
						}

						result = reply.AddString("result", item->LocationURI());
					}

					break;
				}

				default:
					return BWindow::MessageReceived(msg);
			}

			if (result != B_OK) {
				reply.what = B_MESSAGE_NOT_UNDERSTOOD;
				reply.AddString("message", strerror(result));
				reply.AddInt32("error", result);
			}

			msg->SendReply(&reply);
			break;
		}

		case B_REFS_RECEIVED:
			printf("MainWin::MessageReceived: B_REFS_RECEIVED\n");
			_RefsReceived(msg);
			break;
		case B_SIMPLE_DATA:
			printf("MainWin::MessageReceived: B_SIMPLE_DATA\n");
			if (msg->HasRef("refs")) {
				// add to recent documents as it's not done with drag-n-drop
				entry_ref ref;
				for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
					be_roster->AddToRecentDocuments(&ref, kAppSig);
				}
				_RefsReceived(msg);
			}
			break;
		case M_OPEN_PREVIOUS_PLAYLIST:
			OpenPlaylist(msg);
			break;

		case B_UNDO:
		case B_REDO:
			fPlaylistWindow->PostMessage(msg);
			break;

		case M_MEDIA_SERVER_STARTED:
		{
			printf("TODO: implement M_MEDIA_SERVER_STARTED\n");
//
//			BAutolock _(fPlaylist);
//			BMessage fakePlaylistMessage(MSG_PLAYLIST_CURRENT_ITEM_CHANGED);
//			fakePlaylistMessage.AddInt32("index",
//				fPlaylist->CurrentItemIndex());
//			PostMessage(&fakePlaylistMessage);
			break;
		}

		case M_MEDIA_SERVER_QUIT:
			printf("TODO: implement M_MEDIA_SERVER_QUIT\n");
//			if (fController->Lock()) {
//				fController->CleanupNodes();
//				fController->Unlock();
//			}
			break;

		// PlaylistObserver messages
		case MSG_PLAYLIST_ITEM_ADDED:
		{
			PlaylistItem* item;
			int32 index;
			if (msg->FindPointer("item", (void**)&item) == B_OK
				&& msg->FindInt32("index", &index) == B_OK) {
				_AddPlaylistItem(item, index);
			}
			break;
		}
		case MSG_PLAYLIST_ITEM_REMOVED:
		{
			int32 index;
			if (msg->FindInt32("index", &index) == B_OK)
				_RemovePlaylistItem(index);
			break;
		}
		case MSG_PLAYLIST_CURRENT_ITEM_CHANGED:
		{
			BAutolock _(fPlaylist);

			int32 index;
			if (msg->FindInt32("index", &index) < B_OK
				|| index != fPlaylist->CurrentItemIndex())
				break;
			PlaylistItemRef item(fPlaylist->ItemAt(index));
			if (item.Get() != NULL) {
				printf("open playlist item: %s\n", item->Name().String());
				OpenPlaylistItem(item);
				_MarkPlaylistItem(index);
			}
			break;
		}

		// ControllerObserver messages
		case MSG_CONTROLLER_FILE_FINISHED:
		{
			BAutolock _(fPlaylist);

			bool hadNext = fPlaylist->SetCurrentItemIndex(
				fPlaylist->CurrentItemIndex() + 1);
			if (!hadNext) {
				if (fHasVideo) {
					if (fCloseWhenDonePlayingMovie)
						PostMessage(B_QUIT_REQUESTED);
				} else {
					if (fCloseWhenDonePlayingSound)
						PostMessage(B_QUIT_REQUESTED);
				}
			}
			break;
		}
		case MSG_CONTROLLER_FILE_CHANGED:
		{
			status_t result = B_ERROR;
			msg->FindInt32("result", &result);
			PlaylistItemRef itemRef;
			PlaylistItem* item;
			if (msg->FindPointer("item", (void**)&item) == B_OK) {
				itemRef.SetTo(item, true);
					// The reference was passed along with the message.
			} else {
				BAutolock _(fPlaylist);
				itemRef.SetTo(fPlaylist->ItemAt(
					fPlaylist->CurrentItemIndex()));
			}
			_PlaylistItemOpened(itemRef, result);
			break;
		}
		case MSG_CONTROLLER_VIDEO_TRACK_CHANGED:
		{
			int32 index;
			if (msg->FindInt32("index", &index) == B_OK) {
				int32 i = 0;
				while (BMenuItem* item = fVideoTrackMenu->ItemAt(i)) {
					item->SetMarked(i == index);
					i++;
				}
			}
			break;
		}
		case MSG_CONTROLLER_AUDIO_TRACK_CHANGED:
		{
			int32 index;
			if (msg->FindInt32("index", &index) == B_OK) {
				int32 i = 0;
				while (BMenuItem* item = fAudioTrackMenu->ItemAt(i)) {
					item->SetMarked(i == index);
					i++;
				}
			}
			break;
		}
		case MSG_CONTROLLER_PLAYBACK_STATE_CHANGED:
		{
			uint32 state;
			if (msg->FindInt32("state", (int32*)&state) == B_OK)
				fControls->SetPlaybackState(state);
			break;
		}
		case MSG_CONTROLLER_POSITION_CHANGED:
		{
			float position;
			if (msg->FindFloat("position", &position) == B_OK) {
				fControls->SetPosition(position, fController->TimePosition(),
					fController->TimeDuration());
			}
			break;
		}
		case MSG_CONTROLLER_VOLUME_CHANGED:
		{
			float volume;
			if (msg->FindFloat("volume", &volume) == B_OK)
				fControls->SetVolume(volume);
			break;
		}
		case MSG_CONTROLLER_MUTED_CHANGED:
		{
			bool muted;
			if (msg->FindBool("muted", &muted) == B_OK)
				fControls->SetMuted(muted);
			break;
		}

		// menu item messages
		case M_FILE_NEWPLAYER:
			gMainApp->NewWindow();
			break;
		case M_FILE_OPEN:
		{
			BMessenger target(this);
			BMessage result(B_REFS_RECEIVED);
			BMessage appMessage(M_SHOW_OPEN_PANEL);
			appMessage.AddMessenger("target", target);
			appMessage.AddMessage("message", &result);
			appMessage.AddString("title", "Open Clips");
			appMessage.AddString("label", "Open");
			be_app->PostMessage(&appMessage);
			break;
		}
		case M_FILE_INFO:
			ShowFileInfo();
			break;
		case M_FILE_PLAYLIST:
			ShowPlaylistWindow();
			break;
		case B_ABOUT_REQUESTED:
			be_app->PostMessage(msg);
			break;
		case M_FILE_CLOSE:
			PostMessage(B_QUIT_REQUESTED);
			break;
		case M_FILE_QUIT:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;

		case M_TOGGLE_FULLSCREEN:
			_ToggleFullscreen();
			break;

		case M_TOGGLE_ALWAYS_ON_TOP:
			_ToggleAlwaysOnTop();
			break;

		case M_TOGGLE_NO_INTERFACE:
			_ToggleNoInterface();
			break;

		case M_VIEW_SIZE:
		{
			int32 size;
			if (msg->FindInt32("size", &size) == B_OK) {
				if (!fHasVideo)
					break;
				if (fIsFullscreen)
					_ToggleFullscreen();
				_ResizeWindow(size);
			}
			break;
		}

/*
		case B_ACQUIRE_OVERLAY_LOCK:
			printf("B_ACQUIRE_OVERLAY_LOCK\n");
			fVideoView->OverlayLockAcquire();
			break;

		case B_RELEASE_OVERLAY_LOCK:
			printf("B_RELEASE_OVERLAY_LOCK\n");
			fVideoView->OverlayLockRelease();
			break;
*/
		case B_MOUSE_WHEEL_CHANGED:
		{
			float dx = msg->FindFloat("be:wheel_delta_x");
			float dy = msg->FindFloat("be:wheel_delta_y");
			bool inv = modifiers() & B_COMMAND_KEY;
			if (dx > 0.1)	PostMessage(inv ? M_VOLUME_DOWN : M_SKIP_PREV);
			if (dx < -0.1)	PostMessage(inv ? M_VOLUME_UP : M_SKIP_NEXT);
			if (dy > 0.1)	PostMessage(inv ? M_SKIP_PREV : M_VOLUME_DOWN);
			if (dy < -0.1)	PostMessage(inv ? M_SKIP_NEXT : M_VOLUME_UP);
			break;
		}

		case M_SKIP_NEXT:
			fControls->SkipForward();
			break;

		case M_SKIP_PREV:
			fControls->SkipBackward();
			break;

		case M_VOLUME_UP:
			fController->VolumeUp();
			break;

		case M_VOLUME_DOWN:
			fController->VolumeDown();
			break;

		case M_ASPECT_SAME_AS_SOURCE:
			if (fHasVideo) {
				int width;
				int height;
				int widthAspect;
				int heightAspect;
				fController->GetSize(&width, &height,
					&widthAspect, &heightAspect);
				VideoFormatChange(width, height, widthAspect, heightAspect);
			}
			break;

		case M_ASPECT_NO_DISTORTION:
			if (fHasVideo) {
				int width;
				int height;
				fController->GetSize(&width, &height);
				VideoFormatChange(width, height, width, height);
			}
			break;

		case M_ASPECT_4_3:
			VideoAspectChange(4, 3);
			break;

		case M_ASPECT_16_9: // 1.77 : 1
			VideoAspectChange(16, 9);
			break;

		case M_ASPECT_83_50: // 1.66 : 1
			VideoAspectChange(83, 50);
			break;

		case M_ASPECT_7_4: // 1.75 : 1
			VideoAspectChange(7, 4);
			break;

		case M_ASPECT_37_20: // 1.85 : 1
			VideoAspectChange(37, 20);
			break;

		case M_ASPECT_47_20: // 2.35 : 1
			VideoAspectChange(47, 20);
			break;

		case M_SET_PLAYLIST_POSITION:
		{
			BAutolock _(fPlaylist);

			int32 index;
			if (msg->FindInt32("index", &index) == B_OK)
				fPlaylist->SetCurrentItemIndex(index);
			break;
		}

		case MSG_OBJECT_CHANGED:
			// received from fGlobalSettingsListener
			// TODO: find out which object, if we ever watch more than
			// the global settings instance...
			_AdoptGlobalSettings();
			break;

		case M_SHOW_IF_NEEDED:
			_ShowIfNeeded();
			break;

		default:
			if (msg->what >= M_SELECT_AUDIO_TRACK
				&& msg->what <= M_SELECT_AUDIO_TRACK_END) {
				fController->SelectAudioTrack(msg->what - M_SELECT_AUDIO_TRACK);
				break;
			}
			if (msg->what >= M_SELECT_VIDEO_TRACK
				&& msg->what <= M_SELECT_VIDEO_TRACK_END) {
				fController->SelectVideoTrack(msg->what - M_SELECT_VIDEO_TRACK);
				break;
			}
			// let BWindow handle the rest
			BWindow::MessageReceived(msg);
	}
}


void
MainWin::WindowActivated(bool active)
{
	fController->PlayerActivated(active);
}


bool
MainWin::QuitRequested()
{
	BMessage message(M_PLAYER_QUIT);
	GetQuitMessage(&message);
	be_app->PostMessage(&message);
	return true;
}


void
MainWin::MenusBeginning()
{
	_SetupVideoAspectItems(fVideoAspectMenu);
}


// #pragma mark -


void
MainWin::OpenPlaylist(const BMessage* playlistArchive)
{
	if (playlistArchive == NULL)
		return;

	BAutolock _(this);
	BAutolock playlistLocker(fPlaylist);

	if (fPlaylist->Unarchive(playlistArchive) != B_OK)
		return;

	int32 currentIndex;
	if (playlistArchive->FindInt32("index", &currentIndex) != B_OK)
		currentIndex = 0;
	fPlaylist->SetCurrentItemIndex(currentIndex);

	playlistLocker.Unlock();

	playlistArchive->FindInt64("position", (int64*)&fInitialSeekPosition);

	if (IsHidden())
		Show();
}


void
MainWin::OpenPlaylistItem(const PlaylistItemRef& item)
{
	status_t ret = fController->SetToAsync(item);
	if (ret != B_OK) {
		fprintf(stderr, "MainWin::OpenPlaylistItem() - Failed to send message "
			"to Controller.\n");
		(new BAlert("error", NAME" encountered an internal error. "
			"The file could not be opened.", "OK"))->Go();
		_PlaylistItemOpened(item, ret);
	} else {
		BString string;
		string << "Opening '" << item->Name() << "'.";
		fControls->SetDisabledString(string.String());

		if (IsHidden()) {
			BMessage showMessage(M_SHOW_IF_NEEDED);
			BMessageRunner::StartSending(BMessenger(this), &showMessage,
				150000, 1);
		}
	}
}


void
MainWin::ShowFileInfo()
{
	if (!fInfoWin)
		fInfoWin = new InfoWin(Frame().LeftTop(), fController);

	if (fInfoWin->Lock()) {
		if (fInfoWin->IsHidden())
			fInfoWin->Show();
		else
			fInfoWin->Activate();
		fInfoWin->Unlock();
	}
}


void
MainWin::ShowPlaylistWindow()
{
	if (fPlaylistWindow->Lock()) {
		// make sure the window shows on the same workspace as ourself
		uint32 workspaces = Workspaces();
		if (fPlaylistWindow->Workspaces() != workspaces)
			fPlaylistWindow->SetWorkspaces(workspaces);

		// show or activate
		if (fPlaylistWindow->IsHidden())
			fPlaylistWindow->Show();
		else
			fPlaylistWindow->Activate();

		fPlaylistWindow->Unlock();
	}
}


void
MainWin::VideoAspectChange(int forcedWidth, int forcedHeight, float widthScale)
{
	// Force specific source size and pixel width scale.
	if (fHasVideo) {
		int width;
		int height;
		fController->GetSize(&width, &height);
		VideoFormatChange(forcedWidth, forcedHeight,
			lround(width * widthScale), height);
	}
}


void
MainWin::VideoAspectChange(float widthScale)
{
	// Called when video aspect ratio changes and the original
	// width/height should be restored too, display aspect is not known,
	// only pixel width scale.
	if (fHasVideo) {
		int width;
		int height;
		fController->GetSize(&width, &height);
		VideoFormatChange(width, height, lround(width * widthScale), height);
	}
}


void
MainWin::VideoAspectChange(int widthAspect, int heightAspect)
{
	// Called when video aspect ratio changes and the original
	// width/height should be restored too.
	if (fHasVideo) {
		int width;
		int height;
		fController->GetSize(&width, &height);
		VideoFormatChange(width, height, widthAspect, heightAspect);
	}
}


void
MainWin::VideoFormatChange(int width, int height, int widthAspect,
	int heightAspect)
{
	// Called when video format or aspect ratio changes.

	printf("VideoFormatChange enter: width %d, height %d, "
		"aspect ratio: %d:%d\n", width, height, widthAspect, heightAspect);

	// remember current view scale
	int percent = _CurrentVideoSizeInPercent();

 	fSourceWidth = width;
 	fSourceHeight = height;
 	fWidthAspect = widthAspect;
 	fHeightAspect = heightAspect;

	if (percent == 100)
		_ResizeWindow(100);
	else
	 	FrameResized(Bounds().Width(), Bounds().Height());

	printf("VideoFormatChange leave\n");
}


void
MainWin::GetQuitMessage(BMessage* message)
{
	message->AddPointer("instance", this);
	message->AddRect("window frame", Frame());
	message->AddBool("audio only", !fHasVideo);
	message->AddInt64("creation time", fCreationTime);
	if (!fHasVideo && fHasAudio) {
		// store playlist, current index and position if this is audio
		BMessage playlistArchive;

		BAutolock controllerLocker(fController);
		playlistArchive.AddInt64("position", fController->TimePosition());
		controllerLocker.Unlock();

		if (!fPlaylist)
			return;
		BAutolock playlistLocker(fPlaylist);
		if (fPlaylist->Archive(&playlistArchive) != B_OK
			|| playlistArchive.AddInt32("index",
				fPlaylist->CurrentItemIndex()) != B_OK
			|| message->AddMessage("playlist", &playlistArchive) != B_OK) {
			fprintf(stderr, "Failed to store current playlist.\n");
		}
	}
}


BHandler*
MainWin::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	BPropertyInfo propertyInfo(sPropertyInfo);
	switch (propertyInfo.FindMatch(message, index, specifier, what, property)) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			return this;
	}

	return BWindow::ResolveSpecifier(message, index, specifier, what, property);
}


status_t
MainWin::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("suites", "suite/vnd.Haiku-MediaPlayer");
	if (status != B_OK)
		return status;

	BPropertyInfo propertyInfo(sPropertyInfo);
	status = data->AddFlat("messages", &propertyInfo);
	if (status != B_OK)
		return status;

	return BWindow::GetSupportedSuites(data);
}


// #pragma mark -


void
MainWin::_RefsReceived(BMessage* message)
{
	// the playlist is replaced by dropped files
	// or the dropped files are appended to the end
	// of the existing playlist if <shift> is pressed
	bool append = false;
	if (message->FindBool("append to playlist", &append) != B_OK)
		append = modifiers() & B_SHIFT_KEY;

	BAutolock _(fPlaylist);
	int32 appendIndex = append ? APPEND_INDEX_APPEND_LAST
		: APPEND_INDEX_REPLACE_PLAYLIST;
	message->AddInt32("append_index", appendIndex);

	// forward the message to the playlist window,
	// so that undo/redo is used for modifying the playlist
	fPlaylistWindow->PostMessage(message);

	if (message->FindRect("window frame", &fNoVideoFrame) != B_OK) {
		fNoVideoFrame = BRect();
		_ShowIfNeeded();
	}
}


void
MainWin::_PlaylistItemOpened(const PlaylistItemRef& item, status_t result)
{
	if (result != B_OK) {
		BAutolock _(fPlaylist);

		item->SetPlaybackFailed();
		bool allItemsFailed = true;
		int32 count = fPlaylist->CountItems();
		for (int32 i = 0; i < count; i++) {
			if (!fPlaylist->ItemAtFast(i)->PlaybackFailed()) {
				allItemsFailed = false;
				break;
			}
		}

		if (allItemsFailed) {
			// Display error if all files failed to play.
			BString message;
			message << "The file '";
			message << item->Name();
			message << "' could not be opened.\n\n";

			if (result == B_MEDIA_NO_HANDLER) {
				// give a more detailed message for the most likely of all
				// errors
				message << "There is no decoder installed to handle the "
					"file format, or the decoder has trouble with the "
					"specific version of the format.";
			} else {
				message << "Error: " << strerror(result);
			}
			(new BAlert("error", message.String(), "OK"))->Go();
		} else {
			// Just go to the next file and don't bother user (yet)
			fPlaylist->SetCurrentItemIndex(fPlaylist->CurrentItemIndex() + 1);
		}

		fHasFile = false;
		fHasVideo = false;
		fHasAudio = false;
		SetTitle(NAME);
	} else {
		fHasFile = true;
		fHasVideo = fController->VideoTrackCount() != 0;
		fHasAudio = fController->AudioTrackCount() != 0;
		SetTitle(item->Name().String());
		fController->SetTimePosition(fInitialSeekPosition);
		fInitialSeekPosition = 0;
	}
	_SetupWindow();

	if (result == B_OK)
		_SetFileAttributes();
}


void
MainWin::_SetupWindow()
{
//	printf("MainWin::_SetupWindow\n");
	// Populate the track menus
	_SetupTrackMenus(fAudioTrackMenu, fVideoTrackMenu);
	// Enable both if a file was loaded
	fAudioTrackMenu->SetEnabled(fHasFile);
	fVideoTrackMenu->SetEnabled(fHasFile);

	fVideoMenu->SetEnabled(fHasVideo);
	fAudioMenu->SetEnabled(fHasAudio);
	int previousSourceWidth = fSourceWidth;
	int previousSourceHeight = fSourceHeight;
	int previousWidthAspect = fWidthAspect;
	int previousHeightAspect = fHeightAspect;
	if (fHasVideo) {
		fController->GetSize(&fSourceWidth, &fSourceHeight,
			&fWidthAspect, &fHeightAspect);
	} else {
		fSourceWidth = 0;
		fSourceHeight = 0;
		fWidthAspect = 1;
		fHeightAspect = 1;
	}
	_UpdateControlsEnabledStatus();

	if (!fHasVideo && fNoVideoFrame.IsValid()) {
		MoveTo(fNoVideoFrame.LeftTop());
		ResizeTo(fNoVideoFrame.Width(), fNoVideoFrame.Height());
	}
	fNoVideoFrame = BRect();
	_ShowIfNeeded();

	// Adopt the size and window layout if necessary
	if (previousSourceWidth != fSourceWidth
		|| previousSourceHeight != fSourceHeight
		|| previousWidthAspect != fWidthAspect
		|| previousHeightAspect != fHeightAspect) {

		_SetWindowSizeLimits();

		if (!fIsFullscreen) {
			// Resize to 100% but stay on screen
			_ResizeWindow(100, !fHasVideo, true);
		} else {
			// Make sure we relayout the video view when in full screen mode
			FrameResized(Frame().Width(), Frame().Height());
		}
	}

	fVideoView->MakeFocus();
}


void
MainWin::_CreateMenu()
{
	fFileMenu = new BMenu(NAME);
	fPlaylistMenu = new BMenu("Playlist"B_UTF8_ELLIPSIS);
	fAudioMenu = new BMenu("Audio");
	fVideoMenu = new BMenu("Video");
	fVideoAspectMenu = new BMenu("Aspect ratio");
	fSettingsMenu = new BMenu("Settings");
	fAudioTrackMenu = new BMenu("Track");
	fVideoTrackMenu = new BMenu("Track");

	fMenuBar->AddItem(fFileMenu);
	fMenuBar->AddItem(fAudioMenu);
	fMenuBar->AddItem(fVideoMenu);
	fMenuBar->AddItem(fSettingsMenu);

	fFileMenu->AddItem(new BMenuItem("New player"B_UTF8_ELLIPSIS,
		new BMessage(M_FILE_NEWPLAYER), 'N'));
	fFileMenu->AddSeparatorItem();

//	fFileMenu->AddItem(new BMenuItem("Open File"B_UTF8_ELLIPSIS,
//		new BMessage(M_FILE_OPEN), 'O'));
	// Add recent files
	BRecentFilesList recentFiles(10, false, NULL, kAppSig);
	BMenuItem *item = new BMenuItem(recentFiles.NewFileListMenu(
		"Open file"B_UTF8_ELLIPSIS, new BMessage(B_REFS_RECEIVED),
		NULL, this, 10, false, NULL, 0, kAppSig), new BMessage(M_FILE_OPEN));
	item->SetShortcut('O', 0);
	fFileMenu->AddItem(item);

	fFileMenu->AddItem(new BMenuItem("File info"B_UTF8_ELLIPSIS,
		new BMessage(M_FILE_INFO), 'I'));
	fFileMenu->AddItem(fPlaylistMenu);
	fPlaylistMenu->Superitem()->SetShortcut('P', B_COMMAND_KEY);
	fPlaylistMenu->Superitem()->SetMessage(new BMessage(M_FILE_PLAYLIST));

	fFileMenu->AddSeparatorItem();
	fFileMenu->AddItem(new BMenuItem("About " NAME B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));
	fFileMenu->AddSeparatorItem();
	fFileMenu->AddItem(new BMenuItem("Close", new BMessage(M_FILE_CLOSE), 'W'));
	fFileMenu->AddItem(new BMenuItem("Quit", new BMessage(M_FILE_QUIT), 'Q'));

	fPlaylistMenu->SetRadioMode(true);

	fAudioMenu->AddItem(fAudioTrackMenu);

	fVideoMenu->AddItem(fVideoTrackMenu);
	fVideoMenu->AddSeparatorItem();
	BMessage* resizeMessage = new BMessage(M_VIEW_SIZE);
	resizeMessage->AddInt32("size", 50);
	fVideoMenu->AddItem(new BMenuItem("50% scale", resizeMessage, '0'));

	resizeMessage = new BMessage(M_VIEW_SIZE);
	resizeMessage->AddInt32("size", 100);
	fVideoMenu->AddItem(new BMenuItem("100% scale", resizeMessage, '1'));

	resizeMessage = new BMessage(M_VIEW_SIZE);
	resizeMessage->AddInt32("size", 200);
	fVideoMenu->AddItem(new BMenuItem("200% scale", resizeMessage, '2'));

	resizeMessage = new BMessage(M_VIEW_SIZE);
	resizeMessage->AddInt32("size", 300);
	fVideoMenu->AddItem(new BMenuItem("300% scale", resizeMessage, '3'));

	resizeMessage = new BMessage(M_VIEW_SIZE);
	resizeMessage->AddInt32("size", 400);
	fVideoMenu->AddItem(new BMenuItem("400% scale", resizeMessage, '4'));

	fVideoMenu->AddSeparatorItem();

	fVideoMenu->AddItem(new BMenuItem("Full screen",
		new BMessage(M_TOGGLE_FULLSCREEN), 'F'));

	fVideoMenu->AddSeparatorItem();

	_SetupVideoAspectItems(fVideoAspectMenu);
	fVideoMenu->AddItem(fVideoAspectMenu);

	fNoInterfaceMenuItem = new BMenuItem("No interface",
		new BMessage(M_TOGGLE_NO_INTERFACE), 'B');
	fSettingsMenu->AddItem(fNoInterfaceMenuItem);
	fSettingsMenu->AddItem(new BMenuItem("Always on top",
		new BMessage(M_TOGGLE_ALWAYS_ON_TOP), 'T'));
	fSettingsMenu->AddSeparatorItem();
	item = new BMenuItem("Settings"B_UTF8_ELLIPSIS,
		new BMessage(M_SETTINGS), 'S');
	fSettingsMenu->AddItem(item);
	item->SetTarget(be_app);
}


void
MainWin::_SetupVideoAspectItems(BMenu* menu)
{
	BMenuItem* item;
	while ((item = menu->RemoveItem(0L)) != NULL)
		delete item;

	int width;
	int height;
	int widthAspect;
	int heightAspect;
	fController->GetSize(&width, &height, &widthAspect, &heightAspect);
		// We don't care if there is a video track at all. In that
		// case we should end up not marking any item.

	// NOTE: The item marking may end up marking for example both
	// "Stream Settings" and "16 : 9" if the stream settings happen to
	// be "16 : 9".

	menu->AddItem(item = new BMenuItem("Stream settings",
		new BMessage(M_ASPECT_SAME_AS_SOURCE)));
	item->SetMarked(widthAspect == fWidthAspect
		&& heightAspect == fHeightAspect);

	menu->AddItem(item = new BMenuItem("No aspect correction",
		new BMessage(M_ASPECT_NO_DISTORTION)));
	item->SetMarked(width == fWidthAspect && height == fHeightAspect);

	menu->AddSeparatorItem();

	menu->AddItem(item = new BMenuItem("4 : 3",
		new BMessage(M_ASPECT_4_3)));
	item->SetMarked(fWidthAspect == 4 && fHeightAspect == 3);
	menu->AddItem(item = new BMenuItem("16 : 9",
		new BMessage(M_ASPECT_16_9)));
	item->SetMarked(fWidthAspect == 16 && fHeightAspect == 9);

	menu->AddSeparatorItem();

	menu->AddItem(item = new BMenuItem("1.66 : 1",
		new BMessage(M_ASPECT_83_50)));
	item->SetMarked(fWidthAspect == 83 && fHeightAspect == 50);
	menu->AddItem(item = new BMenuItem("1.75 : 1",
		new BMessage(M_ASPECT_7_4)));
	item->SetMarked(fWidthAspect == 7 && fHeightAspect == 4);
	menu->AddItem(item = new BMenuItem("1.85 : 1 (American)",
		new BMessage(M_ASPECT_37_20)));
	item->SetMarked(fWidthAspect == 37 && fHeightAspect == 20);
	menu->AddItem(item = new BMenuItem("2.35 : 1 (Cinemascope)",
		new BMessage(M_ASPECT_47_20)));
	item->SetMarked(fWidthAspect == 47 && fHeightAspect == 20);
}


void
MainWin::_SetupTrackMenus(BMenu* audioTrackMenu, BMenu* videoTrackMenu)
{
	audioTrackMenu->RemoveItems(0, audioTrackMenu->CountItems(), true);
	videoTrackMenu->RemoveItems(0, videoTrackMenu->CountItems(), true);

	char s[100];

	int count = fController->AudioTrackCount();
	int current = fController->CurrentAudioTrack();
	for (int i = 0; i < count; i++) {
		sprintf(s, "Track %d", i + 1);
		BMenuItem* item = new BMenuItem(s,
			new BMessage(M_SELECT_AUDIO_TRACK + i));
		item->SetMarked(i == current);
		audioTrackMenu->AddItem(item);
	}
	if (!count) {
		audioTrackMenu->AddItem(new BMenuItem("none", new BMessage(M_DUMMY)));
		audioTrackMenu->ItemAt(0)->SetMarked(true);
	}


	count = fController->VideoTrackCount();
	current = fController->CurrentVideoTrack();
	for (int i = 0; i < count; i++) {
		sprintf(s, "Track %d", i + 1);
		BMenuItem* item = new BMenuItem(s,
			new BMessage(M_SELECT_VIDEO_TRACK + i));
		item->SetMarked(i == current);
		videoTrackMenu->AddItem(item);
	}
	if (!count) {
		videoTrackMenu->AddItem(new BMenuItem("none", new BMessage(M_DUMMY)));
		videoTrackMenu->ItemAt(0)->SetMarked(true);
	}
}


void
MainWin::_GetMinimumWindowSize(int& width, int& height) const
{
	width = MIN_WIDTH;
	height = 0;
	if (!fNoInterface) {
		width = max_c(width, fMenuBarWidth);
		width = max_c(width, fControlsWidth);
		height = fMenuBarHeight + fControlsHeight;
	}
}


void
MainWin::_GetUnscaledVideoSize(int& videoWidth, int& videoHeight) const
{
	if (fWidthAspect != 0 && fHeightAspect != 0) {
		videoWidth = fSourceHeight * fWidthAspect / fHeightAspect;
		videoHeight = fSourceWidth * fHeightAspect / fWidthAspect;
		// Use the scaling which produces an enlarged view.
		if (videoWidth > fSourceWidth) {
			// Enlarge width
			videoHeight = fSourceHeight;
		} else {
			// Enlarge height
			videoWidth = fSourceWidth;
		}
	} else {
		videoWidth = fSourceWidth;
		videoHeight = fSourceHeight;
	}
}

void
MainWin::_SetWindowSizeLimits()
{
	int minWidth;
	int minHeight;
	_GetMinimumWindowSize(minWidth, minHeight);
	SetSizeLimits(minWidth - 1, 32000, minHeight - 1, fHasVideo ?
		32000 : minHeight - 1);
}


int
MainWin::_CurrentVideoSizeInPercent() const
{
	if (!fHasVideo)
		return 0;

	int videoWidth;
	int videoHeight;
	_GetUnscaledVideoSize(videoWidth, videoHeight);

	int viewWidth = fVideoView->Bounds().IntegerWidth() + 1;
	int viewHeight = fVideoView->Bounds().IntegerHeight() + 1;

	int widthPercent = videoWidth * 100 / viewWidth;
	int heightPercent = videoHeight * 100 / viewHeight;

	if (widthPercent > heightPercent)
		return widthPercent;
	return heightPercent;
}


void
MainWin::_ResizeWindow(int percent, bool useNoVideoWidth, bool stayOnScreen)
{
	// Get required window size
	int videoWidth;
	int videoHeight;
	_GetUnscaledVideoSize(videoWidth, videoHeight);

	videoWidth = (videoWidth * percent) / 100;
	videoHeight = (videoHeight * percent) / 100;

	// Calculate and set the minimum window size
	int width;
	int height;
	_GetMinimumWindowSize(width, height);

	width = max_c(width, videoWidth) - 1;
	if (useNoVideoWidth)
		width = max_c(width, fNoVideoWidth);
	height = height + videoHeight - 1;

	if (stayOnScreen) {
		BRect screenFrame(BScreen(this).Frame());
		BRect frame(Frame());
		BRect decoratorFrame(DecoratorFrame());

		// Shrink the screen frame by the window border size
		screenFrame.top += frame.top - decoratorFrame.top;
		screenFrame.left += frame.left - decoratorFrame.left;
		screenFrame.right += frame.right - decoratorFrame.right;
		screenFrame.bottom += frame.bottom - decoratorFrame.bottom;

		// Update frame to what the new size would be
		frame.right = frame.left + width;
		frame.bottom = frame.top + height;

		if (!screenFrame.Contains(frame)) {
			// Resize the window so it doesn't extend outside the current
			// screen frame.
			if (frame.Width() > screenFrame.Width()
				|| frame.Height() > screenFrame.Height()) {
				// too large
				int widthDiff
					= frame.IntegerWidth() - screenFrame.IntegerWidth();
				int heightDiff
					= frame.IntegerHeight() - screenFrame.IntegerHeight();

				float shrinkScale;
				if (widthDiff > heightDiff)
					shrinkScale = (float)(width - widthDiff) / width;
				else
					shrinkScale = (float)(height - heightDiff) / height;

				// Resize width/height and center window
				width = lround(width * shrinkScale);
				height = lround(height * shrinkScale);
				MoveTo((screenFrame.left + screenFrame.right - width) / 2,
					(screenFrame.top + screenFrame.bottom - height) / 2);
			} else {
				// just off-screen on one or more sides
				int offsetX = 0;
				int offsetY = 0;
				if (frame.left < screenFrame.left)
					offsetX = (int)(screenFrame.left - frame.left);
				else if (frame.right > screenFrame.right)
					offsetX = (int)(screenFrame.right - frame.right);
				if (frame.top < screenFrame.top)
					offsetY = (int)(screenFrame.top - frame.top);
				else if (frame.bottom > screenFrame.bottom)
					offsetY = (int)(screenFrame.bottom - frame.bottom);
				MoveBy(offsetX, offsetY);
			}
		}
	}

	ResizeTo(width, height);
}


void
MainWin::_ResizeVideoView(int x, int y, int width, int height)
{
	printf("_ResizeVideoView: %d,%d, width %d, height %d\n", x, y,
		width, height);

	// Keep aspect ratio, place video view inside
	// the background area (may create black bars).
	int videoWidth;
	int videoHeight;
	_GetUnscaledVideoSize(videoWidth, videoHeight);
	float scaledWidth  = videoWidth;
	float scaledHeight = videoHeight;
	float factor = min_c(width / scaledWidth, height / scaledHeight);
	int renderWidth = lround(scaledWidth * factor);
	int renderHeight = lround(scaledHeight * factor);
	if (renderWidth > width)
		renderWidth = width;
	if (renderHeight > height)
		renderHeight = height;

	int xOffset = x + (width - renderWidth) / 2;
	int yOffset = y + (height - renderHeight) / 2;

	fVideoView->MoveTo(xOffset, yOffset);
	fVideoView->ResizeTo(renderWidth - 1, renderHeight - 1);
}


// #pragma mark -


void
MainWin::_MouseDown(BMessage *msg, BView* originalHandler)
{
	BPoint screen_where;
	uint32 buttons = msg->FindInt32("buttons");

	// On Zeta, only "screen_where" is relyable, "where" and "be:view_where"
	// seem to be broken
	if (B_OK != msg->FindPoint("screen_where", &screen_where)) {
		// Workaround for BeOS R5, it has no "screen_where"
		if (!originalHandler || msg->FindPoint("where", &screen_where) < B_OK)
			return;
		originalHandler->ConvertToScreen(&screen_where);
	}

//	msg->PrintToStream();

//	if (1 == msg->FindInt32("buttons") && msg->FindInt32("clicks") == 1) {

	if (1 == buttons && msg->FindInt32("clicks") % 2 == 0) {
		BRect r(screen_where.x - 1, screen_where.y - 1, screen_where.x + 1,
			screen_where.y + 1);
		if (r.Contains(fMouseDownMousePos)) {
			PostMessage(M_TOGGLE_FULLSCREEN);
			return;
		}
	}

	if (2 == buttons && msg->FindInt32("clicks") % 2 == 0) {
		BRect r(screen_where.x - 1, screen_where.y - 1, screen_where.x + 1,
			screen_where.y + 1);
		if (r.Contains(fMouseDownMousePos)) {
			PostMessage(M_TOGGLE_NO_INTERFACE);
			return;
		}
	}

/*
		// very broken in Zeta:
		fMouseDownMousePos = fVideoView->ConvertToScreen(
			msg->FindPoint("where"));
*/
	fMouseDownMousePos = screen_where;
	fMouseDownWindowPos = Frame().LeftTop();

	if (buttons == 1 && !fIsFullscreen) {
		// start mouse tracking
		fVideoView->SetMouseEventMask(B_POINTER_EVENTS | B_NO_POINTER_HISTORY
			/* | B_LOCK_WINDOW_FOCUS */);
		fMouseDownTracking = true;
	}

	// pop up a context menu if right mouse button is down for 200 ms

	if ((buttons & 2) == 0)
		return;
	bigtime_t start = system_time();
	bigtime_t delay = 200000;
	BPoint location;
	do {
		fVideoView->GetMouse(&location, &buttons);
		if ((buttons & 2) == 0)
			break;
		snooze(1000);
	} while (system_time() - start < delay);

	if (buttons & 2)
		_ShowContextMenu(screen_where);
}


void
MainWin::_MouseMoved(BMessage *msg, BView* originalHandler)
{
//	msg->PrintToStream();

	BPoint mousePos;
	uint32 buttons = msg->FindInt32("buttons");

	if (1 == buttons && fMouseDownTracking && !fIsFullscreen) {
/*
		// very broken in Zeta:
		BPoint mousePos = msg->FindPoint("where");
		printf("view where: %.0f, %.0f => ", mousePos.x, mousePos.y);
		fVideoView->ConvertToScreen(&mousePos);
*/
		// On Zeta, only "screen_where" is relyable, "where"
		// and "be:view_where" seem to be broken
		if (B_OK != msg->FindPoint("screen_where", &mousePos)) {
			// Workaround for BeOS R5, it has no "screen_where"
			if (!originalHandler || msg->FindPoint("where", &mousePos) < B_OK)
				return;
			originalHandler->ConvertToScreen(&mousePos);
		}
//		printf("screen where: %.0f, %.0f => ", mousePos.x, mousePos.y);
		float delta_x = mousePos.x - fMouseDownMousePos.x;
		float delta_y = mousePos.y - fMouseDownMousePos.y;
		float x = fMouseDownWindowPos.x + delta_x;
		float y = fMouseDownWindowPos.y + delta_y;
//		printf("move window to %.0f, %.0f\n", x, y);
		MoveTo(x, y);
	}
}


void
MainWin::_MouseUp(BMessage *msg)
{
//	msg->PrintToStream();
	fMouseDownTracking = false;
}


void
MainWin::_ShowContextMenu(const BPoint &screen_point)
{
	printf("Show context menu\n");
	BPopUpMenu *menu = new BPopUpMenu("context menu", false, false);
	BMenuItem *item;
	menu->AddItem(item = new BMenuItem("Full screen",
		new BMessage(M_TOGGLE_FULLSCREEN), 'F'));
	item->SetMarked(fIsFullscreen);
	item->SetEnabled(fHasVideo);

	BMenu* aspectSubMenu = new BMenu("Aspect ratio");
	_SetupVideoAspectItems(aspectSubMenu);
	aspectSubMenu->SetTargetForItems(this);
	menu->AddItem(item = new BMenuItem(aspectSubMenu));
	item->SetEnabled(fHasVideo);

	menu->AddItem(item = new BMenuItem("No interface",
		new BMessage(M_TOGGLE_NO_INTERFACE), 'B'));
	item->SetMarked(fNoInterface);
	item->SetEnabled(fHasVideo);

	menu->AddSeparatorItem();

	// Add track selector menus
	BMenu* audioTrackMenu = new BMenu("Audio track");
	BMenu* videoTrackMenu = new BMenu("Video track");
	_SetupTrackMenus(audioTrackMenu, videoTrackMenu);

	audioTrackMenu->SetTargetForItems(this);
	videoTrackMenu->SetTargetForItems(this);

	menu->AddItem(item = new BMenuItem(audioTrackMenu));
	item->SetEnabled(fHasAudio);

	menu->AddItem(item = new BMenuItem(videoTrackMenu));
	item->SetEnabled(fHasVideo);

	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem("About " NAME B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(M_FILE_QUIT), 'Q'));

	menu->SetTargetForItems(this);
	BRect r(screen_point.x - 5, screen_point.y - 5, screen_point.x + 5,
		screen_point.y + 5);
	menu->Go(screen_point, true, true, r, true);
}


/*!	Trap keys that are about to be send to background or renderer view.
	Return true if it shouldn't be passed to the view.
*/
bool
MainWin::_KeyDown(BMessage *msg)
{
	// TODO: use the shortcut mechanism instead!

	uint32 key = msg->FindInt32("key");
	uint32 rawChar = msg->FindInt32("raw_char");
	uint32 modifier = msg->FindInt32("modifiers");

	printf("key 0x%lx, rawChar 0x%lx, modifiers 0x%lx\n", key, rawChar,
		modifier);

	// ignore the system modifier namespace
	if ((modifier & (B_CONTROL_KEY | B_COMMAND_KEY))
			== (B_CONTROL_KEY | B_COMMAND_KEY))
		return false;

	switch (rawChar) {
		case B_SPACE:
			fController->TogglePlaying();
			return true;

		case B_ESCAPE:
			if (!fIsFullscreen)
				break;

			PostMessage(M_TOGGLE_FULLSCREEN);
			return true;

		case B_ENTER:		// Enter / Return
			if (modifier & B_COMMAND_KEY) {
				PostMessage(M_TOGGLE_FULLSCREEN);
				return true;
			} else
				break;

		case B_TAB:
			if ((modifier & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY
					| B_MENU_KEY)) == 0) {
				PostMessage(M_TOGGLE_FULLSCREEN);
				return true;
			} else
				break;

		case B_UP_ARROW:
			if ((modifier & B_COMMAND_KEY) != 0)
				PostMessage(M_SKIP_NEXT);
			else
				PostMessage(M_VOLUME_UP);
			return true;

		case B_DOWN_ARROW:
			if ((modifier & B_COMMAND_KEY) != 0)
				PostMessage(M_SKIP_PREV);
			else
				PostMessage(M_VOLUME_DOWN);
			return true;

		case B_RIGHT_ARROW:
			if ((modifier & B_COMMAND_KEY) != 0)
				PostMessage(M_VOLUME_UP);
			else
				PostMessage(M_SKIP_NEXT);
			return true;

		case B_LEFT_ARROW:
			if ((modifier & B_COMMAND_KEY) != 0)
				PostMessage(M_VOLUME_DOWN);
			else
				PostMessage(M_SKIP_PREV);
			return true;

		case B_PAGE_UP:
			PostMessage(M_SKIP_NEXT);
			return true;

		case B_PAGE_DOWN:
			PostMessage(M_SKIP_PREV);
			return true;
	}

	switch (key) {
		case 0x3a:  		// numeric keypad +
			if ((modifier & B_COMMAND_KEY) == 0) {
				PostMessage(M_VOLUME_UP);
				return true;
			} else {
				break;
			}

		case 0x25:  		// numeric keypad -
			if ((modifier & B_COMMAND_KEY) == 0) {
				PostMessage(M_VOLUME_DOWN);
				return true;
			} else {
				break;
			}

		case 0x38:			// numeric keypad up arrow
			PostMessage(M_VOLUME_UP);
			return true;

		case 0x59:			// numeric keypad down arrow
			PostMessage(M_VOLUME_DOWN);
			return true;

		case 0x39:			// numeric keypad page up
		case 0x4a:			// numeric keypad right arrow
			PostMessage(M_SKIP_NEXT);
			return true;

		case 0x5a:			// numeric keypad page down
		case 0x48:			// numeric keypad left arrow
			PostMessage(M_SKIP_PREV);
			return true;

		case 0x34:			// delete button
		case 0x3e: 			// d for delete
		case 0x2b:			// t for Trash
			if ((modifiers() & B_COMMAND_KEY) != 0) {
				BAutolock _(fPlaylist);
				BMessage removeMessage(M_PLAYLIST_REMOVE_AND_PUT_INTO_TRASH);
				removeMessage.AddInt32("playlist index",
					fPlaylist->CurrentItemIndex());
				fPlaylistWindow->PostMessage(&removeMessage);
				return true;
			}
			break;
	}

	return false;
}


// #pragma mark -


void
MainWin::_ToggleFullscreen()
{
	printf("_ToggleFullscreen enter\n");

	if (!fHasVideo) {
		printf("_ToggleFullscreen - ignoring, as we don't have a video\n");
		return;
	}

	fIsFullscreen = !fIsFullscreen;

	if (fIsFullscreen) {
		// switch to fullscreen

		fSavedFrame = Frame();
		printf("saving current frame: %d %d %d %d\n", int(fSavedFrame.left),
			int(fSavedFrame.top), int(fSavedFrame.right),
			int(fSavedFrame.bottom));
		BScreen screen(this);
		BRect rect(screen.Frame());

		Hide();
		MoveTo(rect.left, rect.top);
		ResizeTo(rect.Width(), rect.Height());
		Show();

	} else {
		// switch back from full screen mode

		Hide();
		MoveTo(fSavedFrame.left, fSavedFrame.top);
		ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
		Show();
	}

	fVideoView->SetFullscreen(fIsFullscreen);

	_MarkItem(fSettingsMenu, M_TOGGLE_FULLSCREEN, fIsFullscreen);

	printf("_ToggleFullscreen leave\n");
}

void
MainWin::_ToggleAlwaysOnTop()
{
	fAlwaysOnTop = !fAlwaysOnTop;
	SetFeel(fAlwaysOnTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL);

	_MarkItem(fSettingsMenu, M_TOGGLE_ALWAYS_ON_TOP, fAlwaysOnTop);
}


void
MainWin::_ToggleNoInterface()
{
	printf("_ToggleNoInterface enter\n");

	if (fIsFullscreen || !fHasVideo) {
		// Fullscreen playback is always without interface and
		// audio playback is always with interface. So we ignore these
		// two states here.
		printf("_ToggleNoControls leave, doing nothing, we are fullscreen\n");
		return;
	}

	fNoInterface = !fNoInterface;
	_SetWindowSizeLimits();

	if (fNoInterface) {
		MoveBy(0, fMenuBarHeight);
		ResizeBy(0, -(fControlsHeight + fMenuBarHeight));
		SetLook(B_BORDERED_WINDOW_LOOK);
	} else {
		MoveBy(0, -fMenuBarHeight);
		ResizeBy(0, fControlsHeight + fMenuBarHeight);
		SetLook(B_TITLED_WINDOW_LOOK);
	}

	_MarkItem(fSettingsMenu, M_TOGGLE_NO_INTERFACE, fNoInterface);

	printf("_ToggleNoInterface leave\n");
}


void
MainWin::_ShowIfNeeded()
{
	if (find_thread(NULL) != Thread())
		return;

	if (IsHidden()) {
		Show();
		UpdateIfNeeded();
	}
}


// #pragma mark -


/*!	Sets some standard attributes of the currently played file.
	This should only be a temporary solution.
*/
void
MainWin::_SetFileAttributes()
{
	BAutolock locker(fPlaylist);
	const FilePlaylistItem* item
		= dynamic_cast<const FilePlaylistItem*>(fController->Item());
	if (item == NULL)
		return;

	if (!fHasVideo && fHasAudio) {
		BNode node(&item->Ref());
		if (node.InitCheck())
			return;

		locker.Unlock();

		// write duration

		attr_info info;
		status_t status = node.GetAttrInfo("Audio:Length", &info);
		if (status != B_OK || info.size == 0) {
			time_t duration = fController->TimeDuration() / 1000000L;

			char text[256];
			snprintf(text, sizeof(text), "%02ld:%02ld", duration / 60,
				duration % 60);
			node.WriteAttr("Audio:Length", B_STRING_TYPE, 0, text,
				strlen(text) + 1);
		}

		// write bitrate

		status = node.GetAttrInfo("Audio:Bitrate", &info);
		if (status != B_OK || info.size == 0) {
			media_format format;
			if (fController->GetEncodedAudioFormat(&format) == B_OK
				&& format.type == B_MEDIA_ENCODED_AUDIO) {
				int32 bitrate = (int32)(format.u.encoded_audio.bit_rate / 1000);
				char text[256];
				snprintf(text, sizeof(text), "%ld kbit", bitrate);
				node.WriteAttr("Audio:Bitrate", B_STRING_TYPE, 0, text,
					strlen(text) + 1);
			}
		}
	}
}


void
MainWin::_UpdateControlsEnabledStatus()
{
	uint32 enabledButtons = 0;
	if (fHasVideo || fHasAudio) {
		enabledButtons |= PLAYBACK_ENABLED | SEEK_ENABLED
			| SEEK_BACK_ENABLED | SEEK_FORWARD_ENABLED;
	}
	if (fHasAudio)
		enabledButtons |= VOLUME_ENABLED;

	BAutolock _(fPlaylist);
	bool canSkipPrevious, canSkipNext;
	fPlaylist->GetSkipInfo(&canSkipPrevious, &canSkipNext);
	if (canSkipPrevious)
		enabledButtons |= SKIP_BACK_ENABLED;
	if (canSkipNext)
		enabledButtons |= SKIP_FORWARD_ENABLED;

	fControls->SetEnabled(enabledButtons);

	fNoInterfaceMenuItem->SetEnabled(fHasVideo);
}


void
MainWin::_UpdatePlaylistMenu()
{
	if (!fPlaylist->Lock())
		return;

	fPlaylistMenu->RemoveItems(0, fPlaylistMenu->CountItems(), true);

	int32 count = fPlaylist->CountItems();
	for (int32 i = 0; i < count; i++) {
		PlaylistItem* item = fPlaylist->ItemAtFast(i);
		_AddPlaylistItem(item, i);
	}
	fPlaylistMenu->SetTargetForItems(this);

	_MarkPlaylistItem(fPlaylist->CurrentItemIndex());

	fPlaylist->Unlock();
}


void
MainWin::_AddPlaylistItem(PlaylistItem* item, int32 index)
{
	BMessage* message = new BMessage(M_SET_PLAYLIST_POSITION);
	message->AddInt32("index", index);
	BMenuItem* menuItem = new BMenuItem(item->Name().String(), message);
	fPlaylistMenu->AddItem(menuItem, index);
}


void
MainWin::_RemovePlaylistItem(int32 index)
{
	delete fPlaylistMenu->RemoveItem(index);
}


void
MainWin::_MarkPlaylistItem(int32 index)
{
	if (BMenuItem* item = fPlaylistMenu->ItemAt(index)) {
		item->SetMarked(true);
		// ... and in case the menu is currently on screen:
		if (fPlaylistMenu->LockLooper()) {
			fPlaylistMenu->Invalidate();
			fPlaylistMenu->UnlockLooper();
		}
	}
}


void
MainWin::_MarkItem(BMenu* menu, uint32 command, bool mark)
{
	if (BMenuItem* item = menu->FindItem(command))
		item->SetMarked(mark);
}


void
MainWin::_AdoptGlobalSettings()
{
	mpSettings settings = Settings::CurrentSettings();
		// thread safe

	fCloseWhenDonePlayingMovie = settings.closeWhenDonePlayingMovie;
	fCloseWhenDonePlayingSound = settings.closeWhenDonePlayingSound;
}


