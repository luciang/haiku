/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

#include <BeBuild.h>
#include <Alert.h>
#include <Box.h>
#include <MenuItem.h>

#include "AutoLock.h"
#include "ContainerWindow.h"
#include "Commands.h"
#include "Screen.h"
#include "SelectionWindow.h"

const int frameThickness = 9;

const uint32 kSelectButtonPressed = 'sbpr';

SelectionWindow::SelectionWindow(BContainerWindow *window)
	:	BWindow(BRect(0, 0, 270, 0),
			"Select", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_V_RESIZABLE
			| B_NO_WORKSPACE_ACTIVATION | B_ASYNCHRONOUS_CONTROLS
			| B_NOT_ANCHORED_ON_ACTIVATE),
		fParentWindow(window)
{
	if (window->Feel() & kPrivateDesktopWindowFeel)
		// The window will not show up if we have B_FLOATING_SUBSET_WINDOW_FEEL
		// and use it with the desktop window since it's never in front.
		SetFeel(B_NORMAL_WINDOW_FEEL);
	
	AddToSubset(fParentWindow);

	BRect backgroundRect = Bounds();
	backgroundRect.InsetBy(-1, -1);
	BView *backgroundView = new BBox(backgroundRect, "bgView", B_FOLLOW_ALL);
	AddChild(backgroundView);

	BMenu *menu = new BPopUpMenu("");
	
	menu->AddItem(new BMenuItem("starts with", NULL));
	menu->AddItem(new BMenuItem("ends with", NULL));
	menu->AddItem(new BMenuItem("contains", NULL));
	menu->AddItem(new BMenuItem("matches wildcard expression", NULL));
	menu->AddItem(new BMenuItem("matches regular expression", NULL));

	menu->SetLabelFromMarked(true);
	menu->ItemAt(3)->SetMarked(true);
		// Set wildcard matching to default.
	
	// Set up the menu field
	fMatchingTypeMenuField = new BMenuField(BRect(7, 6, Bounds().right - 5, 0),
		NULL, "Name", menu);
	backgroundView->AddChild(fMatchingTypeMenuField);
	fMatchingTypeMenuField->SetDivider(fMatchingTypeMenuField->StringWidth("Name") + 8);
	fMatchingTypeMenuField->ResizeToPreferred();
	
	// Set up the expression text control
	fExpressionTextControl = new BTextControl(BRect(7, fMatchingTypeMenuField->
		Bounds().bottom + 11, Bounds().right - 6, 0), NULL, NULL, NULL, NULL,
		B_FOLLOW_LEFT_RIGHT);
	backgroundView->AddChild(fExpressionTextControl);
	fExpressionTextControl->ResizeToPreferred();
	fExpressionTextControl->MakeFocus(true);
	
	// Set up the Invert checkbox
	fInverseCheckBox = new BCheckBox(BRect(7, fExpressionTextControl->Frame().bottom
		+ 6, 6, 6), NULL, "Invert", NULL);
	backgroundView->AddChild(fInverseCheckBox);
	fInverseCheckBox->ResizeToPreferred();
	
	// Set up the Ignore Case checkbox
	fIgnoreCaseCheckBox = new BCheckBox(BRect(fInverseCheckBox->Frame().right + 10,
		fInverseCheckBox->Frame().top, 6, 6), NULL, "Ignore case", NULL);
	fIgnoreCaseCheckBox->SetValue(1);
	backgroundView->AddChild(fIgnoreCaseCheckBox);
	fIgnoreCaseCheckBox->ResizeToPreferred();

	// Set up the Select button
	fSelectButton = new BButton(BRect(0, 0, 5, 5), NULL, "Select",
		new BMessage(kSelectButtonPressed), B_FOLLOW_RIGHT);
		
	backgroundView->AddChild(fSelectButton);
	fSelectButton->ResizeToPreferred();
	fSelectButton->MoveTo(Bounds().right - 10 - fSelectButton->Bounds().right,
		fExpressionTextControl->Frame().bottom + 9);
	fSelectButton->MakeDefault(true);
	#if !B_BEOS_VERSION_DANO
	fSelectButton->SetLowColor(backgroundView->ViewColor());
	fSelectButton->SetViewColor(B_TRANSPARENT_COLOR);
	#endif
	
	font_height fh;
	be_plain_font->GetHeight(&fh);
	// Center the checkboxes vertically to the button
	float topMiddleButton =
		(fSelectButton->Bounds().Height() / 2 -
		(fh.ascent + fh.descent + fh.leading + 4) / 2) + fSelectButton->Frame().top;
	fInverseCheckBox->MoveTo(fInverseCheckBox->Frame().left, topMiddleButton);
	fIgnoreCaseCheckBox->MoveTo(fIgnoreCaseCheckBox->Frame().left, topMiddleButton);

	float bottomMinWidth = 32 + fSelectButton->Bounds().Width() +
		fInverseCheckBox->Bounds().Width() + fIgnoreCaseCheckBox->Bounds().Width();
	float topMinWidth = be_plain_font->StringWidth("Name matches wildcard expression:###");
	float minWidth = bottomMinWidth > topMinWidth ? bottomMinWidth : topMinWidth;

	Run();

	Lock();
	ResizeTo(minWidth, fSelectButton->Frame().bottom + 6);

	SetSizeLimits(
		/* Minimum Width */ minWidth,
		/* Maximum Width */ 1280,
		/* Minimum Height */ Bounds().bottom,
		/* Maximum Height */ Bounds().bottom);

	MoveCloseToMouse();	
	Unlock();
}

