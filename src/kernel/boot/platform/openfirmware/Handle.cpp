/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <SupportDefs.h>

#include "Handle.h"
#include "openfirmware.h"


Handle::Handle(int handle, bool takeOwnership)
	:
	fHandle(handle),
	fOwnHandle(takeOwnership)
{
}


Handle::Handle(void)
	:
	fHandle(0)
{
}


Handle::~Handle()
{
	if (fOwnHandle)
		of_close(fHandle);
}


void
Handle::SetHandle(int handle, bool takeOwnership)
{
	if (fHandle && fOwnHandle)
		of_close(fHandle);

	fHandle = handle;
	fOwnHandle = takeOwnership;
}


ssize_t
Handle::ReadAt(void *cookie, off_t pos, void *buffer, size_t bufferSize)
{
	if (pos == -1 || of_seek(fHandle, pos) != OF_FAILED)
		return of_read(fHandle, buffer, bufferSize);

	return B_ERROR;
}


ssize_t
Handle::WriteAt(void *cookie, off_t pos, const void *buffer, size_t bufferSize)
{
	if (pos == -1 || of_seek(fHandle, pos) != OF_FAILED)
		return of_write(fHandle, buffer, bufferSize);

	return B_ERROR;
}

