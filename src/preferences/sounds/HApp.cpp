/*
 * Copyright 2003-2010, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Atsushi Takamatsu
 *		Jérôme Duval
 *		Oliver Ruiz Dorantes
 */


#include "HApp.h"
#include "HWindow.h"

#include <Alert.h>
#include <Catalog.h>
#include <Locale.h>


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "SoundsHApp"


HApp::HApp()
	:
	BApplication("application/x-vnd.Haiku-Sounds")
{
	BRect rect;
	rect.Set(200, 150, 590, 570);

	HWindow* window = new HWindow(rect, B_TRANSLATE("Sounds"));
	window->Show();
}


HApp::~HApp()
{
}


void
HApp::AboutRequested()
{
	BAlert* alert = new BAlert(B_TRANSLATE("About Sounds"),
		B_TRANSLATE("Sounds\n"
			"  Brought to you by :\n"
			"\tOliver Ruiz Dorantes\n"
			"\tJérôme DUVAL.\n"
			"  Original work from Atsushi Takamatsu.\n"
			"Copyright ©2003-2006 Haiku"),
		B_TRANSLATE("OK"));
	alert->Go();
}


//	#pragma mark -


int
main()
{
	HApp app;
	app.Run();

	return 0;
}

