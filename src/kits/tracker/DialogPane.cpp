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

#include "Window.h"
#include "DialogPane.h"
#include "Thread.h"
#include "Utilities.h"

void 
ViewList::RemoveAll(BView *)
{
	EachListItemIgnoreResult(this, &BView::RemoveSelf);
}

static void
AddSelf(BView *self, BView *to)
{
	to->AddChild(self);
}

void
ViewList::AddAll(BView *toParent)
{
	EachListItem(this, &AddSelf, toParent);
}


DialogPane::DialogPane(BRect mode1Frame, BRect mode2Frame, int32 initialMode,
	const char *name, uint32 followFlags, uint32 flags)
	:	BView(FrameForMode(initialMode, mode1Frame, mode2Frame, mode2Frame),
			name, followFlags, flags),
		fMode1Frame(mode1Frame),
		fMode2Frame(mode2Frame),
		fMode3Frame(mode2Frame)
{
	SetMode(initialMode, true);
}


DialogPane::DialogPane(BRect mode1Frame, BRect mode2Frame, BRect mode3Frame,
	int32 initialMode, const char *name, uint32 followFlags, uint32 flags)
	:	BView(FrameForMode(initialMode, mode1Frame, mode2Frame, mode3Frame),
			name, followFlags, flags),
		fMode1Frame(mode1Frame),
		fMode2Frame(mode2Frame),
		fMode3Frame(mode3Frame)
{
	SetMode(initialMode, true);
}


DialogPane::~DialogPane()
{
	fMode3Items.RemoveAll(this);
	fMode2Items.RemoveAll(this);
}


void 
DialogPane::SetMode(int32 mode, bool initialSetup)
{
	ASSERT(mode < 3 && mode >= 0);
	
	if (!initialSetup && mode == fMode)
		return;
	
	int32 oldMode = fMode;
	fMode = mode;
	
	bool followBottom = (ResizingMode() & B_FOLLOW_BOTTOM) != 0;
	// if we are follow bottom, we will move ourselves, need to place us back
	float bottomOffset = 0;
	if (followBottom)
		bottomOffset = Window()->Bounds().bottom - Frame().bottom;

	BRect newBounds(BoundsForMode(fMode));
	if (!initialSetup)
		ResizeParentWindow(fMode, oldMode);

	ResizeTo(newBounds.Width(), newBounds.Height());

	float delta = 0;
	if (followBottom)
		delta = (Window()->Bounds().bottom - Frame().bottom) - bottomOffset;
	
	if (delta != 0) {
		MoveBy(0, delta);
		if (fLatch && (fLatch->ResizingMode() & B_FOLLOW_BOTTOM))
			fLatch->MoveBy(0, delta);			
	}

	switch (fMode) {
		case 0:
			{
				if (oldMode > 1)
					fMode3Items.RemoveAll(this);
				if (oldMode > 0)
					fMode2Items.RemoveAll(this);
				
				BView *separator = FindView("separatorLine");
				if (separator) {
					BRect frame(separator->Frame());
					frame.InsetBy(-1, -1);
					RemoveChild(separator);
					Invalidate();
				}
					
				AddChild(new SeparatorLine(BPoint(newBounds.left, newBounds.top
					+ newBounds.Height() / 2), newBounds.Width(), false,
					"separatorLine"));
			}
			break;
		case 1:
			{
				if (oldMode > 1) 
					fMode3Items.RemoveAll(this);
				else 
					fMode2Items.AddAll(this);

				BView *separator = FindView("separatorLine");
				if (separator) {
					BRect frame(separator->Frame());
					frame.InsetBy(-1, -1);
					RemoveChild(separator);
					Invalidate();
				}
			}
			break;						
		case 2:
			{
				fMode3Items.AddAll(this);
				if (oldMode < 1) 
					fMode2Items.AddAll(this);
	
				BView *separator = FindView("separatorLine");
				if (separator) {
					BRect frame(separator->Frame());
					frame.InsetBy(-1, -1);
					RemoveChild(separator);
					Invalidate();
				}
			}
			break;						
	}
}

void 
DialogPane::AttachedToWindow()
{
	BView *parent = Parent();
	if (parent) {
		SetViewColor(parent->ViewColor());
		SetLowColor(parent->LowColor());
	}
}

void 
DialogPane::ResizeParentWindow(int32 from, int32 to)
{
	if (!Window())
		return;

	BRect oldBounds = BoundsForMode(from);
	BRect newBounds = BoundsForMode(to);

	BPoint by = oldBounds.RightBottom() - newBounds.RightBottom();
	if (by != BPoint(0, 0))
		Window()->ResizeBy(by.x, by.y);
}

