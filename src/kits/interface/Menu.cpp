//------------------------------------------------------------------------------
//	Copyright (c) 2001-2005, Haiku, Inc.
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		Menu.cpp
//	Authors:		Marc Flerackers (mflerackers@androme.be)
//					Stefano Ceccherini (burton666@libero.it)
//	Description:	BMenu display a menu of selectable items.
//------------------------------------------------------------------------------
#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Window.h>

#include <MenuWindow.h>

#ifndef COMPILE_FOR_R5
menu_info BMenu::sMenuInfo;
#endif

class _ExtraMenuData_ {
public:
	menu_tracking_hook trackingHook;
	void *trackingState;

	_ExtraMenuData_(menu_tracking_hook func, void *state)
	{
		trackingHook = func;
		trackingState = state;
	};
};


static property_info
sPropList[] = {
	{ "Enabled", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if menu or menu item is enabled; false "
		"otherwise." },
	
	{ "Enabled", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Enables or disables menu or menu item." },
	
	{ "Label", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the string label of the menu or menu item." },
	
	{ "Label", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the string label of the menu or menu item." },
	
	{ "Mark", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if the menu item or the menu's superitem "
		"is marked; false otherwise." },
	
	{ "Mark", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Marks or unmarks the menu item or the menu's superitem." },
	
	{ "Menu", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label found in \"data\" "
		"and the int32 command found in \"what\" (used as the what field in the CMessage "
		"sent by the item)." },
	
	{ "Menu", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the selected menu or menus." },

	{ "Menu", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the current "
		"specifier off the stack." },
	
	{ "MenuItem", { B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Counts the number of menu items in the specified menu." },
	
	{ "MenuItem", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label found in \"data\" "
		"and the int32 command found in \"what\" (used as the what field in the CMessage "
		"sent by the item)." },
	
	{ "MenuItem", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the specified menu item from its parent menu." },
	
	{ "MenuItem", { B_EXECUTE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Invokes the specified menu item." },
	
	{ "MenuItem", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the current "
		"specifier off the stack." },
	{}
};


BMenu::BMenu(const char *name, menu_layout layout)
	:	BView(BRect(), name, 0,	B_WILL_DRAW),
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
		fUseCachedMenuLayout(true),
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
	InitData(NULL);
}


BMenu::BMenu(const char *name, float width, float height)
	:	BView(BRect(0.0f, width, 0.0f, height), name, 0, B_WILL_DRAW),
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
		fUseCachedMenuLayout(true),
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
	InitData(NULL);
}


BMenu::~BMenu()
{
	RemoveItems(0, CountItems(), true);
	
	delete fCachedMenuWindow;
	delete fInitMatrixSize;
	delete fExtraMenuData;
}


BMenu::BMenu(BMessage *archive)
	:	BView(archive)
{
	InitData(archive);
}


BArchivable *
BMenu::Instantiate(BMessage *data)
{
	if (validate_instantiation(data, "BMenu"))
		return new BMenu(data);
	else
		return NULL;
}


status_t
BMenu::Archive(BMessage *data, bool deep) const
{
	status_t err = BView::Archive(data, deep);

	if (err < B_OK)
		return err;

	if (Layout() != B_ITEMS_IN_ROW) {
		err = data->AddInt32("_layout", Layout());

		if (err < B_OK)
			return err;
	}

	err = data->AddBool("_rsize_to_fit", fResizeToFit);

	if (err < B_OK)
		return err;

	err = data->AddBool("_disable", !IsEnabled());

	if (err < B_OK)
		return err;

	err = data->AddBool("_radio", IsRadioMode());

	if (err < B_OK)
		return err;

	err = data->AddBool("_trig_disabled", AreTriggersEnabled());

	if (err < B_OK)
		return err;

	err = data->AddBool("_dyn_label", fDynamicName);

	if (err < B_OK)
		return err;

	err = data->AddFloat("_maxwidth", fMaxContentWidth);

	if (err < B_OK)
		return err;

	if (deep) {
		// TODO store items and rects
	}

	return err;
}


void
BMenu::AttachedToWindow()
{
	BView::AttachedToWindow();
	
	InvalidateLayout();
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
		debugger("BMenu::AddItem(BMenuItem *, int32) this method can only"
				"be called if the menu layout is not B_ITEMS_IN_MATRIX");

	return _AddItem(item, index);
}


bool
BMenu::AddItem(BMenuItem *item, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenuItem *, BRect) this method can only"
			" be called if the menu layout is B_ITEMS_IN_MATRIX");
			
	if (!item)
		return false;
	
	item->fBounds = frame;
	
	return _AddItem(item, CountItems());
}


bool 
BMenu::AddItem(BMenu *submenu)
{
	BMenuItem *item = new BMenuItem(submenu);
	if (!item)
		return false;
	
	return _AddItem(item, CountItems());
}


bool
BMenu::AddItem(BMenu *submenu, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenuItem *, int32) this method can only"
				"be called if the menu layout is not B_ITEMS_IN_MATRIX");

	BMenuItem *item = new BMenuItem(submenu);
	if (!item)
		return false;
		
	return _AddItem(item, index);
}


bool
BMenu::AddItem(BMenu *submenu, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX)
		debugger("BMenu::AddItem(BMenu *, BRect) this method can only"
			" be called if the menu layout is B_ITEMS_IN_MATRIX");
	
	BMenuItem *item = new BMenuItem(submenu);
	item->fBounds = frame;
	
	return _AddItem(item, CountItems());
}


bool
BMenu::AddList(BList *list, int32 index)
{
	// TODO: test this function, it's not documented in the bebook.
	int32 numItems = 0;
	if (list != NULL)
		numItems = list->CountItems();
	
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem *item = static_cast<BMenuItem *>(list->ItemAt(i));
		if (item != NULL)
			_AddItem(item, index + i);
	}	
	
	// TODO: return false if needed
	return true;
}


bool
BMenu::AddSeparatorItem()
{
	BMenuItem *item = new BSeparatorItem();	
	return _AddItem(item, CountItems());
}


bool
BMenu::RemoveItem(BMenuItem *item)
{
	// TODO: Check if item is also deleted
	return RemoveItems(0, 0, item, false);
}


BMenuItem *
BMenu::RemoveItem(int32 index)
{
	BMenuItem *item = ItemAt(index);
	RemoveItems(index, 1, NULL, false);
	return item;
}


bool
BMenu::RemoveItems(int32 index, int32 count, bool del)
{
	return RemoveItems(index, count, NULL, del);	
}


bool
BMenu::RemoveItem(BMenu *submenu)
{
	for (int i = 0; i < fItems.CountItems(); i++)
		if (static_cast<BMenuItem *>(fItems.ItemAt(i))->Submenu() == submenu)
			return RemoveItems(i, 1, NULL, false);

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
	return static_cast<BMenuItem *>(fItems.ItemAt(index))->Submenu();
}


int32
BMenu::IndexOf(BMenuItem *item) const
{
	return fItems.IndexOf(item);
}


int32
BMenu::IndexOf(BMenu *submenu) const
{
	for (int32 i = 0; i < fItems.CountItems(); i++)
		if (static_cast<BMenuItem *>(fItems.ItemAt(i))->Submenu() == submenu)
			return i;

	return -1;
}


BMenuItem *
BMenu::FindItem(const char *label) const
{
	BMenuItem *item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Label() && strcmp(item->Label(), label) == 0)
			break;

		if (item->Submenu()) {
			item = item->Submenu()->FindItem(label);
			if (item)
				break;
		}
	}

	return item;
}


