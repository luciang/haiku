/*
 * Copyright 2007, Jérôme Duval. All rights reserved.
 * Copyright 2005-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ConfigView.h"
#include "HPGSTranslator.h"

#include <StringView.h>
#include <CheckBox.h>

#include <stdio.h>
#include <string.h>


ConfigView::ConfigView(const BRect &frame, uint32 resize, uint32 flags)
	: BView(frame, "HPGSTranslator Settings", resize, flags)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	font_height fontHeight;
	be_bold_font->GetHeight(&fontHeight);
	float height = fontHeight.descent + fontHeight.ascent + fontHeight.leading;

	BRect rect(10, 10, 200, 10 + height);
	BStringView *stringView = new BStringView(rect, "title", "HPGS Images");
	stringView->SetFont(be_bold_font);
	stringView->ResizeToPreferred();
	AddChild(stringView);

	rect.OffsetBy(0, height + 10);
	char version[256];
	sprintf(version, "Version %d.%d.%d, %s",
		int(B_TRANSLATION_MAJOR_VERSION(HPGS_TRANSLATOR_VERSION)),
		int(B_TRANSLATION_MINOR_VERSION(HPGS_TRANSLATOR_VERSION)),
		int(B_TRANSLATION_REVISION_VERSION(HPGS_TRANSLATOR_VERSION)),
		__DATE__);
	stringView = new BStringView(rect, "version", version);
	stringView->ResizeToPreferred();
	AddChild(stringView);

	GetFontHeight(&fontHeight);
	height = fontHeight.descent + fontHeight.ascent + fontHeight.leading;

	rect.OffsetBy(0, height + 5);
	stringView = new BStringView(rect, "copyright", B_UTF8_COPYRIGHT "2007 Haiku Inc.");
	stringView->ResizeToPreferred();
	AddChild(stringView);

	rect.OffsetBy(0, height + 10);
	stringView = new BStringView(rect, "copyright2", "Based on HPGS (http://hpgs.berlios.de)");
	stringView->ResizeToPreferred();
	AddChild(stringView);

	rect.OffsetBy(0, height + 5);
	stringView = new BStringView(rect, "copyright3", B_UTF8_COPYRIGHT "2004-2006 ev-i Informationstechnologie GmbH");
	stringView->ResizeToPreferred();
	AddChild(stringView);
}


ConfigView::~ConfigView()
{
}

