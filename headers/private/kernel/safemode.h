/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_SAFEMODE_H
#define _KERNEL_SAFEMODE_H


#include <driver_settings.h>


// these are BeOS compatible additions to the safemode
// constants in the driver_settings.h header

#define B_SAFEMODE_DISABLE_USER_ADD_ONS		"disableuseraddons"
#define B_SAFEMODE_DISABLE_IDE_DMA			"disableidedma"
#define B_SAFEMODE_DISABLE_ACPI				"disable_acpi"
#define B_SAFEMODE_DISABLE_SMP				"disable_smp"
#define B_SAFEMODE_DISABLE_HYPER_THREADING	"disable_hyperthreading"
#define B_SAFEMODE_FAIL_SAFE_VIDEO_MODE		"fail_safe_video_mode"


#ifdef __cplusplus
extern "C" {
#endif

status_t get_safemode_option(const char *parameter, char *buffer, size_t *_bufferSize);
bool get_safemode_boolean(const char *parameter, bool defaultValue);
status_t _user_get_safemode_option(const char *parameter, char *buffer, size_t *_bufferSize);

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_SAFEMODE_H */
