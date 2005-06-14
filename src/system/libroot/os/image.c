/*
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <libroot_private.h>
#include <user_runtime.h>
#include <syscalls.h>

#include <OS.h>
#include <image.h>

#include <stdlib.h>


static struct rld_export const *sRuntimeLinker;


thread_id
load_image(int32 argCount, const char **args, const char **environ)
{
	char starter[B_FILE_NAME_LENGTH];
	const char **newArgs = NULL;
	int32 envCount = 0;
	thread_id thread;

	if (argCount < 1 || environ == NULL)
		return B_BAD_VALUE;

	// test validity of executable + support for scripts
	{
		status_t status = __test_executable(args[0], starter);
		if (status < B_OK)
			return status;

		if (starter[0]) {
			int32 i;

			// this is a shell script and requires special treatment
			newArgs = malloc((argCount + 2) * sizeof(void *));
			if (newArgs == NULL)
				return B_NO_MEMORY;

			// copy args and have "starter" as new app
			newArgs[0] = starter;
			for (i = 0; i < argCount; i++)
				newArgs[i + 1] = args[i];
			newArgs[i + 1] = NULL;

			args = newArgs;
			argCount++;
		}
	}

	// count environment variables
	while (environ[envCount] != NULL)
		envCount++;

	thread = _kern_load_image(argCount, args, envCount, environ,
		B_NORMAL_PRIORITY, B_WAIT_TILL_LOADED);

	free(newArgs);
	return thread;
}


image_id
load_add_on(char const *name)
{
	return sRuntimeLinker->load_add_on(name, 0);
}


status_t
unload_add_on(image_id id)
{
	return sRuntimeLinker->unload_add_on(id);
}


status_t
get_image_symbol(image_id id, char const *symbolName, int32 symbolType, void **_location)
{
	return sRuntimeLinker->get_image_symbol(id, symbolName, symbolType, _location);
}


status_t
get_nth_image_symbol(image_id id, int32 num, char *nameBuffer, int32 *_nameLength,
	int32 *_symbolType, void **_location)
{
	return sRuntimeLinker->get_nth_image_symbol(id, num, nameBuffer, _nameLength, _symbolType, _location);
}


status_t
_get_image_info(image_id id, image_info *info, size_t infoSize)
{
	return _kern_get_image_info(id, info, infoSize);
}


status_t
_get_next_image_info(team_id team, int32 *cookie, image_info *info, size_t infoSize)
{
	return _kern_get_next_image_info(team, cookie, info, infoSize);
}


void
clear_caches(void *address, size_t length, uint32 flags)
{
	_kern_clear_caches(address, length, flags);
}


//	#pragma mark -


status_t
__test_executable(const char *path, char *starter)
{
	return sRuntimeLinker->test_executable(path, geteuid(), getegid(), starter);
}


void
__init_image(const struct uspace_program_args *args)
{
	sRuntimeLinker = args->rld_export;
}


void _call_init_routines_(void);
void
_call_init_routines_(void)
{
	// This is called by the original BeOS startup code.
	// We don't need it, because our loader already does the job, right?
}

