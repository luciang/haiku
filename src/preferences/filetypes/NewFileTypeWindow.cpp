/*
 * Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "FileTypes.h"
#include "FileTypesWindow.h"
#include "NewFileTypeWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <String.h>
#include <TextControl.h>

#include <string.h>


#undef TR_CONTEXT
#define TR_CONTEXT "New File Type Window"


const uint32 kMsgSupertypeChosen = 'sptc';
const uint32 kMsgNewSupertypeChosen = 'nstc';

const uint32 kMsgNameUpdated = 'nmup';

const uint32 kMsgAddType = 'atyp';


NewFileTypeWindow::NewFileTypeWindow(FileTypesWindow* target, const char* currentType)
	: BWindow(BRect(100, 100, 350, 200), TR("New file type"), B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE | B_ASYNCHRONOUS_CONTROLS),
	fTarget(target)
{
	BRect rect = Bounds();
	BView* topView = new BView(rect, NULL, B_FOLLOW_ALL, B_WILL_DRAW);
	topView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(topView);

	float labelWidth = be_plain_font->StringWidth(TR("Internal name:")) + 2.0f;

	rect.InsetBy(8.0f, 6.0f);
	fSupertypesMenu = new BPopUpMenu("supertypes");
	BMenuItem* item;
	BMessage types;
	if (BMimeType::GetInstalledSupertypes(&types) == B_OK) {
		const char* type;
		int32 i = 0;
		while (types.FindString("super_types", i++, &type) == B_OK) {
			fSupertypesMenu->AddItem(item = new BMenuItem(type,
				new BMessage(kMsgSupertypeChosen)));

			// select super type close to the current type
			if (currentType != NULL) {
				if (!strncmp(type, currentType, strlen(type)))
					item->SetMarked(true);
			} else if (i == 1)
				item->SetMarked(true);
		}

		if (i > 1)
			fSupertypesMenu->AddSeparatorItem();
	}
	fSupertypesMenu->AddItem(new BMenuItem(TR("Add new group"),
		new BMessage(kMsgNewSupertypeChosen)));

	BMenuField* menuField = new BMenuField(rect, "supertypes",
		TR("Group:"), fSupertypesMenu);
	menuField->SetDivider(labelWidth);
	menuField->SetAlignment(B_ALIGN_RIGHT);
	float width, height;
	menuField->GetPreferredSize(&width, &height);
	menuField->ResizeTo(rect.Width(), height);
	topView->AddChild(menuField);

	fNameControl = new BTextControl(rect, "internal", TR("Internal name:"), "",
		NULL, B_FOLLOW_LEFT_RIGHT);
	fNameControl->SetModificationMessage(new BMessage(kMsgNameUpdated));
	fNameControl->SetDivider(labelWidth);
	fNameControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	// filter out invalid characters that can't be part of a MIME type name
	BTextView* textView = fNameControl->TextView();
	const char* disallowedCharacters = "/<>@,;:\"()[]?=";
	for (int32 i = 0; disallowedCharacters[i]; i++) {
		textView->DisallowChar(disallowedCharacters[i]);
	}

	fNameControl->GetPreferredSize(&width, &height);
	fNameControl->ResizeTo(rect.Width(), height);
	fNameControl->MoveTo(8.0f, 12.0f + menuField->Bounds().Height());
	topView->AddChild(fNameControl);

	fAddButton = new BButton(rect, "add", TR("Add type"), new BMessage(kMsgAddType),
		B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	fAddButton->ResizeToPreferred();
	fAddButton->MoveTo(Bounds().Width() - 8.0f - fAddButton->Bounds().Width(),
		Bounds().Height() - 8.0f - fAddButton->Bounds().Height());
	fAddButton->SetEnabled(false);
	topView->AddChild(fAddButton);

	BButton* button = new BButton(rect, "cancel", TR("Cancel"),
		new BMessage(B_QUIT_REQUESTED), B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	button->ResizeToPreferred();
	button->MoveTo(fAddButton->Frame().left - 10.0f - button->Bounds().Width(),
		fAddButton->Frame().top);
	topView->AddChild(button);

	ResizeTo(labelWidth * 4.0f + 24.0f, fNameControl->Bounds().Height()
		+ menuField->Bounds().Height() + fAddButton->Bounds().Height() + 30.0f);
	SetSizeLimits(button->Bounds().Width() + fAddButton->Bounds().Width() + 26.0f,
		32767.0f, Frame().Height(), Frame().Height());

	fAddButton->MakeDefault(true);
	fNameControl->MakeFocus(true);

	target->PlaceSubWindow(this);
}


NewFileTypeWindow::~NewFileTypeWindow()
{
}


void
NewFileTypeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSupertypeChosen:
			fAddButton->SetLabel(TR("Add type"));
			fNameControl->SetLabel(TR("Internal name:"));
			fNameControl->MakeFocus(true);
			break;

		case kMsgNewSupertypeChosen:
			fAddButton->SetLabel(TR("Add group"));
			fNameControl->SetLabel(TR("Group name:"));
			fNameControl->MakeFocus(true);
			break;

		case kMsgNameUpdated:
		{
			bool empty = fNameControl->Text() == NULL
				|| fNameControl->Text()[0] == '\0';

			if (fAddButton->IsEnabled() == empty)
				fAddButton->SetEnabled(!empty);
			break;
		}

		case kMsgAddType:
		{
			BMenuItem* item = fSupertypesMenu->FindMarked();
			if (item != NULL) {
				BString type;
				if (fSupertypesMenu->IndexOf(item) != fSupertypesMenu->CountItems() - 1) {
					// add normal type
					type = item->Label();
					type.Append("/");
				}

				type.Append(fNameControl->Text());

				BMimeType mimeType(type.String());
				if (mimeType.IsInstalled()) {
					error_alert(TR("This file type already exists."));
					break;
				}

				status_t status = mimeType.Install();
				if (status != B_OK)
					error_alert(TR("Could not install file type"), status);
				else {
					BMessage update(kMsgSelectNewType);
					update.AddString("type", type.String());

					fTarget.SendMessage(&update);
				}
			}
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
NewFileTypeWindow::QuitRequested()
{
	fTarget.SendMessage(kMsgNewTypeWindowClosed);
	return true;
}
