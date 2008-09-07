/*
 * Copyright (c) 2008 Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 * Distributed under the terms of the MIT/X11 license.
 *
 * Copyright (c) 1999 Mike Steed. You are free to use and distribute this software
 * as long as it is accompanied by it's documentation and this copyright notice.
 * The software comes with no warranty, etc.
 */
#define ASSIGN_RESOURCES
#include "Common.h"

#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <FindDirectory.h>
#include <Roster.h>
#include <Path.h>


BResources*
read_resources(const char* appSignature)
{
	status_t ret = be_app->GetAppInfo(&kAppInfo);
	if (ret != B_OK) {
		fprintf(stderr, "Failed to init application info: %s\n",
			strerror(ret));
		exit(1);
	}
	
	BFile file(&kAppInfo.ref, O_RDONLY);
	ret = file.InitCheck();
	if (ret != B_OK) {
		fprintf(stderr, "Failed to init application file to read resources: "
			"%s\n", strerror(ret));
		exit(1);
	}

	BResources* r = new BResources(&file);

#define LoadString(n)	(char*)r->LoadResource(B_STRING_TYPE, n, &ignore)
#define LoadColor(n)	*(rgb_color*)r->LoadResource(B_RGB_COLOR_TYPE, n, &ignore)
#define LoadUint8(n)	*(uint8*)r->LoadResource(B_UINT8_TYPE, n, &ignore)

	size_t ignore;
	kVolMenuLabel = LoadString("STR_VM_LABEL");
	kOneFile = LoadString("STR_1_FILE");
	kManyFiles = LoadString("STR_N_FILES");
	kStrRescan = LoadString("STR_RESCAN");
	kStrScanningX = LoadString("STR_SCN_X");
	kStrUnavail = LoadString("STR_UNAVAIL");
	kVolMenuDefault = LoadString("STR_VM_DFLT");
	kVolPrompt = LoadString("STR_VPROMPT");
	kMenuGetInfo = LoadString("STR_M_INFO");
	kMenuOpen = LoadString("STR_M_OPEN");
	kMenuOpenWith = LoadString("STR_M_OPENW");
	kMenuNoApps = LoadString("STR_M_NAPPS");
	kInfoSize = LoadString("STR_SIZE");
	kInfoInFiles = LoadString("STR_INFILES");
	kInfoCreated = LoadString("STR_MADE");
	kInfoModified = LoadString("STR_MOD");
	kInfoTimeFmt = LoadString("STR_TIMEFMT");
	kInfoKind = LoadString("STR_KIND");
	kInfoPath = LoadString("STR_PATH");

	kWindowColor = ui_color(B_PANEL_BACKGROUND_COLOR);
	kOutlineColor = LoadColor("RGB_PIE_OL");
	kPieBGColor = LoadColor("RGB_PIE_BG");
	kEmptySpcColor = LoadColor("RGB_PIE_MT");

	kBasePieColorCount = LoadUint8("N_PIE_COLORS");
	kBasePieColor = new rgb_color[kBasePieColorCount];
	for (int i = 0; i < kBasePieColorCount; i++) {
		char colorName[16] = "RGB_PIE_n";
		colorName[8] = '1' + i;
		kBasePieColor[i] = LoadColor(colorName);
	}

	// Get a reference to the help file.
	BPath path;
	if (find_directory(B_BEOS_DOCUMENTATION_DIRECTORY, &path) == B_OK
		&& path.Append(kHelpFileName) == B_OK) {
		printf("help file =? %s\n", path.Path());
		BEntry entry(path.Path());
		kFoundHelpFile = entry.Exists();
		entry.GetRef(&kHelpFileRef);
	} else
		kFoundHelpFile = false;
		

	return r;
}


void
size_to_string(off_t byteCount, char* name)
{
	struct {
		off_t		limit;
		float		divisor;
		char*		format;
	} scale[] = {
		{ 0x100000,				1024.0,					"%.2f KB" },
		{ 0x40000000,			1048576.0,				"%.2f MB" },
		{ 0x10000000000ull,		1073741824.0,			"%.2f GB" },
		{ 0x4000000000000ull,	1.09951162778e+12,		"%.2f TB" }
	};

	if (byteCount < 1024) {
		sprintf(name, "%lld bytes", byteCount);
	} else {
		int i = 0;
		while (byteCount >= scale[i].limit)
			i++;

		sprintf(name, scale[i].format, byteCount / scale[i].divisor);
	}
}