void 
DialogPane::AddItem(BView *view, int32 toMode)
{
	if (toMode == 1)
		fMode2Items.AddItem(view);
	else if (toMode == 2)
		fMode3Items.AddItem(view);
	if (fMode >= toMode)
		AddChild(view);
}

BRect 
DialogPane::FrameForMode(int32 mode)
{
	switch (mode) {
		case 0:
			return fMode1Frame;
		case 1:
			return fMode2Frame;
		case 2:
			return fMode3Frame;
	}
	return fMode1Frame;
}

BRect 
DialogPane::BoundsForMode(int32 mode)
{
	BRect result;
	switch (mode) {
		case 0:
			result = fMode1Frame;
			break;
		case 1:
			result = fMode2Frame;
			break;
		case 2:
			result = fMode3Frame;
			break;
	}
	result.OffsetTo(0, 0);
	return result;
}

BRect 
DialogPane::FrameForMode(int32 mode, BRect mode1Frame, BRect mode2Frame,
	BRect mode3Frame)
{
	switch (mode) {
		case 0:
			return mode1Frame;
		case 1:
			return mode2Frame;
		case 2:
			return mode3Frame;
	}
	return mode1Frame;
}

const uint32 kValueChanged = 'swch';

void 
DialogPane::SetSwitch(BControl *control)
{
	fLatch = control; 
	control->SetMessage(new BMessage(kValueChanged));
	control->SetTarget(this);
}

void 
DialogPane::MessageReceived(BMessage *message)
{
	if (message->what == kValueChanged) {
		int32 value;
		if (message->FindInt32("be:value", &value) == B_OK)
			SetMode(value);
	} else
		_inherited::MessageReceived(message);
}

PaneSwitch::PaneSwitch(BRect frame, const char *name, bool leftAligned,
	uint32 resizeMask, uint32 flags)
	:	BControl(frame, name, "", 0, resizeMask, flags),
		fLeftAligned(leftAligned),
		fPressing(false)
{
}

void 
PaneSwitch::DoneTracking(BPoint point)
{
	BRect bounds(Bounds());
	bounds.InsetBy(-3, -3);

	fPressing = false;
	Invalidate();
	if (bounds.Contains(point)) {
		SetValue(!Value());
		Invoke();
	}
}

void 
PaneSwitch::Track(BPoint point, uint32)
{
	BRect bounds(Bounds());
	bounds.InsetBy(-3, -3);

	bool newPressing = bounds.Contains(point);
	if (newPressing != fPressing) {
		fPressing = newPressing;
		Invalidate();
	}
}


void 
PaneSwitch::MouseDown(BPoint)
{
	if (!IsEnabled())
		return;
	
	fPressing = true;
	MouseDownThread<PaneSwitch>::TrackMouse(this, &PaneSwitch::DoneTracking,
		&PaneSwitch::Track);
	Invalidate();
}


const rgb_color kNormalColor = {150, 150, 150, 255};
const rgb_color kHighlightColor = {100, 100, 0, 255};

void
PaneSwitch::Draw(BRect)
{
	if (fPressing)
		DrawInState(kPressed);
	else if (Value())
		DrawInState(kExpanded);
	else
		DrawInState(kCollapsed);


	rgb_color markColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	
	bool focused = IsFocus() && Window()->IsActive();
	BRect bounds(Bounds());
	BeginLineArray(2);
	AddLine(BPoint(bounds.left + 2, bounds.bottom - 1),
		BPoint(bounds.right - 2, bounds.bottom - 1), focused ? markColor : ViewColor());
	AddLine(BPoint(bounds.left + 2, bounds.bottom),
		BPoint(bounds.right - 2, bounds.bottom), focused ? kWhite : ViewColor());
	EndLineArray();
}

