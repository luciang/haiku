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

#include <Debug.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <Bitmap.h>
#include <Font.h>
#include <Region.h>
#include <Roster.h>
#include <Resources.h>

#include "BarApp.h"
#include "BarMenuBar.h"
#include "ExpandoMenuBar.h"
#include "ResourceSet.h"
#include "ShowHideMenuItem.h"
#include "TeamMenu.h"
#include "TeamMenuItem.h"
#include "WindowMenu.h"
#include "WindowMenuItem.h"


const float kHPad = 8.0f;
const float kVPad = 1.0f;
const float kLabelOffset = 8.0f;
const float kSwitchWidth = 12;


TTeamMenuItem::TTeamMenuItem(BList *team, BBitmap *icon, char *name, char *sig,
	float width, float height, bool drawLabel, bool vertical)
	:	BMenuItem(new TWindowMenu(team, sig))
{
	InitData(team, icon, name, sig, width, height, drawLabel, vertical);
}


TTeamMenuItem::TTeamMenuItem(float width,float height,bool vertical)
	:	BMenuItem("", NULL)
{
	InitData(NULL, NULL, strdup(""), strdup(""), width, height, false, vertical);
}


void
TTeamMenuItem::InitData(BList *team, BBitmap *icon, char *name, char *sig,
	float width, float height, bool drawLabel, bool vertical)
{
	fTeam = team;
	fIcon = icon;
	fName = name;
	fSig = sig;
	if (fName == NULL) {
		char temp[32];
		snprintf(temp, sizeof(temp), "team %ld", (int32)team->ItemAt(0));
		fName = strdup(temp);
	}

	SetLabel(fName);

	BFont font(be_plain_font);
	fLabelWidth = ceilf(font.StringWidth(fName));
	font_height fontHeight;
	font.GetHeight(&fontHeight);
	fLabelAscent = ceilf(fontHeight.ascent);
	fLabelDescent = ceilf(fontHeight.descent + fontHeight.leading);

	fOverrideWidth = width;
	fOverrideHeight = height;

	fDrawLabel = drawLabel;
	fVertical = vertical;

	fExpanded = false;
}


TTeamMenuItem::~TTeamMenuItem()
{
	delete fTeam;
	delete fIcon;
	free(fName);
	free(fSig);
}


status_t
TTeamMenuItem::Invoke(BMessage *message)
{
	if ((static_cast<TBarApp *>(be_app))->BarView()->InvokeItem(Signature()))
		//	handles drop on application
		return B_OK;

	//	if the app could not handle the drag message
	//	and we were dragging, then kill the drag
	//	should never get here, disabled item will not invoke
	TBarView *barview = (static_cast<TBarApp *>(be_app))->BarView();
	if (barview && barview->Dragging())
		barview->DragStop();

	// bring to front or minimize shortcuts
	uint32 mods = modifiers();	
	if (mods & B_CONTROL_KEY) 
		TShowHideMenuItem::TeamShowHideCommon((mods & B_SHIFT_KEY)
				? B_MINIMIZE_WINDOW : B_BRING_TO_FRONT, Teams());

	return BMenuItem::Invoke(message);
}


void
TTeamMenuItem::SetOverrideWidth(float width)
{
	fOverrideWidth = width;
}


void
TTeamMenuItem::SetOverrideHeight(float height)
{
	fOverrideHeight = height;
}


float
TTeamMenuItem::LabelWidth() const
{
	return fLabelWidth;
}


BList *
TTeamMenuItem::Teams() const
{
	return fTeam;
}


const char *
TTeamMenuItem::Signature() const
{
	return fSig;
}


const char *
TTeamMenuItem::Name() const
{
	return fName;
}


