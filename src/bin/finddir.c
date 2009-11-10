/*
 * Copyright 2002, Sebastian Nozzi <sebnozzi@gmx.net>.
 * Copyright 2004, Francois Revol.
 *
 * Distributed under the terms of the MIT License.
 */

#include <stdio.h>
#include <string.h>

#include <FindDirectory.h>
#include <fs_info.h>


#define NO_ERRORS			0
#define ARGUMENT_MISSING	1
#define WRONG_DIR_TYPE		2

typedef struct {
	const char *key;
	directory_which value;
} directoryType;

#define KEYVALUE_PAIR(key) {#key, key}

directoryType directoryTypes[] = {
	// Generic directories
	KEYVALUE_PAIR(B_DESKTOP_DIRECTORY),
	KEYVALUE_PAIR(B_TRASH_DIRECTORY),
	KEYVALUE_PAIR(B_APPS_DIRECTORY),
	KEYVALUE_PAIR(B_PREFERENCES_DIRECTORY),
	KEYVALUE_PAIR(B_UTILITIES_DIRECTORY),

	// System directories
	KEYVALUE_PAIR(B_SYSTEM_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_ADDONS_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_BOOT_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_FONTS_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_LIB_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_SERVERS_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_APPS_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_BIN_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_DOCUMENTATION_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_PREFERENCES_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_TRANSLATORS_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_MEDIA_NODES_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_SOUNDS_DIRECTORY),
	KEYVALUE_PAIR(B_SYSTEM_DATA_DIRECTORY),

	// Common directories
	KEYVALUE_PAIR(B_COMMON_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_SYSTEM_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_ADDONS_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_BOOT_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_FONTS_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_LIB_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_SERVERS_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_BIN_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_ETC_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_DOCUMENTATION_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_SETTINGS_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_DEVELOP_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_LOG_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_SPOOL_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_TEMP_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_VAR_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_TRANSLATORS_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_MEDIA_NODES_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_SOUNDS_DIRECTORY),
	KEYVALUE_PAIR(B_COMMON_DATA_DIRECTORY),

	// User directories
	KEYVALUE_PAIR(B_USER_DIRECTORY),
	KEYVALUE_PAIR(B_USER_CONFIG_DIRECTORY),
	KEYVALUE_PAIR(B_USER_ADDONS_DIRECTORY),
	KEYVALUE_PAIR(B_USER_BOOT_DIRECTORY),
	KEYVALUE_PAIR(B_USER_FONTS_DIRECTORY),
	KEYVALUE_PAIR(B_USER_LIB_DIRECTORY),
	KEYVALUE_PAIR(B_USER_SETTINGS_DIRECTORY),
	KEYVALUE_PAIR(B_USER_DESKBAR_DIRECTORY),
	KEYVALUE_PAIR(B_USER_PRINTERS_DIRECTORY),
	KEYVALUE_PAIR(B_USER_TRANSLATORS_DIRECTORY),
	KEYVALUE_PAIR(B_USER_MEDIA_NODES_DIRECTORY),
	KEYVALUE_PAIR(B_USER_SOUNDS_DIRECTORY),
	KEYVALUE_PAIR(B_USER_DATA_DIRECTORY),
	KEYVALUE_PAIR(B_USER_CACHE_DIRECTORY),

	// Legacy system directories
	KEYVALUE_PAIR(B_BEOS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_SYSTEM_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_ADDONS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_BOOT_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_FONTS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_LIB_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_SERVERS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_APPS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_BIN_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_ETC_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_DOCUMENTATION_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_PREFERENCES_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_TRANSLATORS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_MEDIA_NODES_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_SOUNDS_DIRECTORY),
	KEYVALUE_PAIR(B_BEOS_DATA_DIRECTORY),

	{NULL, B_USER_DESKBAR_DIRECTORY}
};


static void
listDirectoryWhich(void)
{
	int i;
	
	for (i = 0; directoryTypes[i].key; i++) {
		printf("%s\n", directoryTypes[i].key);
	}
}


static bool
retrieveDirValue(directoryType *list, const char *key,
	directory_which *valueOut)
{
	unsigned i = 0;
	
	while (list[i].key != NULL) {
		if (strcmp(list[i].key, key) == 0) {
			*valueOut = list[i].value;
			return true;
		}

		i++;
	}

	return false;
}


static void
usageMsg()
{
	printf("usage:  /bin/finddir -l | [ -v volume ] directory_which\n");
	printf("\t-l\t    list valid which constants to use\n");
	printf("\t-v <file>   use the specified volume for directory\n");
	printf("\t\t    constants that are volume-specific.\n");
	printf("\t\t    <file> can be any file on that volume.\n");
	printf("\t\t    defaults to the boot volume.\n");
	printf(" For a description of recognized directory_which constants,\n");
	printf(" see the find_directory(...) documentation in the Be Book.\n");
}


int
main(int argc, char *argv[])
{
	int directoryArgNr;
	int status;
	dev_t volume;
	directory_which dirType;
	int returnCode;
	
	status = NO_ERRORS;
	directoryArgNr = 1;
	returnCode = 0;

	dirType = B_BEOS_DIRECTORY; /* so that it compiles */
	
	/* By default use boot volume*/
	volume = dev_for_path("/boot");
	
	if (argc <= 1) {
		status = ARGUMENT_MISSING;
	} else {
		if (strcmp(argv[1], "-l") == 0 ) {
			listDirectoryWhich();
			return 0;
		}
		if (strcmp(argv[1], "-v") == 0 ) {
			if (argc >= 3) {
				dev_t temp_volume;
				/* get volume from second arg */
				temp_volume = dev_for_path(argv[2]);
				
				/* Keep default value in case of error */
				if (temp_volume >= 0)
					volume = temp_volume;
				
				/* two arguments were used for volume */
				directoryArgNr+=2;
			} else {
				/* set status to argument missing */
				status = ARGUMENT_MISSING;
			}
		}
	}
	
	if (status == NO_ERRORS && argc > directoryArgNr) {
		/* get directory constant from next argument */

		if (!retrieveDirValue(directoryTypes, argv[directoryArgNr], &dirType))
			status = WRONG_DIR_TYPE;
	} else {
		status = ARGUMENT_MISSING;
	}
	
	/* Do the actual directoy finding */
	
	if (status == NO_ERRORS) {
		/* Question: would B_PATH_NAME_LENGTH alone have been enough? */
		char buffer[B_PATH_NAME_LENGTH+B_FILE_NAME_LENGTH];
		status_t result = find_directory (dirType, volume, false, buffer,
			sizeof(buffer));
		if (result == B_OK) {
			printf("%s\n", buffer);
		} else {
			/* else what? */
			/* this can not happen! */
			fprintf(stderr, "Serious internal error; contact support\n");
		}
	}

	/* Error messages and return code setting */

	if (status == WRONG_DIR_TYPE) {
		fprintf(stderr, "%s: unrecognized directory_which constant \'%s\'\n", argv[0],
			argv[directoryArgNr]);
		returnCode = 252;
	}

	if (status == ARGUMENT_MISSING) {
		usageMsg();
		returnCode = 255;
	}

	return returnCode;
}

