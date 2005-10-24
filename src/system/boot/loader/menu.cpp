/*
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "menu.h"
#include "loader.h"
#include "RootFileSystem.h"
#include "load_driver_settings.h"

#include <OS.h>

#include <util/kernel_cpp.h>
#include <boot/menu.h>
#include <boot/stage2.h>
#include <boot/vfs.h>
#include <boot/platform.h>
#include <boot/stdio.h>
#include <safemode.h>

#include <string.h>


MenuItem::MenuItem(const char *label, Menu *subMenu)
	:
	fLabel(strdup(label)),
	fTarget(NULL),
	fIsMarked(false),
	fIsSelected(false),
	fIsEnabled(true),
	fType(MENU_ITEM_STANDARD),
	fMenu(NULL),
	fSubMenu(subMenu),
	fData(NULL),
	fHelpText(NULL)
{
	if (subMenu != NULL)
		subMenu->fSuperItem = this;
}


MenuItem::~MenuItem()
{
	if (fSubMenu != NULL)
		fSubMenu->fSuperItem = NULL;

	free(const_cast<char *>(fLabel));
}


void
MenuItem::SetTarget(menu_item_hook target)
{
	fTarget = target;
}


/**	Marks or unmarks a menu item. A marked menu item usually gets a visual
 *	clue like a checkmark that distinguishes it from others.
 *	For menus of type CHOICE_MENU, there can only be one marked item - the
 *	chosen one.
 */

void
MenuItem::SetMarked(bool marked)
{
	if (marked && fMenu != NULL && fMenu->Type() == CHOICE_MENU) {
		// always set choice text of parent if we were marked
		fMenu->SetChoiceText(Label());
	}

	if (fIsMarked == marked)
		return;

	if (marked && fMenu != NULL && fMenu->Type() == CHOICE_MENU) {
		// unmark previous item
		MenuItem *markedItem = fMenu->FindMarked();
		if (markedItem != NULL)
			markedItem->SetMarked(false);
	}

	fIsMarked = marked;

	if (fMenu != NULL)
		fMenu->Draw(this);
}


void
MenuItem::Select(bool selected)
{
	if (fIsSelected == selected)
		return;

	if (selected && fMenu != NULL) {
		// unselect previous item
		MenuItem *selectedItem = fMenu->FindSelected();
		if (selectedItem != NULL)
			selectedItem->Select(false);
	}

	fIsSelected = selected;

	if (fMenu != NULL)
		fMenu->Draw(this);
}


void
MenuItem::SetType(menu_item_type type)
{
	fType = type;
}


void
MenuItem::SetEnabled(bool enabled)
{
	if (fIsEnabled == enabled)
		return;

	fIsEnabled = enabled;

	if (fMenu != NULL)
		fMenu->Draw(this);
}


void
MenuItem::SetData(const void *data)
{
	fData = data;
}


/** This sets a help text that is shown when the item is
 *	selected.
 *	Note, unlike the label, the string is not copied, it's
 *	just referenced and has to stay valid as long as the
 *	item's menu is being used.
 */

void
MenuItem::SetHelpText(const char *text)
{
	fHelpText = text;
}


void 
MenuItem::SetMenu(Menu *menu)
{
	fMenu = menu;
}


//	#pragma mark -


Menu::Menu(menu_type type, const char *title)
	:
	fTitle(title),
	fChoiceText(NULL),
	fCount(0),
	fIsHidden(true),
	fType(type),
	fSuperItem(NULL)
{
}


Menu::~Menu()
{
	// take all remaining items with us

	MenuItem *item;
	while ((item = fItems.Head()) != NULL) {
		fItems.Remove(item);
		delete item;
	}
}


MenuItem *
Menu::ItemAt(int32 index)
{
	if (index < 0 || index >= fCount)
		return NULL;

	MenuItemIterator iterator = ItemIterator();
	MenuItem *item;

	while ((item = iterator.Next()) != NULL) {
		if (index-- == 0)
			return item;
	}

	return NULL;
}


