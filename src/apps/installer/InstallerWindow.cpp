/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005-2008, Jérôme DUVAL
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "InstallerWindow.h"

#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <GridLayoutBuilder.h>
#include <GroupLayoutBuilder.h>
#include <LayoutUtils.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <StatusBar.h>
#include <String.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>

#include "tracker_private.h"

#include "DialogPane.h"
#include "PackageViews.h"
#include "PartitionMenuItem.h"
#include "WorkerThread.h"


#undef TR_CONTEXT
#define TR_CONTEXT "InstallerWindow"

#define DRIVESETUP_SIG "application/x-vnd.Haiku-DriveSetup"

const uint32 BEGIN_MESSAGE = 'iBGN';
const uint32 SHOW_BOTTOM_MESSAGE = 'iSBT';
const uint32 SETUP_MESSAGE = 'iSEP';
const uint32 START_SCAN = 'iSSC';
const uint32 PACKAGE_CHECKBOX = 'iPCB';
const uint32 ENCOURAGE_DRIVESETUP = 'iENC';

class LogoView : public BView {
public:
								LogoView(const BRect& frame);
								LogoView();
	virtual						~LogoView();

	virtual	void				Draw(BRect update);

	virtual	void				GetPreferredSize(float* _width, float* _height);

private:
			void				_Init();

			BBitmap*			fLogo;
};


LogoView::LogoView(const BRect& frame)
	:
	BView(frame, "logoview", B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init();
}


LogoView::LogoView()
	:
	BView("logoview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init();
}


LogoView::~LogoView(void)
{
	delete fLogo;
}


void
LogoView::Draw(BRect update)
{
	if (fLogo == NULL)
		return;

	BRect bounds(Bounds());
	BPoint placement;
	placement.x = (bounds.left + bounds.right - fLogo->Bounds().Width()) / 2;
	placement.y = (bounds.top + bounds.bottom - fLogo->Bounds().Height()) / 2;

	DrawBitmap(fLogo, placement);
}


void
LogoView::GetPreferredSize(float* _width, float* _height)
{
	float width = 0.0;
	float height = 0.0;
	if (fLogo) {
		width = fLogo->Bounds().Width();
		height = fLogo->Bounds().Height();
	}
	if (_width)
		*_width = width;
	if (_height)
		*_height = height;
}


void
LogoView::_Init()
{
	fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "haikulogo.png");
}


// #pragma mark -


static BLayoutItem*
layout_item_for(BView* view)
{
	BLayout* layout = view->Parent()->GetLayout();
	int32 index = layout->IndexOfView(view);
	return layout->ItemAt(index);
}


