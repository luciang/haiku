/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <Errors.h>
#include <Message.h>
#include <Region.h>
#include <Window.h>

#include <Slider.h>


rgb_color
_long_to_color_(int32 color)
{
	return *((rgb_color*)&color);
}

int32
_color_to_long_(rgb_color color)
{
	return *((int32*)&color);
}


BSlider::BSlider(BRect frame, const char *name, const char *label, BMessage *message, 
				 int32 minValue, int32 maxValue, thumb_style thumbType, 
				 uint32 resizingMode, uint32 flags)
	: BControl(frame, name, label, message, resizingMode, flags)
{
	fModificationMessage = NULL;
	fSnoozeAmount = 20000;
	fOrientation = B_HORIZONTAL;
	fBarThickness = 6.0f;
	fMinLimitStr = NULL;
	fMaxLimitStr = NULL;
	fMinValue = minValue;
	fMaxValue = maxValue;

	SetValue(0);

	fKeyIncrementValue = 1;
	fHashMarkCount = 0;
	fHashMarks = B_HASH_MARKS_NONE;
	fStyle = thumbType;

	if (Style() == B_BLOCK_THUMB)
		SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_4_TINT));
	else
		SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_4_TINT));

	UseFillColor(false, NULL);

	_InitObject();
}


BSlider::BSlider(BRect frame, const char *name, const char *label, BMessage *message, 
				 int32 minValue, int32 maxValue, orientation posture,
				 thumb_style thumbType, uint32 resizingMode, uint32 flags)
	: BControl(frame, name, label, message, resizingMode, flags)
{
	fModificationMessage = NULL;
	fSnoozeAmount = 20000;
	fOrientation = posture;
	fBarThickness = 6.0f;
	fMinLimitStr = NULL;
	fMaxLimitStr = NULL;
	fMinValue = minValue;
	fMaxValue = maxValue;

	SetValue(0);

	fKeyIncrementValue = 1;
	fHashMarkCount = 0;
	fHashMarks = B_HASH_MARKS_NONE;
	fStyle = thumbType;

	if (Style() == B_BLOCK_THUMB)
		SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_4_TINT));
	else
		SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_4_TINT));

	UseFillColor(false, NULL);

	_InitObject();
}


BSlider::~BSlider()
{
#if USE_OFF_SCREEN_VIEW
	if (fOffScreenBits)
		delete fOffScreenBits;
#endif

	delete fModificationMessage;
	free(fMinLimitStr);
	free(fMaxLimitStr);
}


BSlider::BSlider(BMessage *archive)
	:	BControl (archive)
{
	fModificationMessage = NULL;

	if (archive->HasMessage("_mod_msg")) {
		BMessage *message = new BMessage;

		archive->FindMessage("_mod_msg", message);
		
		SetModificationMessage(message);
	}

	if (archive->FindInt32("_sdelay", &fSnoozeAmount) != B_OK)
		SetSnoozeAmount(20000);

	int32 color;

	if (archive->FindInt32("_fcolor", &color) == B_OK) {
		rgb_color fillColor = _long_to_color_(color);
		UseFillColor(true, &fillColor);
	} else
		UseFillColor(false);

	int32 orient;

	if (archive->FindInt32("_orient", &orient) == B_OK)
		fOrientation = (orientation)orient;
	else
		fOrientation = B_HORIZONTAL;

	fMinLimitStr = NULL;
	fMaxLimitStr = NULL;

	const char *minlbl = NULL, *maxlbl = NULL;

	archive->FindString("_minlbl", &minlbl);
	archive->FindString("_maxlbl", &maxlbl);

	SetLimitLabels(minlbl, maxlbl);

	if (archive->FindInt32("_min", &fMinValue) != B_OK)
		fMinValue = 0;

	if (archive->FindInt32("_max", &fMaxValue) != B_OK)
		fMaxValue = 100;

	if (archive->FindInt32("_incrementvalue", &fKeyIncrementValue) != B_OK)
		fKeyIncrementValue = 1;

	if (archive->FindInt32("_hashcount", &fHashMarkCount) != B_OK)
		fHashMarkCount = 11;

	int16 hashloc;

	if (archive->FindInt16("_hashloc", &hashloc) == B_OK)
		fHashMarks = (hash_mark_location)hashloc;
	else
		fHashMarks = B_HASH_MARKS_NONE;

	int16 sstyle;

	if (archive->FindInt16("_sstyle", &sstyle) == B_OK)
		fStyle = (thumb_style)sstyle;
	else
		fStyle = B_BLOCK_THUMB;

	if (archive->FindInt32("_bcolor", &color) == B_OK)
		SetBarColor(_long_to_color_(color));
	else {
		if (Style() == B_BLOCK_THUMB)
			SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
				B_DARKEN_4_TINT));
		else
			SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
				B_DARKEN_4_TINT));
	}

	float bthickness;

	if (archive->FindFloat("_bthickness", &bthickness) == B_OK)
		fBarThickness = bthickness;
	else
		fBarThickness = 6.0f;

	_InitObject();
}


void
BSlider::_InitObject()
{
	fLocation.x = 0;
	fLocation.y = 0;
	fInitialLocation.x = 0;
	fInitialLocation.y = 0;

#if USE_OFF_SCREEN_VIEW
	fOffScreenBits = NULL;
	fOffScreenView = NULL;
#endif
}


BArchivable*
BSlider::Instantiate(BMessage *archive)
{
	if (validate_instantiation(archive, "BSlider"))
		return new BSlider(archive);

	return NULL;
}


