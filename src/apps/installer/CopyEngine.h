/*
 * Copyright 2005, Jérôme DUVAL. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CopyEngine_h
#define _CopyEngine_h

#include "InstallerCopyLoopControl.h"

#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <Looper.h>
#include <Messenger.h>
#include <Partition.h>
#include <Volume.h>

class InstallerWindow;

const uint32 ENGINE_START = 'eSRT';

class CopyEngine : public BLooper {
	public:
		CopyEngine(InstallerWindow *window);
		void MessageReceived(BMessage *msg);
		void ScanDisksPartitions(BMenu *srcMenu, BMenu *targetMenu);
		void SetPackagesList(BList *list);
		void SetSpaceRequired(off_t bytes) { fSpaceRequired = bytes; };
		bool Cancel();
	private:
		void LaunchInitScript(BPath &path);
		void LaunchFinishScript(BPath &path);
		void SetStatusMessage(char *status);
		void Start(BMenu *srcMenu, BMenu *targetMenu);
		status_t CopyFolder(BDirectory &srcDir, BDirectory &targetDir);

		InstallerWindow *fWindow;
		BDiskDeviceRoster fDDRoster;
		InstallerCopyLoopControl *fControl;
		BList *fPackages;
		off_t fSpaceRequired;
};

#endif /* _CopyEngine_h */
