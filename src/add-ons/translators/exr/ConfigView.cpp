/*
 * Copyright 2008, Jérôme Duval. All rights reserved.
 * Copyright 2005-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2009, Maxime Simon, maxime.simon@gmail.com. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ConfigView.h"
#include "EXRTranslator.h"

#include <StringView.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <SpaceLayoutItem.h>

#include <stdio.h>
#include <string.h>


ConfigView::ConfigView(uint32 flags)
	: BView("EXRTranslator Settings", flags)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BStringView *fTitle = new BStringView("title", "EXR Images");
	fTitle->SetFont(be_bold_font);

	char version[256];
	sprintf(version, "Version %d.%d.%d, %s",
		int(B_TRANSLATION_MAJOR_VERSION(EXR_TRANSLATOR_VERSION)),
		int(B_TRANSLATION_MINOR_VERSION(EXR_TRANSLATOR_VERSION)),
		int(B_TRANSLATION_REVISION_VERSION(EXR_TRANSLATOR_VERSION)),
		__DATE__);
	BStringView *fVersion = new BStringView("version", version);

	BStringView *fCopyright = new BStringView("copyright",
		B_UTF8_COPYRIGHT "2008 Haiku Inc.");

	BStringView *fCopyright2 = new BStringView("copyright2",
		"Based on OpenEXR (http://www.openexr.com)");

	BStringView *fCopyright3 = new BStringView("copyright3",
		B_UTF8_COPYRIGHT "2006, Industrial Light & Magic,");

	BStringView *fCopyright4 = new BStringView("copyright4",
		"a division of Lucasfilm Entertainment Company Ltd");

	// Build the layout
	SetLayout(new BGroupLayout(B_HORIZONTAL));

	AddChild(BGroupLayoutBuilder(B_VERTICAL, 7)
		.Add(fTitle)
		.Add(fVersion)
		.AddGlue()
		.Add(fCopyright)
		.Add(fCopyright2)
		.AddGlue()
		.Add(fCopyright3)
		.Add(fCopyright4)
		.AddGlue()
		.SetInsets(5, 5, 5, 5)
	);

	BFont font;
	GetFont(&font);
	SetExplicitPreferredSize(BSize((font.Size() * 400)/12, (font.Size() * 200)/12));
}
 

ConfigView::~ConfigView()
{
}

