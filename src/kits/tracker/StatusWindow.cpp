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

/*!	A subclass of BWindow that is used to display the status of the Tracker
	operations (copying, deleting, etc.).
*/


#include <Application.h>
#include <Button.h>
#include <ControlLook.h>
#include <Debug.h>
#include <MessageFilter.h>
#include <StringView.h>
#include <String.h>

#include <string.h>

#include "AutoLock.h"
#include "Bitmaps.h"
#include "Commands.h"
#include "StatusWindow.h"
#include "DeskWindow.h"


const float	kDefaultStatusViewHeight = 50;
const float kUpdateGrain = 100000;
const BRect kStatusRect(200, 200, 550, 200);


class TCustomButton : public BButton {
public:
								TCustomButton(BRect frame, uint32 command);
	virtual	void				Draw(BRect updateRect);
private:
			typedef BButton _inherited;
};


class BStatusMouseFilter : public BMessageFilter {
public:
								BStatusMouseFilter();
	virtual	filter_result		Filter(BMessage* message, BHandler** target);
};


namespace BPrivate {
BStatusWindow *gStatusWindow = NULL;
}


BStatusMouseFilter::BStatusMouseFilter()
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN)
{
}


filter_result
BStatusMouseFilter::Filter(BMessage* message, BHandler** target)
{
	// If the target is the status bar, make sure the message goes to the
	// parent view instead.
	if ((*target)->Name() != NULL
		&& strcmp((*target)->Name(), "StatusBar") == 0) {
		BView* view = dynamic_cast<BView*>(*target);
		if (view != NULL)
			view = view->Parent();
		if (view != NULL)
			*target = view;
	}

	return B_DISPATCH_MESSAGE;
}


TCustomButton::TCustomButton(BRect frame, uint32 what)
	:
	BButton(frame, "", "", new BMessage(what), B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW)
{
}


void
TCustomButton::Draw(BRect updateRect)
{
	_inherited::Draw(updateRect);

	if (Message()->what == kStopButton) {
		updateRect = Bounds();
		updateRect.InsetBy(9, 8);
		SetHighColor(0, 0, 0);
		if (Value() == B_CONTROL_ON)
			updateRect.OffsetBy(1, 1);
		FillRect(updateRect);
	} else {
		updateRect = Bounds();
		updateRect.InsetBy(9, 7);
		BRect rect(updateRect);
		rect.right -= 3;

		updateRect.left += 3;
		updateRect.OffsetBy(1, 0);
		SetHighColor(0, 0, 0);
		if (Value() == B_CONTROL_ON) {
			updateRect.OffsetBy(1, 1);
			rect.OffsetBy(1, 1);
		}
		FillRect(updateRect);
		FillRect(rect);
	}
}


BStatusWindow::BStatusWindow()
	:
	BWindow(kStatusRect, "Tracker Status", B_TITLED_WINDOW,
		B_NOT_CLOSABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE,
		B_ALL_WORKSPACES),
	fRetainDesktopFocus(false)
{
	SetSizeLimits(0, 100000, 0, 100000);
	fMouseDownFilter = new BStatusMouseFilter();
	AddCommonFilter(fMouseDownFilter);

	BRect bounds(Bounds());

	BView* view = new BView(bounds, "BackView", B_FOLLOW_ALL, B_WILL_DRAW);
	view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(view);

	Hide();
	Show();
}


BStatusWindow::~BStatusWindow()
{
}


void
BStatusWindow::CreateStatusItem(thread_id thread, StatusWindowState type)
{
	AutoLock<BWindow> lock(this);

	BRect rect(Bounds());
	if (BStatusView* lastView = fViewList.LastItem())
		rect.top = lastView->Frame().bottom + 1;
	rect.bottom = rect.top + kDefaultStatusViewHeight - 1;

	BStatusView* view = new BStatusView(rect, thread, type);
	// the BStatusView will resize itself if needed in its constructor
	ChildAt(0)->AddChild(view);
	fViewList.AddItem(view);

	ResizeTo(Bounds().Width(), view->Frame().bottom);

	// find out if the desktop is the active window
	// if the status window is the only thing to take over active state and
	// desktop was active to begin with, return focus back to desktop
	// when we are done
	bool desktopActive = false;
	{
		AutoLock<BLooper> lock(be_app);
		int32 count = be_app->CountWindows();
		for (int32 index = 0; index < count; index++) {
			if (dynamic_cast<BDeskWindow *>(be_app->WindowAt(index))
				&& be_app->WindowAt(index)->IsActive()) {
				desktopActive = true;
				break;
			}
		}
	}

	if (IsHidden()) {
		fRetainDesktopFocus = desktopActive;
		Minimize(false);
		Show();
	} else
		fRetainDesktopFocus &= desktopActive;
}


