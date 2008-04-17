/*
 * Copyright 2001-2008, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Marc Flerackers (mflerackers@androme.be)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexandre Deckner, alex@zappotek.com
 */

/**	BColorControl displays a palette of selectable colors. */


#include <stdio.h>
#include <stdlib.h>

#include <ColorControl.h>
#include <Bitmap.h>
#include <TextControl.h>
#include <Region.h>
#include <Screen.h>
#include <Window.h>


static const uint32 kMsgColorEntered = 'ccol';
static const uint32 kMinCellSize = 6;
static const float kSelectorPenSize = 2.0f;
static const float kSelectorSize = 4.0f;
static const float kSelectorHSpacing = 2.0f;
static const float kTextFieldsHSpacing = 6.0f;

BColorControl::BColorControl(BPoint leftTop, color_control_layout layout,
	float cellSize, const char *name, BMessage *message,
	bool bufferedDrawing)
	: BControl(BRect(leftTop, leftTop), name, NULL, message,
			B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW | B_NAVIGABLE)
{
	_InitData(layout, cellSize, bufferedDrawing, NULL);
}


BColorControl::BColorControl(BMessage* archive)
	: BControl(archive)
{
	int32 layout;
	float cellSize;
	bool useOffscreen;

	archive->FindInt32("_layout", &layout);
	archive->FindFloat("_csize", &cellSize);
	archive->FindBool("_use_off", &useOffscreen);

	_InitData((color_control_layout)layout, cellSize, useOffscreen, archive);
}


BColorControl::~BColorControl()
{
	delete fBitmap;
}


void
BColorControl::_InitData(color_control_layout layout, float size,
	bool useOffscreen, BMessage* archive)
{
	fPaletteMode = BScreen(B_MAIN_SCREEN_ID).ColorSpace() == B_CMAP8;
		//TODO: we don't support workspace and colorspace changing for now 
		//		so we take the main_screen colorspace at startup
	fColumns = layout;
	fRows = 256 / fColumns;
	fCellSize = ceil(max_c(kMinCellSize, size));
	
	fSelectedPaletteColorIndex = -1;
	fPreviousSelectedPaletteColorIndex = -1;
	fFocusedComponent = 0;
		
	if (archive) {
		int32 value = 0;
		archive->FindInt32("_val", &value);

		SetValue(value);

		fRedText = (BTextControl*)FindView("_red");
		fGreenText = (BTextControl*)FindView("_green");
		fBlueText = (BTextControl*)FindView("_blue");
	} else {
		BRect rect(0.0f, 0.0f, 70.0f, 15.0f);
		float labelWidth = StringWidth("Green:") + 5;
		rect.right = labelWidth + StringWidth("999") + 20;

		// red

		fRedText = new BTextControl(rect, "_red", "Red:", "0",
			new BMessage(kMsgColorEntered), B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE);
		fRedText->SetDivider(labelWidth);

		float offset = fRedText->Bounds().Height() + 2;

		for (int32 i = 0; i < 256; i++)
			fRedText->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fRedText->TextView()->AllowChar(i);
		fRedText->TextView()->SetMaxBytes(3);

		// green

		rect.OffsetBy(0.0f, offset);
		fGreenText = new BTextControl(rect, "_green", "Green:", "0",
			new BMessage(kMsgColorEntered), B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE);
		fGreenText->SetDivider(labelWidth);

		for (int32 i = 0; i < 256; i++)
			fGreenText->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fGreenText->TextView()->AllowChar(i);
		fGreenText->TextView()->SetMaxBytes(3);

		// blue

		rect.OffsetBy(0.0f, offset);
		fBlueText = new BTextControl(rect, "_blue", "Blue:", "0",
			new BMessage(kMsgColorEntered), B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE);
		fBlueText->SetDivider(labelWidth);

		for (int32 i = 0; i < 256; i++)
			fBlueText->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fBlueText->TextView()->AllowChar(i);
		fBlueText->TextView()->SetMaxBytes(3);
		
		_LayoutView();
	
		AddChild(fRedText);
		AddChild(fGreenText);
		AddChild(fBlueText);
	}

	if (useOffscreen) {
		BRect bounds = fPaletteFrame;
		bounds.InsetBy(-2.0f, -2.0f); 
		
		fBitmap = new BBitmap(bounds, B_RGB32, true, false);			
		fOffscreenView = new BView(bounds, "off_view", 0, 0);

		fBitmap->Lock();
		fBitmap->AddChild(fOffscreenView);
		fBitmap->Unlock();
	} else {
		fBitmap = NULL;
		fOffscreenView = NULL;
	}
}