BMenuItem *
BMenu::FindItem(uint32 command) const
{
	BMenuItem *item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Command() == command)
			break;

		if (item->Submenu()) {
			item = item->Submenu()->FindItem(command);
			if (item)
				break;
		}
	}

	return item;
}


status_t 
BMenu::SetTargetForItems(BHandler *handler)
{
	for (int32 i = 0; i < fItems.CountItems(); i++)
		if (static_cast<BMenuItem *>(fItems.ItemAt(i))->SetTarget(handler) != B_OK)
			return B_ERROR;

	return B_OK;
}


status_t
BMenu::SetTargetForItems(BMessenger messenger)
{
	for (int32 i = 0; i < fItems.CountItems (); i++)
		if (ItemAt(i)->SetTarget(messenger) != B_OK)
			return B_ERROR;

	return B_OK;
}


void
BMenu::SetEnabled(bool enabled)
{
	fEnabled = enabled;

	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->SetEnabled(enabled);
}


void
BMenu::SetRadioMode(bool flag)
{
	fRadioMode = flag;
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
}


bool
BMenu::IsLabelFromMarked()
{
	return fDynamicName;
}


bool
BMenu::IsEnabled() const
{
	return fEnabled;
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
	for (int i = 0; i < fItems.CountItems(); i++)
		if (((BMenuItem*)fItems.ItemAt(i))->IsMarked())
			return (BMenuItem*)fItems.ItemAt(i);

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
	switch (bytes[0]) {
		case B_UP_ARROW:
		{
			if (fSelected) 	{
				fSelected->fSelected = false;

				if (fSelected == fItems.FirstItem())
					fSelected = static_cast<BMenuItem *>(fItems.LastItem());
				else
					fSelected = ItemAt(IndexOf(fSelected) - 1);
			} else
				fSelected = static_cast<BMenuItem *>(fItems.LastItem());

			fSelected->fSelected = true;

			break;
		}
		case B_DOWN_ARROW:
		{
			if (fSelected) {
				fSelected->fSelected = false;

				if (fSelected == fItems.LastItem())
					fSelected = static_cast<BMenuItem *>(fItems.FirstItem());
				else
					fSelected = ItemAt(IndexOf(fSelected) + 1);
			} else
				fSelected = static_cast<BMenuItem *>(fItems.FirstItem());

			fSelected->fSelected = true;

			break;
		}
		case B_HOME:
		{
			if (fSelected)
				fSelected->fSelected = false;

			fSelected = static_cast<BMenuItem *>(fItems.FirstItem());
			fSelected->fSelected = true;

			break;
		}
		case B_END:
		{
			if (fSelected)
				fSelected->fSelected = false;

			fSelected = static_cast<BMenuItem *>(fItems.LastItem());
			fSelected->fSelected = true;

			break;
		}
		case B_ENTER:
		case B_SPACE:
		{
			if (fSelected)
				InvokeItem(fSelected);

			break;
		}
		default:
			BView::KeyDown(bytes, numBytes);
	}
}


