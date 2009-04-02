/*
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "CharacterWindow.h"

#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Button.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayoutBuilder.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <Slider.h>
#include <SplitLayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#include "CharacterView.h"
#include "UnicodeBlockView.h"


static const uint32 kMsgUnicodeBlockSelected = 'unbs';
static const uint32 kMsgCharacterChanged = 'chch';
static const uint32 kMsgFontSelected = 'fnts';
static const uint32 kMsgFontSizeChanged = 'fsch';
static const uint32 kMsgPrivateBlocks = 'prbl';
static const uint32 kMsgContainedBlocks = 'cnbl';
static const uint32 kMsgFilterChanged = 'fltr';
static const uint32 kMsgFilterEntered = 'flte';
static const uint32 kMsgClearFilter = 'clrf';

static const int32 kMinFontSize = 10;
static const int32 kMaxFontSize = 72;


class FontSizeSlider : public BSlider {
public:
	FontSizeSlider(const char* name, const char* label, BMessage* message,
			int32 min, int32 max)
		: BSlider(name, label, NULL, min, max, B_HORIZONTAL)
	{
		SetModificationMessage(message);
	}
	
protected:
	const char* UpdateText() const
	{
		snprintf(fText, sizeof(fText), "%ldpt", Value());
		return fText;
	}

private:
	mutable char	fText[32];
};

class MouseMovedFilter : public BMessageFilter {
public:
	MouseMovedFilter()
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_MOVED)
	{
	}

	bool HasMoved() const
	{
		return fMouseMoved != 0;
	}

	void ResetMoved()
	{
		fMouseMoved = 0;
	}

	virtual filter_result Filter(BMessage* message, BHandler** /*_target*/)
	{
		fMouseMoved++;
		return B_DISPATCH_MESSAGE;
	}

private:
	int32	fMouseMoved;
};

class EscapeMessageFilter : public BMessageFilter {
public:
	EscapeMessageFilter(uint32 command)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_KEY_DOWN),
		fCommand(command)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** /*_target*/)
	{
		const char* bytes;
		if (message->what != B_KEY_DOWN
			|| message->FindString("bytes", &bytes) != B_OK
			|| bytes[0] != B_ESCAPE)
			return B_DISPATCH_MESSAGE;

		Looper()->PostMessage(fCommand);
		return B_SKIP_MESSAGE;
	}

private:
	uint32	fCommand;
};


