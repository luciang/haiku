/*
 * Copyright 2001-2008, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stefano Ceccherini (stefano.ceccherini@gmail.com)
 *		Rene Gollent (anevilyak@gmail.com)
 */

#include <new>
#include <ctype.h>
#include <string.h>

#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <Layout.h>
#include <LayoutUtils.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <Window.h>

#include <AppServerLink.h>
#include <BMCPrivate.h>
#include <MenuPrivate.h>
#include <MenuWindow.h>
#include <ServerProtocol.h>

#include "utf8_functions.h"

#define USE_CACHED_MENUWINDOW 1

using std::nothrow;
using BPrivate::BMenuWindow;

namespace BPrivate {

class TriggerList {
public:
	TriggerList() {}
	~TriggerList() {}

	// TODO: make this work with Unicode characters!

	bool HasTrigger(uint32 c)
		{ return fList.HasItem((void*)tolower(c)); }
	bool AddTrigger(uint32 c)
		{ return fList.AddItem((void*)tolower(c)); }

private:
	BList	fList;
};


class ExtraMenuData {
public:
	menu_tracking_hook trackingHook;
	void *trackingState;

	ExtraMenuData(menu_tracking_hook func, void *state)
	{
		trackingHook = func;
		trackingState = state;
	}
};


}	// namespace BPrivate


menu_info BMenu::sMenuInfo;
bool BMenu::sAltAsCommandKey;


static property_info sPropList[] = {
	{ "Enabled", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if menu or menu item is enabled; false "
		"otherwise.",
		0, { B_BOOL_TYPE }
	},

	{ "Enabled", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Enables or disables menu or menu item.",
		0, { B_BOOL_TYPE }
	},

	{ "Label", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the string label of the menu or menu item.",
		0, { B_STRING_TYPE }
	},

	{ "Label", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the string label of the menu or menu item.",
		0, { B_STRING_TYPE }
	},

	{ "Mark", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if the menu item or the menu's superitem "
		"is marked; false otherwise.",
		0, { B_BOOL_TYPE }
	},

	{ "Mark", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Marks or unmarks the menu item or the menu's superitem.",
		0, { B_BOOL_TYPE }
	},

	{ "Menu", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label found in \"data\" "
		"and the int32 command found in \"what\" (used as the what field in the CMessage "
		"sent by the item)." , 0, {},
		{ 	{{{"data", B_STRING_TYPE}}}
		}
	},

	{ "Menu", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the selected menu or menus.", 0, {}
	},

	{ "Menu", { },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the current "
		"specifier off the stack.", 0, {}
	},

	{ "MenuItem", { B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Counts the number of menu items in the specified menu.",
		0, { B_INT32_TYPE }
	},

	{ "MenuItem", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label found in \"data\" "
		"and the int32 command found in \"what\" (used as the what field in the CMessage "
		"sent by the item).", 0, {},
		{	{ {{"data", B_STRING_TYPE },
			{"be:invoke_message", B_MESSAGE_TYPE},
			{"what", B_INT32_TYPE},
			{"be:target", B_MESSENGER_TYPE}} }
		}
	},

	{ "MenuItem", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the specified menu item from its parent menu."
	},

	{ "MenuItem", { B_EXECUTE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Invokes the specified menu item."
	},

	{ "MenuItem", { },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the current "
		"specifier off the stack."
	},

	{}
};


const char *kEmptyMenuLabel = "<empty>";


struct BMenu::LayoutData {
	BSize	preferred;
};


BMenu::BMenu(const char *name, menu_layout layout)
	: BView(BRect(0, 0, 0, 0), name, 0, B_WILL_DRAW),
	fChosenItem(NULL),
	fPad(14.0f, 2.0f, 20.0f, 0.0f),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(0),
	fLayout(layout),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fRedrawAfterSticky(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


BMenu::BMenu(const char *name, float width, float height)
	: BView(BRect(0.0f, width, 0.0f, height), name, 0, B_WILL_DRAW),
	fChosenItem(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(0),
	fLayout(B_ITEMS_IN_MATRIX),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fRedrawAfterSticky(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


BMenu::~BMenu()
{
	_DeleteMenuWindow();

	RemoveItems(0, CountItems(), true);

	delete fInitMatrixSize;
	delete fExtraMenuData;
	delete fLayoutData;
}


BMenu::BMenu(BMessage *archive)
	: BView(archive),
	fChosenItem(NULL),
	fPad(14.0f, 2.0f, 20.0f, 0.0f),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(0),
	fLayout(B_ITEMS_IN_ROW),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fRedrawAfterSticky(false),
	fAttachAborted(false)
{
	_InitData(archive);
}


BArchivable*
BMenu::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BMenu"))
		return new (nothrow) BMenu(archive);

	return NULL;
}


status_t
BMenu::Archive(BMessage *data, bool deep) const
{
	status_t err = BView::Archive(data, deep);

	if (err == B_OK && Layout() != B_ITEMS_IN_ROW)
		err = data->AddInt32("_layout", Layout());
	if (err == B_OK)
		err = data->AddBool("_rsize_to_fit", fResizeToFit);
	if (err == B_OK)
		err = data->AddBool("_disable", !IsEnabled());
	if (err ==  B_OK)
		err = data->AddBool("_radio", IsRadioMode());
	if (err == B_OK)
		err = data->AddBool("_trig_disabled", AreTriggersEnabled());
	if (err == B_OK)
		err = data->AddBool("_dyn_label", fDynamicName);
	if (err == B_OK)
		err = data->AddFloat("_maxwidth", fMaxContentWidth);
	if (err == B_OK && deep) {
		BMenuItem *item = NULL;
		int32 index = 0;
		while ((item = ItemAt(index++)) != NULL) {
			BMessage itemData;
			item->Archive(&itemData, deep);
			err = data->AddMessage("_items", &itemData);
			if (err != B_OK)
				break;
			if (fLayout == B_ITEMS_IN_MATRIX) {
				err = data->AddRect("_i_frames", item->fBounds);
			}
		}
	}

	return err;
}


void
BMenu::AttachedToWindow()
{
	BView::AttachedToWindow();

	sAltAsCommandKey = true;
	key_map *keys = NULL;
	char *chars = NULL;
	get_key_map(&keys, &chars);
	if (keys == NULL || keys->left_command_key != 0x5d
		|| keys->right_command_key != 0x5f)
		sAltAsCommandKey = false;
	free(chars);
	free(keys);

	BMenuItem *superItem = Superitem();
	BMenu *superMenu = Supermenu();
	if (AddDynamicItem(B_INITIAL_ADD)) {
		do {
			if (superMenu != NULL && !superMenu->_OkToProceed(superItem)) {
				AddDynamicItem(B_ABORT);
				fAttachAborted = true;
				break;
			}
		} while (AddDynamicItem(B_PROCESSING));
	}

	if (!fAttachAborted) {
		_CacheFontInfo();
		_LayoutItems(0);
		_UpdateWindowViewSize(false);
	}
}


void
BMenu::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


bool
BMenu::AddItem(BMenuItem *item)
{
	return AddItem(item, CountItems());
}


bool
BMenu::AddItem(BMenuItem *item, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenuItem *, int32) this method can only "
				"be called if the menu layout is not B_ITEMS_IN_MATRIX");

	if (!item || !_AddItem(item, index))
		return false;

	InvalidateLayout();
	if (LockLooper()) {
		if (!Window()->IsHidden()) {
			_LayoutItems(index);
			_UpdateWindowViewSize(false);
			Invalidate();
		}
		UnlockLooper();
	}
	return true;
}


bool
BMenu::AddItem(BMenuItem *item, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenuItem *, BRect) this method can only "
			"be called if the menu layout is B_ITEMS_IN_MATRIX");

	if (!item)
		return false;

	item->fBounds = frame;

	int32 index = CountItems();
	if (!_AddItem(item, index)) {
		return false;
	}

	if (LockLooper()) {
		if (!Window()->IsHidden()) {
			_LayoutItems(index);
			Invalidate();
		}
		UnlockLooper();
	}

	return true;
}