void
TTeamMenuItem::GetContentSize(float *width, float *height)
{
	BRect iconBounds;

	if (fIcon)
		iconBounds = fIcon->Bounds();
	else
		iconBounds = BRect(0, 0, 15, 15);

	BMenuItem::GetContentSize(width, height);

	if (fOverrideWidth != -1.0f)
		*width = fOverrideWidth;
	else
		*width = kHPad + iconBounds.Width() + kLabelOffset + fLabelWidth + kHPad + 20;

	if (fOverrideHeight != -1.0f) 
		*height = fOverrideHeight;
	else {
		*height = iconBounds.Height();
		float labelHeight = fLabelAscent + fLabelDescent;
		if (labelHeight > *height)
			*height = labelHeight;
		*height += (kVPad * 2) + 2;
	}
	*height += 2;
}


void
TTeamMenuItem::Draw()
{
	BRect frame(Frame());
	BMenu *menu = Menu();
	menu->PushState();
	rgb_color menuColor = menu->ViewColor();

	//	if not selected or being tracked on, fill with gray
	TBarView *barview = (static_cast<TBarApp *>(be_app))->BarView();
	bool canHandle = !barview->Dragging() || barview->AppCanHandleTypes(Signature());
	if (!IsSelected() && !menu->IsRedrawAfterSticky() || !canHandle || !IsEnabled()) {
		frame.InsetBy(1, 1);
		menu->SetHighColor(menuColor);
		menu->FillRect(frame);
	}

	//	draw the gray, unselected item, border
	if (!IsSelected() || !IsEnabled()) {
		rgb_color shadow = tint_color(menuColor, B_DARKEN_1_TINT);
		rgb_color light = tint_color(menuColor, B_LIGHTEN_2_TINT);

		frame = Frame();
		if (!fVertical)
			frame.top += 1;

		menu->SetHighColor(shadow);
		if (fVertical)
			menu->StrokeLine(frame.LeftBottom(), frame.RightBottom());
		else
			menu->StrokeLine(frame.LeftBottom() + BPoint(1, 0), frame.RightBottom());

		menu->StrokeLine(frame.RightBottom(), frame.RightTop());

		menu->SetHighColor(light);
		menu->StrokeLine(frame.RightTop() + BPoint(-1, 0), frame.LeftTop());
		if (fVertical)
			menu->StrokeLine(frame.LeftTop(), frame.LeftBottom() + BPoint(0, -1));
		else
			menu->StrokeLine(frame.LeftTop(), frame.LeftBottom());
	}

	//	if selected or being tracked on, fill with the hilite gray color
	if (IsEnabled() && IsSelected() && !menu->IsRedrawAfterSticky() && canHandle) {
		// fill
		menu->SetHighColor(tint_color(menuColor, B_HIGHLIGHT_BACKGROUND_TINT));
		menu->FillRect(frame);

		// these continue the dark grey border on the left or top edge
		menu->SetHighColor(tint_color(menuColor, B_DARKEN_4_TINT));
		if (fVertical)
			// dark line at top
			menu->StrokeLine(frame.LeftTop(), frame.RightTop());
		else
			// dark line on the left
			menu->StrokeLine(frame.LeftTop(), frame.LeftBottom());
	} else 
		menu->SetLowColor(menuColor);

	menu->MovePenTo(ContentLocation());
	DrawContent();
	menu->PopState();
}


