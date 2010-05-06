/*
 * Copyright 2008, Stephan Aßmus, <superstippi@gmx.de>
 * Copyright 2008, Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "AntialiasingSettingsView.h"

#include <stdio.h>
#include <stdlib.h>

#include <freetype/config/ftoption.h>
	// for detected the availablility of subpixel anti-aliasing

#include <Box.h>
#include <Catalog.h>
#include <GridLayoutBuilder.h>
#include <GroupLayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <TextView.h>

#include "APRWindow.h"

#undef TR_CONTEXT
#define TR_CONTEXT "AntialiasingSettingsView"


//#define DISABLE_HINTING_CONTROL
	// if defined, the hinting menu is disabled (hinting not properly
	// implemented)

static const int32 kMsgSetAntialiasing = 'anti';
static const int32 kMsgSetHinting = 'hint';
static const int32 kMsgSetAverageWeight = 'avrg';
static const char* kSubpixelLabel = B_TRANSLATE_MARK("LCD subpixel");
static const char* kGrayscaleLabel = B_TRANSLATE_MARK("Grayscale");
static const char* kNoHintingLabel = B_TRANSLATE_MARK("Off");
static const char* kMonospacedHintingLabel =
	B_TRANSLATE_MARK("Monospaced fonts only");
static const char* kFullHintingLabel = B_TRANSLATE_MARK("On");


// #pragma mark - private libbe API


enum {
	HINTING_MODE_OFF = 0,
	HINTING_MODE_ON,
	HINTING_MODE_MONOSPACED_ONLY
};

extern void set_subpixel_antialiasing(bool subpix);
extern status_t get_subpixel_antialiasing(bool* subpix);
extern void set_hinting_mode(uint8 hinting);
extern status_t get_hinting_mode(uint8* hinting);
extern void set_average_weight(unsigned char averageWeight);
extern status_t get_average_weight(unsigned char* averageWeight);


//	#pragma mark -


AntialiasingSettingsView::AntialiasingSettingsView(const char* name)
	: BView(name, 0)
{
	// collect the current system settings
	if (get_subpixel_antialiasing(&fCurrentSubpixelAntialiasing) != B_OK)
		fCurrentSubpixelAntialiasing = false;
	fSavedSubpixelAntialiasing = fCurrentSubpixelAntialiasing;

	if (get_hinting_mode(&fCurrentHinting) != B_OK)
		fCurrentHinting = HINTING_MODE_ON;
	fSavedHinting = fCurrentHinting;

	if (get_average_weight(&fCurrentAverageWeight) != B_OK)
		fCurrentAverageWeight = 100;
	fSavedAverageWeight = fCurrentAverageWeight;

	// create the controls

	// antialiasing menu
	_BuildAntialiasingMenu();
	fAntialiasingMenuField = new BMenuField("antialiasing",
		B_TRANSLATE("Antialiasing type:"), fAntialiasingMenu, NULL);

	// "average weight" in subpixel filtering
	fAverageWeightControl = new BSlider("averageWeightControl",
		B_TRANSLATE("Reduce colored edges filter strength:"),
		new BMessage(kMsgSetAverageWeight), 0, 255, B_HORIZONTAL);
	fAverageWeightControl->SetLimitLabels(B_TRANSLATE("Off"),
		B_TRANSLATE("Strong"));
	fAverageWeightControl->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAverageWeightControl->SetHashMarkCount(255 / 15);
	fAverageWeightControl->SetEnabled(false);

	// hinting menu
	_BuildHintingMenu();
	fHintingMenuField = new BMenuField("hinting", B_TRANSLATE("Glyph hinting:"),
		fHintingMenu, NULL);

#ifdef DISABLE_HINTING_CONTROL
	fHintingMenuField->SetEnabled(false);
#endif

#ifndef FT_CONFIG_OPTION_SUBPIXEL_RENDERING
	// subpixelAntialiasingDisabledLabel
	BFont infoFont(*be_plain_font);
	infoFont.SetFace(B_ITALIC_FACE);
	rgb_color infoColor = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_4_TINT);
	// TODO: Replace with layout friendly constructor once available.
	BRect textBounds = Bounds();
	BTextView* subpixelAntialiasingDisabledLabel = new BTextView(
		textBounds, "unavailable label", textBounds, &infoFont, &infoColor,
		B_FOLLOW_NONE, B_WILL_DRAW | B_SUPPORTS_LAYOUT);
	subpixelAntialiasingDisabledLabel->SetText(B_TRANSLATE(
		"Subpixel based anti-aliasing in combination with glyph hinting is not "
		"available in this build of Haiku to avoid possible patent issues. To "
		"enable this feature, you have to build Haiku yourself and enable "
		"certain options in the libfreetype configuration header."));
	subpixelAntialiasingDisabledLabel->SetViewColor(
		ui_color(B_PANEL_BACKGROUND_COLOR));
	subpixelAntialiasingDisabledLabel->MakeEditable(false);
	subpixelAntialiasingDisabledLabel->MakeSelectable(false);
#endif // !FT_CONFIG_OPTION_SUBPIXEL_RENDERING

	SetLayout(new BGroupLayout(B_VERTICAL));

	// controls pane
	AddChild(BGridLayoutBuilder(10, 10)
		.Add(fHintingMenuField->CreateLabelLayoutItem(), 0, 0)
		.Add(fHintingMenuField->CreateMenuBarLayoutItem(), 1, 0)

		.Add(fAntialiasingMenuField->CreateLabelLayoutItem(), 0, 1)
		.Add(fAntialiasingMenuField->CreateMenuBarLayoutItem(), 1, 1)

		.Add(fAverageWeightControl, 0, 2, 2)

#ifndef FT_CONFIG_OPTION_SUBPIXEL_RENDERING
		// hinting+subpixel unavailable info
		.Add(subpixelAntialiasingDisabledLabel, 0, 3, 2)
#else
		.Add(BSpaceLayoutItem::CreateGlue(), 0, 3, 2)
#endif

		.SetInsets(10, 10, 10, 10)
	);

	_SetCurrentAntialiasing();
	_SetCurrentHinting();
	_SetCurrentAverageWeight();
}


AntialiasingSettingsView::~AntialiasingSettingsView()
{
}


void
AntialiasingSettingsView::AttachedToWindow()
{
	if (Parent() != NULL)
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fAntialiasingMenu->SetTargetForItems(this);
	fHintingMenu->SetTargetForItems(this);
	fAverageWeightControl->SetTarget(this);
}


void
AntialiasingSettingsView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case kMsgSetAntialiasing:
		{
			bool subpixelAntialiasing;
			if (msg->FindBool("antialiasing", &subpixelAntialiasing) != B_OK
				|| subpixelAntialiasing == fCurrentSubpixelAntialiasing)
				break;
			fCurrentSubpixelAntialiasing = subpixelAntialiasing;
			fAverageWeightControl->SetEnabled(fCurrentSubpixelAntialiasing);

			set_subpixel_antialiasing(fCurrentSubpixelAntialiasing);

			Window()->PostMessage(kMsgUpdate);
			break;
		}
		case kMsgSetHinting:
		{
			int8 hinting;
			if (msg->FindInt8("hinting", &hinting) != B_OK
				|| hinting == fCurrentHinting)
				break;

			fCurrentHinting = hinting;
			set_hinting_mode(fCurrentHinting);

			Window()->PostMessage(kMsgUpdate);
			break;
		}
		case kMsgSetAverageWeight:
		{
			int32 averageWeight = fAverageWeightControl->Value();
			if (averageWeight == fCurrentAverageWeight)
				break;

			fCurrentAverageWeight = averageWeight;

			set_average_weight(fCurrentAverageWeight);

			Window()->PostMessage(kMsgUpdate);
			break;
		}
		default:
			BView::MessageReceived(msg);
	}
}


void
AntialiasingSettingsView::_BuildAntialiasingMenu()
{
	fAntialiasingMenu = new BPopUpMenu(B_TRANSLATE("Antialiasing menu"));

	BMessage* message = new BMessage(kMsgSetAntialiasing);
	message->AddBool("antialiasing", false);

	BMenuItem* item = new BMenuItem(B_TRANSLATE(kGrayscaleLabel), message);

	fAntialiasingMenu->AddItem(item);

	message = new BMessage(kMsgSetAntialiasing);
	message->AddBool("antialiasing", true);

	item = new BMenuItem(B_TRANSLATE(kSubpixelLabel), message);

	fAntialiasingMenu->AddItem(item);
}


void
AntialiasingSettingsView::_BuildHintingMenu()
{
	fHintingMenu = new BPopUpMenu(B_TRANSLATE("Hinting menu"));

	BMessage* message = new BMessage(kMsgSetHinting);
	message->AddInt8("hinting", HINTING_MODE_OFF);
	fHintingMenu->AddItem(new BMenuItem(B_TRANSLATE(kNoHintingLabel),
		message));

	message = new BMessage(kMsgSetHinting);
	message->AddInt8("hinting", HINTING_MODE_ON);
	fHintingMenu->AddItem(new BMenuItem(B_TRANSLATE(kFullHintingLabel),
		message));

	message = new BMessage(kMsgSetHinting);
	message->AddInt8("hinting", HINTING_MODE_MONOSPACED_ONLY);
	fHintingMenu->AddItem(new BMenuItem(B_TRANSLATE(kMonospacedHintingLabel),
		message));
}


void
AntialiasingSettingsView::_SetCurrentAntialiasing()
{
	BMenuItem *item = fAntialiasingMenu->FindItem(
		fCurrentSubpixelAntialiasing
		? B_TRANSLATE(kSubpixelLabel)
		: B_TRANSLATE(kGrayscaleLabel));
	if (item != NULL)
		item->SetMarked(true);
	if (fCurrentSubpixelAntialiasing)
		fAverageWeightControl->SetEnabled(true);
}


void
AntialiasingSettingsView::_SetCurrentHinting()
{
	const char* label = NULL;
	switch (fCurrentHinting) {
		case HINTING_MODE_OFF:
			label = kNoHintingLabel;
			break;
		case HINTING_MODE_ON:
			label = kFullHintingLabel;
			break;
		case HINTING_MODE_MONOSPACED_ONLY:
			label = kMonospacedHintingLabel;
			break;
	}

	BMenuItem *item = fHintingMenu->FindItem(B_TRANSLATE(label));
	if (item != NULL)
		item->SetMarked(true);
}


void
AntialiasingSettingsView::_SetCurrentAverageWeight()
{
	fAverageWeightControl->SetValue(fCurrentAverageWeight);
}


void
AntialiasingSettingsView::SetDefaults()
{
}


bool
AntialiasingSettingsView::IsDefaultable()
{
	return false;
}


bool
AntialiasingSettingsView::IsRevertable()
{
	return fCurrentSubpixelAntialiasing != fSavedSubpixelAntialiasing
		|| fCurrentHinting != fSavedHinting
		|| fCurrentAverageWeight != fSavedAverageWeight;
}


void
AntialiasingSettingsView::Revert()
{
	if (!IsRevertable())
		return;

	fCurrentSubpixelAntialiasing = fSavedSubpixelAntialiasing;
	fCurrentHinting = fSavedHinting;
	fCurrentAverageWeight = fSavedAverageWeight;

	set_subpixel_antialiasing(fCurrentSubpixelAntialiasing);
	set_hinting_mode(fCurrentHinting);
	set_average_weight(fCurrentAverageWeight);

	_SetCurrentAntialiasing();
	_SetCurrentHinting();
	_SetCurrentAverageWeight();
}
