/*
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _KERNEL_VM_VM_PAGE_H
#define _KERNEL_VM_VM_PAGE_H


#include <vm/vm.h>


struct kernel_args;

extern int32 gMappedPagesCount;

#ifdef __cplusplus
extern "C" {
#endif

void vm_page_init_num_pages(struct kernel_args *args);
status_t vm_page_init(struct kernel_args *args);
status_t vm_page_init_post_area(struct kernel_args *args);
status_t vm_page_init_post_thread(struct kernel_args *args);

status_t vm_mark_page_inuse(addr_t page);
status_t vm_mark_page_range_inuse(addr_t startPage, addr_t length);
void vm_page_free(struct VMCache *cache, struct vm_page *page);
status_t vm_page_set_state(struct vm_page *page, int state);
void vm_page_requeue(struct vm_page *page, bool tail);

// get some data about the number of pages in the system
size_t vm_page_num_pages(void);
size_t vm_page_num_free_pages(void);
size_t vm_page_num_available_pages(void);
size_t vm_page_num_unused_pages(void);
void vm_page_get_stats(system_info *info);

status_t vm_page_write_modified_page_range(struct VMCache *cache,
	uint32 firstPage, uint32 endPage);
status_t vm_page_write_modified_pages(struct VMCache *cache);
void vm_page_schedule_write_page(struct vm_page *page);
void vm_page_schedule_write_page_range(struct VMCache *cache,
	uint32 firstPage, uint32 endPage);

void vm_page_unreserve_pages(uint32 count);
void vm_page_reserve_pages(uint32 count);
bool vm_page_try_reserve_pages(uint32 count);

struct vm_page *vm_page_allocate_page(int pageState);
struct vm_page *vm_page_allocate_page_run(int state, addr_t base,
	addr_t length);
struct vm_page *vm_page_allocate_page_run_no_base(int state, addr_t count);
struct vm_page *vm_page_at_index(int32 index);
struct vm_page *vm_lookup_page(addr_t pageNumber);

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_VM_VM_PAGE_H */