status_t
BSlider::Archive(BMessage *archive, bool deep) const
{
	BControl::Archive(archive, deep);

	if (ModificationMessage())
		archive->AddMessage("_mod_msg", ModificationMessage());

	archive->AddInt32("_sdelay", fSnoozeAmount);

	archive->AddInt32("_bcolor", _color_to_long_(fBarColor));

	if (FillColor(NULL))
		archive->AddInt32("_fcolor", _color_to_long_(fFillColor));

	if (fMinLimitStr)
		archive->AddString("_minlbl", fMinLimitStr);

	if (fMaxLimitStr)
		archive->AddString("_maxlbl", fMaxLimitStr);

	archive->AddInt32("_min", fMinValue);
	archive->AddInt32("_max", fMaxValue);

	archive->AddInt32("_incrementvalue", fKeyIncrementValue);

	archive->AddInt32("_hashcount", fHashMarkCount);

	archive->AddInt16("_hashloc", fHashMarks);
	
	archive->AddInt16("_sstyle", fStyle);

	archive->AddInt32("_orient", fOrientation);

	archive->AddFloat("_bthickness", fBarThickness);

	return B_OK;
}


status_t
BSlider::Perform(perform_code d, void *arg)
{
	return BControl::Perform(d, arg);
}


void
BSlider::WindowActivated(bool state)
{
	BControl::WindowActivated(state);
}


void
BSlider::AttachedToWindow()
{
	ResizeToPreferred();

	fLocation.Set(9.0f, 0.0f);

#if USE_OFF_SCREEN_VIEW
	BRect bounds(Bounds());

	if (!fOffScreenView) {
		fOffScreenView = new BView(bounds, "", B_FOLLOW_ALL, B_WILL_DRAW);

		BFont font;
		GetFont(&font);
		fOffScreenView->SetFont(&font);
	}

	if (!fOffScreenBits) {
		fOffScreenBits = new BBitmap(bounds, B_CMAP8, true, false);

		if (fOffScreenBits && fOffScreenView)
			fOffScreenBits->AddChild(fOffScreenView);

	} else if (fOffScreenView)
		fOffScreenBits->AddChild(fOffScreenView);
#endif // USE_OFF_SCREEN_VIEW

	SetValue(Value());

	BView* view = OffscreenView();

	BControl::AttachedToWindow();

	if (view && Parent()) {
		rgb_color color = Parent()->ViewColor();

/*		fOffScreenBits->Lock();
		fOffScreenView->SetViewColor(color);
		fOffScreenView->SetLowColor(color);
		fOffScreenBits->Unlock();*/

		view->LockLooper();
		view->SetViewColor(B_TRANSPARENT_COLOR);
		view->SetLowColor(color);
		view->UnlockLooper();
	}
}


void
BSlider::AllAttached()
{
	BControl::AllAttached();
}


void
BSlider::AllDetached()
{
	BControl::AllDetached();
}


void
BSlider::DetachedFromWindow()
{
	BControl::DetachedFromWindow();

#if USE_OFF_SCREEN_VIEW
	if (fOffScreenBits) {
		delete fOffScreenBits;
		fOffScreenBits = NULL;
		fOffScreenView = NULL;
	}
#endif
}


void
BSlider::MessageReceived(BMessage *msg)
{
	BControl::MessageReceived(msg);
}


void
BSlider::FrameMoved(BPoint new_position)
{
	BControl::FrameMoved(new_position);
}


void
BSlider::FrameResized(float w,float h)
{
	BControl::FrameResized(w, h);

	BRect bounds(Bounds());

	if (bounds.right <= 0.0f || bounds.bottom <= 0.0f)
		return;

#if USE_OFF_SCREEN_VIEW
	if (fOffScreenBits) {
		fOffScreenBits->RemoveChild(fOffScreenView);
		delete fOffScreenBits;

		fOffScreenView->ResizeTo(bounds.Width(), bounds.Height());
	
		fOffScreenBits = new BBitmap(Bounds(), B_CMAP8, true, false);
		fOffScreenBits->AddChild(fOffScreenView);
	}
#endif

	SetValue(Value());
	// virtual
}


void
BSlider::KeyDown(const char *bytes, int32 numBytes)
{
	if (!IsEnabled() || IsHidden())
		return;

	switch (bytes[0]) {
		case B_LEFT_ARROW:
		case B_DOWN_ARROW: {
			SetValue(Value() - KeyIncrementValue());
			Invoke();
			break;
		}
		case B_RIGHT_ARROW:
		case B_UP_ARROW: {
			SetValue(Value() + KeyIncrementValue());
			Invoke();
			break;
		}
		default:
			BControl::KeyDown(bytes, numBytes);
	}
}


bool
BSlider::_ConstrainPoint(BPoint point, BPoint comparePoint) const
{
	if (fOrientation == B_HORIZONTAL) {
		if (point.x != comparePoint.x) {
			if (point.x < _MinPosition())
				point.x = _MinPosition();
			else if (point.x > _MaxPosition())
				point.x = _MaxPosition();

			return true;
		}
	} else {
		if (point.y != comparePoint.y) {
			if (point.y > _MinPosition())
				point.y = _MinPosition();
			else if (point.y < _MaxPosition())
				point.y = _MaxPosition();

			return true;
		}
	}

	return false;
}


void
BSlider::MouseDown(BPoint point)
{
	if (!IsEnabled())
		return;

	if (BarFrame().Contains(point) || ThumbFrame().Contains(point))
		fInitialLocation = _Location();

	uint32 buttons;
	GetMouse(&point, &buttons, true);

	_ConstrainPoint(point, fInitialLocation);
	SetValue(ValueForPoint(point));

	InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);

	if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
		SetTracking(true);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	} else {
		// synchronous mouse tracking
		BPoint prevPoint;

		while (buttons) {
			prevPoint = point;

			snooze(SnoozeAmount());
			GetMouse(&point, &buttons, true);

			if (_ConstrainPoint(point, prevPoint)) {
				SetValue(ValueForPoint(point));
				InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
			}
		}
	}

	if ((Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) == 0) {
		if (_Location() != fInitialLocation)
			Invoke();
	}
}


