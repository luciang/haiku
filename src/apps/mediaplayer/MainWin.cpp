/*
 * MainWin.cpp - Media Player for the Haiku Operating System
 *
 * Copyright (C) 2006 Marcus Overhagen <marcus@overhagen.de>
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
#include "MainWin.h"
#include "MainApp.h"

#include <View.h>
#include <Screen.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Application.h>
#include <Alert.h>
#include <stdio.h>
#include <string.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <String.h>
#include <Debug.h>

#define NAME "MediaPlayer"
#define MIN_WIDTH 250

enum 
{
	M_DUMMY = 0x100,
	M_FILE_OPEN = 0x1000,
	M_FILE_NEWPLAYER,
	M_FILE_INFO,
	M_FILE_ABOUT,
	M_FILE_CLOSE,
	M_FILE_QUIT,	
	M_VIEW_50,
	M_VIEW_100,
	M_VIEW_200,
	M_VIEW_300,
	M_VIEW_400,
	M_SCALE_TO_NATIVE_SIZE,
	M_TOGGLE_FULLSCREEN,
	M_TOGGLE_NO_BORDER,
	M_TOGGLE_NO_MENU,
	M_TOGGLE_NO_CONTROLS,
	M_TOGGLE_NO_BORDER_NO_MENU_NO_CONTROLS,
	M_TOGGLE_ALWAYS_ON_TOP,
	M_TOGGLE_KEEP_ASPECT_RATIO,
	M_PREFERENCES,
	M_VOLUME_UP,
	M_VOLUME_DOWN,
	M_CHANNEL_NEXT,
	M_CHANNEL_PREV,
	M_ASPECT_100000_1,
	M_ASPECT_106666_1,
	M_ASPECT_109091_1,
	M_ASPECT_141176_1,
	M_ASPECT_720_576,
	M_ASPECT_704_576,
	M_ASPECT_544_576,
	M_SELECT_AUDIO_TRACK		= 0x00000800,
	M_SELECT_AUDIO_TRACK_END	= 0x00000fff,
	M_SELECT_VIDEO_TRACK		= 0x00010000,
	M_SELECT_VIDEO_TRACK_END	= 0x000fffff,
};

//#define printf(a...)


MainWin::MainWin()
 :	BWindow(BRect(200,200,450,400), NAME, B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS /* | B_WILL_ACCEPT_FIRST_CLICK */)
 ,  fFilePanel(NULL)
 ,	fHasFile(false)
 ,	fHasVideo(false)
 ,	fController(new Controller)
 ,	fIsFullscreen(false)
 ,	fKeepAspectRatio(true)
 ,	fAlwaysOnTop(false)
 ,	fNoMenu(false)
 ,	fNoBorder(false)
 ,	fNoControls(false)
 ,	fSourceWidth(0)
 ,	fSourceHeight(0)
 ,	fWidthScale(1.0)
 ,	fHeightScale(1.0)
 ,	fMouseDownTracking(false)
 ,	fFrameResizedTriggeredAutomatically(false)
 ,	fIgnoreFrameResized(false)
 ,	fFrameResizedCalled(true)
{
	BRect rect = Bounds();

	// background
	fBackground = new BView(rect, "background", B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	fBackground->SetViewColor(0,0,0);
	AddChild(fBackground);

	// menu
	fMenuBar = new BMenuBar(fBackground->Bounds(), "menu");
	CreateMenu();
	fBackground->AddChild(fMenuBar);
	fMenuBar->ResizeToPreferred();
	fMenuBarHeight = (int)fMenuBar->Frame().Height() + 1;
	fMenuBar->SetResizingMode(B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT);

	// video view
	rect = BRect(0, fMenuBarHeight, fBackground->Bounds().right, fMenuBarHeight + 10);
	fVideoView = new VideoView(rect, "video display", B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	fBackground->AddChild(fVideoView);
	
	// controls
	rect = BRect(0, fMenuBarHeight + 11, fBackground->Bounds().right, fBackground->Bounds().bottom);
	fControls = new ControllerView(rect, fController);
	fBackground->AddChild(fControls);
	fControls->ResizeToPreferred();
	fControlsHeight = (int)fControls->Frame().Height() + 1;
	fControlsWidth = (int)fControls->Frame().Width() + 1;
	fControls->SetResizingMode(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT_RIGHT);
	fControls->MoveTo(0, fBackground->Bounds().bottom - fControlsHeight + 1);
	
	fVideoView->ResizeTo(fBackground->Bounds().Width(), fBackground->Bounds().Height() - fMenuBarHeight - fControlsHeight);
	
	fController->SetVideoView(fVideoView);
	fVideoView->IsOverlaySupported();
	
	printf("fMenuBarHeight %d\n", fMenuBarHeight);
	printf("fControlsHeight %d\n", fControlsHeight);
	printf("fControlsWidth %d\n", fControlsWidth);

	SetupWindow();
	
	Show();
}


MainWin::~MainWin()
{
	printf("MainWin::~MainWin\n");
	delete fController;
	delete fFilePanel;
}


void
MainWin::OpenFile(const entry_ref &ref)
{
	printf("MainWin::OpenFile\n");

	status_t err = fController->SetTo(ref);
	if (err != B_OK) {
		char s[300];
		sprintf(s, "Can't open file\n\n%s\n\nError 0x%08lx\n(%s)\n",
			ref.name, err, strerror(err));
		(new BAlert("error", s, "OK"))->Go();
		fHasFile = false;
		fHasVideo = false;
		SetTitle(NAME);
	} else {
		fHasFile = true;
		fHasVideo = fController->VideoTrackCount() != 0;
		SetTitle(ref.name);
	}
	SetupWindow();
}

void
MainWin::SetupWindow()
{
	printf("MainWin::SetupWindow\n");	

	// Pupulate the track menus
	SetupTrackMenus();
	// Enable both if a file was loaded
	fAudioMenu->SetEnabled(fHasFile);
	fVideoMenu->SetEnabled(fHasFile);
	// Select first track (might be "none") in both
	fAudioMenu->ItemAt(0)->SetMarked(true);
	fVideoMenu->ItemAt(0)->SetMarked(true);
	
	if (fHasVideo) {
//		VideoFormatChange(int width, int height, float width_scale, float height_scale)
		fSourceWidth = 320;
		fSourceHeight = 240;
		fWidthScale = 1.24;
		fHeightScale = 1.35;
	} else {
		fSourceWidth = 320;
		fSourceHeight = 240;
		fWidthScale = 1.24;
		fHeightScale = 1.35;
	}
	
	int video_width;
	int video_height;

	// get required window size
	if (fHasVideo) {
		video_width = 400;
		video_height = 300;
	} else {
		video_width = 0;
		video_height = 0;
	}
	
	// Calculate and set the initial window size
	int width = max_c(fControlsWidth, video_width);
	int height = (fNoMenu ? 0 : fMenuBarHeight) + (fNoControls ? 0 : fControlsHeight) + video_height;
	SetWindowSizeLimits();
	ResizeTo(width - 1, height - 1);
	
	fVideoView->MakeFocus();
	
//	VideoFormatChange(fSourceWidth, fSourceHeight, fWidthScale, fHeightScale);

}


void
MainWin::SetWindowSizeLimits()
{
	int min_width = fNoControls  ? MIN_WIDTH : fControlsWidth;
	int min_height = (fNoMenu ? 0 : fMenuBarHeight) + (fNoControls ? 0 : fControlsHeight);
	
	SetSizeLimits(min_width - 1, 32000, min_height - 1, fHasVideo ? 32000 : min_height - 1);
}


void
MainWin::CreateMenu()
{
	fFileMenu = new BMenu(NAME);
	fViewMenu = new BMenu("View");
	fSettingsMenu = new BMenu("Settings");
	fAudioMenu = new BMenu("Audio Track");
	fVideoMenu = new BMenu("Video Track");
	fDebugMenu = new BMenu("Debug");

	fMenuBar->AddItem(fFileMenu);
	fMenuBar->AddItem(fViewMenu);
	fMenuBar->AddItem(fSettingsMenu);
	fMenuBar->AddItem(fDebugMenu);
	
	fFileMenu->AddItem(new BMenuItem("New Player", new BMessage(M_FILE_NEWPLAYER), 'N', B_COMMAND_KEY));
	fFileMenu->AddSeparatorItem();
	fFileMenu->AddItem(new BMenuItem("Open File"B_UTF8_ELLIPSIS, new BMessage(M_FILE_OPEN), 'O', B_COMMAND_KEY));
	fFileMenu->AddItem(new BMenuItem("File Info"B_UTF8_ELLIPSIS, new BMessage(M_FILE_INFO), 'I', B_COMMAND_KEY));
	fFileMenu->AddSeparatorItem();
	fFileMenu->AddItem(new BMenuItem("About", new BMessage(M_FILE_ABOUT), 'A', B_COMMAND_KEY));
	fFileMenu->AddSeparatorItem();
	fFileMenu->AddItem(new BMenuItem("Close", new BMessage(M_FILE_CLOSE), 'W', B_COMMAND_KEY));
	fFileMenu->AddItem(new BMenuItem("Quit", new BMessage(M_FILE_QUIT), 'Q', B_COMMAND_KEY));

	fViewMenu->AddItem(fAudioMenu);
	fViewMenu->AddItem(fVideoMenu);
	fViewMenu->AddSeparatorItem();
	fViewMenu->AddItem(new BMenuItem("50% scale", new BMessage(M_VIEW_50), '0', B_COMMAND_KEY));
	fViewMenu->AddItem(new BMenuItem("100% scale", new BMessage(M_VIEW_100), '1', B_COMMAND_KEY));
	fViewMenu->AddItem(new BMenuItem("200% scale", new BMessage(M_VIEW_200), '2', B_COMMAND_KEY));
	fViewMenu->AddItem(new BMenuItem("300% scale", new BMessage(M_VIEW_300), '3', B_COMMAND_KEY));
	fViewMenu->AddItem(new BMenuItem("400% scale", new BMessage(M_VIEW_400), '4', B_COMMAND_KEY));
	fViewMenu->AddSeparatorItem();
	fViewMenu->AddItem(new BMenuItem("Full Screen", new BMessage(M_TOGGLE_FULLSCREEN), 'F', B_COMMAND_KEY));
//	fViewMenu->SetRadioMode(true);
//	fViewMenu->AddSeparatorItem();
//	fViewMenu->AddItem(new BMenuItem("Always on Top", new BMessage(M_TOGGLE_ALWAYS_ON_TOP), 'T', B_COMMAND_KEY));

//	fSettingsMenu->AddSeparatorItem();
	fSettingsMenu->AddItem(new BMenuItem("No Menu", new BMessage(M_TOGGLE_NO_MENU), 'M', B_COMMAND_KEY));
	fSettingsMenu->AddItem(new BMenuItem("No Border", new BMessage(M_TOGGLE_NO_BORDER), 'B', B_COMMAND_KEY));
	fSettingsMenu->AddItem(new BMenuItem("No Controls", new BMessage(M_TOGGLE_NO_CONTROLS), 'C', B_COMMAND_KEY));
	fSettingsMenu->AddItem(new BMenuItem("Always on Top", new BMessage(M_TOGGLE_ALWAYS_ON_TOP), 'T', B_COMMAND_KEY));
	fSettingsMenu->AddItem(new BMenuItem("Keep Aspect Ratio", new BMessage(M_TOGGLE_KEEP_ASPECT_RATIO), 'K', B_COMMAND_KEY));
	fSettingsMenu->AddSeparatorItem();
	fSettingsMenu->AddItem(new BMenuItem("Preferences"B_UTF8_ELLIPSIS, new BMessage(M_PREFERENCES), 'P', B_COMMAND_KEY));

	fDebugMenu->AddItem(new BMenuItem("pixel aspect ratio 1.00000:1", new BMessage(M_ASPECT_100000_1)));
	fDebugMenu->AddItem(new BMenuItem("pixel aspect ratio 1.06666:1", new BMessage(M_ASPECT_106666_1)));
	fDebugMenu->AddItem(new BMenuItem("pixel aspect ratio 1.09091:1", new BMessage(M_ASPECT_109091_1)));
	fDebugMenu->AddItem(new BMenuItem("pixel aspect ratio 1.41176:1", new BMessage(M_ASPECT_141176_1)));
	fDebugMenu->AddItem(new BMenuItem("force 720 x 576, display aspect 4:3", new BMessage(M_ASPECT_720_576)));
	fDebugMenu->AddItem(new BMenuItem("force 704 x 576, display aspect 4:3", new BMessage(M_ASPECT_704_576)));
	fDebugMenu->AddItem(new BMenuItem("force 544 x 576, display aspect 4:3", new BMessage(M_ASPECT_544_576)));

	fAudioMenu->SetRadioMode(true);
	fVideoMenu->SetRadioMode(true);
/*	
	fSettingsMenu->ItemAt(3)->SetMarked(fIsFullscreen);
	fSettingsMenu->ItemAt(5)->SetMarked(fNoMenu);
	fSettingsMenu->ItemAt(6)->SetMarked(fNoBorder);
	fSettingsMenu->ItemAt(7)->SetMarked(fAlwaysOnTop);
	fSettingsMenu->ItemAt(8)->SetMarked(fKeepAspectRatio);
	fSettingsMenu->ItemAt(10)->SetEnabled(false); // XXX disable unused preference menu
*/
}


void
MainWin::SetupTrackMenus()
{
	fAudioMenu->RemoveItems(0, fAudioMenu->CountItems(), true);
	fVideoMenu->RemoveItems(0, fVideoMenu->CountItems(), true);
	
	int c, i;
	char s[100];
	
	c = fController->AudioTrackCount();
	for (i = 0; i < c; i++) {
		sprintf(s, "Track %d", i + 1);
		fAudioMenu->AddItem(new BMenuItem(s, new BMessage(M_SELECT_AUDIO_TRACK + i)));
	}
	if (!c)
		fAudioMenu->AddItem(new BMenuItem("none", new BMessage(M_DUMMY)));

	c = fController->VideoTrackCount();
	for (i = 0; i < c; i++) {
		sprintf(s, "Track %d", i + 1);
		fVideoMenu->AddItem(new BMenuItem(s, new BMessage(M_SELECT_VIDEO_TRACK + i)));
	}
	if (!c)
		fVideoMenu->AddItem(new BMenuItem("none", new BMessage(M_DUMMY)));
}


bool
MainWin::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWin::MouseDown(BMessage *msg)
{
	BPoint screen_where;
	uint32 buttons = msg->FindInt32("buttons");

	// On Zeta, only "screen_where" is relyable, "where" and "be:view_where" seem to be broken
	if (B_OK != msg->FindPoint("screen_where", &screen_where)) {
		// Workaround for BeOS R5, it has no "screen_where"
		fVideoView->GetMouse(&screen_where, &buttons, false);
		fVideoView->ConvertToScreen(&screen_where);
	}

//	msg->PrintToStream();

//	if (1 == msg->FindInt32("buttons") && msg->FindInt32("clicks") == 1) {

	if (1 == buttons && msg->FindInt32("clicks") % 2 == 0) {
		BRect r(screen_where.x - 1, screen_where.y - 1, screen_where.x + 1, screen_where.y + 1);
		if (r.Contains(fMouseDownMousePos)) {
			PostMessage(M_TOGGLE_FULLSCREEN);
			return;
		}
	}
	
	if (2 == buttons && msg->FindInt32("clicks") % 2 == 0) {
		BRect r(screen_where.x - 1, screen_where.y - 1, screen_where.x + 1, screen_where.y + 1);
		if (r.Contains(fMouseDownMousePos)) {
			PostMessage(M_TOGGLE_NO_BORDER_NO_MENU_NO_CONTROLS);
			return;
		}
	}

/*
		// very broken in Zeta:
		fMouseDownMousePos = fVideoView->ConvertToScreen(msg->FindPoint("where"));
*/
	fMouseDownMousePos = screen_where;
	fMouseDownWindowPos = Frame().LeftTop();

	if (buttons == 1 && !fIsFullscreen) {
		// start mouse tracking
		fVideoView->SetMouseEventMask(B_POINTER_EVENTS | B_NO_POINTER_HISTORY /* | B_LOCK_WINDOW_FOCUS */);
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
		ShowContextMenu(screen_where);
}


void
MainWin::MouseMoved(BMessage *msg)
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
		// On Zeta, only "screen_where" is relyable, "where" and "be:view_where" seem to be broken
		if (B_OK != msg->FindPoint("screen_where", &mousePos)) {
			// Workaround for BeOS R5, it has no "screen_where"
			fVideoView->GetMouse(&mousePos, &buttons, false);
			fVideoView->ConvertToScreen(&mousePos);
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
MainWin::MouseUp(BMessage *msg)
{
//	msg->PrintToStream();
	fMouseDownTracking = false;
}


void
MainWin::ShowContextMenu(const BPoint &screen_point)
{
	printf("Show context menu\n");
	BPopUpMenu *menu = new BPopUpMenu("context menu", false, false);
	BMenuItem *item;
	menu->AddItem(new BMenuItem("Scale to native size", new BMessage(M_SCALE_TO_NATIVE_SIZE), 'N', B_COMMAND_KEY));
	menu->AddItem(item = new BMenuItem("Full Screen", new BMessage(M_TOGGLE_FULLSCREEN), 'F', B_COMMAND_KEY));
	item->SetMarked(fIsFullscreen);
	menu->AddSeparatorItem();
	menu->AddItem(item = new BMenuItem("No Menu", new BMessage(M_TOGGLE_NO_MENU), 'M', B_COMMAND_KEY));
	item->SetMarked(fNoMenu);
	menu->AddItem(item = new BMenuItem("No Border", new BMessage(M_TOGGLE_NO_BORDER), 'B', B_COMMAND_KEY));
	item->SetMarked(fNoBorder);
	menu->AddItem(item = new BMenuItem("Always on Top", new BMessage(M_TOGGLE_ALWAYS_ON_TOP), 'T', B_COMMAND_KEY));
	item->SetMarked(fAlwaysOnTop);
	menu->AddItem(item = new BMenuItem("Keep Aspect Ratio", new BMessage(M_TOGGLE_KEEP_ASPECT_RATIO), 'K', B_COMMAND_KEY));
	item->SetMarked(fKeepAspectRatio);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About", new BMessage(M_FILE_ABOUT), 'A', B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(M_FILE_QUIT), 'Q', B_COMMAND_KEY));

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("pixel aspect ratio 1.00000:1", new BMessage(M_ASPECT_100000_1)));
	menu->AddItem(new BMenuItem("pixel aspect ratio 1.06666:1", new BMessage(M_ASPECT_106666_1)));
	menu->AddItem(new BMenuItem("pixel aspect ratio 1.09091:1", new BMessage(M_ASPECT_109091_1)));
	menu->AddItem(new BMenuItem("pixel aspect ratio 1.41176:1", new BMessage(M_ASPECT_141176_1)));
	menu->AddItem(new BMenuItem("force 720 x 576, display aspect 4:3", new BMessage(M_ASPECT_720_576)));
	menu->AddItem(new BMenuItem("force 704 x 576, display aspect 4:3", new BMessage(M_ASPECT_704_576)));
	menu->AddItem(new BMenuItem("force 544 x 576, display aspect 4:3", new BMessage(M_ASPECT_544_576)));

	menu->SetTargetForItems(this);
	BRect r(screen_point.x - 5, screen_point.y - 5, screen_point.x + 5, screen_point.y + 5);
	menu->Go(screen_point, true, true, r, true);
}


void
MainWin::VideoFormatChange(int width, int height, float width_scale, float height_scale)
{
	// called when video format or aspect ratio changes
	
	printf("VideoFormatChange enter: width %d, height %d, width_scale %.6f, height_scale %.6f\n", width, height, width_scale, height_scale);

	if (width_scale < 1.0 && height_scale >= 1.0) {
		width_scale  = 1.0 / width_scale;
		height_scale = 1.0 / height_scale;
		printf("inverting! new values: width_scale %.6f, height_scale %.6f\n", width_scale, height_scale);
	}
	
 	fSourceWidth  = width;
 	fSourceHeight = height;
 	fWidthScale   = width_scale;
 	fHeightScale  = height_scale;
	
/*	
//	ResizeWindow(Bounds().Width() + 1, Bounds().Height() + 1, true);

	if (fIsFullscreen) {
		AdjustFullscreenRenderer();
	} else {
		AdjustWindowedRenderer(false);
	}
*/

	printf("VideoFormatChange leave\n");

}


void
MainWin::Zoom(BPoint rec_position, float rec_width, float rec_height)
{
	PostMessage(M_TOGGLE_FULLSCREEN);
}


void
MainWin::FrameResized(float new_width, float new_height)
{
	if (new_width != Bounds().Width() || new_height != Bounds().Height()) {
		debugger("size wrong\n");
	}
	
	printf("FrameResized enter: new_width %.0f, new_height %.0f\n", new_width, new_height);
	
	int max_video_width  = new_width + 1;
	int max_video_height = new_height + 1;
	if (!fNoMenu)
		max_video_height -= fMenuBarHeight;
	if (!fNoControls)
		max_video_height -= fControlsHeight;

	ASSERT(max_video_height >= 0);
	
	if (max_video_height == 0 && !fVideoView->IsHidden())
		fVideoView->Hide();
	if (max_video_height != 0 && fVideoView->IsHidden())
		fVideoView->Show();
	
	fVideoViewBounds = BRect(0, fNoMenu ? 0 : fMenuBarHeight, max_video_width - 1, max_video_height - 1);


/*		



return;
	
	printf("FrameResized enter: new_width %.0f, new_height %.0f, bounds width %.0f, bounds height %.0f\n", new_width, new_height, Bounds().Width(), Bounds().Height());
	
	if (fIsFullscreen) {

		printf("FrameResized in fullscreen mode\n");

		fIgnoreFrameResized = false;
		AdjustFullscreenRenderer();

	} else {
	
		if (fFrameResizedTriggeredAutomatically) {
			fFrameResizedTriggeredAutomatically = false;
			printf("FrameResized triggered automatically\n");

			fIgnoreFrameResized = false;
		
			AdjustWindowedRenderer(false);
		} else {
			printf("FrameResized by user in window mode\n");

			if (fIgnoreFrameResized) {
				fIgnoreFrameResized = false;
				printf("FrameResized ignored\n");
				return;
			}

			AdjustWindowedRenderer(true);
		}

	}
*/

	printf("FrameResized leave\n");
}


void
MainWin::ShowFileInfo()
{
}


void
MainWin::UpdateWindowTitle()
{
	char buf[100];
	sprintf(buf, "%s - %d x %d, %.3f:%.3f => %.0f x %.0f", NAME, fSourceWidth, fSourceHeight, fWidthScale, fHeightScale, fVideoView->Bounds().Width() + 1, fVideoView->Bounds().Height() + 1);
	SetTitle(buf);
}

/*
void
MainWin::AdjustFullscreenRenderer()
{
	// n.b. we don't have a menu in fullscreen mode!

	if (fKeepAspectRatio) {
			
		// Keep aspect ratio, place render inside
		// the background area (may create black bars).
		float max_width  = fBackground->Bounds().Width() + 1.0f;
		float max_height = fBackground->Bounds().Height() + 1.0f;
		float scaled_width  = fSourceWidth * fWidthScale;
		float scaled_height = fSourceHeight * fHeightScale;
		float factor = min_c(max_width / scaled_width, max_height / scaled_height);
		int render_width = int(scaled_width * factor);
		int render_height = int(scaled_height * factor);
		int x_ofs = (int(max_width) - render_width) / 2;
		int y_ofs = (int(max_height) - render_height) / 2;

		printf("AdjustFullscreenRenderer: background %.1f x %.1f, src video %d x %d, "
			  "scaled video %.3f x %.3f, factor %.3f, render %d x %d, x-ofs %d, y-ofs %d\n", 
			  max_width, max_height, fSourceWidth, fSourceHeight, scaled_width, scaled_height,
			  factor, render_width, render_height, x_ofs, y_ofs);
	
		fVideoView->MoveTo(x_ofs, y_ofs);
		fVideoView->ResizeTo(render_width - 1, render_height - 1);

	} else {

		printf("AdjustFullscreenRenderer: using whole background area\n");

		// no need to keep aspect ratio, make
		// render cover the whole background
		fVideoView->MoveTo(0, 0);
		fVideoView->ResizeTo(fBackground->Bounds().Width(), fBackground->Bounds().Height());

	}
}


void
MainWin::AdjustWindowedRenderer(bool user_resized)
{
	printf("AdjustWindowedRenderer enter - user_resized %d\n", user_resized);

	// In windowed mode, the renderer always covers the
	// whole background, accounting for the menu
	fVideoView->MoveTo(0, fNoMenu ? 0 : fMenuBarHeight);
	fVideoView->ResizeTo(fBackground->Bounds().Width(), fBackground->Bounds().Height() - (fNoMenu ? 0 : fMenuBarHeight));

	if (fKeepAspectRatio) {
		// To keep the aspect ratio correct, we
		// do resize the window as required

		float max_width  = Bounds().Width() + 1.0f;
		float max_height = Bounds().Height() + 1.0f - (fNoMenu ? 0 : fMenuBarHeight);
		float scaled_width  = fSourceWidth * fWidthScale;
		float scaled_height = fSourceHeight * fHeightScale;
		
		if (!user_resized && (scaled_width > max_width || scaled_height > max_height)) {
			// A format switch occured, and the window was
			// smaller then the video source. As it was not
			// initiated by the user resizing the window, we
			// enlarge the window to fit the video.
			fIgnoreFrameResized = true;
			ResizeTo(scaled_width - 1, scaled_height - 1 + (fNoMenu ? 0 : fMenuBarHeight));
//			Sync();
			return;
		}

		float display_aspect_ratio = scaled_width / scaled_height;
		int new_width  = int(max_width);
		int new_height = int(max_width / display_aspect_ratio + 0.5);
		
		printf("AdjustWindowedRenderer: old display %d x %d, src video %d x %d, "
			  "scaled video %.3f x %.3f, aspect ratio %.3f, new display %d x %d\n", 
			  int(max_width), int(max_height), fSourceWidth, fSourceHeight, scaled_width, scaled_height,
			  display_aspect_ratio, new_width, new_height);

		fIgnoreFrameResized = true;
		ResizeTo(new_width - 1, new_height - 1 + (fNoMenu ? 0 : fMenuBarHeight));
//		Sync();
	}

	printf("AdjustWindowedRenderer leave\n");
}
*/

void
MainWin::ToggleNoBorderNoMenu()
{
	if (!fNoMenu && fNoBorder) {
		// if no border, switch of menu, too
		PostMessage(M_TOGGLE_NO_MENU);
	} else
	if (fNoMenu && !fNoBorder) {
		// if no menu, switch of border, too
		PostMessage(M_TOGGLE_NO_BORDER);
	} else {
		// both are either on or off, toggle both
		PostMessage(M_TOGGLE_NO_MENU);
		PostMessage(M_TOGGLE_NO_BORDER);
	}
}


void
MainWin::ToggleFullscreen()
{
	printf("ToggleFullscreen enter\n");

	if (!fHasVideo) {
		printf("ToggleFullscreen - ignoring, as we don't have a video\n");
		return;
	}

	if (!fFrameResizedCalled) {
		printf("ToggleFullscreen - ignoring, as FrameResized wasn't called since last switch\n");
		return;
	}
	fFrameResizedCalled = false;
	
	
	fIsFullscreen = !fIsFullscreen;
	
	if (fIsFullscreen) {
		// switch to fullscreen
		
		fSavedFrame = Frame();
		printf("saving current frame: %d %d %d %d\n", int(fSavedFrame.left), int(fSavedFrame.top), int(fSavedFrame.right), int(fSavedFrame.bottom));
		BScreen screen(this);
		BRect rect(screen.Frame());

		Hide();
		if (!fNoMenu) {
			// if we have a menu, remove it now
			fMenuBar->Hide();
		}
		fFrameResizedTriggeredAutomatically = true;		
		MoveTo(rect.left, rect.top);
		ResizeTo(rect.Width(), rect.Height());
		Show();

	} else {
		// switch back from full screen mode

		Hide();
		// if we need a menu, show it now
		if (!fNoMenu) {
			fMenuBar->Show();
		}
		fFrameResizedTriggeredAutomatically = true;		
		MoveTo(fSavedFrame.left, fSavedFrame.top);
		ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
		Show();
		
	}

	// FrameResized() will do the required adjustments
	
	printf("ToggleFullscreen leave\n");
}

void
MainWin::ToggleNoControls()
{
	printf("ToggleNoControls enter\n");

	if (fIsFullscreen) {
		// fullscreen is always without menu	
		printf("ToggleNoControls leave, doing nothing, we are fullscreen\n");
		return;
	}

	fNoControls = !fNoControls;
	SetWindowSizeLimits();

	if (fNoControls) {
		fControls->Hide();
		ResizeBy(0, - fControlsHeight);
	} else {
		fControls->Show();
		ResizeBy(0, fControlsHeight);
	}

	printf("ToggleNoControls leave\n");
}

void
MainWin::ToggleNoMenu()
{
	printf("ToggleNoMenu enter\n");

	if (fIsFullscreen) {
		// fullscreen is always without menu	
		printf("ToggleNoMenu leave, doing nothing, we are fullscreen\n");
		return;
	}

	fNoMenu = !fNoMenu;
	SetWindowSizeLimits();

	if (fNoMenu) {
		fMenuBar->Hide();
		fVideoView->MoveTo(0, 0);
		fVideoView->ResizeBy(0, fMenuBarHeight);
		MoveBy(0, fMenuBarHeight);
		ResizeBy(0, - fMenuBarHeight);
	} else {
		fMenuBar->Show();
		fVideoView->MoveTo(0, fMenuBarHeight);
		fVideoView->ResizeBy(0, - fMenuBarHeight);
		MoveBy(0, - fMenuBarHeight);
		ResizeBy(0, fMenuBarHeight);
	}

	printf("ToggleNoMenu leave\n");
}


void
MainWin::ToggleNoBorder()
{
	printf("ToggleNoBorder enter\n");
	fNoBorder = !fNoBorder;
	SetLook(fNoBorder ? B_BORDERED_WINDOW_LOOK : B_TITLED_WINDOW_LOOK);
	printf("ToggleNoBorder leave\n");
}


void
MainWin::ToggleAlwaysOnTop()
{
	printf("ToggleAlwaysOnTop enter\n");
	fAlwaysOnTop = !fAlwaysOnTop;
	SetFeel(fAlwaysOnTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL);
	printf("ToggleAlwaysOnTop leave\n");
}


void
MainWin::ToggleKeepAspectRatio()
{
	printf("ToggleKeepAspectRatio enter\n");
	fKeepAspectRatio = !fKeepAspectRatio;
	FrameResized(Bounds().Width(), Bounds().Height());
	printf("ToggleKeepAspectRatio leave\n");
}


void
MainWin::RefsReceived(BMessage *msg)
{
	printf("MainWin::RefsReceived\n");
	// the first file is played in this window,
	// if additional files (refs) are included,
	// more windows are launched
	
	entry_ref ref;
	int n = 0;
	for (int i = 0; B_OK == msg->FindRef("refs", i, &ref); i++) {
		BEntry entry(&ref, true);
		if (!entry.Exists() || !entry.IsFile())
			continue;
		if (n == 0) {
			OpenFile(ref);
		} else {
			BMessage m(B_REFS_RECEIVED);
			m.AddRef("refs", &ref);
			gMainApp->NewWindow()->PostMessage(&m);
		}
		n++;
	}
}


/* Trap keys that are about to be send to background or renderer view.
 * Return B_OK if it shouldn't be passed to the view
 */
status_t
MainWin::KeyDown(BMessage *msg)
{
//	msg->PrintToStream();
	
	uint32 key		 = msg->FindInt32("key");
	uint32 raw_char  = msg->FindInt32("raw_char");
	uint32 modifiers = msg->FindInt32("modifiers");
	
	printf("key 0x%lx, raw_char 0x%lx, modifiers 0x%lx\n", key, raw_char, modifiers);
	
	switch (raw_char) {
		case B_SPACE:
			PostMessage(M_TOGGLE_NO_BORDER_NO_MENU_NO_CONTROLS);
			return B_OK;

		case B_ESCAPE:
			if (fIsFullscreen) {
				PostMessage(M_TOGGLE_FULLSCREEN);
				return B_OK;
			} else
				break;

		case B_ENTER:		// Enter / Return
			if (modifiers & B_COMMAND_KEY) {
				PostMessage(M_TOGGLE_FULLSCREEN);
				return B_OK;
			} else
				break;
				
		case B_TAB:
			if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY | B_MENU_KEY)) == 0) {
				PostMessage(M_TOGGLE_FULLSCREEN);
				return B_OK;
			} else
				break;
		
		case B_UP_ARROW:
			if (modifiers & B_COMMAND_KEY) {
				PostMessage(M_CHANNEL_NEXT);
			} else {
				PostMessage(M_VOLUME_UP);
			}
			return B_OK;

		case B_DOWN_ARROW:
			if (modifiers & B_COMMAND_KEY) {
				PostMessage(M_CHANNEL_PREV);
			} else {
				PostMessage(M_VOLUME_DOWN);
			}
			return B_OK;
			
		case B_RIGHT_ARROW:
			if (modifiers & B_COMMAND_KEY) {
				PostMessage(M_VOLUME_UP);
			} else {
				PostMessage(M_CHANNEL_NEXT);
			}
			return B_OK;

		case B_LEFT_ARROW:
			if (modifiers & B_COMMAND_KEY) {
				PostMessage(M_VOLUME_DOWN);
			} else {
				PostMessage(M_CHANNEL_PREV);
			}
			return B_OK;

		case B_PAGE_UP:
			PostMessage(M_CHANNEL_NEXT);
			return B_OK;
			
		case B_PAGE_DOWN:
			PostMessage(M_CHANNEL_PREV);
			return B_OK;
	}

	switch (key) {
		case 0x3a:  		// numeric keypad +
			if ((modifiers & B_COMMAND_KEY) == 0) {
				printf("if\n");
				PostMessage(M_VOLUME_UP);
				return B_OK;
			} else {
				printf("else\n");
				break;
			}

		case 0x25:  		// numeric keypad -
			if ((modifiers & B_COMMAND_KEY) == 0) {
				PostMessage(M_VOLUME_DOWN);
				return B_OK;
			} else {
				break;
			}

		case 0x38:			// numeric keypad up arrow
			PostMessage(M_VOLUME_UP);
			return B_OK;

		case 0x59:			// numeric keypad down arrow
			PostMessage(M_VOLUME_DOWN);
			return B_OK;
			
		case 0x39:			// numeric keypad page up
		case 0x4a:			// numeric keypad right arrow
			PostMessage(M_CHANNEL_NEXT);
			return B_OK;
			
		case 0x5a:			// numeric keypad page down
		case 0x48:			// numeric keypad left arrow
			PostMessage(M_CHANNEL_PREV);
			return B_OK;
	}
	
	return B_ERROR;
}


