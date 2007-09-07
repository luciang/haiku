/*
 * Copyright 2004-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		mccall@@digitalparadise.co.uk
 *		Julun <host.haiku@gmx.de>
 */
 
#include <Application.h>
#include <Message.h>
#include <Screen.h>
#include <TabView.h>

#include <stdio.h>

#include "BaseView.h"
#include "SettingsView.h"
#include "Time.h"
#include "TimeMessages.h"
#include "TimeWindow.h"
#include "TimeSettings.h"
#include "ZoneView.h"

#define TIME_WINDOW_RIGHT	400 //332
#define TIME_WINDOW_BOTTOM	227 //208


TTimeWindow::TTimeWindow()
	: BWindow(BRect(0, 0, TIME_WINDOW_RIGHT, TIME_WINDOW_BOTTOM), 
		"Time & Date", B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE )
{
	MoveTo(dynamic_cast<TimeApplication *>(be_app)->WindowCorner());

	BRect frame = Frame();
	BRect bounds = Bounds();
	BRect screenFrame = BScreen().Frame();
	// Code to make sure that the window doesn't get drawn off screen...
	if (!(screenFrame.right >= frame.right && screenFrame.bottom >= frame.bottom))
		MoveTo((screenFrame.right - bounds.right) * 0.5f, 
			(screenFrame.bottom - bounds.bottom) * 0.5f);
	
	InitWindow(); 
	SetPulseRate(500000);
}


void 
TTimeWindow::MessageReceived(BMessage *message)
{
	switch(message->what) {
		case H_USER_CHANGE:
		{
			bool istime;
			if (message->FindBool("time", &istime) == B_OK)
				fBaseView->ChangeTime(message);
			break;
		}
		
		case H_RTC_CHANGE:
			fBaseView->SetGMTime(fTimeSettings->GMTime());
			break;
		
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool 
TTimeWindow::QuitRequested()
{
	dynamic_cast<TimeApplication *>(be_app)->SetWindowCorner(BPoint(Frame().left,Frame().top));
	
	fBaseView->StopWatchingAll(fTimeSettings);
	fBaseView->StopWatchingAll(fTimeZones);
	
	be_app->PostMessage(B_QUIT_REQUESTED);
	
	return BWindow::QuitRequested();
	
}


void 
TTimeWindow::InitWindow()
{
	BRect bounds(Bounds());
	
	fBaseView = new TTimeBaseView(bounds, "background view");
	AddChild(fBaseView);
	
	bounds.top = 9;
	BTabView *tabview = new BTabView(bounds, "tab_view");
	
	bounds = tabview->Bounds();
	bounds.InsetBy(4, 6);
	bounds.bottom -= tabview->TabHeight();
	
	fTimeSettings = new TSettingsView(bounds);
	if (fBaseView->StartWatchingAll(fTimeSettings) != B_OK)
		printf("StartWatchingAll(TimeSettings) failed!!!\n");

	fTimeZones = new TZoneView(bounds);
	if (fBaseView->StartWatchingAll(fTimeZones) != B_OK)
		printf("TimeZones->StartWatchingAll(TimeZone) failed!!!\n");

	// add tabs
	BTab *tab = new BTab();
	tabview->AddTab(fTimeSettings, tab);
	tab->SetLabel("Settings");
	
	tab = new BTab();
	tabview->AddTab(fTimeZones, tab);
	tab->SetLabel("Time Zone");
	
	fBaseView->AddChild(tabview);

	float width;
	float height;
	fTimeSettings->GetPreferredSize(&width, &height);
	// width/ height from settingsview + all InsetBy etc..
	ResizeTo(width +10, height + tabview->TabHeight() +25);
}