bool
BMenu::AddItem(BMenu *submenu)
{
	BMenuItem *item = new (nothrow) BMenuItem(submenu);
	if (!item)
		return false;

	if (!AddItem(item, CountItems())) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::AddItem(BMenu *submenu, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenuItem *, int32) this method can only "
				"be called if the menu layout is not B_ITEMS_IN_MATRIX");

	BMenuItem *item = new (nothrow) BMenuItem(submenu);
	if (!item)
		return false;

	if (!AddItem(item, index)) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::AddItem(BMenu *submenu, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenu *, BRect) this method can only "
			"be called if the menu layout is B_ITEMS_IN_MATRIX");

	BMenuItem *item = new (nothrow) BMenuItem(submenu);
	if (!item)
		return false;

	if (!AddItem(item, frame)) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::AddList(BList *list, int32 index)
{
	// TODO: test this function, it's not documented in the bebook.
	if (list == NULL)
		return false;

	bool locked = LockLooper();

	int32 numItems = list->CountItems();
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem *item = static_cast<BMenuItem *>(list->ItemAt(i));
		if (item != NULL) {
			if (!_AddItem(item, index + i))
				break;
		}
	}

	InvalidateLayout();
	if (locked && Window() != NULL && !Window()->IsHidden()) {
		// Make sure we update the layout if needed.
		_LayoutItems(index);
		_UpdateWindowViewSize(false);
		Invalidate();
	}

	if (locked)
		UnlockLooper();

	return true;
}


bool
BMenu::AddSeparatorItem()
{
	BMenuItem *item = new (nothrow) BSeparatorItem();
	if (!item || !AddItem(item, CountItems())) {
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::RemoveItem(BMenuItem *item)
{
	return _RemoveItems(0, 0, item, false);
}


BMenuItem *
BMenu::RemoveItem(int32 index)
{
	BMenuItem *item = ItemAt(index);
	if (item != NULL)
		_RemoveItems(0, 0, item, false);
	return item;
}


bool
BMenu::RemoveItems(int32 index, int32 count, bool deleteItems)
{
	return _RemoveItems(index, count, NULL, deleteItems);
}


bool
BMenu::RemoveItem(BMenu *submenu)
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (static_cast<BMenuItem *>(fItems.ItemAtFast(i))->Submenu() == submenu)
			return _RemoveItems(i, 1, NULL, false);
	}

	return false;
}


int32
BMenu::CountItems() const
{
	return fItems.CountItems();
}


BMenuItem *
BMenu::ItemAt(int32 index) const
{
	return static_cast<BMenuItem *>(fItems.ItemAt(index));
}


BMenu *
BMenu::SubmenuAt(int32 index) const
{
	BMenuItem *item = static_cast<BMenuItem *>(fItems.ItemAt(index));
	return (item != NULL) ? item->Submenu() : NULL;
}


int32
BMenu::IndexOf(BMenuItem *item) const
{
	return fItems.IndexOf(item);
}


int32
BMenu::IndexOf(BMenu *submenu) const
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (ItemAt(i)->Submenu() == submenu)
			return i;
	}

	return -1;
}


BMenuItem *
BMenu::FindItem(const char *label) const
{
	BMenuItem *item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Label() && strcmp(item->Label(), label) == 0)
			return item;

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(label);
			if (item != NULL)
				return item;
		}
	}

	return NULL;
}


BMenuItem *
BMenu::FindItem(uint32 command) const
{
	BMenuItem *item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Command() == command)
			return item;

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(command);
			if (item != NULL)
				return item;
		}
	}

	return NULL;
}


status_t
BMenu::SetTargetForItems(BHandler *handler)
{
	status_t status = B_OK;
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		status = ItemAt(i)->SetTarget(handler);
		if (status < B_OK)
			break;
	}

	return status;
}


status_t
BMenu::SetTargetForItems(BMessenger messenger)
{
	status_t status = B_OK;
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		status = ItemAt(i)->SetTarget(messenger);
		if (status < B_OK)
			break;
	}

	return status;
}


void
BMenu::SetEnabled(bool enabled)
{
	if (fEnabled == enabled)
		return;

	fEnabled = enabled;

	if (fSuperitem)
		fSuperitem->SetEnabled(enabled);
}


void
BMenu::SetRadioMode(bool flag)
{
	fRadioMode = flag;
	if (!flag)
		SetLabelFromMarked(false);
}


void
BMenu::SetTriggersEnabled(bool flag)
{
	fTriggerEnabled = flag;
}


void
BMenu::SetMaxContentWidth(float width)
{
	fMaxContentWidth = width;
}


void
BMenu::SetLabelFromMarked(bool flag)
{
	fDynamicName = flag;
	if (flag)
		SetRadioMode(true);
}


bool
BMenu::IsLabelFromMarked()
{
	return fDynamicName;
}


bool
BMenu::IsEnabled() const
{
	if (!fEnabled)
		return false;

	return fSuper ? fSuper->IsEnabled() : true ;
}