void
BMenu::Draw(BRect updateRect)
{
	DrawBackground(updateRect);
	DrawItems(updateRect);
}


void
BMenu::GetPreferredSize(float *width, float *height)
{
	ComputeLayout(0, true, false, width, height);
}


void
BMenu::ResizeToPreferred()
{
	BView::ResizeToPreferred();
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
	CacheFontInfo();
	LayoutItems(0);
	Invalidate();
}


BHandler *
BMenu::ResolveSpecifier(BMessage *msg, int32 index,
								  BMessage *specifier, int32 form,
								  const char *property)
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
	status_t err;

	if (data == NULL)
		return B_BAD_VALUE;

	err = data->AddString("suites", "suite/vnd.Be-menu");

	if (err < B_OK)
		return err;
	
	BPropertyInfo prop_info(sPropList);
	err = data->AddFlat("messages", &prop_info);

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
	:	BView(frame, name, resizingMode, flags),
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
		fUseCachedMenuLayout(true),
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
	InitData(NULL);
}


BPoint
BMenu::ScreenLocation()
{
	BMenu *superMenu = Supermenu();
	BMenuItem *superItem = Superitem();

	if (superMenu == NULL && superItem == NULL) {
		debugger("BMenu can't determine where to draw."
			"Override BMenu::ScreenLocation() to determine location.");
	}
	
	BPoint point;
	if (superMenu->Layout() == B_ITEMS_IN_COLUMN)
		point = superItem->Frame().RightTop();
	else
		point = superItem->Frame().LeftBottom() + BPoint(0.0f, 1.0f);
	
	superMenu->ConvertToScreen(&point);
	
	return point;
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
	_show(selectFirst);
}


void
BMenu::Hide()
{
	_hide();
}


BMenuItem *
BMenu::Track(bool openAnyway, BRect *clickToOpenRect)
{
	if (IsStickyPrefOn())
		openAnyway = false;
		
	SetStickyMode(openAnyway);
	
	if (LockLooper()) {
		RedrawAfterSticky(Bounds());
		UnlockLooper();
	}
	
	if (clickToOpenRect != NULL && LockLooper()) {
		fExtraRect = clickToOpenRect;
		ConvertFromScreen(fExtraRect);
		UnlockLooper();
	}

	int action;
	BMenuItem *menuItem = _track(&action, -1);
	
	SetStickyMode(false);
	fExtraRect = NULL;
	
	return menuItem;
}


