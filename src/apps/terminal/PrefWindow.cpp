/*
 * Copyright 2007-2008, Haiku, Inc.
 * Copyright 2003-2004 Kian Duffy, myob@users.sourceforge.net
 * Parts Copyright 1998-1999 Kazuho Okui and Takashi Murai. 
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "AppearPrefView.h"
#include "PrefHandler.h"
#include "PrefWindow.h"
#include "PrefView.h"
#include "TermConst.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <FilePanel.h>
#include <GroupLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <Path.h>

#include <stdio.h>


PrefWindow::PrefWindow(const BMessenger &messenger)
	: BWindow(BRect(0, 0, 375, 185), "Terminal Preferences",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_RESIZABLE|B_NOT_ZOOMABLE|B_AUTO_UPDATE_SIZE_LIMITS),
	fPreviousPref(new PrefHandler(PrefHandler::Default())),
	fSavePanel(NULL),
	fDirty(false),
	fTerminalMessenger(messenger)
{
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.AddGroup(B_VERTICAL, 1)
		.SetInsets(10, 10, 10, 10)
			.Add(new AppearancePrefView("Appearance", fTerminalMessenger))
			.AddGroup(B_HORIZONTAL)
				.Add(fSaveAsFileButton = new BButton("savebutton",
					"Save to File" B_UTF8_ELLIPSIS,
					new BMessage(MSG_SAVEAS_PRESSED), B_WILL_DRAW))
				.AddGlue()
				.Add(fRevertButton = new BButton("revertbutton",
					"Cancel", new BMessage(MSG_REVERT_PRESSED),
					B_WILL_DRAW))
				.Add(fSaveButton = new BButton("okbutton", "OK",
					new BMessage(MSG_SAVE_PRESSED), B_WILL_DRAW))
			.End()
		.End();
		
	
	fSaveButton->MakeDefault(true);
	
	AddShortcut('Q', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));
	AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));

	CenterOnScreen();
	Show();
}


PrefWindow::~PrefWindow()
{
}


void
PrefWindow::Quit()
{
	fTerminalMessenger.SendMessage(MSG_PREF_CLOSED);
	delete fPreviousPref;
	delete fSavePanel;
	BWindow::Quit();
}


bool
PrefWindow::QuitRequested()
{
	if (!fDirty)
		return true;

	BAlert *alert = new BAlert("", "Save changes to this preference panel?",
		"Cancel", "Don't Save", "Save",
		B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
		B_WARNING_ALERT); 
	alert->SetShortcut(0, B_ESCAPE); 
	alert->SetShortcut(1, 'd'); 
	alert->SetShortcut(2, 's'); 

	int32 index = alert->Go();
	if (index == 0)
		return false;

	if (index == 2)
		_Save();

	return true;
}


void
PrefWindow::_SaveAs()
{
	if (!fSavePanel) {
		BMessenger messenger(this);
		fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger);
	}
	
	fSavePanel->Show();
}


void
PrefWindow::_SaveRequested(BMessage *msg)
{
	entry_ref dirref;
	const char *filename;

	msg->FindRef("directory", &dirref);
	msg->FindString("name", &filename);

	BDirectory dir(&dirref);
	BPath path(&dir, filename);

	PrefHandler::Default()->SaveAsText(path.Path(), PREFFILE_MIMETYPE, TERM_SIGNATURE);
}


void
PrefWindow::_Save()
{
	delete fPreviousPref;
	fPreviousPref = new PrefHandler(PrefHandler::Default());

	BPath path;
	if (PrefHandler::GetDefaultPath(path) == B_OK) {
		PrefHandler::Default()->SaveAsText(path.Path(), PREFFILE_MIMETYPE);
		fDirty = false;
	}
}


void
PrefWindow::_Revert()
{
	if (fDirty) {
		PrefHandler::SetDefault(new PrefHandler(fPreviousPref));

		fTerminalMessenger.SendMessage(MSG_HALF_FONT_CHANGED);
		fTerminalMessenger.SendMessage(MSG_COLOR_CHANGED);
		fTerminalMessenger.SendMessage(MSG_INPUT_METHOD_CHANGED);

		fDirty = false;
	}
}


void
PrefWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_SAVE_PRESSED:
			_Save();
			PostMessage(B_QUIT_REQUESTED);
			break;

		case MSG_SAVEAS_PRESSED:
			_SaveAs();
			break;

		case MSG_REVERT_PRESSED:
			_Revert();
			PostMessage(B_QUIT_REQUESTED);
			break;

		case MSG_PREF_MODIFIED:
			fDirty = true;
			break;

		case B_SAVE_REQUESTED:
			_SaveRequested(msg);
			break;

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}
