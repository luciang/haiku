/*
 * Copyright 2003-2008, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval
 *		Oliver Ruiz Dorantes
 *		Atsushi Takamatsu
 */


#include "HWindow.h"
#include "HEventList.h"

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Box.h>
#include <Button.h>
#include <ClassInfo.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <MediaFiles.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringView.h>
#include <Sound.h>


HWindow::HWindow(BRect rect, const char *name)
	: _inherited(rect, name, B_TITLED_WINDOW, 0),
	fFilePanel(NULL),
	fPlayer(NULL)
{
	InitGUI();
	float min_width, min_height, max_width, max_height;
	GetSizeLimits(&min_width, &max_width, &min_height, &max_height);
	min_width = 300;
	min_height = 200;
	SetSizeLimits(min_width, max_width, min_height, max_height);

	fFilePanel = new BFilePanel();
	fFilePanel->SetTarget(this);
}


HWindow::~HWindow()
{
	delete fFilePanel;
	delete fPlayer;
}


void
HWindow::InitGUI()
{
	BRect rect = Bounds();
	rect.bottom -= 106;
	BView *listView = new BView(rect, "", B_FOLLOW_NONE, B_WILL_DRAW | B_PULSE_NEEDED);
	listView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(listView);

	rect.left += 13;
	rect.right -= 13;
	rect.top += 28;
	rect.bottom -= 7;
	fEventList = new HEventList(rect);
	listView->AddChild(fEventList);
	fEventList->SetType(BMediaFiles::B_SOUNDS);
	fEventList->SetSelectionMode(B_SINGLE_SELECTION_LIST);

	rect = Bounds();
	rect.top = rect.bottom - 105;
	BView *view = new BView(rect, "", B_FOLLOW_NONE, B_WILL_DRAW | B_PULSE_NEEDED);
	view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(view);
	rect = view->Bounds().InsetBySelf(12, 12);
	BBox *box = new BBox(rect, "", B_FOLLOW_ALL);
	view->AddChild(box);
	rect = box->Bounds();
	rect.top += 10;
	rect.left += 15;
	rect.right -= 10;
	rect.bottom = rect.top + 20;
	BMenu *menu = new BMenu("file");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	menu->AddSeparatorItem();
	
	menu->AddItem(new BMenuItem("<none>", new BMessage(M_NONE_MESSAGE)));
	menu->AddItem(new BMenuItem("Other" B_UTF8_ELLIPSIS, new BMessage(M_OTHER_MESSAGE)));
	BMenuField *menuField = new BMenuField(rect, "filemenu", "Sound File:", menu,
		B_FOLLOW_TOP | B_FOLLOW_LEFT);
	menuField->SetDivider(menuField->StringWidth("Sound File:") + 10);
	box->AddChild(menuField);
	rect.OffsetBy(-2, menuField->Bounds().Height() + 15);
	BButton *button = new BButton(rect, "stop", "Stop", new BMessage(M_STOP_MESSAGE),
		B_FOLLOW_RIGHT | B_FOLLOW_TOP);
	button->ResizeToPreferred();
	button->SetEnabled(false);
	button->MoveTo(box->Bounds().right - button->Bounds().Width() - 15, rect.top);
	box->AddChild(button);

	rect = button->Frame();
	view->ResizeTo(view->Bounds().Width(), 24 + rect.bottom + 12);
	box->ResizeTo(box->Bounds().Width(), rect.bottom + 12);

	button->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
	button = new BButton(rect, "play", "Play", new BMessage(M_PLAY_MESSAGE),
		B_FOLLOW_RIGHT | B_FOLLOW_TOP);
	button->ResizeToPreferred();
	button->SetEnabled(false);
	button->MoveTo(rect.left - button->Bounds().Width() - 15, rect.top);
	box->AddChild(button);
	
	view->MoveTo(0, listView->Frame().bottom);
	ResizeTo(Bounds().Width(), listView->Frame().bottom + view->Bounds().Height());
	listView->SetResizingMode(B_FOLLOW_ALL);
	view->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);

	// setup file menu
	SetupMenuField();
	menu->FindItem("<none>")->SetMarked(true);
}


