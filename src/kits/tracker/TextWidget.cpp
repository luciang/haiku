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

#include <string.h>
#include <stdlib.h>

#include <Alert.h>
#include <Debug.h>
#include <Directory.h>
#include <MessageFilter.h>
#include <ScrollView.h>
#include <TextView.h>
#include <Volume.h>
#include <Window.h>

#include "Attributes.h"
#include "ContainerWindow.h"
#include "Commands.h"
#include "FSUtils.h"
#include "PoseView.h"
#include "TextWidget.h"
#include "Utilities.h"
#include "WidgetAttributeText.h"


const float kWidthMargin = 20;


BTextWidget::BTextWidget(Model *model, BColumn *column, BPoseView *view)
	:
	fText(WidgetAttributeText::NewWidgetText(model, column, view)),
	fAttrHash(column->AttrHash()),
	fAlignment(column->Alignment()),
	fEditable(column->Editable()),
	fVisible(true),
	fActive(false),
	fSymLink(model->IsSymLink())
{
}


BTextWidget::~BTextWidget()
{
	delete fText;
}


int
BTextWidget::Compare(const BTextWidget &with, BPoseView *view) const
{
	return fText->Compare(*with.fText, view);
}


const char *
BTextWidget::Text(const BPoseView *view) const
{
	StringAttributeText *textAttribute = dynamic_cast<StringAttributeText *>(fText);
	if (textAttribute == NULL)
		return NULL;

	return textAttribute->ValueAsText(view);
}


float
BTextWidget::TextWidth(const BPoseView *pose) const
{
	return fText->Width(pose);
}


float
BTextWidget::PreferredWidth(const BPoseView *pose) const
{
	return fText->PreferredWidth(pose) + 1;
}


BRect
BTextWidget::ColumnRect(BPoint poseLoc, const BColumn *column,
	const BPoseView *view)
{
	if (view->ViewMode() != kListMode) {
		// ColumnRect only makes sense in list view, return
		// CalcRect otherwise
		return CalcRect(poseLoc, column, view);
	}
	BRect result;
	result.left = column->Offset() + poseLoc.x;
	result.right = result.left + column->Width();
	result.bottom = poseLoc.y + view->ListElemHeight() - 1;
	result.top = result.bottom - view->FontHeight();
	return result;
}


BRect
BTextWidget::CalcRectCommon(BPoint poseLoc, const BColumn *column,
	const BPoseView *view, float textWidth)
{
	BRect result;
	if (view->ViewMode() == kListMode) {
		poseLoc.x += column->Offset();

		switch (fAlignment) {
			case B_ALIGN_LEFT:
				result.left = poseLoc.x;
				result.right = result.left + textWidth + 1;
				break;

			case B_ALIGN_CENTER:
				result.left = poseLoc.x + (column->Width() / 2) - (textWidth / 2);
				if (result.left < 0)
					result.left = 0;
				result.right = result.left + textWidth + 1;
				break;

			case B_ALIGN_RIGHT:
				result.right = poseLoc.x + column->Width();
				result.left = result.right - textWidth - 1;
				if (result.left < 0)
					result.left = 0;
				break;
			default:
				TRESPASS();
		}

		result.bottom = poseLoc.y + (view->ListElemHeight() - 1);
	} else {
		if (view->ViewMode() == kIconMode) {
			// large/scaled icon mode
			result.left = poseLoc.x + (view->IconSizeInt() - textWidth) / 2;
		} else {
			// mini icon mode
			result.left = poseLoc.x + B_MINI_ICON + kMiniIconSeparator;
		}

		result.right = result.left + textWidth;
		result.bottom = poseLoc.y + view->IconPoseHeight();

	}
	result.top = result.bottom - view->FontHeight();

	return result;
}


BRect
BTextWidget::CalcRect(BPoint poseLoc, const BColumn *column,
	const BPoseView *view)
{
	return CalcRectCommon(poseLoc, column, view, fText->Width(view));
}


BRect
BTextWidget::CalcOldRect(BPoint poseLoc, const BColumn *column,
	const BPoseView *view)
{
	return CalcRectCommon(poseLoc, column, view, fText->CurrentWidth());
}