void
BColorControl::_LayoutView()
{
	if (fPaletteMode){
		fPaletteFrame.Set(2.0f, 2.0f,
			float(fColumns) * fCellSize + 2.0,
			float(fRows) * fCellSize + 2.0);
	} else {		
		fPaletteFrame.Set(2.0f, 2.0f,
			float(fColumns) * fCellSize + 2.0,
			float(fRows) * fCellSize + 2.0 - 1.0);
			//1 pixel adjust so that the inner space 
			//has exactly rows*cellsize pixels in height
	}
	
	BRect rect = fPaletteFrame.InsetByCopy(-2.0,-2.0);	//bevel
	
	if (rect.Height() < fBlueText->Frame().bottom) {
		// adjust the height to fit
		rect.bottom = fBlueText->Frame().bottom;
	}
	
	float offset = floor(rect.bottom / 4);
	float y = offset;
	if (offset < fRedText->Bounds().Height() + 2) {
		offset = fRedText->Bounds().Height() + 2;
		y = 0;
	}	

	fRedText->MoveTo(rect.right + kTextFieldsHSpacing, y);

	y += offset;
	fGreenText->MoveTo(rect.right + kTextFieldsHSpacing, y);

	y += offset;
	fBlueText->MoveTo(rect.right + kTextFieldsHSpacing, y);
	
	ResizeTo(rect.Width() + kTextFieldsHSpacing + fRedText->Bounds().Width(), rect.Height());

}


BArchivable *
BColorControl::Instantiate(BMessage *archive)
{
	if (validate_instantiation(archive, "BColorControl"))
		return new BColorControl(archive);

	return NULL;
}


status_t
BColorControl::Archive(BMessage *archive, bool deep) const
{
	status_t status = BControl::Archive(archive, deep);

	if (status == B_OK)
		status = archive->AddInt32("_layout", Layout());
	if (status == B_OK)
		status = archive->AddFloat("_csize", fCellSize);
	if (status == B_OK)
		status = archive->AddBool("_use_off", fOffscreenView != NULL);

	return status;
}


void
BColorControl::SetLayout(BLayout* layout)
{
	// We need to implement this method, since we have another SetLayout()
	// method and C++ has this special method hiding "feature".
	BControl::SetLayout(layout);
}


void
BColorControl::SetValue(int32 value)
{
	rgb_color c1 = ValueAsColor();
	rgb_color c2;
	c2.red = (value & 0xFF000000) >> 24;
	c2.green = (value & 0x00FF0000) >> 16;
	c2.blue = (value & 0x0000FF00) >> 8;
	c2.alpha = 255;
	char string[4];

	if (fPaletteMode) {
		//workaround when two indexes have the same color
		rgb_color c = BScreen(Window()).ColorForIndex(fSelectedPaletteColorIndex);
		c.alpha = 255;
		if (fSelectedPaletteColorIndex == -1 || c != c2) {
				//here SetValue hasn't been called by mouse tracking
			fSelectedPaletteColorIndex = BScreen(Window()).IndexForColor(c2);
		}				
		
		c2 = BScreen(Window()).ColorForIndex(fSelectedPaletteColorIndex);	
		
		Invalidate(_PaletteSelectorFrame(fPreviousSelectedPaletteColorIndex));
		Invalidate(_PaletteSelectorFrame(fSelectedPaletteColorIndex));		
		
		fPreviousSelectedPaletteColorIndex = fSelectedPaletteColorIndex;	
	} else {		
		float invalidateRadius = kSelectorSize/2 + kSelectorPenSize;
		BPoint p;
		
		if (c1.red != c2.red) {
			p = _SelectorPosition(_RampFrame(1), c1.red);
			Invalidate(BRect(p.x - invalidateRadius, p.y - invalidateRadius,
				 p.x + invalidateRadius, p.y + invalidateRadius));	
				 
			p = _SelectorPosition(_RampFrame(1), c2.red);
			Invalidate(BRect(p.x - invalidateRadius, p.y - invalidateRadius,
				 p.x + invalidateRadius, p.y + invalidateRadius));	
		}
		if (c1.green != c2.green) {
			p = _SelectorPosition(_RampFrame(2), c1.green);
			Invalidate(BRect(p.x - invalidateRadius, p.y - invalidateRadius,
				 p.x + invalidateRadius, p.y + invalidateRadius));
				 	
			p = _SelectorPosition(_RampFrame(2), c2.green);
			Invalidate(BRect(p.x - invalidateRadius, p.y - invalidateRadius,
				 p.x + invalidateRadius, p.y + invalidateRadius));		
		}
		if (c1.blue != c2.blue) {
			p = _SelectorPosition(_RampFrame(3), c1.blue);
			Invalidate(BRect(p.x - invalidateRadius, p.y - invalidateRadius,
				 p.x + invalidateRadius, p.y + invalidateRadius));
				 	
			p = _SelectorPosition(_RampFrame(3), c2.blue);
			Invalidate(BRect(p.x - invalidateRadius, p.y - invalidateRadius,
				 p.x + invalidateRadius, p.y + invalidateRadius));				
		} 
	}
	
	// the textcontrols have to be updated even when the color
	// hasn't changed since the value is clamped upstream
	// and the textcontrols would still show the unclamped value
	sprintf(string, "%d", c2.red);
	fRedText->SetText(string);
	sprintf(string, "%d", c2.green);
	fGreenText->SetText(string);
	sprintf(string, "%d", c2.blue);
	fBlueText->SetText(string);

	if (Value() == value)
		return;

	BControl::SetValueNoUpdate(value);

	if (LockLooper()) {
		Window()->UpdateIfNeeded();
		UnlockLooper();
	}
}


