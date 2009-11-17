/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _KERNEL_ARCH_X86_PAGING_H
#define _KERNEL_ARCH_X86_PAGING_H


#include <SupportDefs.h>

#include <heap.h>
#include <int.h>


#define PAGE_INVALIDATE_CACHE_SIZE 64

#define ADDR_SHIFT(x) ((x) >> 12)
#define ADDR_REVERSE_SHIFT(x) ((x) << 12)

#define VADDR_TO_PDENT(va) (((va) / B_PAGE_SIZE) / 1024)
#define VADDR_TO_PTENT(va) (((va) / B_PAGE_SIZE) % 1024)


class TranslationMapPhysicalPageMapper;


typedef struct page_table_entry {
	uint32	present:1;
	uint32	rw:1;
	uint32	user:1;
	uint32	write_through:1;
	uint32	cache_disabled:1;
	uint32	accessed:1;
	uint32	dirty:1;
	uint32	reserved:1;
	uint32	global:1;
	uint32	avail:3;
	uint32	addr:20;
} page_table_entry;

typedef struct page_directory_entry {
	uint32	present:1;
	uint32	rw:1;
	uint32	user:1;
	uint32	write_through:1;
	uint32	cache_disabled:1;
	uint32	accessed:1;
	uint32	reserved:1;
	uint32	page_size:1;
	uint32	global:1;
	uint32	avail:3;
	uint32	addr:20;
} page_directory_entry;


struct vm_translation_map_arch_info : DeferredDeletable {
	page_directory_entry*		pgdir_virt;
	page_directory_entry*		pgdir_phys;
	TranslationMapPhysicalPageMapper* page_mapper;
	vint32						ref_count;
	vint32						active_on_cpus;
		// mask indicating on which CPUs the map is currently used
	int							num_invalidate_pages;
	addr_t						pages_to_invalidate[PAGE_INVALIDATE_CACHE_SIZE];

								vm_translation_map_arch_info();
	virtual						~vm_translation_map_arch_info();

	inline	void				AddReference();
	inline	void				RemoveReference();

			void				Delete();
};


void x86_early_prepare_page_tables(page_table_entry* pageTables, addr_t address,
		size_t size);
void x86_put_pgtable_in_pgdir(page_directory_entry* entry,
	addr_t physicalPageTable, uint32 attributes);
void x86_update_all_pgdirs(int index, page_directory_entry entry);


static inline void
init_page_directory_entry(page_directory_entry *entry)
{
	*(uint32 *)entry = 0;
}


static inline void
update_page_directory_entry(page_directory_entry *entry, page_directory_entry *with)
{
	// update page directory entry atomically
	*(uint32 *)entry = *(uint32 *)with;
}


static inline void
init_page_table_entry(page_table_entry *entry)
{
	*(uint32 *)entry = 0;
}


static inline void
update_page_table_entry(page_table_entry *entry, page_table_entry *with)
{
	// update page table entry atomically
	*(uint32 *)entry = *(uint32 *)with;
}


inline void
vm_translation_map_arch_info::AddReference()
{
	atomic_add(&ref_count, 1);
}


inline void
vm_translation_map_arch_info::RemoveReference()
{
	if (atomic_add(&ref_count, -1) == 1)
		Delete();
}


#endif	// _KERNEL_ARCH_X86_PAGING_H