bool
BMenu::IsRadioMode() const
{
	return fRadioMode;
}


bool
BMenu::AreTriggersEnabled() const
{
	return fTriggerEnabled;
}


bool
BMenu::IsRedrawAfterSticky() const
{
	return fRedrawAfterSticky;
}


float
BMenu::MaxContentWidth() const
{
	return fMaxContentWidth;
}


BMenuItem *
BMenu::FindMarked()
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem *item = ItemAt(i);
		if (item->IsMarked())
			return item;
	}

	return NULL;
}


BMenu *
BMenu::Supermenu() const
{
	return fSuper;
}


BMenuItem *
BMenu::Superitem() const
{
	return fSuperitem;
}


void
BMenu::MessageReceived(BMessage *msg)
{
	BView::MessageReceived(msg);
}


void
BMenu::KeyDown(const char *bytes, int32 numBytes)
{
	// TODO: Test how it works on beos and implement it correctly
	switch (bytes[0]) {
		case B_UP_ARROW:
			if (fLayout == B_ITEMS_IN_COLUMN)
				_SelectNextItem(fSelected, false);
			break;

		case B_DOWN_ARROW:
			if (fLayout == B_ITEMS_IN_COLUMN)
				_SelectNextItem(fSelected, true);
			break;

		case B_LEFT_ARROW:
			if (fLayout == B_ITEMS_IN_ROW)
				_SelectNextItem(fSelected, false);
			else {
				// this case has to be handled a bit specially.
				BMenuItem *item = Superitem();
				if (item) {
					if (dynamic_cast<BMenuBar *>(Supermenu())) {
						// if we're at the top menu below the menu bar, pass the keypress to
						// the menu bar so we can move to another top level menu
						BMessenger msgr(Supermenu());
						msgr.SendMessage(Window()->CurrentMessage());
					} else
						Supermenu()->_SelectItem(item, false, false);
				}
			}
			break;

		case B_RIGHT_ARROW:
			if (fLayout == B_ITEMS_IN_ROW)
				_SelectNextItem(fSelected, true);
			else {
				if (fSelected && fSelected->Submenu()) {
					_SelectItem(fSelected, true, true);
				} else if (dynamic_cast<BMenuBar *>(Supermenu())) {
					// if we have no submenu and we're an
					// item in the top menu below the menubar,
					// pass the keypress to the menubar
					// so you can use the keypress to switch menus.				
					BMessenger msgr(Supermenu());
					msgr.SendMessage(Window()->CurrentMessage());
				}
			}
			break;

		case B_ENTER:
		case B_SPACE:
			if (fSelected) {
				InvokeItem(fSelected);
				QuitTracking(false);			
			}
			break;

		case B_ESCAPE:
			QuitTracking();
			break;

		default:
		{
			uint32 trigger = UTF8ToCharCode(&bytes);

			for (uint32 i = CountItems(); i-- > 0;) {
				BMenuItem* item = ItemAt(i);
				if (item->fTriggerIndex < 0 || item->fTrigger != trigger)
					continue;

				InvokeItem(item);
			}
			break;
		}
	}
}


void
BMenu::Draw(BRect updateRect)
{
	if (_RelayoutIfNeeded()) {
		Invalidate();
		return;
	}

	DrawBackground(updateRect);
	_DrawItems(updateRect);
}


BSize
BMenu::MinSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() ? GetLayout()->MinSize()
		: fLayoutData->preferred);
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


BSize
BMenu::MaxSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() ? GetLayout()->MaxSize()
		: fLayoutData->preferred);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


BSize
BMenu::PreferredSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() ? GetLayout()->PreferredSize()
		: fLayoutData->preferred);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


void
BMenu::GetPreferredSize(float *_width, float *_height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fLayoutData->preferred.width;
	if (_height)
		*_height = fLayoutData->preferred.height;
}


void
BMenu::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


void
BMenu::DoLayout()
{
	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout()) {
		BView::DoLayout();
		return;
	}

	if (_RelayoutIfNeeded())
		Invalidate();
}


void
BMenu::FrameMoved(BPoint new_position)
{
	BView::FrameMoved(new_position);
}


void
BMenu::FrameResized(float new_width, float new_height)
{
	BView::FrameResized(new_width, new_height);
}


void
BMenu::InvalidateLayout()
{
	InvalidateLayout(false);
}


void
BMenu::InvalidateLayout(bool descendants)
{
	fUseCachedMenuLayout = false;
	fLayoutData->preferred.Set(B_SIZE_UNSET, B_SIZE_UNSET);

	BView::InvalidateLayout(descendants);
}


BHandler *
BMenu::ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
						int32 form, const char *property)
{
	BPropertyInfo propInfo(sPropList);
	BHandler *target = NULL;

	switch (propInfo.FindMatch(msg, 0, specifier, form, property)) {
		case B_ERROR:
			break;

		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			target = this;
			break;
		case 8:
			// TODO: redirect to menu
			target = this;
			break;
		case 9:
		case 10:
		case 11:
		case 12:
			target = this;
			break;
		case 13:
			// TODO: redirect to menuitem
			target = this;
			break;
	}

	if (!target)
		target = BView::ResolveSpecifier(msg, index, specifier, form,
		property);

	return target;
}


status_t
BMenu::GetSupportedSuites(BMessage *data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-menu");

	if (err < B_OK)
		return err;

	BPropertyInfo propertyInfo(sPropList);
	err = data->AddFlat("messages", &propertyInfo);

	if (err < B_OK)
		return err;

	return BView::GetSupportedSuites(data);
}


status_t
BMenu::Perform(perform_code d, void *arg)
{
	return BView::Perform(d, arg);
}


void
BMenu::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
}


void
BMenu::AllAttached()
{
	BView::AllAttached();
}


void
BMenu::AllDetached()
{
	BView::AllDetached();
}


