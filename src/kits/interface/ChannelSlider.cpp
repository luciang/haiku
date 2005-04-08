/* 
 * Copyright 2005, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

// TODO: Currently it works correctly only with vertical sliders.

#include <Bitmap.h>
#include <ChannelSlider.h>
#include <Debug.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <Window.h>

const static unsigned char
kVerticalKnobData[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0xcb, 0xcb, 0xcb, 0xcb, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x12,
	0xff, 0xff, 0xff, 0xff, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0xff
};


const static unsigned char
kHorizontalKnobData[] = {
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0xff, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0xcb, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x00, 0x12, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x12, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0xff, 0xff	
};


static property_info
sPropertyInfo[] = {
	{ "Orientation",
	{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
	{ B_DIRECT_SPECIFIER, 0 }, "" },
		
	{ 0 }
};
	

BChannelSlider::BChannelSlider(BRect area, const char *name, const char *label,
	BMessage *model, int32 channels, uint32 resizeMode, uint32 flags)
	: BChannelControl(area, name, label, model, channels, resizeMode, flags)
{
	InitData();
}


BChannelSlider::BChannelSlider(BRect area, const char *name, const char *label,
	BMessage *model, orientation o, int32 channels, uint32 resizeMode, uint32 flags)
	: BChannelControl(area, name, label, model, channels, resizeMode, flags)

{
	InitData();
	SetOrientation(o);
}


BChannelSlider::BChannelSlider(BMessage *archive)
	: BChannelControl(archive)
{
	InitData();

	orientation orient;
	if (archive->FindInt32("_orient", (int32 *)&orient) == B_OK)
		SetOrientation(orient);
}


BChannelSlider::~BChannelSlider()
{
	delete fLeftKnob;
	delete fMidKnob;
	delete fRightKnob;
	delete[] fInitialValues;
}


BArchivable *
BChannelSlider::Instantiate(BMessage *archive)
{
	if (validate_instantiation(archive, "BChannelSlider"))
		return new BChannelSlider(archive);
	else
		return NULL;
}


status_t
BChannelSlider::Archive(BMessage *into, bool deep) const
{
	status_t status = BChannelControl::Archive(into, deep);
	if (status == B_OK)
		status = into->AddInt32("_orient", (int32)Orientation());

	return status;
}


orientation
BChannelSlider::Orientation() const
{
	return Vertical() ? B_VERTICAL : B_HORIZONTAL;
}


void
BChannelSlider::SetOrientation(orientation _orientation)
{
	bool isVertical = _orientation == B_VERTICAL;
	if (isVertical != Vertical()) {
		fVertical = isVertical;
		Invalidate(Bounds());
	}	
}


int32
BChannelSlider::MaxChannelCount() const
{
	return 32;
}


bool
BChannelSlider::SupportsIndividualLimits() const
{
	return false;
}


void
BChannelSlider::AttachedToWindow()
{
	BView *parent = Parent();
	if (parent != NULL)
		SetViewColor(parent->ViewColor());
	
	inherited::AttachedToWindow();
}


void
BChannelSlider::AllAttached()
{
	BControl::AllAttached();
}


void
BChannelSlider::DetachedFromWindow()
{
	inherited::DetachedFromWindow();
}


void
BChannelSlider::AllDetached()
{
	BControl::AllDetached();
}


void
BChannelSlider::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case B_SET_PROPERTY:
		case B_GET_PROPERTY:
		{	
			BMessage reply(B_REPLY);
			int32 index = 0;
			BMessage specifier;
			int32 what = 0;
			const char *property = NULL;
			bool handled = false;
			status_t status = message->GetCurrentSpecifier(&index, &specifier, &what, &property);
			BPropertyInfo propInfo(sPropertyInfo);		
			if (status == B_OK 
				&& propInfo.FindMatch(message, index, &specifier, what, property) >= 0) {					
				handled = true;
				if (message->what == B_SET_PROPERTY) {
					orientation orient;
					if (specifier.FindInt32("data", (int32 *)&orient) == B_OK) {
						SetOrientation(orient);
						Invalidate(Bounds());
					}
				} else if (message->what == B_GET_PROPERTY)
					reply.AddInt32("result", (int32)Orientation());
				else
					status = B_BAD_SCRIPT_SYNTAX;					
			}	
					
			if (handled) {
				reply.AddInt32("error", status);
				message->SendReply(&reply);
			} else
				inherited::MessageReceived(message);
			break;
		}
		default:
			inherited::MessageReceived(message);
			break;
	}
}


void
BChannelSlider::Draw(BRect updateRect)
{
	UpdateFontDimens();
	DrawThumbs();
	
	BRect bounds(Bounds());
	float labelWidth = StringWidth(Label());
	
	DrawString(Label(), BPoint((bounds.Width() - labelWidth) / 2, fBaseLine));
	Sync();	
}


void
BChannelSlider::MouseDown(BPoint where)
{
	if (!IsEnabled())
		BControl::MouseDown(where);
	else {
		fCurrentChannel = -1;
		fMinpoint = 0;
		
		// Search the channel on which the mouse was over
		int32 numChannels = CountChannels();	
		for (int32 channel = 0; channel < numChannels; channel++) {
			BRect frame = ThumbFrameFor(channel);
			frame.OffsetBy(fClickDelta);
			
			float range = ThumbRangeFor(channel);
			
			if (Vertical()) {		
				fMinpoint = frame.top + frame.Height() / 2;
				frame.bottom += range;
			} else {
				fMinpoint = frame.Width();
				frame.right += range;
			}
			
			// Found. Now set the initial values
			if (frame.Contains(where)) {
				uint32 buttons = 0;
				BMessage *currentMessage = Window()->CurrentMessage();
				if (currentMessage != NULL)
					currentMessage->FindInt32("buttons", (int32 *)&buttons);
				
				fAllChannels = (buttons & B_SECONDARY_MOUSE_BUTTON) == 0;
				fCurrentChannel = channel;
				
				if (fInitialValues != NULL && fAllChannels) {
					delete[] fInitialValues;
					fInitialValues = NULL;
				}
				
				if (fInitialValues == NULL)
					fInitialValues = new int32[CountChannels()];
				
				if (fAllChannels) {
					for (int32 i = 0; i < CountChannels(); i++)
						fInitialValues[i] = ValueFor(i);
				} else
					fInitialValues[fCurrentChannel] = ValueFor(fCurrentChannel);								
				
				if (Window()->Flags() & B_ASYNCHRONOUS_CONTROLS) {
					if (!IsTracking()) {
						SetTracking(true);
						DrawThumbs();
						Flush();	
					}
					
					MouseMovedCommon(where, where);
					SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
				
				} else {
					debugger("BChannelSlider::MouseDown(): SYNCHRONOUS CONTROLS NOT YET SUPPORTED");
				}
						
				break;
			}
		}
	}
}


void
BChannelSlider::MouseUp(BPoint where)
{
	if (IsEnabled() && IsTracking()) {
		FinishChange();
		SetTracking(false);
		fAllChannels = false;
		fCurrentChannel = -1;
		fMinpoint = 0;
	} else
		BControl::MouseUp(where);
}


void
BChannelSlider::MouseMoved(BPoint where, uint32 code, const BMessage *message)
{
	if (IsEnabled() && IsTracking())
		MouseMovedCommon(where, B_ORIGIN);
	else
		BControl::MouseMoved(where, code, message);
}


void
BChannelSlider::WindowActivated(bool state)
{
	BControl::WindowActivated(state);
}


void
BChannelSlider::KeyDown(const char *bytes, int32 numBytes)
{
	BControl::KeyDown(bytes, numBytes);
}


void
BChannelSlider::KeyUp(const char *bytes, int32 numBytes)
{
	BView::KeyUp(bytes, numBytes);
}


void
BChannelSlider::FrameResized(float newWidth, float newHeight)
{
	inherited::FrameResized(newWidth, newHeight);
	Invalidate(Bounds());
}


void
BChannelSlider::SetFont(const BFont *font, uint32 mask)
{
	inherited::SetFont(font, mask);
}


void
BChannelSlider::MakeFocus(bool focusState)
{
	if (focusState && !IsFocus())
		fFocusChannel = -1;
	BControl::MakeFocus(focusState);
}


void
BChannelSlider::SetEnabled(bool on)
{
	BControl::SetEnabled(on);
}


void
BChannelSlider::GetPreferredSize(float *width, float *height)
{
	// TODO: Implement
}


BHandler *
BChannelSlider::ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
								int32 form, const char *property)
{
	BHandler *target = this;
	BPropertyInfo propertyInfo(sPropertyInfo);
	if (propertyInfo.FindMatch(msg, index, specifier, form, property) < B_OK)
		target = BChannelControl::ResolveSpecifier(msg, index, specifier, form, property); 
	
	return target;
}


status_t
BChannelSlider::GetSupportedSuites(BMessage *data)
{
	if (data == NULL)
		return B_BAD_VALUE;
	
	data->AddString("suites", "suite/vnd.Be-channel-slider");

	BPropertyInfo propInfo(sPropertyInfo);
	data->AddFlat("messages", &propInfo, 1);
	
	return inherited::GetSupportedSuites(data);
}


void
BChannelSlider::DrawChannel(BView *into, int32 channel, BRect area, bool pressed)
{
	float hCenter = area.Width() / 2;
	float vCenter = area.Height() / 2;
	
	BPoint leftTop;
	BPoint bottomRight;
	if (Vertical()) {
		leftTop.Set(area.left + hCenter, area.top + vCenter);
		bottomRight.Set(leftTop.x , leftTop.y + ThumbRangeFor(channel));
	} else {
		leftTop.Set(area.left, area.top + vCenter);
		bottomRight.Set(area.left + ThumbRangeFor(channel), leftTop.y);
	}	
	
	DrawGroove(into, channel, leftTop, bottomRight);
	
	BPoint thumbLocation = leftTop;
	if (Vertical())
		thumbLocation.y += ThumbDeltaFor(channel);
	
	DrawThumb(into, channel, thumbLocation, pressed);
}


void
BChannelSlider::DrawGroove(BView *into, int32 channel, BPoint topLeft, BPoint bottomRight)
{
	ASSERT(into != NULL);
	BRect rect(topLeft, bottomRight);
	
	DrawThumbFrame(fBackingView, rect.InsetByCopy(-2.5, -2.5));		
	
	rect.InsetBy(-0.5, -0.5);
	into->FillRect(rect, B_SOLID_HIGH); 
}


void
BChannelSlider::DrawThumb(BView *into, int32 channel, BPoint where, bool pressed)
{
	ASSERT(into != NULL);
	
	const BBitmap *thumb = ThumbFor(channel, pressed);
	if (thumb == NULL)
		return;

	BRect bitmapBounds = thumb->Bounds();
	where.x -= bitmapBounds.right / 2;
	where.y -= bitmapBounds.bottom / 2;
	
	into->DrawBitmapAsync(thumb, where);
		
	if (pressed) {
		into->PushState();
	
		into->SetDrawingMode(B_OP_ALPHA);
		
		rgb_color color = tint_color(into->ViewColor(), B_DARKEN_4_TINT);
		color.alpha = 128;
		into->SetHighColor(color);
		
		BRect rect(where, where);
		rect.right += bitmapBounds.right;
		rect.bottom += bitmapBounds.bottom;
		
		into->FillRect(rect);

		into->PopState();
	}
}


const BBitmap *
BChannelSlider::ThumbFor(int32 channel, bool pressed)
{
	// TODO: Finish me
	if (fLeftKnob == NULL) {
		if (Vertical()) {
			fLeftKnob = new BBitmap(BRect(0, 0, 11, 14), B_CMAP8);
			fLeftKnob->SetBits(kVerticalKnobData, sizeof(kVerticalKnobData), 0, B_CMAP8);
		} else {
			fLeftKnob = new BBitmap(BRect(0, 0, 14, 10), B_CMAP8);
			fLeftKnob->SetBits(kHorizontalKnobData, sizeof(kHorizontalKnobData), 0, B_CMAP8);	
		}
	}
	
	return fLeftKnob;
}


BRect
BChannelSlider::ThumbFrameFor(int32 channel)
{
	UpdateFontDimens();
	
	BRect frame(0, 0, 0, 0);
	const BBitmap *thumb = ThumbFor(channel, false);
	if (thumb != NULL) {
		frame = thumb->Bounds();
		if (Vertical())
			frame.OffsetBy(channel * frame.Width(), fLineFeed * 2);
		else
			frame.OffsetBy(fLineFeed, fLineFeed + channel * frame.Height());
	}
	
	return frame;	
}


float
BChannelSlider::ThumbDeltaFor(int32 channel)
{
	float delta = 0;
	if (channel >= 0 && channel < MaxChannelCount()) {
		float range = ThumbRangeFor(channel);
		int32 limitRange = MaxLimitList()[channel] - MinLimitList()[channel];
		delta = ValueList()[channel] * range / limitRange;  
			
		if (Vertical())
			delta = range - delta;
	}
	
	return delta;	
}


float
BChannelSlider::ThumbRangeFor(int32 channel)
{
	UpdateFontDimens();
	
	float range = 0;	
	BRect bounds = Bounds();
	BRect frame = ThumbFrameFor(channel);
	if (Vertical())
		range = bounds.Height() - frame.Height() - fLineFeed * 4;
	else
		range = bounds.Width() - frame.Width() - fLineFeed * 2;
	
	return range; 
}


void
BChannelSlider::InitData()
{
	UpdateFontDimens();
	
	fLeftKnob = NULL;
	fMidKnob = NULL;
	fRightKnob = NULL;
	fBacking = NULL;
	fBackingView = NULL;
	fVertical = Bounds().Width() / Bounds().Height() < 1;
	fClickDelta = B_ORIGIN;
	
	fCurrentChannel = -1;
	fAllChannels = false;
	fInitialValues = NULL;
	fMinpoint = 0;
	fFocusChannel = -1;
	
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
BChannelSlider::FinishChange()
{
	if (fInitialValues != NULL) {
		if (fAllChannels) {
			// TODO: Iterate through the list of channels, and invoke only
			// for changed values
			
			InvokeChannel();
			
		} else {
			if (ValueList()[fCurrentChannel] != fInitialValues[fCurrentChannel]) {
				SetValueFor(fCurrentChannel, ValueList()[fCurrentChannel]);
				Invoke();
			}	
		}
	}
	
	SetTracking(false);
	Redraw();
}


void
BChannelSlider::UpdateFontDimens()
{
	font_height height;
	GetFontHeight(&height);
	fBaseLine = height.ascent + height.leading;
	fLineFeed = fBaseLine + height.descent;
}


void
BChannelSlider::DrawThumbs()
{
	BRect first = ThumbFrameFor(0);
	BRect last = ThumbFrameFor(CountChannels() - 1);
		
	if (fBacking == NULL) {
		BRect bitmapFrame;
		if (Vertical()) {
			bitmapFrame.top = first.top - ThumbRangeFor(0);
			bitmapFrame.bottom = last.bottom;
			bitmapFrame.left = first.left;
			bitmapFrame.right = last.right;
		} else {
			bitmapFrame.top = first.top;
			bitmapFrame.bottom = last.bottom;
			bitmapFrame.left = first.left;
			bitmapFrame.right = last.right + ThumbRangeFor(0);
		}
		
		bitmapFrame.InsetBy(-3, -3);
		bitmapFrame.OffsetTo(B_ORIGIN);
	
		fBacking = new BBitmap(bitmapFrame, BScreen(Window()).ColorSpace(), true, false);
		if (fBacking->Lock()) {
			fBackingView = new BView(bitmapFrame, "backing view", B_FOLLOW_NONE, B_WILL_DRAW);
			fBacking->AddChild(fBackingView);
			fBackingView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
			fBackingView->SetLowColor(fBackingView->ViewColor());
			fBacking->Unlock();
		}			
	}
	
	BPoint drawHere;
	drawHere.x = (Bounds().Width() - fBacking->Bounds().Width()) / 2;
	drawHere.y = (Bounds().Height() - fBacking->Bounds().Height()) / 2;
		
	if (fBacking->Lock()) {
		// Clear the view's background
		fBackingView->FillRect(fBackingView->Bounds(), B_SOLID_LOW);
		for (int32 channel = 0; channel < CountChannels(); channel++) {
			BRect channelArea = ThumbFrameFor(channel);
			// TODO: HACK!!! What am I missing ?
			// Why do I need this to draw the channel in the correct location ?
			channelArea.OffsetBy(3, -21);	
			DrawChannel(fBackingView, channel, channelArea, fMinpoint != 0); 
		}
		fBackingView->Sync();
		fBacking->Unlock();
	}
		
	fClickDelta = drawHere;
	
	// TODO: Look at the above comment
	fClickDelta.y -= 21;
	fClickDelta.x += 3;
	
	DrawBitmapAsync(fBacking, drawHere);
}


void
BChannelSlider::DrawThumbFrame(BView *into, const BRect &area)
{
	rgb_color oldColor = into->HighColor();
	
	into->SetHighColor(255, 255, 255);
	into->StrokeRect(area);
	into->SetHighColor(tint_color(into->ViewColor(), B_DARKEN_1_TINT));
	into->StrokeLine(area.LeftTop(), BPoint(area.right, area.top));
	into->StrokeLine(area.LeftTop(), BPoint(area.left, area.bottom - 1));
	into->SetHighColor(tint_color(into->ViewColor(), B_DARKEN_2_TINT));
	into->StrokeLine(BPoint(area.left + 1, area.top + 1), BPoint(area.right - 1, area.top + 1));
	into->StrokeLine(BPoint(area.left + 1, area.top + 1), BPoint(area.left + 1, area.bottom - 2));
	
	into->SetHighColor(oldColor);
}


bool
BChannelSlider::Vertical() const
{
	return fVertical;
}


void
BChannelSlider::Redraw()
{
	Invalidate(Bounds());
	Flush();
}


void
BChannelSlider::MouseMovedCommon(BPoint point, BPoint point2)
{
	float floatValue = 0;
	int32 limitRange = MaxLimitList()[fCurrentChannel] - MinLimitList()[fCurrentChannel];
	float range = ThumbRangeFor(fCurrentChannel);
	if (Vertical())
		floatValue = range - (point.y - fMinpoint);		
	else
		floatValue = range + (point.x - fMinpoint);
	
	int32 value = (int32)(floatValue / range * limitRange);
	if (fAllChannels)
		SetAllValue(value);
	else
		SetValueFor(fCurrentChannel, value);
		
	InvokeNotifyChannel(ModificationMessage());
	DrawThumbs();
}


void BChannelSlider::_Reserved_BChannelSlider_0(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_1(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_2(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_3(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_4(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_5(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_6(void *, ...) {}
void BChannelSlider::_Reserved_BChannelSlider_7(void *, ...) {}
