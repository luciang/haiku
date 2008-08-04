/*
 * Copyright 2008, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Fredrik Modeen 
 *
 */
#include "JoystickTweaker.h"
#include "Joystick.h"

#include <Font.h>

#include <stdio.h>
#include <stdlib.h>

#define STRINGLENGTHCPY 64

#include <UTF8.h>

#ifdef DEBUG
        inline void LOG(const char *fmt, ...) { char buf[1024]; va_list ap; va_start(ap, fmt); vsprintf(buf, fmt, ap); va_end(ap); \
                fputs(buf, _BJoystickTweaker::sLogFile); fflush(_BJoystickTweaker::sLogFile); }
        #define LOG_ERR(text...) LOG(text)
FILE *_BJoystickTweaker::sLogFile = NULL;
#else
        #define LOG(text...) 
        #define LOG_ERR(text...) fprintf(stderr, text)
#endif

#define CALLED() LOG("%s\n", __PRETTY_FUNCTION__)

_BJoystickTweaker::_BJoystickTweaker()
{
	CALLED();
#ifdef DEBUG
	sLogFile = fopen("/var/log/libdevice.log", "a");
#endif		
}


_BJoystickTweaker::_BJoystickTweaker(BJoystick &stick)
{
	CALLED();
#ifdef DEBUG
	sLogFile = fopen("/var/log/libdevice.log", "a");
#endif		
	
	fJoystick = &stick;
}


_BJoystickTweaker::~_BJoystickTweaker()
{
}


status_t
_BJoystickTweaker::save_config(const entry_ref *ref)
{
	CALLED();
	return B_ERROR;
}


status_t
_BJoystickTweaker::scan_including_disabled(const char* rootPath, BList *list,
						BEntry *rootEntry)
{
	BDirectory root;

	if (rootEntry != NULL)
		root.SetTo( rootEntry);
	else if (rootPath != NULL)
		root.SetTo(rootPath);
	else
		return B_ERROR;
		
	BEntry entry;
	
	ASSERT(list != NULL);
	while ((root.GetNextEntry(&entry)) > B_ERROR ) {
		if (entry.IsDirectory()) {
			scan_including_disabled(rootPath, list, &entry);
		} else {
			BPath path;
			entry.GetPath(&path);
			
			BString *str = new BString(path.Path());
			str->RemoveFirst(rootPath);
			list->AddItem(str);
		}
	}
	return B_OK;
}


void 
_BJoystickTweaker::scan_including_disabled()
{
	CALLED();
	// First, we empty the list
	for (int32 count = fJoystick->_fDevices->CountItems() - 1; count >= 0; count--)
		free(fJoystick->_fDevices->RemoveItem(count));

	scan_including_disabled(DEVICEPATH, fJoystick->_fDevices);
}


status_t
_BJoystickTweaker::get_info()
{
	CALLED();
	return B_ERROR;
}


status_t
_BJoystickTweaker::get_info(_joystick_info* info, 
	const char * ref)
{
	CALLED();
	status_t err = B_ERROR;
	BString str(JOYSTICKPATH);
	str.Append(ref);
	
	FILE *file = fopen(str.String(), "r");
	if (file != NULL) {
		char line [STRINGLENGTHCPY];
		while (fgets ( line, sizeof line, file ) != NULL ) {
			int len = strlen(line);
    		if (len > 0 && line[len-1] == '\n')
        		line[len-1] = '\0';
			BuildFromJoystickDesc(line, info);
		}
		fclose(file);
	}
	
	err = B_OK;
	return err;
}


void 
_BJoystickTweaker::BuildFromJoystickDesc(char *string, _joystick_info* info)
{
	BString str(string);
	str.RemoveAll("\"");
	
	if (str.IFindFirst("module") != -1) {
		str.RemoveFirst("module = ");	
		strncpy(info->module_name, str.String(), STRINGLENGTHCPY);
	} else if (str.IFindFirst("gadget") != -1) {
		str.RemoveFirst("gadget = ");
		strncpy(info->controller_name, str.String(), STRINGLENGTHCPY);
	} else if (str.IFindFirst("num_axes") != -1) {
		str.RemoveFirst("num_axes = ");
		//info->num_axes = atoi(str.String());
		//LOG("%s\n", str.String());
	} else if (str.IFindFirst("num_hats") != -1) {
		str.RemoveFirst("num_hats = ");
		//info->num_hats = atoi(str.String());
		//LOG("%s\n", str.String());
	} else if (str.IFindFirst("num_buttons") != -1) {
		str.RemoveFirst("num_buttons = ");
		//info->num_buttons = atoi(str.String());
		//LOG("%s\n", str.String());
	} else if (str.IFindFirst("num_sticks") != -1) {
		str.RemoveFirst("num_sticks = ");
		//info->num_sticks = atoi(str.String());
		//LOG("%s\n", str.String());
	} else {
	//	LOG("Path = %s\n", str->String());
	}
}


status_t
_BJoystickTweaker::SendIOCT(uint32 op)
{
	status_t err = B_ERROR;
	switch (op) {
	case B_JOYSTICK_SET_DEVICE_MODULE:
		break;

	case B_JOYSTICK_GET_DEVICE_MODULE:
		break;

	case B_JOYSTICK_GET_SPEED_COMPENSATION:
	case B_JOYSTICK_SET_SPEED_COMPENSATION:
	case B_JOYSTICK_GET_MAX_LATENCY:
	case B_JOYSTICK_SET_MAX_LATENCY:
	case B_JOYSTICK_SET_RAW_MODE:
	default:
		break;
	}
	return err;
}
