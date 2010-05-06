/*
 * Copyright 2002-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jerome Duval (jerome.duval@free.fr)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Jonas Sundström, jonas@kirilla.se
 */


#include "BackgroundsView.h"

#include <stdio.h>
#include <stdlib.h>

#include <Bitmap.h>
#include <Catalog.h>
#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <Messenger.h>
#include <MimeType.h>
#include <Point.h>
#include <PopUpMenu.h>

#include <be_apps/Tracker/Background.h>

#include "ImageFilePanel.h"


#define TR_CONTEXT "Main View"


static const uint32 kMsgApplySettings = 'aply';
static const uint32 kMsgRevertSettings = 'rvrt';

static const uint32 kMsgUpdateColor = 'upcl';
static const uint32 kMsgAllWorkspaces = 'alwk';
static const uint32 kMsgCurrentWorkspace = 'crwk';
static const uint32 kMsgDefaultFolder = 'dffl';
static const uint32 kMsgOtherFolder = 'otfl';
static const uint32 kMsgNoImage = 'noim';
static const uint32 kMsgOtherImage = 'otim';
static const uint32 kMsgImageSelected = 'imsl';
static const uint32 kMsgFolderSelected = 'flsl';

static const uint32 kMsgCenterPlacement = 'cnpl';
static const uint32 kMsgManualPlacement = 'mnpl';
static const uint32 kMsgScalePlacement = 'scpl';
static const uint32 kMsgTilePlacement = 'tlpl';
static const uint32 kMsgIconLabelOutline = 'ilol';

static const uint32 kMsgImagePlacement = 'xypl';
static const uint32 kMsgUpdatePreviewPlacement = 'pvpl';


const uint8 kHandCursorData[68] = {
	16, 1, 2, 2,

	0, 0,		// 0000000000000000
	7, 128,		// 0000011110000000
	61, 112,	// 0011110101110000
	37, 40,		// 0010010100101000
	36, 168,	// 0010010010101000
	18, 148,	// 0001001010010100
	18, 84,		// 0001001001010100
	9, 42,		// 0000100100101010
	8, 1,		// 0000100000000001
	60, 1,		// 0011110000000001
	76, 1,		// 0100110000000001
	66, 1,		// 0100001000000001
	48, 1,		// 0011000000000001
	12, 1,		// 0000110000000001
	2, 0,		// 0000001000000000
	1, 0,		// 0000000100000000

	0, 0,		// 0000000000000000
	7, 128,		// 0000011110000000
	63, 240,	// 0011111111110000
	63, 248,	// 0011111111111000
	63, 248,	// 0011111111111000
	31, 252,	// 0001111111111100
	31, 252,	// 0001111111111100
	15, 254,	// 0000111111111110
	15, 255,	// 0000111111111111
	63, 255,	// 0011111111111111
	127, 255,	// 0111111111111111
	127, 255,	// 0111111111111111
	63, 255,	// 0011111111111111
	15, 255,	// 0000111111111111
	3, 254,		// 0000001111111110
	1, 248		// 0000000111111000
};


BackgroundsView::BackgroundsView()
	:
	BBox("BackgroundsView"),
	fCurrent(NULL),
	fCurrentInfo(NULL),
	fLastImageIndex(-1),
	fPathList(1, true),
	fImageList(1, true),
	fFoundPositionSetting(false)
{
	SetBorder(B_NO_BORDER);

	fPreview = new BBox("preview");
	fPreview->SetLabel(B_TRANSLATE("Preview"));

	fPreView = new PreView();

	fTopLeft = new FramePart(FRAME_TOP_LEFT);
	fTop = new FramePart(FRAME_TOP);
	fTopRight = new FramePart(FRAME_TOP_RIGHT);
	fLeft = new FramePart(FRAME_LEFT_SIDE);
	fRight = new FramePart(FRAME_RIGHT_SIDE);
	fBottomLeft = new FramePart(FRAME_BOTTOM_LEFT);
	fBottom = new FramePart(FRAME_BOTTOM);
	fBottomRight = new FramePart(FRAME_BOTTOM_RIGHT);

	fXPlacementText = new BTextControl(B_TRANSLATE("X:"), NULL,
		new BMessage(kMsgImagePlacement));
	fYPlacementText = new BTextControl(B_TRANSLATE("Y:"), NULL,
		new BMessage(kMsgImagePlacement));

	fXPlacementText->TextView()->SetMaxBytes(5);
	fYPlacementText->TextView()->SetMaxBytes(5);

	for (int32 i = 0; i < 256; i++) {
		if ((i < '0' || i > '9') && i != '-') {
			fXPlacementText->TextView()->DisallowChar(i);
			fYPlacementText->TextView()->DisallowChar(i);
		}
	}

	BView* view = BLayoutBuilder::Group<>()
		.AddGlue()
		.AddGroup(B_VERTICAL, 20)
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.AddGrid(0, 0)
					.Add(fTopLeft, 0, 0)
					.Add(fTop, 1, 0)
					.Add(fTopRight, 2, 0)
					.Add(fLeft, 0, 1)
					.Add(fPreView, 1, 1)
					.Add(fRight, 2, 1)
					.Add(fBottomLeft, 0, 2)
					.Add(fBottom, 1, 2)
					.Add(fBottomRight, 2, 2)
					.End()
				.AddGlue()
				.End()
			.AddGroup(B_HORIZONTAL, 10)
				.Add(fXPlacementText)
				.Add(fYPlacementText)
				.End()
			.AddGlue()
			.SetInsets(10, 10, 10, 10)
			.End()
		.AddGlue()
		.View();

	fPreview->AddChild(view);

	BBox* rightbox = new BBox("rightbox");

	fWorkspaceMenu = new BPopUpMenu(B_TRANSLATE("pick one"));
	fWorkspaceMenu->AddItem(new BMenuItem(B_TRANSLATE("All workspaces"),
		new BMessage(kMsgAllWorkspaces)));
	BMenuItem* menuItem;
	fWorkspaceMenu->AddItem(menuItem = new BMenuItem(
		B_TRANSLATE("Current workspace"),
		new BMessage(kMsgCurrentWorkspace)));
	menuItem->SetMarked(true);
	fLastWorkspaceIndex =
		fWorkspaceMenu->IndexOf(fWorkspaceMenu->FindMarked());
	fWorkspaceMenu->AddSeparatorItem();
	fWorkspaceMenu->AddItem(new BMenuItem(B_TRANSLATE("Default folder"),
		new BMessage(kMsgDefaultFolder)));
	fWorkspaceMenu->AddItem(new BMenuItem(
		B_TRANSLATE("Other folder" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOtherFolder)));

	BMenuField* workspaceMenuField = new BMenuField(BRect(0, 0, 130, 18),
		"workspaceMenuField", NULL, fWorkspaceMenu, true);
	workspaceMenuField->ResizeToPreferred();
	rightbox->SetLabel(workspaceMenuField);

	fImageMenu = new BPopUpMenu(B_TRANSLATE("pick one"));
	fImageMenu->AddItem(new BGImageMenuItem(B_TRANSLATE("None"), -1,
		new BMessage(kMsgNoImage)));
	fImageMenu->AddSeparatorItem();
	fImageMenu->AddItem(new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOtherImage)));

	BMenuField* imageMenuField = new BMenuField(NULL, fImageMenu);
	imageMenuField->SetAlignment(B_ALIGN_RIGHT);
	imageMenuField->ResizeToPreferred();

	fPlacementMenu = new BPopUpMenu(B_TRANSLATE("pick one"));
	fPlacementMenu->AddItem(new BMenuItem(B_TRANSLATE("Manual"),
		new BMessage(kMsgManualPlacement)));
	fPlacementMenu->AddItem(new BMenuItem(B_TRANSLATE("Center"),
		new BMessage(kMsgCenterPlacement)));
	fPlacementMenu->AddItem(new BMenuItem(B_TRANSLATE("Scale to fit"),
		new BMessage(kMsgScalePlacement)));
	fPlacementMenu->AddItem(new BMenuItem(B_TRANSLATE("Tile"),
		new BMessage(kMsgTilePlacement)));

	BMenuField* placementMenuField = new BMenuField(NULL, fPlacementMenu);
	placementMenuField->SetAlignment(B_ALIGN_RIGHT);

	fIconLabelOutline = new BCheckBox(B_TRANSLATE("Icon label outline"),
		new BMessage(kMsgIconLabelOutline));
	fIconLabelOutline->SetValue(B_CONTROL_OFF);

	fPicker = new BColorControl(BPoint(0, 0), B_CELLS_32x8, 7.0, "Picker",
		new BMessage(kMsgUpdateColor));

	BStringView* imageStringView =
		new BStringView(NULL, B_TRANSLATE("Image:"));
	BStringView* placementStringView =
		new BStringView(NULL, B_TRANSLATE("Placement:"));

	imageStringView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_NO_VERTICAL));
	placementStringView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_NO_VERTICAL));

	view = BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, 10)
			.AddGroup(B_VERTICAL, 10)
				.AddGrid(10, 10)
					.Add(imageStringView, 0, 0)
					.Add(placementStringView, 0, 1)
					.Add(imageMenuField, 1, 0)
					.Add(placementMenuField, 1, 1)
					.End()
				.Add(fIconLabelOutline)
				.End()
			.Add(fPicker)
			.SetInsets(10, 10, 10, 10)
			.End()
		.View();

	rightbox->AddChild(view);

	fRevert = new BButton(B_TRANSLATE("Revert"),
		new BMessage(kMsgRevertSettings));
	fApply = new BButton(B_TRANSLATE("Apply"),
		new BMessage(kMsgApplySettings));

	fRevert->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_NO_VERTICAL));
	fApply->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_NO_VERTICAL));

	view = BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, 10)
			.AddGroup(B_HORIZONTAL, 10)
				.Add(fPreview)
				.Add(rightbox)
				.End()
			.AddGroup(B_HORIZONTAL, 0)
				.Add(fRevert)
				.Add(fApply)
				.End()
			.SetInsets(10, 10, 10, 10)
			.End()
		.View();

	AddChild(view);

	fApply->MakeDefault(true);
}