void
HWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case M_OTHER_MESSAGE:
		{
			BMenuField *menufield = cast_as(FindView("filemenu"), BMenuField);
			BMenu *menu = menufield->Menu();

			HEventRow* row = (HEventRow *)fEventList->CurrentSelection();
			if (row != NULL) {
				BPath path(row->Path());
				if (path.InitCheck() != B_OK) {
					BMenuItem *item = menu->FindItem("<none>");
					if (item)
						item->SetMarked(true);
				} else {
					BMenuItem *item = menu->FindItem(path.Leaf());
					if (item)
						item->SetMarked(true);
				}
			}
			fFilePanel->Show();
			break;
		}

		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			HEventRow* row = (HEventRow *)fEventList->CurrentSelection();
			if (message->FindRef("refs", &ref) == B_OK && row != NULL) {
				BMenuField *menufield = cast_as(FindView("filemenu"), BMenuField);
				BMenu *menu = menufield->Menu();

				// check audio file
				BNode node(&ref);
				BNodeInfo ninfo(&node);
				char type[B_MIME_TYPE_LENGTH + 1];
				ninfo.GetType(type);
				BMimeType mtype(type);
				BMimeType superType;
				mtype.GetSupertype(&superType);
				if (superType.Type() == NULL || strcmp(superType.Type(), "audio") != 0) {
					beep();
					(new BAlert("", "This is not a audio file.", "OK", NULL, NULL,
						B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go();
					break;
				}

				// add file item
				BMessage *msg = new BMessage(M_ITEM_MESSAGE);
				BPath path(&ref);
				msg->AddRef("refs", &ref);
				BMenuItem *menuitem = menu->FindItem(path.Leaf());
				if (!menuitem)
					menu->AddItem(menuitem = new BMenuItem(path.Leaf(), msg), 0);
				// refresh item
				fEventList->SetPath(BPath(&ref).Path());
				// check file menu
				if (menuitem)
					menuitem->SetMarked(true);
			}
			break;
		}

		case M_PLAY_MESSAGE:
		{
			HEventRow* row = (HEventRow *)fEventList->CurrentSelection();
			if (row != NULL) {
				const char *path = row->Path();
				if (path) {
					entry_ref ref;
					::get_ref_for_path(path, &ref);
					delete fPlayer;
					fPlayer = new BFileGameSound(&ref, false);
					fPlayer->StartPlaying();
				}
			}
			break;
		}

		case M_STOP_MESSAGE:
		{
			if (!fPlayer)
				break;
			if (fPlayer->IsPlaying()) {
				fPlayer->StopPlaying();
				delete fPlayer;
				fPlayer = NULL;
			}
			break;
		}

		case M_EVENT_CHANGED:
		{
			const char *path;
			BMenuField *menufield = cast_as(FindView("filemenu"), BMenuField);
			BMenu *menu = menufield->Menu();

			if (message->FindString("path", &path) == B_OK) {
				BPath path(path);
				if (path.InitCheck() != B_OK) {
					BMenuItem *item = menu->FindItem("<none>");
					if (item)
						item->SetMarked(true);
				} else {
					BMenuItem *item = menu->FindItem(path.Leaf());
					if (item)
						item->SetMarked(true);
				}
			}
			break;
		}

		case M_ITEM_MESSAGE:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				fEventList->SetPath(BPath(&ref).Path());
			break;
		}

		case M_NONE_MESSAGE:
		{
			fEventList->SetPath(NULL);
			break;
		}

		default:
			_inherited::MessageReceived(message);
	}
}


