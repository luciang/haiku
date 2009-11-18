/*
 * Copyright 2003-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval,
 *		Axel Dörfler (axeld@pinc-software.de)
 *		Andrew McCall (mccall@digitalparadise.co.uk)
 */


#include "Mouse.h"
#include "MouseWindow.h"

#include <Alert.h>
#include <Screen.h>

#undef TR_CONTEXT
#define TR_CONTEXT "MouseApplication"

const char* kSignature = "application/x-vnd.Haiku-Mouse";


MouseApplication::MouseApplication()
	:
	BApplication(kSignature)					
{
	be_locale->GetAppCatalog(&fCatalog);
	
	BRect rect(0, 0, 397, 293);
	MouseWindow *window = new MouseWindow(rect);
	window->Show();
}


void
MouseApplication::AboutRequested()
{
	(new BAlert("about", TR("...by Andrew Edward McCall"),
		TR("Dig Deal")))->Go();
}


//	#pragma mark -


int
main(int /*argc*/, char **/*argv*/)
{
	MouseApplication app;
	app.Run();

	return 0;
}
