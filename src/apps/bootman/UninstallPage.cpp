/*
 * Copyright 2008, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 * 
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 */


#include "UninstallPage.h"


#include <Catalog.h>
#include <RadioButton.h>
#include <TextView.h>

#include <string.h>


#define TR_CONTEXT "UninstallPage"


UninstallPage::UninstallPage(BMessage* settings, BRect frame, const char* name)
	: WizardPageView(settings, frame, name, B_FOLLOW_ALL, 
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE)
{
	_BuildUI();
}


UninstallPage::~UninstallPage()
{
}


void
UninstallPage::FrameResized(float width, float height)
{
	WizardPageView::FrameResized(width, height);
	_Layout();
}


static const float kTextDistance = 10;

void
UninstallPage::_BuildUI()
{
	BRect rect(Bounds());
	
	BString text;
	text <<
		TR_CMT("Uninstall Boot Manager", "Title") << "\n\n" <<
		TR("Please locate the Master Boot Record (MBR) save file to "
		"restore from. This is the file that was created when the "
		"boot manager was first installed.");
	fDescription = CreateDescription(rect, "description", text);
	
	MakeHeading(fDescription);
	AddChild(fDescription);
	
	_Layout();
}


void
UninstallPage::_Layout()
{
	LayoutDescriptionVertically(fDescription);	
}