bool
BMenu::AddDynamicItem(add_state s)
{
	// Implemented in subclasses
	return false;
}


void
BMenu::DrawBackground(BRect update)
{
	BRect rect = Bounds() & update;
	rgb_color oldColor = HighColor();
	
	SetHighColor(sMenuInfo.background_color);
	FillRect(rect, B_SOLID_HIGH);
	
	SetHighColor(oldColor);
}


void
BMenu::SetTrackingHook(menu_tracking_hook func, void *state)
{
	delete fExtraMenuData;
	fExtraMenuData = new _ExtraMenuData_(func, state);
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
BMenu::InitData(BMessage *data)
{
	// TODO: Get _color, _fname, _fflt from the message, if present
	BFont font;
	font.SetFamilyAndStyle(sMenuInfo.f_family, sMenuInfo.f_style);
	font.SetSize(sMenuInfo.font_size);
	SetFont(&font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
	
	SetLowColor(sMenuInfo.background_color);
	SetViewColor(sMenuInfo.background_color);
				
	if (data != NULL) {
		data->FindInt32("_layout", (int32 *)&fLayout);
		data->FindBool("_rsize_to_fit", &fResizeToFit);
		data->FindBool("_disable", &fEnabled);
		data->FindBool("_radio", &fRadioMode);
		
		bool disableTrigger = false;
		data->FindBool("_trig_disabled", &disableTrigger);
		fTriggerEnabled = !disableTrigger;
		
		data->FindBool("_dyn_label", &fDynamicName);
		data->FindFloat("_maxwidth", &fMaxContentWidth);
	}
}


bool
BMenu::_show(bool selectFirstItem)
{
	// Menu windows get the BMenu's handler name
	fCachedMenuWindow = new BMenuWindow(Name());
	
	fCachedMenuWindow->ChildAt(0)->AddChild(this);
	
	// We're doing this here because ConvertToScreen() needs:
	// 1. The BView to be attached (see the above line).
	// 2. The looper locked or not running (the Show() call below starts the looper)
	if (fSuper != NULL)
		fSuperbounds = fSuper->ConvertToScreen(fSuper->Bounds());
	fCachedMenuWindow->ResizeTo(Bounds().Width() + 4, Bounds().Height() + 4);
	fCachedMenuWindow->MoveTo(ScreenLocation());
	fCachedMenuWindow->Show();
	
	return true;
}


void
BMenu::_hide()
{
	if (fCachedMenuWindow != NULL) {
		fCachedMenuWindow->Lock();
		fCachedMenuWindow->ChildAt(0)->RemoveChild(this);
		fCachedMenuWindow->Quit();
		fCachedMenuWindow = NULL;
	}
	
}


BMenuItem *
BMenu::_track(int *action, long start)
{
	// TODO: Take Sticky mode into account, handle submenus
	BPoint location;
	ulong buttons;
	BMenuItem *item = NULL;
	do {
		if (LockLooper()) {
			GetMouse(&location, &buttons);
			if (OverSuper(location)) {
				UnlockLooper();
				break;
			}
					
			item = HitTestItems(location, B_ORIGIN);
						
			// TODO: Sometimes the menu flickers a bit.
			// try to be smarter and suggest an update area, 
			// instead of invalidating the whole view.
			if (item != NULL && item != fSelected) {
				SelectItem(item);
				Invalidate();
			}
																	
			UnlockLooper();
		}
		
		snooze(50000);
	} while (buttons != 0);
	
	// TODO: A deeper investigation of actions
	// would be nice. Consider building an enum
	// with the possible actions, and putting it in a
	// private, shared header (BMenuBar needs to know about them too).
	if (action != NULL) {
		if (buttons != 0)
			*action = 0;
		else 
			*action = 5;
	}
	
	if (LockLooper()) {
		SelectItem(NULL);
		UnlockLooper();
	}
		
	return item;
}


bool
BMenu::_AddItem(BMenuItem *item, int32 index)
{
	ASSERT(item != NULL);
	
	item->SetSuper(this);

	bool err = fItems.AddItem(item, index);

	if (!err)
		return err;

	// Make sure we update the layout in case we are already attached.
	if (Window() && fResizeToFit) {
		LayoutItems(index);
		Invalidate();
	}

	// Find the root menu window, so we can install this item.
	BMenu *root = this;
	while (root->Supermenu())
		root = root->Supermenu();

	if (root->Window())
		Install(root->Window());
	
	return err;
}


bool
BMenu::RemoveItems(int32 index, int32 count, BMenuItem *_item, bool del)
{
	bool result = false;
	 
	// The plan is simple: If we're given a BMenuItem directly, we use it
	// and ignore index and count. Otherwise, we use them instead.
	if (_item != NULL) {
		fItems.RemoveItem(_item);
		_item->SetSuper(NULL);
		_item->Uninstall();	
		if (del)
			delete _item;
		result = true;
	} else {
		BMenuItem *item = NULL;	
		// We iterate backwards because it's simpler
		// TODO: We should check if index and count are in bounds.
		for (int32 i = index + count - 1; i >= index; i--) {
			item = static_cast<BMenuItem *>(fItems.ItemAt(index));
			if (item != NULL) {
				fItems.RemoveItem(item);
				item->SetSuper(NULL);
				item->Uninstall();
				if (del)
					delete item;
				if (!result)
					result = true;
			}
		}
	}	
	
	InvalidateLayout();
	
	return result;
}


void
BMenu::LayoutItems(int32 index)
{
	CalcTriggers();

	float width, height;
	ComputeLayout(index, true, true, &width, &height);

	ResizeTo(width, height);
	
	// Move the BMenu to 2, 2, if it's attached to a BMenuWindow,
	// (that means it's a BMenu, BMenuBars are attached to regular BWindows).
	// This is needed to be able to draw the frame around the BMenu.
	if (dynamic_cast<BMenuWindow *>(Window()) != NULL)
		MoveTo(2, 2);
}


void
BMenu::ComputeLayout(int32 index, bool bestFit, bool moveItems,
						  float* width, float* height)
{
	// TODO: Take "bestFit", "moveItems", "index" into account.
	BRect frame(0, 0, 0, 0);
	float iWidth, iHeight;
	BMenuItem *item = NULL;
	
	switch (fLayout) {
		case B_ITEMS_IN_COLUMN:
		{
			for (int32 i = 0; i < fItems.CountItems(); i++) {
				item = ItemAt(i);			
				if (item != NULL) {
					item->GetContentSize(&iWidth, &iHeight);
	
					if (item->fModifiers && item->fShortcutChar)
						iWidth += 25.0f;
			
					item->fBounds.left = 0.0f;
					item->fBounds.top = frame.bottom;
					item->fBounds.bottom = item->fBounds.top + iHeight + fPad.top + fPad.bottom;

					frame.right = max_c(frame.right, iWidth + fPad.left + fPad.right) + 20;
					frame.bottom = item->fBounds.bottom + 1.0f;
				}
			}
			
			for (int32 i = 0; i < fItems.CountItems(); i++)
				ItemAt(i)->fBounds.right = frame.right;

			frame.right = (float)ceil(frame.right);
			frame.bottom--;
			break;
		}
		
		case B_ITEMS_IN_ROW:
		{
			font_height fh;
			GetFontHeight(&fh);
			frame = BRect(0.0f, 0.0f, 0.0f,
				(float)ceil(fh.ascent) + (float)ceil(fh.descent) + fPad.top + fPad.bottom);

			for (int32 i = 0; i < fItems.CountItems(); i++) {
				item = ItemAt(i);
				if (item != NULL) {
					item->GetContentSize(&iWidth, &iHeight);

					item->fBounds.left = frame.right;
					item->fBounds.top = 0.0f;
					item->fBounds.right = item->fBounds.left + iWidth + fPad.left + fPad.right;

					frame.right = item->fBounds.right + 1.0f;
					frame.bottom = max_c(frame.bottom, iHeight + fPad.top + fPad.bottom);
				}
			}
			
			for (int32 i = 0; i < fItems.CountItems(); i++)
				ItemAt(i)->fBounds.bottom = frame.bottom;

			frame.right = (float)ceil(frame.right) + 8.0f;
			break;
		}
		
		case B_ITEMS_IN_MATRIX:
		{
			for (int32 i = 0; i < CountItems(); i++) {
				item = ItemAt(i);
				if (item != NULL) {
					frame.left = min_c(frame.left, item->Frame().left);
					frame.right = max_c(frame.right, item->Frame().right);
					frame.top = min_c(frame.top, item->Frame().top);
					frame.bottom = max_c(frame.bottom, item->Frame().bottom);
				}			
			}		
			break;
		}
	
		default:
			break;
	}
	
	// This is for BMenuBar.
	if ((ResizingMode() & B_FOLLOW_LEFT_RIGHT) == B_FOLLOW_LEFT_RIGHT) {
		if (Parent())
			*width = Parent()->Frame().Width() + 1;
		else
			*width = Window()->Frame().Width() + 1;
		
		*height = frame.Height();
	} else {
		*width = frame.Width();
		*height = frame.Height();
	}
}


BRect
BMenu::Bump(BRect current, BPoint extent, int32 index) const
{
	return BRect();
}


BPoint
BMenu::ItemLocInRect(BRect frame) const
{
	return BPoint();
}


BRect
BMenu::CalcFrame(BPoint where, bool *scrollOn)
{
	return BRect();
}


bool
BMenu::ScrollMenu(BRect bounds, BPoint loc, bool *fast)
{
	return false;
}


void
BMenu::ScrollIntoView(BMenuItem *item)
{
}


void
BMenu::DrawItems(BRect updateRect)
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (ItemAt(i)->Frame().Intersects(updateRect))
			ItemAt(i)->Draw();			
	}
}


