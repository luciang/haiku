/*
 * Copyright 2008-2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 * 
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 * 		Axel Dörfler <axeld@pinc-software.de>
 */ 
 
#include "BootManagerWindow.h"

#include <Alert.h>
#include <Application.h>
#include <TextView.h>


static const char* kSignature = "application/x-vnd.Haiku-bootman";


class BootManager : public BApplication {
public:
	BootManager();

	virtual void ReadyToRun();

	virtual void AboutRequested();
};


BootManager::BootManager()
	: BApplication(kSignature)
{
}


void
BootManager::ReadyToRun()
{
	BootManagerWindow * window = new BootManagerWindow();
	window->Show();
}


void
BootManager::AboutRequested()
{
	BAlert *alert = new BAlert("about", "Haiku Boot Manager\n\n"
		"written by\n"
		"\tDavid Dengg\n"
		"\tMichael Pfeiffer\n"
		"\n"
		"Copyright 2008-10, Haiku Inc.\n", "OK");
	BTextView *view = alert->TextView();
	BFont font;

	view->SetStylable(true);

	view->GetFont(&font);
	font.SetSize(18);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, 18, &font);

	alert->Go();
}


//	#pragma mark -


int
main(int /*argc*/, char** /*argv*/)
{
	BootManager application;
	application.Run();

	return 0;
}
