/*
 * Copyright 2001-2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <Slider.h>

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <Errors.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <Region.h>
#include <String.h>
#include <Window.h>


BSlider::BSlider(BRect frame, const char* name, const char* label,
			BMessage* message, int32 minValue, int32 maxValue,
			thumb_style thumbType, uint32 resizingMode, uint32 flags)
	: BControl(frame, name, label, message, resizingMode, flags),
	fModificationMessage(NULL),
	fSnoozeAmount(20000),

	fMinLimitLabel(NULL),
	fMaxLimitLabel(NULL),

	fMinValue(minValue),
	fMaxValue(maxValue),
	fKeyIncrementValue(1),

	fHashMarkCount(0),
	fHashMarks(B_HASH_MARKS_NONE),

	fStyle(thumbType),

	fOrientation(B_HORIZONTAL),
	fBarThickness(6.0)
{
	SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_4_TINT));

	UseFillColor(false, NULL);

	_InitObject();
	SetValue(0);
}


BSlider::BSlider(BRect frame, const char *name, const char *label,
			BMessage *message, int32 minValue, int32 maxValue,
			orientation posture, thumb_style thumbType, uint32 resizingMode,
			uint32 flags)
	: BControl(frame, name, label, message, resizingMode, flags),
	fModificationMessage(NULL),
	fSnoozeAmount(20000),

	fMinLimitLabel(NULL),
	fMaxLimitLabel(NULL),

	fMinValue(minValue),
	fMaxValue(maxValue),
	fKeyIncrementValue(1),

	fHashMarkCount(0),
	fHashMarks(B_HASH_MARKS_NONE),

	fStyle(thumbType),

	fOrientation(posture),
	fBarThickness(6.0)
{
	SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_4_TINT));

	UseFillColor(false, NULL);

	_InitObject();
	SetValue(0);
}


BSlider::BSlider(const char *name, const char *label, BMessage *message, 
			int32 minValue, int32 maxValue, orientation posture,
			thumb_style thumbType, uint32 flags)
	: BControl(name, label, message, flags),
	fModificationMessage(NULL),
	fSnoozeAmount(20000),

	fMinLimitLabel(NULL),
	fMaxLimitLabel(NULL),

	fMinValue(minValue),
	fMaxValue(maxValue),
	fKeyIncrementValue(1),

	fHashMarkCount(0),
	fHashMarks(B_HASH_MARKS_NONE),

	fStyle(thumbType),

	fOrientation(posture),
	fBarThickness(6.0)
{
	SetBarColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_4_TINT));

	UseFillColor(false, NULL);

	_InitObject();
	SetValue(0);
}


BSlider::BSlider(BMessage *archive)
	: BControl(archive)
{
	fModificationMessage = NULL;

	if (archive->HasMessage("_mod_msg")) {
		BMessage* message = new BMessage;

		archive->FindMessage("_mod_msg", message);
		
		SetModificationMessage(message);
	}

	if (archive->FindInt32("_sdelay", &fSnoozeAmount) != B_OK)
		SetSnoozeAmount(20000);

	rgb_color color;
	if (archive->FindInt32("_fcolor", (int32 *)&color) == B_OK)
		UseFillColor(true, &color);
	else
		UseFillColor(false);

	int32 orient;
	if (archive->FindInt32("_orient", &orient) == B_OK)
		fOrientation = (orientation)orient;
	else
		fOrientation = B_HORIZONTAL;

	fMinLimitLabel = NULL;
	fMaxLimitLabel = NULL;

	const char* minlbl = NULL;
	const char* maxlbl = NULL;

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

	if (archive->FindInt32("_bcolor", (int32 *)&color) != B_OK)
		color = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_4_TINT);
	SetBarColor(color);

	float bthickness;
	if (archive->FindFloat("_bthickness", &bthickness) == B_OK)
		fBarThickness = bthickness;
	else
		fBarThickness = 6.0f;

	_InitObject();
}


BSlider::~BSlider()
{
#if USE_OFF_SCREEN_VIEW
	delete fOffScreenBits;
#endif

	delete fModificationMessage;
	free(fMinLimitLabel);
	free(fMaxLimitLabel);
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

	fUpdateText = NULL;
	fMinSize.Set(-1, -1);
	fMaxUpdateTextWidth = -1.0;
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
	status_t ret = BControl::Archive(archive, deep);

	if (ModificationMessage() && ret == B_OK)
		ret = archive->AddMessage("_mod_msg", ModificationMessage());

	if (ret == B_OK)
		ret = archive->AddInt32("_sdelay", fSnoozeAmount);
	if (ret == B_OK)
		ret = archive->AddInt32("_bcolor", (const uint32 &)fBarColor);

	if (FillColor(NULL) && ret == B_OK)
		ret = archive->AddInt32("_fcolor", (const uint32 &)fFillColor);

	if (ret == B_OK && fMinLimitLabel)
		ret = archive->AddString("_minlbl", fMinLimitLabel);

	if (ret == B_OK && fMaxLimitLabel)
		ret = archive->AddString("_maxlbl", fMaxLimitLabel);

	if (ret == B_OK)
		ret = archive->AddInt32("_min", fMinValue);
	if (ret == B_OK)
		ret = archive->AddInt32("_max", fMaxValue);

	if (ret == B_OK)
		ret = archive->AddInt32("_incrementvalue", fKeyIncrementValue);
	if (ret == B_OK)
		ret = archive->AddInt32("_hashcount", fHashMarkCount);
	if (ret == B_OK)
		ret = archive->AddInt16("_hashloc", fHashMarks);
	if (ret == B_OK)	
		ret = archive->AddInt16("_sstyle", fStyle);
	if (ret == B_OK)
		ret = archive->AddInt32("_orient", fOrientation);
	if (ret == B_OK)
		ret = archive->AddFloat("_bthickness", fBarThickness);

	return ret;
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
	fUpdateText = UpdateText();

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

	BControl::AttachedToWindow();

	BView* view = OffscreenView();
	if (view) {
		rgb_color color = ViewColor();
		if (Parent() != NULL)
			color = Parent()->ViewColor();

/*		fOffScreenBits->Lock();
		fOffScreenView->SetViewColor(color);
		fOffScreenView->SetLowColor(color);
		fOffScreenBits->Unlock();*/

		view->LockLooper();
		view->SetViewColor(B_TRANSPARENT_COLOR);
		view->SetLowColor(color);
		view->UnlockLooper();
	}

	SetValue(Value());
		// makes sure the value is within valid bounds
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

	Invalidate();
}