BRect
BTextWidget::CalcClickRect(BPoint poseLoc, const BColumn *column,
	const BPoseView* view)
{
	BRect result = CalcRect(poseLoc, column, view);
	if (result.Width() < kWidthMargin) {
		// if resulting rect too narrow, make it a bit wider
		// for comfortable clicking
		if (column && column->Width() < kWidthMargin)
			result.right = result.left + column->Width();
		else
			result.right = result.left + kWidthMargin;
	}
	return result;
}


void
BTextWidget::MouseUp(BRect bounds, BPoseView *view, BPose *pose, BPoint)
{
	// Start editing without delay if the pose was selected recently and this
	// click is not the second click of a doubleclick.
	// If the pose has been selected a long time ago, check again
	// for a double click (inducing a delay).

	// TODO: re-enable modifiers, one should be enough

	if (IsEditable() && pose->IsSelected()) {
		bigtime_t delta = system_time() - pose->SelectionTime();
		bigtime_t doubleClickSpeed;
		get_click_speed(&doubleClickSpeed);
		bigtime_t oldClickSpeed = 2 * doubleClickSpeed;

		// freshly selected and not a double click
		if (delta > doubleClickSpeed && delta < oldClickSpeed) {
			StartEdit(bounds, view, pose);
			return;
		}

		// TODO: reimplement asynchronous
		// selected a longer time ago, redo a double click detection
		if (delta > oldClickSpeed) {
			// check for double click
			bigtime_t doubleClickTime = system_time() + doubleClickSpeed;
			while (system_time() < doubleClickTime) {
				// loop for double-click time and watch the mouse and keyboard

				BPoint point;
				uint32 buttons;
				view->GetMouse(&point, &buttons, false);

				// double click
				if (buttons)
					return;

				// mouse moved too far
				if (!bounds.Contains(point))
					return;

				//if (modifiers() & (B_SHIFT_KEY | B_COMMAND_KEY
				//	| B_CONTROL_KEY | B_MENU_KEY))
				//	// watch the keyboard (ignoring standard locking keys)
				//	break;

				snooze(10000);
			}
			StartEdit(bounds, view, pose);
		}
	}
}


static filter_result
TextViewFilter(BMessage *message, BHandler **, BMessageFilter *filter)
{
	uchar key;
	if (message->FindInt8("byte", (int8 *)&key) != B_OK)
		return B_DISPATCH_MESSAGE;

	BPoseView *poseView = dynamic_cast<BContainerWindow*>(filter->Looper())->
		PoseView();

	if (key == B_RETURN || key == B_ESCAPE) {
		poseView->CommitActivePose(key == B_RETURN);
		return B_SKIP_MESSAGE;
	}

	if (key == B_TAB) {
		if (poseView->ActivePose()) {
			if (message->FindInt32("modifiers") & B_SHIFT_KEY)
				poseView->ActivePose()->EditPreviousWidget(poseView);
			else
				poseView->ActivePose()->EditNextWidget(poseView);
		}

		return B_SKIP_MESSAGE;
	}

	// the BTextView doesn't respect window borders when resizing itself;
	// we try to work-around this "bug" here.

	// find the text editing view
	BView *scrollView = poseView->FindView("BorderView");
	if (scrollView != NULL) {
		BTextView *textView = dynamic_cast<BTextView *>(scrollView->FindView("WidgetTextView"));
		if (textView != NULL) {
			BRect rect = scrollView->Frame();

			if (rect.right + 3 > poseView->Bounds().right
				|| rect.left - 3 < 0)
				textView->MakeResizable(true, NULL);
		}
	}

	return B_DISPATCH_MESSAGE;
}


