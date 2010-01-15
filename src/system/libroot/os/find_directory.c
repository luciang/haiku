/*
 * Copyright 2004, François Revol.
 * Copyright 2007-2009, Axel Dörfler, axeld@pinc-software.de.
 *
 * Distributed under the terms of the MIT license.
 */

// TODO: this call is currently compiled for the kernel and libroot separately;
//		they may not always return the same directory right now!

#ifdef _KERNEL_MODE
#	include <vfs.h>
#else
#	include <syscalls.h>
#endif

#include <FindDirectory.h>
#include <fs_info.h>

#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>

#include <user_group.h>

/* use pwents to find home */
#define USE_PWENTS


/* Haiku system directories */

#define SYSTEM "system"

static const char *kSystemDirectories[] = {
	SYSTEM,										// B_SYSTEM_DIRECTORY
	SYSTEM,										// B_BEOS_SYSTEM_DIRECTORY
	SYSTEM "/add-ons",
	SYSTEM "/boot",
	SYSTEM "/data/fonts",
	SYSTEM "/lib",
	SYSTEM "/servers",
	SYSTEM "/apps",
	SYSTEM "/bin",
	"common/etc",
	SYSTEM "/documentation",
	SYSTEM "/preferences",
	SYSTEM "/add-ons/Translators",
	SYSTEM "/add-ons/media",
	SYSTEM "/data/sounds",
	SYSTEM "/data",
};

/* Common directories, shared among users */

#define COMMON "common"
	// ToDo: this is for now and might be changed back to "home"
	//	(or even something else) later

static const char *kCommonDirectories[] = {
	COMMON "",								// B_COMMON_DIRECTORY
	COMMON "",								// B_COMMON_SYSTEM_DIRECTORY
	COMMON "/add-ons",
	COMMON "/boot",
	COMMON "/data/fonts",
	COMMON "/lib",
	COMMON "/servers",
	COMMON "/bin",
	COMMON "/etc",
	COMMON "/documentation",
	COMMON "/settings",
	"develop",								// B_COMMON_DEVELOP_DIRECTORY
	"var/log",								// B_COMMON_LOG_DIRECTORY
	"var/spool",							// B_COMMON_SPOOL_DIRECTORY
	"var/tmp",								// B_COMMON_TEMP_DIRECTORY
	"var",									// B_COMMON_VAR_DIRECTORY
	COMMON "/add-ons/Translators",
	COMMON "/add-ons/media",
	COMMON "/data/sounds",
	COMMON "/data",
};

/* User directories */

#define HOME "$h"

static const char *kUserDirectories[] = {
	HOME "",								// B_USER_DIRECTORY
	HOME "/config",							// B_USER_CONFIG_DIRECTORY
	HOME "/config/add-ons",
	HOME "/config/boot",
	HOME "/config/data/fonts",
	HOME "/config/lib",
	HOME "/config/settings",
	HOME "/config/be",
	HOME "/config/settings/printers",
	HOME "/config/add-ons/Translators",
	HOME "/config/add-ons/media",
	HOME "/config/data/sounds",
	HOME "/config/data",
	HOME "/config/cache",
};


/*! make dir and its parents if needed */
static int
create_path(const char *path, mode_t mode)
{
	char buffer[B_PATH_NAME_LENGTH + 1];
	int pathLength;
	int i = 0;

	if (path == NULL || ((pathLength = strlen(path)) > B_PATH_NAME_LENGTH))
		return EINVAL;

	while (++i < pathLength) {
		char *slash = strchr(&path[i], '/');
		struct stat st;

		if (slash == NULL)
			i = pathLength;
		else if (i != slash - path)
			i = slash - path;
		else
			continue;

		strlcpy(buffer, path, i + 1);
		if (stat(buffer, &st) < 0) {
			errno = 0;
			if (mkdir(buffer, mode) < 0)
				return errno;
		}
	}

	return 0;
}


//	#pragma mark -