int
BMenu::State(BMenuItem **item) const
{
	return 0;
}


void
BMenu::InvokeItem(BMenuItem *item, bool now)
{
	if (item->Submenu())
		item->Submenu()->Show();
	else if (IsRadioMode())
		item->SetMarked(true);

	item->Invoke();
}


bool
BMenu::OverSuper(BPoint location)
{
	if (!Supermenu())
		return false;
	
	ConvertToScreen(&location);
	
	return fSuperbounds.Contains(location);
}


bool
BMenu::OverSubmenu(BMenuItem *item, BPoint loc)
{
	// TODO: we assume that loc is in screen coords
	if (!item->Submenu())
		return false;
	
	return item->Submenu()->Window()->Frame().Contains(loc);
}


BMenuWindow	*
BMenu::MenuWindow()
{
	return fCachedMenuWindow;
}


void
BMenu::DeleteMenuWindow()
{
	delete fCachedMenuWindow;
	fCachedMenuWindow = NULL;
}


BMenuItem *
BMenu::HitTestItems(BPoint where, BPoint slop) const
{
	// TODO: Take "slop" into account ?
	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem *item = ItemAt(i);
		if (item->Frame().Contains(where))
			return item;
	}

	return NULL;
}


BRect
BMenu::Superbounds() const
{
	return fSuperbounds;
}


