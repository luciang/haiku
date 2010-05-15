/*
 * Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "AttributeListView.h"
#include "AttributeWindow.h"
#include "DropTargetListView.h"
#include "ExtensionWindow.h"
#include "FileTypes.h"
#include "FileTypesWindow.h"
#include "IconView.h"
#include "MimeTypeListView.h"
#include "NewFileTypeWindow.h"
#include "PreferredAppMenu.h"
#include "StringView.h"

#include <Alignment.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <GridLayoutBuilder.h>
#include <GroupLayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <SplitView.h>
#include <TextControl.h>

#include <OverrideAlert.h>
#include <be_apps/Tracker/RecentItems.h>

#include <stdio.h>
#include <stdlib.h>


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "FileTypes Window"


const uint32 kMsgTypeSelected = 'typs';
const uint32 kMsgAddType = 'atyp';
const uint32 kMsgRemoveType = 'rtyp';

const uint32 kMsgExtensionSelected = 'exts';
const uint32 kMsgExtensionInvoked = 'exti';
const uint32 kMsgAddExtension = 'aext';
const uint32 kMsgRemoveExtension = 'rext';
const uint32 kMsgRuleEntered = 'rule';

const uint32 kMsgAttributeSelected = 'atrs';
const uint32 kMsgAttributeInvoked = 'atri';
const uint32 kMsgAddAttribute = 'aatr';
const uint32 kMsgRemoveAttribute = 'ratr';

const uint32 kMsgPreferredAppChosen = 'papc';
const uint32 kMsgSelectPreferredApp = 'slpa';
const uint32 kMsgSamePreferredAppAs = 'spaa';

const uint32 kMsgPreferredAppOpened = 'paOp';
const uint32 kMsgSamePreferredAppAsOpened = 'spaO';

const uint32 kMsgTypeEntered = 'type';
const uint32 kMsgDescriptionEntered = 'dsce';

const uint32 kMsgToggleIcons = 'tgic';
const uint32 kMsgToggleRule = 'tgrl';


class TypeIconView : public IconView {
	public:
		TypeIconView(const char* name);
		virtual ~TypeIconView();

		virtual void Draw(BRect updateRect);
		virtual void GetPreferredSize(float* _width, float* _height);

	protected:
		virtual BRect BitmapRect() const;
};


class ExtensionListView : public DropTargetListView {
	public:
		ExtensionListView(const char* name,
			list_view_type type = B_SINGLE_SELECTION_LIST,
			uint32 flags = B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
		virtual ~ExtensionListView();

		virtual void MessageReceived(BMessage* message);
		virtual bool AcceptsDrag(const BMessage* message);

		void SetType(BMimeType* type);

	private:
		BMimeType	fType;
};


//	#pragma mark -


TypeIconView::TypeIconView(const char* name)
	: IconView(name)
{
	ShowEmptyFrame(false);
}


TypeIconView::~TypeIconView()
{
}


void
TypeIconView::Draw(BRect updateRect)
{
	if (!IsEnabled())
		return;

	IconView::Draw(updateRect);

	const char* text = NULL;

	switch (IconSource()) {
		case kNoIcon:
			text = B_TRANSLATE("no icon");
			break;
		case kApplicationIcon:
			text = B_TRANSLATE("(from application)");
			break;
		case kSupertypeIcon:
			text = B_TRANSLATE("(from super type)");
			break;

		default:
			return;
	}

	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DISABLED_LABEL_TINT));
	SetLowColor(ViewColor());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	float y = fontHeight.ascent;
	if (IconSource() == kNoIcon) {
		// center text in the middle of the icon
		y += (IconSize() - fontHeight.ascent - fontHeight.descent) / 2.0f;
	} else
		y += IconSize() + 3.0f;

	DrawString(text, BPoint(ceilf((Bounds().Width() - StringWidth(text)) / 2.0f),
		ceilf(y)));
}


void
TypeIconView::GetPreferredSize(float* _width, float* _height)
{
	if (_width) {
		float a = StringWidth(B_TRANSLATE("(from application)"));
		float b = StringWidth(B_TRANSLATE("(from super type)"));
		float width = max_c(a, b);
		if (width < IconSize())
			width = IconSize();

		*_width = ceilf(width);
	}

	if (_height) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		*_height = IconSize() + 3.0f + ceilf(fontHeight.ascent
			+ fontHeight.descent);
	}
}


BRect
TypeIconView::BitmapRect() const
{
	if (IconSource() == kNoIcon) {
		// this also defines the drop target area
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		float width = StringWidth(B_TRANSLATE("no icon")) + 8.0f;
		float height = ceilf(fontHeight.ascent + fontHeight.descent) + 6.0f;
		float x = (Bounds().Width() - width) / 2.0f;
		float y = ceilf((IconSize() - fontHeight.ascent - fontHeight.descent)
			/ 2.0f) - 3.0f;

		return BRect(x, y, x + width, y + height);
	}

	float x = (Bounds().Width() - IconSize()) / 2.0f;
	return BRect(x, 0.0f, x + IconSize() - 1, IconSize() - 1);
}


//	#pragma mark -


ExtensionListView::ExtensionListView(const char* name,
		list_view_type type, uint32 flags)
	: DropTargetListView(name, type, flags)
{
}


ExtensionListView::~ExtensionListView()
{
}


void
ExtensionListView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && AcceptsDrag(message)) {
		// create extension list
		BList list;
		entry_ref ref;
		for (int32 index = 0; message->FindRef("refs", index++, &ref) == B_OK; ) {
			const char* point = strchr(ref.name, '.');
			if (point != NULL && point[1])
				list.AddItem(strdup(++point));
		}

		merge_extensions(fType, list);

		// delete extension list
		for (int32 index = list.CountItems(); index-- > 0;) {
			free(list.ItemAt(index));
		}
	} else
		DropTargetListView::MessageReceived(message);
}


bool
ExtensionListView::AcceptsDrag(const BMessage* message)
{
	if (fType.Type() == NULL)
		return false;

	int32 count = 0;
	entry_ref ref;

	for (int32 index = 0; message->FindRef("refs", index++, &ref) == B_OK; ) {
		const char* point = strchr(ref.name, '.');
		if (point != NULL && point[1])
			count++;
	}

	return count > 0;
}


void
ExtensionListView::SetType(BMimeType* type)
{
	if (type != NULL)
		fType.SetTo(type->Type());
	else
		fType.Unset();
}


//	#pragma mark -


FileTypesWindow::FileTypesWindow(const BMessage& settings)
	:
	BWindow(_Frame(settings), B_TRANSLATE("FileTypes"), B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fNewTypeWindow(NULL)
{
	bool showIcons;
	bool showRule;
	if (settings.FindBool("show_icons", &showIcons) != B_OK)
		showIcons = true;
	if (settings.FindBool("show_rule", &showRule) != B_OK)
		showRule = false;

	SetLayout(new BGroupLayout(B_VERTICAL));
	float padding = 3.0f;
	BAlignment labelAlignment = BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP);
	if (be_control_look) {
		// padding = be_control_look->DefaultItemSpacing();
			// this seems to be very large!
		labelAlignment = be_control_look->DefaultLabelAlignment();
	}
	BAlignment fullAlignment =
		BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);

	// add the menu
	BMenuBar* menuBar = new BMenuBar("");

	BMenu* menu = new BMenu(B_TRANSLATE("File"));
	BMenuItem* item;
	menu->AddItem(item = new BMenuItem(
		B_TRANSLATE("New resource file" B_UTF8_ELLIPSIS),
		NULL, 'N', B_COMMAND_KEY));
	item->SetEnabled(false);

	BMenu* recentsMenu = BRecentFilesList::NewFileListMenu(
		B_TRANSLATE("Open" B_UTF8_ELLIPSIS), NULL, NULL,
		be_app, 10, false, NULL, kSignature);
	item = new BMenuItem(recentsMenu, new BMessage(kMsgOpenFilePanel));
	item->SetShortcut('O', B_COMMAND_KEY);
	menu->AddItem(item);

	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Application types" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenApplicationTypesWindow)));
	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem(B_TRANSLATE("About FileTypes" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED)));
	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q', B_COMMAND_KEY));
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Settings"));
	item = new BMenuItem(B_TRANSLATE("Show icons in list"),
		new BMessage(kMsgToggleIcons));
	item->SetMarked(showIcons);
	item->SetTarget(this);
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Show recognition rule"),
		new BMessage(kMsgToggleRule));
	item->SetMarked(showRule);
	item->SetTarget(this);
	menu->AddItem(item);
	menuBar->AddItem(menu);

	AddChild(menuBar);
	menuBar->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	// MIME Types list
	BButton* addTypeButton = new BButton("add",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddType));

	fRemoveTypeButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveType) );

	fTypeListView = new MimeTypeListView("typeview", NULL, showIcons, false);
	fTypeListView->SetSelectionMessage(new BMessage(kMsgTypeSelected));

	BScrollView* typeListScrollView = new BScrollView("scrollview",
		fTypeListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	// "Icon" group

	fIconView = new TypeIconView("icon");
	fIconBox = new BBox("Icon BBox");
	fIconBox->SetLabel(B_TRANSLATE("Icon"));
	fIconBox->AddChild(BGroupLayoutBuilder(B_VERTICAL, padding)
		.Add(BSpaceLayoutItem::CreateGlue(), 1)
		.Add(fIconView, 3)
		.Add(BSpaceLayoutItem::CreateGlue(), 1)
		.SetInsets(padding, padding, padding, padding)
	);

	// "File Recognition" group

	fRecognitionBox = new BBox("Recognition Box");
	fRecognitionBox->SetLabel(B_TRANSLATE("File recognition"));
	fRecognitionBox->SetExplicitAlignment(fullAlignment);

	fExtensionLabel = new StringView(B_TRANSLATE("Extensions:"), NULL);
	fExtensionLabel->LabelView()->SetExplicitAlignment(labelAlignment);

	fAddExtensionButton = new BButton("add ext",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddExtension));

	fRemoveExtensionButton = new BButton("remove ext", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveExtension));

	fExtensionListView = new ExtensionListView("listview ext",
		B_SINGLE_SELECTION_LIST);
	fExtensionListView->SetSelectionMessage(
		new BMessage(kMsgExtensionSelected));
	fExtensionListView->SetInvocationMessage(
		new BMessage(kMsgExtensionInvoked));

	BScrollView* scrollView = new BScrollView("scrollview ext",
		fExtensionListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	fRuleControl = new BTextControl("rule", B_TRANSLATE("Rule:"), "",
		new BMessage(kMsgRuleEntered));
	fRuleControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	fRuleControl->Hide();

	BView* recognitionBoxGrid =
		BGridLayoutBuilder(padding, padding)
			.Add(fExtensionLabel->LabelView(), 0, 0)
			.Add(scrollView, 0, 1, 2, 3)
			.Add(fAddExtensionButton, 2, 1)
			.Add(fRemoveExtensionButton, 2, 2)
			.Add(fRuleControl, 0, 4, 3, 1)
			.SetInsets(padding, padding, padding, padding);

	recognitionBoxGrid->SetExplicitAlignment(fullAlignment);
	fRecognitionBox->AddChild(recognitionBoxGrid);

	// "Description" group

	fDescriptionBox = new BBox("description BBox");
	fDescriptionBox->SetLabel(B_TRANSLATE("Description"));
	fDescriptionBox->SetExplicitAlignment(fullAlignment);

	fInternalNameView = new StringView(B_TRANSLATE("Internal name:"), NULL);
	fInternalNameView->SetEnabled(false);
	fTypeNameControl = new BTextControl("type", B_TRANSLATE("Type name:"), "",
		new BMessage(kMsgTypeEntered));
	fDescriptionControl = new BTextControl("description",
		B_TRANSLATE("Description:"), "", new BMessage(kMsgDescriptionEntered));

	fDescriptionBox->AddChild(BGridLayoutBuilder(padding, padding)
		.Add(fInternalNameView->LabelView(), 0, 0)
		.Add(fInternalNameView->TextView(), 1, 0)
		.Add(fTypeNameControl->CreateLabelLayoutItem(), 0, 1)
		.Add(fTypeNameControl->CreateTextViewLayoutItem(), 1, 1, 2)
		.Add(fDescriptionControl->CreateLabelLayoutItem(), 0, 2)
		.Add(fDescriptionControl->CreateTextViewLayoutItem(), 1, 2, 2)
		.SetInsets(padding, padding, padding, padding)
	);


	// "Preferred Application" group

	fPreferredBox = new BBox("preferred BBox");
	fPreferredBox->SetLabel(B_TRANSLATE("Preferred application"));

	menu = new BPopUpMenu("preferred");
	menu->AddItem(item = new BMenuItem(B_TRANSLATE("None"),
		new BMessage(kMsgPreferredAppChosen)));
	item->SetMarked(true);
	fPreferredField = new BMenuField("preferred", (char*)NULL, menu);

	fSelectButton = new BButton("select",
		B_TRANSLATE("Select" B_UTF8_ELLIPSIS),
		new BMessage(kMsgSelectPreferredApp));

	fSameAsButton = new BButton("same as",
		B_TRANSLATE("Same as" B_UTF8_ELLIPSIS),
		new BMessage(kMsgSamePreferredAppAs));

	fPreferredBox->AddChild(
		BGroupLayoutBuilder(B_HORIZONTAL, padding)
			.Add(fPreferredField)
			.Add(fSelectButton)
			.Add(fSameAsButton)
			.SetInsets(padding, padding, padding, padding)
	);

	// "Extra Attributes" group
	fAttributeBox = new BBox("Attribute Box");
	fAttributeBox->SetLabel(B_TRANSLATE("Extra attributes"));

	fAddAttributeButton = new BButton("add attr",
		"Add" B_UTF8_ELLIPSIS, new BMessage(kMsgAddAttribute));

	fRemoveAttributeButton = new BButton("remove attr", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveAttribute));

	fAttributeListView = new AttributeListView("listview attr");
	fAttributeListView->SetSelectionMessage(
		new BMessage(kMsgAttributeSelected));
	fAttributeListView->SetInvocationMessage(
		new BMessage(kMsgAttributeInvoked));

	BScrollView* attributesScroller = new BScrollView("scrollview attr",
		fAttributeListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	fAttributeBox->AddChild(BGridLayoutBuilder(padding, padding)
		.Add(attributesScroller, 0, 0, 2, 3)
		.Add(fAddAttributeButton, 2, 0)
		.Add(fRemoveAttributeButton, 2, 1)
		.SetInsets(padding, padding, padding, padding)
	);


	BView* topView =
		BGroupLayoutBuilder(B_HORIZONTAL, padding)
		.SetInsets(padding, padding, padding, padding)
		.Add(BGroupLayoutBuilder(B_VERTICAL, padding)
			.Add(typeListScrollView)
			.Add(BGroupLayoutBuilder(B_HORIZONTAL, padding)
				.Add(addTypeButton).Add(fRemoveTypeButton)
			)
		)
		// Right side
		.Add(BGroupLayoutBuilder(B_VERTICAL, padding)
			.Add(BGroupLayoutBuilder(B_HORIZONTAL, padding)
				.Add(fIconBox, 1).Add(fRecognitionBox, 3)
			)
			.Add(fDescriptionBox)
			.Add(fPreferredBox)
			.Add(fAttributeBox, 5)
		);

	//topView->SetExplicitAlignment(fullAlignment);
	//topView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(topView);

	_SetType(NULL);
	_ShowSnifferRule(showRule);

	BMimeType::StartWatching(this);
}


FileTypesWindow::~FileTypesWindow()
{
	BMimeType::StopWatching(this);
}


BRect
FileTypesWindow::_Frame(const BMessage& settings) const
{
	BRect rect;
	if (settings.FindRect("file_types_frame", &rect) == B_OK)
		return rect;

	return BRect(80.0f, 80.0f, 0.0f, 0.0f);
}


void
FileTypesWindow::_ShowSnifferRule(bool show)
{
	if (fRuleControl->IsHidden() == !show)
		return;

	if (!show)
		fRuleControl->Hide();
	else
		fRuleControl->Show();
}


void
FileTypesWindow::_UpdateExtensions(BMimeType* type)
{
	// clear list

	for (int32 i = fExtensionListView->CountItems(); i-- > 0;) {
		delete fExtensionListView->ItemAt(i);
	}
	fExtensionListView->MakeEmpty();

	// fill it again

	if (type == NULL)
		return;

	BMessage extensions;
	if (type->GetFileExtensions(&extensions) != B_OK)
		return;

	const char* extension;
	int32 i = 0;
	while (extensions.FindString("extensions", i++, &extension) == B_OK) {
		char dotExtension[B_FILE_NAME_LENGTH];
		snprintf(dotExtension, B_FILE_NAME_LENGTH, ".%s", extension);

		fExtensionListView->AddItem(new BStringItem(dotExtension));
	}
}


void
FileTypesWindow::_AdoptPreferredApplication(BMessage* message, bool sameAs)
{
	if (fCurrentType.Type() == NULL)
		return;

	BString preferred;
	if (retrieve_preferred_app(message, sameAs, fCurrentType.Type(), preferred)
		!= B_OK) {
		return;
	}

	status_t status = fCurrentType.SetPreferredApp(preferred.String());
	if (status != B_OK)
		error_alert(B_TRANSLATE("Could not set preferred application"),
			status);
}


void
FileTypesWindow::_UpdatePreferredApps(BMimeType* type)
{
	update_preferred_app_menu(fPreferredField->Menu(), type,
		kMsgPreferredAppChosen);
}


void
FileTypesWindow::_UpdateIcon(BMimeType* type)
{
	if (type != NULL)
		fIconView->SetTo(*type);
	else
		fIconView->Unset();
}


void
FileTypesWindow::_SetType(BMimeType* type, int32 forceUpdate)
{
	bool enabled = type != NULL;

	// update controls

	if (type != NULL) {
		if (fCurrentType == *type) {
			if (!forceUpdate)
				return;
		} else
			forceUpdate = B_EVERYTHING_CHANGED;

		if (&fCurrentType != type)
			fCurrentType.SetTo(type->Type());

		fInternalNameView->SetText(type->Type());

		char description[B_MIME_TYPE_LENGTH];

		if ((forceUpdate & B_SHORT_DESCRIPTION_CHANGED) != 0) {
			if (type->GetShortDescription(description) != B_OK)
				description[0] = '\0';
			fTypeNameControl->SetText(description);
		}

		if ((forceUpdate & B_LONG_DESCRIPTION_CHANGED) != 0) {
			if (type->GetLongDescription(description) != B_OK)
				description[0] = '\0';
			fDescriptionControl->SetText(description);
		}

		if ((forceUpdate & B_SNIFFER_RULE_CHANGED) != 0) {
			BString rule;
			if (type->GetSnifferRule(&rule) != B_OK)
				rule = "";
			fRuleControl->SetText(rule.String());
		}

		fExtensionListView->SetType(&fCurrentType);
	} else {
		fCurrentType.Unset();
		fInternalNameView->SetText(NULL);
		fTypeNameControl->SetText(NULL);
		fDescriptionControl->SetText(NULL);
		fRuleControl->SetText(NULL);
		fPreferredField->Menu()->ItemAt(0)->SetMarked(true);
		fExtensionListView->SetType(NULL);
		fAttributeListView->SetTo(NULL);
	}

	if ((forceUpdate & B_FILE_EXTENSIONS_CHANGED) != 0)
		_UpdateExtensions(type);

	if ((forceUpdate & B_PREFERRED_APP_CHANGED) != 0)
		_UpdatePreferredApps(type);

	if ((forceUpdate & (B_ICON_CHANGED | B_PREFERRED_APP_CHANGED)) != 0)
		_UpdateIcon(type);

	if ((forceUpdate & B_ATTR_INFO_CHANGED) != 0)
		fAttributeListView->SetTo(type);

	// enable/disable controls

	fIconView->SetEnabled(enabled);

	fInternalNameView->SetEnabled(enabled);
	fTypeNameControl->SetEnabled(enabled);
	fDescriptionControl->SetEnabled(enabled);
	fPreferredField->SetEnabled(enabled);

	fRemoveTypeButton->SetEnabled(enabled);

	fSelectButton->SetEnabled(enabled);
	fSameAsButton->SetEnabled(enabled);

	fExtensionLabel->SetEnabled(enabled);
	fAddExtensionButton->SetEnabled(enabled);
	fRemoveExtensionButton->SetEnabled(false);
	fRuleControl->SetEnabled(enabled);

	fAddAttributeButton->SetEnabled(enabled);
	fRemoveAttributeButton->SetEnabled(false);
}


void
FileTypesWindow::PlaceSubWindow(BWindow* window)
{
	window->MoveTo(Frame().left + (Frame().Width() - window->Frame().Width())
		/ 2.0f, Frame().top + (Frame().Height() - window->Frame().Height())
		/ 2.0f);
}


void
FileTypesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
		{
			type_code type;
			if (message->GetInfo("refs", &type) == B_OK
				&& type == B_REF_TYPE) {
				be_app->PostMessage(message);
			}
			break;
		}

		case kMsgToggleIcons:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void **)&item) != B_OK)
				break;

			item->SetMarked(!fTypeListView->IsShowingIcons());
			fTypeListView->ShowIcons(item->IsMarked());

			// update settings
			BMessage update(kMsgSettingsChanged);
			update.AddBool("show_icons", item->IsMarked());
			be_app_messenger.SendMessage(&update);
			break;
		}

		case kMsgToggleRule:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void **)&item) != B_OK)
				break;

			item->SetMarked(fRuleControl->IsHidden());
			_ShowSnifferRule(item->IsMarked());

			// update settings
			BMessage update(kMsgSettingsChanged);
			update.AddBool("show_rule", item->IsMarked());
			be_app_messenger.SendMessage(&update);
			break;
		}

		case kMsgTypeSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				MimeTypeItem* item
					= (MimeTypeItem*)fTypeListView->ItemAt(index);
				if (item != NULL) {
					BMimeType type(item->Type());
					_SetType(&type);
				} else
					_SetType(NULL);
			}
			break;
		}

		case kMsgAddType:
			if (fNewTypeWindow == NULL) {
				fNewTypeWindow
					= new NewFileTypeWindow(this, fCurrentType.Type());
				fNewTypeWindow->Show();
			} else
				fNewTypeWindow->Activate();
			break;

		case kMsgNewTypeWindowClosed:
			fNewTypeWindow = NULL;
			break;

		case kMsgRemoveType:
		{
			if (fCurrentType.Type() == NULL)
				break;

			BAlert* alert;
			if (fCurrentType.IsSupertypeOnly()) {
				alert = new BPrivate::OverrideAlert(
					B_TRANSLATE("FileTypes request"),
					B_TRANSLATE("Removing a super type cannot be reverted.\n"
					"All file types that belong to this super type "
					"will be lost!\n\n"
					"Are you sure you want to do this? To remove the whole "
					"group, hold down the Shift key and press \"Remove\"."),
					B_TRANSLATE("Remove"), B_SHIFT_KEY, B_TRANSLATE("Cancel"),
					0, NULL, 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			} else {
				alert = new BAlert(B_TRANSLATE("FileTypes request"),
					B_TRANSLATE("Removing a file type cannot be reverted.\n"
					"Are you sure you want to remove it?"),
					B_TRANSLATE("Remove"), B_TRANSLATE("Cancel"),
					NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			}
			if (alert->Go())
				break;

			status_t status = fCurrentType.Delete();
			if (status != B_OK) {
				fprintf(stderr, B_TRANSLATE(
					"Could not remove file type: %s\n"), strerror(status));
			}
			break;
		}

		case kMsgSelectNewType:
		{
			const char* type;
			if (message->FindString("type", &type) == B_OK)
				fTypeListView->SelectNewType(type);
			break;
		}

		// File Recognition group

		case kMsgExtensionSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				BStringItem* item
					= (BStringItem*)fExtensionListView->ItemAt(index);
				fRemoveExtensionButton->SetEnabled(item != NULL);
			}
			break;
		}

		case kMsgExtensionInvoked:
		{
			if (fCurrentType.Type() == NULL)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				BStringItem* item
					= (BStringItem*)fExtensionListView->ItemAt(index);
				if (item == NULL)
					break;

				BWindow* window
					= new ExtensionWindow(this, fCurrentType, item->Text());
				window->Show();
			}
			break;
		}

		case kMsgAddExtension:
		{
			if (fCurrentType.Type() == NULL)
				break;

			BWindow* window = new ExtensionWindow(this, fCurrentType, NULL);
			window->Show();
			break;
		}

		case kMsgRemoveExtension:
		{
			int32 index = fExtensionListView->CurrentSelection();
			if (index < 0 || fCurrentType.Type() == NULL)
				break;

			BMessage extensions;
			if (fCurrentType.GetFileExtensions(&extensions) == B_OK) {
				extensions.RemoveData("extensions", index);
				fCurrentType.SetFileExtensions(&extensions);
			}
			break;
		}

		case kMsgRuleEntered:
		{
			// check rule
			BString parseError;
			if (BMimeType::CheckSnifferRule(fRuleControl->Text(),
					&parseError) != B_OK) {
				parseError.Prepend(
					B_TRANSLATE("Recognition rule is not valid:\n\n"));
				error_alert(parseError.String());
			} else
				fCurrentType.SetSnifferRule(fRuleControl->Text());
			break;
		}

		// Description group

		case kMsgTypeEntered:
		{
			fCurrentType.SetShortDescription(fTypeNameControl->Text());
			break;
		}

		case kMsgDescriptionEntered:
		{
			fCurrentType.SetLongDescription(fDescriptionControl->Text());
			break;
		}

		// Preferred Application group

		case kMsgPreferredAppChosen:
		{
			const char* signature;
			if (message->FindString("signature", &signature) != B_OK)
				signature = NULL;

			fCurrentType.SetPreferredApp(signature);
			break;
		}

		case kMsgSelectPreferredApp:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title",
				B_TRANSLATE("Select preferred application"));
			panel.AddInt32("message", kMsgPreferredAppOpened);
			panel.AddMessenger("target", this);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgPreferredAppOpened:
			_AdoptPreferredApplication(message, false);
			break;

		case kMsgSamePreferredAppAs:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title",
				B_TRANSLATE("Select same preferred application as"));
			panel.AddInt32("message", kMsgSamePreferredAppAsOpened);
			panel.AddMessenger("target", this);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgSamePreferredAppAsOpened:
			_AdoptPreferredApplication(message, true);
			break;

		// Extra Attributes group

		case kMsgAttributeSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				AttributeItem* item
					= (AttributeItem*)fAttributeListView->ItemAt(index);
				fRemoveAttributeButton->SetEnabled(item != NULL);
			}
			break;
		}

		case kMsgAttributeInvoked:
		{
			if (fCurrentType.Type() == NULL)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				AttributeItem* item
					= (AttributeItem*)fAttributeListView->ItemAt(index);
				if (item == NULL)
					break;

				BWindow* window = new AttributeWindow(this, fCurrentType,
					item);
				window->Show();
			}
			break;
		}

		case kMsgAddAttribute:
		{
			if (fCurrentType.Type() == NULL)
				break;

			BWindow* window = new AttributeWindow(this, fCurrentType, NULL);
			window->Show();
			break;
		}

		case kMsgRemoveAttribute:
		{
			int32 index = fAttributeListView->CurrentSelection();
			if (index < 0 || fCurrentType.Type() == NULL)
				break;

			BMessage attributes;
			if (fCurrentType.GetAttrInfo(&attributes) == B_OK) {
				const char* kAttributeNames[] = {
					"attr:public_name", "attr:name", "attr:type",
					"attr:editable", "attr:viewable", "attr:extra",
					"attr:alignment", "attr:width", "attr:display_as"
				};

				for (uint32 i = 0; i <
						sizeof(kAttributeNames) / sizeof(kAttributeNames[0]);
						i++) {
					attributes.RemoveData(kAttributeNames[i], index);
				}

				fCurrentType.SetAttrInfo(&attributes);
			}
			break;
		}

		case B_META_MIME_CHANGED:
		{
			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK)
				break;

			if (fCurrentType.Type() == NULL)
				break;

			if (!strcasecmp(fCurrentType.Type(), type)) {
				if (which != B_MIME_TYPE_DELETED)
					_SetType(&fCurrentType, which);
				else
					_SetType(NULL);
			} else {
				// this change could still affect our current type

				if (which == B_MIME_TYPE_DELETED
					|| which == B_SUPPORTED_TYPES_CHANGED
					|| which == B_PREFERRED_APP_CHANGED)
					_UpdatePreferredApps(&fCurrentType);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


bool
FileTypesWindow::QuitRequested()
{
	BMessage update(kMsgSettingsChanged);
	update.AddRect("file_types_frame", Frame());
	be_app_messenger.SendMessage(&update);

	be_app->PostMessage(kMsgTypesWindowClosed);
	return true;
}