BackgroundsView::~BackgroundsView()
{
	delete fPanel;
	delete fFolderPanel;
}


void
BackgroundsView::AllAttached()
{
	fPlacementMenu->SetTargetForItems(this);
	fImageMenu->SetTargetForItems(this);
	fWorkspaceMenu->SetTargetForItems(this);
	fXPlacementText->SetTarget(this);
	fYPlacementText->SetTarget(this);
	fIconLabelOutline->SetTarget(this);
	fPicker->SetTarget(this);
	fApply->SetTarget(this);
	fRevert->SetTarget(this);

	BPath path;
	entry_ref ref;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &path) == B_OK) {
		path.Append("artwork");
		get_ref_for_path(path.Path(), &ref);
	}

	BMessenger messenger(this);
	fPanel = new ImageFilePanel(B_OPEN_PANEL, &messenger, &ref,
		B_FILE_NODE, false, NULL, new CustomRefFilter(true));
	fPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Select"));

	fFolderPanel = new BFilePanel(B_OPEN_PANEL, &messenger, NULL,
		B_DIRECTORY_NODE, false, NULL, new CustomRefFilter(false));
	fFolderPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Select"));

	_LoadSettings();
	_LoadDesktopFolder();

	BPoint point;
	if (fSettings.FindPoint("pos", &point) == B_OK) {
		fFoundPositionSetting = true;
		Window()->MoveTo(point);
	}

	fApply->SetEnabled(false);
	fRevert->SetEnabled(false);
}