void
BSlider::MouseUp(BPoint point)
{
	if (IsTracking()) {
		if (_Location() != fInitialLocation)
			Invoke();

		SetTracking(false);
	} else
		BControl::MouseUp(point);
}


void
BSlider::MouseMoved(BPoint point, uint32 transit, const BMessage *message)
{
	if (IsTracking()) {
		if (_ConstrainPoint(point, _Location())) {
			SetValue(ValueForPoint(point));
			InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
		}
	} else
		BControl::MouseMoved(point, transit, message);
}


void
BSlider::Pulse()
{
	BControl::Pulse();
}


void
BSlider::SetLabel(const char *label)
{
	BControl::SetLabel(label);
}


void
BSlider::SetLimitLabels(const char *minLabel, const char *maxLabel)
{
	if (minLabel) {
		if (fMinLimitStr)
			free(fMinLimitStr);
		fMinLimitStr = strdup(minLabel);
	}

	if (maxLabel) {
		if (fMaxLimitStr)
			free(fMaxLimitStr);
		fMaxLimitStr = strdup(maxLabel);
	}

	// TODO: Auto resizing?!? I would not want this as an app programmer!
	ResizeToPreferred();
	Invalidate();
}


const char*
BSlider::MinLimitLabel() const
{
	return fMinLimitStr;
}


const char*
BSlider::MaxLimitLabel() const
{
	return fMaxLimitStr;
}


void
BSlider::SetValue(int32 value)
{
	if (value < fMinValue)
		value = fMinValue;
	if (value > fMaxValue)
		value = fMaxValue;

	if (value != Value()) {
		BPoint loc;
		float pos = (float)(value - fMinValue) / (float)(fMaxValue - fMinValue) *
			_MaxPosition() - _MinPosition();

		if (fOrientation == B_HORIZONTAL) {
			loc.x = ceil(_MinPosition() + pos);
			loc.y = 0;
		} else {
			loc.x = 0;
			loc.y = floor(_MaxPosition() - pos);
		}

		BRect oldThumbFrame = ThumbFrame();

		if (IsFocus() && Style() == B_TRIANGLE_THUMB) {
			// we need to update the region with the focus mark as well
			// (a method BSlider::FocusMarkFrame() would be nice as well)
			if (fOrientation == B_HORIZONTAL)
				oldThumbFrame.bottom += 2;
			else
				oldThumbFrame.left -= 2;
		}

		// While it would be enough to do this dependent on fUseFillColor,
		// that doesn't work out if DrawBar() has been overridden by a sub class
		if (fOrientation == B_HORIZONTAL)
			oldThumbFrame.top = BarFrame().top;
		else
			oldThumbFrame.right = BarFrame().right;

		_SetLocation(loc);

		BControl::SetValue(value);

		Invalidate(oldThumbFrame | ThumbFrame());
	}
}


int32
BSlider::ValueForPoint(BPoint location) const
{
	return (int32)(((fOrientation == B_HORIZONTAL ? location.x : location.y)
			- _MinPosition())
		* (fMaxValue - fMinValue) / (_MaxPosition() - _MinPosition())) + fMinValue;
}


void
BSlider::SetPosition(float position)
{
	if (position <= 0.0f)
		BControl::SetValue(fMinValue);
	else if (position >= 1.0f)
		BControl::SetValue(fMaxValue);
	else
		BControl::SetValue((int32)(position * (fMaxValue - fMinValue) + fMinValue));
}


float
BSlider::Position() const
{
	return ((float)(Value() - fMinValue) / (float)(fMaxValue - fMinValue));
}


void
BSlider::SetEnabled(bool on)
{
	BControl::SetEnabled(on);
}


void
BSlider::GetLimits(int32 *minimum, int32 *maximum)
{
	*minimum = fMinValue;
	*maximum = fMaxValue;
}


void
BSlider::Draw(BRect updateRect)
{
	// clear out background
	BRegion background(updateRect);
	background.Exclude(BarFrame());
	background.Exclude(ThumbFrame());
	if (background.Frame().IsValid())
		OffscreenView()->FillRegion(&background, B_SOLID_LOW);

	DrawSlider();
}


void
BSlider::DrawSlider()
{
#if USE_OFF_SCREEN_VIEW
	if (!fOffScreenBits)
		return;
#endif

#if USE_OFF_SCREEN_VIEW
	if (fOffScreenBits->Lock()) {
#endif
		DrawBar();
		DrawHashMarks();
		DrawThumb();
		DrawFocusMark();
		DrawText();

#if USE_OFF_SCREEN_VIEW
		fOffscreenView->Sync();
		DrawBitmap(fOffScreenBits, B_ORIGIN);

		fOffScreenBits->Unlock();
	}
#endif
}