BMenu::BMenu(BRect frame, const char *name, uint32 resizingMode, uint32 flags,
		menu_layout layout, bool resizeToFit)
	: BView(frame, name, resizingMode, flags),
	fChosenItem(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fState(0),
	fLayout(layout),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(resizeToFit),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fRedrawAfterSticky(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


void
BMenu::SetItemMargins(float left, float top, float right, float bottom)
{
	fPad.Set(left, top, right, bottom);
}


void
BMenu::GetItemMargins(float *left, float *top, float *right,
	float *bottom) const
{
	if (left != NULL)
		*left = fPad.left;
	if (top != NULL)
		*top = fPad.top;
	if (right != NULL)
		*right = fPad.right;
	if (bottom != NULL)
		*bottom = fPad.bottom;
}


menu_layout
BMenu::Layout() const
{
	return fLayout;
}


void
BMenu::Show()
{
	Show(false);
}


void
BMenu::Show(bool selectFirst)
{
	_Install(NULL);
	_Show(selectFirst);
}


void
BMenu::Hide()
{
	_Hide();
	_Uninstall();
}


BMenuItem *
BMenu::Track(bool sticky, BRect *clickToOpenRect)
{
	if (sticky && LockLooper()) {
		//RedrawAfterSticky(Bounds());
			// the call above didn't do anything, so I've removed it for now
		UnlockLooper();
	}

	if (clickToOpenRect != NULL && LockLooper()) {
		fExtraRect = clickToOpenRect;
		ConvertFromScreen(fExtraRect);
		UnlockLooper();
	}

	_SetStickyMode(sticky);

	int action;
	BMenuItem *menuItem = _Track(&action);

	fExtraRect = NULL;

	return menuItem;
}


bool
BMenu::AddDynamicItem(add_state state)
{
	// Implemented in subclasses
	return false;
}


void
BMenu::DrawBackground(BRect update)
{
	rgb_color oldColor = HighColor();
	SetHighColor(sMenuInfo.background_color);
	FillRect(Bounds() & update, B_SOLID_HIGH);
	SetHighColor(oldColor);
}


void
BMenu::SetTrackingHook(menu_tracking_hook func, void *state)
{
	delete fExtraMenuData;
	fExtraMenuData = new (nothrow) BPrivate::ExtraMenuData(func, state);
}


void BMenu::_ReservedMenu3() {}
void BMenu::_ReservedMenu4() {}
void BMenu::_ReservedMenu5() {}
void BMenu::_ReservedMenu6() {}


BMenu &
BMenu::operator=(const BMenu &)
{
	return *this;
}


void
BMenu::_InitData(BMessage* archive)
{
	// TODO: Get _color, _fname, _fflt from the message, if present
	BFont font;
	font.SetFamilyAndStyle(sMenuInfo.f_family, sMenuInfo.f_style);
	font.SetSize(sMenuInfo.font_size);
	SetFont(&font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);

	fLayoutData = new LayoutData;
	
	SetLowColor(sMenuInfo.background_color);
	SetViewColor(sMenuInfo.background_color);

	fTriggerEnabled = sMenuInfo.triggers_always_shown;

	if (archive != NULL) {
		archive->FindInt32("_layout", (int32 *)&fLayout);
		archive->FindBool("_rsize_to_fit", &fResizeToFit);
		bool disabled;
		if (archive->FindBool("_disable", &disabled) == B_OK)
			fEnabled = !disabled;
		archive->FindBool("_radio", &fRadioMode);

		bool disableTrigger = false;
		archive->FindBool("_trig_disabled", &disableTrigger);
		fTriggerEnabled = !disableTrigger;

		archive->FindBool("_dyn_label", &fDynamicName);
		archive->FindFloat("_maxwidth", &fMaxContentWidth);

		BMessage msg;
       		for (int32 i = 0; archive->FindMessage("_items", i, &msg) == B_OK; i++) {
			BArchivable *object = instantiate_object(&msg);
			if (BMenuItem *item = dynamic_cast<BMenuItem *>(object)) {
				BRect bounds;
				if (fLayout == B_ITEMS_IN_MATRIX
					&& archive->FindRect("_i_frames", i, &bounds) == B_OK)
					AddItem(item, bounds);
				else
					AddItem(item);
			}
		}
	}
}


bool
BMenu::_Show(bool selectFirstItem)
{
	// See if the supermenu has a cached menuwindow,
	// and use that one if possible.
	BMenuWindow *window = NULL;
	bool ourWindow = false;
	if (fSuper != NULL) {
		fSuperbounds = fSuper->ConvertToScreen(fSuper->Bounds());
		window = fSuper->_MenuWindow();
	}

	// Otherwise, create a new one
	// This happens for "stand alone" BPopUpMenus
	// (i.e. not within a BMenuField)
	if (window == NULL) {
		// Menu windows get the BMenu's handler name
		window = new (nothrow) BMenuWindow(Name());
		ourWindow = true;
	}

	if (window == NULL)
		return false;

	if (window->Lock()) {
		fAttachAborted = false;
		window->AttachMenu(this);

		// Menu didn't have the time to add its items: aborting...
		if (fAttachAborted) {
			window->DetachMenu();
			// TODO: Probably not needed, we can just let _hide() quit the window
			if (ourWindow)
				window->Quit();
			else
				window->Unlock();
			return false;
		}

		// Move the BMenu to 1, 1, if it's attached to a BMenuWindow,
		// (that means it's a BMenu, BMenuBars are attached to regular BWindows).
		// This is needed to be able to draw the frame around the BMenu.
		if (dynamic_cast<BMenuWindow *>(window) != NULL)
			MoveTo(1, 1);

		_UpdateWindowViewSize(true);
		window->Show();

		if (selectFirstItem)
			_SelectItem(ItemAt(0));

		window->Unlock();
	}

	return true;
}


void
BMenu::_Hide()
{
	BMenuWindow *window = dynamic_cast<BMenuWindow *>(Window());
	if (window == NULL || !window->Lock())
		return;

	if (fSelected != NULL)
		_SelectItem(NULL);

	window->Hide();
	window->DetachMenu();
		// we don't want to be deleted when the window is removed

#if USE_CACHED_MENUWINDOW
	if (fSuper != NULL)
		window->Unlock();
	else
#endif
		window->Quit();
		// it's our window, quit it


	// Delete the menu window used by our submenus
	_DeleteMenuWindow();
}


const bigtime_t kHysteresis = 200000; // TODO: Test and reduce if needed.


BMenuItem *
BMenu::_Track(int *action, long start)
{
	// TODO: cleanup
	BMenuItem *item = NULL;
	bigtime_t openTime = system_time();
	bigtime_t closeTime = 0;

	fState = MENU_STATE_TRACKING;
	if (fSuper != NULL)
		fSuper->fState = MENU_STATE_TRACKING_SUBMENU;

	BPoint location;
	uint32 buttons;
	if (LockLooper()) {	
		GetMouse(&location, &buttons);
		UnlockLooper();	
	}

	bool releasedOnce = buttons == 0;
	while (fState != MENU_STATE_CLOSED) {
		if (_CustomTrackingWantsToQuit())
			break;

		if (!LockLooper())
			break;
		
		BMenuWindow *window = static_cast<BMenuWindow *>(Window());
		BPoint screenLocation = ConvertToScreen(location);
		if (window->CheckForScrolling(screenLocation)) {
			UnlockLooper();		
			continue;
		}
	
		item = _HitTestItems(location, B_ORIGIN);
		if (item != NULL) {
			_UpdateStateOpenSelect(item, openTime, closeTime);
			if (!releasedOnce)
				releasedOnce = true;
		
		} else if (_OverSubmenu(fSelected, screenLocation)) {
			// Since the submenu has its own looper,
			// we can unlock ours. Doing so also make sure
			// that our window gets any update message to
			// redraw itself
			UnlockLooper();
			int submenuAction = MENU_STATE_TRACKING;
			BMenu *submenu = fSelected->Submenu();
			submenu->_SetStickyMode(_IsStickyMode());

			// The following call blocks until the submenu
			// gives control back to us, either because the mouse
			// pointer goes out of the submenu's bounds, or because
			// the user closes the menu 
			BMenuItem *submenuItem = submenu->_Track(&submenuAction);
			if (submenuAction == MENU_STATE_CLOSED) {
				item = submenuItem;
				fState = MENU_STATE_CLOSED;			
			}
			if (!LockLooper())
				break;
		} else if (_OverSuper(screenLocation)) {
			fState = MENU_STATE_TRACKING;			
			UnlockLooper();
			break;			
		} else {
			// Mouse pointer outside menu

			if (system_time() > closeTime + kHysteresis
				&& fState != MENU_STATE_TRACKING_SUBMENU) {
				_SelectItem(NULL);
				fState = MENU_STATE_TRACKING;
			}

			if (fSuper != NULL) {
				// Give supermenu the chance to continue tracking
				*action = fState;
				UnlockLooper();
				return NULL;
			}
		}

		UnlockLooper();

		if (fState != MENU_STATE_CLOSED) {
			bigtime_t snoozeAmount = 50000;
			snooze(snoozeAmount);

			BPoint newLocation;
			uint32 newButtons;
			if (LockLooper()) {	
				GetMouse(&newLocation, &newButtons, true);
				UnlockLooper();
			}

			if (newLocation != location || newButtons != buttons) {
				if (!releasedOnce && newButtons == 0 && buttons != 0)
					releasedOnce = true;				
				location = newLocation;
				buttons = newButtons;
			}
		
			if (releasedOnce)
				_UpdateStateClose(item, location, buttons);
		}	
	}

	if (action != NULL)
		*action = fState;

	if (fSelected != NULL && LockLooper()) {
		_SelectItem(NULL);
		UnlockLooper();
	}

	// delete the menu window recycled for all the child menus
	_DeleteMenuWindow();

	return item;
}


void
BMenu::_UpdateStateOpenSelect(BMenuItem* item, bigtime_t& openTime,
	bigtime_t& closeTime)
{
	if (fState == MENU_STATE_CLOSED)
		return;

	if (item != fSelected && system_time() > closeTime + kHysteresis) {
		_SelectItem(item, false);
		openTime = system_time();
	} else if (system_time() > kHysteresis + openTime && item->Submenu() != NULL
		&& item->Submenu()->Window() == NULL) {
		// Open the submenu if it's not opened yet, but only if
		// the mouse pointer stayed over there for some time
		// (hysteresis)
		_SelectItem(item);
		closeTime = system_time();
	}
	if (fState != MENU_STATE_TRACKING)
		fState = MENU_STATE_TRACKING;
}


void
BMenu::_UpdateStateClose(BMenuItem* item, const BPoint& where,
	const uint32& buttons)
{
	if (fState == MENU_STATE_CLOSED)
		return;

	if (buttons != 0 && _IsStickyMode()) {
		if (item == NULL)
			fState = MENU_STATE_CLOSED;
		else
			_SetStickyMode(false);
	} else if (buttons == 0 && !_IsStickyMode()) {
		if (fExtraRect != NULL && fExtraRect->Contains(where)) {
			_SetStickyMode(true);
			fExtraRect = NULL;
				// Setting this to NULL will prevent this code
				// to be executed next time
		} else
			fState = MENU_STATE_CLOSED;
	}
}


bool
BMenu::_AddItem(BMenuItem *item, int32 index)
{
	ASSERT(item != NULL);
	if (index < 0 || index > fItems.CountItems())
		return false;

	if (!fItems.AddItem(item, index))
		return false;

	// install the item on the supermenu's window
	// or onto our window, if we are a root menu
	BWindow* window = NULL;
	if (Superitem() != NULL)
		window = Superitem()->fWindow;
	else
		window = Window();
	if (window != NULL)
		item->Install(window);

	item->SetSuper(this);
	return true;
}


bool
BMenu::_RemoveItems(int32 index, int32 count, BMenuItem *item, bool deleteItems)
{
	bool success = false;
	bool invalidateLayout = false;

	bool locked = LockLooper();
	BWindow *window = Window();

	// The plan is simple: If we're given a BMenuItem directly, we use it
	// and ignore index and count. Otherwise, we use them instead.
	if (item != NULL) {
		if (fItems.RemoveItem(item)) {
			if (item == fSelected && window != NULL)
				_SelectItem(NULL);
			item->Uninstall();
			item->SetSuper(NULL);
			if (deleteItems)
				delete item;
			success = invalidateLayout = true;
		}
	} else {
		// We iterate backwards because it's simpler
		int32 i = min_c(index + count - 1, fItems.CountItems() - 1);
		// NOTE: the range check for "index" is done after
		// calculating the last index to be removed, so
		// that the range is not "shifted" unintentionally
		index = max_c(0, index);
		for (; i >= index; i--) {
			item = static_cast<BMenuItem*>(fItems.ItemAt(i));
			if (item != NULL) {
				if (fItems.RemoveItem(item)) {
					if (item == fSelected && window != NULL)
						_SelectItem(NULL);
					item->Uninstall();
					item->SetSuper(NULL);
					if (deleteItems)
						delete item;
					success = true;
					invalidateLayout = true;
				} else {
					// operation not entirely successful
					success = false;
					break;
				}
			}
		}
	}

	if (invalidateLayout) {
		InvalidateLayout();
		if (locked && window != NULL) {
			_LayoutItems(0);
			_UpdateWindowViewSize(false);
			Invalidate();
		}
	}

	if (locked)
		UnlockLooper();

	return success;
}


bool
BMenu::_RelayoutIfNeeded()
{
	if (!fUseCachedMenuLayout) {
		fUseCachedMenuLayout = true;
		_CacheFontInfo();
		_LayoutItems(0);
		return true;
	}
	return false;
}


void
BMenu::_LayoutItems(int32 index)
{
	_CalcTriggers();

	float width, height;
	_ComputeLayout(index, fResizeToFit, true, &width, &height);

	if (fResizeToFit)
		ResizeTo(width, height);
}


BSize
BMenu::_ValidatePreferredSize()
{
	if (!fLayoutData->preferred.IsWidthSet())
		_ComputeLayout(0, true, false, NULL, NULL);

	return fLayoutData->preferred;
}


void
BMenu::_ComputeLayout(int32 index, bool bestFit, bool moveItems,
	float* _width, float* _height)
{
	// TODO: Take "bestFit", "moveItems", "index" into account,
	// Recalculate only the needed items,
	// not the whole layout every time

	BRect frame;

	switch (fLayout) {
		case B_ITEMS_IN_COLUMN:
			_ComputeColumnLayout(index, bestFit, moveItems, frame);
			break;

		case B_ITEMS_IN_ROW:
			_ComputeRowLayout(index, bestFit, moveItems, frame);
			break;

		case B_ITEMS_IN_MATRIX:
			_ComputeMatrixLayout(frame);
			break;

		default:
			break;
	}

	// change width depending on resize mode
	BSize size;
	if ((ResizingMode() & B_FOLLOW_LEFT_RIGHT) == B_FOLLOW_LEFT_RIGHT) {
		if (Parent())
			size.width = Parent()->Frame().Width() + 1;
		else if (Window())
			size.width = Window()->Frame().Width() + 1;
		else
			size.width = Bounds().Width();
	} else
		size.width = frame.Width();

	size.height = frame.Height();

	if (_width)
		*_width = size.width;

	if (_height)
		*_height = size.height;

	if (bestFit)
		fLayoutData->preferred = size;

	if (moveItems)
		fUseCachedMenuLayout = true;
}


void
BMenu::_ComputeColumnLayout(int32 index, bool bestFit, bool moveItems,
	BRect& frame)
{
	BFont font;
	GetFont(&font);
	bool command = false;
	bool control = false;
	bool shift = false;

	if (index > 0)
		frame = ItemAt(index - 1)->Frame();
	else
		frame.Set(0, 0, 0, 0);

	for (; index < fItems.CountItems(); index++) {
		BMenuItem *item = ItemAt(index);

		float width, height;
		item->GetContentSize(&width, &height);

		if (item->fModifiers && item->fShortcutChar) {
			width += font.Size();
			if (item->fModifiers & B_COMMAND_KEY)
				command = true;
			if (item->fModifiers & B_CONTROL_KEY)
				control = true;
			if (item->fModifiers & B_SHIFT_KEY)
				shift = true;
		}

		item->fBounds.left = 0.0f;
		item->fBounds.top = frame.bottom + (index > 0 ? 1.0f : 0.0f);
		item->fBounds.bottom = item->fBounds.top + height + fPad.top
			+ fPad.bottom;

		if (item->fSubmenu != NULL)
			width += item->Frame().Height();

		frame.right = max_c(frame.right, width + fPad.left + fPad.right);
		frame.bottom = item->fBounds.bottom;
	}

	if (command)
		frame.right += 17;
	if (control)
		frame.right += 17;
	if (shift)
		frame.right += 22;

	if (fMaxContentWidth > 0)
		frame.right = min_c(frame.right, fMaxContentWidth);

	if (moveItems) {
		for (int32 i = 0; i < fItems.CountItems(); i++)
			ItemAt(i)->fBounds.right = frame.right;
	}

	frame.top = 0;
	frame.right = ceilf(frame.right);
}


void
BMenu::_ComputeRowLayout(int32 index, bool bestFit, bool moveItems,
	BRect& frame)
{
	font_height fh;
	GetFontHeight(&fh);
	frame.Set(0.0f, 0.0f, 0.0f, ceilf(fh.ascent + fh.descent + fPad.top
		+ fPad.bottom));

	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem *item = ItemAt(i);
		float width, height;
		if (item != NULL) {
			item->GetContentSize(&width, &height);

			item->fBounds.left = frame.right;
			item->fBounds.top = 0.0f;
			item->fBounds.right = item->fBounds.left + width + fPad.left
				+ fPad.right;

			frame.right = item->Frame().right + 1.0f;
			frame.bottom = max_c(frame.bottom, height + fPad.top + fPad.bottom);
		}
	}

	if (moveItems) {
		for (int32 i = 0; i < fItems.CountItems(); i++)
			ItemAt(i)->fBounds.bottom = frame.bottom;
	}

	if (bestFit)
		frame.right = ceilf(frame.right);
	else
		frame.right = Bounds().right;
}


void
BMenu::_ComputeMatrixLayout(BRect &frame)
{
	frame.Set(0, 0, 0, 0);
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem *item = ItemAt(i);
		if (item != NULL) {
			frame.left = min_c(frame.left, item->Frame().left);
			frame.right = max_c(frame.right, item->Frame().right);
			frame.top = min_c(frame.top, item->Frame().top);
			frame.bottom = max_c(frame.bottom, item->Frame().bottom);
		}
	}
}