CharacterWindow::CharacterWindow()
	: BWindow(BRect(100, 100, 700, 550), "CharacterMap", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE
			| B_AUTO_UPDATE_SIZE_LIMITS)
{
	BMessage settings;
	_LoadSettings(settings);

	BRect frame;
	if (settings.FindRect("window frame", &frame) == B_OK) {
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), frame.Height());
	}

	// create GUI

	SetLayout(new BGroupLayout(B_VERTICAL));

	BMenuBar* menuBar = new BMenuBar("menu");

	fFilterControl = new BTextControl("Filter:", NULL, new BMessage(kMsgFilterEntered));
	fFilterControl->SetModificationMessage(new BMessage(kMsgFilterChanged));

	BButton* clearButton = new BButton("clear", "Clear",
		new BMessage(kMsgClearFilter));

	fUnicodeBlockView = new UnicodeBlockView("unicodeBlocks");
	fUnicodeBlockView->SetSelectionMessage(
		new BMessage(kMsgUnicodeBlockSelected));

	BScrollView* unicodeScroller = new BScrollView("unicodeScroller",
		fUnicodeBlockView, 0, false, true);

	fCharacterView = new CharacterView("characters");
	fCharacterView->SetTarget(this, kMsgCharacterChanged);

	// TODO: have a context object shared by CharacterView/UnicodeBlockView
	bool show;
	if (settings.FindBool("show private blocks", &show) == B_OK) {
		fCharacterView->ShowPrivateBlocks(show);
		fUnicodeBlockView->ShowPrivateBlocks(show);
	}
	if (settings.FindBool("show contained blocks only", &show) == B_OK) {
		fCharacterView->ShowContainedBlocksOnly(show);
		fUnicodeBlockView->ShowPrivateBlocks(show);
	}

	const char* family;
	const char* style;
	if (settings.FindString("font family", &family) == B_OK
		&& settings.FindString("font style", &style) == B_OK) {
		_SetFont(family, style);
	}

	int32 fontSize;
	if (settings.FindInt32("font size", &fontSize) == B_OK) {
		BFont font = fCharacterView->CharacterFont();
		if (fontSize < kMinFontSize)
			fontSize = kMinFontSize;
		else if (fontSize > kMaxFontSize)
			fontSize = kMaxFontSize;
		font.SetSize(fontSize);

		fCharacterView->SetCharacterFont(font);
	} else
		fontSize = (int32)fCharacterView->CharacterFont().Size();

	BScrollView* characterScroller = new BScrollView("characterScroller",
		fCharacterView, 0, false, true);

	fFontSizeSlider = new FontSizeSlider("fontSizeSlider", "Font Size:",
		new BMessage(kMsgFontSizeChanged), kMinFontSize, kMaxFontSize);
	fFontSizeSlider->SetValue(fontSize);

	fCodeView = new BStringView("code", "-");
	fCodeView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		fCodeView->PreferredSize().Height()));

	AddChild(BGroupLayoutBuilder(B_VERTICAL)
		.Add(menuBar)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)//BSplitLayoutBuilder()
			.Add(BGroupLayoutBuilder(B_VERTICAL, 10)
				.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)
					.Add(fFilterControl)
					.Add(clearButton))
				.Add(unicodeScroller))
			.Add(BGroupLayoutBuilder(B_VERTICAL, 10)
				.Add(characterScroller)
				.Add(fFontSizeSlider)
				.Add(fCodeView))
			.SetInsets(10, 10, 10, 10)));

	// Add menu

	// "File" menu
	BMenu* menu = new BMenu("File");
	BMenuItem* item;

	menu->AddItem(item = new BMenuItem("About CharacterMap" B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));

	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(this);
	item->SetTarget(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("View");
	menu->AddItem(item = new BMenuItem("Show Private Blocks",
		new BMessage(kMsgPrivateBlocks)));
	item->SetMarked(fCharacterView->IsShowingPrivateBlocks());
// TODO: this feature is not yet supported by Haiku!
#if 0
	menu->AddItem(item = new BMenuItem("Only Show Blocks Contained in Font",
		new BMessage(kMsgContainedBlocks)));
	item->SetMarked(fCharacterView->IsShowingContainedBlocksOnly());
#endif
	menuBar->AddItem(menu);

	menuBar->AddItem(_CreateFontMenu());

	AddCommonFilter(new EscapeMessageFilter(kMsgClearFilter));
	fMouseMovedFilter = new MouseMovedFilter();
	AddCommonFilter(fMouseMovedFilter);

	// TODO: why is this needed?
	fUnicodeBlockView->SetTarget(this);

	fFilterControl->MakeFocus();
}


CharacterWindow::~CharacterWindow()
{
}


void
CharacterWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_COPY:
			PostMessage(message, fCharacterView);
			break;

		case kMsgUnicodeBlockSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) != B_OK
				|| index < 0)
				break;

			BlockListItem* item
				= static_cast<BlockListItem*>(fUnicodeBlockView->ItemAt(index));
			fCharacterView->ScrollTo(item->BlockIndex());

			// Give the filter control focus if we got here by mouse action
			if (fMouseMovedFilter->HasMoved())
				fFilterControl->MakeFocus();
			break;
		}

		case kMsgCharacterChanged:
		{
			uint32 character;
			if (message->FindInt32("character", (int32*)&character) != B_OK)
				break;

			char utf8[16];
			CharacterView::UnicodeToUTF8(character, utf8, sizeof(utf8));

			char utf8Hex[32];
			CharacterView::UnicodeToUTF8Hex(character, utf8Hex,
				sizeof(utf8Hex));

			char text[128];
			snprintf(text, sizeof(text), "'%s' Code: %#lx (%ld), UTF-8: %s",
				utf8, character, character, utf8Hex);

			fCodeView->SetText(text);
			break;
		}

		case kMsgFontSelected:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void**)&item) != B_OK)
				break;

			fSelectedFontItem->SetMarked(false);

			// If it's the family menu, just select the first style
			if (item->Submenu() != NULL)
				item = item->Submenu()->ItemAt(0);

			if (item != NULL) {
				item->SetMarked(true);
				fSelectedFontItem = item;

				_SetFont(item->Menu()->Name(), item->Label());
			}	
			break;
		}

		case kMsgFontSizeChanged:
		{
			int32 size = fFontSizeSlider->Value();
			if (size < kMinFontSize)
				size = kMinFontSize;
			else if (size > kMaxFontSize)
				size = kMaxFontSize;

			BFont font = fCharacterView->CharacterFont();
			font.SetSize(size);
			fCharacterView->SetCharacterFont(font);
			break;
		}

		case kMsgPrivateBlocks:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void**)&item) != B_OK
				|| item == NULL)
				break;
	
			item->SetMarked(!item->IsMarked());

			fCharacterView->ShowPrivateBlocks(item->IsMarked());
			fUnicodeBlockView->ShowPrivateBlocks(item->IsMarked());
			break;
		}

		case kMsgContainedBlocks:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void**)&item) != B_OK
				|| item == NULL)
				break;
	
			item->SetMarked(!item->IsMarked());

			fCharacterView->ShowContainedBlocksOnly(item->IsMarked());
			fUnicodeBlockView->ShowContainedBlocksOnly(item->IsMarked());
			break;
		}

		case kMsgFilterChanged:
			fUnicodeBlockView->SetFilter(fFilterControl->Text());
			fMouseMovedFilter->ResetMoved();
			break;

		case kMsgFilterEntered:
			if (!fMouseMovedFilter->HasMoved()) {
				fUnicodeBlockView->MakeFocus();
				fUnicodeBlockView->Select(0);
			}
			break;

		case kMsgClearFilter:
			fFilterControl->SetText("");
			fFilterControl->MakeFocus();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
