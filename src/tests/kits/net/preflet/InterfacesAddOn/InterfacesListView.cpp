/*
 * Copyright 2004-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Philippe Houdoin
 * 		Fredrik Modéen
 */


#include "InterfacesListView.h"
#include "Setting.h"

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <IconUtils.h>
#include <File.h>
#include <Resources.h>

#include <AutoDeleter.h>
#include <net_notifications.h>



class SocketOpener {
public:
	SocketOpener()
	{
		fSocket = socket(AF_INET, SOCK_DGRAM, 0);
	}

	~SocketOpener()
	{
		close(fSocket);
	}

	status_t InitCheck()
	{
		return fSocket >= 0 ? B_OK : B_ERROR;
	}

	operator int() const
	{
		return fSocket;
	}

private:
	int	fSocket;
};




// #pragma mark -


status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char *)our_image >= (char *)image.text
			&& (char *)our_image <= (char *)image.text + image.text_size)
			return B_OK;
	}

	return B_ERROR;
}


InterfaceListItem::InterfaceListItem(const char* name)
	: 
	BListItem(0, false),
	fEnabled(true),
	fIcon(NULL), 	
	fSetting(new Setting(name))
{
	_InitIcon();
}


InterfaceListItem::~InterfaceListItem()
{
	delete fIcon;
}


void InterfaceListItem::Update(BView* owner, const BFont* font)
{	
	BListItem::Update(owner,font);	
	font_height height;
	font->GetHeight(&height);

	// TODO: take into account icon height, if he's taller...
	SetHeight((height.ascent+height.descent+height.leading) * 3.0 + 8);
}


void
InterfaceListItem::DrawItem(BView* owner, BRect /*bounds*/, bool complete)
{
	BListView* list = dynamic_cast<BListView*>(owner);
	if (!list)
		return;
		
	font_height height;
	BFont font;
	owner->GetFont(&font);
	font.GetHeight(&height);
	float fntheight = height.ascent+height.descent+height.leading;

	BRect bounds = list->ItemFrame(list->IndexOf(this));		
								
	rgb_color oldviewcolor = owner->ViewColor();
	rgb_color oldlowcolor = owner->LowColor();
	rgb_color oldcolor = owner->HighColor();

	rgb_color color = oldviewcolor;
	if ( IsSelected() ) 
		color = tint_color(color, B_HIGHLIGHT_BACKGROUND_TINT);

	owner->SetViewColor( color );
	owner->SetHighColor( color );
	owner->SetLowColor( color );
	owner->FillRect(bounds);

	owner->SetViewColor( oldviewcolor);
	owner->SetLowColor( oldlowcolor );
	owner->SetHighColor( oldcolor );

	BPoint iconPt = bounds.LeftTop() + BPoint(4,4);
	BPoint namePt = iconPt + BPoint(32+8, fntheight);
	BPoint driverPt = iconPt + BPoint(32+8, fntheight*2);
	BPoint commentPt = iconPt + BPoint(32+8, fntheight*3);
		
	drawing_mode mode = owner->DrawingMode();
	if (fEnabled)
		owner->SetDrawingMode(B_OP_OVER);
	else {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		owner->SetHighColor(0, 0, 0, 32);
	}
	
	owner->DrawBitmapAsync(fIcon, iconPt);

	if (!fEnabled)
		owner->SetHighColor(tint_color(oldcolor, B_LIGHTEN_1_TINT));

	owner->SetFont(be_bold_font);
	owner->DrawString(Name(), namePt);
	owner->SetFont(be_plain_font);

	if (fEnabled) {
		BString str("Enabled, IPv4 address: ");
		str << fSetting->IP();
		owner->DrawString(str.String(), driverPt);
		if (fSetting->AutoConfigured())
			owner->DrawString("DHCP enabled", commentPt);
		else
			owner->DrawString("DHCP disabled, use static IP address", commentPt);

		
	} else 
		owner->DrawString("Disabled.", driverPt);

	owner->SetHighColor(oldcolor);
	owner->SetDrawingMode(mode);
}


