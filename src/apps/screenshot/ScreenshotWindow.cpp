/*
 * Copyright 2010 Wim van der Meer <WPJvanderMeer@gmail.com>
 * Copyright Karsten Heimrich, host.haiku@gmx.de.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Karsten Heimrich
 *		Fredrik Modéen
 *		Christophe Huriaux
 *		Wim van der Meer
 */


#include "ScreenshotWindow.h"

#include <stdlib.h>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GridLayoutBuilder.h>
#include <GroupLayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>

#include "PreviewView.h"
#include "Utility.h"


enum {
	kActiveWindow,
	kIncludeBorder,
	kIncludeCursor,
	kNewScreenshot,
	kImageFormat,
	kLocationChanged,
	kChooseLocation,
	kSaveScreenshot
};


class DirectoryRefFilter : public BRefFilter {
public:
	virtual ~DirectoryRefFilter()
	{
	}

	virtual bool Filter(const entry_ref* ref, BNode* node,
		struct stat_beos* stat, const char* filetype)
	{
		return node->IsDirectory();
	}
};


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "ScreenshotWindow"


ScreenshotWindow::ScreenshotWindow(const Utility& utility, bool silent,
	bool clipboard)
	:
	BWindow(BRect(0, 0, 200.0, 100.0), B_TRANSLATE("Screenshot"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AVOID_FRONT
		| B_QUIT_ON_WINDOW_CLOSE | B_AUTO_UPDATE_SIZE_LIMITS
		| B_CLOSE_ON_ESCAPE),
	fUtility(utility),
	fDelayControl(NULL),
	fScreenshot(NULL),
	fOutputPathPanel(NULL),
	fLastSelectedPath(NULL),
	fDelay(0),
	fIncludeBorder(false),
	fIncludeCursor(false),
	fGrabActiveWindow(false),
	fOutputFilename(NULL),
	fExtension(""),
	fImageFileType(B_PNG_FORMAT)
{
	// _ReadSettings() needs a valid fOutputPathMenu
	fOutputPathMenu = new BMenu(B_TRANSLATE("Please select"));
	_ReadSettings();

	// _NewScreenshot() needs a valid fNameControl
	BString name(B_TRANSLATE(fUtility.sDefaultFileNameBase));
	name << 1;
	name = _FindValidFileName(name.String());
	fNameControl = new BTextControl("", B_TRANSLATE("Name:"), name, NULL);

	// Check if fUtility contains valid data
	if (fUtility.wholeScreen == NULL) {
		_NewScreenshot(silent, clipboard);
		return;
	}
	
	fScreenshot = fUtility.MakeScreenshot(fIncludeCursor, fGrabActiveWindow,
		fIncludeBorder);

	fActiveWindow = new BCheckBox(B_TRANSLATE("Capture active window"),
		new BMessage(kActiveWindow));
	if (fGrabActiveWindow)
		fActiveWindow->SetValue(B_CONTROL_ON);

	fWindowBorder = new BCheckBox(B_TRANSLATE("Include window border"),
		new BMessage(kIncludeBorder));
	if (fIncludeBorder)
		fWindowBorder->SetValue(B_CONTROL_ON);
	if (!fGrabActiveWindow)
		fWindowBorder->SetEnabled(false);

	fShowCursor = new BCheckBox(B_TRANSLATE("Include mouse pointer"),
		new BMessage(kIncludeCursor));
	if (fIncludeCursor)
		fShowCursor->SetValue(B_CONTROL_ON);

	BString delay;
	delay << fDelay / 1000000;
	fDelayControl = new BTextControl("", B_TRANSLATE("Delay:"), delay.String(),
		NULL);
	_DisallowChar(fDelayControl->TextView());
	fDelayControl->TextView()->SetAlignment(B_ALIGN_RIGHT);
	BStringView* seconds = new BStringView("", B_TRANSLATE("seconds"));
	seconds->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BMenuField* menuField2 = new BMenuField(B_TRANSLATE("Save in:"),
		fOutputPathMenu);

	fTranslatorMenu = new BMenu(B_TRANSLATE("Please select"));
	_SetupTranslatorMenu();
	BMenuField* menuField = new BMenuField(B_TRANSLATE("Save as:"),
		fTranslatorMenu);

	BBox* divider = new BBox(B_FANCY_BORDER, NULL);
	divider->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 1));

	BButton* saveScreenshot  = new BButton("", B_TRANSLATE("Save"),
		new BMessage(kSaveScreenshot));

	fPreview = new PreviewView();

	BGridLayout* gridLayout = BGridLayoutBuilder(0.0, 5.0)
		.Add(fNameControl->CreateLabelLayoutItem(), 0, 0)
		.Add(fNameControl->CreateTextViewLayoutItem(), 1, 0)
		.Add(menuField->CreateLabelLayoutItem(), 0, 1)
		.Add(menuField->CreateMenuBarLayoutItem(), 1, 1)
		.Add(menuField2->CreateLabelLayoutItem(), 0, 2)
		.Add(menuField2->CreateMenuBarLayoutItem(), 1, 2);
	gridLayout->SetMinColumnWidth(1,
		menuField->StringWidth("SomethingLongHere"));

	SetLayout(new BGroupLayout(B_HORIZONTAL));

	AddChild(BGroupLayoutBuilder(B_VERTICAL)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10.0)
			.Add(fPreview)
			.AddGroup(B_VERTICAL)
				.Add(fActiveWindow)
				.Add(fWindowBorder)
				.Add(fShowCursor)
				.AddGroup(B_HORIZONTAL, 5.0)
					.Add(fDelayControl->CreateLabelLayoutItem())
					.Add(fDelayControl->CreateTextViewLayoutItem())
					.Add(seconds)
					.End()
				.AddStrut(10.0)
				.Add(gridLayout->View())
				.AddGlue()
				.End())
		.AddStrut(10)
		.Add(divider)
		.AddStrut(10)
		.AddGroup(B_HORIZONTAL, 10.0)
			.Add(new BButton("", B_TRANSLATE("Copy to clipboard"),
				new BMessage(B_COPY)))
			.Add(new BButton("", B_TRANSLATE("New screenshot"),
				new BMessage(kNewScreenshot)))
			.AddGlue()
			.Add(saveScreenshot)
			.End()
		.SetInsets(10.0, 10.0, 10.0, 10.0)
	);

	saveScreenshot->MakeDefault(true);

	_UpdatePreviewPanel();
	_UpdateFilenameSelection();

	CenterOnScreen();
	Show();
}