void
BStatusWindow::InitStatusItem(thread_id thread, int32 totalItems,
	off_t totalSize, const entry_ref* destDir, bool showCount)
{
	AutoLock<BWindow> lock(this);

	int32 numItems = fViewList.CountItems();
	for (int32 index = 0; index < numItems; index++) {
		BStatusView* view = fViewList.ItemAt(index);
		if (view->Thread() == thread) {
			view->InitStatus(totalItems, totalSize, destDir, showCount);
			break;
		}
	}

}


void
BStatusWindow::UpdateStatus(thread_id thread, const char* curItem,
	off_t itemSize, bool optional)
{
	AutoLock<BWindow> lock(this);

	int32 numItems = fViewList.CountItems();
	for (int32 index = 0; index < numItems; index++) {
		BStatusView* view = fViewList.ItemAt(index);
		if (view->Thread() == thread) {
			view->UpdateStatus(curItem, itemSize, optional);
			break;
		}
	}
}


void
BStatusWindow::RemoveStatusItem(thread_id thread)
{
	AutoLock<BWindow> lock(this);
	BStatusView* winner = NULL;

	int32 numItems = fViewList.CountItems();
	int32 index;
	for (index = 0; index < numItems; index++) {
		BStatusView* view = fViewList.ItemAt(index);
		if (view->Thread() == thread) {
			winner = view;
			break;
		}
	}

	if (winner != NULL) {
		// The height by which the other views will have to be moved (in pixel
		// count).
		float height = winner->Bounds().Height() + 1;
		fViewList.RemoveItem(winner);
		winner->RemoveSelf();
		delete winner;

		if (--numItems == 0 && !IsHidden()) {
			BDeskWindow* desktop = NULL;
			if (fRetainDesktopFocus) {
				AutoLock<BLooper> lock(be_app);
				int32 count = be_app->CountWindows();
				for (int32 index = 0; index < count; index++) {
					desktop = dynamic_cast<BDeskWindow*>(
						be_app->WindowAt(index));
					if (desktop != NULL)
						break;
				}
			}
			Hide();
			if (desktop != NULL) {
				// desktop was active when we first started,
				// make it active again
				desktop->Activate();
			}
		}

		for (; index < numItems; index++)
			fViewList.ItemAt(index)->MoveBy(0, -height);

		ResizeTo(Bounds().Width(), Bounds().Height() - height);
	}
}


bool
BStatusWindow::CheckCanceledOrPaused(thread_id thread)
{
	bool wasCanceled = false;
	bool isPaused = false;

	BStatusView* view = NULL;

	for (;;) {

		AutoLock<BWindow> lock(this);
		// check if cancel or pause hit
		for (int32 index = fViewList.CountItems() - 1; index >= 0; index--) {

			view = fViewList.ItemAt(index);
			if (view && view->Thread() == thread) {
				isPaused = view->IsPaused();
				wasCanceled = view->WasCanceled();
				break;
			}
		}
		lock.Unlock();

		if (wasCanceled || !isPaused)
			break;

		if (isPaused && view != NULL) {
			AutoLock<BWindow> lock(this);
			// say we are paused
			view->Invalidate();
			lock.Unlock();

			ASSERT(find_thread(NULL) == view->Thread());

			// and suspend ourselves
			// we will get resumend from BStatusView::MessageReceived
			suspend_thread(view->Thread());
		}
		break;

	}

	return wasCanceled;
}


bool
BStatusWindow::AttemptToQuit()
{
	// called when tracker is quitting
	// try to cancel all the move/copy/empty trash threads in a nice way
	// by issuing cancels
	int32 count = fViewList.CountItems();

	if (count == 0)
		return true;

	for (int32 index = 0; index < count; index++)
		fViewList.ItemAt(index)->SetWasCanceled();

	// maybe next time everything will have been canceled
	return false;
}


void
BStatusWindow::WindowActivated(bool state)
{
	if (!state)
		fRetainDesktopFocus = false;

	return _inherited::WindowActivated(state);
}


// #pragma mark - BStatusView


