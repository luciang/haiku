/*
** Copyright 2002, Manuel J. Petit. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#ifndef KERNEL_USER_RUNTIME_H_
#define KERNEL_USER_RUNTIME_H_

#include <image.h>
#include <defines.h>

#define MAGIC_APP_NAME	"_APP_"

struct rld_export
{
	// runtime linker API export
	image_id (*load_add_on)(char const *path, uint32 flags);
	status_t (*unload_add_on)(image_id imageID);
	status_t (*get_image_symbol)(image_id imageID, char const *symbolName,
		int32 symbolType, void **_location);
	status_t (*get_nth_image_symbol)(image_id imageID, int32 num, char *symbolName,
		int32 *nameLength, int32 *symbolType, void **_location);
};

struct uspace_program_args
{
	char program_name[SYS_MAX_OS_NAME_LEN];
	char program_path[SYS_MAX_PATH_LEN];
	int  argc;
	int  envc;
	char **argv;
	char **envp;

	/*
	 * hooks into rld for POSIX and BeOS library/module loading
	 */
	struct rld_export *rld_export;
};

typedef void (libinit_f)(unsigned, struct uspace_program_args const *);

//void INIT_BEFORE_CTORS(unsigned, struct uspace_prog_args const *);
//void INIT_AFTER_CTORS(unsigned, struct uspace_prog_args const *);

#endif	/* KERNEL_USER_RUNTIME_H_ */