ScreenshotWindow::~ScreenshotWindow()
{
	if (fOutputPathPanel)
		delete fOutputPathPanel->RefFilter();

	delete fOutputPathPanel;
	delete fScreenshot;
}


void
ScreenshotWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kActiveWindow:
			fGrabActiveWindow = false;
			if (fActiveWindow->Value() == B_CONTROL_ON)
				fGrabActiveWindow = true;

			fWindowBorder->SetEnabled(fGrabActiveWindow);

			delete fScreenshot;
			fScreenshot = fUtility.MakeScreenshot(fIncludeCursor,
				fGrabActiveWindow, fIncludeBorder);
			_UpdatePreviewPanel();
			break;

		case kIncludeBorder:
			fIncludeBorder = (fWindowBorder->Value() == B_CONTROL_ON);
			delete fScreenshot;
			fScreenshot = fUtility.MakeScreenshot(fIncludeCursor,
				fGrabActiveWindow, fIncludeBorder);
			_UpdatePreviewPanel();
			break;

		case kIncludeCursor:
			fIncludeCursor = (fShowCursor->Value() == B_CONTROL_ON);
			delete fScreenshot;
			fScreenshot = fUtility.MakeScreenshot(fIncludeCursor,
				fGrabActiveWindow, fIncludeBorder);
			_UpdatePreviewPanel();
			break;

		case kNewScreenshot:
			fDelay = (atoi(fDelayControl->Text()) * 1000000) + 50000;
			_NewScreenshot();
			break;

		case kImageFormat:
			message->FindInt32("be:type", &fImageFileType);
			fNameControl->SetText(_FindValidFileName(
				fNameControl->Text()).String());
			_UpdateFilenameSelection();
			break;

		case kLocationChanged:
		{
			void* source = NULL;
			if (message->FindPointer("source", &source) == B_OK)
				fLastSelectedPath = static_cast<BMenuItem*> (source);

			fNameControl->SetText(_FindValidFileName(
				fNameControl->Text()).String());

			_UpdateFilenameSelection();
			break;
		}

		case kChooseLocation:
		{
			if (!fOutputPathPanel) {
				BMessenger target(this);
				fOutputPathPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
					B_DIRECTORY_NODE, false, NULL, new DirectoryRefFilter());
				fOutputPathPanel->Window()->SetTitle(
					B_TRANSLATE("Choose folder"));
				fOutputPathPanel->SetButtonLabel(B_DEFAULT_BUTTON,
					B_TRANSLATE("Select"));
				fOutputPathPanel->SetButtonLabel(B_CANCEL_BUTTON,
					B_TRANSLATE("Cancel"));
			}
			fOutputPathPanel->Show();
			break;
		}

		case B_CANCEL:
			fLastSelectedPath->SetMarked(true);
			break;

		case kSaveScreenshot:
			if (_SaveScreenshot() == B_OK)
				be_app->PostMessage(B_QUIT_REQUESTED);
			break;

		case B_COPY:
			fUtility.CopyToClipboard(fScreenshot);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ScreenshotWindow::Quit()
{
	if (fUtility.wholeScreen != NULL)
		_WriteSettings();
	BWindow::Quit();
}


