/*
 * Copyright 2008, Ralf Schülke, teammaui@web.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
 
#include <stdio.h>
#include <unistd.h>

#include <Button.h>

#include "PairsTopButton.h"
#include "PairsGlobal.h"


TopButton::TopButton(int x, int y, BMessage* message)
	: BButton(BRect(x, y, x + kBitmapSize, y + kBitmapSize), "top_button",
		"?", message)
{
	SetFontSize(54);
}


TopButton::~TopButton()
{
}
