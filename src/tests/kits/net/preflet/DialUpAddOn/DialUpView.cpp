/* -----------------------------------------------------------------------
 * Copyright (c) 2003-2004 Waldemar Kornewald, Waldemar.Kornewald@web.de
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 * ----------------------------------------------------------------------- */

#include "DialUpView.h"
#include "DialUpAddon.h"

#include <cstring>
#include "InterfaceUtils.h"
#include "MessageDriverSettingsUtils.h"
#include "TextRequestDialog.h"

// built-in add-ons
#include "ConnectionOptionsAddon.h"
#include "GeneralAddon.h"
#include "IPCPAddon.h"
#include "PPPoEAddon.h"

#include <PPPInterface.h>
#include <settings_tools.h>
#include <TemplateList.h>

#include <Application.h>

#include <Alert.h>
#include <Button.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TabView.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>


// message constants
static const uint32 kMsgCreateNew = 'NEWI';
static const uint32 kMsgFinishCreateNew = 'FNEW';
static const uint32 kMsgDeleteCurrent = 'DELI';
static const uint32 kMsgSelectInterface = 'SELI';
static const uint32 kMsgConnectButton = 'CONI';

// labels
#ifdef LANG_GERMAN
static const char *kLabelInterface = "Verbindung: ";
static const char *kLabelInterfaceName = "Verbindungs-Name: ";
static const char *kLabelCreateNewInterface = "Neue Verbindung Erstellen";
static const char *kLabelCreateNew = "Neu...";
static const char *kLabelDeleteCurrent = "Auswahl Löschen";
static const char *kLabelConnect = "Verbinden";
static const char *kLabelDisconnect = "Trennen";
static const char *kLabelOK = "OK";
#else
static const char *kLabelInterface = "Interface: ";
static const char *kLabelInterfaceName = "Interface Name: ";
static const char *kLabelCreateNewInterface = "Create New Interface";
static const char *kLabelCreateNew = "Create New...";
static const char *kLabelDeleteCurrent = "Delete Current";
static const char *kLabelConnect = "Connect";
static const char *kLabelDisconnect = "Disconnect";
static const char *kLabelOK = "OK";
#endif

// connection status strings
#ifdef LANG_GERMAN
static const char *kTextConnecting = "Verbinde...";
static const char *kTextConnectionEstablished = "Verbindung hergestellt.";
static const char *kTextNotConnected = "Nicht verbunden.";
static const char *kTextDeviceUpFailed = "Konnte Verbindung nicht aufbauen.";
static const char *kTextAuthenticating = "Authentifizierung...";
static const char *kTextAuthenticationFailed = "Authentifizierung fehlgeschlagen!";
static const char *kTextConnectionLost = "Verbindung verloren!";
static const char *kTextCreationError = "Fehler beim Initialisieren!";
static const char *kTextNoInterfacesFound = "Bitte erstellen Sie eine neue "
											"Verbindung.";
static const char *kTextChooseInterfaceName = "Bitte denken Sie sich einen neuen "
											"Namen für diese Verbindung aus.";
#else
static const char *kTextConnecting = "Connecting...";
static const char *kTextConnectionEstablished = "Connection established.";
static const char *kTextNotConnected = "Not connected.";
static const char *kTextDeviceUpFailed = "Failed to connect.";
static const char *kTextAuthenticating = "Authenticating...";
static const char *kTextAuthenticationFailed = "Authentication failed!";
static const char *kTextConnectionLost = "Connection lost!";
static const char *kTextCreationError = "Error creating interface!";
static const char *kTextNoInterfacesFound = "Please create a new interface...";
static const char *kTextChooseInterfaceName = "Please choose a new name for this "
											"interface.";
#endif

// error strings for alerts
#ifdef LANG_GERMAN
static const char *kErrorTitle = "Fehler";
static const char *kErrorNoPPPStack = "Fehler: Kein Zugriff auf den PPP Stack!";
static const char *kErrorInterfaceExists = "Fehler: Eine Verbindung mit diesem Namen "
										"existiert bereits!";
static const char *kErrorLoadingFailed = "Fehler: Konfiguration ist fehlerhaft! Die "
										"Einstellungen werden zurückgesetzt.";
static const char *kErrorSavingFailed = "Fehler: Speichern der Einstellungen ist "
										"fehlgeschlagen!";