void
ScreenshotWindow::_NewScreenshot(bool silent, bool clipboard)
{
	BMessage message(B_ARGV_RECEIVED);
	int32 argc = 3;
	BString delay;
	delay << fDelay / 1000000;
	message.AddString("argv", "screenshot");
	message.AddString("argv", "--delay");
	message.AddString("argv", delay);
	
	if (silent || clipboard) {
		if (silent) {
			argc++;
			message.AddString("argv", "--silent");
		}
		if (clipboard) {
			argc++;
			message.AddString("argv", "--clipboard");
		}
		if (fIncludeBorder) {
			argc++;
			message.AddString("argv", "--border");
		}
		if (fIncludeCursor) {
			argc++;
			message.AddString("argv", "--mouse-pointer");
		}
		if (fGrabActiveWindow) {
			argc++;
			message.AddString("argv", "--window");
		}
		if (fLastSelectedPath) {
			BPath path(_GetDirectory());
			if (path != NULL) {
				path.Append(fNameControl->Text());
				argc++;
				message.AddString("argv", path.Path());
			}
		}
	}
	message.AddInt32("argc", argc);

	be_roster->Launch("application/x-vnd.haiku-screenshot-cli", &message);
	be_app->PostMessage(B_QUIT_REQUESTED);
}


void
ScreenshotWindow::_UpdatePreviewPanel()
{
	float height = 150.0f;

	float width = (fScreenshot->Bounds().Width() 
		/ fScreenshot->Bounds().Height()) * height;

	// to prevent a preview way too wide
	if (width > 400.0f) {
		width = 400.0f;
		height = (fScreenshot->Bounds().Height()
			/ fScreenshot->Bounds().Width()) * width;
	}

	fPreview->SetExplicitMinSize(BSize(width, height));

	fPreview->ClearViewBitmap();
	fPreview->SetViewBitmap(fScreenshot, fScreenshot->Bounds(),
		fPreview->Bounds(), B_FOLLOW_ALL, B_FILTER_BITMAP_BILINEAR);
}


void
ScreenshotWindow::_DisallowChar(BTextView* textView)
{
	for (uint32 i = 0; i < '0'; ++i)
		textView->DisallowChar(i);

	for (uint32 i = '9' + 1; i < 255; ++i)
		textView->DisallowChar(i);
}


