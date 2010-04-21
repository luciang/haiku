/*
 * Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "MimeTypeListView.h"
#include "TypeListWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <GroupLayoutBuilder.h>
#include <Locale.h>
#include <ScrollView.h>

#include <string.h>


#undef TR_CONTEXT
#define TR_CONTEXT "Type List Window"


const uint32 kMsgTypeSelected = 'tpsl';
const uint32 kMsgSelected = 'seld';


TypeListWindow::TypeListWindow(const char* currentType,
	uint32 what, BWindow* target)
	:
	BWindow(BRect(100, 100, 360, 440), TR("Choose type"), B_MODAL_WINDOW,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fWhat(what)
{
	float padding = 3.0f;
	//if (be_control_look)
		// padding = be_control_look->DefaultItemSpacing();
		// seems too big

	fSelectButton = new BButton("select", TR("Done"),
		new BMessage(kMsgSelected));
	fSelectButton->SetEnabled(false);

	BButton* button = new BButton("cancel", TR("Cancel"),
		new BMessage(B_CANCEL));

	fSelectButton->MakeDefault(true);

	fListView = new MimeTypeListView("typeview", NULL, true, false);
	fListView->SetSelectionMessage(new BMessage(kMsgTypeSelected));
	fListView->SetInvocationMessage(new BMessage(kMsgSelected));

	BScrollView* scrollView = new BScrollView("scrollview", fListView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	SetLayout(new BGroupLayout(B_VERTICAL));
	AddChild(BGroupLayoutBuilder(B_VERTICAL, padding)
		.Add(scrollView)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, padding)		
			.Add(button)
			.Add(fSelectButton)
		)
		.SetInsets(padding, padding, padding, padding)
	);

	BAlignment buttonAlignment = 
		BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER);
	button->SetExplicitAlignment(buttonAlignment);
	fSelectButton->SetExplicitAlignment(buttonAlignment);

	MoveTo(target->Frame().LeftTop() + BPoint(15.0f, 15.0f));
}


TypeListWindow::~TypeListWindow()
{
}


void
TypeListWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTypeSelected:
			fSelectButton->SetEnabled(fListView->CurrentSelection() >= 0);
			break;

		case kMsgSelected:
		{
			MimeTypeItem* item = dynamic_cast<MimeTypeItem*>(fListView->ItemAt(
				fListView->CurrentSelection()));
			if (item != NULL) {
				BMessage select(fWhat);
				select.AddString("type", item->Type());
				fTarget.SendMessage(&select);
			}

			// supposed to fall through
		}
		case B_CANCEL:
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}