#else
static const char *kErrorTitle = "Error";
static const char *kErrorNoPPPStack = "Error: Could not access the PPP stack!";
static const char *kErrorInterfaceExists = "Error: An interface with this name "
										"already exists!";
static const char *kErrorLoadingFailed = "Error: Failed loading interface! The "
										"current settings will be deleted.";
static const char *kErrorSavingFailed = "Error: Failed saving interface settings!";
#endif


static
status_t
up_down_thread(void *data)
{
	static_cast<DialUpView*>(data)->UpDownThread();
	return B_OK;
}


DialUpView::DialUpView(BRect frame)
	: BView(frame, "DialUpView", B_FOLLOW_ALL, 0),
	fListener(this),
	fUpDownThread(-1),
	fDriverSettings(NULL),
	fCurrentItem(NULL),
	fWatching(PPP_UNDEFINED_INTERFACE_ID),
	fKeepLabel(false)
{
	BRect bounds = Bounds();
		// for caching
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	// add messenger to us so add-ons can contact us
	BMessenger messenger(this);
	fAddons.AddMessenger(DUN_MESSENGER, messenger);
	
	// create pop-up with all interfaces and "New..."/"Delete current" items
	fInterfaceMenu = new BPopUpMenu(kLabelCreateNew);
	BRect rect = bounds;
	rect.InsetBy(5, 5);
	rect.bottom = rect.top + 20;
	fMenuField = new BMenuField(rect, "Interfaces", kLabelInterface, fInterfaceMenu);
	fMenuField->SetDivider(StringWidth(fMenuField->Label()) + 5);
	
	rect.top = rect.bottom + 10;
	rect.bottom = bounds.bottom
		- 20 // height of bottom controls
		- 20; // space for bottom controls
	fTabView = new BTabView(rect, "TabView", B_WIDTH_FROM_LABEL);
	BRect tabViewRect(fTabView->Bounds());
	tabViewRect.bottom -= fTabView->TabHeight();
	fAddons.AddRect(DUN_TAB_VIEW_RECT, tabViewRect);
	
	BRect tmpRect(rect);
	tmpRect.top += (tmpRect.Height() - 15) / 2;
	tmpRect.bottom = tmpRect.top + 15;
	fStringView = new BStringView(tmpRect, "NoInterfacesFound",
		kTextNoInterfacesFound);
	fStringView->SetAlignment(B_ALIGN_CENTER);
	fStringView->Hide();
	tmpRect.top = tmpRect.bottom + 10;
	tmpRect.bottom = tmpRect.top + 25;
	fCreateNewButton = new BButton(tmpRect, "CreateNewButton",
		kLabelCreateNewInterface, new BMessage(kMsgCreateNew));
	fCreateNewButton->ResizeToPreferred();
	tmpRect.left = (rect.Width() - fCreateNewButton->Bounds().Width()) / 2 + rect.left;
	fCreateNewButton->MoveTo(tmpRect.left, tmpRect.top);
	fCreateNewButton->Hide();
	
	rect.top = rect.bottom + 15;
	rect.bottom = rect.top + 15;
	rect.right = rect.left + 200;
	fStatusView = new BStringView(rect, "StatusView", kTextNotConnected, B_FOLLOW_BOTTOM);
	
	rect.InsetBy(0, -5);
	rect.left = rect.right + 5;
	rect.right = bounds.right - 5;
	fConnectButton = new BButton(rect, "ConnectButton", kLabelConnect,
		new BMessage(kMsgConnectButton), B_FOLLOW_BOTTOM);
	
	AddChild(fMenuField);
	AddChild(fTabView);
	AddChild(fStringView);
	AddChild(fCreateNewButton);
	AddChild(fStatusView);
	AddChild(fConnectButton);
	
	// initialize
	LoadInterfaces();
	LoadAddons();
	CreateTabs();
	fCurrentItem = NULL;
		// reset, otherwise SelectInterface will not load the settings
	SelectInterface(0);
	UpdateControls();
}


DialUpView::~DialUpView()
{
	SaveSettingsToFile();
	
	int32 tmp;
	wait_for_thread(fUpDownThread, &tmp);
	
	// free known add-on types (these should free their known add-on types, etc.)
	DialUpAddon *addon;
	for(int32 index = 0;
			fAddons.FindPointer(DUN_DELETE_ON_QUIT, index,
				reinterpret_cast<void**>(&addon)) == B_OK;
			index++)
		delete addon;
}