void
BackgroundsView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
			RefsReceived(msg);
			break;

		case kMsgUpdatePreviewPlacement:
		{
			BString xstring, ystring;
			xstring << (int)fPreView->fPoint.x;
			ystring << (int)fPreView->fPoint.y;
			fXPlacementText->SetText(xstring.String());
			fYPlacementText->SetText(ystring.String());
			_UpdatePreview();
			_UpdateButtons();
			break;
		}

		case kMsgManualPlacement:
			_UpdatePreview();
			_UpdateButtons();
			break;

		case kMsgTilePlacement:
		case kMsgScalePlacement:
		case kMsgCenterPlacement:
			_UpdatePreview();
			_UpdateButtons();
			break;

		case kMsgIconLabelOutline:
			_UpdateButtons();
			break;

		case kMsgUpdateColor:
		case kMsgImagePlacement:
			_UpdatePreview();
			_UpdateButtons();
			break;

		case kMsgCurrentWorkspace:
		case kMsgAllWorkspaces:
			fImageMenu->FindItem(kMsgNoImage)->SetLabel(B_TRANSLATE("None"));
			fLastWorkspaceIndex = fWorkspaceMenu->IndexOf(
				fWorkspaceMenu->FindMarked());
			if (fCurrent && fCurrent->IsDesktop()) {
				_UpdateButtons();
			} else {
				_SetDesktop(true);
				_LoadDesktopFolder();
			}
			break;

		case kMsgDefaultFolder:
			fImageMenu->FindItem(kMsgNoImage)->SetLabel(B_TRANSLATE("None"));
			fLastWorkspaceIndex = fWorkspaceMenu->IndexOf(
				fWorkspaceMenu->FindMarked());
			_SetDesktop(false);
			_LoadDefaultFolder();
			break;

		case kMsgOtherFolder:
			fFolderPanel->Show();
			break;

		case kMsgOtherImage:
			fPanel->Show();
			break;

		case B_CANCEL:
		{
			PRINT(("cancel received\n"));
			void* pointer;
			msg->FindPointer("source", &pointer);
			if (pointer == fPanel) {
				if (fLastImageIndex >= 0)
					_FindImageItem(fLastImageIndex)->SetMarked(true);
				else
					fImageMenu->ItemAt(0)->SetMarked(true);
			} else if (pointer == fFolderPanel) {
				if (fLastWorkspaceIndex >= 0)
					fWorkspaceMenu->ItemAt(fLastWorkspaceIndex)
						->SetMarked(true);
			}
			break;
		}

		case kMsgImageSelected:
		case kMsgNoImage:
			fLastImageIndex = ((BGImageMenuItem*)fImageMenu->FindMarked())
				->ImageIndex();
			_UpdatePreview();
			_UpdateButtons();
			break;

		case kMsgFolderSelected:
			fImageMenu->FindItem(kMsgNoImage)->SetLabel(B_TRANSLATE("Default"));
			fLastWorkspaceIndex = fWorkspaceMenu->IndexOf(
				fWorkspaceMenu->FindMarked());
			_SetDesktop(false);

			_LoadRecentFolder(*fPathList.ItemAt(fWorkspaceMenu->IndexOf(
				fWorkspaceMenu->FindMarked()) - 6));
			break;

		case kMsgApplySettings:
		{
			_Save();

			//_NotifyServer();
			thread_id notify_thread;
			notify_thread = spawn_thread(BackgroundsView::_NotifyThread,
				"notifyServer", B_NORMAL_PRIORITY, this);
			resume_thread(notify_thread);
			_UpdateButtons();
			break;
		}
		case kMsgRevertSettings:
			_UpdateWithCurrent();
			break;

		default:
			BView::MessageReceived(msg);
			break;
	}
}


void
BackgroundsView::_LoadDesktopFolder()
{
	BPath path;
	if (find_directory(B_DESKTOP_DIRECTORY, &path) == B_OK) {
		status_t err;
		err = get_ref_for_path(path.Path(), &fCurrentRef);
		if (err != B_OK)
			printf("error in LoadDesktopSettings\n");
		_LoadFolder(true);
	}
}


void
BackgroundsView::_LoadDefaultFolder()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		BString pathString = path.Path();
		pathString << "/Tracker/DefaultFolderTemplate";
		status_t err;
		err = get_ref_for_path(pathString.String(), &fCurrentRef);
		if (err != B_OK)
			printf("error in LoadDefaultFolderSettings\n");
		_LoadFolder(false);
	}
}


void
BackgroundsView::_LoadRecentFolder(BPath path)
{
	status_t err;
	err = get_ref_for_path(path.Path(), &fCurrentRef);
	if (err != B_OK)
		printf("error in LoadRecentFolder\n");
	_LoadFolder(false);
}


void
BackgroundsView::_LoadFolder(bool isDesktop)
{
	if (fCurrent) {
		delete fCurrent;
		fCurrent = NULL;
	}

	BNode node(&fCurrentRef);
	if (node.InitCheck() == B_OK)
		fCurrent = BackgroundImage::GetBackgroundImage(&node, isDesktop, this);

	_UpdateWithCurrent();
}


void
BackgroundsView::_UpdateWithCurrent(void)
{
	if (fCurrent == NULL)
		return;

	fPlacementMenu->FindItem(kMsgScalePlacement)
		->SetEnabled(fCurrent->IsDesktop());
	fPlacementMenu->FindItem(kMsgCenterPlacement)
		->SetEnabled(fCurrent->IsDesktop());

	if (fWorkspaceMenu->IndexOf(fWorkspaceMenu->FindMarked()) > 5)
		fImageMenu->FindItem(kMsgNoImage)->SetLabel(B_TRANSLATE("Default"));
	else
		fImageMenu->FindItem(kMsgNoImage)->SetLabel(B_TRANSLATE("None"));

	for (int32 i = fImageMenu->CountItems() - 5; i >= 0; i--) {
		fImageMenu->RemoveItem(2);
	}

	for (int32 i = fImageList.CountItems() - 1; i >= 0; i--) {
		BMessage* message = new BMessage(kMsgImageSelected);
		_AddItem(new BGImageMenuItem(GetImage(i)->GetName(), i, message));
	}

	fImageMenu->SetTargetForItems(this);

	fCurrentInfo = fCurrent->ImageInfoForWorkspace(current_workspace());

	if (!fCurrentInfo) {
		fImageMenu->FindItem(kMsgNoImage)->SetMarked(true);
		fPlacementMenu->FindItem(kMsgManualPlacement)->SetMarked(true);
		fIconLabelOutline->SetValue(B_CONTROL_ON);
	} else {
		fIconLabelOutline->SetValue(fCurrentInfo->fTextWidgetLabelOutline
			? B_CONTROL_ON : B_CONTROL_OFF);

		fLastImageIndex = fCurrentInfo->fImageIndex;
		_FindImageItem(fLastImageIndex)->SetMarked(true);

		if (fLastImageIndex > -1) {

			BString xtext, ytext;
			int32 cmd = 0;
			switch (fCurrentInfo->fMode) {
				case BackgroundImage::kCentered:
					cmd = kMsgCenterPlacement;
					break;
				case BackgroundImage::kScaledToFit:
					cmd = kMsgScalePlacement;
					break;
				case BackgroundImage::kAtOffset:
					cmd = kMsgManualPlacement;
					xtext << (int)fCurrentInfo->fOffset.x;
					ytext << (int)fCurrentInfo->fOffset.y;
					break;
				case BackgroundImage::kTiled:
					cmd = kMsgTilePlacement;
					break;
			}

			if (cmd != 0)
				fPlacementMenu->FindItem(cmd)->SetMarked(true);

			fXPlacementText->SetText(xtext.String());
			fYPlacementText->SetText(ytext.String());
		} else {
			fPlacementMenu->FindItem(kMsgManualPlacement)->SetMarked(true);
		}
	}

	rgb_color color = {255, 255, 255, 255};
	if (fCurrent->IsDesktop()) {
		color = BScreen().DesktopColor();
		fPicker->SetEnabled(true);
	} else
		fPicker->SetEnabled(false);

	fPicker->SetValue(color);

	_UpdatePreview();
	_UpdateButtons();
}


