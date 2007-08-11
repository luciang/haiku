/*
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Manuel J. Petit. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef KERNEL_USER_RUNTIME_H_
#define KERNEL_USER_RUNTIME_H_


#include <image.h>
#include <OS.h>


#define MAGIC_APP_NAME	"_APP_"

struct user_space_program_args {
	char	program_name[B_OS_NAME_LENGTH];
	char	program_path[B_PATH_NAME_LENGTH];
	port_id	error_port;
	uint32	error_token;
	int		arg_count;
	int		env_count;
	char	**args;
	char	**env;
};

struct rld_export {
	// runtime linker API export
	image_id (*load_add_on)(char const *path, uint32 flags);
	status_t (*unload_add_on)(image_id imageID);
	status_t (*get_image_symbol)(image_id imageID, char const *symbolName,
		int32 symbolType, void **_location);
	status_t (*get_nth_image_symbol)(image_id imageID, int32 num, char *symbolName,
		int32 *nameLength, int32 *symbolType, void **_location);
	status_t (*test_executable)(const char *path, uid_t user, gid_t group,
		char *starter);
	status_t (*get_next_image_dependency)(image_id id, uint32 *cookie,
		const char **_name);

	status_t (*reinit_after_fork)();

	const struct user_space_program_args *program_args;
};

extern struct rld_export *__gRuntimeLoader;

#endif	/* KERNEL_USER_RUNTIME_H_ */
