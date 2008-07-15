/*
 * Copyright 2001-2008, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Frans van Nispen (xlr8@tref.nl)
 *		Ingo Weinhold <ingo_weinhold@gmx.de>
 *		Stephan Aßmus <superstippi@gmx.de>
 */

//!	BStringView draws a non-editable text string.


#include <StringView.h>

#include <LayoutUtils.h>
#include <Message.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


BStringView::BStringView(BRect frame, const char* name, const char* text,
			uint32 resizeMask, uint32 flags)
	:	BView(frame, name, resizeMask, flags),
		fText(text ? strdup(text) : NULL),
		fAlign(B_ALIGN_LEFT),
		fPreferredSize(-1, -1)
{
}


BStringView::BStringView(const char* name, const char* text, uint32 flags)
	:	BView(name, flags),
		fText(text ? strdup(text) : NULL),
		fAlign(B_ALIGN_LEFT),
		fPreferredSize(-1, -1)
{
}


BStringView::BStringView(BMessage* data)
	:	BView(data),
		fText(NULL),
		fPreferredSize(-1, -1)
{
	int32 align;
	if (data->FindInt32("_align", &align) == B_OK)
		fAlign = (alignment)align;
	else
		fAlign = B_ALIGN_LEFT;

	const char* text;
	if (data->FindString("_text", &text) != B_OK)
		text = NULL;

	SetText(text);
}


BArchivable*
BStringView::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "BStringView"))
		return NULL;

	return new BStringView(data);
}


status_t
BStringView::Archive(BMessage* data, bool deep) const
{
	status_t err = BView::Archive(data, deep);

	if (err == B_OK && fText)
		err = data->AddString("_text", fText);

	if (err == B_OK)
		err = data->AddInt32("_align", fAlign);

	return err;
}


BStringView::~BStringView()
{
	free(fText);
}


void
BStringView::SetText(const char* text)
{
	if ((text && fText && !strcmp(text, fText)) || (!text && !fText))
		return;

	free(fText);
	fText = text ? strdup(text) : NULL;

	InvalidateLayout();
	Invalidate();
}


const char*
BStringView::Text() const
{
	return fText;
}


void
BStringView::SetAlignment(alignment flag)
{
	fAlign = flag;
	Invalidate();
}


alignment
BStringView::Alignment() const
{
	return fAlign;
}


void
BStringView::AttachedToWindow()
{
	if (Parent())
		SetViewColor(Parent()->ViewColor());
}


void
BStringView::Draw(BRect bounds)
{
	if (!fText)
		return;

	SetLowColor(ViewColor());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	BRect bounds = Bounds();

	float y = (bounds.top + bounds.bottom - ceilf(fontHeight.ascent)
		- ceilf(fontHeight.descent)) / 2.0 + ceilf(fontHeight.ascent);
	float x;
	switch (fAlign) {
		case B_ALIGN_RIGHT:
			x = bounds.Width() - StringWidth(fText);
			break;

		case B_ALIGN_CENTER:
			x = (bounds.Width() - StringWidth(fText)) / 2.0;
			break;

		default:
			x = 0.0;
			break;
	}

	DrawString(fText, BPoint(x, y));
}


void
BStringView::ResizeToPreferred()
{
	float width, height;
	GetPreferredSize(&width, &height);

	// Resize the width only for B_ALIGN_LEFT (if its large enough already, that is)
	if (Bounds().Width() > width && Alignment() != B_ALIGN_LEFT)
		width = Bounds().Width();

	BView::ResizeTo(width, height);
}


void
BStringView::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fPreferredSize.width;

	if (_height)
		*_height = fPreferredSize.height;
}


void
BStringView::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}


void
BStringView::MouseDown(BPoint point)
{
	BView::MouseDown(point);
}


void
BStringView::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


void
BStringView::MouseMoved(BPoint point, uint32 transit, const BMessage* msg)
{
	BView::MouseMoved(point, transit, msg);
}


void
BStringView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BStringView::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


void
BStringView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
}


BHandler*
BStringView::ResolveSpecifier(BMessage* msg, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	return NULL;
}


void
BStringView::MakeFocus(bool state)
{
	BView::MakeFocus(state);
}


void
BStringView::AllAttached()
{
	BView::AllAttached();
}


void
BStringView::AllDetached()
{
	BView::AllDetached();
}


status_t
BStringView::GetSupportedSuites(BMessage* message)
{
	return BView::GetSupportedSuites(message);
}


void
BStringView::InvalidateLayout(bool descendants)
{
	// invalidate cached preferred size
	fPreferredSize.Set(-1, -1);

	BView::InvalidateLayout(descendants);
}


BSize
BStringView::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


BSize
BStringView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


BSize
BStringView::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


status_t
BStringView::Perform(perform_code d, void* arg)
{
	return B_ERROR;
}


void BStringView::_ReservedStringView1() {}
void BStringView::_ReservedStringView2() {}
void BStringView::_ReservedStringView3() {}


BStringView&
BStringView::operator=(const BStringView&)
{
	// Assignment not allowed (private)
	return *this;
}

BSize
BStringView::_ValidatePreferredSize()
{
	if (fPreferredSize.width < 0) {
		// width
		fPreferredSize.width = ceilf(StringWidth(fText));

		// height
		font_height fontHeight;
		GetFontHeight(&fontHeight);
	
		fPreferredSize.height = ceilf(fontHeight.ascent + fontHeight.descent 
			+ fontHeight.leading);
	}

	return fPreferredSize;
}

