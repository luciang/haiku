/*
 * Copyright 1999, Be Incorporated.
 * Copyright 2007, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Be Incorporated
 *		Eric Petit <eric.petit@lapsus.org>
 */

#include "string.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include <sys/ioctl.h>

#include "GlobalData.h"


/* Initialization code shared between primary and cloned accelerants */
static
status_t InitCommon(int fd)
{
	status_t ret;

	/* Memorize the file descriptor */
	gFd = fd;

	/* Contact driver and get a pointer to the registers and shared data */
	if ((ret = ioctl(fd, VMWARE_GET_PRIVATE_DATA, &gSharedArea,
			sizeof(area_id))) != B_OK) {
		TRACE("VMWARE_GET_PRIVATE_DATA failed (%d\n", ret);
		return ret;
	}

	/* Clone the shared area for our use */
	if ((gSharedArea = clone_area("VMware shared", (void **)&gSi,
			B_ANY_ADDRESS, B_READ_AREA|B_WRITE_AREA, gSharedArea)) < 0) {
		TRACE("could not clone shared area (%d)\n", gSharedArea);
		return gSharedArea;
	}

	return B_OK;
}


static void
UninitCommon()
{
	delete_area(gSharedArea);
}


status_t
INIT_ACCELERANT(int fd)
{
	status_t ret;

	TRACE("INIT_ACCELERANT (%d)\n", fd);

	gAccelerantIsClone = 0;

	/* Common initialization for the primary accelerant and clones */
	if ((ret = InitCommon(fd)) == B_OK) {
		/* Init semaphores */
		INIT_BEN(gSi->engineLock);
		INIT_BEN(gSi->fifoLock);
	}

	TRACE("INIT_ACCELERANT: %d\n", ret);
	return ret;
}


ssize_t
ACCELERANT_CLONE_INFO_SIZE()
{
	return MAX_SAMPLE_DEVICE_NAME_LENGTH;
}


void
GET_ACCELERANT_CLONE_INFO(void *data)
{
	/* TODO */
}


status_t
CLONE_ACCELERANT(void *data)
{
	/* TODO */
	return B_ERROR;
}


void
UNINIT_ACCELERANT()
{
	UninitCommon();
	if (gAccelerantIsClone)
		close(gFd);
	else if (gUpdateThread > B_OK) {
		status_t exitValue;
		gUpdateThreadDie = 1;
		wait_for_thread(gUpdateThread, &exitValue);
	}
	ioctl(gFd, VMWARE_FIFO_STOP, NULL, 0);
}

