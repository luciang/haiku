/*
 * Copyright (c) 2003-2004 Kian Duffy <myob@users.sourceforge.net>
 * Copyright (c) 1998,99 Kazuho Okui and Takashi Murai. 
 *
 * Distributed unter the terms of the MIT license.
 */


#include <View.h>
#include <Button.h>
#include <MenuField.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <TextControl.h>
#include <stdlib.h>
#include <Beep.h>

#include "ShellPrefView.h"
#include "PrefHandler.h"
#include "TermWindow.h"
#include "TermBuffer.h"
#include "TermConst.h"
#include "MenuUtil.h"
#include "TTextControl.h"


extern PrefHandler *PrefHandler::Default();


ShellPrefView::ShellPrefView(BRect frame, const char *name,
	TermWindow *window)
	: PrefView(frame, name)
{
	fTermWindow = window;

	mCols = new TTextControl(BRect(0, 0, 160, 20), "cols", "Columns", "",
		new BMessage(MSG_COLS_CHANGED));
	AddChild(mCols);
	mCols->SetText(PrefHandler::Default()->getString(PREF_COLS));

	mRows = new TTextControl(BRect(0, 30, 160, 50), "rows", "Rows", "",
		new BMessage(MSG_ROWS_CHANGED));
	AddChild(mRows);
	mRows->SetText(PrefHandler::Default()->getString(PREF_ROWS));

	mHistory = new TTextControl(BRect(0, 60, 160, 80), "history", "History", "",
		new BMessage(MSG_HISTORY_CHANGED));
	AddChild(mHistory);
	mHistory->SetText(PrefHandler::Default()->getString(PREF_HISTORY_SIZE));
}


void
ShellPrefView::Revert()
{
	mCols->SetText(PrefHandler::Default()->getString(PREF_COLS));
	mRows->SetText(PrefHandler::Default()->getString(PREF_ROWS));
	mHistory->SetText(PrefHandler::Default()->getString(PREF_HISTORY_SIZE));
}


void
ShellPrefView::SaveIfModified()
{
	BMessenger messenger(fTermWindow);

	if (mCols->IsModified()) {
		PrefHandler::Default()->setString(PREF_COLS, mCols->Text());
		messenger.SendMessage(MSG_COLS_CHANGED);
		mCols->ModifiedText(false);
	}
	if (mRows->IsModified()) {
		PrefHandler::Default()->setString(PREF_ROWS, mRows->Text());
		messenger.SendMessage(MSG_ROWS_CHANGED);
		mRows->ModifiedText(false);
	}
	//if (mShell->IsModified())
	//	PrefHandler::Default()->setString (PREF_SHELL, mShell->Text());

	if (mHistory->IsModified()) {
		int size = atoi(mHistory->Text());
		if (size < 512)
			mHistory->SetText("512");
		if (size > 1048575)
			mHistory->SetText("1048575");

		PrefHandler::Default()->setString (PREF_HISTORY_SIZE, mHistory->Text());
	}
}


void
ShellPrefView::SetControlLabels(PrefHandler& labels)
{
	mCols->SetLabel("Columns");
	mRows->SetLabel("Rows");
	//mShell->SetLabel("Shell");
	mHistory->SetLabel("History Size");
}


void
ShellPrefView::AttachedToWindow()
{
	mCols->SetTarget(this);
	mRows->SetTarget(this);
	//mShell->SetTarget(this);
	mHistory->SetTarget(this);
}


void
ShellPrefView::MessageReceived(BMessage *msg)
{
	bool modified = false;
	int size;

	switch (msg->what) {
		case MSG_COLS_CHANGED:
			size = atoi (mCols->Text());
			if (size >= MAX_COLS || size < MIN_COLS) {
				mCols->SetText (PrefHandler::Default()->getString (PREF_COLS));
				beep ();
			} else {
				PrefHandler::Default()->setString (PREF_COLS, mCols->Text());
				modified = true;
			}
			break;

		case MSG_ROWS_CHANGED:
			size = atoi (mRows->Text());
			if (size >= MAX_COLS || size < MIN_COLS) {
				mRows->SetText(PrefHandler::Default()->getString(PREF_ROWS));
				beep ();
			} else {
				PrefHandler::Default()->setString(PREF_ROWS, mRows->Text());
				modified = true;
			}
			break;

//		case MSG_SHELL_CHANGED:
//			PrefHandler::Default()->setString (PREF_SHELL, mShell->Text());
//			Window()->PostMessage(MSG_PREF_MODIFIED);
//			break;

		case MSG_HISTORY_CHANGED:
			size = atoi(mHistory->Text());
			
			if (size < 512 || size > 1048575) {
				mHistory->SetText(PrefHandler::Default()->getString(PREF_HISTORY_SIZE));
				beep ();
			} else {
				PrefHandler::Default()->setString(PREF_HISTORY_SIZE, mHistory->Text());
				Window()->PostMessage(MSG_PREF_MODIFIED);
			}
			break;

		default:
			PrefView::MessageReceived(msg);
			break;      
	}

	if (modified) {
		fTermWindow->PostMessage(msg);
		Window()->PostMessage(MSG_PREF_MODIFIED);
	}
}