rgb_color
BColorControl::ValueAsColor()
{
	int32 value = Value();
	rgb_color color;

	color.red = (value & 0xFF000000) >> 24;
	color.green = (value & 0x00FF0000) >> 16;
	color.blue = (value & 0x0000FF00) >> 8;
	color.alpha = 255;

	return color;
}


void
BColorControl::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);

	fRedText->SetEnabled(enabled);
	fGreenText->SetEnabled(enabled);
	fBlueText->SetEnabled(enabled);
}


void
BColorControl::AttachedToWindow()
{
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BControl::AttachedToWindow();

	fRedText->SetTarget(this);
	fGreenText->SetTarget(this);
	fBlueText->SetTarget(this);
	
	if (fBitmap)
		_InitOffscreen();	
}


void
BColorControl::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgColorEntered:
		{
			rgb_color color;
			color.red = min_c(strtol(fRedText->Text(), NULL, 10), 255);
			color.green = min_c(strtol(fGreenText->Text(), NULL, 10), 255);
			color.blue = min_c(strtol(fBlueText->Text(), NULL, 10), 255);
			color.alpha = 255;			
					
			SetValue(color);
			Invoke();
			break;
		}
		default:
			BControl::MessageReceived(message);
	}
}


void
BColorControl::Draw(BRect updateRect)
{
	if (fBitmap) {
		if (!fBitmap->Lock())
			return;

		if (fOffscreenView->Bounds().Intersects(updateRect)) {
			BRegion region(updateRect);
			ConstrainClippingRegion(&region);
			DrawBitmap(fBitmap, B_ORIGIN);
			ConstrainClippingRegion(NULL);
		}

		fBitmap->Unlock();
		_DrawSelectors(this);
		
	} else
		_DrawColorArea(this, updateRect);
		_DrawSelectors(this);
}


