/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Hugo Santos, hugosantos@gmail.com
 *		Dario Casalinuovo
 */


#include "NetworkStatusView.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <unistd.h>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <Drivers.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>
#include <TextView.h>

#include <net_notifications.h>

#include "NetworkStatus.h"
#include "NetworkStatusIcons.h"


static const char *kStatusDescriptions[] = {
	"Unknown",
	"No Link",
	"No stateful configuration",
	"Configuring",
	"Ready"
};

extern "C" _EXPORT BView *instantiate_deskbar_item(void);


const uint32 kMsgShowConfiguration = 'shcf';
const uint32 kMsgOpenNetworkPreferences = 'onwp';

const uint32 kMinIconWidth = 16;
const uint32 kMinIconHeight = 16;


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


//	#pragma mark -


NetworkStatusView::NetworkStatusView(BRect frame, int32 resizingMode,
		bool inDeskbar)
	: BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_FRAME_EVENTS),
	fInDeskbar(inDeskbar),
	fStatus(kStatusUnknown)
{
	_Init();

	if (!inDeskbar) {
		// we were obviously added to a standard window - let's add a dragger
		frame.OffsetTo(B_ORIGIN);
		frame.top = frame.bottom - 7;
		frame.left = frame.right - 7;
		BDragger* dragger = new BDragger(frame, this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(dragger);
	} else
		_Update();
}


NetworkStatusView::NetworkStatusView(BMessage* archive)
	: BView(archive),
	fInDeskbar(false)
{
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK
		&& !strcasecmp(info.signature, "application/x-vnd.Be-TSKB"))
		fInDeskbar = true;

	_Init();
}


NetworkStatusView::~NetworkStatusView()
{
}


void
NetworkStatusView::_Init()
{
	for (int i = 0; i < kStatusCount; i++) {
		fBitmaps[i] = NULL;
	}

	_UpdateBitmaps();
}


void
NetworkStatusView::_UpdateBitmaps()
{
	for (int i = 0; i < kStatusCount; i++) {
		delete fBitmaps[i];
		fBitmaps[i] = NULL;
	}

	image_info info;
	if (our_image(info) != B_OK)
		return;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return;

	BResources resources(&file);
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	if (resources.InitCheck() < B_OK)
		return;
#endif

	for (int i = 0; i < kStatusCount; i++) {
		const void* data = NULL;
		size_t size;
		data = resources.LoadResource(B_VECTOR_ICON_TYPE,
			kNetworkStatusNoDevice + i, &size);
		if (data != NULL) {
			BBitmap* icon = new BBitmap(Bounds(), B_RGBA32);
			if (icon->InitCheck() == B_OK
				&& BIconUtils::GetVectorIcon((const uint8 *)data,
					size, icon) == B_OK) {
				fBitmaps[i] = icon;
			} else
				delete icon;
		}
	}
}


void
NetworkStatusView::_Quit()
{
	if (fInDeskbar) {
		BDeskbar deskbar;
		deskbar.RemoveItem(kDeskbarItemName);
	} else
		be_app->PostMessage(B_QUIT_REQUESTED);
}


NetworkStatusView *
NetworkStatusView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "NetworkStatusView"))
		return NULL;

	return new NetworkStatusView(archive);
}


status_t
NetworkStatusView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSignature);
	if (status == B_OK)
		status = archive->AddString("class", "NetworkStatusView");

	return status;
}


void
NetworkStatusView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	SetLowColor(ViewColor());

	start_watching_network(
		B_WATCH_NETWORK_INTERFACE_CHANGES | B_WATCH_NETWORK_LINK_CHANGES, this);

	_Update();
}


void
NetworkStatusView::DetachedFromWindow()
{
	stop_watching_network(this);
}


void
NetworkStatusView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NETWORK_MONITOR:
			_Update();
			break;

		case kMsgShowConfiguration:
			_ShowConfiguration(message);
			break;

		case kMsgOpenNetworkPreferences:
			_OpenNetworksPreferences();
			break;

		case B_ABOUT_REQUESTED:
			_AboutRequested();
			break;

		case B_QUIT_REQUESTED:
			_Quit();
			break;

		default:
			BView::MessageReceived(message);
	}
}


void
NetworkStatusView::FrameResized(float width, float height)
{
	_UpdateBitmaps();
	Invalidate();
}


void
NetworkStatusView::Draw(BRect updateRect)
{
	if (fBitmaps[fStatus] == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);
	DrawBitmap(fBitmaps[fStatus]);
	SetDrawingMode(B_OP_COPY);
}


void
NetworkStatusView::_ShowConfiguration(BMessage* message)
{
	static const struct information_entry {
		const char*	label;
		int32		control;
	} kInformationEntries[] = {
		{ "Address", SIOCGIFADDR },
		{ "Broadcast", SIOCGIFBRDADDR },
		{ "Netmask", SIOCGIFNETMASK },
		{ NULL }
	};

	SocketOpener socket;
	if (socket.InitCheck() != B_OK)
		return;

	const char* name;
	if (message->FindString("interface", &name) != B_OK)
		return;

	ifreq request;
	if (!_PrepareRequest(request, name))
		return;

	BString text = name;
	text += " information:\n";
	size_t boldLength = text.Length();

	for (int i = 0; kInformationEntries[i].label; i++) {
		if (ioctl(socket, kInformationEntries[i].control, &request,
				sizeof(request)) < 0) {
			continue;
		}

		char address[32];
		sockaddr_in* inetAddress = NULL;
		switch (kInformationEntries[i].control) {
			case SIOCGIFNETMASK:
				inetAddress = (sockaddr_in*)&request.ifr_mask;
				break;
			default:
				inetAddress = (sockaddr_in*)&request.ifr_addr;
				break;
		}

		if (inet_ntop(AF_INET, &inetAddress->sin_addr, address,
				sizeof(address)) == NULL) {
			return;
		}

		text += "\n";
		text += kInformationEntries[i].label;
		text += ": ";
		text += address;
	}

	BAlert* alert = new BAlert(name, text.String(), "Ok");
	BTextView* view = alert->TextView();
	BFont font;

	view->SetStylable(true);
	view->GetFont(&font);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, boldLength, &font);

	alert->Go(NULL);
}