InstallerWindow::InstallerWindow()
	: BWindow(BRect(-2000, -2000, -1800, -1800), TR("Installer"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fEncouragedToSetupPartitions(false),
	fDriveSetupLaunched(false),
	fInstallStatus(kReadyForInstall),
	fWorkerThread(new WorkerThread(this)),
	fCopyEngineCancelSemaphore(-1)
{
	LogoView* logoView = new LogoView();

	fStatusView = new BTextView("statusView", be_plain_font, NULL, B_WILL_DRAW);
	fStatusView->SetInsets(10, 10, 10, 10);
	fStatusView->MakeEditable(false);
	fStatusView->MakeSelectable(false);

	BSize logoSize = logoView->MinSize();
	logoView->SetExplicitMaxSize(logoSize);
	fStatusView->SetExplicitMinSize(BSize(logoSize.width * 0.66, B_SIZE_UNSET));

	fDestMenu = new BPopUpMenu(TR("scanning" B_UTF8_ELLIPSIS), true, false);
	fSrcMenu = new BPopUpMenu(TR("scanning" B_UTF8_ELLIPSIS), true, false);

	fSrcMenuField = new BMenuField("srcMenuField", TR("Install from:"),
		fSrcMenu, NULL);
	fSrcMenuField->SetAlignment(B_ALIGN_RIGHT);

	fDestMenuField = new BMenuField("destMenuField", TR("Onto:"), fDestMenu,
		NULL);
	fDestMenuField->SetAlignment(B_ALIGN_RIGHT);

	fPackagesSwitch = new PaneSwitch("options_button");
	fPackagesSwitch->SetLabels(TR("Hide optional packages"),
		TR("Show optional packages"));
	fPackagesSwitch->SetMessage(new BMessage(SHOW_BOTTOM_MESSAGE));
	fPackagesSwitch->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		B_SIZE_UNSET));
	fPackagesSwitch->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_TOP));

	fPackagesView = new PackagesView("packages_view");
	BScrollView* packagesScrollView = new BScrollView("packagesScroll",
		fPackagesView, B_WILL_DRAW, false, true);

	const char* requiredDiskSpaceString
		= TR("Additional disk space required: 0.0 KiB");
	fSizeView = new BStringView("size_view", requiredDiskSpaceString);
	fSizeView->SetAlignment(B_ALIGN_RIGHT);
	fSizeView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fSizeView->SetExplicitAlignment(
		BAlignment(B_ALIGN_RIGHT, B_ALIGN_MIDDLE));

	fProgressBar = new BStatusBar("progress", TR("Install progress:  "));
	fProgressBar->SetMaxValue(100.0);

	fBeginButton = new BButton("begin_button", TR("Begin"),
		new BMessage(BEGIN_MESSAGE));
	fBeginButton->MakeDefault(true);
	fBeginButton->SetEnabled(false);

	fSetupButton = new BButton("setup_button",
		TR("Set up partitions" B_UTF8_ELLIPSIS), new BMessage(SETUP_MESSAGE));

	fMakeBootableButton = new BButton("makebootable_button",
		TR("Write boot sector"), new BMessage(MSG_WRITE_BOOT_SECTOR));
	fMakeBootableButton->SetEnabled(false);

	SetLayout(new BGroupLayout(B_HORIZONTAL));
	AddChild(BGroupLayoutBuilder(B_VERTICAL)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL)
			.Add(logoView)
			.Add(fStatusView)
		)
		.Add(new BSeparatorView(B_HORIZONTAL, B_PLAIN_BORDER))
		.Add(BGroupLayoutBuilder(B_VERTICAL, 10)
			.Add(BGridLayoutBuilder(0, 10)
				.Add(fSrcMenuField->CreateLabelLayoutItem(), 0, 0)
				.Add(fSrcMenuField->CreateMenuBarLayoutItem(), 1, 0)
				.Add(fDestMenuField->CreateLabelLayoutItem(), 0, 1)
				.Add(fDestMenuField->CreateMenuBarLayoutItem(), 1, 1)

				.Add(BSpaceLayoutItem::CreateVerticalStrut(5), 0, 2, 2)

				.Add(fPackagesSwitch, 0, 3, 2)
				.Add(packagesScrollView, 0, 4, 2)
				.Add(fProgressBar, 0, 5, 2)
				.Add(fSizeView, 0, 6, 2)
			)

			.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10)
				.Add(fSetupButton)
				.Add(fMakeBootableButton)
				.AddGlue()
				.Add(fBeginButton)
			)
			.SetInsets(10, 10, 10, 10)
		)
	);

	// Make the optional packages and progress bar invisible on start
	fPackagesLayoutItem = layout_item_for(packagesScrollView);
	fPkgSwitchLayoutItem = layout_item_for(fPackagesSwitch);
	fSizeViewLayoutItem = layout_item_for(fSizeView);
	fProgressLayoutItem = layout_item_for(fProgressBar);

	fPackagesLayoutItem->SetVisible(false);
	fSizeViewLayoutItem->SetVisible(false);
	fProgressLayoutItem->SetVisible(false);

	// Setup tool tips for the non-obvious features
	fSetupButton->SetToolTip(
		TR("Launch the DriveSetup utility to partition\n"
		"available hard drives and other media.\n"
		"Partitions can be initialized with the\n"
		"Be File System needed for a Haiku boot\n"
		"partition."));
	fMakeBootableButton->SetToolTip(
		TR("Writes the Haiku boot code to the partition start\n"
		"sector. This step is automatically performed by\n"
		"the installation, but you can manually make a\n"
		"partition bootable in case you do not need to\n"
		"perform an installation."));

	// finish creating window
	if (!be_roster->IsRunning(kDeskbarSignature))
		SetFlags(Flags() | B_NOT_MINIMIZABLE);

	CenterOnScreen();
	Show();

	fDriveSetupLaunched = be_roster->IsRunning(DRIVESETUP_SIG);
 
	if (Lock()) {
		fSetupButton->SetEnabled(!fDriveSetupLaunched);
		Unlock();
	}

	be_roster->StartWatching(this);

	PostMessage(START_SCAN);
}


