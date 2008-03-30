/*
 * Copyright 2008, Michael Pfeiffer, laplace@users.sourceforge.net. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef BOOT_MANAGER_CONTROLLER_H
#define BOOT_MANAGER_CONTROLLER_H

#include "WizardController.h"

#include <Message.h>

class BootDrive;

class WizardView;
class WizardPageView;
class BRect;

/*
	Remainder of Settings Message Format:
   (See also BootDrive.h)
   	"install" bool (whether install or uninstall should be performed)
   	"file" String (the file where the backup of the MBR is saved)
   	"defaultPartition" int32 (index of default partition)
   	"timeout" int32 (timeout in seconds, -1 for no timeout)
   	"installStatus" int32 (status_t)
   	"saveMBRStatus" int32 (status_t)
   	"restoreMBRStatus" int32 (status_t)
 */

class BootManagerController : public WizardController
{
public:
	BootManagerController();
	virtual ~BootManagerController();

protected:
	virtual int32 InitialState();
	virtual int32 NextState(int32 state);
	virtual WizardPageView* CreatePage(int32 state, WizardView* wizard);

private:

	enum State {
		kStateEntry,
		
		// Install states
		kStateSaveMBR,
		kStateMBRSaved,
		kStatePartitions,
		kStateDefaultPartitions,
		kStateInstallSummary,
		kStateInstalled,
		
		// Uninstall states
		kStateUninstall,
		kStateUninstalled
	};
	
	bool _HasSelectedPartitions();	
	bool _WriteBootMenu();
	bool _SaveMBR();
	bool _RestoreMBR();
	
	WizardPageView* _CreateSaveMBRPage(BRect frame);
	WizardPageView* _CreateMBRSavedPage(BRect frame);
	WizardPageView* _CreateInstallSummaryPage(BRect frame);
	WizardPageView* _CreateInstalledPage(BRect frame);
	WizardPageView* _CreateUninstallPage(BRect frame);
	WizardPageView* _CreateUninstalledPage(BRect frame);
	
	BMessage fSettings;
	BootDrive* fBootDrive;
};


#endif	// BOOT_MANAGER_CONTROLLER_H