void
ScreenshotWindow::_SetupOutputPathMenu(const BMessage& settings)
{
	fOutputPathMenu->SetLabelFromMarked(true);

	BString lastSelectedPath;
	settings.FindString("lastSelectedPath", &lastSelectedPath);

	BPath path;
	find_directory(B_USER_DIRECTORY, &path);

	BString label(B_TRANSLATE("Home folder"));
	_AddItemToPathMenu(path.Path(), label, 0,
		(path.Path() == lastSelectedPath));

	path.Append("Desktop");
	label.SetTo(B_TRANSLATE("Desktop"));
	_AddItemToPathMenu(path.Path(), label, 0, (
		path.Path() == lastSelectedPath));

	find_directory(B_BEOS_ETC_DIRECTORY, &path);
	path.Append("artwork");

	label.SetTo(B_TRANSLATE("Artwork folder"));
	_AddItemToPathMenu(path.Path(), label, 2,
		(path.Path() == lastSelectedPath));

	int32 i = 0;
	BString userPath;
	while (settings.FindString("path", ++i, &userPath) == B_OK) {
		_AddItemToPathMenu(userPath.String(), userPath, 3,
			(userPath == lastSelectedPath));
	}

	if (!fLastSelectedPath) {
		if (settings.IsEmpty() || lastSelectedPath.Length() == 0) {
			fOutputPathMenu->ItemAt(1)->SetMarked(true);
			fLastSelectedPath = fOutputPathMenu->ItemAt(1);
		} else
			_AddItemToPathMenu(lastSelectedPath.String(), lastSelectedPath, 3,
				true);
	}

	fOutputPathMenu->AddItem(new BSeparatorItem());
	fOutputPathMenu->AddItem(new BMenuItem(B_TRANSLATE("Choose folder..."),
		new BMessage(kChooseLocation)));
}


void
ScreenshotWindow::_AddItemToPathMenu(const char* path, BString& label,
	int32 index, bool markItem)
{
	BMessage* message = new BMessage(kLocationChanged);
	message->AddString("path", path);

	fOutputPathMenu->TruncateString(&label, B_TRUNCATE_MIDDLE,
		fOutputPathMenu->StringWidth("SomethingLongHere"));

	fOutputPathMenu->AddItem(new BMenuItem(label.String(), message), index);

	if (markItem) {
		fOutputPathMenu->ItemAt(index)->SetMarked(true);
		fLastSelectedPath = fOutputPathMenu->ItemAt(index);
	}
}


void
ScreenshotWindow::_UpdateFilenameSelection()
{
	fNameControl->MakeFocus(true);
	fNameControl->TextView()->Select(0,	fNameControl->TextView()->TextLength()
		- fExtension.Length());

	fNameControl->TextView()->ScrollToSelection();
}


void
ScreenshotWindow::_SetupTranslatorMenu()
{
	BMessage message(kImageFormat);
	fTranslatorMenu = new BMenu("Please select");
	BTranslationUtils::AddTranslationItems(fTranslatorMenu, B_TRANSLATOR_BITMAP,
		&message, NULL, NULL, NULL);

	fTranslatorMenu->SetLabelFromMarked(true);

	if (fTranslatorMenu->ItemAt(0))
		fTranslatorMenu->ItemAt(0)->SetMarked(true);

	int32 imageFileType;
	for (int32 i = 0; i < fTranslatorMenu->CountItems(); ++i) {
		BMenuItem* item = fTranslatorMenu->ItemAt(i);
		if (item && item->Message()) {
			item->Message()->FindInt32("be:type", &imageFileType);
			if (fImageFileType == imageFileType) {
				item->SetMarked(true);
				MessageReceived(item->Message());
				break;
			}
		}
	}
}


status_t
ScreenshotWindow::_SaveScreenshot()
{
	if (!fScreenshot || !fLastSelectedPath)
		return B_ERROR;

	BPath path(_GetDirectory());

	if (path == NULL)
		return B_ERROR;

	path.Append(fNameControl->Text());

	BEntry entry;
	entry.SetTo(path.Path());

	if (entry.Exists()) {
		BAlert* overwriteAlert = new BAlert(
			B_TRANSLATE("overwrite"),
			B_TRANSLATE("This file already exists.\n Are you sure would "
				"you like to overwrite it?"),
			B_TRANSLATE("Cancel"),
			B_TRANSLATE("Overwrite"),
			NULL, B_WIDTH_AS_USUAL, B_EVEN_SPACING, B_WARNING_ALERT);

			overwriteAlert->SetShortcut(0, B_ESCAPE);

			if (overwriteAlert->Go() == 0)
				return B_CANCELED;
	}

	return fUtility.Save(&fScreenshot, path.Path(), fImageFileType);
}


