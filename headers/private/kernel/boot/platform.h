/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/
#ifndef KERNEL_BOOT_PLATFORM_H
#define KERNEL_BOOT_PLATFORM_H


#include <SupportDefs.h>
#include <boot/vfs.h>


struct stage2_args;

#ifdef __cplusplus
extern "C" {
#endif

/* debug functions */
extern void panic(const char *format, ...);
extern void dprintf(const char *format, ...);

/* heap functions */
extern void platform_release_heap(struct stage2_args *args, void *base);
extern status_t platform_init_heap(struct stage2_args *args, void **_base, void **_top);

/* MMU/memory functions */
extern status_t platform_allocate_region(void **_virtualAddress, size_t size, uint8 protection);
extern status_t platform_free_region(void *address, size_t size);

/* boot options */
#define BOOT_OPTION_MENU			1
#define BOOT_OPTION_DEBUG_OUTPUT	2

extern uint32 platform_boot_options(void);

/* misc functions */
extern void platform_switch_to_logo(void);
extern void platform_switch_to_text_mode(void);
extern void platform_start_kernel(void);

#ifdef __cplusplus
}

class Node;
namespace boot {
	class Partition;
}

/* device functions */

// these functions have to be implemented in C++
extern status_t platform_get_boot_device(struct stage2_args *args, Node **_device);
extern status_t platform_add_block_devices(struct stage2_args *args, NodeList *devicesList);
extern status_t platform_get_boot_partition(struct stage2_args *args, Node *bootDevice,
					NodeList *partitions, boot::Partition **_partition);

#endif

#endif	/* KERNEL_BOOT_PLATFORM_H */
