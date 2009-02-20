/*
 * Copyright 2008-09, Oliver Ruiz Dorantes, <oliver.ruiz.dorantes_at_gmail.com>
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <Alert.h>
#include <Button.h>
#include <GroupLayoutBuilder.h>
#include <StatusBar.h>
#include <SpaceLayoutItem.h>
#include <TextView.h>
#include <TabView.h>
#include <ListView.h>
#include <ListItem.h>
#include <MessageRunner.h>

#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/bdaddrUtils.h>

#include "InquiryPanel.h"

// private funcionaility provided by kit
extern uint8 GetInquiryTime();

static const uint32 kMsgStart = 'InSt';
static const uint32 kMsgFinish = 'InFn';
static const uint32 kMsgShowDebug = 'ShDG';

static const uint32 kMsgInquiry = 'iQbt';
static const uint32 kMsgAddListDevice = 'aDdv';

static const uint32 kMsgAddToRemoteList = 'aDdL';
static const uint32 kMsgSecond = 'sCMs';

// TODO: Implement a BluetoothDeviceListItem class, this one is stolen from somewhere .....
class RangeItem : public BListItem
{
	public:
		RangeItem(uint32 lowAddress, uint32 highAddress, const char* name);
		~RangeItem();
		virtual void DrawItem(BView *, BRect, bool = false);
		static int Compare(const void *firstArg, const void *secondArg);
	private:
		char* fName;
		uint32 fLowAddress, fHighAddress;
};

RangeItem::RangeItem(uint32 lowAddress, uint32 highAddress, const char* name)
	: BListItem(),
	fLowAddress(lowAddress), 
	fHighAddress(highAddress)
{
	fName = strdup(name);
}

RangeItem::~RangeItem()
{
	delete fName;
}

/***********************************************************
 * DrawItem
 ***********************************************************/
void
RangeItem::DrawItem(BView *owner, BRect itemRect, bool complete)
{
	rgb_color kBlack = { 0,0,0,0 };
	rgb_color kHighlight = { 156,154,156,0 };
	
	if (IsSelected() || complete) {
		rgb_color color;
		if (IsSelected())
			color = kHighlight;
		else
			color = owner->ViewColor();
		
		owner->SetHighColor(color);
		owner->SetLowColor(color);
		owner->FillRect(itemRect);
		owner->SetHighColor(kBlack);
		
	} else {
		owner->SetLowColor(owner->ViewColor());
	}
	
	BFont font = be_plain_font;
	font_height	finfo;
	font.GetHeight(&finfo);
	
	BPoint point = BPoint(itemRect.left + 17, itemRect.bottom - finfo.descent + 1);
	owner->SetFont(be_fixed_font);
	owner->SetHighColor(kBlack);
	owner->MovePenTo(point);
	
/*	if (fLowAddress >= 0) {
		char string[255];
		sprintf(string, "0x%04lx - 0x%04lx", fLowAddress, fHighAddress);
		owner->DrawString(string);
	}
	point += BPoint(174, 0);*/
	owner->SetFont(be_plain_font);
	owner->MovePenTo(point);
	owner->DrawString(fName);
}

int 
RangeItem::Compare(const void *firstArg, const void *secondArg)
{
	const RangeItem *item1 = *static_cast<const RangeItem * const *>(firstArg);
	const RangeItem *item2 = *static_cast<const RangeItem * const *>(secondArg);
	
	if (item1->fLowAddress < item2->fLowAddress) {
		return -1;
	} else if (item1->fLowAddress > item2->fLowAddress) {
		return 1;
	} else 
		return 0;

}



class PanelDiscoveryListener : public DiscoveryListener {

public:

	PanelDiscoveryListener(InquiryPanel* iPanel) : DiscoveryListener() , fInquiryPanel(iPanel)
	{

	}


	void
	DeviceDiscovered(RemoteDevice* btDevice, DeviceClass cod)
	{
		BMessage* message = new BMessage(kMsgAddListDevice);
		message->AddPointer("remote", btDevice);
		
		fInquiryPanel->PostMessage(message);
	}


	void
	InquiryCompleted(int discType)
	{
		BMessage* message = new BMessage(kMsgFinish);
		fInquiryPanel->PostMessage(message);
	}


	void
	InquiryStarted(status_t status)
	{
		BMessage* message = new BMessage(kMsgStart);
		fInquiryPanel->PostMessage(message);
	}

private:
	InquiryPanel*	fInquiryPanel;

};