// Assumes the SuperMenu to be locked (due to calling ConvertToScreen())
BPoint
BMenu::ScreenLocation()
{
	BMenu *superMenu = Supermenu();
	BMenuItem *superItem = Superitem();

	if (superMenu == NULL || superItem == NULL) {
		debugger("BMenu can't determine where to draw."
			"Override BMenu::ScreenLocation() to determine location.");
	}

	BPoint point;
	if (superMenu->Layout() == B_ITEMS_IN_COLUMN)
		point = superItem->Frame().RightTop() + BPoint(1.0f, 1.0f);
	else
		point = superItem->Frame().LeftBottom() + BPoint(1.0f, 1.0f);

	superMenu->ConvertToScreen(&point);

	return point;
}


BRect
BMenu::_CalcFrame(BPoint where, bool *scrollOn)
{
	// TODO: Improve me
	BRect bounds = Bounds();
	BRect frame = bounds.OffsetToCopy(where);

	BScreen screen(Window());
	BRect screenFrame = screen.Frame();

	BMenu *superMenu = Supermenu();
	BMenuItem *superItem = Superitem();

	if (scrollOn != NULL) {
		// basically, if this returns false, it means
		// that the menu frame won't fit completely inside the screen
		*scrollOn = !screenFrame.Contains(bounds);
	}

	// TODO: Horrible hack:
	// When added to a BMenuField, a BPopUpMenu is the child of
	// a _BMCMenuBar_ to "fake" the menu hierarchy
	if (superMenu == NULL || superItem == NULL
		|| dynamic_cast<_BMCMenuBar_ *>(superMenu) != NULL) {
		// just move the window on screen

		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
		else if (frame.top < screenFrame.top)
			frame.OffsetBy(0, -frame.top);

		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
		else if (frame.left < screenFrame.left)
			frame.OffsetBy(-frame.left, 0);

		return frame;
	}

	if (superMenu->Layout() == B_ITEMS_IN_COLUMN) {
		if (frame.right > screenFrame.right)
			frame.OffsetBy(-superItem->Frame().Width() - frame.Width() - 2, 0);

		if (frame.left < 0)
			frame.OffsetBy(-frame.left + 6, 0);

		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	} else {
		if (frame.bottom > screenFrame.bottom) {
			if (scrollOn != NULL && superMenu != NULL
				&& dynamic_cast<BMenuBar *>(superMenu) != NULL
				&& frame.top < (screenFrame.bottom - 80)) {
				*scrollOn = true;
			} else {
				frame.OffsetBy(0, -superItem->Frame().Height() - frame.Height() - 3);
			}
		}

		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
	}

	return frame;
}