void
BTextWidget::StartEdit(BRect bounds, BPoseView *view, BPose *pose)
{
	if (!IsEditable())
		return;

	// don't allow editing of the trash directory name
	BEntry entry(pose->TargetModel()->EntryRef());
	if (entry.InitCheck() == B_OK && FSIsTrashDir(&entry))
		return;

	// don't allow editing of the "Disks" icon name
	if (pose->TargetModel()->IsRoot())
		return;

	if (!ConfirmChangeIfWellKnownDirectory(&entry, "rename"))
		return;

	// get bounds with full text length
	BRect rect(bounds);
	BRect textRect(bounds);
	rect.OffsetBy(-2, -1);
	rect.right += 1;

	BFont font;
	view->GetFont(&font);
	BTextView *textView = new BTextView(rect, "WidgetTextView", textRect, &font, 0,
		B_FOLLOW_ALL, B_WILL_DRAW);

	textView->SetWordWrap(false);
	DisallowMetaKeys(textView);
	fText->SetUpEditing(textView);

	textView->AddFilter(new BMessageFilter(B_KEY_DOWN, TextViewFilter));

	rect.right = rect.left + textView->LineWidth() + 3;
	// center new width, if necessary
	if (view->ViewMode() == kIconMode
		|| view->ViewMode() == kListMode && fAlignment == B_ALIGN_CENTER) {
		rect.OffsetBy(bounds.Width() / 2 - rect.Width() / 2, 0);
	}

	rect.bottom = rect.top + textView->LineHeight() + 1;
	textRect = rect.OffsetToCopy(2, 1);
	textRect.right -= 3;
	textRect.bottom--;
	textView->SetTextRect(textRect);

	textRect = view->Bounds();
	bool hitBorder = false;
	if (rect.left < 1)
		rect.left = 1, hitBorder = true;
	if (rect.right > textRect.right)
		rect.right = textRect.right - 2, hitBorder = true;

	textView->MoveTo(rect.LeftTop());
	textView->ResizeTo(rect.Width(), rect.Height());

	BScrollView *scrollView = new BScrollView("BorderView", textView, 0, 0, false,
		false, B_PLAIN_BORDER);
	view->AddChild(scrollView);

	// configure text view
	switch (view->ViewMode()) {
		case kIconMode:
			textView->SetAlignment(B_ALIGN_CENTER);
			break;

		case kMiniIconMode:
			textView->SetAlignment(B_ALIGN_LEFT);
			break;

		case kListMode:
			textView->SetAlignment(fAlignment);
			break;
	}
	textView->MakeResizable(true, hitBorder ? NULL : scrollView);

	view->SetActivePose(pose);		// tell view about pose
	SetActive(true);				// for widget

	textView->SelectAll();
	textView->MakeFocus();

	// make this text widget invisible while we edit it
	SetVisible(false);

	ASSERT(view->Window());	// how can I not have a Window here???

	if (view->Window())
		// force immediate redraw so TextView appears instantly
		view->Window()->UpdateIfNeeded();
}


void
BTextWidget::StopEdit(bool saveChanges, BPoint poseLoc, BPoseView *view,
	BPose *pose, int32 poseIndex)
{
	// find the text editing view
	BView *scrollView = view->FindView("BorderView");
	ASSERT(scrollView);
	if (!scrollView)
		return;

	BTextView *textView = dynamic_cast<BTextView *>(scrollView->FindView("WidgetTextView"));
	ASSERT(textView);
	if (!textView)
		return;

	BColumn *column = view->ColumnFor(fAttrHash);
	ASSERT(column);
	if (!column)
		return;

	if (saveChanges && fText->CommitEditedText(textView)) {
		// we have an actual change, re-sort
		view->CheckPoseSortOrder(pose, poseIndex);
	}

	// make text widget visible again
	SetVisible(true);
	view->Invalidate(ColumnRect(poseLoc, column, view));

	// force immediate redraw so TEView disappears
	scrollView->RemoveSelf();
	delete scrollView;

	ASSERT(view->Window());
	view->Window()->UpdateIfNeeded();
	view->MakeFocus();

	SetActive(false);
}


void
BTextWidget::CheckAndUpdate(BPoint loc, const BColumn *column, BPoseView *view)
{
	BRect oldRect;
	if (view->ViewMode() != kListMode)
		oldRect = CalcOldRect(loc, column, view);

	if (fText->CheckAttributeChanged() && fText->CheckViewChanged(view)) {
		BRect invalRect(ColumnRect(loc, column, view));
		if (view->ViewMode() != kListMode)
			invalRect = invalRect | oldRect;
		view->Invalidate(invalRect);
	}
}


void
BTextWidget::SelectAll(BPoseView *view)
{
	BTextView *text = dynamic_cast<BTextView *>(view->FindView("WidgetTextView"));
	if (text)
		text->SelectAll();
}


