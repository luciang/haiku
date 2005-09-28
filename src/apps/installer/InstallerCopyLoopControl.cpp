/*
 * Copyright 2005, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 */
#include "InstallerCopyLoopControl.h"

InstallerCopyLoopControl::InstallerCopyLoopControl(InstallerWindow *window)
	: CopyLoopControl(),
	fWindow(window)
{
}


InstallerCopyLoopControl::~InstallerCopyLoopControl(void)
{
}


bool 
InstallerCopyLoopControl::FileError(const char *message, const char *name, status_t error,
			bool allowContinue)
{
	return false;
}
	

void 
InstallerCopyLoopControl::UpdateStatus(const char *name, entry_ref ref, int32 count, 
			bool optional)
{
}


bool 
InstallerCopyLoopControl::CheckUserCanceled()
{
	return false;
}


InstallerCopyLoopControl::OverwriteMode 
InstallerCopyLoopControl::OverwriteOnConflict(const BEntry *srcEntry, 
			const char *destName, const BDirectory *destDir, bool srcIsDir, 
			bool dstIsDir)
{
	return kReplace;
}


bool 
InstallerCopyLoopControl::SkipEntry(const BEntry *, bool file)
{
	return false;
}


void 
InstallerCopyLoopControl::ChecksumChunk(const char *block, size_t size)
{
}


bool 
InstallerCopyLoopControl::ChecksumFile(const entry_ref *ref)
{
	return true;
}


bool 
InstallerCopyLoopControl::SkipAttribute(const char *attributeName)
{
	return false;
}


bool 
InstallerCopyLoopControl::PreserveAttribute(const char *attributeName)
{
	return false;
}

