/*
 * Copyright 2004-2006, the Haiku project. All rights reserved.
 * Distributed under the terms of the Haiku License.
 *
 * Authors in chronological order:
 *  mccall@digitalparadise.co.uk
 *  Jérôme Duval
 *  Marcus Overhagen
*/
#include <InterfaceDefs.h>
#include <TranslationUtils.h>
#include <Bitmap.h>
#include <Button.h>
#include <Slider.h>
#include <TextControl.h>
#include <Window.h>
#include <Font.h>

#include "KeyboardView.h"
#include "KeyboardMessages.h"

// user interface
const uint32 kBorderSpace = 10;
const uint32 kItemSpace = 7;

KeyboardView::KeyboardView(BRect rect)
 :	BView(rect, "keyboard_view", B_FOLLOW_LEFT | B_FOLLOW_TOP,
 		  B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP)
{
	BRect frame;

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	fIconBitmap = BTranslationUtils::GetBitmap("key_bmap");
	fClockBitmap = BTranslationUtils::GetBitmap("clock_bmap");

	float labelwidth = StringWidth("Delay until key repeat")+20;
	
	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	
	float labelheight = fontHeight.ascent + fontHeight.descent +
						fontHeight.leading;
	
	// Create the "Key repeat rate" slider...
	frame.Set(kBorderSpace,kBorderSpace,kBorderSpace + labelwidth,kBorderSpace + (labelheight*2) + (kBorderSpace*2));
	fRepeatSlider = new BSlider(frame,"key_repeat_rate",
										"Key repeat rate",
										new BMessage(SLIDER_REPEAT_RATE),
										20,300,B_BLOCK_THUMB,
										B_FOLLOW_LEFT,B_WILL_DRAW);
	fRepeatSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fRepeatSlider->SetHashMarkCount(5);
	fRepeatSlider->SetLimitLabels("Slow","Fast");
	
	
	// Create the "Delay until key repeat" slider...
	frame.OffsetBy(0,frame.Height() + kBorderSpace);
	fDelaySlider = new BSlider(frame,"delay_until_key_repeat",
						"Delay until key repeat",
						new BMessage(SLIDER_DELAY_RATE),250000,1000000,
						B_BLOCK_THUMB,B_FOLLOW_LEFT,B_WILL_DRAW);
	fDelaySlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fDelaySlider->SetHashMarkCount(4);
	fDelaySlider->SetLimitLabels("Short","Long");
	
	// Create the "Typing test area" text box...
	frame.OffsetBy(0,frame.Height() + 15);
	frame.right = fDelaySlider->Frame().right + kBorderSpace
		+ (fIconBitmap != NULL ? fIconBitmap->Bounds().Width() : 0);
	BTextControl *textcontrol = new BTextControl(frame,"typing_test_area",NULL,
									"Typing test area",
									new BMessage('TTEA'),
									B_FOLLOW_LEFT,B_WILL_DRAW);
	textcontrol->SetAlignment(B_ALIGN_LEFT,B_ALIGN_CENTER);
	
	float width, height;
	textcontrol->GetPreferredSize(&width, &height);
	textcontrol->ResizeTo(frame.Width(),height);
	
	// Create the box for the sliders...
	frame.left = frame.top = kBorderSpace;
	frame.right = frame.left + fDelaySlider->Frame().right + (kBorderSpace * 2)
		+ (fClockBitmap != NULL ? fClockBitmap->Bounds().Width() : 0);
	frame.bottom = textcontrol->Frame().bottom + (kBorderSpace * 2);
	fBox = new BBox(frame,"keyboard_box",B_FOLLOW_LEFT, B_WILL_DRAW,
		B_FANCY_BORDER);
	AddChild(fBox);
	
	fBox->AddChild(fRepeatSlider);
	fBox->AddChild(fDelaySlider);
	fBox->AddChild(textcontrol);	

	//Add the "Default" button..	
	frame.left = kBorderSpace;
	frame.top = fBox->Frame().bottom + kBorderSpace;
	frame.right = frame.left + 1;
	frame.bottom = frame.top + 1;
	BButton *button = new BButton(frame,"keyboard_defaults","Defaults",
						new BMessage(BUTTON_DEFAULTS));
	button->ResizeToPreferred();
	AddChild(button);
	
	// Add the "Revert" button...
	frame = button->Frame();
	frame.OffsetBy(frame.Width() + kItemSpace, 0);
	button = new BButton(frame,"keyboard_revert","Revert",
						new BMessage(BUTTON_REVERT));
	button->ResizeToPreferred();
	button->SetEnabled(false);
	AddChild(button);
	
	ResizeTo(fBox->Frame().right + kBorderSpace, button->Frame().bottom + kBorderSpace);
}

void
KeyboardView::Draw(BRect updateFrame)
{
	BPoint pt;
	pt.x = fRepeatSlider->Frame().right + 10;

	if (fIconBitmap != NULL) {
		pt.y = fRepeatSlider->Frame().bottom - 35
			- fIconBitmap->Bounds().Height() / 3;
		fBox->DrawBitmap(fIconBitmap,pt);
	}

	if (fClockBitmap != NULL) {
		pt.y = fDelaySlider->Frame().bottom - 35
			- fClockBitmap->Bounds().Height() / 3;
		fBox->DrawBitmap(fClockBitmap,pt);
	}
}

void
KeyboardView::AttachedToWindow(void)
{
	Window()->ResizeTo(Bounds().Width(), Bounds().Height());
}

