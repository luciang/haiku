/*
** Copyright 2002, Manuel J. Petit. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#ifndef RUN_TIME_LINKER_H
#define RUN_TIME_LINKER_H


#include <user_runtime.h>


int RLD_STARTUP(void *);
int rldmain(void *arg);

status_t unload_program(image_id imageID);
image_id load_program(char const *path, void **entry);
status_t unload_library(image_id imageID);
image_id load_library(char const *path, uint32 flags);
status_t unload_addon(image_id imageID);
image_id load_addon(char const *path);
status_t get_nth_symbol(image_id imageID, int32 num, char *nameBuffer, int32 *_nameLength,
	int32 *_type, void **_location);
status_t get_symbol(image_id imageID, char const *symbolName, int32 symbolType,
	void **_location);

void rldelf_init(struct uspace_program_args const *uspa);
void rldexport_init(struct uspace_program_args *uspa);

// RLD heap
void  rldheap_init(void);
void *rldalloc(size_t);
void  rldfree(void *p);

#endif	/* RUN_TIME_LINKER_H */