void
DialUpView::AttachedToWindow()
{
	fInterfaceMenu->SetTargetForItems(this);
	fCreateNewButton->SetTarget(this);
	fConnectButton->SetTarget(this);
	
	if(fListener.InitCheck() != B_OK) {
		(new BAlert(kErrorTitle, kErrorNoPPPStack, kLabelOK,
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go(NULL);
		fConnectButton->Hide();
	}
}


void
DialUpView::MessageReceived(BMessage *message)
{
	switch(message->what) {
		case PPP_REPORT_MESSAGE:
			HandleReportMessage(message);
		break;
		
		// -------------------------------------------------
		case kMsgCreateNew: {
			(new TextRequestDialog(kLabelCreateNewInterface, kTextChooseInterfaceName,
					kLabelInterfaceName))->Go(
				new BInvoker(new BMessage(kMsgFinishCreateNew), this));
		} break;
		
		case kMsgFinishCreateNew: {
			int32 which;
			message->FindInt32("which", &which);
			const char *name = message->FindString("text");
			if(which == 1 && name && strlen(name) > 0)
				AddInterface(name, true);
			
			if(fCurrentItem)
				fCurrentItem->SetMarked(true);
		} break;
		// -------------------------------------------------
		
		case kMsgDeleteCurrent: {
			if(!fCurrentItem)
				return;
			
			fInterfaceMenu->RemoveItem(fCurrentItem);
			BDirectory settings, profile;
			GetPPPDirectories(&settings, &profile);
			BEntry entry;
			settings.FindEntry(fCurrentItem->Label(), &entry);
			entry.Remove();
			profile.FindEntry(fCurrentItem->Label(), &entry);
			entry.Remove();
			delete fCurrentItem;
			fCurrentItem = NULL;
			
			BMenuItem *marked = fInterfaceMenu->FindMarked();
			if(marked)
				marked->SetMarked(false);
			
			UpdateControls();
			SelectInterface(0);
				// this stops watching the deleted interface
		} break;
		
		case kMsgSelectInterface: {
			int32 index;
			message->FindInt32("index", &index);
			SelectInterface(index);
		} break;
		
		case kMsgConnectButton: {
			if(!fCurrentItem || fUpDownThread != -1)
				return;
			
			fUpDownThread = spawn_thread(up_down_thread, "up_down_thread",
				B_NORMAL_PRIORITY, this);
			resume_thread(fUpDownThread);
		} break;
		
		default:
			BView::MessageReceived(message);
	}
}


bool
DialUpView::SelectInterfaceNamed(const char *name)
{
	BMenuItem *item = fInterfaceMenu->FindItem(name);
	
	int32 index = fInterfaceMenu->IndexOf(item);
	if(!item || index >= CountInterfaces())
		return false;
	
	SelectInterface(index);
	
	return true;
}


bool
DialUpView::NeedsRequest() const
{
	return fGeneralAddon ? fGeneralAddon->NeedsAuthenticationRequest() : false;
}


BView*
DialUpView::AuthenticationView() const
{
	return fGeneralAddon ? fGeneralAddon->AuthenticationView() : NULL;
}


BView*
DialUpView::StatusView() const
{
	return fStatusView;
}


BView*
DialUpView::ConnectButton() const
{
	return fConnectButton;
}


bool
DialUpView::LoadSettings(bool isNew)
{
	fSettings.MakeEmpty();
	fProfile.MakeEmpty();
	BMessage *settingsPointer = fCurrentItem ? &fSettings : NULL,
		*profilePointer = fCurrentItem ? &fProfile : NULL;
	
	if(fCurrentItem && !isNew) {
		BString name("pppidf/");
		name << fCurrentItem->Label();
		if(!ReadMessageDriverSettings(name.String(), &fSettings))
			return false;
		name = "pppidf/profile/";
		name << fCurrentItem->Label();
		if(!ReadMessageDriverSettings(name.String(), &fProfile))
			profilePointer = settingsPointer;
	}
	
	DialUpAddon *addon;
	for(int32 index = 0; fAddons.FindPointer(DUN_TAB_ADDON_TYPE, index,
			reinterpret_cast<void**>(&addon)) == B_OK; index++) {
		if(!addon)
			continue;
		
		if(!addon->LoadSettings(settingsPointer, profilePointer, isNew))
			return false;
	}
	
	// TODO: check if settings are valid
	
	return true;
}


void
DialUpView::IsModified(bool *settings, bool *profile)
{
	*settings = *profile = false;
	bool addonSettingsChanged, addonProfileChanged;
		// for current addon
	
	DialUpAddon *addon;
	for(int32 index = 0; fAddons.FindPointer(DUN_TAB_ADDON_TYPE, index,
			reinterpret_cast<void**>(&addon)) == B_OK; index++) {
		if(!addon)
			continue;
		
		addon->IsModified(&addonSettingsChanged, &addonProfileChanged);
		if(addonSettingsChanged)
			*settings = true;
		if(addonProfileChanged)
			*profile = true;
	}
}


bool
DialUpView::SaveSettings(BMessage *settings, BMessage *profile, bool saveTemporary)
{
	if(!fCurrentItem || !settings || !profile)
		return false;
	
	DialUpAddon *addon;
	TemplateList<DialUpAddon*> addons;
	for(int32 index = 0;
			fAddons.FindPointer(DUN_TAB_ADDON_TYPE, index,
				reinterpret_cast<void**>(&addon)) == B_OK; index++) {
		if(!addon)
			continue;
		
		int32 insertIndex = 0;
		for(; insertIndex < addons.CountItems(); insertIndex++)
			if(addons.ItemAt(insertIndex)->Priority() <= addon->Priority())
				break;
		
		addons.AddItem(addon, insertIndex);
	}
	
	settings->AddInt32("Interface", static_cast<int32>(fWatching));
	if(fCurrentItem)
		settings->AddString("InterfaceName", fCurrentItem->Label());
	
	for(int32 index = 0; index < addons.CountItems(); index++)
		if(!addons.ItemAt(index)->SaveSettings(settings, profile, saveTemporary))
			return false;
	
	return true;
}


bool
DialUpView::SaveSettingsToFile()
{
	bool settingsChanged, profileChanged;
	IsModified(&settingsChanged, &profileChanged);
	if(!settingsChanged && !profileChanged)
		return true;
	
	BMessage settings, profile;
	if(!SaveSettings(&settings, &profile, false))
		return false;
	
	BDirectory settingsDirectory;
	BDirectory profileDirectory;
	GetPPPDirectories(&settingsDirectory, &profileDirectory);
	if(settingsDirectory.InitCheck() != B_OK || profileDirectory.InitCheck() != B_OK)
		return false;
	
	BFile file;
	if(settingsChanged) {
		settingsDirectory.CreateFile(fCurrentItem->Label(), &file);
		WriteMessageDriverSettings(file, settings);
	}
	
	if(profileChanged) {
		profileDirectory.CreateFile(fCurrentItem->Label(), &file);
		WriteMessageDriverSettings(file, profile);
	}
	
	return true;
}


void
DialUpView::UpDownThread()
{
	SaveSettingsToFile();
	BMessage settings, profile;
	SaveSettings(&settings, &profile, true);
		// save temporary profile
	driver_settings *temporaryProfile = MessageToDriverSettings(profile);
	
	PPPInterface interface;
	if(fWatching == PPP_UNDEFINED_INTERFACE_ID) {
		interface = fListener.Manager().InterfaceWithName(fCurrentItem->Label());
		if(interface.InitCheck() != B_OK)
			interface = fListener.Manager().CreateInterfaceWithName(
				fCurrentItem->Label(), temporaryProfile);
	} else {
		interface = fWatching;
		interface.SetProfile(temporaryProfile);
	}
	
	free_driver_settings(temporaryProfile);
	
	if(interface.InitCheck() != B_OK) {
		Window()->Lock();
		fStatusView->SetText(kTextCreationError);
		Window()->Unlock();
		return;
	}
	
	ppp_interface_info_t info;
	interface.GetInterfaceInfo(&info);
	if(info.info.phase == PPP_DOWN_PHASE)
		interface.Up();
	else
		interface.Down();
	
	fUpDownThread = -1;
}


void
DialUpView::GetPPPDirectories(BDirectory *settingsDirectory,
	BDirectory *profileDirectory) const
{
	if(settingsDirectory) {
		BDirectory settings(PPP_INTERFACE_SETTINGS_PATH);
		if(settings.InitCheck() != B_OK) {
			create_directory(PPP_INTERFACE_SETTINGS_PATH, 0750);
			settings.SetTo(PPP_INTERFACE_SETTINGS_PATH);
		}
		
		*settingsDirectory = settings;
	}
	
	if(profileDirectory) {
		BDirectory profile(PPP_INTERFACE_SETTINGS_PATH "/profile");
		if(profile.InitCheck() != B_OK) {
			create_directory(PPP_INTERFACE_SETTINGS_PATH "/profile", 0750);
			profile.SetTo(PPP_INTERFACE_SETTINGS_PATH "/profile");
		}
		
		*profileDirectory = profile;
	}
}


void
DialUpView::HandleReportMessage(BMessage *message)
{
	thread_id sender;
	message->FindInt32("sender", &sender);
	
	send_data(sender, B_OK, NULL, 0);
	
	if(!fCurrentItem)
		return;
	
	ppp_interface_id id;
	if(message->FindInt32("interface", reinterpret_cast<int32*>(&id)) != B_OK
			|| (fWatching != PPP_UNDEFINED_INTERFACE_ID && id != fWatching))
		return;
	
	int32 type, code;
	message->FindInt32("type", &type);
	message->FindInt32("code", &code);
	
	if(type == PPP_MANAGER_REPORT && code == PPP_REPORT_INTERFACE_CREATED) {
		PPPInterface interface(id);
		if(interface.InitCheck() != B_OK)
			return;
		
		ppp_interface_info_t info;
		interface.GetInterfaceInfo(&info);
		if(strcasecmp(info.info.name, fCurrentItem->Label()))
			return;
		
		WatchInterface(id);
	} else if(type == PPP_CONNECTION_REPORT) {
		if(fWatching == PPP_UNDEFINED_INTERFACE_ID)
			return;
		
		UpdateStatus(code);
	} else if(type == PPP_DESTRUCTION_REPORT) {
		if(fWatching == PPP_UNDEFINED_INTERFACE_ID)
			return;
		
		WatchInterface(fListener.Manager().InterfaceWithName(fCurrentItem->Label()));
	}
}


void
DialUpView::CreateTabs()
{
	// create tabs for all registered and valid tab add-ons
	DialUpAddon *addon;
	BView *target;
	float width, height;
	TemplateList<DialUpAddon*> addons;
	
	for(int32 index = 0;
			fAddons.FindPointer(DUN_TAB_ADDON_TYPE, index,
				reinterpret_cast<void**>(&addon)) == B_OK;
			index++) {
		if(!addon || addon->Position() < 0)
			continue;
		
		int32 insertIndex = 0;
		for(; insertIndex < addons.CountItems(); insertIndex++)
			if(addons.ItemAt(insertIndex)->Position() > addon->Position())
				break;
		
		addons.AddItem(addon, insertIndex);
	}
	
	for(int32 index = 0; index < addons.CountItems(); index++) {
		addon = addons.ItemAt(index);
		
		if(!addon->GetPreferredSize(&width, &height))
			continue;
		
		target = addon->CreateView(BPoint(0, 0));
		if(!target)
			continue;
		
		fTabView->AddTab(target, NULL);
	}
}


void
DialUpView::UpdateStatus(int32 code)
{
	switch(code) {
		case PPP_REPORT_UP_ABORTED:
		case PPP_REPORT_DEVICE_UP_FAILED:
		case PPP_REPORT_LOCAL_AUTHENTICATION_FAILED:
		case PPP_REPORT_PEER_AUTHENTICATION_FAILED:
		case PPP_REPORT_DOWN_SUCCESSFUL:
		case PPP_REPORT_CONNECTION_LOST: {
			fConnectButton->SetLabel(kLabelConnect);
		} break;
		
		default:
			fConnectButton->SetLabel(kLabelDisconnect);
	}
	
	// maybe the status string must not be changed (codes that set fKeepLabel to false
	// should still be handled)
	if(fKeepLabel && code != PPP_REPORT_GOING_UP && code != PPP_REPORT_UP_SUCCESSFUL)
		return;
	
	if(fListener.InitCheck() != B_OK) {
		fStatusView->SetText(kErrorNoPPPStack);
		return;
	}
	
	// only errors should set fKeepLabel to true
	switch(code) {
		case PPP_REPORT_GOING_UP:
			fKeepLabel = false;
			fStatusView->SetText(kTextConnecting);
		break;
		
		case PPP_REPORT_UP_SUCCESSFUL:
			fKeepLabel = false;
			fStatusView->SetText(kTextConnectionEstablished);
		break;
		
		case PPP_REPORT_UP_ABORTED:
		case PPP_REPORT_DOWN_SUCCESSFUL:
			fStatusView->SetText(kTextNotConnected);
		break;
		
		case PPP_REPORT_DEVICE_UP_FAILED:
			fKeepLabel = true;
			fStatusView->SetText(kTextDeviceUpFailed);
		break;
		
		case PPP_REPORT_LOCAL_AUTHENTICATION_REQUESTED:
		case PPP_REPORT_PEER_AUTHENTICATION_REQUESTED:
			fStatusView->SetText(kTextAuthenticating);
		break;
		
		case PPP_REPORT_LOCAL_AUTHENTICATION_FAILED:
		case PPP_REPORT_PEER_AUTHENTICATION_FAILED:
			fKeepLabel = true;
			fStatusView->SetText(kTextAuthenticationFailed);
		break;
		
		case PPP_REPORT_CONNECTION_LOST:
			fKeepLabel = true;
			fStatusView->SetText(kTextConnectionLost);
		break;
	}
}


void
DialUpView::WatchInterface(ppp_interface_id ID)
{
	// This method can be used to update the interface's connection status.
	
	if(fWatching != ID) {
		fListener.StopWatchingInterfaces();
		
		if(ID == PPP_UNDEFINED_INTERFACE_ID || fListener.WatchInterface(ID))
			fWatching = ID;
	}
	
	// update status
	PPPInterface interface(fWatching);
	if(interface.InitCheck() != B_OK) {
		UpdateStatus(PPP_REPORT_DOWN_SUCCESSFUL);
		return;
	}
	
	ppp_interface_info_t info;
	interface.GetInterfaceInfo(&info);
	
	// transform phase into status
	switch(info.info.phase) {
		case PPP_DOWN_PHASE:
			UpdateStatus(PPP_REPORT_DOWN_SUCCESSFUL);
		break;
		
		case PPP_TERMINATION_PHASE:
		break;
		
		case PPP_ESTABLISHED_PHASE:
			UpdateStatus(PPP_REPORT_UP_SUCCESSFUL);
		break;
		
		default:
			UpdateStatus(PPP_REPORT_GOING_UP);
	}
}


void
DialUpView::LoadInterfaces()
{
	fInterfaceMenu->AddSeparatorItem();
	fInterfaceMenu->AddItem(new BMenuItem(kLabelCreateNewInterface,
		new BMessage(kMsgCreateNew)));
	fDeleterItem = new BMenuItem(kLabelDeleteCurrent,
		new BMessage(kMsgDeleteCurrent));
	fInterfaceMenu->AddItem(fDeleterItem);
	
	BDirectory settingsDirectory;
	BEntry entry;
	BPath path;
	GetPPPDirectories(&settingsDirectory, NULL);
	while(settingsDirectory.GetNextEntry(&entry) == B_OK) {
		if(entry.IsFile()) {
			entry.GetPath(&path);
			AddInterface(path.Leaf(), true);
		}
	}
}


void
DialUpView::LoadAddons()
{
	// Load integrated add-ons:
	// "Connection Options" tab
	ConnectionOptionsAddon *connectionOptionsAddon =
		new ConnectionOptionsAddon(&fAddons);
	fAddons.AddPointer(DUN_TAB_ADDON_TYPE, connectionOptionsAddon);
	fAddons.AddPointer(DUN_DELETE_ON_QUIT, connectionOptionsAddon);
	// "General" tab
	GeneralAddon *fGeneralAddon = new GeneralAddon(&fAddons);
	fAddons.AddPointer(DUN_TAB_ADDON_TYPE, fGeneralAddon);
	fAddons.AddPointer(DUN_DELETE_ON_QUIT, fGeneralAddon);
/*
	// "IPCP" protocol
	IPCPAddon *ipcpAddon = new IPCPAddon(&fAddons);
	fAddons.AddPointer(DUN_TAB_ADDON_TYPE, ipcpAddon);
	fAddons.AddPointer(DUN_DELETE_ON_QUIT, ipcpAddon);
	// "PPPoE" device
	PPPoEAddon *pppoeAddon = new PPPoEAddon(&fAddons);
	fAddons.AddPointer(DUN_DEVICE_ADDON_TYPE, pppoeAddon);
	fAddons.AddPointer(DUN_DELETE_ON_QUIT, pppoeAddon);
*/	
	// "PAP" authenticator
	BMessage addon;
#ifdef LANG_GERMAN
	addon.AddString("FriendlyName", "Unverschlüsselt");
#else
	addon.AddString("FriendlyName", "Plain-text Authentication");
#endif
	addon.AddString("TechnicalName", "PAP");
	addon.AddString("KernelModuleName", "pap");
	fAddons.AddMessage(DUN_AUTHENTICATOR_ADDON_TYPE, &addon);
	// addon.MakeEmpty(); // for next authenticator
	
	// TODO:
	// load all add-ons from the add-ons folder
}


void
DialUpView::AddInterface(const char *name, bool isNew)
{
	if(fInterfaceMenu->FindItem(name)) {
		(new BAlert(kErrorTitle, kErrorInterfaceExists, kLabelOK,
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go(NULL);
		return;
	}
	
	BMenuItem *item = new BMenuItem(name, new BMessage(kMsgSelectInterface));
	item->SetTarget(this);
	int32 index = FindNextMenuInsertionIndex(fInterfaceMenu, name);
	if(index > CountInterfaces())
		index = CountInterfaces();
	fInterfaceMenu->AddItem(item, index);
	UpdateControls();
	
	item->SetMarked(true);
	SelectInterface(index, isNew);
}


void
DialUpView::SelectInterface(int32 index, bool isNew)
{
	BMenuItem *item = fInterfaceMenu->FindMarked();
	if(fCurrentItem && item == fCurrentItem)
		return;
	
	if(fCurrentItem && !SaveSettingsToFile())
		(new BAlert(kErrorTitle, kErrorSavingFailed, kLabelOK,
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go(NULL);
	
	if(index >= CountInterfaces() || index < 0) {
		if(CountInterfaces() > 0)
			SelectInterface(0);
		else {
			fCurrentItem = NULL;
			WatchInterface(PPP_UNDEFINED_INTERFACE_ID);
		}
	} else {
		fCurrentItem = fInterfaceMenu->ItemAt(index);
		if(!fCurrentItem) {
			SelectInterface(0);
			return;
		}
		
		fCurrentItem->SetMarked(true);
		fDeleterItem->SetEnabled(true);
		WatchInterface(fListener.Manager().InterfaceWithName(fCurrentItem->Label()));
	}
	
	if(!fCurrentItem)
		LoadSettings(false);
			// tell modules to unload all settings
	else if(!isNew && !LoadSettings(false)) {
		(new BAlert(kErrorTitle, kErrorLoadingFailed, kLabelOK,
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go(NULL);
		LoadSettings(true);
	} else if(isNew && !LoadSettings(true))
		(new BAlert(kErrorTitle, kErrorLoadingFailed, kLabelOK,
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go(NULL);
}


int32
DialUpView::CountInterfaces() const
{
	return fInterfaceMenu->CountItems() - 3;
}


void
DialUpView::UpdateControls()
{
	if(fTabView->IsHidden() && CountInterfaces() > 0) {
		fInterfaceMenu->SetLabelFromMarked(true);
		fStringView->Hide();
		fCreateNewButton->Hide();
		fTabView->Show();
		fConnectButton->SetEnabled(true);
	} else if(!fTabView->IsHidden() && CountInterfaces() == 0) {
		fDeleterItem->SetEnabled(false);
		fInterfaceMenu->SetRadioMode(false);
		fInterfaceMenu->Superitem()->SetLabel(kLabelCreateNew);
		fTabView->Hide();
		fStringView->Show();
		fCreateNewButton->Show();
		fConnectButton->SetEnabled(false);
	}
}
