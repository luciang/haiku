//****************************************************************************************
//
//	File:		PulseWindow.cpp
//
//	Written by:	Daniel Switkin
//
//	Copyright 1999, Be Incorporated
//
//****************************************************************************************


#include "PulseWindow.h"
#include "PulseApp.h"
#include "Common.h"
#include "DeskbarPulseView.h"

#include <Alert.h>
#include <Deskbar.h>
#include <Screen.h>

#include <stdlib.h>
#include <string.h>


PulseWindow::PulseWindow(BRect rect) :
	BWindow(rect, "Pulse", B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE)
{
	SetPulseRate(200000);

	PulseApp *pulseapp = (PulseApp *)be_app;
	BRect bounds = Bounds();
	fNormalPulseView = new NormalPulseView(bounds);
	AddChild(fNormalPulseView);

	fMiniPulseView = new MiniPulseView(bounds, "MiniPulseView", pulseapp->prefs);
	AddChild(fMiniPulseView);

	fMode = pulseapp->prefs->window_mode;
	if (fMode == MINI_WINDOW_MODE) {
		SetLook(B_MODAL_WINDOW_LOOK);
		SetFeel(B_NORMAL_WINDOW_FEEL);
		SetFlags(B_NOT_ZOOMABLE);
		fNormalPulseView->Hide();
		SetSizeLimits(GetMinimumViewWidth() - 1, 4096, 2, 4096);
		ResizeTo(rect.Width(), rect.Height());
	} else
		fMiniPulseView->Hide();

	fPrefsWindow = NULL;
}


PulseWindow::~PulseWindow()
{
	PulseApp *pulseapp = (PulseApp *)be_app;

	if (fMode == NORMAL_WINDOW_MODE)
		pulseapp->prefs->normal_window_rect = Frame();
	else if (fMode == MINI_WINDOW_MODE)
		pulseapp->prefs->mini_window_rect = Frame();
}


void
PulseWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case PV_NORMAL_MODE:
		case PV_MINI_MODE:
		case PV_DESKBAR_MODE:
			SetMode(message->what);
			break;
		case PRV_NORMAL_FADE_COLORS:
		case PRV_NORMAL_CHANGE_COLOR:
			fNormalPulseView->UpdateColors(message);
			break;
		case PRV_MINI_CHANGE_COLOR:
			fMiniPulseView->UpdateColors(message);
			break;
		case PRV_QUIT:
			fPrefsWindow = NULL;
			break;
		case PV_PREFERENCES: {
			// If the window is already open, bring it to the front
			if (fPrefsWindow != NULL) {
				fPrefsWindow->Activate(true);
				break;
			}
			// Otherwise launch a new preferences window
			PulseApp *pulseapp = (PulseApp *)be_app;
			fPrefsWindow = new PrefsWindow(pulseapp->prefs->prefs_window_rect,
				"Pulse settings", new BMessenger(this), pulseapp->prefs);
			fPrefsWindow->Show();
			break;
		}
		case PV_ABOUT: {
			BAlert *alert = new BAlert("Info", "Pulse\n\nBy David Ramsey and Arve Hjønnevåg\nRevised by Daniel Switkin", "OK");
			// Use the asynchronous version so we don't block the window's thread
			alert->Go(NULL);
			break;
		}
		case PV_QUIT:
			PostMessage(B_QUIT_REQUESTED);
			break;
		case PV_CPU_MENU_ITEM:
			// Call the correct version based on whose menu sent the message
			if (fMiniPulseView->IsHidden())
				fNormalPulseView->ChangeCPUState(message);
			else
				fMiniPulseView->ChangeCPUState(message);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
PulseWindow::MoveOnScreen()
{
	// check if the window is on screen, and move it if not
	BRect frame = Frame();
	BRect screenFrame = BScreen().Frame();

	if (frame.left > screenFrame.right)
		MoveBy(screenFrame.right - frame.right - 10, 0);
	else if (frame.right < 0)
		MoveTo(10, frame.top);

	if (frame.top > screenFrame.bottom)
		MoveBy(0, screenFrame.bottom - frame.bottom - 10);
	else if (frame.bottom < 0)
		MoveTo(frame.left, 10);
}


void
PulseWindow::SetMode(int newmode)
{
	PulseApp *pulseapp = (PulseApp *)be_app;

	switch (newmode) {
		case PV_NORMAL_MODE:
			if (fMode == MINI_WINDOW_MODE) {
				pulseapp->prefs->mini_window_rect = Frame();
				pulseapp->prefs->window_mode = NORMAL_WINDOW_MODE;
				pulseapp->prefs->Save();
			}
			fMiniPulseView->Hide();
			fNormalPulseView->Show();
			fMode = NORMAL_WINDOW_MODE;
			SetType(B_TITLED_WINDOW);
			SetFlags(B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
			ResizeTo(pulseapp->prefs->normal_window_rect.IntegerWidth(),
				pulseapp->prefs->normal_window_rect.IntegerHeight());
			MoveTo(pulseapp->prefs->normal_window_rect.left,
				pulseapp->prefs->normal_window_rect.top);
			MoveOnScreen();
			break;

		case PV_MINI_MODE:
			if (fMode == NORMAL_WINDOW_MODE) {
				pulseapp->prefs->normal_window_rect = Frame();
				pulseapp->prefs->window_mode = MINI_WINDOW_MODE;
				pulseapp->prefs->Save();
			}
			fNormalPulseView->Hide();
			fMiniPulseView->Show();
			fMode = MINI_WINDOW_MODE;
			SetLook(B_MODAL_WINDOW_LOOK);
			SetFeel(B_NORMAL_WINDOW_FEEL);
			SetFlags(B_NOT_ZOOMABLE);
			SetSizeLimits(GetMinimumViewWidth() - 1, 4096, 2, 4096);
			ResizeTo(pulseapp->prefs->mini_window_rect.IntegerWidth(),
				pulseapp->prefs->mini_window_rect.IntegerHeight());
			MoveTo(pulseapp->prefs->mini_window_rect.left,
				pulseapp->prefs->mini_window_rect.top);
			MoveOnScreen();
			break;

		case PV_DESKBAR_MODE:
			// Do not set window's mode to DESKBAR_MODE because the
			// destructor needs to save the correct BRect. ~PulseApp()
			// will handle launching the replicant after our prefs are saved.
			pulseapp->prefs->window_mode = DESKBAR_MODE;
			PostMessage(B_QUIT_REQUESTED);
			break;
	}
}


bool
PulseWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