void
MainWin::DispatchMessage(BMessage *msg, BHandler *handler)
{
	if ((msg->what == B_MOUSE_DOWN) && (handler == fBackground || handler == fVideoView))
		MouseDown(msg);
	if ((msg->what == B_MOUSE_MOVED) && (handler == fBackground || handler == fVideoView))
		MouseMoved(msg);
	if ((msg->what == B_MOUSE_UP) && (handler == fBackground || handler == fVideoView))
		MouseUp(msg);

	if ((msg->what == B_KEY_DOWN) && (handler == fBackground || handler == fVideoView)) {

		// special case for PrintScreen key
		if (msg->FindInt32("key") == B_PRINT_KEY) {
			fVideoView->OverlayScreenshotPrepare();
			BWindow::DispatchMessage(msg, handler);
			fVideoView->OverlayScreenshotCleanup();
			return;
		}
		
		// every other key gets dispatched to our KeyDown first
		if (KeyDown(msg) == B_OK) {
			// it got handled, don't pass it on
			return;
		}
	}
	
	BWindow::DispatchMessage(msg, handler);
}


void
MainWin::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case B_REFS_RECEIVED:
			printf("MainWin::MessageReceived: B_REFS_RECEIVED\n");
			RefsReceived(msg);
			break;
		case B_SIMPLE_DATA:
			printf("MainWin::MessageReceived: B_SIMPLE_DATA\n");
			if (msg->HasRef("refs"))
				RefsReceived(msg);
			break;
		case M_FILE_NEWPLAYER:
			gMainApp->NewWindow();
			break;
		case M_FILE_OPEN:
			if (!fFilePanel) {
				fFilePanel = new BFilePanel();
				fFilePanel->SetTarget(BMessenger(0, this));
				fFilePanel->SetPanelDirectory("/boot/home/");
			}
			fFilePanel->Show();
			break;
		case M_FILE_INFO:
			ShowFileInfo();
			break;
		case M_FILE_ABOUT:
			BAlert *alert;
			alert = new BAlert("about", NAME"\n\n Written by Marcus Overhagen","Thanks");
			if (fAlwaysOnTop) {
				ToggleAlwaysOnTop();
				alert->Go();
				ToggleAlwaysOnTop();
			} else {
				alert->Go();
			}
			break;
		case M_FILE_CLOSE:
			PostMessage(B_QUIT_REQUESTED);
			break;
		case M_FILE_QUIT:	
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;

		case M_TOGGLE_FULLSCREEN:
			ToggleFullscreen();