void
BColorControl::_DrawColorArea(BView* target, BRect update)
{
	BRegion region(update);
	target->ConstrainClippingRegion(&region);
	
	rgb_color noTint = ui_color(B_PANEL_BACKGROUND_COLOR),
	lightenmax = tint_color(noTint, B_LIGHTEN_MAX_TINT),
	darken1 = tint_color(noTint, B_DARKEN_1_TINT),
	darken4 = tint_color(noTint, B_DARKEN_4_TINT);
		
	BRect bevelRect = fPaletteFrame.InsetByCopy(-2.0,-2.0);	//bevel
	
	// First bevel
	target->SetHighColor(darken1);
	target->StrokeLine(bevelRect.LeftBottom(), bevelRect.LeftTop());
	target->StrokeLine(bevelRect.LeftTop(), bevelRect.RightTop());
	target->SetHighColor(lightenmax);
	target->StrokeLine(BPoint(bevelRect.left + 1.0f, bevelRect.bottom), bevelRect.RightBottom());
	target->StrokeLine(bevelRect.RightBottom(), BPoint(bevelRect.right, bevelRect.top + 1.0f));

	bevelRect.InsetBy(1.0f, 1.0f);

	// Second bevel
	target->SetHighColor(darken4);
	target->StrokeLine(bevelRect.LeftBottom(), bevelRect.LeftTop());
	target->StrokeLine(bevelRect.LeftTop(), bevelRect.RightTop());
	target->SetHighColor(noTint);
	target->StrokeLine(BPoint(bevelRect.left + 1.0f, bevelRect.bottom), bevelRect.RightBottom());
	target->StrokeLine(bevelRect.RightBottom(), BPoint(bevelRect.right, bevelRect.top + 1.0f));
	
	if (fPaletteMode) {	      	      
      	int colBegin = max_c(0, -1 + int(update.left) / int(fCellSize));
      	int colEnd = min_c(fColumns, 2 + int(update.right) / int(fCellSize));
      	int rowBegin = max_c(0, -1 + int(update.top) / int(fCellSize));
      	int rowEnd = min_c(fRows, 2 + int(update.bottom) / int(fCellSize));
      	
      	//grid
		target->SetHighColor(darken1);		
		for (int xi = 0; xi < fColumns+1; xi++){
			float x = fPaletteFrame.left + float(xi) * fCellSize;
			target->StrokeLine(BPoint(x, fPaletteFrame.top), BPoint(x, fPaletteFrame.bottom));				
		}
		for (int yi = 0; yi < fRows+1; yi++){
			float y = fPaletteFrame.top + float(yi) * fCellSize;
			target->StrokeLine(BPoint(fPaletteFrame.left, y), BPoint(fPaletteFrame.right, y));				
		} 
		    
        //colors  	        
      	for (int col = colBegin; col < colEnd; col++){
      		for (int row = rowBegin; row < rowEnd; row++){  
      			uint8 colorIndex = row * fColumns + col;
      			float x = fPaletteFrame.left + col * fCellSize;
				float y = fPaletteFrame.top + row * fCellSize;
				
      			target->SetHighColor(system_colors()->color_list[colorIndex]);
				target->FillRect(BRect(x+1, y+1, x + fCellSize - 1, y + fCellSize - 1));
      		}      		
      	}       				 
	} else {
		rgb_color white = {255, 255, 255, 255};
		rgb_color red = {255, 0, 0, 255};
		rgb_color green = {0, 255, 0, 255};
		rgb_color blue = {0, 0, 255, 255};
			
		_ColorRamp(_RampFrame(0), target, white, 0, false, update);	
		_ColorRamp(_RampFrame(1), target, red, 0, false, update);	
		_ColorRamp(_RampFrame(2), target, green, 0, false, update);	
		_ColorRamp(_RampFrame(3), target, blue, 0, false, update);		
	}
	
	ConstrainClippingRegion(NULL);
}


void
BColorControl::_DrawSelectors(BView* target)
{
	rgb_color noTint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenmax = tint_color(noTint, B_LIGHTEN_MAX_TINT);
	
	if (fPaletteMode) {
		if (fSelectedPaletteColorIndex != -1) {			     	
	      	target->SetHighColor(lightenmax);
			target->StrokeRect(_PaletteSelectorFrame(fSelectedPaletteColorIndex));
		}			
	} else {
		rgb_color color = ValueAsColor();			
		target->SetPenSize(kSelectorPenSize);		
		target->SetHighColor(255, 255, 255);
		
		target->StrokeEllipse(_SelectorPosition(_RampFrame(1), color.red),
			 kSelectorSize / 2, kSelectorSize / 2);		
		target->StrokeEllipse(_SelectorPosition(_RampFrame(2), color.green),
			 kSelectorSize / 2, kSelectorSize / 2);			
		target->StrokeEllipse(_SelectorPosition(_RampFrame(3), color.blue),
			 kSelectorSize / 2, kSelectorSize / 2);	
			 
		target->SetPenSize(1.0f);		
	}
}


