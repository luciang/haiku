/*
 * Copyright 2008-09, Oliver Ruiz Dorantes, <oliver.ruiz.dorantes_at_gmail.com>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include <stdio.h>

#include <Alert.h>
#include <Catalog.h>
#include <Locale.h>
#include <Messenger.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>

#include <GroupLayoutBuilder.h>
#include <SpaceLayoutItem.h>

#include <PincodeWindow.h>

#include "defs.h"
#include "InquiryPanel.h"
#include "BluetoothWindow.h"

#include "RemoteDevicesView.h"

#define TR_CONTEXT "Remote devices"

static const uint32 kMsgAddDevices = 'ddDv';
static const uint32 kMsgRemoveDevice = 'rmDv';
static const uint32 kMsgPairDevice = 'trDv';
static const uint32 kMsgBlockDevice = 'blDv';
static const uint32 kMsgRefreshDevices = 'rfDv';

RemoteDevicesView::RemoteDevicesView(const char *name, uint32 flags)
 :	BView(name, flags)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	addButton = new BButton("add", TR("Add" B_UTF8_ELLIPSIS),
										new BMessage(kMsgAddDevices));

	removeButton = new BButton("remove", TR("Remove"),
										new BMessage(kMsgRemoveDevice));

	pairButton = new BButton("pair", TR("Pair" B_UTF8_ELLIPSIS),
										new BMessage(kMsgPairDevice));


	blockButton = new BButton("block", TR("As blocked"),
										new BMessage(kMsgBlockDevice));


	availButton = new BButton("check", TR("Refresh" B_UTF8_ELLIPSIS),
										new BMessage(kMsgRefreshDevices));

	// Set up device list
	fDeviceList = new BListView("DeviceList", B_SINGLE_SELECTION_LIST);

	fScrollView = new BScrollView("ScrollView", fDeviceList, 0, false, true);
	fScrollView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	SetLayout(new BGroupLayout(B_VERTICAL));

	// TODO: use all the additional height
	AddChild(BGroupLayoutBuilder(B_HORIZONTAL, 10)
		.Add(fScrollView)
		//.Add(BSpaceLayoutItem::CreateHorizontalStrut(5))
		.Add(BGroupLayoutBuilder(B_VERTICAL)
						.Add(addButton)
						.Add(removeButton)
						.AddGlue()
						.Add(availButton)
						.AddGlue()
						.Add(pairButton)
						.Add(blockButton)
						.AddGlue()
						.SetInsets(0, 15, 0, 15)
			)
		.SetInsets(5, 5, 5, 100)
	);

	fDeviceList->SetSelectionMessage(NULL);
}

RemoteDevicesView::~RemoteDevicesView(void)
{

}

void
RemoteDevicesView::AttachedToWindow(void)
{
	fDeviceList->SetTarget(this);
	addButton->SetTarget(this);
	removeButton->SetTarget(this);
	pairButton->SetTarget(this);
	blockButton->SetTarget(this);
	availButton->SetTarget(this);

	LoadSettings();
	fDeviceList->Select(0);
}

void
RemoteDevicesView::MessageReceived(BMessage *msg)
{
	printf("what = %ld\n", msg->what);
	switch(msg->what) {
		case kMsgAddDevices:
		{
			InquiryPanel* iPanel = new InquiryPanel(BRect(100,100,450,450), ActiveLocalDevice);
			iPanel->Show();
			break;
		}

		case kMsgRemoveDevice:
			printf("kMsgRemoveDevice: %ld\n", fDeviceList->CurrentSelection(0));
			fDeviceList->RemoveItem(fDeviceList->CurrentSelection(0));
			break;
		case kMsgAddToRemoteList:
		{
			BListItem* device;
			msg->FindPointer("device", (void**)&device);
			fDeviceList->AddItem(device);
			fDeviceList->Invalidate();
			break;
		}

		case kMsgPairDevice:
		{
			bdaddr_t address;
			address.b[5] = 0x55;
			address.b[4] = 0x44;
			address.b[3] = 0x33;
			address.b[2] = 0x22;
			address.b[1] = 0x11;
			address.b[0] = 0x00;

			PincodeWindow* iPincode = new PincodeWindow(address, 0);
			iPincode->Show();
			break;
		}

		default:
			BView::MessageReceived(msg);
			break;
	}
}

void RemoteDevicesView::LoadSettings(void)
{

}

bool RemoteDevicesView::IsDefaultable(void)
{
	return true;
}