void
TTeamMenuItem::DrawContent()
{
	BMenu *menu = Menu();
	if (fIcon) {
		if (fIcon->ColorSpace() == B_RGBA32) {
			menu->SetDrawingMode(B_OP_ALPHA);
			menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		} else {
			menu->SetDrawingMode(B_OP_OVER);
		}
		BRect frame(Frame());

		if (!fVertical)
			frame.top += 1;

		BRect iconBounds(fIcon->Bounds());
		BRect dstRect(iconBounds);
		float extra = fVertical ? 0.0f : 1.0f;
		BPoint contLoc = ContentLocation();
		dstRect.OffsetTo(BPoint(contLoc.x + kHPad, contLoc.y + 
			((frame.Height() - iconBounds.Height()) / 2) + extra));
		menu->DrawBitmapAsync(fIcon, dstRect);

		menu->SetDrawingMode(B_OP_COPY);

		float labelHeight = fLabelAscent + fLabelDescent;
		BPoint drawLoc = contLoc + BPoint(kHPad, kVPad);
		drawLoc.x += iconBounds.Width() + kLabelOffset;
		drawLoc.y = frame.top + ((frame.Height() - labelHeight) / 2) + 1.0f;
		menu->MovePenTo(drawLoc);
	}

	//	set the pen to black so that either method will draw in the same color
	//	low color is set in inherited::DrawContent, override makes sure its what we want
	if (fDrawLabel) {
		menu->SetHighColor(0, 0, 0);

		//	override the drawing of the content when the item is disabled
		//	the wrong lowcolor is used when the item is disabled since the
		//	text color does not change
		DrawContentLabel();			
	}

	// Draw the expandable icon.
	TBarView *barView = (static_cast<TBarApp *>(be_app))->BarView();
	if (fVertical && static_cast<TBarApp *>(be_app)->Settings()->superExpando
		&& barView->Expando()) {
		BRect frame(Frame());
		BRect rect(0, 0, kSwitchWidth, 10);
		rect.OffsetTo(BPoint(frame.right - rect.Width(),
			ContentLocation().y + ((frame.Height() - rect.Height()) / 2)));

		rgb_color outlineColor = {80, 80, 80, 255};
		rgb_color middleColor = {200, 200, 200, 255};

		menu->SetDrawingMode(B_OP_OVER);

		if (!fExpanded) {
			menu->BeginLineArray(6);

			menu->AddLine(BPoint(rect.left + 3, rect.top + 1), 
				BPoint(rect.left + 3, rect.bottom - 1), outlineColor);
			menu->AddLine(BPoint(rect.left + 3, rect.top + 1), 
				BPoint(rect.left + 7, rect.top + 5), outlineColor);
			menu->AddLine(BPoint(rect.left + 7, rect.top + 5), 
				BPoint(rect.left + 3, rect.bottom - 1), outlineColor);

			menu->AddLine(BPoint(rect.left + 4, rect.top + 3), 
				BPoint(rect.left + 4, rect.bottom - 3), middleColor);
			menu->AddLine(BPoint(rect.left + 5, rect.top + 4), 
				BPoint(rect.left + 5, rect.bottom - 4), middleColor);
			menu->AddLine(BPoint(rect.left + 5, rect.top + 5), 
				BPoint(rect.left + 6, rect.top + 5), middleColor);
			menu->EndLineArray();
		} else {
			// expanded state

			menu->BeginLineArray(6);
			menu->AddLine(BPoint(rect.left + 1, rect.top + 3), 
				BPoint(rect.right - 3, rect.top + 3), outlineColor);
			menu->AddLine(BPoint(rect.left + 1, rect.top + 3), 
				BPoint(rect.left + 5, rect.top + 7), outlineColor);
			menu->AddLine(BPoint(rect.left + 5, rect.top + 7), 
				BPoint(rect.right - 3, rect.top + 3), outlineColor);

			menu->AddLine(BPoint(rect.left + 3, rect.top + 4), 
				BPoint(rect.right - 5, rect.top + 4), middleColor);
			menu->AddLine(BPoint(rect.left + 4, rect.top + 5), 
				BPoint(rect.right - 6, rect.top + 5), middleColor);
			menu->AddLine(BPoint(rect.left + 5, rect.top + 5), 
				BPoint(rect.left + 5, rect.top + 6), middleColor);
			menu->EndLineArray();
		}
	}
}