CharacterWindow::QuitRequested()
{
	_SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


status_t
CharacterWindow::_OpenSettings(BFile& file, uint32 mode)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;

	path.Append("CharacterMap settings");

	return file.SetTo(path.Path(), mode);
}


status_t
CharacterWindow::_LoadSettings(BMessage& settings)
{
	BFile file;
	status_t status = _OpenSettings(file, B_READ_ONLY);
	if (status < B_OK)
		return status;

	return settings.Unflatten(&file);
}


status_t
CharacterWindow::_SaveSettings()
{
	BFile file;
	status_t status = _OpenSettings(file, B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (status < B_OK)
		return status;

	BMessage settings('chrm');
	status = settings.AddRect("window frame", Frame());
	if (status != B_OK)
		return status;

	if (status == B_OK) {
		status = settings.AddBool("show private blocks",
			fCharacterView->IsShowingPrivateBlocks());
	}
	if (status == B_OK) {
		status = settings.AddBool("show contained blocks only",
			fCharacterView->IsShowingContainedBlocksOnly());
	}
	
	if (status == B_OK) {
		BFont font = fCharacterView->CharacterFont();
		status = settings.AddInt32("font size", font.Size());
		
		font_family family;
		font_style style;
		if (status == B_OK)
			font.GetFamilyAndStyle(&family, &style);
		if (status == B_OK)
			status = settings.AddString("font family", family);
		if (status == B_OK)
			status = settings.AddString("font style", style);
	}

	if (status == B_OK)
		status = settings.Flatten(&file);

	return status;
}


void
CharacterWindow::_SetFont(const char* family, const char* style)
{
	BFont font = fCharacterView->CharacterFont();
	font.SetFamilyAndStyle(family, style);

	fCharacterView->SetCharacterFont(font);
}


BMenu*
CharacterWindow::_CreateFontMenu()
{
	BMenu* menu = new BMenu("Font");

	font_family currentFamily;
	font_style currentStyle;
	fCharacterView->CharacterFont().GetFamilyAndStyle(&currentFamily,
		&currentStyle);

	int32 numFamilies = count_font_families();

	for (int32 i = 0; i < numFamilies; i++) {
		font_family family;
		if (get_font_family(i, &family) == B_OK) {
			BMenu* subMenu = new BMenu(family);
			menu->AddItem(new BMenuItem(subMenu,
				new BMessage(kMsgFontSelected)));

			int numStyles = count_font_styles(family);
			for (int32 j = 0; j < numStyles; j++) {
				font_style style;
				uint32 flags;
				if (get_font_style(family, j, &style, &flags) == B_OK) {
					BMenuItem* item = new BMenuItem(style,
						new BMessage(kMsgFontSelected));
					subMenu->AddItem(item);

					if (!strcmp(family, currentFamily)
						&& !strcmp(style, currentStyle)) {
						fSelectedFontItem = item;
						item->SetMarked(true);
					}
				}
			}
		}
	}

	return menu;
}