void
BSlider::KeyDown(const char *bytes, int32 numBytes)
{
	if (!IsEnabled() || IsHidden())
		return;

	int32 newValue = Value();

	switch (bytes[0]) {
		case B_LEFT_ARROW:
		case B_DOWN_ARROW: {
			newValue -= KeyIncrementValue();
			break;
		}
		case B_RIGHT_ARROW:
		case B_UP_ARROW: {
			newValue += KeyIncrementValue();
			break;
		}
		case B_HOME:
			newValue = fMinValue;
			break;
		case B_END:
			newValue = fMaxValue;
			break;
		default:
			BControl::KeyDown(bytes, numBytes);
			return;
	}

	if (newValue != Value()) {
		SetValue(newValue);
		InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
	}
}


/*!
	Makes sure the \a point is within valid bounds.
	Returns \c true if the relevant coordinate (depending on the orientation
	of the slider) differs from \a comparePoint.
*/
bool
BSlider::_ConstrainPoint(BPoint& point, BPoint comparePoint) const
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

	if (_Location() != fInitialLocation)
		InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);

	if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
		SetTracking(true);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
	} else {
		// synchronous mouse tracking
		BPoint prevPoint;

		while (buttons) {
			prevPoint = point;

			snooze(SnoozeAmount());
			GetMouse(&point, &buttons, true);

			if (_ConstrainPoint(point, prevPoint)) {
				int32 value = ValueForPoint(point);
				if (value != Value()) {
					SetValue(value);
					InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
				}
			}
		}
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
			int32 value = ValueForPoint(point);
			if (value != Value()) {
				SetValue(value);
				InvokeNotify(ModificationMessage(), B_CONTROL_MODIFIED);
			}
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
	free(fMinLimitLabel);
	fMinLimitLabel = minLabel ? strdup(minLabel) : NULL;

	free(fMaxLimitLabel);
	fMaxLimitLabel = maxLabel ? strdup(maxLabel) : NULL;

	InvalidateLayout();

	// TODO: This is for backwards compatibility and should
	// probably be removed when breaking binary compatiblity.
	// Applications like our own Mouse rely on this behavior.
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		ResizeToPreferred();

	Invalidate();
}


const char*
BSlider::MinLimitLabel() const
{
	return fMinLimitLabel;
}


