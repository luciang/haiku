/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "package.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "package.h"
#include "PackageEntry.h"
#include "PackageEntryAttribute.h"
#include "PackageReader.h"


struct PackageContentListHandler : PackageContentHandler {
	PackageContentListHandler(bool listAttributes)
		:
		fLevel(0),
		fListAttribute(listAttributes)
	{
	}

	virtual status_t HandleEntry(PackageEntry* entry)
	{
		fLevel++;

		int indentation = (fLevel - 1) * 2;
		printf("%*s", indentation, "");

		// name and size
		printf("%-*s", indentation < 32 ? 32 - indentation : 0, entry->Name());
		printf("  %8llu", entry->Data().UncompressedSize());

		// time
		struct tm* time = localtime(&entry->ModifiedTime().tv_sec);
		printf("  %04d-%02d-%02d %02d:%02d:%02d",
			1900 + time->tm_year, time->tm_mon + 1, time->tm_mday,
			time->tm_hour, time->tm_min, time->tm_sec);

		// file type
		mode_t mode = entry->Mode();
		if (S_ISREG(mode))
			printf("  -");
		else if (S_ISDIR(mode))
			printf("  d");
		else if (S_ISLNK(mode))
			printf("  l");
		else
			printf("  ?");

		// permissions
		char buffer[4];
		printf("%s", _PermissionString(buffer, mode >> 6,
			(mode & S_ISUID) != 0));
		printf("%s", _PermissionString(buffer, mode >> 3,
			(mode & S_ISGID) != 0));
		printf("%s", _PermissionString(buffer, mode, false));

		// print the symlink path
		if (S_ISLNK(mode))
			printf("  -> %s", entry->SymlinkPath());

		printf("\n");
		return B_OK;
	}

	virtual status_t HandleEntryAttribute(PackageEntry* entry,
		PackageEntryAttribute* attribute)
	{
		if (!fListAttribute)
			return B_OK;

		int indentation = fLevel * 2;
		printf("%*s<", indentation, "");
		printf("%-*s  %8llu", indentation < 31 ? 31 - indentation : 0,
			attribute->Name(), attribute->Data().UncompressedSize());

		uint32 type = attribute->Type();
		if (isprint(type & 0xff) && isprint((type >> 8) & 0xff)
			 && isprint((type >> 16) & 0xff) && isprint(type >> 24)) {
			printf("  '%c%c%c%c'", int(type >> 24), int((type >> 16) & 0xff),
				int((type >> 8) & 0xff), int(type & 0xff));
		} else
			printf("  %#lx", type);

		printf(">\n");
		return B_OK;
	}

	virtual status_t HandleEntryDone(PackageEntry* entry)
	{
		fLevel--;
		return B_OK;
	}

	virtual void HandleErrorOccurred()
	{
	}

private:
	static const char* _PermissionString(char* buffer, uint32 mode, bool sticky)
	{
		buffer[0] = (mode & 0x4) != 0 ? 'r' : '-';
		buffer[1] = (mode & 0x2) != 0 ? 'w' : '-';

		if ((mode & 0x1) != 0)
			buffer[2] = sticky ? 's' : 'x';
		else
			buffer[2] = '-';

		buffer[3] = '\0';
		return buffer;
	}

private:
	int		fLevel;
	bool	fListAttribute;
};


int
command_list(int argc, const char* const* argv)
{
	bool listAttributes = false;

	while (true) {
		static struct option sLongOptions[] = {
			{ "help", no_argument, 0, 'h' },
			{ 0, 0, 0, 0 }
		};

		opterr = 0; // don't print errors
		int c = getopt_long(argc, (char**)argv, "+ha", sLongOptions, NULL);
		if (c == -1)
			break;

		switch (c) {
			case 'a':
				listAttributes = true;
				break;

			case 'h':
				print_usage_and_exit(false);
				break;

			default:
				print_usage_and_exit(true);
				break;
		}
	}

	// One argument should remain -- the package file name.
	if (optind + 1 != argc)
		print_usage_and_exit(true);

	const char* packageFileName = argv[optind++];

	// open package
	PackageReader packageReader;
	status_t error = packageReader.Init(packageFileName);
printf("Init(): %s\n", strerror(error));
	if (error != B_OK)
		return 1;

	// list
	PackageContentListHandler handler(listAttributes);
	error = packageReader.ParseContent(&handler);
printf("ParseContent(): %s\n", strerror(error));
	if (error != B_OK)
		return 1;

	return 0;
}
