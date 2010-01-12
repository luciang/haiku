/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SettingsWindow.h"
#include "Settings.h"

#include <Application.h>
#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <Locale.h>
#include <StringView.h>
#include <String.h>
#include <Slider.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <Screen.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <stdio.h>


#undef TR_CONTEXT
#define TR_CONTEXT "SettingsWindow"


static const uint32 kMsgDefaults = 'dflt';
static const uint32 kMsgRevert = 'rvrt';
static const uint32 kMsgSliderUpdate = 'slup';
static const uint32 kMsgSwapEnabledUpdate = 'swen';


class SizeSlider : public BSlider {
	public:
		SizeSlider(const char* name, const char* label,
			BMessage* message, int32 min, int32 max, uint32 flags);
		virtual ~SizeSlider();

		virtual const char* UpdateText() const;

	private:
		mutable BString	fText;
};


static const int64 kMegaByte = 1048576;


const char *
byte_string(int64 size)
{
	double value = 1. * size;
	static char string[256];

	if (value < 1024)
		snprintf(string, sizeof(string), TR("%Ld B"), size);
	else {
		static const char *units[] = {TR_MARK("KB"), TR_MARK("MB"), TR_MARK("GB"), NULL};
		int32 i = -1;

		do {
			value /= 1024.0;
			i++;
		} while (value >= 1024 && units[i + 1]);

		off_t rounded = off_t(value * 100LL);
		sprintf(string, "%g %s", rounded / 100.0, TR(units[i]));
	}

	return string;
}


//	#pragma mark -


SizeSlider::SizeSlider(const char* name, const char* label,
	BMessage* message, int32 min, int32 max, uint32 flags)
	: BSlider(name, label, message, min, max, B_HORIZONTAL, B_BLOCK_THUMB, flags)
{
	rgb_color color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	UseFillColor(true, &color);
}


SizeSlider::~SizeSlider()
{
}


const char *
SizeSlider::UpdateText() const
{
	fText = byte_string(Value() * kMegaByte);

	return fText.String();
}


//	#pragma mark -


SettingsWindow::SettingsWindow()
	: BWindow(BRect(0, 0, 269, 172), TR("VirtualMemory"), B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS)
{
	BView* view = new BGroupView();

	fSwapEnabledCheckBox = new BCheckBox("enable swap",
		TR("Enable virtual memory"),
		new BMessage(kMsgSwapEnabledUpdate));
	fSwapEnabledCheckBox->SetValue(fSettings.SwapEnabled());

	BBox* box = new BBox("box", B_FOLLOW_LEFT_RIGHT);
	box->SetLabel(fSwapEnabledCheckBox);

	system_info info;
	get_system_info(&info);

	BString string = TR("Physical memory: ");
	string << byte_string((off_t)info.max_pages * B_PAGE_SIZE);
	BStringView* memoryView = new BStringView("physical memory", string.String());

	string = TR("Current swap file size: ");
	string << byte_string(fSettings.SwapSize());
	BStringView* swapfileView = new BStringView("current swap size", string.String());

	BPopUpMenu* menu = new BPopUpMenu("volumes");

	// collect volumes
	// TODO: listen to volume changes!
	// TODO: accept dropped volumes

	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (!volume.IsPersistent() || volume.GetName(name) != B_OK || !name[0])
			continue;

		BMenuItem* item = new BMenuItem(name, NULL);
		menu->AddItem(item);

		if (volume.Device() == fSettings.SwapVolume().Device())
			item->SetMarked(true);
	}

	BMenuField* field = new BMenuField("devices", TR("Use volume:"), menu);
	field->SetEnabled(false);

	off_t minSize, maxSize;
	_GetSwapFileLimits(minSize, maxSize);

	fSizeSlider = new SizeSlider("size slider", TR("Requested swap file size:"),
		new BMessage(kMsgSliderUpdate), minSize / kMegaByte, maxSize / kMegaByte,
		B_WILL_DRAW | B_FRAME_EVENTS);
	fSizeSlider->SetLimitLabels(TR("999 MB"), TR("999 MB"));
	fSizeSlider->SetViewColor(255, 0, 255);

	fWarningStringView = new BStringView("", "");
	fWarningStringView->SetAlignment(B_ALIGN_CENTER);

	view->SetLayout(new BGroupLayout(B_HORIZONTAL)); 
	view->AddChild(BGroupLayoutBuilder(B_VERTICAL, 10) 
		.AddGroup(B_HORIZONTAL)
			.Add(memoryView)
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(swapfileView)
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(field)
			.AddGlue()
		.End()
		.Add(fSizeSlider)
		.Add(fWarningStringView)
		.SetInsets(10, 10, 10, 10)
	);
	box->AddChild(view);

	// Add "Defaults" and "Revert" buttons

	fDefaultsButton = new BButton("defaults", TR("Defaults"), new BMessage(kMsgDefaults));
	fDefaultsButton->SetEnabled(fSettings.IsDefaultable());

	fRevertButton = new BButton("revert", TR("Revert"), new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);

	SetLayout(new BGroupLayout(B_HORIZONTAL)); 
	AddChild(BGroupLayoutBuilder(B_VERTICAL, 10) 
		.Add(box) 
		.AddGroup(B_HORIZONTAL, 10) 
			.Add(fDefaultsButton) 
			.Add(fRevertButton) 
			.AddGlue() 
		.End() 
		.SetInsets(10, 10, 10, 10) 
	); 

	_Update();

	BScreen screen;
	BRect screenFrame = screen.Frame();

	if (!screenFrame.Contains(fSettings.WindowPosition())) {
		// move on screen, centered
		CenterOnScreen();
	} else
		MoveTo(fSettings.WindowPosition());
}