void
BSlider::DrawBar()
{
	BRect frame = BarFrame();
	BView *view = OffscreenView();

	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenmax;
	rgb_color darken1;
	rgb_color darken2;
	rgb_color darkenmax;

	rgb_color barColor;
	rgb_color fillColor;

	if (IsEnabled()) {
		lightenmax	= tint_color(no_tint, B_LIGHTEN_MAX_TINT);
		darken1		= tint_color(no_tint, B_DARKEN_1_TINT);
		darken2		= tint_color(no_tint, B_DARKEN_2_TINT);
		darkenmax	= tint_color(no_tint, B_DARKEN_MAX_TINT);
		barColor	= fBarColor;
		fillColor	= fFillColor;
	} else {
		lightenmax	= tint_color(no_tint, B_LIGHTEN_MAX_TINT);
		darken1		= no_tint;
		darken2		= tint_color(no_tint, B_DARKEN_1_TINT);
		darkenmax	= tint_color(no_tint, B_DARKEN_3_TINT);

		barColor.red	= (fBarColor.red + no_tint.red) / 2;
		barColor.green	= (fBarColor.green + no_tint.green) / 2;
		barColor.blue	= (fBarColor.blue + no_tint.blue) / 2;

		fillColor.red	= (fFillColor.red + no_tint.red) / 2;
		fillColor.green	= (fFillColor.green + no_tint.green) / 2;
		fillColor.blue	= (fFillColor.blue + no_tint.blue) / 2;
	}
	
	if (fUseFillColor) {
		if (fOrientation == B_HORIZONTAL) {
			view->SetHighColor(barColor);
			view->FillRect(BRect((float)floor(frame.left + 1 + Position() *
				(frame.Width() - 2)), frame.top, frame.right, frame.bottom));

			view->SetHighColor(fillColor);
			view->FillRect(BRect(frame.left, frame.top,
				(float)floor(frame.left + 1 + Position() * (frame.Width() - 2)),
				frame.bottom));
		} else {
			view->SetHighColor(barColor);
			view->FillRect(BRect(frame.left, frame.top, frame.right,
				(float)floor(frame.bottom - 1 - Position() * (frame.Height() - 2))));

			view->SetHighColor(fillColor);
			view->FillRect(BRect(frame.left,
				(float)floor(frame.bottom - 1 - Position() *
				(frame.Height() - 2)), frame.right, frame.bottom));

		}
	} else {
		view->SetHighColor(barColor);
		view->FillRect(frame);
	}

	view->SetHighColor(darken1);
	view->StrokeLine(BPoint(frame.left, frame.top),
					 BPoint(frame.left + 1.0f, frame.top));
	view->StrokeLine(BPoint(frame.left, frame.bottom),
					 BPoint(frame.left + 1.0f, frame.bottom));
	view->StrokeLine(BPoint(frame.right - 1.0f, frame.top),
					 BPoint(frame.right, frame.top));

	view->SetHighColor(darken2);
	view->StrokeLine(BPoint(frame.left + 1.0f, frame.top),
					 BPoint(frame.right - 1.0f, frame.top));
	view->StrokeLine(BPoint(frame.left, frame.bottom - 1.0f),
					 BPoint(frame.left, frame.top + 1.0f));

	view->SetHighColor(lightenmax);
	view->StrokeLine(BPoint(frame.left + 1.0f, frame.bottom),
					 BPoint(frame.right, frame.bottom));
	view->StrokeLine(BPoint(frame.right, frame.bottom - 1.0f),
					 BPoint(frame.right, frame.top + 1.0f));

	frame.InsetBy(1.0f, 1.0f);

	view->SetHighColor(darkenmax);
	view->StrokeLine(BPoint(frame.left, frame.bottom),
					 BPoint(frame.left, frame.top));
	view->StrokeLine(BPoint(frame.left + 1.0f, frame.top),
					 BPoint(frame.right, frame.top));
}


void
BSlider::DrawHashMarks()
{
	BRect frame = HashMarksFrame();
	BView *view = OffscreenView();

	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenmax;
	rgb_color darken2;

	if (IsEnabled()) {
		lightenmax	= tint_color(no_tint, B_LIGHTEN_MAX_TINT);
		darken2		= tint_color(no_tint, B_DARKEN_2_TINT);
	} else {
		lightenmax	= tint_color(no_tint, B_LIGHTEN_2_TINT);
		darken2		= tint_color(no_tint, B_DARKEN_1_TINT);
	}

	float pos = _MinPosition();
	float factor = (_MaxPosition() - pos) / (fHashMarkCount - 1);

	if (fHashMarks & B_HASH_MARKS_TOP) {

		view->BeginLineArray(fHashMarkCount * 2);

		if (fOrientation == B_HORIZONTAL) {
			for (int32 i = 0; i < fHashMarkCount; i++) {
				view->AddLine(BPoint(pos, frame.top),
							  BPoint(pos, frame.top + 5), darken2);
				view->AddLine(BPoint(pos + 1, frame.top),
							  BPoint(pos + 1, frame.top + 5), lightenmax);

				pos += factor;
			}
		} else {
			for (int32 i = 0; i < fHashMarkCount; i++) {
				view->AddLine(BPoint(frame.left, pos),
							  BPoint(frame.left + 5, pos), darken2);
				view->AddLine(BPoint(frame.left, pos + 1),
							  BPoint(frame.left + 5, pos + 1), lightenmax);

				pos += factor;
			}
		}

		view->EndLineArray();
	}

	pos = _MinPosition();

	if (fHashMarks & B_HASH_MARKS_BOTTOM) {
#if !USE_OFF_SCREEN_VIEW
		if (Style() == B_TRIANGLE_THUMB) {
			// ToDo: this is a temporary workaround to clean out some drawing
			// left-overs when moving the thumb around - might flicker, so we
			// should do it differently
			BRect rect = frame;
			rect.top = frame.bottom - 5;
			FillRect(rect, B_SOLID_LOW);
		}
#endif

		view->BeginLineArray(fHashMarkCount * 2);

		if (fOrientation == B_HORIZONTAL) {
			for (int32 i = 0; i < fHashMarkCount; i++) {
				view->AddLine(BPoint(pos, frame.bottom - 5),
							  BPoint(pos, frame.bottom), darken2);
				view->AddLine(BPoint(pos + 1, frame.bottom - 5),
							  BPoint(pos + 1, frame.bottom), lightenmax);

				pos += factor;
			}
		} else {
			for (int32 i = 0; i < fHashMarkCount; i++) {
				view->AddLine(BPoint(frame.right - 5, pos),
							  BPoint(frame.right, pos), darken2);
				view->AddLine(BPoint(frame.right - 5, pos + 1),
							  BPoint(frame.right, pos + 1), lightenmax);

				pos += factor;
			}
		}

		view->EndLineArray();
	}
}