void
BTextWidget::Draw(BRect eraseRect, BRect textRect, float, BPoseView *view,
	BView *drawView, bool selected, uint32 clipboardMode, BPoint offset, bool direct)
{
	textRect.OffsetBy(offset);

	if (direct) {
#ifdef __HAIKU__
		// draw selection box if selected
		if (selected) {
#else
		// erase area we're going to draw in
		// NOTE: WidgetTextOutline() is reused for
		// erasing background on R5 here
		if (view->WidgetTextOutline() || selected) {
#endif
			drawView->SetDrawingMode(B_OP_COPY);
			eraseRect.OffsetBy(offset);
//			drawView->FillRect(eraseRect, B_SOLID_LOW);
			drawView->FillRect(textRect, B_SOLID_LOW);
		} else
			drawView->SetDrawingMode(B_OP_OVER);

		// set high color
		rgb_color highColor;
		if (view->IsDesktopWindow()) {
			if (selected)
				highColor = kWhite;
			else
				highColor = view->DeskTextColor();
		} else if (selected && view->Window()->IsActive()) {
			highColor = kWhite;
		} else
			highColor = kBlack;

		if (clipboardMode == kMoveSelectionTo && !selected) {
			drawView->SetDrawingMode(B_OP_ALPHA);
			drawView->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			highColor.alpha = 64;
		}
		drawView->SetHighColor(highColor);
	}

	BPoint loc;
	loc.y = textRect.bottom - view->FontInfo().descent;
	loc.x = textRect.left + 1;

	const char* fittingText = fText->FittingText(view);

#ifdef __HAIKU__
	// TODO: Comparing view and drawView here to avoid rendering
	// the text outline when producing a drag bitmap. The check is
	// not fully correct, since an offscreen view is also used in some
	// other rare cases (something to do with columns). But for now, this
	// fixes the broken drag bitmaps when dragging icons from the Desktop.
	if (!selected && view == drawView && view->WidgetTextOutline()) {
		// draw a halo around the text by using the "false bold"
		// feature for text rendering. Either black or white is used for
		// the glow (whatever acts as contrast) with a some alpha value,
		drawView->SetDrawingMode(B_OP_ALPHA);
		drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);

		BFont font;
		drawView->GetFont(&font);

		rgb_color textColor = drawView->HighColor();
		if (textColor.red + textColor.green + textColor.blue < 128 * 3) {
			// dark text on light outline
			rgb_color glowColor = kWhite;

			font.SetFalseBoldWidth(2.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			glowColor.alpha = 30;
			drawView->SetHighColor(glowColor);

			drawView->DrawString(fittingText, loc);

			font.SetFalseBoldWidth(1.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			glowColor.alpha = 65;
			drawView->SetHighColor(glowColor);

			drawView->DrawString(fittingText, loc);

			font.SetFalseBoldWidth(0.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
		} else {
			// light text on dark outline
			rgb_color outlineColor = kBlack;

			font.SetFalseBoldWidth(1.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			outlineColor.alpha = 30;
			drawView->SetHighColor(outlineColor);

			drawView->DrawString(fittingText, loc);

			font.SetFalseBoldWidth(0.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

			outlineColor.alpha = 200;
			drawView->SetHighColor(outlineColor);

			drawView->DrawString(fittingText, loc + BPoint(1, 1));
		}

		drawView->SetDrawingMode(B_OP_OVER);
		drawView->SetHighColor(textColor);
	}
#endif // __HAIKU__

	drawView->DrawString(fittingText, loc);

	if (fSymLink && (fAttrHash == view->FirstColumn()->AttrHash())) {
		// ToDo:
		// this should be exported to the WidgetAttribute class, probably
		// by having a per widget kind style
		if (direct) {
			rgb_color underlineColor = drawView->HighColor();
			underlineColor.alpha = 180;
			drawView->SetHighColor(underlineColor);
			drawView->SetDrawingMode(B_OP_ALPHA);
			drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		}

		textRect.right = textRect.left + fText->Width(view);
			// only underline text part
		drawView->StrokeLine(textRect.LeftBottom(), textRect.RightBottom(),
			B_MIXED_COLORS);
	}
}
