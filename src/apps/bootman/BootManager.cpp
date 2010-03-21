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
#include <Catalog.h>
#include <Locale.h>
#include <TextView.h>


#undef TR_CONTEXT
#define TR_CONTEXT "BootManager"


static const char* kSignature = "application/x-vnd.Haiku-bootman";


class BootManager : public BApplication {
public:
	BootManager();

	virtual void ReadyToRun();

	virtual void AboutRequested();

private:
	BCatalog fCatalog;
};


BootManager::BootManager()
	: BApplication(kSignature)
{
	be_locale->GetAppCatalog(&fCatalog);
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
	BString aboutText;
	const char* title = TR_CMT("Haiku Boot Manager", "About text title");
	aboutText <<
		title << "\n\n" <<
		TR("written by") << "\n"
		"\tDavid Dengg\n"
		"\tMichael Pfeiffer\n"
		"\n" <<
		TR_CMT("Copyright %year, Haiku Inc.\n", "Leave %year untranslated");
	aboutText.ReplaceLast("%year", "2008-2010");
	BAlert *alert = new BAlert("about",
		aboutText.String(), TR("OK"));
	BTextView *view = alert->TextView();
	BFont font;

	view->SetStylable(true);

	view->GetFont(&font);
	font.SetSize(18);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, strlen(title), &font);

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