InstallerWindow::~InstallerWindow()
{
	_SetCopyEngineCancelSemaphore(-1);
	be_roster->StopWatching(this);
}


void
InstallerWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_RESET:
		{
			_SetCopyEngineCancelSemaphore(-1);

			status_t error;
			if (msg->FindInt32("error", &error) == B_OK) {
				char errorMessage[2048];
				snprintf(errorMessage, sizeof(errorMessage), TR("An error was "
					"encountered and the installation was not completed:\n\n"
					"Error:  %s"), strerror(error));
				(new BAlert("error", errorMessage, TR("OK")))->Go();
			}

			_DisableInterface(false);

			fProgressLayoutItem->SetVisible(false);
			fPkgSwitchLayoutItem->SetVisible(true);
			_ShowOptionalPackages();
			_UpdateControls();
			break;
		}
		case START_SCAN:
			_ScanPartitions();
			break;
		case BEGIN_MESSAGE:
			switch (fInstallStatus) {
				case kReadyForInstall:
				{
					_SetCopyEngineCancelSemaphore(create_sem(1,
						"copy engine cancel"));

					BList* list = new BList();
					int32 size = 0;
					fPackagesView->GetPackagesToInstall(list, &size);
					fWorkerThread->SetLock(fCopyEngineCancelSemaphore);
					fWorkerThread->SetPackagesList(list);
					fWorkerThread->SetSpaceRequired(size);
					fInstallStatus = kInstalling;
					fWorkerThread->StartInstall();
					fBeginButton->SetLabel(TR("Stop"));
					_DisableInterface(true);

					fProgressBar->SetTo(0.0, NULL, NULL);

					fPkgSwitchLayoutItem->SetVisible(false);
					fPackagesLayoutItem->SetVisible(false);
					fSizeViewLayoutItem->SetVisible(false);
					fProgressLayoutItem->SetVisible(true);
					break;
				}
				case kInstalling:
				{
					_QuitCopyEngine(true);
					break;
				}
				case kFinished:
					PostMessage(B_QUIT_REQUESTED);
					break;
				case kCancelled:
					break;
			}
			break;
		case SHOW_BOTTOM_MESSAGE:
			_ShowOptionalPackages();
			break;
		case SOURCE_PARTITION:
			_PublishPackages();
			_UpdateControls();
			break;
		case TARGET_PARTITION:
			_UpdateControls();
			break;
		case SETUP_MESSAGE:
			_LaunchDriveSetup();
			break;
		case PACKAGE_CHECKBOX:
		{
			char buffer[15];
			fPackagesView->GetTotalSizeAsString(buffer, sizeof(buffer));
			char string[256];
			snprintf(string, sizeof(string),
				TR("Additional disk space required: %s"), buffer);
			fSizeView->SetText(string);
			break;
		}
		case ENCOURAGE_DRIVESETUP:
		{
			(new BAlert("use drive setup", TR("No partitions have been found "
				"that are suitable for installation. Please set up partitions "
				"and initialize at least one partition with the Be File "
				"System."), TR("OK")))->Go();
		}
		case MSG_STATUS_MESSAGE:
		{
// TODO: Was this supposed to prevent status messages still arriving
// after the copy engine was shut down?
//			if (fInstallStatus != kInstalling)
//				break;
			float progress;
			if (msg->FindFloat("progress", &progress) == B_OK) {
				const char* currentItem;
				if (msg->FindString("item", &currentItem) != B_OK) {
					currentItem = TR_CMT("???",
						"Unknown currently copied item");
				}
				BString trailingLabel;
				int32 currentCount;
				int32 maximumCount;
				if (msg->FindInt32("current", &currentCount) == B_OK
					&& msg->FindInt32("maximum", &maximumCount) == B_OK) {
					char buffer[64];
					snprintf(buffer, sizeof(buffer), TR_CMT("%1ld of %2ld",
						"number of files copied"), currentCount, maximumCount);
					trailingLabel << buffer;
				} else {
					trailingLabel << TR_CMT("?? of ??", "Unknown progress");
				}
				fProgressBar->SetTo(progress, currentItem,
					trailingLabel.String());
			} else {
				const char *status;
				if (msg->FindString("status", &status) == B_OK) {
					fLastStatus = fStatusView->Text();
					_SetStatusMessage(status);
				} else
					_SetStatusMessage(fLastStatus.String());
			}
			break;
		}
		case MSG_INSTALL_FINISHED:
		{
			
			_SetCopyEngineCancelSemaphore(-1);

			fBeginButton->SetLabel(TR("Quit"));

			PartitionMenuItem* dstItem
				= (PartitionMenuItem*)fDestMenu->FindMarked();

			char status[1024];
			if (be_roster->IsRunning(kDeskbarSignature)) {
				snprintf(status, sizeof(status), TR("Installation completed. "
					"Boot sector has been written to '%s'. Press Quit to "
					"leave the Installer or choose a new target volume to "
					"perform another installation."),
					dstItem ? dstItem->Name() : TR_CMT("???",
						"Unknown partition name"));
			} else {
				snprintf(status, sizeof(status), TR("Installation completed. "
					"Boot sector has been written to '%s'. Press Quit to "
					"restart the computer or choose a new target volume "
					"to perform another installation."),
					dstItem ? dstItem->Name() : TR_CMT("???",
						"Unknown partition name"));
			}

			_SetStatusMessage(status);
			fInstallStatus = kFinished;
			_DisableInterface(false);
			fProgressLayoutItem->SetVisible(false);
			fPkgSwitchLayoutItem->SetVisible(true);
			_ShowOptionalPackages();
			break;
		}
		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char *signature;
			if (msg->FindString("be:signature", &signature) == B_OK
				&& strcasecmp(signature, DRIVESETUP_SIG) == 0) {
				fDriveSetupLaunched = msg->what == B_SOME_APP_LAUNCHED;
				fBeginButton->SetEnabled(!fDriveSetupLaunched);
				_DisableInterface(fDriveSetupLaunched);
				if (fDriveSetupLaunched)
					_SetStatusMessage(TR("Running DriveSetup" B_UTF8_ELLIPSIS
						"\n\nClose DriveSetup to continue with the "
						"installation."));
				else
					_ScanPartitions();
			}
			break;
		}
		case MSG_WRITE_BOOT_SECTOR:
			fWorkerThread->WriteBootSector(fDestMenu);
			break;

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


