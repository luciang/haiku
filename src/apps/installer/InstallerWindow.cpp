/*
 * Copyright 2005, Jérôme DUVAL. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Box.h>
#include <ClassInfo.h>
#include <Directory.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <string.h>
#include <String.h>
#include "InstallerWindow.h"
#include "PartitionMenuItem.h"

#define DRIVESETUP_SIG "application/x-vnd.Be-DRV$"

const uint32 BEGIN_MESSAGE = 'iBGN';
const uint32 SHOW_BOTTOM_MESSAGE = 'iSBT';
const uint32 SETUP_MESSAGE = 'iSEP';
const uint32 START_SCAN = 'iSSC';
const uint32 SRC_PARTITION = 'iSPT';
const uint32 DST_PARTITION = 'iDPT';
const uint32 PACKAGE_CHECKBOX = 'iPCB';

InstallerWindow::InstallerWindow(BRect frame_rect)
	: BWindow(frame_rect, "Installer", B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
	fDriveSetupLaunched(false),
	fCopyEngine(this)
{

	BRect bounds = Bounds();
	bounds.bottom += 1;
	bounds.right += 1;
	fBackBox = new BBox(bounds, NULL, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER);
	AddChild(fBackBox);

	BRect statusRect(bounds.left+120, bounds.top+14, bounds.right-14, bounds.top+60);
	BRect textRect(statusRect);
	textRect.OffsetTo(B_ORIGIN);
	textRect.InsetBy(2,2);
	fStatusView = new BTextView(statusRect, "statusView", textRect,
		be_plain_font, NULL, B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW);
	fStatusView->MakeEditable(false);
	fStatusView->MakeSelectable(false);
	
	BScrollView *scroll = new BScrollView("statusScroll", fStatusView, B_FOLLOW_LEFT|B_FOLLOW_TOP, B_WILL_DRAW|B_FRAME_EVENTS);
        fBackBox->AddChild(scroll);

	fBeginButton = new BButton(BRect(bounds.right-90, bounds.bottom-35, bounds.right-11, bounds.bottom-11), 
		"begin_button", "Begin", new BMessage(BEGIN_MESSAGE), B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	fBeginButton->MakeDefault(true);
	fBackBox->AddChild(fBeginButton);

	fSetupButton = new BButton(BRect(bounds.left+11, bounds.bottom-35, bounds.left+111, bounds.bottom-22),
		"setup_button", "Setup partitions" B_UTF8_ELLIPSIS, new BMessage(SETUP_MESSAGE), B_FOLLOW_LEFT|B_FOLLOW_BOTTOM);
	fBackBox->AddChild(fSetupButton);
	fSetupButton->Hide();

	fPackagesView = new PackagesView(BRect(bounds.left+12, bounds.top+4, bounds.right-15-B_V_SCROLL_BAR_WIDTH, bounds.bottom-61), "packages_view");
	fPackagesScrollView = new BScrollView("packagesScroll", fPackagesView, B_FOLLOW_LEFT | B_FOLLOW_BOTTOM, B_WILL_DRAW,
		false, true);
	fBackBox->AddChild(fPackagesScrollView);
	fPackagesScrollView->Hide();

	fDrawButton = new DrawButton(BRect(bounds.left+12, bounds.bottom-33, bounds.left+100, bounds.bottom-20),
		"options_button", "Fewer options", "More options", new BMessage(SHOW_BOTTOM_MESSAGE));
	fBackBox->AddChild(fDrawButton);

	fDestMenu = new BPopUpMenu("scanning" B_UTF8_ELLIPSIS);
	fSrcMenu = new BPopUpMenu("scanning" B_UTF8_ELLIPSIS);

	BRect fieldRect(bounds.left+50, bounds.top+70, bounds.right-13, bounds.top+90);
	fSrcMenuField = new BMenuField(fieldRect, "srcMenuField",
                "Install from: ", fSrcMenu);
        fSrcMenuField->SetDivider(70.0);
        fSrcMenuField->SetAlignment(B_ALIGN_RIGHT);
        fBackBox->AddChild(fSrcMenuField);

	fieldRect.OffsetBy(0,23);
	fDestMenuField = new BMenuField(fieldRect, "destMenuField",
		"Onto: ", fDestMenu);
	fDestMenuField->SetDivider(70.0);
	fDestMenuField->SetAlignment(B_ALIGN_RIGHT);
	fBackBox->AddChild(fDestMenuField);

	BRect sizeRect = fBackBox->Bounds();
	sizeRect.top = 105;
	sizeRect.bottom = sizeRect.top + 15;
	sizeRect.right -= 12;
	sizeRect.left = sizeRect.right - 150;
	fSizeView = new BStringView(sizeRect, "size_view", "Disk space required: 0.0 KB", B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	fSizeView->SetAlignment(B_ALIGN_RIGHT);
	fBackBox->AddChild(fSizeView);
	fSizeView->Hide();

	// finish creating window
	Show();

	fDriveSetupLaunched = be_roster->IsRunning(DRIVESETUP_SIG);
	be_roster->StartWatching(this);
	
	PostMessage(START_SCAN);
}

InstallerWindow::~InstallerWindow()
{
	be_roster->StopWatching(this);
}


void
InstallerWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case START_SCAN:
			StartScan();
			break;
		case BEGIN_MESSAGE:
			fCopyEngine.Start();
			break;
		case SHOW_BOTTOM_MESSAGE:
			ShowBottom();
			break;
		case SRC_PARTITION:
			PublishPackages();
			break;
		case SETUP_MESSAGE:
			LaunchDriveSetup();
			break;
		case PACKAGE_CHECKBOX: {
			char buffer[15];
			fPackagesView->GetTotalSizeAsString(buffer);
			char string[255];
			sprintf(string, "Disk space required: %s", buffer);
			fSizeView->SetText(string);
			break;
		}
		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char *signature;
			if (msg->FindString("be:signature", &signature)==B_OK
				&& strcasecmp(signature, DRIVESETUP_SIG)==0) {
				DisableInterface(msg->what == B_SOME_APP_LAUNCHED);
                        }
                        break;
                }
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
			"Please close the DriveSetup window before closing the\nInstaller window.", "OK"))->Go();
		return false;
	}
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void 
InstallerWindow::ShowBottom()
{
	if (fDrawButton->Value()) {
		ResizeTo(332,306);
		if (fSetupButton->IsHidden())
			fSetupButton->Show();
		if (fPackagesScrollView->IsHidden())
			fPackagesScrollView->Show();
		if (fSizeView->IsHidden())
			fSizeView->Show();
	} else {
		if (!fSetupButton->IsHidden())
			fSetupButton->Hide();
		if (!fPackagesScrollView->IsHidden())
			fPackagesScrollView->Hide();
		if (!fSizeView->IsHidden())
			fSizeView->Hide();
		ResizeTo(332,160);
	}
}


void
InstallerWindow::LaunchDriveSetup()
{
	if (be_roster->Launch(DRIVESETUP_SIG)!=B_OK)
		fprintf(stderr, "There was an error while launching DriveSetup\n");
}


void
InstallerWindow::DisableInterface(bool disable)
{
	if (!disable) {
		StartScan();
	}
	fDriveSetupLaunched = disable;
	fBeginButton->SetEnabled(!disable);
	fSetupButton->SetEnabled(!disable);
	fSrcMenuField->SetEnabled(!disable);
	fDestMenuField->SetEnabled(!disable);
	if (disable)
		SetStatusMessage("Running DriveSetup" B_UTF8_ELLIPSIS "\nClose DriveSetup to continue with the\ninstallation.");
}


void
InstallerWindow::StartScan()
{
	SetStatusMessage("Scanning for disks" B_UTF8_ELLIPSIS);

	BMenuItem *item;
	while ((item = fSrcMenu->RemoveItem((int32)0)))
		delete item;
	while ((item = fDestMenu->RemoveItem((int32)0)))
		delete item;

	fCopyEngine.ScanDisksPartitions(fSrcMenu, fDestMenu);

	fSrcMenu->AddItem(new PartitionMenuItem("BeOS 5 PE Max Edition V3.1 beta", 
		new BMessage(SRC_PARTITION), "/BeOS 5 PE Max Edition V3.1 beta"));

	if (fSrcMenu->ItemAt(0)) {
		fSrcMenu->ItemAt(0)->SetMarked(true);
		PublishPackages();
	}
	SetStatusMessage("Choose the disk you want to install onto\nfrom the pop-up menu. Then click \"Begin\".");
}


void
InstallerWindow::PublishPackages()
{
	fPackagesView->Clean();
	PartitionMenuItem *item = (PartitionMenuItem *)fSrcMenu->FindMarked();
	if (!item)
		return;

	BPath directory(item->Path());
	directory.Append("_packages_");
	BDirectory dir(directory.Path());
	if (dir.InitCheck()!=B_OK)
		return;

	BEntry packageEntry;
	BList packages;
	while (dir.GetNextEntry(&packageEntry)==B_OK) {
		Package *package = Package::PackageFromEntry(packageEntry);
		if (package) {
			packages.AddItem(package);
		}
	}
	packages.SortItems(ComparePackages);

	fPackagesView->AddPackages(packages, new BMessage(PACKAGE_CHECKBOX));
	PostMessage(PACKAGE_CHECKBOX);	
}


int 
InstallerWindow::ComparePackages(const void *firstArg, const void *secondArg)
{
	const Group *group1 = *static_cast<const Group * const *>(firstArg);
	const Group *group2 = *static_cast<const Group * const *>(secondArg);
	const Package *package1 = dynamic_cast<const Package *>(group1);
	const Package *package2 = dynamic_cast<const Package *>(group2);
	int sameGroup = strcmp(group1->GroupName(), group2->GroupName());
	if (sameGroup != 0)
		return sameGroup;
	if (!package2)
		return -1;
	if (!package1)
		return 1;
	return strcmp(package1->Name(), package2->Name());
}


void
InstallerWindow::SetStatusMessage(char *text)
{
	BAutolock(this);
	fStatusView->SetText(text);
}