void
BackgroundsView::_Save()
{
	bool textWidgetLabelOutline
		= fIconLabelOutline->Value() == B_CONTROL_ON;

	BackgroundImage::Mode mode = _FindPlacementMode();
	BPoint offset(atoi(fXPlacementText->Text()), atoi(fYPlacementText->Text()));

	if (!fCurrent->IsDesktop()) {
		if (fCurrentInfo == NULL) {
			fCurrentInfo = new BackgroundImage::BackgroundImageInfo(
				B_ALL_WORKSPACES, fLastImageIndex, mode, offset,
				textWidgetLabelOutline, 0, 0);
			fCurrent->Add(fCurrentInfo);
		} else {
			fCurrentInfo->fTextWidgetLabelOutline = textWidgetLabelOutline;
			fCurrentInfo->fMode = mode;
			if (fCurrentInfo->fMode == BackgroundImage::kAtOffset)
				fCurrentInfo->fOffset = offset;
			fCurrentInfo->fImageIndex = fLastImageIndex;
		}
	} else {
		uint32 workspaceMask = 1;
		int32 workspace = current_workspace();
		for (; workspace; workspace--)
			workspaceMask *= 2;

		if (fCurrentInfo != NULL) {
			if (fWorkspaceMenu->FindItem(kMsgCurrentWorkspace)->IsMarked()) {
				if (fCurrentInfo->fWorkspace & workspaceMask
					&& fCurrentInfo->fWorkspace != workspaceMask) {
					fCurrentInfo->fWorkspace = fCurrentInfo->fWorkspace
						^ workspaceMask;
					fCurrentInfo = new BackgroundImage::BackgroundImageInfo(
						workspaceMask, fLastImageIndex, mode, offset,
						textWidgetLabelOutline, fCurrentInfo->fImageSet,
						fCurrentInfo->fCacheMode);
					fCurrent->Add(fCurrentInfo);
				} else if (fCurrentInfo->fWorkspace == workspaceMask) {
					fCurrentInfo->fTextWidgetLabelOutline
						= textWidgetLabelOutline;
					fCurrentInfo->fMode = mode;
					if (fCurrentInfo->fMode == BackgroundImage::kAtOffset)
						fCurrentInfo->fOffset = offset;

					fCurrentInfo->fImageIndex = fLastImageIndex;
				}
			} else {
				fCurrent->RemoveAll();

				fCurrentInfo = new BackgroundImage::BackgroundImageInfo(
					B_ALL_WORKSPACES, fLastImageIndex, mode, offset,
					textWidgetLabelOutline, fCurrent->GetShowingImageSet(),
					fCurrentInfo->fCacheMode);
				fCurrent->Add(fCurrentInfo);
			}
		} else {
			if (fWorkspaceMenu->FindItem(kMsgCurrentWorkspace)->IsMarked()) {
				fCurrentInfo = new BackgroundImage::BackgroundImageInfo(
					workspaceMask, fLastImageIndex, mode, offset,
					textWidgetLabelOutline, fCurrent->GetShowingImageSet(), 0);
			} else {
				fCurrent->RemoveAll();
				fCurrentInfo = new BackgroundImage::BackgroundImageInfo(
					B_ALL_WORKSPACES, fLastImageIndex, mode, offset,
					textWidgetLabelOutline, fCurrent->GetShowingImageSet(), 0);
			}
			fCurrent->Add(fCurrentInfo);
		}

		if (!fWorkspaceMenu->FindItem(kMsgCurrentWorkspace)->IsMarked()) {
			for (int32 i = 0; i < count_workspaces(); i++) {
				BScreen().SetDesktopColor(fPicker->ValueAsColor(), i, true);
			}
		} else
			BScreen().SetDesktopColor(fPicker->ValueAsColor(), true);
	}

	BNode node(&fCurrentRef);

	status_t status = fCurrent->SetBackgroundImage(&node);
	if (status != B_OK) {
		// TODO: this should be a BAlert!
		printf("setting background image failed: %s\n", strerror(status));
	}
}


void
BackgroundsView::_NotifyServer()
{
	BMessenger tracker("application/x-vnd.Be-TRAK");

	if (fCurrent->IsDesktop()) {
		tracker.SendMessage(new BMessage(B_RESTORE_BACKGROUND_IMAGE));
	} else {
		int32 i = -1;
		BMessage reply;
		int32 err;
		BEntry currentEntry(&fCurrentRef);
		BPath currentPath(&currentEntry);
		bool isCustomFolder
			= !fWorkspaceMenu->FindItem(kMsgDefaultFolder)->IsMarked();

		do {
			BMessage msg(B_GET_PROPERTY);
			i++;

			// look at the "Poses" in every Tracker window
			msg.AddSpecifier("Poses");
			msg.AddSpecifier("Window", i);

			reply.MakeEmpty();
			tracker.SendMessage(&msg, &reply);

			// break out of the loop when we're at the end of
			// the windows
			if (reply.what == B_MESSAGE_NOT_UNDERSTOOD
				&& reply.FindInt32("error", &err) == B_OK
				&& err == B_BAD_INDEX)
				break;

			// don't stop for windows that don't understand
			// a request for "Poses"; they're not displaying
			// folders
			if (reply.what == B_MESSAGE_NOT_UNDERSTOOD
				&& reply.FindInt32("error", &err) == B_OK
				&& err != B_BAD_SCRIPT_SYNTAX)
				continue;

			BMessenger trackerWindow;
			if (reply.FindMessenger("result", &trackerWindow) != B_OK)
				continue;

			if (isCustomFolder) {
				// found a window with poses, ask for its path
				msg.MakeEmpty();
				msg.what = B_GET_PROPERTY;
				msg.AddSpecifier("Path");
				msg.AddSpecifier("Poses");
				msg.AddSpecifier("Window", i);

				reply.MakeEmpty();
				tracker.SendMessage(&msg, &reply);

				// go on with the next if this din't have a path
				if (reply.what == B_MESSAGE_NOT_UNDERSTOOD)
					continue;

				entry_ref ref;
				if (reply.FindRef("result", &ref) == B_OK) {
					BEntry entry(&ref);
					BPath path(&entry);

					// these are not the paths you're looking for
					if (currentPath != path)
						continue;
				}
			}

			trackerWindow.SendMessage(B_RESTORE_BACKGROUND_IMAGE);
		} while (true);
	}
}


int32
BackgroundsView::_NotifyThread(void* data)
{
	BackgroundsView* view = (BackgroundsView*)data;

	view->_NotifyServer();
	return B_OK;
}