void
BMenu::CacheFontInfo()
{
	font_height fh;
	GetFontHeight(&fh);
	fAscent = fh.ascent;
	fDescent = fh.descent;
	fFontHeight = (float)ceil(fh.ascent + fh.descent + fh.leading);
}


void
BMenu::ItemMarked(BMenuItem *item)
{
	if (IsRadioMode()) {
		for (int32 i = 0; i < CountItems(); i++)
			if (ItemAt(i) != item)
				ItemAt(i)->SetMarked(false);
	}

	if (IsLabelFromMarked() && Superitem())
		Superitem()->SetLabel(item->Label());
}


void
BMenu::Install(BWindow *target)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Install(target);
}


void
BMenu::Uninstall()
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Uninstall();
}


void
BMenu::SelectItem(BMenuItem *menuItem, uint32 showSubmenu, bool selectFirstItem)
{
	// TODO: make use of "showSubmenu" and "selectFirstItem".
	if (fSelected != NULL) {
		fSelected->Select(false);
		if (fSelected->Submenu() != NULL)
			fSelected->Submenu()->_hide();
	}
	
	if (menuItem != NULL)
		menuItem->Select(true);
		
	fSelected = menuItem;
	if (fSelected != NULL && fSelected->Submenu() != NULL)
		fSelected->Submenu()->_show();
	
}


BMenuItem *
BMenu::CurrentSelection() const
{
	return fSelected;
}


bool 
BMenu::SelectNextItem(BMenuItem *item, bool forward)
{
	return false;
}


BMenuItem *
BMenu::NextItem(BMenuItem *item, bool forward) const
{
	return NULL;
}


bool 
BMenu::IsItemVisible(BMenuItem *item) const
{
	return false;
}


