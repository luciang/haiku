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
struct vm_address_space;


#ifdef __cplusplus
extern "C" {
#endif

status_t vm_address_space_init(void);
status_t vm_address_space_init_post_sem(void);

void vm_delete_address_space(struct vm_address_space *aspace);
status_t vm_create_address_space(team_id id, addr_t base, addr_t size,
			bool kernel, struct vm_address_space **_aspace);
status_t vm_delete_areas(struct vm_address_space *aspace);
struct vm_address_space *vm_get_kernel_address_space(void);
struct vm_address_space *vm_kernel_address_space(void);
team_id vm_kernel_address_space_id(void);
struct vm_address_space *vm_get_current_user_address_space(void);
team_id vm_current_user_address_space_id(void);
struct vm_address_space *vm_get_address_space(team_id team);
void vm_put_address_space(struct vm_address_space *aspace);
#define vm_swap_address_space(aspace) arch_vm_aspace_swap(aspace)

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_VM_ADDRESS_SPACE_H */