void
BColorControl::_ColorRamp(BRect rect, BView* target,
	rgb_color baseColor, int16 flag, bool focused, BRect update)
{
	float width = rect.Width() + 1;
	rgb_color color;
	color.alpha = 255;
	
	update = update & rect;

	if (update.IsValid() && update.Width() >= 0){		
		target->BeginLineArray((int32)update.Width() + 1);
	
		for (float i = (update.left - rect.left); i <= (update.right - rect.left) + 1; i++) {
			color.red = (uint8)(i * baseColor.red / width);
			color.green = (uint8)(i * baseColor.green / width);
			color.blue = (uint8)(i * baseColor.blue / width);
	
			target->AddLine(BPoint(rect.left + i, rect.top),
				BPoint(rect.left + i, rect.bottom - 1), color);			
		}
	
		target->EndLineArray();
	}
}


BPoint
BColorControl::_SelectorPosition(const BRect& rampRect, uint8 shade) const
{
	float radius = kSelectorSize / 2 + kSelectorPenSize / 2;
	
	return BPoint(rampRect.left + kSelectorHSpacing + radius + 
		shade * (rampRect.Width() - 2 * (kSelectorHSpacing + radius)) / 255,
		rampRect.top + rampRect.Height() / 2);
}


BRect
BColorControl::_RampFrame(uint8 rampIndex) const
{
	float rampHeight = float(fRows) * fCellSize / 4.0f;	
			
	return BRect( fPaletteFrame.left,
		fPaletteFrame.top + float(rampIndex) * rampHeight,
		fPaletteFrame.right,
		fPaletteFrame.top + float(rampIndex + 1) * rampHeight);
}


BRect
BColorControl::_PaletteSelectorFrame(uint8 colorIndex) const
{
	uint32 row = colorIndex / fColumns;
	uint32 column = colorIndex % fColumns;
	float x = fPaletteFrame.left + column * fCellSize;
	float y = fPaletteFrame.top + row * fCellSize;		
	return BRect(x, y, x + fCellSize, y + fCellSize);
}


void
BColorControl::_InitOffscreen()
{
	if (fBitmap->Lock()) {			
		_DrawColorArea(fOffscreenView, fPaletteFrame.InsetByCopy(-2.0f,-2.0f));
		fOffscreenView->Sync();
		fBitmap->Unlock();
	}
}


void
BColorControl::SetCellSize(float cellSide)
{
	fCellSize = ceil(max_c(kMinCellSize, cellSide));
	_LayoutView();
	ResizeToPreferred();
}


float
BColorControl::CellSize() const
{
	return fCellSize;
}


void
BColorControl::SetLayout(color_control_layout layout)
{
	switch (layout) {
		case B_CELLS_4x64:
			fColumns = 4;
			fRows = 64;
			break;
		case B_CELLS_8x32:
			fColumns = 8;
			fRows = 32;
			break;
		case B_CELLS_16x16:
			fColumns = 16;
			fRows = 16;
			break;
		case B_CELLS_32x8:
			fColumns = 32;
			fRows = 8;
			break;
		case B_CELLS_64x4:
			fColumns = 64;
			fRows = 4;
			break;
	}
	
	_LayoutView();

	ResizeToPreferred();
	Invalidate();
}


color_control_layout
BColorControl::Layout() const
{
	if (fColumns == 4 && fRows == 64)
		return B_CELLS_4x64;
	if (fColumns == 8 && fRows == 32)
		return B_CELLS_8x32;
	if (fColumns == 16 && fRows == 16)
		return B_CELLS_16x16;
	if (fColumns == 32 && fRows == 8)
		return B_CELLS_32x8;
	if (fColumns == 64 && fRows == 4)
		return B_CELLS_64x4;

	return B_CELLS_32x8;
}


void
BColorControl::WindowActivated(bool state)
{
	BControl::WindowActivated(state);
}


void
BColorControl::KeyDown(const char* bytes, int32 numBytes)
{
	// TODO: make this keyboard navigable!
	BControl::KeyDown(bytes, numBytes);
}


void
BColorControl::MouseUp(BPoint point)
{
	fFocusedComponent = 0;
	SetTracking(false);
}