int32 
Menu::IndexOf(MenuItem *searchedItem)
{
	MenuItemIterator iterator = ItemIterator();
	MenuItem *item;
	int32 index = 0;

	while ((item = iterator.Next()) != NULL) {
		if (item == searchedItem)
			return index;

		index++;
	}

	return -1;
}


int32
Menu::CountItems() const
{
	return fCount;
}


MenuItem *
Menu::FindItem(const char *label)
{
	MenuItemIterator iterator = ItemIterator();
	MenuItem *item;

	while ((item = iterator.Next()) != NULL) {
		if (item->Label() != NULL && !strcmp(item->Label(), label))
			return item;
	}

	return NULL;
}


MenuItem *
Menu::FindMarked()
{
	MenuItemIterator iterator = ItemIterator();
	MenuItem *item;

	while ((item = iterator.Next()) != NULL) {
		if (item->IsMarked())
			return item;
	}

	return NULL;
}


MenuItem *
Menu::FindSelected(int32 *_index)
{
	MenuItemIterator iterator = ItemIterator();
	MenuItem *item;
	int32 index = 0;

	while ((item = iterator.Next()) != NULL) {
		if (item->IsSelected()) {
			if (_index != NULL)
				*_index = index;
			return item;
		}

		index++;
	}

	return NULL;
}


void
Menu::AddItem(MenuItem *item)
{
	item->fMenu = this;
	fItems.Add(item);
	fCount++;
}


status_t
Menu::AddSeparatorItem()
{
	MenuItem *item = new MenuItem();
	if (item == NULL)
		return B_NO_MEMORY;

	item->SetType(MENU_ITEM_SEPARATOR);

	AddItem(item);
	return B_OK;
}


MenuItem *
Menu::RemoveItemAt(int32 index)
{
	if (index < 0 || index >= fCount)
		return NULL;

	MenuItemIterator iterator = ItemIterator();
	MenuItem *item;

	while ((item = iterator.Next()) != NULL) {
		if (index-- == 0) {
			RemoveItem(item);
			return item;
		}
	}

	return NULL;
}


void
Menu::RemoveItem(MenuItem *item)
{
	item->fMenu = NULL;
	fItems.Remove(item);
	fCount--;
}


void
Menu::Draw(MenuItem *item)
{
	if (!IsHidden())
		platform_update_menu_item(this, item);
}


void
Menu::Run()
{
	platform_run_menu(this);
}


//	#pragma mark -


static bool
user_menu_boot_volume(Menu *menu, MenuItem *item)
{
	Menu *super = menu->Supermenu();
	if (super == NULL) {
		// huh?
		return true;
	}

	MenuItem *bootItem = super->ItemAt(super->CountItems() - 1);
	bootItem->SetEnabled(true);
	bootItem->Select(true);
	bootItem->SetData(item->Data());

	gKernelArgs.boot_disk.user_selected = true;
	return true;
}


static Menu *
add_boot_volume_menu(Directory *bootVolume)
{
	Menu *menu = new Menu(CHOICE_MENU, "Select Boot Volume");
	MenuItem *item;
	void *cookie;
	int32 count = 0;

	if (gRoot->Open(&cookie, O_RDONLY) == B_OK) {
		Directory *volume;
		while (gRoot->GetNextNode(cookie, (Node **)&volume) == B_OK) {
			// only list bootable volumes
			if (!is_bootable(volume))
				continue;

			char name[B_FILE_NAME_LENGTH];
			if (volume->GetName(name, sizeof(name)) == B_OK) {
				menu->AddItem(item = new MenuItem(name));
				item->SetTarget(user_menu_boot_volume);
				item->SetData(volume);

				if (volume == bootVolume) {
					item->SetMarked(true);
					item->Select(true);
				}

				count++;
			}
		}
		gRoot->Close(cookie);
	}

	if (count == 0) {
		// no boot volume found yet
		menu->AddItem(item = new MenuItem("<No boot volume found>"));
		item->SetType(MENU_ITEM_NO_CHOICE);
		item->SetEnabled(false);
	}

	menu->AddSeparatorItem();

	menu->AddItem(item = new MenuItem("Rescan volumes"));
	item->SetHelpText("Please insert a Haiku CD-ROM or attach a USB disk - depending on your system, you can then boot from them.");
	item->SetType(MENU_ITEM_NO_CHOICE);
	if (count == 0)
		item->Select(true);

	menu->AddItem(item = new MenuItem("Return to main menu"));
	item->SetType(MENU_ITEM_NO_CHOICE);

	if (gKernelArgs.boot_disk.booted_from_image)
		menu->SetChoiceText("CD-ROM or hard drive");

	return menu;
}


