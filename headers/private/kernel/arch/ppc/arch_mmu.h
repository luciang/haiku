/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/
#ifndef _KERNEL_ARCH_PPC_MMU_H
#define _KERNEL_ARCH_PPC_MMU_H


#include <SupportDefs.h>


enum bat_length {
	BAT_LENGTH_128kB	= 0x0000,
	BAT_LENGTH_256kB	= 0x0001,
	BAT_LENGTH_512kB	= 0x0003,
	BAT_LENGTH_1MB		= 0x0007,
	BAT_LENGTH_2MB		= 0x000f,
	BAT_LENGTH_4MB		= 0x001f,
	BAT_LENGTH_8MB		= 0x003f,
	BAT_LENGTH_16MB		= 0x007f,
	BAT_LENGTH_32MB		= 0x00ff,
	BAT_LENGTH_64MB		= 0x01ff,
	BAT_LENGTH_128MB	= 0x03ff,
	BAT_LENGTH_256MB	= 0x07ff,
};

enum bat_protection {
	BAT_READ_ONLY = 1,
	BAT_READ_WRITE = 2,
};

struct block_address_translation {
	// upper 32 bit
	uint32	page_index : 15;				// BEPI, block effective page index
	uint32	_reserved0 : 3;
	uint32	length : 10;
	uint32	kernel_valid : 1;
	uint32	user_valid : 1;
	// lower 32 bit
	uint32	write_through : 1;				// WIMG
	uint32	caching_inhibited : 1;
	uint32	memory_coherent : 1;
	uint32	guarded : 1;
	uint32	_reserved1 : 1;
	uint32	protection : 2;

	void Reset()
	{
		*((uint32 *)this) = 0;
	}
};

struct segment_descriptor {
	uint32	type : 1;						// 0 for page translation descriptors
	uint32	kernel_protection_key : 1;		// Ks, Supervisor-state protection key
	uint32	user_protection_key : 1;		// Kp, User-state protection key
	uint32	no_execute_protection : 1;
	uint32	_reserved : 4;
	uint32	virtual_segment_id : 24;
};

struct page_table_entry {
	// upper 32 bit
	uint32	valid : 1;
	uint32	virtual_segment_id : 24;
	uint32	hash : 1;
	uint32	abbr_page_index : 6;
	// lower 32 bit
	uint32	physical_page_number : 20;
	uint32	_reserved0 : 3;
	uint32	referenced : 1;
	uint32	changed : 1;
	uint32	write_through : 1;				// WIMG
	uint32	caching_inhibited : 1;
	uint32	memory_coherent : 1;
	uint32	guarded : 1;
	uint32	_reserved1 : 1;
	uint32	page_protection : 2;

	static uint32 PrimaryHash(uint32 virtualSegmentID, uint32 virtualAddress);
	static uint32 SecondaryHash(uint32 virtualSegmentID, uint32 virtualAddress);
};

struct page_table_entry_group {
	struct page_table_entry entry[8];
};

extern void ppc_get_page_table(void **_pageTable, size_t *_size);
extern void ppc_set_page_table(void *pageTable, size_t size);

#endif	/* _KERNEL_ARCH_PPC_MMU_H */