void
BMenu::_DrawItems(BRect updateRect)
{
	int32 itemCount = fItems.CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem *item = ItemAt(i);
		if (item->Frame().Intersects(updateRect))
			item->Draw();
	}
}


int
BMenu::State(BMenuItem **item) const
{
	if (fState == MENU_STATE_TRACKING || fState == MENU_STATE_CLOSED)
		return fState;

	if (fSelected != NULL && fSelected->Submenu() != NULL)
		return fSelected->Submenu()->State(item);

	return fState;
}


void
BMenu::InvokeItem(BMenuItem *item, bool now)
{
	if (!item->IsEnabled())
		return;

	// Do the "selected" animation
	// TODO: Doesn't work. This is supposed to highlight
	// and dehighlight the item, works on beos but not on haiku.
	if (!item->Submenu() && LockLooper()) {
		snooze(50000);
		item->Select(true);
		Sync();
		snooze(50000);
		item->Select(false);
		Sync();
		snooze(50000);
		item->Select(true);
		Sync();
		snooze(50000);
		item->Select(false);
		Sync();
		UnlockLooper();
	}

	item->Invoke();
}


bool
BMenu::_OverSuper(BPoint location)
{
	if (!Supermenu())
		return false;

	return fSuperbounds.Contains(location);
}