InquiryPanel::InquiryPanel(BRect frame, LocalDevice* lDevice)
 :	BWindow(frame, "Bluetooth", B_FLOATING_WINDOW,
 		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS,
 		B_ALL_WORKSPACES ), fScanning(false)
 						  , fLocalDevice(lDevice)
{
	BRect iDontCare(0,0,0,0);

	SetLayout(new BGroupLayout(B_HORIZONTAL));

	fScanProgress = new BStatusBar(iDontCare, "status", "Scanning progress", "");
	activeColor = fScanProgress->BarColor();

	if (fLocalDevice == NULL)
		fLocalDevice = LocalDevice::GetLocalDevice();

	fMessage = new BTextView(iDontCare, "description",
		iDontCare, B_FOLLOW_NONE, B_WILL_DRAW | B_SUPPORTS_LAYOUT);
	fMessage->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fMessage->SetLowColor(fMessage->ViewColor());
	fMessage->MakeEditable(false);
	fMessage->MakeSelectable(false);
	
	fInquiryButton = new BButton("Inquiry", "Inquiry",
		new BMessage(kMsgInquiry), B_WILL_DRAW);
		
	fAddButton = new BButton("add", "Add device to list",
		new BMessage(kMsgAddToRemoteList), B_WILL_DRAW);		
	fAddButton->SetEnabled(false);

	fRemoteList = new BListView("AttributeList", B_SINGLE_SELECTION_LIST);


	if (fLocalDevice != NULL) {	
		fMessage->SetText("Check that the bluetooth capabilities of your remote device"
							" are activated. Press Inquiry to start scanning");
		fInquiryButton->SetEnabled(true);
		fDiscoveryAgent = fLocalDevice->GetDiscoveryAgent();
		fDiscoveryListener = new PanelDiscoveryListener(this);
		
		
		SetTitle((const char*)(fLocalDevice->GetFriendlyName().String()));
		
		
	} else {
		fMessage->SetText("There has not been found any bluetooth LocalDevice device registered"
							" on the system");
		fInquiryButton->SetEnabled(false);
	}

	fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgSecond), 1000000L, -1);


	AddChild(BGroupLayoutBuilder(B_VERTICAL, 10)
		.Add(fMessage)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(5))
		.Add(fScanProgress)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(5))
		.Add(fRemoteList)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(5))
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)
			.Add(fAddButton)
			.AddGlue()
			.Add(fInquiryButton)
		)
		.SetInsets(15, 25, 15, 15)
	);
}


void
InquiryPanel::MessageReceived(BMessage *message)
{
	static float timer = 0; // expected time of the inquiry process
	static float scanningTime = 0;
	
	switch (message->what) {
		case kMsgInquiry:
			
			fDiscoveryAgent->StartInquiry(BT_GIAC, fDiscoveryListener, GetInquiryTime());
			
			timer = BT_BASE_INQUIRY_TIME * GetInquiryTime();			
			fScanProgress->SetMaxValue(timer); // does it works as expected?

		break;
		
		case kMsgAddListDevice:
		{
			RemoteDevice* rDevice;
			
			message->FindPointer("remote", (void **)&rDevice);	
			
			fRemoteList->AddItem(new RangeItem(0,1,bdaddrUtils::ToString(rDevice->GetBluetoothAddress())));		
		}
		break;

		case kMsgStart:
			fRemoteList->MakeEmpty();
			fScanProgress->Reset();
			
			scanningTime = 0;			
			fScanning = true;			
			UpdateUIStatus();
			
		break;

		case kMsgFinish:
			
			fScanning = false;
			UpdateUIStatus();
			
		break;
		
		case kMsgSecond:
		{
			if (fScanning) {
				BString elapsedTime = "Remaining ";
				
				fScanProgress->SetTo(scanningTime*100/timer); // TODO should not be needed if SetMaxValue works...
								
				elapsedTime << (int)(timer - scanningTime) << " seconds";
				fScanProgress->SetTrailingText(elapsedTime.String());

				scanningTime = scanningTime + 1;
			}
			
			if (fRemoteList->CurrentSelection() < 0)
				fAddButton->SetEnabled(false);
			else
				fAddButton->SetEnabled(true);
		}
		break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
InquiryPanel::UpdateUIStatus(void)
{
	if (fScanning) {
		fAddButton->SetEnabled(false);
		fInquiryButton->SetEnabled(false);
		fScanProgress->SetBarColor(activeColor);
			
	} else {
		fInquiryButton->SetEnabled(true);
		fScanProgress->SetBarColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		fScanProgress->SetTo(100);
		fScanProgress->SetText("Scan completed");		
	}
}


bool
InquiryPanel::QuitRequested(void)
{

	return true;
}