void 
PaneSwitch::DrawInState(PaneSwitch::State state)
{
	BRect rect(0, 0, 10, 10);

	rgb_color outlineColor = {0, 0, 0, 255};
	rgb_color middleColor = state == kPressed ? kHighlightColor : kNormalColor;


	SetDrawingMode(B_OP_COPY);
	
	switch (state) {
		case kCollapsed:
			BeginLineArray(6);
			
			if (fLeftAligned) {
				AddLine(BPoint(rect.left + 3, rect.top + 1), 
					BPoint(rect.left + 3, rect.bottom - 1), outlineColor);
				AddLine(BPoint(rect.left + 3, rect.top + 1), 
					BPoint(rect.left + 7, rect.top + 5), outlineColor);
				AddLine(BPoint(rect.left + 7, rect.top + 5), 
					BPoint(rect.left + 3, rect.bottom - 1), outlineColor);
					
				AddLine(BPoint(rect.left + 4, rect.top + 3), 
					BPoint(rect.left + 4, rect.bottom - 3), middleColor);
				AddLine(BPoint(rect.left + 5, rect.top + 4), 
					BPoint(rect.left + 5, rect.bottom - 4), middleColor);
				AddLine(BPoint(rect.left + 5, rect.top + 5), 
					BPoint(rect.left + 6, rect.top + 5), middleColor);
			} else {
				AddLine(BPoint(rect.right - 3, rect.top + 1), 
					BPoint(rect.right - 3, rect.bottom - 1), outlineColor);
				AddLine(BPoint(rect.right - 3, rect.top + 1), 
					BPoint(rect.right - 7, rect.top + 5), outlineColor);
				AddLine(BPoint(rect.right - 7, rect.top + 5), 
					BPoint(rect.right - 3, rect.bottom - 1), outlineColor);
					
				AddLine(BPoint(rect.right - 4, rect.top + 3), 
					BPoint(rect.right - 4, rect.bottom - 3), middleColor);
				AddLine(BPoint(rect.right - 5, rect.top + 4), 
					BPoint(rect.right - 5, rect.bottom - 4), middleColor);
				AddLine(BPoint(rect.right - 5, rect.top + 5), 
					BPoint(rect.right - 6, rect.top + 5), middleColor);
			}
			EndLineArray();
			break;

		case kPressed:
			BeginLineArray(7);
			if (fLeftAligned) {
				AddLine(BPoint(rect.left + 1, rect.top + 7), 
					BPoint(rect.left + 7, rect.top + 7), outlineColor);
				AddLine(BPoint(rect.left + 7, rect.top + 1), 
					BPoint(rect.left + 7, rect.top + 7), outlineColor);
				AddLine(BPoint(rect.left + 1, rect.top + 7), 
					BPoint(rect.left + 7, rect.top + 1), outlineColor);
					
				AddLine(BPoint(rect.left + 3, rect.top + 6), 
					BPoint(rect.left + 6, rect.top + 6), middleColor);
				AddLine(BPoint(rect.left + 4, rect.top + 5), 
					BPoint(rect.left + 6, rect.top + 5), middleColor);
				AddLine(BPoint(rect.left + 5, rect.top + 4), 
					BPoint(rect.left + 6, rect.top + 4), middleColor);
				AddLine(BPoint(rect.left + 6, rect.top + 3), 
					BPoint(rect.left + 6, rect.top + 4), middleColor);
			} else {
				AddLine(BPoint(rect.right - 1, rect.top + 7), 
					BPoint(rect.right - 7, rect.top + 7), outlineColor);
				AddLine(BPoint(rect.right - 7, rect.top + 1), 
					BPoint(rect.right - 7, rect.top + 7), outlineColor);
				AddLine(BPoint(rect.right - 1, rect.top + 7), 
					BPoint(rect.right - 7, rect.top + 1), outlineColor);
					
				AddLine(BPoint(rect.right - 3, rect.top + 6), 
					BPoint(rect.right - 6, rect.top + 6), middleColor);
				AddLine(BPoint(rect.right - 4, rect.top + 5), 
					BPoint(rect.right - 6, rect.top + 5), middleColor);
				AddLine(BPoint(rect.right - 5, rect.top + 4), 
					BPoint(rect.right - 6, rect.top + 4), middleColor);
				AddLine(BPoint(rect.right - 6, rect.top + 3), 
					BPoint(rect.right - 6, rect.top + 4), middleColor);
			}
			EndLineArray();
			break;

		case kExpanded:
			BeginLineArray(6);
			AddLine(BPoint(rect.left + 1, rect.top + 3), 
				BPoint(rect.right - 1, rect.top + 3), outlineColor);
			AddLine(BPoint(rect.left + 1, rect.top + 3), 
				BPoint(rect.left + 5, rect.top + 7), outlineColor);
			AddLine(BPoint(rect.left + 5, rect.top + 7), 
				BPoint(rect.right - 1, rect.top + 3), outlineColor);

			AddLine(BPoint(rect.left + 3, rect.top + 4), 
				BPoint(rect.right - 3, rect.top + 4), middleColor);
			AddLine(BPoint(rect.left + 4, rect.top + 5), 
				BPoint(rect.right - 4, rect.top + 5), middleColor);
			AddLine(BPoint(rect.left + 5, rect.top + 5), 
				BPoint(rect.left + 5, rect.top + 6), middleColor);
			EndLineArray();
			break;
	}
}