BStatusView::BStatusView(BRect bounds, thread_id thread,
	StatusWindowState type)
	:
	BView(bounds, "StatusView", B_FOLLOW_NONE, B_WILL_DRAW),
	fType(type),
	fBitmap(NULL),
	fThread(thread)
{
	Init();

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetLowColor(ViewColor());
	SetHighColor(20, 20, 20);
	SetDrawingMode(B_OP_OVER);

	const float buttonWidth = 22;
	const float buttonHeight = 20;

	BRect rect(bounds);
	rect.OffsetTo(B_ORIGIN);
	rect.left += 40;
	rect.right -= buttonWidth * 2 + 12;
	rect.top += 6;
	rect.bottom = rect.top + 15;

	const char* caption = NULL;
	int32 id = 0;

	switch (type) {
		case kCopyState:
			caption = "Preparing to copy items" B_UTF8_ELLIPSIS;
			id = kResCopyStatusBitmap;
			break;

		case kMoveState:
			caption = "Preparing to move items" B_UTF8_ELLIPSIS;
			id = kResMoveStatusBitmap;
			break;

		case kCreateLinkState:
			caption = "Preparing to create links" B_UTF8_ELLIPSIS;
			id = kResMoveStatusBitmap;
			break;

		case kTrashState:
			caption = "Preparing to empty Trash" B_UTF8_ELLIPSIS;
			id = kResTrashStatusBitmap;
			break;

		case kVolumeState:
			caption = "Searching for disks to mount" B_UTF8_ELLIPSIS;
			break;

		case kDeleteState:
			caption = "Preparing to delete items" B_UTF8_ELLIPSIS;
			id = kResTrashStatusBitmap;
			break;

		case kRestoreFromTrashState:
			caption = "Preparing to restore items" B_UTF8_ELLIPSIS;
			break;

		default:
			TRESPASS();
			break;
	}

	if (caption != NULL) {
		fStatusBar = new BStatusBar(rect, "StatusBar", caption);
		fStatusBar->SetBarHeight(12);
		float width, height;
		fStatusBar->GetPreferredSize(&width, &height);
		fStatusBar->ResizeTo(fStatusBar->Frame().Width(), height);
		AddChild(fStatusBar);

		// Figure out how much room we need to display the additional status
		// message below the bar
		font_height fh;
		GetFontHeight(&fh);
		BRect f = fStatusBar->Frame();
		// Height is 3 x the "room from the top" + bar height + room for
		// string.
		ResizeTo(Bounds().Width(), f.top + f.Height() + fh.leading + fh.ascent
			+ fh.descent + f.top);
	}

	if (id != 0)
	 	GetTrackerResources()->GetBitmapResource(B_MESSAGE_TYPE, id, &fBitmap);

	rect = Bounds();
	rect.left = rect.right - buttonWidth * 2 - 7;
	rect.right = rect.left + buttonWidth;
	rect.top = floorf((rect.top + rect.bottom) / 2 + 0.5) - buttonHeight / 2;
	rect.bottom = rect.top + buttonHeight;

	fPauseButton = new TCustomButton(rect, kPauseButton);
	fPauseButton->ResizeTo(buttonWidth, buttonHeight);
	AddChild(fPauseButton);

	rect.OffsetBy(buttonWidth + 2, 0);
	fStopButton = new TCustomButton(rect, kStopButton);
	fStopButton->ResizeTo(buttonWidth, buttonHeight);
	AddChild(fStopButton);
}


BStatusView::~BStatusView()
{
	delete fBitmap;
}


void
BStatusView::Init()
{
	fDestDir = "";
	fCurItem = 0;
	fPendingStatusString[0] = '\0';
	fWasCanceled = false;
	fIsPaused = false;
	fLastUpdateTime = 0;
	fItemSize = 0;
}


void
BStatusView::InitStatus(int32 totalItems, off_t totalSize,
	const entry_ref* destDir, bool showCount)
{
	Init();
	fTotalSize = totalSize;
	fShowCount = showCount;

	BEntry entry;
	char name[B_FILE_NAME_LENGTH];
	if (destDir && (entry.SetTo(destDir) == B_OK)) {
		entry.GetName(name);
		fDestDir = name;
	}

	BString buffer;
	if (totalItems > 0)
		buffer << "of " << totalItems;

	switch (fType) {
		case kCopyState:
			fStatusBar->Reset("Copying: ", buffer.String());
			break;

		case kCreateLinkState:
			fStatusBar->Reset("Creating Links: ", buffer.String());
			break;

		case kMoveState:
			fStatusBar->Reset("Moving: ", buffer.String());
			break;

		case kTrashState:
			fStatusBar->Reset("Emptying Trash" B_UTF8_ELLIPSIS " ",
				buffer.String());
			break;

		case kDeleteState:
			fStatusBar->Reset("Deleting: ", buffer.String());
			break;

		case kRestoreFromTrashState:
			fStatusBar->Reset("Restoring: ", buffer.String());
			break;

		default:
			break;
	}

	fStatusBar->SetMaxValue(1);
		// SetMaxValue has to be here because Reset changes it to 100
	Invalidate();
}