void
SelectionWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kSelectButtonPressed:
			{
					Hide();
					// Order of posting and hiding important
					// since we want to activate the target
					// window when the message arrives.
					// (Hide is synhcronous, while PostMessage is not.)
					// See PoseView::SelectMatchingEntries().
					
				BMessage *selectionInfo = new BMessage(kSelectMatchingEntries);
				selectionInfo->AddInt32("ExpressionType", ExpressionType());
				BString expression;
				Expression(expression);
				selectionInfo->AddString("Expression", expression.String());
				selectionInfo->AddBool("InvertSelection", Invert());
				selectionInfo->AddBool("IgnoreCase", IgnoreCase());
				fParentWindow->PostMessage(selectionInfo);
			}
			break;
	
		default:
			_inherited::MessageReceived(message);
	}
}

bool
SelectionWindow::QuitRequested()
{
	Hide();
	return false;
}

void
SelectionWindow::MoveCloseToMouse()
{
	uint32 buttons;
	BPoint mousePosition;
	
	ChildAt((int32)0)->GetMouse(&mousePosition, &buttons);
	ConvertToScreen(&mousePosition);
	
	// Position the window centered around the mouse...
	BPoint windowPosition = BPoint(mousePosition.x - Frame().Width() / 2,
		mousePosition.y	- Frame().Height() / 2);
	
	// ... unless that's outside of the current screen size:
	BScreen screen;
	windowPosition.x = MAX(0, MIN(screen.Frame().right - Frame().Width(),
		windowPosition.x));
	windowPosition.y = MAX(0, MIN(screen.Frame().bottom - Frame().Height(),
		windowPosition.y));
	
	MoveTo(windowPosition);
}



TrackerStringExpressionType
SelectionWindow::ExpressionType() const
{
	if (!fMatchingTypeMenuField->LockLooper())
		return kNone;
		
	BMenuItem *item = fMatchingTypeMenuField->Menu()->FindMarked();
	if (!item) {
		fMatchingTypeMenuField->UnlockLooper();
		return kNone;
	}
	
	int32 index = fMatchingTypeMenuField->Menu()->IndexOf(item);

	fMatchingTypeMenuField->UnlockLooper();
	
	if (index < kStartsWith || index > kRegexpMatch)
		return kNone;
	
	TrackerStringExpressionType typeArray[] = {	kStartsWith, kEndsWith,
		kContains, kGlobMatch, kRegexpMatch};
	
	return typeArray[index];
}

void
SelectionWindow::Expression(BString &result) const
{
	if (!fExpressionTextControl->LockLooper())
		return;
		
	result = fExpressionTextControl->Text();

	fExpressionTextControl->UnlockLooper();
}

bool
SelectionWindow::IgnoreCase() const
{
	if (!fIgnoreCaseCheckBox->LockLooper())
		return true; // default action.
	
	bool ignore = fIgnoreCaseCheckBox->Value() != 0;

	fIgnoreCaseCheckBox->UnlockLooper();

	return ignore;
}

bool
SelectionWindow::Invert() const
{
	if (!fInverseCheckBox->LockLooper())
		return false; // default action.
	
	bool inverse = fInverseCheckBox->Value() != 0;

	fInverseCheckBox->UnlockLooper();

	return inverse;
}
