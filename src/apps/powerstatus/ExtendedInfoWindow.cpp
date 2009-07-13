/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Clemens Zeidler, haiku@Clemens-Zeidler.de
 */

#include "ExtendedInfoWindow.h"

#include <Box.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <SpaceLayoutItem.h>
#include <String.h>


FontString::FontString()
{
	font = be_plain_font;
}


const int kLineSpacing = 5;

BatteryInfoView::BatteryInfoView()
	:
	BView("battery info view", B_WILL_DRAW |
		B_FULL_UPDATE_ON_RESIZE),
	fPreferredSize(200, 200),
	fMaxStringSize(0, 0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


BatteryInfoView::~BatteryInfoView()
{
	_ClearStringList();
}


void
BatteryInfoView::Update(battery_info& info, acpi_extended_battery_info& extInfo)
{
	fBatteryInfo = info;
	fBatteryExtendedInfo = extInfo;
	
	_FillStringList();
}


void
BatteryInfoView::Draw(BRect updateRect)
{
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	BPoint point(10, 10);
	
	float space = _MeasureString("").height + kLineSpacing;

	for (int i = 0; i < fStringList.CountItems(); i ++)
	{
		FontString* fontString = fStringList.ItemAt(i);
		SetFont(fontString->font);
		DrawString(fontString->string.String(), point);
		point.y += space;
	}	
}		


void
BatteryInfoView::GetPreferredSize(float *width, float *height)
{
	*width = fPreferredSize.width;
	*height = fPreferredSize.height;
}


BSize
BatteryInfoView::_MeasureString(const BString& string)
{
	BFont font;
	GetFont(&font);
	BSize size;
	
	size.width = font.StringWidth(string);

	font_height height;
	font.GetHeight(&height);
	size.height = height.ascent + height.descent;

	return size;
}


void
BatteryInfoView::_FillStringList()
{
	_ClearStringList();

	BString powerUnit;
	BString rateUnit;
	switch (fBatteryExtendedInfo.power_unit) {
		case 0:
			powerUnit = " mWh";
			rateUnit = " mW";
			break;
		
		case 1:
			powerUnit = " mAh";
			rateUnit = " mA";
			break;
	}

	FontString* fontString;

	fontString = new FontString;
	fStringList.AddItem(fontString);
	fontString->font = be_bold_font;
	
	if (fBatteryInfo.state & BATTERY_CHARGING)
		fontString->string = "Battery charging";
	else if (fBatteryInfo.state & BATTERY_DISCHARGING)
		fontString->string = "Battery discharging";
	else if (fBatteryInfo.state & BATTERY_CRITICAL_STATE)
		fontString->string = "Empty Battery Slot";
	else
		fontString->string = "Battery unused";
		
	fontString = new FontString;
	fontString->string = "Capacity: ";
	fontString->string << fBatteryInfo.capacity;
	fontString->string << powerUnit;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Last full Charge: ";
	fontString->string << fBatteryInfo.full_capacity;
	fontString->string << powerUnit;
	_AddToStringList(fontString);
	
	fontString = new FontString;
	fontString->string = "Current Rate: ";
	fontString->string << fBatteryInfo.current_rate;
	fontString->string << rateUnit;
	_AddToStringList(fontString);
	
	// empty line
	fontString = new FontString;
	_AddToStringList(fontString);
	
	fontString = new FontString;
	fontString->string = "Design Capacity: ";
	fontString->string << fBatteryExtendedInfo.design_capacity;
	fontString->string << powerUnit;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Technology: ";
	fontString->string << fBatteryExtendedInfo.technology;
	_AddToStringList(fontString);
	
	fontString = new FontString;
	fontString->string = "Design Voltage: ";
	fontString->string << fBatteryExtendedInfo.design_voltage;
	fontString->string << " mV";
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Design Capacity Warning: ";
	fontString->string << fBatteryExtendedInfo.design_capacity_warning;
	fontString->string << powerUnit;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Design Capacity low Warning: ";
	fontString->string << fBatteryExtendedInfo.design_capacity_low;
	fontString->string << powerUnit;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Capacity Granularity 1: ";
	fontString->string << fBatteryExtendedInfo.capacity_granularity_1;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Capacity Granularity 2: ";
	fontString->string << fBatteryExtendedInfo.capacity_granularity_2;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Model Number: ";
	fontString->string << fBatteryExtendedInfo.model_number;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Serial number: ";
	fontString->string << fBatteryExtendedInfo.serial_number;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "Type: ";
	fontString->string += fBatteryExtendedInfo.type;
	_AddToStringList(fontString);
		
	fontString = new FontString;
	fontString->string = "OEM Info: ";
	fontString->string += fBatteryExtendedInfo.oem_info;
	_AddToStringList(fontString);
	
	fPreferredSize.width = fMaxStringSize.width + 10;
	fPreferredSize.height = (fMaxStringSize.height + kLineSpacing) *
		fStringList.CountItems();
}


void
BatteryInfoView::_AddToStringList(FontString* fontString)
{
	fStringList.AddItem(fontString);
	BSize stringSize = _MeasureString(fontString->string);
	if (fMaxStringSize.width < stringSize.width)
		fMaxStringSize = stringSize;
}


void
BatteryInfoView::_ClearStringList()
{
	for (int i = 0; i < fStringList.CountItems(); i ++)
		delete fStringList.ItemAt(i);
	fStringList.MakeEmpty();
	fMaxStringSize = BSize(0, 0);
}


ExtPowerStatusView::ExtPowerStatusView(PowerStatusDriverInterface* interface,
			BRect frame, int32 resizingMode, int batteryId,
			ExtendedInfoWindow* window)
	:
	PowerStatusView(interface, frame, resizingMode, batteryId),
	fExtendedInfoWindow(window),
	fBatteryInfoView(window->GetExtendedBatteryInfoView()),
	fSelected(false)
{
			
}


void
ExtPowerStatusView::Draw(BRect updateRect)
{
	if (fSelected) {
		SetLowColor(102, 152, 203);
		SetHighColor(102, 152, 203);
		FillRect(updateRect);	
	}
	else {
		SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(updateRect);
	}

	PowerStatusView::Draw(updateRect);
}


void
ExtPowerStatusView::MouseDown(BPoint where)
{
	if (!fSelected) {
		fSelected = true;
		_Update(true);
		fExtendedInfoWindow->BatterySelected(this);
	}
}		


void
ExtPowerStatusView::Select(bool select)
{
	fSelected = select;
	_Update(true);
}


bool
ExtPowerStatusView::IsValid()
{
	if (fBatteryInfo.state & BATTERY_CRITICAL_STATE)
		return false;
	return true;
}


void
ExtPowerStatusView::_Update(bool force)
{
	PowerStatusView::_Update(force);
	if (!fSelected)
		return;
		
	acpi_extended_battery_info extInfo;
	fDriverInterface->GetExtendedBatteryInfo(&extInfo, fBatteryId);

	fBatteryInfoView->Update(fBatteryInfo, extInfo);
	fBatteryInfoView->Invalidate();
}


ExtendedInfoWindow::ExtendedInfoWindow(PowerStatusDriverInterface* interface)
	:
	BWindow(BRect(100, 150, 500, 500), "Extended Battery Info", B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AVOID_FRONT |
		B_ASYNCHRONOUS_CONTROLS),
	fDriverInterface(interface),
	fSelectedView(NULL)
{
	BView *view = new BView(Bounds(), "view", B_FOLLOW_ALL, 0);
	view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(view);

	BGroupLayout* mainLayout = new BGroupLayout(B_VERTICAL);
	mainLayout->SetSpacing(10);
	mainLayout->SetInsets(10, 10, 10, 10);
	view->SetLayout(mainLayout);
	
	BRect rect = Bounds();
	rect.InsetBy(5, 5);
	BBox *infoBox = new BBox(rect, "Power Status Box");
	infoBox->SetLabel("Battery Info");
	BGroupLayout* infoLayout = new BGroupLayout(B_HORIZONTAL);
	infoLayout->SetInsets(10, infoBox->TopBorderOffset() * 2 + 10, 10, 10);
	infoLayout->SetSpacing(10);
	infoBox->SetLayout(infoLayout);
	mainLayout->AddView(infoBox);
	
	BGroupView* batteryView = new BGroupView(B_VERTICAL);
	batteryView->GroupLayout()->SetSpacing(10);
	infoLayout->AddView(batteryView);

	// create before the battery views
	fBatteryInfoView = new BatteryInfoView();
	
	BGroupLayout* batteryLayout = batteryView->GroupLayout();
	BRect batteryRect(0, 0, 50, 30);
	for (int i = 0; i < interface->GetBatteryCount(); i++) {
		ExtPowerStatusView* view = new ExtPowerStatusView(interface,
			batteryRect, B_FOLLOW_NONE, i, this);
		view->SetExplicitMaxSize(BSize(70, 80));
		view->SetExplicitMinSize(BSize(70, 80));

		batteryLayout->AddView(view);
		fBatteryViewList.AddItem(view);
		fDriverInterface->StartWatching(view);
		if (view->IsValid())
			fSelectedView = view;
	}

	batteryLayout->AddItem(BSpaceLayoutItem::CreateGlue());	
	
	infoLayout->AddView(fBatteryInfoView);
	
	if (!fSelectedView && fBatteryViewList.CountItems() > 0)
		fSelectedView = fBatteryViewList.ItemAt(0);	
	fSelectedView->Select();
	
	BSize size = mainLayout->PreferredSize();
	ResizeTo(size.width, size.height);
}


ExtendedInfoWindow::~ExtendedInfoWindow()
{
	for (int i = 0; i < fBatteryViewList.CountItems(); i++) {
		fDriverInterface->StopWatching(fBatteryViewList.ItemAt(i));
	}
}


BatteryInfoView*
ExtendedInfoWindow::GetExtendedBatteryInfoView()
{
	return fBatteryInfoView;
}


void
ExtendedInfoWindow::BatterySelected(ExtPowerStatusView* view)
{
	if (fSelectedView) {
		fSelectedView->Select(false);
		fSelectedView->Invalidate();
	}

	fSelectedView = view;
}