void
InterfaceListItem::_InitIcon()
{
	BBitmap* icon = NULL;
	
	const char* mediaTypeName = "";
	int media = fSetting->Media();
	printf("%s media = 0x%x\n", Name(), media);
	switch (IFM_TYPE(media)) {
		case IFM_ETHER:
			mediaTypeName = "ether";
			break;
		case IFM_IEEE80211:
			mediaTypeName = "wifi";
			break;
	}

	image_info info;
	if (our_image(info) != B_OK)
		return;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return;

	BResources resources(&file);
	if (resources.InitCheck() < B_OK)
		return;

	size_t size;
	// Try specific interface icon?
	const uint8* rawIcon = (const uint8*)resources.LoadResource(B_VECTOR_ICON_TYPE, Name(), &size);
	if (!rawIcon)
		// Not found, try interface media type?
		rawIcon = (const uint8*)resources.LoadResource(B_VECTOR_ICON_TYPE, mediaTypeName, &size);
	if (!rawIcon)
		// Not found, try default interface icon?
		rawIcon = (const uint8*)resources.LoadResource(B_VECTOR_ICON_TYPE, "wifi", &size);

	if (rawIcon) {
		// Now build the bitmap
		icon = new BBitmap(BRect(0, 0, 31, 31), 0, B_RGBA32);
		if (BIconUtils::GetVectorIcon(rawIcon, size, icon) == B_OK)
			fIcon = icon;
		else
			delete icon;
	}
}

// #pragma mark -


InterfacesListView::InterfacesListView(BRect rect, const char* name, uint32 resizingMode)
	: BListView(rect, name, B_SINGLE_SELECTION_LIST, resizingMode)
{
}


InterfacesListView::~InterfacesListView()
{
}


void
InterfacesListView::AttachedToWindow()
{
	BListView::AttachedToWindow();

	_InitList();
	
	start_watching_network(
		B_WATCH_NETWORK_INTERFACE_CHANGES | B_WATCH_NETWORK_LINK_CHANGES, this);
}


void
InterfacesListView::DetachedFromWindow()
{
	BListView::DetachedFromWindow();

	stop_watching_network(this);

	// free all items, they will be retrieved again in AttachedToWindow()
	for (int32 i = CountItems(); i-- > 0;) {
		delete ItemAt(i);
	}
	MakeEmpty();
}


void
InterfacesListView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NETWORK_MONITOR:
			message->PrintToStream();
			_UpdateList();
			break;

		default:
			BListView::MessageReceived(message);
	}
}


InterfaceListItem *
InterfacesListView::FindItem(const char* name)
{
	for (int32 i = CountItems(); i-- > 0;) {
		InterfaceListItem* item = dynamic_cast<InterfaceListItem*>(ItemAt(i));
		if (item == NULL)
			continue;

		if (strcmp(item->Name(), name) == 0)
			return item;
	}

	return NULL;
}


status_t
InterfacesListView::_InitList()
{
	SocketOpener socket;
	if (socket.InitCheck() != B_OK)
		return B_ERROR;

	// iterate over all interfaces and retrieve minimal status
	ifconf config;
	config.ifc_len = sizeof(config.ifc_value);
	if (ioctl(socket, SIOCGIFCOUNT, &config, sizeof(struct ifconf)) < 0)
		return B_ERROR;

	uint32 count = (uint32)config.ifc_value;
	if (count == 0)
		return B_ERROR;

	void* buffer = malloc(count * sizeof(struct ifreq));
	if (buffer == NULL)
		return B_ERROR;

	MemoryDeleter deleter(buffer);
	
	config.ifc_len = count * sizeof(struct ifreq);
	config.ifc_buf = buffer;
	if (ioctl(socket, SIOCGIFCONF, &config, sizeof(struct ifconf)) < 0)
		return B_ERROR;

	ifreq* interface = (ifreq*)buffer;
	MakeEmpty();

	for (uint32 i = 0; i < count; i++) {
		// if (strcmp(interface->ifr_name, "loop") != 0) {
			AddItem(new InterfaceListItem(interface->ifr_name));
	//		printf("Name = %s\n", interface->ifr_name);
		// }
		interface = (ifreq*)((addr_t)interface + IF_NAMESIZE 
			+ interface->ifr_addr.sa_len);
	}	
	return B_OK;
}


status_t
InterfacesListView::_UpdateList()
{
	// TODO
	return B_OK;
}