bool
InstallerWindow::QuitRequested()
{
	if (fDriveSetupLaunched) {
		(new BAlert("driveSetup",
			TR("Please close the DriveSetup window before closing the "
			"Installer window."), TR("OK")))->Go();
		return false;
	}

	if (fInstallStatus != kFinished && (Flags() & B_NOT_MINIMIZABLE) != 0) {
		// This means Deskbar is not running, i.e. Installer is the only
		// thing on the screen and we will reboot the machine once it quits.
		if ((new BAlert("reallyQuit",
			TR("Are you sure you want to abort the installation and restart "
				"the system?"),
			TR("Cancel"), TR("Restart system")))->Go() == 0) {
			return false;
		}
	}

	_QuitCopyEngine(false);
	fWorkerThread->PostMessage(B_QUIT_REQUESTED);
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


// #pragma mark -


void
InstallerWindow::_ShowOptionalPackages()
{
	if (fPackagesLayoutItem && fSizeViewLayoutItem) {
		fPackagesLayoutItem->SetVisible(fPackagesSwitch->Value());
		fSizeViewLayoutItem->SetVisible(fPackagesSwitch->Value());
	}
}


void
InstallerWindow::_LaunchDriveSetup()
{
	if (be_roster->Launch(DRIVESETUP_SIG) != B_OK) {
		// Try really hard to launch it. It's very likely that this fails,
		// when we run from the CD and there is only an incomplete mime
		// database for example...
		BPath path;
		if (find_directory(B_SYSTEM_APPS_DIRECTORY, &path) != B_OK
			|| path.Append("DriveSetup") != B_OK) {
			path.SetTo("/boot/system/apps/DriveSetup");
		}
		BEntry entry(path.Path());
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || be_roster->Launch(&ref) != B_OK) {
			BAlert* alert = new BAlert("error", TR("DriveSetup, the "
				"application to configure disk partitions, could not be "
				"launched."), TR("OK"));
			alert->Go();
		}
	}
}