const char*
BSlider::MaxLimitLabel() const
{
	return fMaxLimitLabel;
}


void
BSlider::SetValue(int32 value)
{
	if (value < fMinValue)
		value = fMinValue;
	if (value > fMaxValue)
		value = fMaxValue;

	if (value == Value())
		return;

	BPoint loc;
	float range = (float)(fMaxValue - fMinValue);
	if (range == 0)
		range = 1;

	float pos = (float)(value - fMinValue) / range *
		_MaxPosition() - _MinPosition();

	if (fOrientation == B_HORIZONTAL) {
		loc.x = ceil(_MinPosition() + pos);
		loc.y = 0;
	} else {
		loc.x = 0;
		loc.y = floor(_MaxPosition() - pos);
	}

	BRect oldThumbFrame = ThumbFrame();

	// While it would be enough to do this dependent on fUseFillColor,
	// that doesn't work out if DrawBar() has been overridden by a sub class
	if (fOrientation == B_HORIZONTAL)
		oldThumbFrame.top = BarFrame().top;
	else
		oldThumbFrame.right = BarFrame().right;

	_SetLocation(loc);

	BControl::SetValueNoUpdate(value);
	BRect invalid = oldThumbFrame | ThumbFrame();

	if (Style() == B_TRIANGLE_THUMB) {
		// 1) we need to take care of pixels touched because of
		//    anti-aliasing
		// 2) we need to update the region with the focus mark as well
		//    (a method BSlider::FocusMarkFrame() would be nice as well)
		if (fOrientation == B_HORIZONTAL) {
			if (IsFocus())
				invalid.bottom += 2;
			invalid.InsetBy(-1, 0);
		} else {
			if (IsFocus())
				invalid.left -= 2;
			invalid.InsetBy(0, -1);
		}
	}

	Invalidate(invalid);

	UpdateTextChanged();
}


