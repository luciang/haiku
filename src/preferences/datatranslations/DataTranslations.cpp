/*
 * Copyright 2002-2010, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Oliver Siebenmarck
 *		Andrew McCall, mccall@digitalparadise.co.uk
 *		Michael Wilber
 */

#include "DataTranslations.h"

#include <Alert.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <String.h>
#include <TranslatorRoster.h>
#include <TextView.h>

#include "DataTranslationsSettings.h"
#include "DataTranslationsWindow.h"


const char* kApplicationSignature = "application/x-vnd.Haiku-DataTranslations";


DataTranslationsApplication::DataTranslationsApplication()
	: BApplication(kApplicationSignature)
{
	new DataTranslationsWindow();
}


DataTranslationsApplication::~DataTranslationsApplication()
{
}


void
DataTranslationsApplication::SetWindowCorner(const BPoint& leftTop)
{
	fSettings.SetWindowCorner(leftTop);
}


void
DataTranslationsApplication::AboutRequested()
{
	BAlert* alert = new BAlert("about", "DataTranslations\n\twritten by Oliver "
		"Siebenmarck and others\n\tCopyright 2002-2010, Haiku Inc. All rights "
		"reserved.\n", "OK");

	BTextView* view = alert->TextView();
	view->SetStylable(true);

	BFont font;
	view->GetFont(&font);
	font.SetSize(18);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, 16, &font);

	alert->Go();
}


void
DataTranslationsApplication::_InstallError(const char* name, status_t status)
{
	BString text("Could not install ");
	text << name << ":\n" << strerror(status);
	BAlert* alert = new BAlert("DataTranslations - Error", text.String(), "Ok");
	alert->Go();
}


/*!
	Installs the given entry in the target directory either by moving
	or copying the entry.
*/
status_t
DataTranslationsApplication::_Install(BDirectory& target, BEntry& entry)
{
	// Find out wether we need to copy it
	status_t status = entry.MoveTo(&target, NULL, true);
	if (status == B_OK)
		return B_OK;

	// we need to copy the file

	// TODO!
	return B_ERROR;
}


void
DataTranslationsApplication::_NoTranslatorError(const char* name)
{
	BString text("The item '");
	text << name << "' does not appear to be a Translator and will not be "
		"installed.";
	BAlert* alert = new BAlert("", text.String(), "Ok");
	alert->Go();
}


void
DataTranslationsApplication::RefsReceived(BMessage* message)
{
	BTranslatorRoster* roster = BTranslatorRoster::Default();

	BPath path;
	status_t status = find_directory(B_USER_ADDONS_DIRECTORY, &path, true);
	if (status != B_OK) {
		_InstallError("translator", status);
		return;
	}

	BDirectory target;
	status = target.SetTo(path.Path());
	if (status == B_OK) {
		if (!target.Contains("Translators"))
			status = target.CreateDirectory("Translators", &target);
		else
			status = target.SetTo(&target, "Translators");
	}
	if (status != B_OK) {
		_InstallError("translator", status);
		return;
	}

	int32 i = 0;
	entry_ref ref;
	while (message->FindRef("refs", i++, &ref) == B_OK) {
		if (!roster->IsTranslator(&ref))
			_NoTranslatorError(ref.name);

		BEntry entry(&ref, true);
		status = entry.InitCheck();
		if (status != B_OK) {
			_InstallError(ref.name, status);
			continue;
		}

		if (target.Contains(ref.name)) {
			BString string("An item named '");
			string << ref.name << "' already exists in the Translators folder! "
				"Shall the existing translator be overwritten?";

			BAlert* alert = new BAlert("DataTranslations - Note", string.String(),
				"Cancel", "Overwrite");
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				continue;

			// the original file will be replaced
		}

		// find out wether we need to copy it or not

		status = _Install(target, entry);
		if (status == B_OK) {
			BAlert* alert = new BAlert("DataTranslations - Note", "The new "
				"translator has been installed successfully.", "OK");
			alert->Go(NULL);
		} else
			_InstallError(ref.name, status);
	}
}


//	#pragma mark -


int
main(int, char**)
{
	DataTranslationsApplication app;
	app.Run();

	return 0;
}
