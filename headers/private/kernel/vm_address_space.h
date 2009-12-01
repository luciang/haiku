/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */
#ifndef _KERNEL_VM_ADDRESS_SPACE_H
#define _KERNEL_VM_ADDRESS_SPACE_H


#include <OS.h>


struct kernel_args;
struct VMAddressSpace;


#ifdef __cplusplus
extern "C" {
#endif

status_t vm_address_space_init(void);
status_t vm_address_space_init_post_sem(void);

void vm_delete_address_space(struct VMAddressSpace *aspace);
status_t vm_create_address_space(team_id id, addr_t base, addr_t size,
			bool kernel, struct VMAddressSpace **_aspace);
status_t vm_delete_areas(struct VMAddressSpace *aspace);
struct VMAddressSpace *vm_get_kernel_address_space(void);
struct VMAddressSpace *vm_kernel_address_space(void);
team_id vm_kernel_address_space_id(void);
struct VMAddressSpace *vm_get_current_user_address_space(void);
team_id vm_current_user_address_space_id(void);
struct VMAddressSpace *vm_get_address_space(team_id team);
void vm_put_address_space(struct VMAddressSpace *aspace);
#define vm_swap_address_space(from, to) arch_vm_aspace_swap(from, to)

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_VM_ADDRESS_SPACE_H */