void
TTeamMenuItem::DrawContentLabel()
{
	BMenu *menu = Menu();
	menu->MovePenBy(0, fLabelAscent);
	menu->SetDrawingMode(B_OP_COPY);

	float cachedWidth = menu->StringWidth(Label());	
	if (Submenu() && fVertical)
		cachedWidth += 18;

	const char *label = Label();
	char *truncLabel = NULL;
	float max = 0;
	if (static_cast<TBarApp *>(be_app)->Settings()->superExpando && fVertical)
		max = menu->MaxContentWidth() - kSwitchWidth;
	else
		max = menu->MaxContentWidth();

	if (max > 0) {
 		BPoint penloc = menu->PenLocation();
		BRect frame = Frame();
		float offset = penloc.x - frame.left;
	 	if (cachedWidth + offset > max) {
			truncLabel = (char *)malloc(strlen(label) + 4);
			if (!truncLabel)
				return;
			TruncateLabel(max-offset, truncLabel);
			label = truncLabel;
		}
	}

	if (!label)
		label = Label();

	TBarView *barview = (static_cast<TBarApp *>(be_app))->BarView();
	bool canHandle = !barview->Dragging()
		|| barview->AppCanHandleTypes(Signature());
	if (IsSelected() && IsEnabled() && canHandle)
		menu->SetLowColor(tint_color(menu->ViewColor(),
			B_HIGHLIGHT_BACKGROUND_TINT));
	else
		menu->SetLowColor(menu->ViewColor());

	menu->DrawString(label);

	free(truncLabel);
}


bool
TTeamMenuItem::IsExpanded()
{
	return fExpanded;
}


void
TTeamMenuItem::ToggleExpandState(bool resizeWindow)
{
	fExpanded = !fExpanded;
	
	if (fExpanded) {
		// Populate Menu() with the stuff from SubMenu().
		TWindowMenu *sub = (static_cast<TWindowMenu *>(Submenu()));
		if (sub) {
			// force the menu to update it's contents.
			bool locked = sub->LockLooper();
				// if locking the looper failed, the menu is just not visible
			sub->AttachedToWindow();
			if (locked)
				sub->UnlockLooper();

			if (sub->CountItems() > 1){
				TExpandoMenuBar *parent = static_cast<TExpandoMenuBar *>(Menu());
				int myindex = parent->IndexOf(this) + 1;

				TWindowMenuItem *windowItem = NULL;
				int childIndex = 0;
				int totalChildren = sub->CountItems() - 4; // hide, show, close, separator.
				for (; childIndex < totalChildren; childIndex++) {
					windowItem = static_cast<TWindowMenuItem *>(sub->RemoveItem((int32)0));
					parent->AddItem(windowItem, myindex + childIndex);
					windowItem->ExpandedItem(true);
				}
				sub->SetExpanded(true, myindex + childIndex);

				if (resizeWindow)
					parent->SizeWindow();
			} else
				fExpanded = fExpanded;
		}
	} else {
		// Remove the goodies from the Menu() that should be in the SubMenu();
		TWindowMenu *sub = static_cast<TWindowMenu *>(Submenu());

		if (sub) {
			TExpandoMenuBar *parent = static_cast<TExpandoMenuBar *>(Menu());

			TWindowMenuItem *windowItem = NULL;
			int childIndex = parent->IndexOf(this) + 1;
			while (!parent->SubmenuAt(childIndex) && childIndex < parent->CountItems()) {
				windowItem = static_cast<TWindowMenuItem *>(parent->RemoveItem(childIndex));
				sub->AddItem(windowItem, 0);
				windowItem->ExpandedItem(false);
			}
			sub->SetExpanded(false, 0);

			if (resizeWindow)
				parent->SizeWindow();
		} else
			fExpanded = fExpanded;
	}
}


TWindowMenuItem*
TTeamMenuItem::ExpandedWindowItem(int32 id)
{
	if (!fExpanded)	// Paranoia
		return NULL;

	TExpandoMenuBar *parent = static_cast<TExpandoMenuBar *>(Menu());
	int childIndex = parent->IndexOf(this) + 1;

	while (!parent->SubmenuAt(childIndex) && childIndex < parent->CountItems()) {
		TWindowMenuItem *item = static_cast<TWindowMenuItem *>(parent->ItemAt(childIndex));
		if (item->ID() == id)
			return item;

		childIndex++;
	}
	return NULL;
}


BRect
TTeamMenuItem::ExpanderBounds() const
{
	BRect bounds(Frame());
	bounds.left = bounds.right - kSwitchWidth;
	return bounds;
}

