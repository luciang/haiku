/* 
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef KERNEL_BOOT_KERNEL_ARGS_H
#define KERNEL_BOOT_KERNEL_ARGS_H


#include <SupportDefs.h>

#include <boot/elf.h>
#include <boot/disk_identifier.h>
#include <boot/driver_settings.h>

#include <platform_kernel_args.h>
#include <arch_kernel_args.h>

#include <util/KMessage.h>

#define CURRENT_KERNEL_ARGS_VERSION	1
#define MAX_KERNEL_ARGS_RANGE		32

// names of common boot_volume fields
#define BOOT_METHOD						"boot method"
#define BOOT_VOLUME_USER_SELECTED		"user selected"
#define BOOT_VOLUME_BOOTED_FROM_IMAGE	"booted from image"
#define BOOT_VOLUME_PARTITION_OFFSET	"partition offset"
#define BOOT_VOLUME_DISK_IDENTIFIER		"disk identifier"

// boot methods
enum {
	BOOT_METHOD_HARD_DISK	= 0,
	BOOT_METHOD_CD			= 1,
	BOOT_METHOD_NET			= 2,

	BOOT_METHOD_DEFAULT		= BOOT_METHOD_HARD_DISK
};

typedef struct kernel_args {
	uint32		kernel_args_size;
	uint32		version;

	struct preloaded_image kernel_image;
	struct preloaded_image *preloaded_images;

	uint32		num_physical_memory_ranges;
	addr_range	physical_memory_range[MAX_PHYSICAL_MEMORY_RANGE];
	uint32		num_physical_allocated_ranges;
	addr_range	physical_allocated_range[MAX_PHYSICAL_ALLOCATED_RANGE];
	uint32		num_virtual_allocated_ranges;
	addr_range	virtual_allocated_range[MAX_VIRTUAL_ALLOCATED_RANGE];
	uint32		num_kernel_args_ranges;
	addr_range	kernel_args_range[MAX_KERNEL_ARGS_RANGE];

	uint32		num_cpus;
	addr_range	cpu_kstack[MAX_BOOT_CPUS];

	KMessage	boot_volume;

	struct driver_settings_file *driver_settings;

	struct {
		bool	enabled;
		int32	width;
		int32	height;
		int32	depth;
		addr_range physical_buffer;
	} frame_buffer;

	void		*debug_output;
	uint32		debug_size;

	platform_kernel_args platform_args;
	arch_kernel_args arch_args;
} kernel_args;

#endif	/* KERNEL_BOOT_KERNEL_ARGS_H */