void
BSlider::DrawThumb()
{
	if (Style() == B_BLOCK_THUMB)
		_DrawBlockThumb();
	else
		_DrawTriangleThumb();
}


void
BSlider::DrawFocusMark()
{
	if (!IsFocus())
		return;

	OffscreenView()->SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));

	BRect frame = ThumbFrame();

	if (fStyle == B_BLOCK_THUMB) {
		frame.left += 2.0f;
		frame.top += 2.0f;
		frame.right -= 3.0f;
		frame.bottom -= 3.0f;
		OffscreenView()->StrokeRect(frame);
	} else {
		if (fOrientation == B_HORIZONTAL) {
			OffscreenView()->StrokeLine(BPoint(frame.left, frame.bottom + 2.0f),
				BPoint(frame.right, frame.bottom + 2.0f));
		} else {
			OffscreenView()->StrokeLine(BPoint(frame.left - 2.0f, frame.top),
				BPoint(frame.left - 2.0f, frame.bottom));
		}
	}
	
}


void
BSlider::DrawText()
{
	BRect bounds(Bounds());
	BView *view = OffscreenView();

	if (IsEnabled()) {
		view->SetHighColor(0, 0, 0);
	} else {
		view->SetHighColor(tint_color(LowColor(), B_DISABLED_LABEL_TINT));
	}

	font_height fheight;

	GetFontHeight(&fheight);

	if (Orientation() == B_HORIZONTAL) {
		if (Label())
			view->DrawString(Label(), BPoint(2.0f, (float)ceil(fheight.ascent)));

		if (fMinLimitStr)
			view->DrawString(fMinLimitStr, BPoint(2.0f, bounds.bottom - 4.0f));

		if (fMaxLimitStr)
			view->DrawString(fMaxLimitStr, BPoint(bounds.right -
												  StringWidth(fMaxLimitStr) - 2.0f,
												  bounds.bottom - 4.0f));
	} else {
		float ascent = (float)ceil(fheight.ascent);

		if (Label())
			view->DrawString(Label(), BPoint(bounds.Width() / 2.0f -
											 StringWidth(Label()) / 2.0f,
											 ascent));

		if (fMaxLimitStr)
			view->DrawString(fMaxLimitStr, BPoint(bounds.Width() / 2.0f -
												  StringWidth(fMaxLimitStr) / 2.0f,
												  ascent +
												  (Label() ? (float)ceil(ascent + fheight.descent + 2.0f)
												  		   : 0.0f)));

		if (fMinLimitStr)
			view->DrawString(fMinLimitStr, BPoint(bounds.Width() / 2.0f -
												  StringWidth(fMinLimitStr) / 2.0f,
												  bounds.bottom - 2.0f));
	}
}


char*
BSlider::UpdateText() const
{
	return NULL;
}


BRect
BSlider::BarFrame() const
{
	BRect frame(Bounds());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float textHeight = (float)ceil(fontHeight.ascent + fontHeight.descent);
	
	if (fStyle == B_BLOCK_THUMB) {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = 8.0f;
			frame.top = 6.0f + (Label() ? textHeight + 4.0f : 0.0f);
			frame.right -= 8.0f;
			frame.bottom = frame.top + fBarThickness;
		} else {
			frame.left = floor((frame.Width() - fBarThickness) / 2.0f);
			frame.top = 12.0f + (Label() ? textHeight : 0.0f) +
				(fMaxLimitStr ? textHeight : 0.0f);
			frame.right = frame.left + fBarThickness;
			frame.bottom = frame.bottom - 8.0f -
				(fMinLimitStr ? textHeight + 4 : 0.0f);
		}
	} else {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = 7.0f;
			frame.top = 6.0f + (Label() ? textHeight + 4.0f : 0.0f);
			frame.right -= 7.0f;
			frame.bottom = frame.top + fBarThickness;
		} else {
			frame.left = floor((frame.Width() - fBarThickness) / 2.0f);
			frame.top = 11.0f + (Label() ? textHeight : 0.0f) +
				(fMaxLimitStr ? textHeight : 0.0f);
			frame.right = frame.left + fBarThickness;
			frame.bottom = frame.bottom - 7.0f -
				(fMinLimitStr ? textHeight + 4 : 0.0f);
		}
	}

	return frame;
}


BRect
BSlider::HashMarksFrame() const
{
	BRect frame(BarFrame());

	if (fOrientation == B_HORIZONTAL) {
		frame.top -= 6.0f;
		frame.bottom += 6.0f;
	} else {
		frame.left -= 6.0f;
		frame.right += 6.0f;
	}

	return frame;
}


BRect
BSlider::ThumbFrame() const
{
	// TODO: The slider looks really ugly and broken when it is too little.
	// I would suggest using BarFrame() here to get the top and bottom coords
	// and spread them further apart for the thumb

	BRect frame = Bounds();
	font_height fheight;

	GetFontHeight(&fheight);

	float textHeight = (float)ceil(fheight.ascent + fheight.descent);

	if (fStyle == B_BLOCK_THUMB) {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = (float)floor(Position() * (_MaxPosition() - _MinPosition()) +
				_MinPosition()) - 8.0f;
			frame.top = 2.0f + (Label() ? textHeight + 4.0f : 0.0f);
			frame.right = frame.left + 17.0f;
			frame.bottom = frame.top + fBarThickness + 7.0f;
		} else {
			frame.left = floor((frame.Width() - fBarThickness) / 2.0f) - 4;
			frame.top = (float)floor(Position() * (_MaxPosition() - _MinPosition()) +
				_MinPosition()) - 8.0f;
			frame.right = frame.left + fBarThickness + 7.0f;
			frame.bottom = frame.top + 17;
		}
	} else {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = (float)floor(Position() * (_MaxPosition() - _MinPosition()) +
				_MinPosition()) - 6;
			frame.right = frame.left + 12.0f;
			frame.bottom = frame.bottom - 2.0f -
				(MinLimitLabel() || MaxLimitLabel() ? textHeight + 4.0f : 0.0f);
			frame.top = frame.bottom - 7.0f;
		} else {
			frame.left = floor((frame.Width() - fBarThickness) / 2.0f) - 3;
			frame.top = (float)floor(Position() * (_MaxPosition() - _MinPosition())) +
				_MinPosition() - 6.0f;
			frame.right = frame.left + 7;
			frame.bottom = frame.top + 12;
		}
	}

	return frame;
}