//			fSettingsMenu->ItemAt(1)->SetMarked(fIsFullscreen);
			break;

		case M_TOGGLE_NO_MENU:
			ToggleNoMenu();
//			fSettingsMenu->ItemAt(3)->SetMarked(fNoMenu);
			break;
			
		case M_TOGGLE_NO_CONTROLS:
			ToggleNoControls();
//			fSettingsMenu->ItemAt(3)->SetMarked(fNoControls);
			break;
		
		case M_TOGGLE_NO_BORDER:
			ToggleNoBorder();
//			fSettingsMenu->ItemAt(4)->SetMarked(fNoBorder);
			break;
			
		case M_TOGGLE_ALWAYS_ON_TOP:
			ToggleAlwaysOnTop();
//			fSettingsMenu->ItemAt(5)->SetMarked(fAlwaysOnTop);
			break;
	
		case M_TOGGLE_KEEP_ASPECT_RATIO:
			ToggleKeepAspectRatio();
//			fSettingsMenu->ItemAt(6)->SetMarked(fKeepAspectRatio);
			break;

		case M_TOGGLE_NO_BORDER_NO_MENU_NO_CONTROLS:
			ToggleNoBorderNoMenu();
			break;


/*
		
		case B_ACQUIRE_OVERLAY_LOCK:
			printf("B_ACQUIRE_OVERLAY_LOCK\n");
			fVideoView->OverlayLockAcquire();
			break;
			
		case B_RELEASE_OVERLAY_LOCK:
			printf("B_RELEASE_OVERLAY_LOCK\n");
			fVideoView->OverlayLockRelease();
			break;
			
		case B_MOUSE_WHEEL_CHANGED:
		{
			printf("B_MOUSE_WHEEL_CHANGED\n");
			float dx = msg->FindFloat("be:wheel_delta_x");
			float dy = msg->FindFloat("be:wheel_delta_y");
			bool inv = modifiers() & B_COMMAND_KEY;
			if (dx > 0.1)	PostMessage(inv ? M_VOLUME_DOWN : M_CHANNEL_PREV);
			if (dx < -0.1)	PostMessage(inv ? M_VOLUME_UP : M_CHANNEL_NEXT);
			if (dy > 0.1)	PostMessage(inv ? M_CHANNEL_PREV : M_VOLUME_DOWN);
			if (dy < -0.1)	PostMessage(inv ? M_CHANNEL_NEXT : M_VOLUME_UP);
			break;
		}

		case M_CHANNEL_NEXT:
		{
			printf("M_CHANNEL_NEXT\n");
			int chan = fController->CurrentChannel();
			if (chan != -1) {
				chan++;
				if (chan < fController->ChannelCount())
					SelectChannel(chan);
			}
			break;
		}

		case M_CHANNEL_PREV:
		{
			printf("M_CHANNEL_PREV\n");
			int chan = fController->CurrentChannel();
			if (chan != -1) {
				chan--;
				if (chan >= 0)
					SelectChannel(chan);
			}
			break;
		}

		case M_VOLUME_UP:
			printf("M_VOLUME_UP\n");
			fController->VolumeUp();
			break;

		case M_VOLUME_DOWN:
			printf("M_VOLUME_DOWN\n");
			fController->VolumeDown();
			break;

		case M_ASPECT_100000_1:
			VideoFormatChange(fSourceWidth, fSourceHeight, 1.0, 1.0);
			break;

		case M_ASPECT_106666_1:
			VideoFormatChange(fSourceWidth, fSourceHeight, 1.06666, 1.0);
			break;

		case M_ASPECT_109091_1:
			VideoFormatChange(fSourceWidth, fSourceHeight, 1.09091, 1.0);
			break;

		case M_ASPECT_141176_1:
			VideoFormatChange(fSourceWidth, fSourceHeight, 1.41176, 1.0);
			break;

		case M_ASPECT_720_576:
			VideoFormatChange(720, 576, 1.06666, 1.0);
			break;

		case M_ASPECT_704_576:
			VideoFormatChange(704, 576, 1.09091, 1.0);
			break;

		case M_ASPECT_544_576:
			VideoFormatChange(544, 576, 1.41176, 1.0);
			break;

		case M_SCALE_TO_NATIVE_SIZE:
			printf("M_SCALE_TO_NATIVE_SIZE\n");
			if (fIsFullscreen) {
				ToggleFullscreen();
			}
			ResizeTo(int(fSourceWidth * fWidthScale), 
					 int(fSourceHeight * fHeightScale) + (fNoMenu ? 0 : fMenuBarHeight));
//			Sync();
			break;

		
		case M_PREFERENCES:
			break;
			
		default:
			if (msg->what >= M_SELECT_CHANNEL && msg->what <= M_SELECT_CHANNEL_END) {
				SelectChannel(msg->what - M_SELECT_CHANNEL);
				break;
			}
			if (msg->what >= M_SELECT_INTERFACE && msg->what <= M_SELECT_INTERFACE_END) {
				SelectInterface(msg->what - M_SELECT_INTERFACE - 1);
				break;
			}
*/			
	}
}