void
BackgroundsView::SaveSettings(void)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(SETTINGS_FILE);
		BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);

		BPoint point = Window()->Frame().LeftTop();
		if (fSettings.ReplacePoint("pos", point) != B_OK)
			fSettings.AddPoint("pos", point);

		entry_ref ref;
		BEntry entry;

		fPanel->GetPanelDirectory(&ref);
		entry.SetTo(&ref);
		entry.GetPath(&path);
		if (fSettings.ReplaceString("paneldir", path.Path()) != B_OK)
			fSettings.AddString("paneldir", path.Path());

		fFolderPanel->GetPanelDirectory(&ref);
		entry.SetTo(&ref);
		entry.GetPath(&path);
		if (fSettings.ReplaceString("folderpaneldir", path.Path()) != B_OK)
			fSettings.AddString("folderpaneldir", path.Path());

		fSettings.RemoveName("recentfolder");
		for (int32 i = 0; i < fPathList.CountItems(); i++) {
			fSettings.AddString("recentfolder", fPathList.ItemAt(i)->Path());
		}

		fSettings.Flatten(&file);
	}
}


void
BackgroundsView::_LoadSettings()
{
	fSettings.MakeEmpty();

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append(SETTINGS_FILE);
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	if (fSettings.Unflatten(&file) != B_OK) {
		printf("Error unflattening settings file %s\n", path.Path());
		return;
	}

	PRINT_OBJECT(fSettings);

	BString string;
	if (fSettings.FindString("paneldir", &string) == B_OK)
		fPanel->SetPanelDirectory(string.String());

	if (fSettings.FindString("folderpaneldir", &string) == B_OK)
		fFolderPanel->SetPanelDirectory(string.String());

	int32 index = 0;
	while (fSettings.FindString("recentfolder", index, &string) == B_OK) {
		if (index == 0)
			fWorkspaceMenu->AddSeparatorItem();

		path.SetTo(string.String());
		int32 i = _AddPath(path);
		BString s;
		s << B_TRANSLATE("Folder: ") << path.Leaf();
		BMenuItem* item = new BMenuItem(s.String(),
			new BMessage(kMsgFolderSelected));
		fWorkspaceMenu->AddItem(item, -i - 1 + 6);
		index++;
	}
	fWorkspaceMenu->SetTargetForItems(this);

	PRINT(("Settings Loaded\n"));
}


void
BackgroundsView::WorkspaceActivated(uint32 oldWorkspaces, bool active)
{
	_UpdateWithCurrent();
}


void
BackgroundsView::_UpdatePreview()
{
	bool imageEnabled = !(fImageMenu->FindItem(kMsgNoImage)->IsMarked());
	if (fPlacementMenu->IsEnabled() ^ imageEnabled)
		fPlacementMenu->SetEnabled(imageEnabled);

	bool textEnabled
		= (fPlacementMenu->FindItem(kMsgManualPlacement)->IsMarked())
		&& imageEnabled;
	if (fXPlacementText->IsEnabled() ^ textEnabled)
		fXPlacementText->SetEnabled(textEnabled);
	if (fYPlacementText->IsEnabled() ^ textEnabled)
		fYPlacementText->SetEnabled(textEnabled);

	if (textEnabled && (strcmp(fXPlacementText->Text(), "") == 0)) {
		fXPlacementText->SetText("0");
		fYPlacementText->SetText("0");
	}
	if (!textEnabled) {
		fXPlacementText->SetText(NULL);
		fYPlacementText->SetText(NULL);
	}

	fXPlacementText->TextView()->MakeSelectable(textEnabled);
	fYPlacementText->TextView()->MakeSelectable(textEnabled);
	fXPlacementText->TextView()->MakeEditable(textEnabled);
	fYPlacementText->TextView()->MakeEditable(textEnabled);

	fPreView->ClearViewBitmap();

	int32 index = ((BGImageMenuItem*)fImageMenu->FindMarked())->ImageIndex();
	if (index >= 0) {
		BBitmap* bitmap = GetImage(index)->GetBitmap();
		if (bitmap) {
			BackgroundImage::BackgroundImageInfo* info
				= new BackgroundImage::BackgroundImageInfo(0, index,
					_FindPlacementMode(), BPoint(atoi(fXPlacementText->Text()),
						atoi(fYPlacementText->Text())),
					fIconLabelOutline->Value() == B_CONTROL_ON, 0, 0);
			if (info->fMode == BackgroundImage::kAtOffset) {
				fPreView->SetEnabled(true);
				fPreView->fPoint.x = atoi(fXPlacementText->Text());
				fPreView->fPoint.y = atoi(fYPlacementText->Text());
			} else
				fPreView->SetEnabled(false);

			fPreView->fImageBounds = BRect(bitmap->Bounds());
			fCurrent->Show(info, fPreView);
		}
	} else
		fPreView->SetEnabled(false);

	fPreView->SetViewColor(fPicker->ValueAsColor());
	fPreView->Invalidate();
}


BackgroundImage::Mode
BackgroundsView::_FindPlacementMode()
{
	BackgroundImage::Mode mode = BackgroundImage::kAtOffset;

	if (fPlacementMenu->FindItem(kMsgCenterPlacement)->IsMarked())
		mode = BackgroundImage::kCentered;
	if (fPlacementMenu->FindItem(kMsgScalePlacement)->IsMarked())
		mode = BackgroundImage::kScaledToFit;
	if (fPlacementMenu->FindItem(kMsgManualPlacement)->IsMarked())
		mode = BackgroundImage::kAtOffset;
	if (fPlacementMenu->FindItem(kMsgTilePlacement)->IsMarked())
		mode = BackgroundImage::kTiled;

	return mode;
}


void
BackgroundsView::_UpdateButtons()
{
	bool hasChanged = false;
	if (fPicker->IsEnabled()
		&& fPicker->ValueAsColor() != BScreen().DesktopColor()) {
		hasChanged = true;
	} else if (fCurrentInfo) {
		if ((fIconLabelOutline->Value() == B_CONTROL_ON)
			^ fCurrentInfo->fTextWidgetLabelOutline) {
			hasChanged = true;
		} else if (_FindPlacementMode() != fCurrentInfo->fMode) {
			hasChanged = true;
		} else if (fCurrentInfo->fImageIndex
			!= ((BGImageMenuItem*)fImageMenu->FindMarked())->ImageIndex()) {
			hasChanged = true;
		} else if (fCurrent->IsDesktop()
			&& ((fCurrentInfo->fWorkspace != B_ALL_WORKSPACES)
				^ (fWorkspaceMenu->FindItem(kMsgCurrentWorkspace)->IsMarked())))
		{
			hasChanged = true;
		} else if (fCurrentInfo->fImageIndex > -1
			&& fCurrentInfo->fMode == BackgroundImage::kAtOffset) {
			BString oldString, newString;
			oldString << (int)fCurrentInfo->fOffset.x;
			if (oldString != BString(fXPlacementText->Text())) {
				hasChanged = true;
			}
			oldString = "";
			oldString << (int)fCurrentInfo->fOffset.y;
			if (oldString != BString(fYPlacementText->Text())) {
				hasChanged = true;
			}
		}
	} else if (fImageMenu->IndexOf(fImageMenu->FindMarked()) > 0) {
		hasChanged = true;
	} else if (fIconLabelOutline->Value() == B_CONTROL_OFF) {
		hasChanged = true;
	}

	fApply->SetEnabled(hasChanged);
	fRevert->SetEnabled(hasChanged);
}


