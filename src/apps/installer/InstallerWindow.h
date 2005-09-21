/*
 * Copyright 2005, Jérôme DUVAL. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _InstallerWindow_h
#define _InstallerWindow_h

#include <Box.h>
#include <Button.h>
#include <Menu.h>
#include <MenuField.h>
#include <TextView.h>
#include <Window.h>
#include "DrawButton.h"

class InstallerWindow : public BWindow {
public:
	InstallerWindow(BRect frameRect);
	virtual ~InstallerWindow();

	virtual void MessageReceived(BMessage *msg);
	virtual bool QuitRequested();
	
private:
	void DisableInterface(bool disable);
	void LaunchDriveSetup();
	void ShowBottom();
	void ScanningComplete();
	void ScanningInProgress();
	BBox *fBackBox;
	BButton *fBeginButton, *fSetupButton;
	DrawButton *fDrawButton;
	bool fDriveSetupLaunched;
	BTextView *fStatusView;
	BMenu* fSrcMenu, *fDestMenu;
	BMenuField* fSrcMenuField, *fDestMenuField;
};

#endif /* _InstallerWindow_h */