void
InstallerWindow::_DisableInterface(bool disable)
{
	fSetupButton->SetEnabled(!disable);
	fMakeBootableButton->SetEnabled(!disable);
	fSrcMenuField->SetEnabled(!disable);
	fDestMenuField->SetEnabled(!disable);
}


void
InstallerWindow::_ScanPartitions()
{
	_SetStatusMessage(TR("Scanning for disks" B_UTF8_ELLIPSIS));

	BMenuItem *item;
	while ((item = fSrcMenu->RemoveItem((int32)0)))
		delete item;
	while ((item = fDestMenu->RemoveItem((int32)0)))
		delete item;

	fWorkerThread->ScanDisksPartitions(fSrcMenu, fDestMenu);

	if (fSrcMenu->ItemAt(0)) {
		_PublishPackages();
	}
	_UpdateControls();
}


void
InstallerWindow::_UpdateControls()
{
	PartitionMenuItem* srcItem = (PartitionMenuItem*)fSrcMenu->FindMarked();
	BString label;
	if (srcItem) {
		label = srcItem->MenuLabel();
	} else {
		if (fSrcMenu->CountItems() == 0)
			label = TR_CMT("<none>", "No partition available");
		else
			label = ((PartitionMenuItem*)fSrcMenu->ItemAt(0))->MenuLabel();
	}
	fSrcMenuField->MenuItem()->SetLabel(label.String());

	// Disable any unsuitable target items, check if at least one partition
	// is suitable.
	bool foundOneSuitableTarget = false;
	for (int32 i = fDestMenu->CountItems() - 1; i >= 0; i--) {
		PartitionMenuItem* dstItem
			= (PartitionMenuItem*)fDestMenu->ItemAt(i);
		if (srcItem != NULL && dstItem->ID() == srcItem->ID()) {
			// Prevent the user from having picked the same partition as source
			// and destination.
			dstItem->SetEnabled(false);
			dstItem->SetMarked(false);
		} else
			dstItem->SetEnabled(dstItem->IsValidTarget());

		if (dstItem->IsEnabled())
			foundOneSuitableTarget = true;
	}

	PartitionMenuItem* dstItem = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (dstItem) {
		label = dstItem->MenuLabel();
	} else {
		if (fDestMenu->CountItems() == 0)
			label = TR_CMT("<none>", "No partition available");
		else
			label = TR("Please choose target");
	}
	fDestMenuField->MenuItem()->SetLabel(label.String());

	if (srcItem && dstItem) {
		char message[255];
		sprintf(message, TR("Press the Begin button to install from '%1s' "
			"onto '%2s'."), srcItem->Name(), dstItem->Name());
		_SetStatusMessage(message);
	} else if (srcItem) {
		_SetStatusMessage(TR("Choose the disk you want to install onto from "
			"the pop-up menu. Then click \"Begin\"."));
	} else if (dstItem) {
		_SetStatusMessage(TR("Choose the source disk from the "
			"pop-up menu. Then click \"Begin\"."));
	} else {
		_SetStatusMessage(TR("Choose the source and destination disk from the "
			"pop-up menus. Then click \"Begin\"."));
	}

	fInstallStatus = kReadyForInstall;
	fBeginButton->SetLabel(TR("Begin"));
	fBeginButton->SetEnabled(srcItem && dstItem);

	// adjust "Write Boot Sector" button
	if (dstItem) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), TR("Write boot sector to '%s'"),
			dstItem->Name());
		label = buffer;
	} else
		label = TR("Write boot sector");
	fMakeBootableButton->SetEnabled(dstItem);
	fMakeBootableButton->SetLabel(label.String());

	if (!fEncouragedToSetupPartitions && !foundOneSuitableTarget) {
		// Focus the users attention on the DriveSetup button
		fEncouragedToSetupPartitions = true;
		PostMessage(ENCOURAGE_DRIVESETUP);
	}
}


