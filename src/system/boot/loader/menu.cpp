/*
 * Copyright 2003-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "menu.h"

#include <errno.h>
#include <string.h>

#include <algorithm>

#include <OS.h>

#include <boot/menu.h>
#include <boot/stage2.h>
#include <boot/vfs.h>
#include <boot/platform.h>
#include <boot/platform/generic/text_console.h>
#include <boot/stdio.h>
#include <safemode.h>
#include <util/kernel_cpp.h>
#include <util/ring_buffer.h>

#include "kernel_debug_config.h"

#include "load_driver_settings.h"
#include "loader.h"
#include "pager.h"
#include "RootFileSystem.h"


#define TRACE_MENU
#ifdef TRACE_MENU
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


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
	MenuItem *item = new(nothrow) MenuItem();
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


static const char*
size_to_string(off_t size, char* buffer, size_t bufferSize)
{
	static const char* const kPrefixes[] = { "K", "M", "G", "T", "P", NULL };
	int32 nextIndex = 0;
	int32 remainder = 0;
	while (size >= 1024 && kPrefixes[nextIndex] != NULL) {
		remainder = size % 1024;
		size /= 1024;
		nextIndex++;

		if (size < 1024) {
			// Compute the decimal remainder and make sure we have at most
			// 3 decimal places (or 4 for 1000 <= size <= 1023).
			int32 factor;
			if (size >= 100)
				factor = 100;
			else if (size >= 10)
				factor = 10;
			else
				factor = 1;

			remainder = (remainder * 1000 + 5 * factor) / 1024;

			if (remainder >= 1000) {
				size++;
				remainder = 0;
			} else
				remainder /= 10 * factor;
		} else
			size += (remainder + 512) / 1024;
	}

	if (remainder == 0) {
		snprintf(buffer, bufferSize, "%" B_PRIdOFF, size);
	} else {
		snprintf(buffer, bufferSize, "%" B_PRIdOFF ".%" B_PRId32, size,
			remainder);
	}

	size_t length = strlen(buffer);
	snprintf(buffer + length, bufferSize - length, " %sB",
		nextIndex == 0 ? "" : kPrefixes[nextIndex - 1]);

	return buffer;
}


// #pragma mark -


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

	gKernelArgs.boot_volume.SetBool(BOOT_VOLUME_USER_SELECTED, true);
	return true;
}


static bool
debug_menu_display_syslog(Menu *menu, MenuItem *item)
{
	ring_buffer* buffer = (ring_buffer*)gKernelArgs.debug_output;
	if (buffer == NULL)
		return true;

	struct TextSource : PagerTextSource {
		TextSource(ring_buffer* buffer)
			:
			fBuffer(buffer)
		{
		}

		virtual size_t BytesAvailable() const
		{
			return ring_buffer_readable(fBuffer);
		}

		virtual size_t Read(size_t offset, void* buffer, size_t size) const
		{
			return ring_buffer_peek(fBuffer, offset, buffer, size);
		}

	private:
		ring_buffer*	fBuffer;
	};

	pager(TextSource(buffer));

	return true;
}


static status_t
save_syslog_to_volume(Directory* directory)
{
	// find an unused name
	char name[16];
	bool found = false;
	for (int i = 0; i < 99; i++) {
		snprintf(name, sizeof(name), "SYSLOG%02d.TXT", i);
		Node* node = directory->Lookup(name, false);
		if (node == NULL) {
			found = true;
			break;
		}

		node->Release();
	}

	if (!found) {
		printf("Failed to find an unused name for the syslog file!\n");
		return B_ERROR;
	}

	printf("Writing syslog to file \"%s\" ...\n", name);

	int fd = open_from(directory, name, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		printf("Failed to create syslog file!\n");
		return fd;
	}

	ring_buffer* syslogBuffer = (ring_buffer*)gKernelArgs.debug_output;
	iovec vecs[2];
	int32 vecCount = ring_buffer_get_vecs(syslogBuffer, vecs);
	if (vecCount > 0) {
		size_t toWrite = ring_buffer_readable(syslogBuffer);

		ssize_t written = writev(fd, vecs, vecCount);
		if (written < 0 || (size_t)written != toWrite) {
			printf("Failed to write to the syslog file \"%s\"!\n", name);
			close(fd);
			return errno;
		}
	}

	close(fd);

	printf("Successfully wrote syslog file.\n");

	return B_OK;
}


static bool
debug_menu_toggle_debug_syslog(Menu *menu, MenuItem *item)
{
	gKernelArgs.keep_debug_output_buffer = item->IsMarked();
	return true;
}


static bool
debug_menu_save_syslog(Menu *menu, MenuItem *item)
{
	Directory* volume = (Directory*)item->Data();

	console_clear_screen();

	save_syslog_to_volume(volume);

	printf("\nPress any key to continue\n");
	console_wait_for_key();

	return true;
}


static Menu *
add_boot_volume_menu(Directory *bootVolume)
{
	Menu *menu = new(nothrow) Menu(CHOICE_MENU, "Select Boot Volume");
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
				menu->AddItem(item = new(nothrow) MenuItem(name));
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
		menu->AddItem(item = new(nothrow) MenuItem("<No boot volume found>"));
		item->SetType(MENU_ITEM_NO_CHOICE);
		item->SetEnabled(false);
	}

	menu->AddSeparatorItem();

	menu->AddItem(item = new(nothrow) MenuItem("Rescan volumes"));
	item->SetHelpText("Please insert a Haiku CD-ROM or attach a USB disk - "
		"depending on your system, you can then boot from them.");
	item->SetType(MENU_ITEM_NO_CHOICE);
	if (count == 0)
		item->Select(true);

	menu->AddItem(item = new(nothrow) MenuItem("Return to main menu"));
	item->SetType(MENU_ITEM_NO_CHOICE);

	if (gKernelArgs.boot_volume.GetBool(BOOT_VOLUME_BOOTED_FROM_IMAGE, false))
		menu->SetChoiceText("CD-ROM or hard drive");

	return menu;
}


static Menu *
add_safe_mode_menu()
{
	Menu *safeMenu = new(nothrow) Menu(SAFE_MODE_MENU, "Safe Mode Options");
	MenuItem *item;

	safeMenu->AddItem(item = new(nothrow) MenuItem("Safe mode"));
	item->SetData(B_SAFEMODE_SAFE_MODE);
	item->SetType(MENU_ITEM_MARKABLE);
	item->SetHelpText("Puts the system into safe mode. This can be enabled "
		"independently from the other options.");

	safeMenu->AddItem(item = new(nothrow) MenuItem("Disable user add-ons"));
	item->SetData(B_SAFEMODE_DISABLE_USER_ADD_ONS);
	item->SetType(MENU_ITEM_MARKABLE);
    item->SetHelpText("Prevents all user installed add-ons from being loaded. "
		"Only the add-ons in the system directory will be used.");

	safeMenu->AddItem(item = new(nothrow) MenuItem("Disable IDE DMA"));
	item->SetData(B_SAFEMODE_DISABLE_IDE_DMA);
	item->SetType(MENU_ITEM_MARKABLE);
    item->SetHelpText("Disables IDE DMA, increasing IDE compatibilty "
		"at the expense of performance.");

	platform_add_menus(safeMenu);

	safeMenu->AddSeparatorItem();
	safeMenu->AddItem(item = new(nothrow) MenuItem("Return to main menu"));

	return safeMenu;
}


static Menu*
add_save_debug_syslog_menu()
{
	Menu* menu = new(nothrow) Menu(STANDARD_MENU, "Save syslog to volume ...");
	MenuItem* item;

	const char* const kHelpText = "Currently only FAT32 volumes are supported. "
		"Newly plugged in removable devices are only recognized after "
		"rebooting.";

	int32 itemsAdded = 0;

	void* cookie;
	if (gRoot->Open(&cookie, O_RDONLY) == B_OK) {
		Node* node;
		while (gRoot->GetNextNode(cookie, &node) == B_OK) {
			Directory* volume = static_cast<Directory*>(node);
			Partition* partition;
			if (gRoot->GetPartitionFor(volume, &partition) != B_OK)
				continue;

			// we support only FAT32 volumes ATM
			if (partition->content_type == NULL
				|| strcmp(partition->content_type, kPartitionTypeFAT32) != 0) {
				continue;
			}

			char name[B_FILE_NAME_LENGTH];
			if (volume->GetName(name, sizeof(name)) != B_OK)
				strcpy(name, "unnamed");

			// append offset, size, and type to the name
			size_t len = strlen(name);
			char offsetBuffer[32];
			char sizeBuffer[32];
			snprintf(name + len, sizeof(name) - len,
				" (%s, offset %s, size %s)", partition->content_type,
				size_to_string(partition->offset, offsetBuffer,
					sizeof(offsetBuffer)),
				size_to_string(partition->size, sizeBuffer,
					sizeof(sizeBuffer)));

			item = new(nothrow) MenuItem(name);
			item->SetData(volume);
			item->SetTarget(&debug_menu_save_syslog);
			item->SetType(MENU_ITEM_NO_CHOICE);
			item->SetHelpText(kHelpText);
			menu->AddItem(item);
			itemsAdded++;
		}

		gRoot->Close(cookie);
	}

	if (itemsAdded == 0) {
		menu->AddItem(item
			= new(nothrow) MenuItem("No supported volumes found"));
		item->SetType(MENU_ITEM_NO_CHOICE);
		item->SetHelpText(kHelpText);
		item->SetEnabled(false);
	}

	menu->AddSeparatorItem();
	menu->AddItem(item = new(nothrow) MenuItem("Return to debug menu"));
	item->SetHelpText(kHelpText);

	return menu;
}


static Menu *
add_debug_menu()
{
	Menu *menu = new(nothrow) Menu(STANDARD_MENU, "Debug Options");
	MenuItem *item;

#if DEBUG_SPINLOCK_LATENCIES
	item = new(std::nothrow) MenuItem("Disable latency checks");
	if (item != NULL) {
		item->SetType(MENU_ITEM_MARKABLE);
		item->SetData(B_SAFEMODE_DISABLE_LATENCY_CHECK);
		item->SetHelpText("Disables latency check panics.");
		menu->AddItem(item);
	}
#endif

	menu->AddItem(item
		= new(nothrow) MenuItem("Enable serial debug output"));
	item->SetData("serial_debug_output");
	item->SetType(MENU_ITEM_MARKABLE);
    item->SetHelpText("Turns on forwarding the syslog output to the serial "
		"interface.");

	menu->AddItem(item
		= new(nothrow) MenuItem("Enable on screen debug output"));
	item->SetData("debug_screen");
	item->SetType(MENU_ITEM_MARKABLE);
    item->SetHelpText("Displays debug output on screen while the system "
		"is booting, instead of the normal boot logo.");

	menu->AddItem(item = new(nothrow) MenuItem("Enable debug syslog"));
	item->SetType(MENU_ITEM_MARKABLE);
	item->SetMarked(gKernelArgs.keep_debug_output_buffer);
	item->SetTarget(&debug_menu_toggle_debug_syslog);
    item->SetHelpText("Enables a special in-memory syslog buffer for this "
    	"session that the boot loader will be able to access after rebooting.");

	ring_buffer* syslogBuffer = (ring_buffer*)gKernelArgs.debug_output;
	if (syslogBuffer != NULL && ring_buffer_readable(syslogBuffer) > 0) {
		menu->AddSeparatorItem();

		menu->AddItem(item
			= new(nothrow) MenuItem("Display syslog from previous session"));
		item->SetTarget(&debug_menu_display_syslog);
		item->SetType(MENU_ITEM_NO_CHOICE);
		item->SetHelpText(
			"Displays the syslog from the previous Haiku session.");

		menu->AddItem(item = new(nothrow) MenuItem(
			"Save syslog from previous session", add_save_debug_syslog_menu()));
		item->SetHelpText("Saves the syslog from the previous Haiku session to "
			"disk. Currently only FAT32 volumes are supported.");
	}

	menu->AddSeparatorItem();
	menu->AddItem(item = new(nothrow) MenuItem("Return to main menu"));

	return menu;
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

		size_t totalBytes = snprintf(buffer + pos, sizeof(buffer) - pos,
			"%s true\n", (const char *)item->Data());
		pos += std::min(totalBytes, sizeof(buffer) - pos - 1);
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
	Menu *menu = new(nothrow) Menu(MAIN_MENU);
	Menu *safeModeMenu = NULL;
	Menu *debugMenu = NULL;
	MenuItem *item;

	TRACE(("user_menu: enter\n"));

	// Add boot volume
	menu->AddItem(item = new(nothrow) MenuItem("Select boot volume",
		add_boot_volume_menu(*_bootVolume)));

	// Add safe mode
	menu->AddItem(item = new(nothrow) MenuItem("Select safe mode options",
		safeModeMenu = add_safe_mode_menu()));

	// add debug menu
	menu->AddItem(item = new(nothrow) MenuItem("Select debug options",
		debugMenu = add_debug_menu()));

	// Add platform dependent menus
	platform_add_menus(menu);

	menu->AddSeparatorItem();

	menu->AddItem(item = new(nothrow) MenuItem("Reboot"));
	item->SetTarget(user_menu_reboot);

	menu->AddItem(item = new(nothrow) MenuItem("Continue booting"));
	if (*_bootVolume == NULL) {
		item->SetEnabled(false);
		menu->ItemAt(0)->Select(true);
	}

	menu->Run();

	// See if a new boot device has been selected, and propagate that back
	if (item->Data() != NULL)
		*_bootVolume = (Directory *)item->Data();

	apply_safe_mode_options(safeModeMenu);
	apply_safe_mode_options(debugMenu);
	delete menu;

	TRACE(("user_menu: leave\n"));

	return B_OK;
}

