/*
 * Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "keyboard.h"
#include "toscalls.h"

#include <boot/platform.h>
#include <boot/stdio.h>
#include <stdarg.h>

#include <Errors.h>

/*!	This works only after console_init() was called.
*/
void
panic(const char *format, ...)
{
	const char greetings[] = "\n*** PANIC ***";
	char buffer[512];
	va_list list;
	int length;

	//platform_switch_to_text_mode();

	Bconputs(DEV_CONSOLE, greetings);
	// send to the emulator's stdout if available
	nat_feat_debugprintf(greetings);
	nat_feat_debugprintf("\n");

	va_start(list, format);
	length = vsnprintf(buffer, sizeof(buffer), format, list);
	va_end(list);

	Bconputs(DEV_CONSOLE, buffer);
	// send to the emulator's stdout if available
	nat_feat_debugprintf(buffer);

	Bconputs(DEV_CONSOLE, "\nPress key to reboot.");

	clear_key_buffer();
	wait_for_key();
	platform_exit();
}


void
dprintf(const char *format, ...)
{
	char buffer[512];
	va_list list;
	int length;

	va_start(list, format);
	length = vsnprintf(buffer, sizeof(buffer), format, list);
	va_end(list);

	Bconput(DEV_AUX, buffer);

	// send to the emulator's stdout if available
	nat_feat_debugprintf(buffer);

	//if (platform_boot_options() & BOOT_OPTION_DEBUG_OUTPUT)
		Bconput(DEV_CONSOLE, buffer);
}