bool
BMenu::_OverSubmenu(BMenuItem *item, BPoint loc)
{
	if (item == NULL)
		return false;

	BMenu *subMenu = item->Submenu();
	if (subMenu == NULL || subMenu->Window() == NULL)
		return false;

	// we assume that loc is in screen coords
	if (subMenu->Window()->Frame().Contains(loc))
		return true;

	return subMenu->_OverSubmenu(subMenu->fSelected, loc);
}


BMenuWindow *
BMenu::_MenuWindow()
{
#if USE_CACHED_MENUWINDOW
	if (fCachedMenuWindow == NULL) {
		char windowName[64];
		snprintf(windowName, 64, "%s cached menu", Name());
		fCachedMenuWindow = new (nothrow) BMenuWindow(windowName);
	}
#endif
	return fCachedMenuWindow;
}


void
BMenu::_DeleteMenuWindow()
{
	if (fCachedMenuWindow != NULL) {
		fCachedMenuWindow->Lock();
		fCachedMenuWindow->Quit();
		fCachedMenuWindow = NULL;
	}
}


BMenuItem *
BMenu::_HitTestItems(BPoint where, BPoint slop) const
{
	// TODO: Take "slop" into account ?

	// if the point doesn't lie within the menu's
	// bounds, bail out immediately
	if (!Bounds().Contains(where))
		return NULL;

	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem *item = ItemAt(i);
		if (item->Frame().Contains(where))
			return item;
	}

	return NULL;
}


BRect
BMenu::_Superbounds() const
{
	return fSuperbounds;
}


void
BMenu::_CacheFontInfo()
{
	font_height fh;
	GetFontHeight(&fh);
	fAscent = fh.ascent;
	fDescent = fh.descent;
	fFontHeight = ceilf(fh.ascent + fh.descent + fh.leading);
}


void
BMenu::_ItemMarked(BMenuItem *item)
{
	if (IsRadioMode()) {
		for (int32 i = 0; i < CountItems(); i++) {
			if (ItemAt(i) != item)
				ItemAt(i)->SetMarked(false);
		}
		InvalidateLayout();
	}

	if (IsLabelFromMarked() && Superitem())
		Superitem()->SetLabel(item->Label());
}


void
BMenu::_Install(BWindow *target)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Install(target);
}


void
BMenu::_Uninstall()
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Uninstall();
}


void
BMenu::_SelectItem(BMenuItem* menuItem, bool showSubmenu, bool selectFirstItem)
{
	// Avoid deselecting and then reselecting the same item
	// which would cause flickering
	if (menuItem != fSelected) {
		if (fSelected != NULL) {
			fSelected->Select(false);
			BMenu *subMenu = fSelected->Submenu();
			if (subMenu != NULL && subMenu->Window() != NULL)
				subMenu->_Hide();
		}

		fSelected = menuItem;
		if (fSelected != NULL)
			fSelected->Select(true);
	}

	if (fSelected != NULL && showSubmenu) {
		BMenu *subMenu = fSelected->Submenu();
		if (subMenu != NULL && subMenu->Window() == NULL) {
			if (!subMenu->_Show(selectFirstItem)) {
				// something went wrong, deselect the item
				fSelected->Select(false);
				fSelected = NULL;
			}
		}
	}
}


bool
BMenu::_SelectNextItem(BMenuItem *item, bool forward)
{
	if (CountItems() == 0) // cannot select next item in an empty menu
		return false;

	BMenuItem *nextItem = _NextItem(item, forward);
	if (nextItem == NULL)
		return false;

	bool openMenu = false;
	if (dynamic_cast<BMenuBar *>(this) != NULL)
		openMenu = true;
	_SelectItem(nextItem, openMenu);
	return true;
}