void 
BMenu::SetIgnoreHidden(bool on)
{
	fIgnoreHidden = on;
}


void
BMenu::SetStickyMode(bool on)
{
	fStickyMode = on;
}


bool
BMenu::IsStickyMode() const
{
	return fStickyMode;
}


void
BMenu::CalcTriggers()
{
	BList triggersList;
	
	// Gathers the existing triggers
	// TODO: Oh great, reinterpret_cast.
	for (int32 i = 0; i < CountItems(); i++) {
		char trigger = ItemAt(i)->Trigger();
		if (trigger != 0)
			triggersList.AddItem(reinterpret_cast<void *>((uint32)trigger));
	}

	// Set triggers for items which don't have one yet
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem *item = ItemAt(i);
		if (item->Trigger() == 0) {
			const char *newTrigger = ChooseTrigger(item->Label(), &triggersList);
			if (newTrigger != NULL) {
				item->SetSysTrigger(*newTrigger);			
				// TODO: This is crap. I'd prefer to have 
				// BMenuItem::SetSysTrigger(const char *) update fTriggerIndex.
				// This isn't the case on beos, but it will probably be like that on haiku.
				item->fTriggerIndex = newTrigger - item->Label();
			}
		}
	}
}


const char *
BMenu::ChooseTrigger(const char *title, BList *chars)
{
	ASSERT(chars != NULL);
	
	if (title == NULL)
		return NULL;
		
	char *titlePtr = const_cast<char *>(title);
	
	char trigger;
	// TODO: Oh great, reinterpret_cast all around
	while ((trigger = *titlePtr) != '\0') {
		if (!chars->HasItem(reinterpret_cast<void *>((uint32)trigger)))	{
			chars->AddItem(reinterpret_cast<void *>((uint32)trigger));
			return titlePtr;
		}
				
		titlePtr++;
	}

	return NULL;
}


void
BMenu::UpdateWindowViewSize(bool upWind)
{
}


bool
BMenu::IsStickyPrefOn()
{
	return sMenuInfo.click_to_open;
}


void
BMenu::RedrawAfterSticky(BRect bounds)
{
}


bool
BMenu::OkToProceed(BMenuItem *)
{
	return false;
}


status_t 
BMenu::ParseMsg(BMessage *msg, int32 *sindex, BMessage *spec,
						 int32 *form, const char **prop, BMenu **tmenu,
						 BMenuItem **titem, int32 *user_data,
						 BMessage *reply) const
{
	return B_ERROR;
}


status_t
BMenu::DoMenuMsg(BMenuItem **next, BMenu *tar, BMessage *m,
						  BMessage *r, BMessage *spec, int32 f) const
{
	return B_ERROR;
}


status_t
BMenu::DoMenuItemMsg(BMenuItem **next, BMenu *tar, BMessage *m,
							  BMessage *r, BMessage *spec, int32 f) const
{
	return B_ERROR;
}


status_t 
BMenu::DoEnabledMsg(BMenuItem *ti, BMenu *tm, BMessage *m,
							 BMessage *r) const
{
	return B_ERROR;
}


status_t
BMenu::DoLabelMsg(BMenuItem *ti, BMenu *tm, BMessage *m,
						   BMessage *r) const
{
	return B_ERROR;
}


status_t 
BMenu::DoMarkMsg(BMenuItem *ti, BMenu *tm, BMessage *m,
						  BMessage *r) const
{
	return B_ERROR;
}


status_t
BMenu::DoDeleteMsg(BMenuItem *ti, BMenu *tm, BMessage *m,
							BMessage *r) const
{
	return B_ERROR;
}


status_t
BMenu::DoCreateMsg(BMenuItem *ti, BMenu *tm, BMessage *m,
							BMessage *r, bool menu) const
{
	return B_ERROR;
}


status_t
set_menu_info(menu_info *info)
{
	if (!info)
		return B_BAD_VALUE;
		
	BPath path;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_OK;

	path.Append("menu_settings");

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE);

	if (file.InitCheck() != B_OK)
		return B_OK;

	file.Write(info, sizeof(menu_info));
	
	BMenu::sMenuInfo = *info;

	return B_OK;
}


status_t
get_menu_info(menu_info *info)
{
	if (!info)
		return B_BAD_VALUE;
	
	*info = BMenu::sMenuInfo;
	
	return B_OK;
}