SettingsWindow::~SettingsWindow()
{
}


	void
SettingsWindow::_Update()
{
	if ((fSwapEnabledCheckBox->Value() != 0) != fSettings.SwapEnabled())
		fSwapEnabledCheckBox->SetValue(fSettings.SwapEnabled());

	off_t minSize, maxSize;
	if (_GetSwapFileLimits(minSize, maxSize) == B_OK) {
		BString minLabel, maxLabel;
		minLabel << byte_string(minSize);
		maxLabel << byte_string(maxSize);
		if (minLabel != fSizeSlider->MinLimitLabel()
				|| maxLabel != fSizeSlider->MaxLimitLabel()) {
			fSizeSlider->SetLimitLabels(minLabel.String(), maxLabel.String());
#ifdef __HAIKU__
			fSizeSlider->SetLimits(minSize / kMegaByte, maxSize / kMegaByte);
#endif
		}

		if (fSizeSlider->Value() != fSettings.SwapSize() / kMegaByte)
			fSizeSlider->SetValue(fSettings.SwapSize() / kMegaByte);

		fSizeSlider->SetEnabled(true);
	} else {
		fSizeSlider->SetValue(minSize);
		fSizeSlider->SetEnabled(false);
	}

	// ToDo: set volume

	fDefaultsButton->SetEnabled(fSettings.IsDefaultable());

	bool changed = fSettings.SwapChanged();
	if (fRevertButton->IsEnabled() != changed) {
		fRevertButton->SetEnabled(changed);
		if (changed)
			fWarningStringView->SetText(TR("Changes will take effect on restart!"));
		else
			fWarningStringView->SetText("");
	}
}


	status_t
SettingsWindow::_GetSwapFileLimits(off_t& minSize, off_t& maxSize)
{
	// minimum size is an arbitrarily chosen MB
	minSize = kMegaByte;

	// maximum size is the free space on the current volume
	// (minus some safety offset, depending on the disk size)

	off_t freeSpace = fSettings.SwapVolume().FreeBytes();
	off_t safetyFreeSpace = fSettings.SwapVolume().Capacity() / 100;
	if (safetyFreeSpace > 1024 * kMegaByte)
		safetyFreeSpace = 1024 * kMegaByte;

	// check if there already is a page file on this disk and
	// adjust the free space accordingly

	BPath path;
	if (find_directory(B_COMMON_VAR_DIRECTORY, &path, false,
			&fSettings.SwapVolume()) == B_OK) {
		path.Append("swap");
		BEntry swap(path.Path());

		off_t size;
		if (swap.GetSize(&size) == B_OK)
			freeSpace += size;
	}

	maxSize = freeSpace - safetyFreeSpace;

	if (maxSize < minSize) {
		maxSize = minSize;
		return B_ERROR;
	}

	return B_OK;
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRevert:
			fSettings.RevertSwapChanges();
			_Update();
			break;
		case kMsgDefaults:
			fSettings.SetSwapDefaults();
			_Update();
			break;
		case kMsgSliderUpdate:
			fSettings.SetSwapSize((off_t)fSizeSlider->Value() * kMegaByte);
			_Update();
			break;
		case kMsgSwapEnabledUpdate:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) != B_OK)
				break;

			if (value == 0) {
				// print out warning, give the user the time to think about it :)
				// ToDo: maybe we want to remove this possibility in the GUI
				//	as Be did, but I thought a proper warning could be helpful
				//	(for those that want to change that anyway)
				int32 choice = (new BAlert("VirtualMemory",
					TR("Disabling virtual memory will have unwanted effects on "
					"system stability once the memory is used up.\n"
					"Virtual memory does not affect system performance "
					"until this point is reached.\n\n"
					"Are you really sure you want to turn it off?"),
					TR("Turn off"), TR("Keep enabled"), NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
				if (choice == 1) {
					fSwapEnabledCheckBox->SetValue(1);
					break;
				}
			}

			fSettings.SetSwapEnabled(value != 0);
			_Update();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


bool
SettingsWindow::QuitRequested()
{
	fSettings.SetWindowPosition(Frame().LeftTop());
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