BMenuItem *
BMenu::_NextItem(BMenuItem *item, bool forward) const
{
	// go to next item, and skip over disabled items such as separators
 	int32 index = fItems.IndexOf(item);
	if (index < 0)
		index = 0;

	const int32 numItems = fItems.CountItems();
	int32 startIndex = index;
	do {
		if (forward)
			index++;
		else
			index--;
 		
		// cycle through menu items
		if (index < 0)
			index = numItems - 1;
		else if (index >= numItems)
			index = 0;
	} while (!ItemAt(index)->IsEnabled() && index != startIndex);

	if (index == startIndex) // we are back where we started and no item was enabled
		return false;

	return ItemAt(index);
}


void
BMenu::_SetIgnoreHidden(bool on)
{
	fIgnoreHidden = on;
}


void
BMenu::_SetStickyMode(bool on)
{
	if (fStickyMode == on)
		return;
	
	fStickyMode = on;

	// If we are switching to sticky mode, propagate the status
	// back to the super menu
	if (fSuper != NULL)
		fSuper->_SetStickyMode(on);
	else {
		// TODO: Ugly hack, but it needs to be done right here in this method
		BMenuBar *menuBar = dynamic_cast<BMenuBar *>(this);
		if (on && menuBar != NULL && menuBar->LockLooper()) {
			// Steal the focus from the current focus view
			// (needed to handle keyboard navigation)
			menuBar->_StealFocus();
			menuBar->UnlockLooper();
		}
	}
}


bool
BMenu::_IsStickyMode() const
{
	return fStickyMode;
}


void
BMenu::_CalcTriggers()
{
	BPrivate::TriggerList triggerList;

	// Gathers the existing triggers set by the user
	for (int32 i = 0; i < CountItems(); i++) {
		char trigger = ItemAt(i)->Trigger();
		if (trigger != 0)
			triggerList.AddTrigger(trigger);
	}

	// Set triggers for items which don't have one yet
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem *item = ItemAt(i);
		if (item->Trigger() == 0) {
			uint32 trigger;
			int32 index;
			if (_ChooseTrigger(item->Label(), index, trigger, triggerList))
				item->SetAutomaticTrigger(index, trigger);
		}
	}
}


bool
BMenu::_ChooseTrigger(const char *title, int32& index, uint32& trigger,
	BPrivate::TriggerList& triggers)
{
	if (title == NULL)
		return false;

	uint32 c;

	// two runs: first we look out for uppercase letters
	// TODO: support Unicode characters correctly!
	for (uint32 i = 0; (c = title[i]) != '\0'; i++) {
		if (!IsInsideGlyph(c) && isupper(c) && !triggers.HasTrigger(c)) {
			index = i;
			trigger = tolower(c);
			return triggers.AddTrigger(c);
		}
	}

	// then, if we still haven't found anything, we accept them all
	index = 0;
	while ((c = UTF8ToCharCode(&title)) != 0) {
		if (!isspace(c) && !triggers.HasTrigger(c)) {
			trigger = tolower(c);
			return triggers.AddTrigger(c);
		}

		index++;
	}

	return false;
}


void
BMenu::_UpdateWindowViewSize(bool updatePosition)
{
	BMenuWindow *window = static_cast<BMenuWindow *>(Window());
	if (window == NULL)
		return;

	if (dynamic_cast<BMenuBar *>(this) != NULL)
		return;

	if (!fResizeToFit)
		return;

	bool scroll;
	const BPoint screenLocation = updatePosition ? ScreenLocation()
		: window->Frame().LeftTop();
	BRect frame = _CalcFrame(screenLocation, &scroll);
	ResizeTo(frame.Width(), frame.Height());

	if (fItems.CountItems() > 0) {
		if (!scroll) {
			window->ResizeTo(Bounds().Width() + 2, Bounds().Height() + 2);
		} else {
			BScreen screen(window);

			// If we need scrolling, resize the window to fit the screen and
			// attach scrollers to our cached BMenuWindow.
			if (dynamic_cast<BMenuBar *>(Supermenu()) == NULL) {
				window->ResizeTo(Bounds().Width() + 2, screen.Frame().bottom);
				frame.top = 0;
			} else {
				// Or, in case our parent was a BMenuBar enable scrolling with
				// normal size.
				window->ResizeTo(Bounds().Width() + 2, screen.Frame().bottom
					- frame.top);
			}

			window->AttachScrollers();
		}
	} else {
		_CacheFontInfo();
		window->ResizeTo(StringWidth(kEmptyMenuLabel) + fPad.left + fPad.right,
			fFontHeight + fPad.top + fPad.bottom);
	}

	if (updatePosition)
		window->MoveTo(frame.LeftTop());
}


bool
BMenu::_OkToProceed(BMenuItem* item)
{
	BPoint where;
	ulong buttons;
	GetMouse(&where, &buttons, false);
	bool stickyMode = _IsStickyMode();
	// Quit if user clicks the mouse button in sticky mode
	// or releases the mouse button in nonsticky mode
	// or moves the pointer over another item
	// TODO: I added the check for BMenuBar to solve a problem with Deskbar.
	// BeOS seems to do something similar. This could also be a bug in Deskbar, though.
	if ((buttons != 0 && stickyMode)
		|| (dynamic_cast<BMenuBar *>(this) == NULL
			&& (buttons == 0 && !stickyMode) || _HitTestItems(where) != item))
		return false;

	return true;
}


bool
BMenu::_CustomTrackingWantsToQuit()
{
	if (fExtraMenuData != NULL && fExtraMenuData->trackingHook != NULL
		&& fExtraMenuData->trackingState != NULL) {
		return fExtraMenuData->trackingHook(this, fExtraMenuData->trackingState);
	}

	return false;
}


void
BMenu::QuitTracking(bool onlyThis)
{
	_SelectItem(NULL);
	if (BMenuBar *menuBar = dynamic_cast<BMenuBar *>(this))
		menuBar->_RestoreFocus();

	fChosenItem = NULL;
	fState = MENU_STATE_CLOSED;

	// Close the whole menu hierarchy
	if (!onlyThis && _IsStickyMode())
		_SetStickyMode(false);	

	_Hide();
}


//	#pragma mark -


// TODO: Maybe the following two methods would fit better into InterfaceDefs.cpp
// In R5, they do all the work client side, we let the app_server handle the details.
status_t
set_menu_info(menu_info *info)
{
	if (!info)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_MENU_INFO);
	link.Attach<menu_info>(*info);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		BMenu::sMenuInfo = *info;
		// Update also the local copy, in case anyone relies on it

	return status;
}


status_t
get_menu_info(menu_info *info)
{
	if (!info)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_MENU_INFO);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<menu_info>(info);

	return status;
}