void
BSlider::SetFlags(uint32 flags)
{
	BControl::SetFlags(flags);
}


void
BSlider::SetResizingMode(uint32 mode)
{
	BControl::SetResizingMode(mode);
}


void
BSlider::GetPreferredSize(float* _width, float* _height)
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);

	int32 rows = 0;

	if (Orientation() == B_HORIZONTAL) {
		*_width = Frame().Width();
		*_height = 12.0f + fBarThickness;

		float labelWidth = 0;
		if (Label()) {
			labelWidth = StringWidth(Label());
			rows++;
		}

		float minWidth = 0;
		if (MinLimitLabel())
			minWidth = StringWidth(MinLimitLabel());
		if (MaxLimitLabel()) {
			// some space between the labels
			if (MinLimitLabel())
				minWidth += 8.0f;

			minWidth += StringWidth(MaxLimitLabel());
		}

		if (minWidth > *_width)
			*_width = minWidth;
		if (labelWidth > *_width)
			*_width = labelWidth;
		if (*_width < 32.0f)
			*_width = 32.0f;

		if (MinLimitLabel() || MaxLimitLabel())
			rows++;

		*_height += rows * ((float)ceil(fontHeight.ascent + fontHeight.descent) + 4.0f);
	} else { 
		// B_VERTICAL
		*_width = 12.0f + fBarThickness;
		*_height = Frame().Height();

		// find largest label

		float minWidth = 0;
		if (Label()) {
			minWidth = StringWidth(Label());
			rows++;
		}
		if (MinLimitLabel()) {
			float width = StringWidth(MinLimitLabel());
			if (width > minWidth)
				minWidth = width;
			rows++;
		}
		if (MaxLimitLabel()) {
			float width = StringWidth(MaxLimitLabel());
			if (width > minWidth)
				minWidth = width;
			rows++;
		}

		if (minWidth > *_width)
			*_width = minWidth;

		float minHeight = 32.0f + rows
			* ((float)ceil(fontHeight.ascent + fontHeight.descent) + 4.0f);

		if (Label() && MaxLimitLabel())
			minHeight -= 4.0f;

		if (minHeight > *_height)
			*_height = minHeight;
	}
}


void
BSlider::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


status_t
BSlider::Invoke(BMessage *msg)
{
	return BControl::Invoke(msg);
}


BHandler*
BSlider::ResolveSpecifier(BMessage *message, int32 index,
						  BMessage *specifier, int32 command,
						  const char *property)
{
	return BControl::ResolveSpecifier(message, index, specifier,
									  command, property);
}


status_t
BSlider::GetSupportedSuites(BMessage *message)
{
	return BControl::GetSupportedSuites(message);
}


void
BSlider::SetModificationMessage(BMessage *message)
{
	if (fModificationMessage)
		delete fModificationMessage;

	fModificationMessage = message;
}


BMessage*
BSlider::ModificationMessage() const
{
	return fModificationMessage;
}


void
BSlider::SetSnoozeAmount(int32 snooze_time)
{
	if (snooze_time < 5000)
		snooze_time = 5000;
	if (snooze_time > 1000000)
		snooze_time = 1000000;

	fSnoozeAmount = snooze_time;
}


int32
BSlider::SnoozeAmount() const
{
	return fSnoozeAmount;
}


void
BSlider::SetKeyIncrementValue(int32 increment_value)
{
	fKeyIncrementValue = increment_value;
}


int32
BSlider::KeyIncrementValue() const
{
	return fKeyIncrementValue;
}


void
BSlider::SetHashMarkCount(int32 hash_mark_count)
{
	fHashMarkCount = hash_mark_count;
	Invalidate();
}


int32
BSlider::HashMarkCount() const
{
	return fHashMarkCount;
}


void
BSlider::SetHashMarks(hash_mark_location where)
{
	fHashMarks = where;
	Invalidate();
}


hash_mark_location
BSlider::HashMarks() const
{
	return fHashMarks;
}


void
BSlider::SetStyle(thumb_style style)
{
	fStyle = style;
	Invalidate();
}


thumb_style
BSlider::Style() const
{
	return fStyle;
}


void
BSlider::SetBarColor(rgb_color bar_color)
{
	fBarColor = bar_color;
	Invalidate();
}


rgb_color
BSlider::BarColor() const
{
	return fBarColor;
}


void
BSlider::UseFillColor(bool use_fill, const rgb_color *bar_color)
{
	fUseFillColor = use_fill;

	if (use_fill && bar_color)
		fFillColor = *bar_color;

	Invalidate();
}


bool
BSlider::FillColor(rgb_color *bar_color) const
{
	if (bar_color && fUseFillColor)
		*bar_color = fFillColor;

	return fUseFillColor;
}


BView*
BSlider::OffscreenView() const
{
#if USE_OFF_SCREEN_VIEW
	return fOffScreenView;
#else
	return (BView*)this;
#endif
}


orientation
BSlider::Orientation() const
{
	return fOrientation;
}


void
BSlider::SetOrientation(orientation posture)
{
	fOrientation = posture;
	Invalidate();
}