void
BackgroundsView::RefsReceived(BMessage* msg)
{
	if (!msg->HasRef("refs") && msg->HasRef("dir_ref")) {
		entry_ref dirRef;
		if (msg->FindRef("dir_ref", &dirRef) == B_OK)
			msg->AddRef("refs", &dirRef);
	}

	entry_ref ref;
	int32 i = 0;
	BMimeType imageType("image");
	BPath desktopPath;
	find_directory(B_DESKTOP_DIRECTORY, &desktopPath);

	while (msg->FindRef("refs", i++, &ref) == B_OK) {
		BPath path;
		BEntry entry(&ref, true);
		path.SetTo(&entry);
		BNode node(&entry);

		if (node.IsFile()) {
			BMimeType refType;
			BMimeType::GuessMimeType(&ref, &refType);
			if (!imageType.Contains(&refType))
				continue;

			BGImageMenuItem* item;
			int32 index = AddImage(path);
			if (index >= 0) {
				item = _FindImageItem(index);
				fLastImageIndex = index;
			} else {
				const char* name = GetImage(-index - 1)->GetName();
				item = new BGImageMenuItem(name, -index - 1,
					new BMessage(kMsgImageSelected));
				_AddItem(item);
				item->SetTarget(this);
				fLastImageIndex = -index - 1;
			}

			// An optional placement may have been sent
			int32 placement = 0;
			if (msg->FindInt32("placement", &placement) == B_OK) {
				BMenuItem* item = fPlacementMenu->FindItem(placement);
				if (item)
					item->SetMarked(true);
			}
			item->SetMarked(true);
			BMessenger(this).SendMessage(kMsgImageSelected);
		} else if (node.IsDirectory()) {
			if (desktopPath == path) {
				fWorkspaceMenu->FindItem(kMsgCurrentWorkspace)->SetMarked(true);
				BMessenger(this).SendMessage(kMsgCurrentWorkspace);
				break;
			}
			BMenuItem* item;
			int32 index = _AddPath(path);
			if (index >= 0) {
				item = fWorkspaceMenu->ItemAt(index + 6);
				fLastWorkspaceIndex = index + 6;
			} else {
				if (fWorkspaceMenu->CountItems() <= 5)
					fWorkspaceMenu->AddSeparatorItem();
				BString s;
				s << B_TRANSLATE("Folder: ") << path.Leaf();
				item = new BMenuItem(s.String(),
					new BMessage(kMsgFolderSelected));
				fWorkspaceMenu->AddItem(item, -index - 1 + 6);
				item->SetTarget(this);
				fLastWorkspaceIndex = -index - 1 + 6;
			}

			item->SetMarked(true);
			BMessenger(this).SendMessage(kMsgFolderSelected);
		}
	}
}


int32
BackgroundsView::_AddPath(BPath path)
{
	int32 count = fPathList.CountItems();
	int32 index = 0;
	for (; index < count; index++) {
		BPath* p = fPathList.ItemAt(index);
		int c = BString(p->Path()).ICompare(path.Path());
		if (c == 0)
			return index;

		if (c > 0)
			break;
	}
	fPathList.AddItem(new BPath(path), index);
	return -index - 1;
}


int32
BackgroundsView::AddImage(BPath path)
{
	int32 count = fImageList.CountItems();
	int32 index = 0;
	for (; index < count; index++) {
		Image* image = fImageList.ItemAt(index);
		if (image->GetPath() == path)
			return index;
	}

	fImageList.AddItem(new Image(path));
	return -index - 1;
}


Image*
BackgroundsView::GetImage(int32 imageIndex)
{
	return fImageList.ItemAt(imageIndex);
}


BGImageMenuItem*
BackgroundsView::_FindImageItem(const int32 imageIndex)
{
	if (imageIndex < 0)
		return (BGImageMenuItem*)fImageMenu->ItemAt(0);

	int32 count = fImageMenu->CountItems() - 2;
	int32 index = 2;
	for (; index < count; index++) {
		BGImageMenuItem* image = (BGImageMenuItem*)fImageMenu->ItemAt(index);
		if (image->ImageIndex() == imageIndex)
			return image;
	}
	return NULL;
}


bool
BackgroundsView::_AddItem(BGImageMenuItem* item)
{
	int32 count = fImageMenu->CountItems() - 2;
	int32 index = 2;
	if (count < index) {
		fImageMenu->AddItem(new BSeparatorItem(), 1);
		count = fImageMenu->CountItems() - 2;
	}

	for (; index < count; index++) {
		BGImageMenuItem* image = (BGImageMenuItem*)fImageMenu->ItemAt(index);
		int c = (BString(image->Label()).ICompare(BString(item->Label())));
		if (c > 0)
			break;
	}
	return fImageMenu->AddItem(item, index);
}


void
BackgroundsView::_SetDesktop(bool isDesktop)
{
	fTopLeft->SetDesktop(isDesktop);
	fTop->SetDesktop(isDesktop);
	fTopRight->SetDesktop(isDesktop);
	fLeft->SetDesktop(isDesktop);
	fRight->SetDesktop(isDesktop);
	fBottomLeft->SetDesktop(isDesktop);
	fBottom->SetDesktop(isDesktop);
	fBottomRight->SetDesktop(isDesktop);

	Invalidate();
}


bool
BackgroundsView::FoundPositionSetting()
{
	return fFoundPositionSetting;
}


//	#pragma mark -


PreView::PreView()
	:
	BControl("PreView", NULL, NULL, B_WILL_DRAW | B_SUBPIXEL_PRECISE),
	fMoveHandCursor(kHandCursorData)
{
	float aspectRatio = BScreen().Frame().Width() / BScreen().Frame().Height();
	float previewWidth = 120.0f;
	float previewHeight = ceil(previewWidth / aspectRatio);

	ResizeTo(previewWidth, previewHeight);
	SetExplicitMinSize(BSize(previewWidth, previewHeight));
	SetExplicitMaxSize(BSize(previewWidth, previewHeight));
}


void
PreView::AttachedToWindow()
{
	rgb_color color = ViewColor();
	BControl::AttachedToWindow();
	SetViewColor(color);
}


