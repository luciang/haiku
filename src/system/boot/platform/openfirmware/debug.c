/*
 * Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <boot/platform.h>
#include <boot/stdio.h>
#include <platform/openfirmware/openfirmware.h>
#include <stdarg.h>


/** ToDo: this works only after console_init() was called.
 *		But it probably should do something before as well...
 */

void
panic(const char *format, ...)
{
	va_list list;

	puts("*** PANIC ***");

	va_start(list, format);
	vprintf(format, list);
	va_end(list);

	of_exit();
}


void
dprintf(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	vprintf(format, list);
	va_end(list);
}


void
dprintf_no_syslog(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	vprintf(format, list);
	va_end(list);
}