int32
BSlider::ValueForPoint(BPoint location) const
{
	float min;
	float max;
	float position;
	if (fOrientation == B_HORIZONTAL) {
		min = _MinPosition();
		max = _MaxPosition();
		position = location.x;
	} else {
		max = _MinPosition();
		min = _MaxPosition();
		position = min + (max - location.y);
	}

	if (position < min)
		position = min;
	if (position > max)
		position = max;

	return (int32)roundf(((position - min) * (fMaxValue - fMinValue) / (max - min)) + fMinValue);
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
	float range = (float)(fMaxValue - fMinValue);
	if (range == 0.0f)
		range = 1.0f;

	return (float)(Value() - fMinValue) / range;
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


// #pragma mark - drawing


void
BSlider::Draw(BRect updateRect)
{
	// clear out background
	BRegion background(updateRect);
	background.Exclude(BarFrame());

	// ToDo: the triangle thumb doesn't delete its background, so we still have
	// to do it Note, this also creates a different behaviour for subclasses,
	// depending on the thumb style - if possible this should be avoided.
	if (Style() == B_BLOCK_THUMB)
		background.Exclude(ThumbFrame());

#if USE_OFF_SCREEN_VIEW
	if (!fOffScreenBits)
		return;

	if (fOffScreenBits->Lock()) {
		fOffScreenView->SetViewColor(ViewColor());
		fOffScreenView->SetLowColor(ViewColor());
#endif

		if (background.Frame().IsValid())
			OffscreenView()->FillRegion(&background, B_SOLID_LOW);

#if USE_OFF_SCREEN_VIEW
		fOffScreenView->Sync();
		fOffScreenBits->Unlock();
	}
#endif

	DrawSlider();
}


void
BSlider::DrawSlider()
{
	if (LockLooper()) {
#if USE_OFF_SCREEN_VIEW
		if (!fOffScreenBits)
			return;
		if (fOffScreenBits->Lock()) {
#endif
			DrawBar();
			DrawHashMarks();
			DrawThumb();
			DrawFocusMark();
			DrawText();

#if USE_OFF_SCREEN_VIEW
			fOffScreenView->Sync();
			fOffScreenBits->Unlock();
	
			DrawBitmap(fOffScreenBits, B_ORIGIN);
		}
#endif
		UnlockLooper();
	}
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

	// exclude the block thumb from the bar filling

	BRect lowerFrame = frame.InsetByCopy(1, 1);
	lowerFrame.top++;
	lowerFrame.left++;
	BRect upperFrame = lowerFrame;
	BRect thumbFrame;

	if (Style() == B_BLOCK_THUMB) {
		thumbFrame = ThumbFrame();

		if (fOrientation == B_HORIZONTAL) {
			lowerFrame.right = thumbFrame.left;
			upperFrame.left = thumbFrame.right;
		} else {
			lowerFrame.top = thumbFrame.bottom;
			upperFrame.bottom = thumbFrame.top;
		}
	} else if (fUseFillColor) {
		if (fOrientation == B_HORIZONTAL) {
			lowerFrame.right = floor(lowerFrame.left - 1 + Position()
				* (lowerFrame.Width() + 1));
			upperFrame.left = lowerFrame.right;
		} else {
			lowerFrame.top = floor(lowerFrame.bottom + 1 - Position()
				* (lowerFrame.Height() + 1));
			upperFrame.bottom = lowerFrame.top;
		}
	}

	view->SetHighColor(barColor);
	view->FillRect(upperFrame);

	if (Style() == B_BLOCK_THUMB || fUseFillColor) {
		if (fUseFillColor)
			view->SetHighColor(fillColor);
		view->FillRect(lowerFrame);
	}

	if (Style() == B_BLOCK_THUMB) {
		// We don't want to stroke the lines over the thumb

		PushState();

		BRegion region;
		GetClippingRegion(&region);
		region.Exclude(thumbFrame);
		ConstrainClippingRegion(&region);
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

	if (Style() == B_BLOCK_THUMB)
		PopState();
}


void
BSlider::DrawHashMarks()
{
	if (fHashMarks == B_HASH_MARKS_NONE)
		return;

	BRect frame = HashMarksFrame();
	BView* view = OffscreenView();

	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenmax;
	rgb_color darken2;

	if (IsEnabled()) {
		lightenmax = tint_color(no_tint, B_LIGHTEN_MAX_TINT);
		darken2 = tint_color(no_tint, B_DARKEN_2_TINT);
	} else {
		lightenmax = tint_color(no_tint, B_LIGHTEN_2_TINT);
		darken2 = tint_color(no_tint, B_DARKEN_1_TINT);
	}

	float pos = _MinPosition();
	int32 hashMarkCount = max_c(fHashMarkCount, 2);
		// draw at least two hashmarks at min/max if
		// fHashMarks != B_HASH_MARKS_NONE
	float factor = (_MaxPosition() - pos) / (hashMarkCount - 1);

	if (fHashMarks & B_HASH_MARKS_TOP) {

		view->BeginLineArray(hashMarkCount * 2);

		if (fOrientation == B_HORIZONTAL) {
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(pos, frame.top),
							  BPoint(pos, frame.top + 5), darken2);
				view->AddLine(BPoint(pos + 1, frame.top),
							  BPoint(pos + 1, frame.top + 5), lightenmax);

				pos += factor;
			}
		} else {
			for (int32 i = 0; i < hashMarkCount; i++) {
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

		view->BeginLineArray(hashMarkCount * 2);

		if (fOrientation == B_HORIZONTAL) {
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(pos, frame.bottom - 5),
							  BPoint(pos, frame.bottom), darken2);
				view->AddLine(BPoint(pos + 1, frame.bottom - 5),
							  BPoint(pos + 1, frame.bottom), lightenmax);

				pos += factor;
			}
		} else {
			for (int32 i = 0; i < hashMarkCount; i++) {
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

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	if (Orientation() == B_HORIZONTAL) {
		if (Label())
			view->DrawString(Label(), BPoint(0.0, ceilf(fontHeight.ascent)));

		// the update text is updated in SetValue() only
		if (fUpdateText != NULL) {
			view->DrawString(fUpdateText, BPoint(bounds.right
				- StringWidth(fUpdateText), ceilf(fontHeight.ascent)));
		}

		if (fMinLimitLabel) {
			view->DrawString(fMinLimitLabel, BPoint(0.0, bounds.bottom
				- fontHeight.descent));
		}

		if (fMaxLimitLabel) {
			view->DrawString(fMaxLimitLabel, BPoint(bounds.right
				- StringWidth(fMaxLimitLabel), bounds.bottom
				- fontHeight.descent));
		}
	} else {
		float lineHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent)
			+ ceilf(fontHeight.leading);
		float baseLine = ceilf(fontHeight.ascent);

		if (Label()) {
			view->DrawString(Label(), BPoint((bounds.Width()
				- StringWidth(Label())) / 2.0, baseLine));
			baseLine += lineHeight;
		}

		if (fMaxLimitLabel) {
			view->DrawString(fMaxLimitLabel, BPoint((bounds.Width()
				- StringWidth(fMaxLimitLabel)) / 2.0, baseLine));
		}

		baseLine = bounds.bottom - ceilf(fontHeight.descent);

		if (fMinLimitLabel) {
			view->DrawString(fMinLimitLabel, BPoint((bounds.Width()
				- StringWidth(fMinLimitLabel)) / 2.0, baseLine));
			baseLine -= lineHeight;
		}

		if (fUpdateText != NULL) {
			view->DrawString(fUpdateText, BPoint((bounds.Width()
				- StringWidth(fUpdateText)) / 2.0, baseLine));
		}
	}
}


// #pragma mark -


const char*
BSlider::UpdateText() const
{
	return NULL;
}


void
BSlider::UpdateTextChanged()
{
	// update text label
	float oldWidth = 0.0;
	if (fUpdateText != NULL)
		oldWidth = StringWidth(fUpdateText);

	const char* oldUpdateText = fUpdateText;
	fUpdateText = UpdateText();
	bool updateTextOnOff = fUpdateText == NULL && oldUpdateText != NULL
		|| fUpdateText != NULL && oldUpdateText == NULL;

	float newWidth = 0.0;
	if (fUpdateText != NULL)
		newWidth = StringWidth(fUpdateText);

	float width = ceilf(max_c(newWidth, oldWidth)) + 2.0f;
	if (width != 0) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		float height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
		float lineHeight = height + ceilf(fontHeight.leading);
		BRect invalid(Bounds());
		if (fOrientation == B_HORIZONTAL)
			invalid = BRect(invalid.right - width, 0, invalid.right, height);
		else {
			if (!updateTextOnOff) {
				invalid.left = (invalid.left + invalid.right - width) / 2;
				invalid.right = invalid.left + width;
				if (fMinLimitLabel)
					invalid.bottom -= lineHeight;
				invalid.top = invalid.bottom - height;
			}
		}
		Invalidate(invalid);
	}

	float oldMaxUpdateTextWidth = fMaxUpdateTextWidth;
	fMaxUpdateTextWidth = MaxUpdateTextWidth();
	if (oldMaxUpdateTextWidth != fMaxUpdateTextWidth)
		InvalidateLayout();
}


BRect
BSlider::BarFrame() const
{
	BRect frame(Bounds());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float textHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
	float leading = ceilf(fontHeight.leading);

	float thumbInset;
	if (fStyle == B_BLOCK_THUMB)
		thumbInset = 8.0;
	else
		thumbInset = 7.0;

	if (Orientation() == B_HORIZONTAL) {
		frame.left = thumbInset;
		frame.top = 6.0 + (Label() ? textHeight + 4.0 : 0.0);
		frame.right -= thumbInset;
		frame.bottom = frame.top + fBarThickness;
	} else {
		frame.left = floorf((frame.Width() - fBarThickness) / 2.0);
		frame.top = thumbInset;
		if (Label())
			frame.top += textHeight;
		if (fMaxLimitLabel) {
			frame.top += textHeight;
			if (Label())
				frame.top += leading;
		}

		frame.right = frame.left + fBarThickness;
		frame.bottom = frame.bottom - thumbInset;
		if (fMinLimitLabel)
			frame.bottom -= textHeight;
		if (fUpdateText) {
			frame.bottom -= textHeight;
			if (fMinLimitLabel)
				frame.bottom -= leading;
		}
	}

	return frame;
}


BRect
BSlider::HashMarksFrame() const
{
	BRect frame(BarFrame());

	if (fOrientation == B_HORIZONTAL) {
		frame.top -= 6.0;
		frame.bottom += 6.0;
	} else {
		frame.left -= 6.0;
		frame.right += 6.0;
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

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float textHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);

	if (fStyle == B_BLOCK_THUMB) {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = floorf(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 8;
			frame.top = 2 + (Label() ? textHeight + 4 : 0);
			frame.right = frame.left + 17;
			frame.bottom = frame.top + fBarThickness + 7;
		} else {
			frame.left = floor((frame.Width() - fBarThickness) / 2) - 4;
			frame.top = floorf(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 8;
			frame.right = frame.left + fBarThickness + 7;
			frame.bottom = frame.top + 17;
		}
	} else {
		if (Orientation() == B_HORIZONTAL) {
			frame.left = floorf(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 6;
			frame.right = frame.left + 12;
			frame.top = 3 + fBarThickness + (Label() ? textHeight + 4 : 0);
			frame.bottom = frame.top + 8;
		} else {
			frame.left = floorf((frame.Width() + fBarThickness) / 2) - 3;
			frame.top = floorf(Position() * (_MaxPosition()
				- _MinPosition())) + _MinPosition() - 6;
			frame.right = frame.left + 8;
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
	BSize preferredSize = PreferredSize();

	if (_width) {
//		*_width = preferredSize.width;
		// NOTE: For compatibility reasons, the BSlider never shrinks
		// horizontally. This only affects applications which do not
		// use the new layout system.
		*_width = max_c(Bounds().Width(), preferredSize.width);
	}

	if (_height)
		*_height = preferredSize.height;
}


void
BSlider::ResizeToPreferred()
{
	BControl::ResizeToPreferred();
}


status_t
BSlider::Invoke(BMessage* message)
{
	return BControl::Invoke(message);
}


BHandler*
BSlider::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 command, const char *property)
{
	return BControl::ResolveSpecifier(message, index, specifier, command,
		property);
}


status_t
BSlider::GetSupportedSuites(BMessage* message)
{
	return BControl::GetSupportedSuites(message);
}


void
BSlider::SetModificationMessage(BMessage* message)
{
	delete fModificationMessage;
	fModificationMessage = message;
}


BMessage*
BSlider::ModificationMessage() const
{
	return fModificationMessage;
}


void
BSlider::SetSnoozeAmount(int32 snoozeTime)
{
	if (snoozeTime < 10000)
		snoozeTime = 10000;
	else if (snoozeTime > 1000000)
		snoozeTime = 1000000;

	fSnoozeAmount = snoozeTime;
}


int32
BSlider::SnoozeAmount() const
{
	return fSnoozeAmount;
}


void
BSlider::SetKeyIncrementValue(int32 incrementValue)
{
	fKeyIncrementValue = incrementValue;
}


int32
BSlider::KeyIncrementValue() const
{
	return fKeyIncrementValue;
}


void
BSlider::SetHashMarkCount(int32 hashMarkCount)
{
	fHashMarkCount = hashMarkCount;
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
// TODO: enable if the hashmark look is influencing the control size!
//	InvalidateLayout();
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
	InvalidateLayout();
	Invalidate();
}


thumb_style
BSlider::Style() const
{
	return fStyle;
}


void
BSlider::SetBarColor(rgb_color barColor)
{
	fBarColor = barColor;
	Invalidate(BarFrame());
}


rgb_color
BSlider::BarColor() const
{
	return fBarColor;
}


void
BSlider::UseFillColor(bool useFill, const rgb_color* barColor)
{
	fUseFillColor = useFill;

	if (useFill && barColor)
		fFillColor = *barColor;

	Invalidate(BarFrame());
}


bool
BSlider::FillColor(rgb_color* barColor) const
{
	if (barColor && fUseFillColor)
		*barColor = fFillColor;

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
	InvalidateLayout();
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
	if (thickness < 1.0)
		thickness = 1.0;
	if (thickness != fBarThickness) {
		// calculate invalid barframe and extend by hashmark size
		float hInset = 0.0;
		float vInset = 0.0;
		if (fOrientation == B_HORIZONTAL)
			vInset = -6.0;
		else
			hInset = -6.0;
		BRect invalid = BarFrame().InsetByCopy(hInset, vInset) | ThumbFrame();

		fBarThickness = thickness;

		invalid = invalid | BarFrame().InsetByCopy(hInset, vInset)
			| ThumbFrame();
		Invalidate(invalid);
		InvalidateLayout();
	}
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

	InvalidateLayout();
}


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


float
BSlider::MaxUpdateTextWidth()
{
	// very simplistic implementation that assumes the string will be widest
	// at the maximum value
	int32 value = Value();
	SetValueNoUpdate(fMaxValue);
	float width = StringWidth(UpdateText());
	SetValueNoUpdate(value);
	// in case the derived class uses a fixed buffer, the contents
	// should be reset for the old value
	UpdateText();
	return width;
}


// #pragma mark - layout related


void
BSlider::InvalidateLayout(bool descendants)
{
	// invalidate cached preferred size
	fMinSize.Set(-1, -1);

	BControl::InvalidateLayout(descendants);
}


BSize
BSlider::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidateMinSize());
}


BSize
BSlider::MaxSize()
{
	BSize maxSize = _ValidateMinSize();
	if (fOrientation == B_HORIZONTAL)	
		maxSize.width = B_SIZE_UNLIMITED;
	else
		maxSize.height = B_SIZE_UNLIMITED;
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), maxSize);
}