status_t
find_directory(directory_which which, dev_t device, bool createIt,
	char *returnedPath, int32 pathLength)
{
	status_t err = B_OK;
	dev_t bootDevice = -1;
	struct fs_info fsInfo;
	struct stat st;
	char *buffer = NULL;
	char *home = NULL;
	const char *template = NULL;

	/* as with the R5 version, no on-stack buffer */
	buffer = (char *)malloc(pathLength);
	memset(buffer, 0, pathLength);

	/* fiddle with non-boot volume for items that need it */
	switch (which) {
		case B_DESKTOP_DIRECTORY:
		case B_TRASH_DIRECTORY:
			bootDevice = dev_for_path("/boot");
			if (device <= 0)
				device = bootDevice;
			if (fs_stat_dev(device, &fsInfo) < B_OK) {
				free(buffer);
				return ENODEV;
			}
			if (device != bootDevice) {
#ifdef _KERNEL_MODE
				err = _user_entry_ref_to_path(device, fsInfo.root, /*"."*/
					NULL, buffer, pathLength);
#else
				err = _kern_entry_ref_to_path(device, fsInfo.root, /*"."*/
					NULL, buffer, pathLength);
#endif
			} else {
				/* use the user id to find the home folder */
				/* done later */
				strlcat(buffer, "/boot", pathLength);
			}
			break;
		default:
			strlcat(buffer, "/boot", pathLength);
			break;
	}

	if (err < B_OK) {
		free(buffer);
		return err;
	}

	switch (which) {
		/* Per volume directories */
		case B_DESKTOP_DIRECTORY:
			if (device == bootDevice || !strcmp(fsInfo.fsh_name, "bfs"))
				template = "$h/Desktop";
			break;
		case B_TRASH_DIRECTORY:
			// TODO: eventually put that into the file system API?
			if (device == bootDevice || !strcmp(fsInfo.fsh_name, "bfs"))
				template = "Trash"; // TODO: add suffix for current user
			else if (!strcmp(fsInfo.fsh_name, "dos"))
				template = "RECYCLED/_BEOS_";
			break;

		/* Haiku system directories */
		case B_SYSTEM_DIRECTORY:
		case B_BEOS_SYSTEM_DIRECTORY:
		case B_SYSTEM_ADDONS_DIRECTORY:
		case B_SYSTEM_BOOT_DIRECTORY:
		case B_SYSTEM_FONTS_DIRECTORY:
		case B_SYSTEM_LIB_DIRECTORY:
		case B_SYSTEM_SERVERS_DIRECTORY:
		case B_SYSTEM_APPS_DIRECTORY:
		case B_SYSTEM_BIN_DIRECTORY:
		case B_BEOS_ETC_DIRECTORY:
		case B_SYSTEM_DOCUMENTATION_DIRECTORY:
		case B_SYSTEM_PREFERENCES_DIRECTORY:
		case B_SYSTEM_TRANSLATORS_DIRECTORY:
		case B_SYSTEM_MEDIA_NODES_DIRECTORY:
		case B_SYSTEM_SOUNDS_DIRECTORY:
		case B_SYSTEM_DATA_DIRECTORY:
			template = kSystemDirectories[which - B_SYSTEM_DIRECTORY];
			break;

		/* Common directories, shared among users */
		case B_COMMON_DIRECTORY:
		case B_COMMON_SYSTEM_DIRECTORY:
		case B_COMMON_ADDONS_DIRECTORY:
		case B_COMMON_BOOT_DIRECTORY:
		case B_COMMON_FONTS_DIRECTORY:
		case B_COMMON_LIB_DIRECTORY:
		case B_COMMON_SERVERS_DIRECTORY:
		case B_COMMON_BIN_DIRECTORY:
		case B_COMMON_ETC_DIRECTORY:
		case B_COMMON_DOCUMENTATION_DIRECTORY:
		case B_COMMON_SETTINGS_DIRECTORY:
		case B_COMMON_DEVELOP_DIRECTORY:
		case B_COMMON_LOG_DIRECTORY:
		case B_COMMON_SPOOL_DIRECTORY:
		case B_COMMON_TEMP_DIRECTORY:
		case B_COMMON_VAR_DIRECTORY:
		case B_COMMON_TRANSLATORS_DIRECTORY:
		case B_COMMON_MEDIA_NODES_DIRECTORY:
		case B_COMMON_SOUNDS_DIRECTORY:
		case B_COMMON_DATA_DIRECTORY:
			template = kCommonDirectories[which - B_COMMON_DIRECTORY];
			break;

		/* User directories */
		case B_USER_DIRECTORY:
		case B_USER_CONFIG_DIRECTORY:
		case B_USER_ADDONS_DIRECTORY:
		case B_USER_BOOT_DIRECTORY:
		case B_USER_FONTS_DIRECTORY:
		case B_USER_LIB_DIRECTORY:
		case B_USER_SETTINGS_DIRECTORY:
		case B_USER_DESKBAR_DIRECTORY:
		case B_USER_PRINTERS_DIRECTORY:
		case B_USER_TRANSLATORS_DIRECTORY:
		case B_USER_MEDIA_NODES_DIRECTORY:
		case B_USER_SOUNDS_DIRECTORY:
		case B_USER_DATA_DIRECTORY:
		case B_USER_CACHE_DIRECTORY:
			template = kUserDirectories[which - B_USER_DIRECTORY];
			break;

		/* Global directories */
		case B_APPS_DIRECTORY:
			template = "apps";
			break;
		case B_PREFERENCES_DIRECTORY:
			template = "preferences";
			break;
		case B_UTILITIES_DIRECTORY:
			template = "utilities";
			break;

		default:
			free(buffer);
			return EINVAL;
	}

	err = B_OK;
	if (template) {
		if (!strncmp(template, "$h", 2)) {
			if (bootDevice > -1 && device != bootDevice) {
				int l = pathLength - strlen(buffer);
				if (l > 5)
					strncat(buffer, "/home", 5);
			} else {
#ifndef _KERNEL_MODE
#ifdef USE_PWENTS
				struct passwd pwBuffer;
				char pwStringBuffer[MAX_PASSWD_BUFFER_SIZE];
				struct passwd *pw;

				if (getpwuid_r(geteuid(), &pwBuffer, pwStringBuffer,
						sizeof(pwStringBuffer), &pw) == 0) {
					home = pw->pw_dir;
				}
#endif	// USE_PWENTS
				if (!home) {
					/* use env var */
					home = getenv("HOME");
				}
#endif	// !_KERNEL_MODE
				if (!home)
					home = "/boot/home";
				strncpy(buffer, home, pathLength);
			}
			template += 2;
		} else
			strlcat(buffer, "/", pathLength);

		if (!err && strlen(buffer) + 2 + strlen(template) < (uint32)pathLength)
			strcat(buffer, template);
		else
			err = err ? err : E2BIG;
	} else
		err = err ? err : ENOENT;

	if (!err && createIt && stat(buffer, &st) < 0)
		err = create_path(buffer, 0755);
	if (!err)
		strlcpy(returnedPath, buffer, pathLength);

	free(buffer);
	return err;
}