float
BSlider::BarThickness() const
{
	return fBarThickness;
}


void
BSlider::SetBarThickness(float thickness)
{
	if (thickness >= 1.0f)
		fBarThickness = thickness;
}


void
BSlider::SetFont(const BFont *font, uint32 properties)
{
	BControl::SetFont(font, properties);

#if USE_OFF_SCREEN_VIEW
	if (fOffScreenView && fOffScreenBits) {
		if (fOffScreenBits->Lock()) {
			fOffScreenView->SetFont(font, properties);
			fOffScreenBits->Unlock();
		}
	}
#endif
}


#ifdef __HAIKU__
void
BSlider::SetLimits(int32 minimum, int32 maximum)
{
	if (minimum <= maximum) {
		fMinValue = minimum;
		fMaxValue = maximum;
	
		int32 value = Value();
		value = max_c(minimum, value);
		value = min_c(maximum, value);

		if (value != Value()) {
			SetValue(value);
		}
	}
}
#endif


void
BSlider::_DrawBlockThumb()
{
	BRect frame = ThumbFrame();
	BView *view = OffscreenView();

	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lighten2;
	rgb_color lighten1;
	rgb_color darken2;
	rgb_color darken3;
	rgb_color darkenmax;

	if (IsEnabled()) {
		lighten2	= tint_color(no_tint, B_LIGHTEN_2_TINT);
		lighten1	= no_tint;
		darken2		= tint_color(no_tint, B_DARKEN_2_TINT);
		darken3		= tint_color(no_tint, B_DARKEN_3_TINT);
		darkenmax	= tint_color(no_tint, B_DARKEN_MAX_TINT);
	} else {
		lighten2	= tint_color(no_tint, B_LIGHTEN_2_TINT);
		lighten1	= tint_color(no_tint, B_LIGHTEN_1_TINT);
		darken2		= tint_color(no_tint, (B_NO_TINT + B_DARKEN_1_TINT) / 2.0);
		darken3		= tint_color(no_tint, B_DARKEN_1_TINT);
		darkenmax	= tint_color(no_tint, B_DARKEN_3_TINT);
	}

	// blank background for shadow
	view->SetHighColor(no_tint);
	view->StrokeLine(BPoint(frame.left, frame.top),
					 BPoint(frame.left, frame.top));

	BRect barFrame = BarFrame();
	if (barFrame.right >= frame.right) {
		// leave out barFrame from shadow background clearing
		view->StrokeLine(BPoint(frame.right, frame.top),
						 BPoint(frame.right, barFrame.top - 1.0f));
		view->StrokeLine(BPoint(frame.right, barFrame.bottom + 1.0f),
						 BPoint(frame.right, frame.bottom));
	} else {
		view->StrokeLine(BPoint(frame.right, frame.top),
						 BPoint(frame.right, frame.bottom));
	}

	view->StrokeLine(BPoint(frame.left, frame.bottom),
					 BPoint(frame.right - 1.0f, frame.bottom));
	view->StrokeLine(BPoint(frame.left, frame.bottom - 1.0f),
					 BPoint(frame.left, frame.bottom - 1.0f));
	view->StrokeLine(BPoint(frame.right - 1.0f, frame.top),
					 BPoint(frame.right - 1.0f, frame.top));

	// Outline (top, left)
	view->SetHighColor(darken3);
	view->StrokeLine(BPoint(frame.left, frame.bottom - 2.0f),
					 BPoint(frame.left, frame.top + 1.0f));
	view->StrokeLine(BPoint(frame.left + 1.0f, frame.top),
					 BPoint(frame.right - 2.0f, frame.top));

	// Shadow
	view->SetHighColor(0, 0, 0, IsEnabled() ? 100 : 50);
	view->SetDrawingMode(B_OP_ALPHA);
	view->StrokeLine(BPoint(frame.right, frame.top + 2.0f),
					 BPoint(frame.right, frame.bottom - 1.0f));
	view->StrokeLine(BPoint(frame.left + 2.0f, frame.bottom),
					 BPoint(frame.right - 1.0f, frame.bottom));

	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(darken3);
	view->StrokeLine(BPoint(frame.right - 1.0f, frame.bottom - 1.0f),
					 BPoint(frame.right - 1.0f, frame.bottom - 1.0f));


	// First bevel
	frame.InsetBy(1.0f, 1.0f);

	view->SetHighColor(darkenmax);
	view->StrokeLine(BPoint(frame.left, frame.bottom),
					 BPoint(frame.right - 1.0f, frame.bottom));
	view->StrokeLine(BPoint(frame.right, frame.bottom - 1.0f),
					 BPoint(frame.right, frame.top));

	view->SetHighColor(lighten2);
	view->StrokeLine(BPoint(frame.left, frame.top),
					 BPoint(frame.left, frame.bottom - 1.0f));
	view->StrokeLine(BPoint(frame.left + 1.0f, frame.top),
					 BPoint(frame.right - 1.0f, frame.top));

	frame.InsetBy(1.0f, 1.0f);

	view->FillRect(BRect(frame.left, frame.top, frame.right - 1.0f, frame.bottom - 1.0f));

	// Second bevel and center dots
	view->SetHighColor(darken2);
	view->StrokeLine(BPoint(frame.left, frame.bottom),
					 BPoint(frame.right, frame.bottom));
	view->StrokeLine(BPoint(frame.right, frame.bottom - 1.0f),
					 BPoint(frame.right, frame.top));

	if (Orientation() == B_HORIZONTAL) {
		view->StrokeLine(BPoint(frame.left + 6.0f, frame.top + 2.0f),
						 BPoint(frame.left + 6.0f, frame.top + 2.0f));
		view->StrokeLine(BPoint(frame.left + 6.0f, frame.top + 4.0f),
						 BPoint(frame.left + 6.0f, frame.top + 4.0f));
		view->StrokeLine(BPoint(frame.left + 6.0f, frame.top + 6.0f),
						 BPoint(frame.left + 6.0f, frame.top + 6.0f));
	} else {
		view->StrokeLine(BPoint(frame.left + 2.0f, frame.top + 6.0f),
						 BPoint(frame.left + 2.0f, frame.top + 6.0f));
		view->StrokeLine(BPoint(frame.left + 4.0f, frame.top + 6.0f),
						 BPoint(frame.left + 4.0f, frame.top + 6.0f));
		view->StrokeLine(BPoint(frame.left + 6.0f, frame.top + 6.0f),
						 BPoint(frame.left + 6.0f, frame.top + 6.0f));
	}

	frame.InsetBy(1.0f, 1.0f);

	// Third bevel
	view->SetHighColor(lighten1);
	view->StrokeLine(BPoint(frame.left, frame.bottom),
					 BPoint(frame.right, frame.bottom));
	view->StrokeLine(BPoint(frame.right, frame.bottom - 1.0f),
					 BPoint(frame.right, frame.top));
}