void
InstallerWindow::_PublishPackages()
{
	fPackagesView->Clean();
	PartitionMenuItem *item = (PartitionMenuItem *)fSrcMenu->FindMarked();
	if (!item)
		return;

	BPath directory;
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition *partition;
	if (roster.GetPartitionWithID(item->ID(), &device, &partition) == B_OK) {
		if (partition->GetMountPoint(&directory) != B_OK)
			return;
	} else if (roster.GetDeviceWithID(item->ID(), &device) == B_OK) {
		if (device.GetMountPoint(&directory) != B_OK)
			return;
	} else
		return; // shouldn't happen

	directory.Append(PACKAGES_DIRECTORY);
	BDirectory dir(directory.Path());
	if (dir.InitCheck() != B_OK)
		return;

	BEntry packageEntry;
	BList packages;
	while (dir.GetNextEntry(&packageEntry) == B_OK) {
		Package* package = Package::PackageFromEntry(packageEntry);
		if (package != NULL)
			packages.AddItem(package);
	}
	packages.SortItems(_ComparePackages);

	fPackagesView->AddPackages(packages, new BMessage(PACKAGE_CHECKBOX));
	PostMessage(PACKAGE_CHECKBOX);
}


void
InstallerWindow::_SetStatusMessage(const char *text)
{
	fStatusView->SetText(text);
}


void
InstallerWindow::_SetCopyEngineCancelSemaphore(sem_id id, bool alreadyLocked)
{
	if (fCopyEngineCancelSemaphore >= 0) {
		if (!alreadyLocked)
			acquire_sem(fCopyEngineCancelSemaphore);
		delete_sem(fCopyEngineCancelSemaphore);
	}
	fCopyEngineCancelSemaphore = id;
}


void
InstallerWindow::_QuitCopyEngine(bool askUser)
{
	if (fCopyEngineCancelSemaphore < 0)
		return;

	// First of all block the copy engine, so that it doesn't continue
	// while the alert is showing, which would be irritating.
	acquire_sem(fCopyEngineCancelSemaphore);
	
	bool quit = true;
	if (askUser) {
		quit = (new BAlert("cancel",
			TR("Are you sure you want to to stop the installation?"),
			TR_CMT("Continue", "In alert after pressing Stop"),
			TR_CMT("Stop", "In alert after pressing Stop"), 0,
			B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go() != 0;
	}

	if (quit) {
		// Make it quit by having it's lock fail...
		_SetCopyEngineCancelSemaphore(-1, true);
	} else
		release_sem(fCopyEngineCancelSemaphore);
}


// #pragma mark -


int
InstallerWindow::_ComparePackages(const void* firstArg, const void* secondArg)
{
	const Group* group1 = *static_cast<const Group* const *>(firstArg);
	const Group* group2 = *static_cast<const Group* const *>(secondArg);
	const Package* package1 = dynamic_cast<const Package*>(group1);
	const Package* package2 = dynamic_cast<const Package*>(group2);
	int sameGroup = strcmp(group1->GroupName(), group2->GroupName());
	if (sameGroup != 0)
		return sameGroup;
	if (package2 == NULL)
		return -1;
	if (package1 == NULL)
		return 1;
	return strcmp(package1->Name(), package2->Name());
}


