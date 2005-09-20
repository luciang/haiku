/*
 * Copyright 2005, J�r�me DUVAL. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _InstallerApp_h
#define _InstallerApp_h

#include <Application.h>
#include "InstallerWindow.h"

class InstallerApp : public BApplication {
public:
	InstallerApp();

public:
	virtual void AboutRequested();
	virtual void ReadyToRun();
	
private:
	InstallerWindow *fWindow;
};

#endif /* _InstallerApp_h */