void
PreView::MouseDown(BPoint point)
{
	if (IsEnabled() && Bounds().Contains(point)) {
		uint32 buttons;
		GetMouse(&point, &buttons);
		if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			fOldPoint = point;
			SetTracking(true);
			BScreen().GetMode(&fMode);
			fXRatio = Bounds().Width() / fMode.virtual_width;
			fYRatio = Bounds().Height() / fMode.virtual_height;
			SetMouseEventMask(B_POINTER_EVENTS,
				B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
		}
	}
}


void
PreView::MouseUp(BPoint point)
{
	if (IsTracking())
		SetTracking(false);
}


void
PreView::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	if (IsEnabled())
		SetViewCursor(&fMoveHandCursor);
	else
		SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);

	if (IsTracking()) {
		float x, y;
		x = fPoint.x + (point.x - fOldPoint.x) / fXRatio;
		y = fPoint.y + (point.y - fOldPoint.y) / fYRatio;
		bool min, max, mustSend = false;
		min = (x > -fImageBounds.Width());
		max = (x < fMode.virtual_width);
		if (min && max) {
			fOldPoint.x = point.x;
			fPoint.x = x;
			mustSend = true;
		} else {
			if (!min && fPoint.x > -fImageBounds.Width()) {
				fPoint.x = -fImageBounds.Width();
				fOldPoint.x = point.x - (x - fPoint.x) * fXRatio;
				mustSend = true;
			}
			if (!max && fPoint.x < fMode.virtual_width) {
				fPoint.x = fMode.virtual_width;
				fOldPoint.x = point.x - (x - fPoint.x) * fXRatio;
				mustSend = true;
			}
		}

		min = (y > -fImageBounds.Height());
		max = (y < fMode.virtual_height);
		if (min && max) {
			fOldPoint.y = point.y;
			fPoint.y = y;
			mustSend = true;
		} else {
			if (!min && fPoint.y > -fImageBounds.Height()) {
				fPoint.y = -fImageBounds.Height();
				fOldPoint.y = point.y - (y - fPoint.y) * fYRatio;
				mustSend = true;
			}
			if (!max && fPoint.y < fMode.virtual_height) {
				fPoint.y = fMode.virtual_height;
				fOldPoint.y = point.y - (y - fPoint.y) * fYRatio;
				mustSend = true;
			}
		}

		if (mustSend) {
			BMessenger messenger(Parent());
			messenger.SendMessage(kMsgUpdatePreviewPlacement);
		}
	}
	BControl::MouseMoved(point, transit, message);
}


//	#pragma mark -


BGImageMenuItem::BGImageMenuItem(const char* label, int32 imageIndex,
	BMessage* message, char shortcut, uint32 modifiers)
	: BMenuItem(label, message, shortcut, modifiers),
	fImageIndex(imageIndex)
{
}


//	#pragma mark -


FramePart::FramePart(int32 part)
	:
	BView(NULL, B_WILL_DRAW | B_FRAME_EVENTS),
	fFramePart(part),
	fIsDesktop(true)
{
	_SetSizeAndAlignment();
}


void
FramePart::Draw(BRect rect)
{
	rgb_color color = HighColor();
	SetDrawingMode(B_OP_COPY);
	SetHighColor(Parent()->ViewColor());

	if (fIsDesktop) {
		switch (fFramePart) {
			case FRAME_TOP_LEFT:
				FillRect(rect);
				SetHighColor(160, 160, 160);
				FillRoundRect(BRect(0, 0, 8, 8), 3, 3);
				SetHighColor(96, 96, 96);
				StrokeRoundRect(BRect(0, 0, 8, 8), 3, 3);
				break;

			case FRAME_TOP:
				SetHighColor(160, 160, 160);
				FillRect(BRect(0, 1, rect.right, 3));
				SetHighColor(96, 96, 96);
				StrokeLine(BPoint(0, 0), BPoint(rect.right, 0));
				SetHighColor(0, 0, 0);
				StrokeLine(BPoint(0, 4), BPoint(rect.right, 4));
				break;

			case FRAME_TOP_RIGHT:
				FillRect(rect);
				SetHighColor(160, 160, 160);
				FillRoundRect(BRect(-4, 0, 4, 8), 3, 3);
				SetHighColor(96, 96, 96);
				StrokeRoundRect(BRect(-4, 0, 4, 8), 3, 3);
				break;

			case FRAME_LEFT_SIDE:
				SetHighColor(160, 160, 160);
				FillRect(BRect(1, 0, 3, rect.bottom));
				SetHighColor(96, 96, 96);
				StrokeLine(BPoint(0, 0), BPoint(0, rect.bottom));
				SetHighColor(0, 0, 0);
				StrokeLine(BPoint(4, 0), BPoint(4, rect.bottom));
				break;

			case FRAME_RIGHT_SIDE:
				SetHighColor(160, 160, 160);
				FillRect(BRect(1, 0, 3, rect.bottom));
				SetHighColor(0, 0, 0);
				StrokeLine(BPoint(0, 0), BPoint(0, rect.bottom));
				SetHighColor(96, 96, 96);
				StrokeLine(BPoint(4, 0), BPoint(4, rect.bottom));
				break;

			case FRAME_BOTTOM_LEFT:
				FillRect(rect);
				SetHighColor(160, 160, 160);
				FillRoundRect(BRect(0, -4, 8, 4), 3, 3);
				SetHighColor(96, 96, 96);
				StrokeRoundRect(BRect(0, -4, 8, 4), 3, 3);
				break;

			case FRAME_BOTTOM:
				SetHighColor(160, 160, 160);
				FillRect(BRect(0, 1, rect.right, 3));
				SetHighColor(0, 0, 0);
				StrokeLine(BPoint(0, 0), BPoint(rect.right, 0));
				SetHighColor(96, 96, 96);
				StrokeLine(BPoint(0, 4), BPoint(rect.right, 4));
				SetHighColor(228, 0, 0);
				StrokeLine(BPoint(5, 2), BPoint(7, 2));
				break;

			case FRAME_BOTTOM_RIGHT:
				FillRect(rect);
				SetHighColor(160, 160, 160);
				FillRoundRect(BRect(-4, -4, 4, 4), 3, 3);
				SetHighColor(96, 96, 96);
				StrokeRoundRect(BRect(-4, -4, 4, 4), 3, 3);
				break;

			default:
				break;
		}
	} else {
		switch (fFramePart) {
			case FRAME_TOP_LEFT:
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 0), BPoint(0, 12));
				StrokeLine(BPoint(0, 0), BPoint(4, 0));
				StrokeLine(BPoint(3, 12), BPoint(3, 12));
				SetHighColor(255, 203, 0);
				FillRect(BRect(1, 1, 3, 9));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(1, 12), BPoint(1, 10));
				StrokeLine(BPoint(2, 10), BPoint(3, 10));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(2, 12), BPoint(2, 11));
				StrokeLine(BPoint(3, 11), BPoint(3, 11));
				break;

			case FRAME_TOP:
				FillRect(BRect(54, 0, rect.right, 8));
				SetHighColor(255, 203, 0);
				FillRect(BRect(0, 1, 52, 9));
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 0), BPoint(53, 0));
				StrokeLine(BPoint(53, 1), BPoint(53, 9));
				StrokeLine(BPoint(54, 9), BPoint(rect.right, 9));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(0, 10), BPoint(rect.right, 10));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(0, 11), BPoint(rect.right, 11));
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 12), BPoint(rect.right, 12));
				break;

			case FRAME_TOP_RIGHT:
				FillRect(BRect(0, 0, 3, 8));
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 12), BPoint(0, 12));
				StrokeLine(BPoint(0, 9), BPoint(3, 9));
				StrokeLine(BPoint(3, 12), BPoint(3, 9));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(0, 10), BPoint(2, 10));
				StrokeLine(BPoint(1, 12), BPoint(1, 12));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(2, 12), BPoint(2, 12));
				StrokeLine(BPoint(0, 11), BPoint(2, 11));
				break;

			case FRAME_LEFT_SIDE:
			case FRAME_RIGHT_SIDE:
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 0), BPoint(0, rect.bottom));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(1, 0), BPoint(1, rect.bottom));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(2, 0), BPoint(2, rect.bottom));
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(3, 0), BPoint(3, rect.bottom));
				break;

			case FRAME_BOTTOM_LEFT:
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 0), BPoint(0, 3));
				StrokeLine(BPoint(0, 3), BPoint(3, 3));
				StrokeLine(BPoint(3, 0), BPoint(3, 0));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(1, 0), BPoint(1, 2));
				StrokeLine(BPoint(3, 1), BPoint(3, 1));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(2, 0), BPoint(2, 2));
				StrokeLine(BPoint(3, 2), BPoint(3, 2));
				break;

			case FRAME_BOTTOM:
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 0), BPoint(rect.right, 0));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(0, 1), BPoint(rect.right, 1));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(0, 2), BPoint(rect.right, 2));
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 3), BPoint(rect.right, 3));
				break;

			case FRAME_BOTTOM_RIGHT:
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(0, 0), BPoint(0, 0));
				SetHighColor(240, 240, 240);
				StrokeLine(BPoint(1, 0), BPoint(1, 1));
				StrokeLine(BPoint(0, 1), BPoint(0, 1));
				SetHighColor(200, 200, 200);
				StrokeLine(BPoint(2, 0), BPoint(2, 2));
				StrokeLine(BPoint(0, 2), BPoint(1, 2));
				SetHighColor(152, 152, 152);
				StrokeLine(BPoint(3, 0), BPoint(3, 3));
				StrokeLine(BPoint(0, 3), BPoint(2, 3));
				break;

			default:
				break;
		}
	}

	SetHighColor(color);
}


