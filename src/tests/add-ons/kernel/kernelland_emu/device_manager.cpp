/*
 * Copyright 2002-2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Ingo Weinhold, bonefish@cs.tu-berlin.de.
 *		Axel Dörfler, axeld@pinc-software.de.
 */

#include <stdio.h>

#include <fs/devfs.h>


extern "C" status_t
devfs_unpublish_partition(const char *devicePath, const char *name)
{
	printf("unpublish partition: %s/%s\n", devicePath, name);
	return B_OK;
}


extern "C" status_t
devfs_publish_partition(const char *path, const partition_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	printf("publish partition: %s (device \"%s\", size %Ld)\n", path, info->device, info->size);
	return B_OK;
}