BSize
BSlider::PreferredSize()
{
	BSize preferredSize = _ValidateMinSize();
	if (fOrientation == B_HORIZONTAL)	
		preferredSize.width = max_c(100.0, preferredSize.width);
	else
		preferredSize.height = max_c(100.0, preferredSize.height);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), preferredSize);
}


// #pragma mark - private

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
	// ToDo: this also draws over the hash marks (though it's not *that* noticeable)
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

	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenmax;
	rgb_color lighten1;
	rgb_color darken2;
	rgb_color darken3;
	rgb_color darkenmax;

	if (IsEnabled()) {
		lightenmax	= tint_color(no_tint, B_LIGHTEN_MAX_TINT);
		lighten1	= no_tint;
		darken2		= tint_color(no_tint, B_DARKEN_2_TINT);
		darken3		= tint_color(no_tint, B_DARKEN_3_TINT);
		darkenmax	= tint_color(no_tint, B_DARKEN_MAX_TINT);
	} else {
		lightenmax	= tint_color(no_tint, B_LIGHTEN_2_TINT);
		lighten1	= tint_color(no_tint, B_LIGHTEN_1_TINT);
		darken2		= tint_color(no_tint, (B_NO_TINT + B_DARKEN_1_TINT) / 2);
		darken3		= tint_color(no_tint, B_DARKEN_1_TINT);
		darkenmax	= tint_color(no_tint, B_DARKEN_3_TINT);
	}

	if (Orientation() == B_HORIZONTAL) {
		view->SetHighColor(lighten1);
		view->FillTriangle(
			BPoint(frame.left + 1, frame.bottom - 3),
			BPoint((frame.left + frame.right) / 2, frame.top + 1),
			BPoint(frame.right - 1, frame.bottom - 3));

		view->SetHighColor(no_tint);
		view->StrokeLine(BPoint(frame.right - 2, frame.bottom - 3),
			BPoint(frame.left + 3, frame.bottom - 3));

		view->SetHighColor(darkenmax);
		view->StrokeLine(BPoint(frame.left, frame.bottom - 1),
			BPoint(frame.right, frame.bottom - 1));
		view->StrokeLine(BPoint(frame.right, frame.bottom - 2),
			BPoint((frame.left + frame.right) / 2, frame.top));

		view->SetHighColor(darken2);
		view->StrokeLine(BPoint(frame.right - 1, frame.bottom - 2),
			BPoint(frame.left + 1, frame.bottom - 2));
		view->SetHighColor(darken3);
		view->StrokeLine(BPoint(frame.left, frame.bottom - 2),
			BPoint((frame.left + frame.right) / 2 - 1, frame.top + 1));

		view->SetHighColor(lightenmax);
		view->StrokeLine(BPoint(frame.left + 2, frame.bottom - 3),
			BPoint((frame.left + frame.right) / 2, frame.top + 1));

		// Shadow
		view->SetHighColor(0, 0, 0, IsEnabled() ? 80 : 40);
		view->SetDrawingMode(B_OP_ALPHA);
		view->StrokeLine(BPoint(frame.left + 1, frame.bottom),
			BPoint(frame.right, frame.bottom));
	} else {
		view->SetHighColor(lighten1);
		view->FillTriangle(
			BPoint(frame.left, (frame.top + frame.bottom) / 2),
			BPoint(frame.right - 1, frame.top + 1),
			BPoint(frame.right - 1, frame.bottom - 1));

		view->SetHighColor(darkenmax);
		view->StrokeLine(BPoint(frame.right - 1, frame.top),
			BPoint(frame.right - 1, frame.bottom));
		view->StrokeLine(BPoint(frame.right - 1, frame.bottom),
			BPoint(frame.right - 2, frame.bottom));

		view->SetHighColor(darken2);
		view->StrokeLine(BPoint(frame.right - 2, frame.top + 2),
			BPoint(frame.right - 2, frame.bottom - 1));
		view->StrokeLine(
			BPoint(frame.left, (frame.top + frame.bottom) / 2),
			BPoint(frame.right - 2, frame.top));
		view->SetHighColor(darken3);
		view->StrokeLine(
			BPoint(frame.left + 1, (frame.top + frame.bottom) / 2 + 1),
			BPoint(frame.right - 3, frame.bottom - 1));

		view->SetHighColor(lightenmax);
		view->StrokeLine(
			BPoint(frame.left + 1, (frame.top + frame.bottom) / 2),
			BPoint(frame.right - 2, frame.top + 1));

		// Shadow
		view->SetHighColor(0, 0, 0, IsEnabled() ? 80 : 40);
		view->SetDrawingMode(B_OP_ALPHA);
		view->StrokeLine(BPoint(frame.right, frame.top + 1),
			BPoint(frame.right, frame.bottom));
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

	return BarFrame().bottom - 1.0f;
}