void
BColorControl::MouseDown(BPoint point)
{
	if (!fPaletteFrame.Contains(point))
		return;
	
	if (fPaletteMode) {	      	
      	int column = (int) ( (point.x - fPaletteFrame.left) / fCellSize );
		int row = (int) ( (point.y - fPaletteFrame.top) / fCellSize );
      	int colorIndex = row * fColumns + column;
      	if (colorIndex >= 0 && colorIndex < 256) {      		
      		fSelectedPaletteColorIndex = colorIndex;
      		SetValue(system_colors()->color_list[colorIndex]);
    	}
	} else {
		rgb_color color = ValueAsColor();
	
		uint8 shade = (unsigned char)max_c(0,
			 min_c((point.x - _RampFrame(0).left) * 255 / _RampFrame(0).Width(), 255));
				
		if (_RampFrame(0).Contains(point)) {
			color.red = color.green = color.blue = shade;
			fFocusedComponent = 1;
		} else if (_RampFrame(1).Contains(point)) {
			color.red = shade;
			fFocusedComponent = 2;
		} else if (_RampFrame(2).Contains(point)) {
			color.green = shade;
			fFocusedComponent = 3;
		} else if (_RampFrame(3).Contains(point)){
			color.blue = shade;
			fFocusedComponent = 4;
		}
		
		SetValue(color);
		
	}
	
	Invoke();

	SetTracking(true);
	MakeFocus();
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY|B_LOCK_WINDOW_FOCUS);
}


void
BColorControl::MouseMoved(BPoint point, uint32 transit,
	const BMessage *message)
{
	if (!IsTracking())
		return;	
		
	if (fPaletteMode && fPaletteFrame.Contains(point)) {		      	
      	int column = (int) ( (point.x - fPaletteFrame.left) / fCellSize );
		int row = (int) ( (point.y - fPaletteFrame.top) / fCellSize );
      	int colorIndex = row * fColumns + column;
      	if (colorIndex >= 0 && colorIndex < 256) {
      		fSelectedPaletteColorIndex = colorIndex;
      		SetValue(system_colors()->color_list[colorIndex]);      		
    	} 
    	
	} else {		
		if (fFocusedComponent == 0)
			return;
	
		rgb_color color = ValueAsColor();
	
		uint8 shade = (unsigned char)max_c(0,
			min_c((point.x - _RampFrame(0).left) * 255 / _RampFrame(0).Width(), 255));
		
		switch (fFocusedComponent) {
			case 1:
				color.red = color.green = color.blue = shade;
				break;
			case 2:
				color.red = shade;
				break;
			case 3:
				color.green = shade;
				break;
			case 4:
				color.blue = shade;
				break;
			default:
				break;
		}
		
		SetValue(color);
	}
	
	Invoke();
}


void
BColorControl::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


void
BColorControl::GetPreferredSize(float *_width, float *_height)
{
	BRect rect = fPaletteFrame.InsetByCopy(-2.0,-2.0);	//bevel	
	
	if (rect.Height() < fBlueText->Frame().bottom) {		
		// adjust the height to fit
		rect.bottom = fBlueText->Frame().bottom;
	}
	
	if (_width)
		*_width = rect.Width() + kTextFieldsHSpacing + fRedText->Bounds().Width();

	if (_height)
		*_height = rect.Height();
}


void
BColorControl::ResizeToPreferred()
{
	BControl::ResizeToPreferred();

	_LayoutView();
}


status_t
BColorControl::Invoke(BMessage *msg)
{
	return BControl::Invoke(msg);
}


void
BColorControl::FrameMoved(BPoint new_position)
{
	BControl::FrameMoved(new_position);
}


void
BColorControl::FrameResized(float new_width, float new_height)
{
	BControl::FrameResized(new_width, new_height);
}


BHandler *
BColorControl::ResolveSpecifier(BMessage *msg, int32 index,
	BMessage *specifier, int32 form, const char *property)
{
	return BControl::ResolveSpecifier(msg, index, specifier, form, property);
}


status_t
BColorControl::GetSupportedSuites(BMessage *data)
{
	return BControl::GetSupportedSuites(data);
}


void
BColorControl::MakeFocus(bool state)
{
	BControl::MakeFocus(state);
}


void
BColorControl::AllAttached()
{
	BControl::AllAttached();
}


void
BColorControl::AllDetached()
{
	BControl::AllDetached();
}


status_t
BColorControl::Perform(perform_code d, void *arg)
{
	return BControl::Perform(d, arg);
}


void BColorControl::_ReservedColorControl1() {}
void BColorControl::_ReservedColorControl2() {}
void BColorControl::_ReservedColorControl3() {}
void BColorControl::_ReservedColorControl4() {}


BColorControl &
BColorControl::operator=(const BColorControl &)
{
	return *this;
}
