/*
 * Copyright 2004-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andrew McCall <mccall@@digitalparadise.co.uk>
 *		Julun <host.haiku@gmx.de>
 *
 */

#include "TimeWindow.h"
#include "BaseView.h"
#include "DateTimeView.h"
#include "TimeMessages.h"
#include "TimeSettings.h"
#include "ZoneView.h"


#include <Application.h>
#include <Message.h>
#include <Screen.h>
#include <TabView.h>


TTimeWindow::TTimeWindow(BRect rect)
	: BWindow(rect, "Time", B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE)
{
	_InitWindow();
	_AlignWindow();

	AddShortcut('A', B_COMMAND_KEY, new BMessage(B_ABOUT_REQUESTED));
}


TTimeWindow::~TTimeWindow()
{
}


void 
TTimeWindow::MessageReceived(BMessage *message)
{
	switch(message->what) {
		case H_USER_CHANGE:
			fBaseView->ChangeTime(message);
			fDateTimeView->CheckCanRevert();
			break;
		
		case B_ABOUT_REQUESTED:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool 
TTimeWindow::QuitRequested()
{
	TimeSettings().SetLeftTop(Frame().LeftTop());

	fBaseView->StopWatchingAll(fTimeZoneView);
	fBaseView->StopWatchingAll(fDateTimeView);
	
	be_app->PostMessage(B_QUIT_REQUESTED);
	
	return BWindow::QuitRequested();
}


void 
TTimeWindow::_InitWindow()
{
	SetPulseRate(500000);

	fDateTimeView = new DateTimeView(Bounds());
	
	BRect bounds = fDateTimeView->Bounds();
	fTimeZoneView = new TimeZoneView(bounds);

	fBaseView = new TTimeBaseView(bounds, "baseView");
	AddChild(fBaseView);
	
	fBaseView->StartWatchingAll(fDateTimeView);
	fBaseView->StartWatchingAll(fTimeZoneView);

	bounds.OffsetBy(10.0, 10.0);
	BTabView *tabView = new BTabView(bounds.InsetByCopy(-5.0, -5.0),
		"tabView" , B_WIDTH_AS_USUAL, B_FOLLOW_NONE);
	
	BTab *tab = new BTab();
	tabView->AddTab(fDateTimeView, tab);
	tab->SetLabel("Date & Time");

	tab = new BTab();
	tabView->AddTab(fTimeZoneView, tab);
	tab->SetLabel("Timezone");

	fBaseView->AddChild(tabView);
	tabView->ResizeBy(0.0, tabView->TabHeight());
	fBaseView->ResizeTo(tabView->Bounds().Width() + 10.0, 
		tabView->Bounds().Height() + 10.0);

	ResizeTo(fBaseView->Bounds().Width(), fBaseView->Bounds().Height());
}


void
TTimeWindow::_AlignWindow()
{
	BPoint pt = TimeSettings().LeftTop();
	MoveTo(pt);

	BRect frame = Frame();
	BRect screen = BScreen().Frame();
	if (!frame.Intersects(screen.InsetByCopy(50.0, 50.0))) {
		BRect bounds(Bounds());
		BPoint leftTop((screen.Width() - bounds.Width()) / 2.0,
			(screen.Height() - bounds.Height()) / 2.0);

		MoveTo(leftTop);
	}
}