void
HWindow::SetupMenuField()
{
	BMenuField *menufield = cast_as(FindView("filemenu"), BMenuField);
	BMenu *menu = menufield->Menu();
	int32 count = fEventList->CountRows();
	for (int32 i = 0; i < count; i++) {
		HEventRow *row = (HEventRow *)fEventList->RowAt(i);
		if (!row)
			continue;

		BPath path(row->Path());
		if (path.InitCheck() != B_OK)
			continue;
		if (menu->FindItem(path.Leaf()))
			continue;

		BMessage *msg = new BMessage(M_ITEM_MESSAGE);
		entry_ref ref;
		::get_ref_for_path(path.Path(), &ref);
		msg->AddRef("refs", &ref);
		menu->AddItem(new BMenuItem(path.Leaf(), msg), 0);
	}

	BPath path;
	BDirectory dir;
	BEntry entry;
	BPath item_path;
	
	status_t err = find_directory(B_BEOS_SOUNDS_DIRECTORY, &path);
	if (err == B_OK)
		err = dir.SetTo(path.Path());
	while (err == B_OK) {
		err = dir.GetNextEntry((BEntry*)&entry, true);
		if (entry.InitCheck() != B_NO_ERROR)
			break;

		entry.GetPath(&item_path);

		if (menu->FindItem(item_path.Leaf()))
			continue;

		BMessage *msg = new BMessage(M_ITEM_MESSAGE);
		entry_ref ref;
		::get_ref_for_path(item_path.Path(), &ref);
		msg->AddRef("refs", &ref);
		menu->AddItem(new BMenuItem(item_path.Leaf(), msg), 0);
	}

	err = find_directory(B_USER_SOUNDS_DIRECTORY, &path);
	if (err == B_OK)
		err = dir.SetTo(path.Path());
	while (err == B_OK) {
		err = dir.GetNextEntry((BEntry*)&entry, true);
		if (entry.InitCheck() != B_NO_ERROR)
			break;

		entry.GetPath(&item_path);

		if (menu->FindItem(item_path.Leaf()))
			continue;

		BMessage *msg = new BMessage(M_ITEM_MESSAGE);
		entry_ref ref;

		::get_ref_for_path(item_path.Path(), &ref);
		msg->AddRef("refs", &ref);
		menu->AddItem(new BMenuItem(item_path.Leaf(), msg), 0);
	}

	err = find_directory(B_COMMON_SOUNDS_DIRECTORY, &path);
	if (err == B_OK)
		err = dir.SetTo(path.Path());	
	while (err == B_OK) {
		err = dir.GetNextEntry((BEntry*)&entry, true);
		if (entry.InitCheck() != B_NO_ERROR)
			break;

		entry.GetPath(&item_path);

		if (menu->FindItem(item_path.Leaf()))
			continue;

		BMessage *msg = new BMessage(M_ITEM_MESSAGE);
		entry_ref ref;

		::get_ref_for_path(item_path.Path(), &ref);
		msg->AddRef("refs", &ref);
		menu->AddItem(new BMenuItem(item_path.Leaf(), msg), 0);
	}

}


void
HWindow::Pulse()
{
	HEventRow* row = (HEventRow *)fEventList->CurrentSelection();
	BMenuField *menufield = cast_as(FindView("filemenu"), BMenuField);
	BButton *button = cast_as(FindView("play"), BButton);
	BButton *stop = cast_as(FindView("stop"), BButton);

	if (!menufield)
		return;

	if (row != NULL) {
		menufield->SetEnabled(true);

		const char *path = row->Path();
		if (path && strcmp(path, ""))
			button->SetEnabled(true);
		else
			button->SetEnabled(false);
	} else {
		menufield->SetEnabled(false);
		button->SetEnabled(false);
	}

	if (fPlayer) {
		if (fPlayer->IsPlaying())
			stop->SetEnabled(true);
		else
			stop->SetEnabled(false);
	} else
		stop->SetEnabled(false);
}


void
HWindow::DispatchMessage(BMessage *message, BHandler *handler)
{
	if (message->what == B_PULSE)
		Pulse();
	BWindow::DispatchMessage(message, handler);
}


bool
HWindow::QuitRequested()
{
	fEventList->RemoveAll();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
