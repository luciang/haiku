/*
 * Copyright 2009-2010, Colin Günther, coling@gmx.de.
 * All Rights Reserved. Distributed under the terms of the MIT License.
 *
 */


#include <posix/sys/mman.h>

#include <compat/sys/param.h>
#include <compat/sys/firmware.h>
#include <compat/sys/haiku-module.h>

#include <stdlib.h>
#include <string.h>

#include <FindDirectory.h>
#include <StorageDefs.h>
#include <SupportDefs.h>

#include <device.h>


#define MAX_FBSD_FIRMWARE_NAME_CHARS 64
	// For strndup, beeing cautious in kernel code is a good thing.
	// NB: This constant doesn't exist in FreeBSD.


static const char*
getHaikuFirmwareName(const char* fbsdFirmwareName,
	const char* unknownFirmwareName)
{
	int i;

	if (__haiku_firmware_name_map == NULL)
		return unknownFirmwareName;

	for (i = 0; i < __haiku_firmware_parts_count; i++) {
		if (strcmp(__haiku_firmware_name_map[i][0], fbsdFirmwareName) == 0)
			return __haiku_firmware_name_map[i][1];
	}
	return unknownFirmwareName;
}


const struct firmware*
firmware_get(const char* fbsdFirmwareName)
{
	char*				fbsdFirmwareNameCopy = NULL;
	int					fileDescriptor = 0;
	struct firmware*	firmware = NULL;
	int32				firmwareFileSize;
	char*				firmwarePath = NULL;
	const char*			haikuFirmwareName = NULL;
	ssize_t				readCount = 0;

	haikuFirmwareName = getHaikuFirmwareName(fbsdFirmwareName,
		fbsdFirmwareName);

	firmwarePath = (char*)malloc(B_PATH_NAME_LENGTH);
	if (firmwarePath == NULL)
		goto cleanup;

	if (find_directory(B_SYSTEM_DATA_DIRECTORY, -1, false,
		firmwarePath, B_PATH_NAME_LENGTH) != B_OK)
		goto cleanup;

	strlcat(firmwarePath, "/firmware/", B_PATH_NAME_LENGTH);
	strlcat(firmwarePath, gDriverName, B_PATH_NAME_LENGTH);
	strlcat(firmwarePath, "/", B_PATH_NAME_LENGTH);
	strlcat(firmwarePath, haikuFirmwareName, B_PATH_NAME_LENGTH);

	fileDescriptor = open(firmwarePath, B_READ_ONLY);
	if (fileDescriptor == -1)
		goto cleanup;

	firmwareFileSize = lseek(fileDescriptor, 0, SEEK_END);
	lseek(fileDescriptor, 0, SEEK_SET);

	fbsdFirmwareNameCopy = strndup(fbsdFirmwareName,
		MAX_FBSD_FIRMWARE_NAME_CHARS);
	if (fbsdFirmwareNameCopy == NULL)
		goto cleanup;

	firmware = (struct firmware*)malloc(sizeof(struct firmware));
	if (firmware == NULL)
		goto cleanup;

	firmware->data = malloc(firmwareFileSize);
	if (firmware->data == NULL)
		goto cleanup;

	readCount = read(fileDescriptor, (void*)firmware->data, firmwareFileSize);
	if (readCount == -1 || readCount < firmwareFileSize) {
		free((void*)firmware->data);
		goto cleanup;
	}

	firmware->datasize = firmwareFileSize;
	firmware->name = fbsdFirmwareNameCopy;
	firmware->version = __haiku_firmware_version;

	close(fileDescriptor);
	free(firmwarePath);
	return firmware;

cleanup:
	if (firmware)
		free(firmware);
	if (fbsdFirmwareNameCopy)
		free(fbsdFirmwareNameCopy);
	if (firmwarePath)
		free(firmwarePath);
	if (fileDescriptor)
		close(fileDescriptor);
	return NULL;
}


void
firmware_put(const struct firmware* firmware, int flags)
{
	if (firmware == NULL)
		return;

	if (firmware->data)
		free((void*)firmware->data);
	if (firmware->name)
		free((void*)firmware->name);
	free((void*)firmware);
}