void
BStatusView::Draw(BRect updateRect)
{
	if (fBitmap) {
		BPoint location;
		location.x = (fStatusBar->Frame().left - fBitmap->Bounds().Width()) / 2;
		location.y = (Bounds().Height()- fBitmap->Bounds().Height()) / 2;
		DrawBitmap(fBitmap, location);
	}

	BRect bounds(Bounds());

	if (be_control_look != NULL) {
		be_control_look->DrawRaisedBorder(this, bounds, updateRect,
		ViewColor());
	} else {
		// draw a frame, which also separates multiple BStatusViews
		rgb_color light = tint_color(ViewColor(), B_LIGHTEN_MAX_TINT);
		rgb_color shadow = tint_color(ViewColor(), B_DARKEN_1_TINT);
		BeginLineArray(4);
			AddLine(BPoint(bounds.left, bounds.bottom - 1.0f),
					BPoint(bounds.left, bounds.top), light);
			AddLine(BPoint(bounds.left + 1.0f, bounds.top),
					BPoint(bounds.right, bounds.top), light);
			AddLine(BPoint(bounds.right, bounds.top + 1.0f),
					BPoint(bounds.right, bounds.bottom), shadow);
			AddLine(BPoint(bounds.right - 1.0f, bounds.bottom),
					BPoint(bounds.left, bounds.bottom), shadow);
		EndLineArray();
	}

	SetHighColor(0, 0, 0);

	BPoint tp = fStatusBar->Frame().LeftBottom();
	font_height fh;
	GetFontHeight(&fh);
	tp.y += ceilf(fh.leading) + ceilf(fh.ascent);

	if (IsPaused())
		DrawString("Paused: click to resume or stop", tp);
	else if (fDestDir.Length()) {
		BString buffer;
		buffer << "To: " << fDestDir;
		SetHighColor(0, 0, 0);
		DrawString(buffer.String(), tp);
	}
}


void
BStatusView::AttachedToWindow()
{
	fPauseButton->SetTarget(this);
	fStopButton->SetTarget(this);
}


void
BStatusView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kPauseButton:
			fIsPaused = !fIsPaused;
			fPauseButton->SetValue(fIsPaused ? B_CONTROL_ON : B_CONTROL_OFF);
			if (!fIsPaused) {

				// force window update
				Invalidate();

				// let 'er rip
				resume_thread(Thread());
			}
			break;

		case kStopButton:
			fWasCanceled = true;
			if (fIsPaused) {
				// resume so that the copy loop gets a chance to finish up
				fIsPaused = false;

				// force window update
				Invalidate();

				// let 'er rip
				resume_thread(Thread());
			}
			break;

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


void
BStatusView::UpdateStatus(const char *curItem, off_t itemSize, bool optional)
{
	float currentTime = system_time();

	if (fShowCount) {

		if (curItem)
			fCurItem++;

		fItemSize += itemSize;

		if (!optional || ((currentTime - fLastUpdateTime) > kUpdateGrain)) {
			if (curItem != NULL || fPendingStatusString[0]) {
				// forced update or past update time

				BString buffer;
				buffer <<  fCurItem << " ";

				// if we don't have curItem, take the one from the stash
				const char *statusItem = curItem != NULL
					? curItem : fPendingStatusString;

				fStatusBar->Update((float)fItemSize / fTotalSize, statusItem,
					buffer.String());

				// we already displayed this item, clear the stash
				fPendingStatusString[0] =  '\0';

				fLastUpdateTime = currentTime;
			}
			else
				// don't have a file to show, just update the bar
				fStatusBar->Update((float)fItemSize / fTotalSize);

			fItemSize = 0;
		} else if (curItem != NULL) {
			// stash away the name of the item we are currently processing
			// so we can show it when the time comes
			strncpy(fPendingStatusString, curItem, 127);
			fPendingStatusString[127] = '0';
		}
	} else {
		fStatusBar->Update((float)fItemSize / fTotalSize);
		fItemSize = 0;
	}
}


void
BStatusView::SetWasCanceled()
{
	fWasCanceled = true;
}