static Menu *
add_safe_mode_menu()
{
	Menu *safeMenu = new Menu(SAFE_MODE_MENU, "Safe Mode Options");
	MenuItem *item;

	safeMenu->AddItem(item = new MenuItem("Safe mode"));
	item->SetData(B_SAFEMODE_SAFE_MODE);
	item->SetType(MENU_ITEM_MARKABLE);
	item->SetHelpText("Puts the system into safe mode. This can be enabled independently "
		"from the other options.");

	safeMenu->AddItem(item = new MenuItem("Disable user add-ons"));
	item->SetData(B_SAFEMODE_DISABLE_USER_ADD_ONS);
	item->SetType(MENU_ITEM_MARKABLE);
	item->SetHelpText("Prevent all user installed add-ons to be loaded. Only the add-ons "
		"in the system directory will be used.");

	safeMenu->AddItem(item = new MenuItem("Disable IDE DMA"));
	item->SetData(B_SAFEMODE_DISABLE_IDE_DMA);
	item->SetType(MENU_ITEM_MARKABLE);

	platform_add_menus(safeMenu);

	safeMenu->AddSeparatorItem();
	safeMenu->AddItem(item = new MenuItem("Return to main menu"));

	return safeMenu;
}


static void
apply_safe_mode_options(Menu *menu)
{
	MenuItemIterator iterator = menu->ItemIterator();
	MenuItem *item;
	char buffer[2048];
	int32 pos = 0;

	buffer[0] = '\0';

	while ((item = iterator.Next()) != NULL) {
		if (item->Type() == MENU_ITEM_SEPARATOR || !item->IsMarked()
			|| item->Data() == NULL || (uint32)pos > sizeof(buffer))
			continue;

		pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s true\n",
			(const char *)item->Data());
	}

	add_safe_mode_settings(buffer);
}


static bool
user_menu_reboot(Menu *menu, MenuItem *item)
{
	platform_exit();
	return true;
}


status_t
user_menu(Directory **_bootVolume)
{
	Menu *menu = new Menu(MAIN_MENU);
	Menu *safeModeMenu;
	MenuItem *item;

	// Add boot volume
	menu->AddItem(item = new MenuItem("Select boot volume", add_boot_volume_menu(*_bootVolume)));

	// Add safe mode
	menu->AddItem(item = new MenuItem("Select safe mode options", safeModeMenu = add_safe_mode_menu()));

	// Add platform dependent menus
	platform_add_menus(menu);

	menu->AddSeparatorItem();
	if (*_bootVolume == NULL) {
		menu->AddItem(item = new MenuItem("Reboot"));
		item->SetTarget(user_menu_reboot);
	}

	menu->AddItem(item = new MenuItem("Continue booting"));
	if (*_bootVolume == NULL) {
		item->SetEnabled(false);
		menu->ItemAt(0)->Select(true);
	}

	menu->Run();

	// See if a new boot device has been selected, and propagate that back
	if (item->Data() != NULL)
		*_bootVolume = (Directory *)item->Data();

	apply_safe_mode_options(safeModeMenu);
	delete menu;

	return B_OK;
}