void
BSlider::_DrawTriangleThumb()
{
	BRect frame = ThumbFrame();
	BView *view = OffscreenView();

	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR),
		lighten1 = tint_color(no_tint, B_LIGHTEN_1_TINT),
//		lighten2 = tint_color(no_tint, B_LIGHTEN_2_TINT),
//		lightenmax = tint_color(no_tint, B_LIGHTEN_MAX_TINT),
//		darken1 = tint_color(no_tint, B_DARKEN_1_TINT),
		darken2 = tint_color(no_tint, B_DARKEN_2_TINT),
//		darken3 = tint_color(no_tint, B_DARKEN_3_TINT),
		darkenmax = tint_color(no_tint, B_DARKEN_MAX_TINT);

	view->SetDrawingMode(B_OP_OVER);

	if (Orientation() == B_HORIZONTAL) {
		view->SetHighColor(lighten1);
		view->FillTriangle(BPoint(frame.left, frame.bottom - 2.0f),
			BPoint(frame.left + 6.0f, frame.top),
			BPoint(frame.right, frame.bottom - 2.0f));

		view->SetHighColor(darkenmax);
		view->StrokeLine(BPoint(frame.right, frame.bottom),
			BPoint(frame.left, frame.bottom));
		view->StrokeLine(BPoint(frame.right, frame.bottom - 1),
			BPoint(frame.left + 6.0f, frame.top));

		view->SetHighColor(darken2);
		view->StrokeLine(BPoint(frame.right - 1, frame.bottom - 1),
			BPoint(frame.left, frame.bottom - 1));
		view->StrokeLine(BPoint(frame.left, frame.bottom - 1),
			BPoint(frame.left + 5.0f, frame.top + 1));

		view->SetHighColor(no_tint);
		view->StrokeLine(BPoint(frame.right - 2, frame.bottom - 2.0f),
			BPoint(frame.left + 3.0f, frame.bottom - 2.0f));
		view->StrokeLine(BPoint(frame.right - 3, frame.bottom - 3.0f),
			BPoint(frame.left + 6.0f, frame.top + 1));
	} else {
		view->SetHighColor(lighten1);
		view->FillTriangle(BPoint(frame.left + 1.0f, frame.top),
			BPoint(frame.left + 7.0f, frame.top + 6.0f),
			BPoint(frame.left + 1.0f, frame.bottom));

		view->SetHighColor(darkenmax);
		view->StrokeLine(BPoint(frame.left, frame.top + 1),
			BPoint(frame.left, frame.bottom));
		view->StrokeLine(BPoint(frame.left + 1.0f, frame.bottom),
			BPoint(frame.left + 7.0f, frame.top + 6.0f));

		view->SetHighColor(darken2);
		view->StrokeLine(BPoint(frame.left, frame.top),
			BPoint(frame.left, frame.bottom - 1));
		view->StrokeLine(BPoint(frame.left + 1.0f, frame.top),
			BPoint(frame.left + 6.0f, frame.top + 5.0f));

		view->SetHighColor(no_tint);
		view->StrokeLine(BPoint(frame.left + 1.0f, frame.top + 2.0f),
			BPoint(frame.left + 1.0f, frame.bottom - 1.0f));
		view->StrokeLine(BPoint(frame.left + 2.0f, frame.bottom - 2.0f),
			BPoint(frame.left + 6.0f, frame.top + 6.0f));
	}

	view->SetDrawingMode(B_OP_COPY);
}


BPoint
BSlider::_Location() const
{
	return fLocation;
}


void
BSlider::_SetLocation(BPoint p)
{
	fLocation = p;
}


float
BSlider::_MinPosition() const
{
	if (fOrientation == B_HORIZONTAL)
		return BarFrame().left + 1.0f;
	else
		return BarFrame().bottom - 1.0f;
}


float
BSlider::_MaxPosition() const
{
	if (fOrientation == B_HORIZONTAL)
		return BarFrame().right - 1.0f;
	else
		return BarFrame().top + 1.0f;
}


extern "C"
void _ReservedSlider4__7BSlider(BSlider *slider, int32 minimum, int32 maximum)
{
#ifdef __HAIKU__
	slider->SetLimits(minimum, maximum);
#endif
}


void BSlider::_ReservedSlider5() {}
void BSlider::_ReservedSlider6() {}
void BSlider::_ReservedSlider7() {}
void BSlider::_ReservedSlider8() {}
void BSlider::_ReservedSlider9() {}
void BSlider::_ReservedSlider10() {}
void BSlider::_ReservedSlider11() {}
void BSlider::_ReservedSlider12() {}


BSlider &
BSlider::operator=(const BSlider &)
{
	return *this;
}