BString
ScreenshotWindow::_FindValidFileName(const char* name)
{
	BString baseName(name);

	if (fExtension.Compare(""))
		baseName.RemoveLast(fExtension);

	if (!fLastSelectedPath)
		return baseName;

	BPath orgPath(_GetDirectory());
	if (orgPath == NULL)
		return baseName;

	fExtension = BString(fUtility.GetFileNameExtension(fImageFileType));

	BPath outputPath = orgPath;
	BString fileName;
	fileName << baseName << fExtension;
	outputPath.Append(fileName);

	if (!BEntry(outputPath.Path()).Exists())
		return fileName;

	if (baseName.FindFirst(B_TRANSLATE(fUtility.sDefaultFileNameBase)) == 0)
		baseName.SetTo(fUtility.sDefaultFileNameBase);

	BEntry entry;
	int32 index = 1;

	do {
		fileName = "";
		fileName << baseName << index++ << fExtension;
		outputPath.SetTo(orgPath.Path());
		outputPath.Append(fileName);
		entry.SetTo(outputPath.Path());
	} while (entry.Exists());

	return fileName;
}


BPath
ScreenshotWindow::_GetDirectory()
{
	BPath path;

	BMessage* message = fLastSelectedPath->Message();
	const char* stringPath;
	if (message && message->FindString("path", &stringPath) == B_OK)
		path.SetTo(stringPath);

	return path;
}


void
ScreenshotWindow::_ReadSettings()
{
	BMessage settings;

	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
		return;

	settingsPath.Append("Screenshot_settings");

	BFile file(settingsPath.Path(), B_READ_ONLY);
	if (file.InitCheck() == B_OK)
		settings.Unflatten(&file);

	if (settings.FindInt32("type", &fImageFileType) != B_OK)
		fImageFileType = B_PNG_FORMAT;
	settings.FindBool("includeBorder", &fIncludeBorder);
	settings.FindBool("includeCursor", &fIncludeCursor);
	settings.FindBool("grabActiveWindow", &fGrabActiveWindow);
	settings.FindInt64("delay", &fDelay);
	settings.FindString("outputFilename", &fOutputFilename);

	_SetupOutputPathMenu(settings);
}


void
ScreenshotWindow::_WriteSettings()
{
	if (fDelayControl)
		fDelay = (atoi(fDelayControl->Text()) * 1000000) + 50000;

	BMessage settings;

	settings.AddInt32("type", fImageFileType);
	settings.AddBool("includeBorder", fIncludeBorder);
	settings.AddBool("includeCursor", fIncludeCursor);
	settings.AddBool("grabActiveWindow", fGrabActiveWindow);
	settings.AddInt64("delay", fDelay);
	settings.AddString("outputFilename", fOutputFilename);

	BString path;
	int32 count = fOutputPathMenu->CountItems();
	if (count > 5) {
		for (int32 i = count - 3; i > count - 8 && i > 2; --i) {
			BMenuItem* item = fOutputPathMenu->ItemAt(i);
			if (item) {
				BMessage* msg = item->Message();
				if (msg && msg->FindString("path", &path) == B_OK)
					settings.AddString("path", path.String());
			}
		}
	}

	if (fLastSelectedPath) {
		BMessage* msg = fLastSelectedPath->Message();
		if (msg && msg->FindString("path", &path) == B_OK)
			settings.AddString("lastSelectedPath", path.String());
	}

	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
		return;
	settingsPath.Append("Screenshot_settings");

	BFile file(settingsPath.Path(), B_CREATE_FILE | B_ERASE_FILE
		| B_WRITE_ONLY);
	if (file.InitCheck() == B_OK) {
		ssize_t size;
		settings.Flatten(&file, &size);
	}
}