void
FramePart::SetDesktop(bool isDesktop)
{
	fIsDesktop = isDesktop;

	_SetSizeAndAlignment();
	Invalidate();
}


void
FramePart::_SetSizeAndAlignment()
{
	if (fIsDesktop) {
		switch (fFramePart) {
			case FRAME_TOP_LEFT:
				SetExplicitMinSize(BSize(4, 4));
				SetExplicitMaxSize(BSize(4, 4));
				break;

			case FRAME_TOP:
				SetExplicitMinSize(BSize(1, 4));
				SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 4));
				break;

			case FRAME_TOP_RIGHT:
				SetExplicitMinSize(BSize(4, 4));
				SetExplicitMaxSize(BSize(4, 4));
				break;

			case FRAME_LEFT_SIDE:
				SetExplicitMinSize(BSize(4, 1));
				SetExplicitMaxSize(BSize(4, B_SIZE_UNLIMITED));
				break;

			case FRAME_RIGHT_SIDE:
				SetExplicitMinSize(BSize(4, 1));
				SetExplicitMaxSize(BSize(4, B_SIZE_UNLIMITED));
				break;

			case FRAME_BOTTOM_LEFT:
				SetExplicitMinSize(BSize(4, 4));
				SetExplicitMaxSize(BSize(4, 4));
				break;

			case FRAME_BOTTOM:
				SetExplicitMinSize(BSize(1, 4));
				SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 4));
				break;

			case FRAME_BOTTOM_RIGHT:
				SetExplicitMaxSize(BSize(4, 4));
				SetExplicitMinSize(BSize(4, 4));
				break;

			default:
				break;
		}
	} else {
		switch (fFramePart) {
			case FRAME_TOP_LEFT:
				SetExplicitMinSize(BSize(3, 12));
				SetExplicitMaxSize(BSize(3, 12));
				break;

			case FRAME_TOP:
				SetExplicitMinSize(BSize(1, 12));
				SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 12));
				break;

			case FRAME_TOP_RIGHT:
				SetExplicitMinSize(BSize(3, 12));
				SetExplicitMaxSize(BSize(3, 12));
				break;

			case FRAME_LEFT_SIDE:
				SetExplicitMinSize(BSize(3, 1));
				SetExplicitMaxSize(BSize(3, B_SIZE_UNLIMITED));
				break;

			case FRAME_RIGHT_SIDE:
				SetExplicitMinSize(BSize(3, 1));
				SetExplicitMaxSize(BSize(3, B_SIZE_UNLIMITED));
				break;

			case FRAME_BOTTOM_LEFT:
				SetExplicitMinSize(BSize(3, 3));
				SetExplicitMaxSize(BSize(3, 3));
				break;

			case FRAME_BOTTOM:
				SetExplicitMinSize(BSize(1, 3));
				SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 3));
				break;

			case FRAME_BOTTOM_RIGHT:
				SetExplicitMaxSize(BSize(3, 3));
				SetExplicitMinSize(BSize(3, 3));
				break;

			default:
				break;
		}
	}

	switch (fFramePart) {
		case FRAME_TOP_LEFT:
			SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_BOTTOM));
			break;

		case FRAME_TOP:
			SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_BOTTOM));
			break;

		case FRAME_TOP_RIGHT:
			SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_BOTTOM));
			break;

		case FRAME_LEFT_SIDE:
			SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_MIDDLE));
			break;

		case FRAME_RIGHT_SIDE:
			SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));
			break;

		case FRAME_BOTTOM_LEFT:
			SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_TOP));
			break;

		case FRAME_BOTTOM:
			SetExplicitAlignment(BAlignment(B_ALIGN_CENTER, B_ALIGN_TOP));
			break;

		case FRAME_BOTTOM_RIGHT:
			SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
			break;

		default:
			break;
	}
}

