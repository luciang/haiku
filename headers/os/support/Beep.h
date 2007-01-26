/*
 * Copyright 2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BEEP_H
#define _BEEP_H


#include <BeBuild.h>
#include <SupportDefs.h>


#ifdef __cplusplus
// These functions are only exported for C++

status_t beep();
status_t system_beep(const char* eventName);
status_t add_system_beep_event(const char* eventName, uint32 flags = 0);

#endif

#endif	// _BEEP_H
