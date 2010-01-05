/*
 * Copyright 2009-2010, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2009, Bryce Groff, brycegroff@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <stdio.h>

#include "InitializeParameterEditor.h"

#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <GridLayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PartitionParameterEditor.h>
#include <PopUpMenu.h>
#include <SpaceLayoutItem.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>


static uint32 MSG_BLOCK_SIZE = 'blsz';


InitializeBFSEditor::InitializeBFSEditor()
	:
	BPartitionParameterEditor(),
	fView(NULL),
	fNameTC(NULL),
	fBlockSizeMF(NULL),
	fUseIndicesCB(NULL),
	fParameters(NULL)
{
	_CreateViewControls();
}


InitializeBFSEditor::~InitializeBFSEditor()
{
}


BView*
InitializeBFSEditor::View()
{
	return fView;
}


bool
InitializeBFSEditor::FinishedEditing()
{
	fParameters = "";
	if (BMenuItem* item = fBlockSizeMF->Menu()->FindMarked()) {
		const char* size;
		BMessage* message = item->Message();
		if (!message || message->FindString("size", &size) < B_OK)
			size = "2048";
		// TODO: use libroot driver settings API
		fParameters << "block_size " << size << ";\n";
	}
	if (fUseIndicesCB->Value() == B_CONTROL_OFF)
		fParameters << "noindex;\n";

	fParameters << "name \"" << fNameTC->Text() << "\";\n";

	return true;
}


status_t
InitializeBFSEditor::GetParameters(BString* parameters)
{
	if (parameters == NULL)
		return B_BAD_VALUE;

	*parameters = fParameters;
	return B_OK;
}


status_t
InitializeBFSEditor::PartitionNameChanged(const char* name)
{
	fNameTC->SetText(name);
	return B_OK;
}


void
InitializeBFSEditor::_CreateViewControls()
{
	fNameTC = new BTextControl("Name", "Haiku", NULL);
	// TODO find out what is the max length for this specific FS partition name
	fNameTC->TextView()->SetMaxBytes(31);

	BPopUpMenu* blocksizeMenu = new BPopUpMenu("Blocksize");
	BMessage* message = new BMessage(MSG_BLOCK_SIZE);
	message->AddString("size", "1024");
	blocksizeMenu->AddItem(new BMenuItem("1024 (Mostly small files)",
		message));
	message = new BMessage(MSG_BLOCK_SIZE);
	message->AddString("size", "2048");
	BMenuItem* defaultItem = new BMenuItem("2048 (Recommended)", message);
	blocksizeMenu->AddItem(defaultItem);
	message = new BMessage(MSG_BLOCK_SIZE);
	message->AddString("size", "4096");
	blocksizeMenu->AddItem(new BMenuItem("4096", message));
	message = new BMessage(MSG_BLOCK_SIZE);
	message->AddString("size", "8192");
	blocksizeMenu->AddItem(new BMenuItem("8192 (Mostly large files)",
		message));

	fBlockSizeMF = new BMenuField("Blocksize", blocksizeMenu, NULL);
	defaultItem->SetMarked(true);

	fUseIndicesCB = new BCheckBox("Enable query support", NULL);
	fUseIndicesCB->SetValue(true);
	fUseIndicesCB->SetToolTip("Disabling query support may speed up "
		"certain file system operations, but should only be used "
		"if one is absolutely certain that one will not need queries.\n"
		"Any volume that is intended for booting Haiku must have query "
		"support enabled.");

	float spacing = be_control_look->DefaultItemSpacing();

	fView = BGridLayoutBuilder(spacing, spacing)
		// row 1
		.Add(fNameTC->CreateLabelLayoutItem(), 0, 0)
		.Add(fNameTC->CreateTextViewLayoutItem(), 1, 0)

		// row 2
		.Add(fBlockSizeMF->CreateLabelLayoutItem(), 0, 1)
		.Add(fBlockSizeMF->CreateMenuBarLayoutItem(), 1, 1)

		// row 3
		.Add(fUseIndicesCB, 0, 2, 2)

		.SetInsets(spacing, spacing, spacing, spacing)
	;
}