void
NetworkStatusView::MouseDown(BPoint point)
{
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
	menu->SetAsyncAutoDestruct(true);
	menu->SetFont(be_plain_font);

	for (int32 i = 0; i < fInterfaces.CountItems(); i++) {
		BString& name = *fInterfaces.ItemAt(i);

		BString label = name;
		label += ": ";
		label += kStatusDescriptions[
			_DetermineInterfaceStatus(name.String())];

		BMessage* info = new BMessage(kMsgShowConfiguration);
		info->AddString("interface", name.String());
		menu->AddItem(new BMenuItem(label.String(), info));
	}

	menu->AddSeparatorItem();
	//menu->AddItem(new BMenuItem("About NetworkStatus" B_UTF8_ELLIPSIS,
	//	new BMessage(B_ABOUT_REQUESTED)));
	menu->AddItem(new BMenuItem("Open Networks Preferences" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenNetworkPreferences)));

	if (fInDeskbar)
		menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));
	menu->SetTargetForItems(this);

	ConvertToScreen(&point);
	menu->Go(point, true, true, true);
}


void
NetworkStatusView::_AboutRequested()
{
	BAlert* alert = new BAlert("about", "NetworkStatus\n"
		"\twritten by Axel Dörfler and Hugo Santos\n"
		"\tCopyright 2007, Haiku, Inc.\n", "Ok");
	BTextView *view = alert->TextView();
	BFont font;

	view->SetStylable(true);

	view->GetFont(&font);
	font.SetSize(18);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, 13, &font);

	alert->Go();
}


bool
NetworkStatusView::_PrepareRequest(struct ifreq& request, const char* name)
{
	if (strlen(name) > IF_NAMESIZE)
		return false;

	strcpy(request.ifr_name, name);
	return true;
}


int32
NetworkStatusView::_DetermineInterfaceStatus(const char* name)
{
	SocketOpener socket;
	if (socket.InitCheck() != B_OK)
		return kStatusUnknown;

	ifreq request;
	if (!_PrepareRequest(request, name))
		return kStatusUnknown;

	uint32 flags = 0;
	if (ioctl(socket, SIOCGIFFLAGS, &request, sizeof(struct ifreq)) == 0)
		flags = request.ifr_flags;

	int32 status = kStatusNoLink;

	// TODO: no kStatusLinkNoConfig yet

	if (flags & IFF_CONFIGURING)
		status = kStatusConnecting;
	else if ((flags & (IFF_UP | IFF_LINK)) == (IFF_UP | IFF_LINK))
		status = kStatusReady;

	return status;
}


void
NetworkStatusView::_Update(bool force)
{
	SocketOpener socket;
	if (socket.InitCheck() != B_OK)
		return;

	// iterate over all interfaces and retrieve minimal status

	ifconf config;
	config.ifc_len = sizeof(config.ifc_value);
	if (ioctl(socket, SIOCGIFCOUNT, &config, sizeof(struct ifconf)) < 0)
		return;

	uint32 count = (uint32)config.ifc_value;
	if (count == 0)
		return;

	void* buffer = malloc(count * sizeof(struct ifreq));
	if (buffer == NULL)
		return;

	config.ifc_len = count * sizeof(struct ifreq);
	config.ifc_buf = buffer;
	if (ioctl(socket, SIOCGIFCONF, &config, sizeof(struct ifconf)) < 0) {
		free(buffer);
		return;
	}

	ifreq* interface = (ifreq*)buffer;

	int32 oldStatus = fStatus;
	fStatus = kStatusUnknown;
	fInterfaces.MakeEmpty();

	for (uint32 i = 0; i < count; i++) {
		if (strncmp(interface->ifr_name, "loop", 4) && interface->ifr_name[0]) {
			fInterfaces.AddItem(new BString(interface->ifr_name));
			int32 status = _DetermineInterfaceStatus(interface->ifr_name);
			if (status > fStatus)
				fStatus = status;
		}

		interface = (ifreq *)((addr_t)interface + IF_NAMESIZE
			+ interface->ifr_addr.sa_len);
	}

	free(buffer);

	if (fStatus != oldStatus)
		Invalidate();
}


void
NetworkStatusView::_OpenNetworksPreferences()
{
	status_t status = be_roster->Launch("application/x-vnd.Haiku-Network");
	if (status != B_OK && status != B_ALREADY_RUNNING) {
		BString errorMessage("Launching the Network preflet failed.\n\n"
			"Error: ");
		errorMessage << strerror(status);
		BAlert* alert = new BAlert("launch error", errorMessage.String(),
			"Ok");

		// asynchronous alert in order to not block replicant host application
		alert->Go(NULL);
	}
}


//	#pragma mark -


extern "C" _EXPORT BView *
instantiate_deskbar_item(void)
{
	return new NetworkStatusView(BRect(0, 0, 15, 15),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}

