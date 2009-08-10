/*
 * Copyright 2001-2009, Haiku, Inc.
 * Copyright 2003-2004 Kian Duffy, myob@users.sourceforge.net
 * Parts Copyright 1998-1999 Kazuho Okui and Takashi Murai.
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "AppearPrefView.h"

#include <stdio.h>
#include <stdlib.h>

#include <Button.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <TextControl.h>
#include <View.h>

#include "MenuUtil.h"
#include "PrefHandler.h"
#include "TermConst.h"


static bool
IsFontUsable(const BFont &font)
{
	// TODO: If BFont::IsFullAndHalfFixed() was implemented, we could
	// use that. But I don't think it's easily implementable using
	// Freetype.

	if (font.IsFixed())
		return true;

	bool widthOk = true;
	int lastWidth = 0;
	char buffer[2] = {0, 0};
	for (int c = 0x20 ; c <= 0x7e; c++){
		buffer[0] = c;

		int width = (int)ceilf(font.StringWidth(buffer));
		if (c > 0x20 && width != lastWidth)
			widthOk = false;
		lastWidth = width;
	}

	return widthOk;
}


AppearancePrefView::AppearancePrefView(BRect frame, const char *name,
		BMessenger messenger)
	: PrefView(frame, name),
	fAppearancePrefViewMessenger(messenger)
{
  	const char *color_tbl[] = {
		PREF_TEXT_FORE_COLOR,
		PREF_TEXT_BACK_COLOR,
		PREF_CURSOR_FORE_COLOR,
		PREF_CURSOR_BACK_COLOR,
		PREF_SELECT_FORE_COLOR,
		PREF_SELECT_BACK_COLOR,
#if 0
		"",
		PREF_IM_FORE_COLOR,
		PREF_IM_BACK_COLOR,
		PREF_IM_SELECT_COLOR,
		"",
		PREF_ANSI_BLACK_COLOR,
		PREF_ANSI_RED_COLOR,
		PREF_ANSI_GREEN_COLOR,
		PREF_ANSI_YELLOW_COLOR,
		PREF_ANSI_BLUE_COLOR,
		PREF_ANSI_MAGENTA_COLOR,
		PREF_ANSI_CYAN_COLOR,
		PREF_ANSI_WHITE_COLOR,
#endif
		NULL
  	};

	float fontDividerSize = StringWidth("Font:") + 8.0;
	float sizeDividerSize = StringWidth("Size:") + 8.0;
	float greenDividerSize = StringWidth("Green:") + 8.0;
	if (greenDividerSize > sizeDividerSize)
		sizeDividerSize = greenDividerSize;
	else
		greenDividerSize = sizeDividerSize;
	float colorDividerSize = StringWidth("Color:") + 8.0;

	if (fontDividerSize < colorDividerSize)
		fontDividerSize = colorDividerSize;
	else
		colorDividerSize = fontDividerSize;

	BRect r(5, 5, 261, 25);

	BMenu *menu = _MakeFontMenu(MSG_HALF_FONT_CHANGED,
		PrefHandler::Default()->getString(PREF_HALF_FONT_FAMILY));
	fFont = new BMenuField(r, "font", "Font:", menu);
	fFont->SetDivider(fontDividerSize);
	fFont->SetAlignment(B_ALIGN_RIGHT);
	AddChild(fFont);

	r.OffsetBy(r.Width() + 10, 0);
	menu = _MakeSizeMenu(MSG_HALF_SIZE_CHANGED,
		PrefHandler::Default()->getInt32(PREF_HALF_FONT_SIZE));
	fFontSize = new BMenuField(r, "size", "Size:", menu);
	fFontSize->SetDivider(sizeDividerSize);
	fFontSize->SetAlignment(B_ALIGN_RIGHT);
	AddChild(fFontSize);

	r.OffsetBy(-r.Width() - 10,r.Height() + 10);
	fColorField = new BMenuField(r, "color", "Color:",
		MakeMenu(MSG_COLOR_FIELD_CHANGED, color_tbl, color_tbl[0]));
	fColorField->SetDivider(colorDividerSize);
	fColorField->SetAlignment(B_ALIGN_RIGHT);
	AddChild(fColorField);

  	fColorControl = SetupColorControl(BPoint(r.left, r.bottom + 10),
  		B_CELLS_32x8, 8.0, MSG_COLOR_CHANGED);
  	fColorControl->SetValue(PrefHandler::Default()->getRGB(PREF_TEXT_FORE_COLOR));

	BTextControl* redInput = (BTextControl*)fColorControl->FindView("_red");
	BTextControl* greenInput = (BTextControl*)fColorControl->FindView("_green");
	BTextControl* blueInput = (BTextControl*)fColorControl->FindView("_blue");
	
	redInput->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	greenInput->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	blueInput->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	redInput->SetDivider(greenDividerSize);
	greenInput->SetDivider(greenDividerSize);
	blueInput->SetDivider(greenDividerSize);
}


void
AppearancePrefView::GetPreferredSize(float *_width, float *_height)
{
	if (_width)
		*_width = Bounds().Width();

	if (*_height)
		*_height = fColorControl->Frame().bottom;
}


void
AppearancePrefView::Revert()
{
	fColorField->Menu()->ItemAt(0)->SetMarked(true);
	fColorControl->SetValue(PrefHandler::Default()->getRGB(PREF_TEXT_FORE_COLOR));

	fFont->Menu()->FindItem(PrefHandler::Default()->getString(PREF_HALF_FONT_FAMILY))->SetMarked(true);
	fFontSize->Menu()->FindItem(PrefHandler::Default()->getString(PREF_HALF_FONT_FAMILY))->SetMarked(true);
}


void
AppearancePrefView::AttachedToWindow()
{
	fFontSize->Menu()->SetTargetForItems(this);
	fFont->Menu()->SetTargetForItems(this);

  	fColorControl->SetTarget(this);
  	fColorField->Menu()->SetTargetForItems(this);
}


void
AppearancePrefView::MessageReceived(BMessage *msg)
{
	bool modified = false;

	switch (msg->what) {
		case MSG_HALF_FONT_CHANGED:
			PrefHandler::Default()->setString(PREF_HALF_FONT_FAMILY,
				fFont->Menu()->FindMarked()->Label());
			modified = true;
			break;

		case MSG_HALF_SIZE_CHANGED:
			PrefHandler::Default()->setString(PREF_HALF_FONT_SIZE,
				fFontSize->Menu()->FindMarked()->Label());
			modified = true;
			break;

		case MSG_COLOR_CHANGED:
			PrefHandler::Default()->setRGB(fColorField->Menu()->FindMarked()->Label(),
				fColorControl->ValueAsColor());
			modified = true;
			break;

		case MSG_COLOR_FIELD_CHANGED:
			fColorControl->SetValue(PrefHandler::Default()->getRGB(
				fColorField->Menu()->FindMarked()->Label()));
			break;

		default:
			PrefView::MessageReceived(msg);
			return;
	}

	if (modified) {
		fAppearancePrefViewMessenger.SendMessage(msg);

		BMessenger messenger(this);
		messenger.SendMessage(MSG_PREF_MODIFIED);
	}
}


BMenu *
AppearancePrefView::_MakeFontMenu(uint32 command, const char *defaultFontName)
{
	BPopUpMenu *menu = new BPopUpMenu("");
	int32 numFamilies = count_font_families();

	for (int32 i = 0; i < numFamilies; i++) {
		font_family family;
		uint32 flags;

		if (get_font_family(i, &family, &flags) == B_OK) {
			BFont font;
			font.SetFamilyAndStyle(family, NULL);
			if (IsFontUsable(font)) {
				BMenuItem *item = new BMenuItem(family, new BMessage(command));
				menu->AddItem(item);
				if (!strcmp(defaultFontName, family))
					item->SetMarked(true);
			}
		}
	}

	return menu;
}


BMenu *
AppearancePrefView::_MakeSizeMenu(uint32 command, uint8 defaultSize)
{
	BPopUpMenu *menu = new BPopUpMenu("size");
	int32 sizes[] = {9, 10, 11, 12, 14, 16, 18, 0};

	bool found = false;

	for (uint32 i = 0; sizes[i]; i++) {
		BString string;
		string << sizes[i];

		BMenuItem* item = new BMenuItem(string.String(), new BMessage(command));
		menu->AddItem(item);

		if (sizes[i] == defaultSize) {
			item->SetMarked(true);
			found = true;
		} else if (sizes[i] > defaultSize && !found) {
			BString string;
			string << defaultSize;
			BMenuItem* item = new BMenuItem(string.String(), new BMessage(command));
			item->SetMarked(true);
			menu->AddItem(item, i);
			found = true;
		}
	}

	return menu;
}