float
BSlider::_MaxPosition() const
{
	if (fOrientation == B_HORIZONTAL)
		return BarFrame().right - 1.0f;

	return BarFrame().top + 1.0f;
}


BSize
BSlider::_ValidateMinSize()
{
	if (fMinSize.width >= 0) {
		// the preferred size is up to date
		return fMinSize;
	}

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float width = 0.0;
	float height = 0.0;

	if (fMaxUpdateTextWidth < 0.0)
		fMaxUpdateTextWidth = MaxUpdateTextWidth();

	if (Orientation() == B_HORIZONTAL) {
		height = 12.0 + fBarThickness;
		int32 rows = 0;

		float labelWidth = 0;
		int32 labelRows = 0;
		float labelSpacing = StringWidth("M") * 2;
		if (Label()) {
			labelWidth = StringWidth(Label());
			labelRows = 1;
		}
		if (fMaxUpdateTextWidth > 0.0) {
			if (labelWidth > 0)
				labelWidth += labelSpacing;
			labelWidth += fMaxUpdateTextWidth;
			labelRows = 1;
		}
		rows += labelRows;

		if (MinLimitLabel())
			width = StringWidth(MinLimitLabel());
		if (MaxLimitLabel()) {
			// some space between the labels
			if (MinLimitLabel())
				width += labelSpacing;

			width += StringWidth(MaxLimitLabel());
		}

		if (labelWidth > width)
			width = labelWidth;
		if (width < 32.0)
			width = 32.0;

		if (MinLimitLabel() || MaxLimitLabel())
			rows++;

		height += rows * (ceilf(fontHeight.ascent)
			+ ceilf(fontHeight.descent) + 4.0);
	} else { 
		// B_VERTICAL
		width = 12.0 + fBarThickness;
		height = 32.0;

		float lineHeightNoLeading = ceilf(fontHeight.ascent)
			+ ceilf(fontHeight.descent);
		float lineHeight = lineHeightNoLeading + ceilf(fontHeight.leading);

		// find largest label
		float labelWidth = 0;
		if (Label()) {
			labelWidth = StringWidth(Label());
			height += lineHeightNoLeading;
		}
		if (MaxLimitLabel()) {
			labelWidth = max_c(labelWidth, StringWidth(MaxLimitLabel()));
			height += Label() ? lineHeight : lineHeightNoLeading;
		}
		if (MinLimitLabel()) {
			labelWidth = max_c(labelWidth, StringWidth(MinLimitLabel()));
			height += lineHeightNoLeading;
		}
		if (fMaxUpdateTextWidth > 0.0) {
			labelWidth = max_c(labelWidth, fMaxUpdateTextWidth);
			height += MinLimitLabel() ? lineHeight : lineHeightNoLeading;
		}

		width = max_c(labelWidth, width);
	}

	fMinSize.width = width;
	fMinSize.height = height;

	return fMinSize;
}


// #pragma mark - FBC padding

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


//	#pragma mark - BeOS compatibility


#if __GNUC__ < 3

extern "C"
void
_ReservedSlider4__7BSlider(BSlider *slider, int32 minimum, int32 maximum)
{
	slider->BSlider::SetLimits(minimum, maximum);
}

extern "C"
float
_ReservedSlider5__7BSlider(BSlider *slider)
{
	return slider->BSlider::MaxUpdateTextWidth();
}

extern "C" void _ReservedSlider1__7BSlider() {}
extern "C" void _ReservedSlider2__7BSlider() {}
extern "C" void _ReservedSlider3__7BSlider() {}

#endif	// __GNUC__ < 3
