/*
** Copyright 2002-2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the Haiku License.
**
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/


#include <OS.h>
#include <KernelExport.h>

#include <kerrors.h>
#include <vm.h>
#include <vm_priv.h>
#include <vm_page.h>
#include <vm_cache.h>
#include <vm_store_anonymous_noswap.h>
#include <vm_store_device.h>
#include <vm_store_null.h>
#include <file_cache.h>
#include <memheap.h>
#include <debug.h>
#include <console.h>
#include <int.h>
#include <smp.h>
#include <lock.h>
#include <khash.h>
#include <thread.h>
#include <team.h>

#include <boot/stage2.h>
#include <boot/elf.h>

#include <arch/cpu.h>
#include <arch/vm.h>

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

//#define TRACE_VM
#ifdef TRACE_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

vm_address_space *kernel_aspace;

#define ASPACE_HASH_TABLE_SIZE 1024
static void *aspace_table;
static sem_id aspace_hash_sem;


static void
_dump_aspace(vm_address_space *aspace)
{
	vm_area *area;

	dprintf("dump of address space at %p:\n", aspace);
	dprintf("name: '%s'\n", aspace->name);
	dprintf("id: 0x%lx\n", aspace->id);
	dprintf("ref_count: %ld\n", aspace->ref_count);
	dprintf("fault_count: %ld\n", aspace->fault_count);
	dprintf("working_set_size: 0x%lx\n", aspace->working_set_size);
	dprintf("translation_map: %p\n", &aspace->translation_map);
	dprintf("virtual_map.base: 0x%lx\n", aspace->virtual_map.base);
	dprintf("virtual_map.size: 0x%lx\n", aspace->virtual_map.size);
	dprintf("virtual_map.change_count: 0x%x\n", aspace->virtual_map.change_count);
	dprintf("virtual_map.sem: 0x%lx\n", aspace->virtual_map.sem);
	dprintf("virtual_map.region_hint: %p\n", aspace->virtual_map.area_hint);
	dprintf("virtual_map.region_list:\n");
	for (area = aspace->virtual_map.areas; area != NULL; area = area->aspace_next) {
		dprintf(" region 0x%lx: ", area->id);
		dprintf("base_addr = 0x%lx ", area->base);
		dprintf("size = 0x%lx ", area->size);
		dprintf("name = '%s' ", area->name);
		dprintf("protection = 0x%lx\n", area->protection);
	}
}


static int
dump_aspace(int argc, char **argv)
{
	vm_address_space *aspace;

	if (argc < 2) {
		dprintf("aspace: not enough arguments\n");
		return 0;
	}

	// if the argument looks like a number, treat it as such
	if (isdigit(argv[1][0])) {
		unsigned long num = strtoul(argv[1], NULL, 0);
		aspace_id id = num;

		aspace = hash_lookup(aspace_table, &id);
		if (aspace == NULL) {
			dprintf("invalid aspace id\n");
		} else {
			_dump_aspace(aspace);
		}
		return 0;
	} else {
		// walk through the aspace list, looking for the arguments as a name
		struct hash_iterator iter;

		hash_open(aspace_table, &iter);
		while ((aspace = hash_next(aspace_table, &iter)) != NULL) {
			if(aspace->name != NULL && strcmp(argv[1], aspace->name) == 0) {
				_dump_aspace(aspace);
			}
		}
	}
	return 0;
}


static int
dump_aspace_list(int argc, char **argv)
{
	vm_address_space *as;
	struct hash_iterator iter;

	dprintf("addr\tid\t%32s\tbase\t\tsize\n", "name");

	hash_open(aspace_table, &iter);
	while ((as = hash_next(aspace_table, &iter)) != NULL) {
		dprintf("%p\t0x%lx\t%32s\t0x%lx\t\t0x%lx\n",
			as, as->id, as->name, as->virtual_map.base, as->virtual_map.size);
	}
	hash_close(aspace_table, &iter, false);
	return 0;
}


static int
aspace_compare(void *_a, const void *key)
{
	vm_address_space *aspace = _a;
	const aspace_id *id = key;

	if (aspace->id == *id)
		return 0;

	return -1;
}


static uint32
aspace_hash(void *_a, const void *key, uint32 range)
{
	vm_address_space *aspace = _a;
	const aspace_id *id = key;

	if (aspace != NULL)
		return aspace->id % range;

	return *id % range;
}


/** When this function is called, all references to this address space
 *	have been released, so it's safe to remove it.
 */

static void
delete_address_space(vm_address_space *aspace)
{
	TRACE(("delete_address_space: called on aspace 0x%lx\n", aspace->id));

	if (aspace == kernel_aspace)
		panic("tried to delete the kernel aspace!\n");

	// put this aspace in the deletion state
	// this guarantees that no one else will add regions to the list
	acquire_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0, 0);

	aspace->state = VM_ASPACE_STATE_DELETION;

	(*aspace->translation_map.ops->destroy)(&aspace->translation_map);

	free(aspace->name);
	delete_sem(aspace->virtual_map.sem);
	free(aspace);
}


//	#pragma mark -


vm_address_space *
vm_get_aspace_by_id(aspace_id aid)
{
	vm_address_space *aspace;

	acquire_sem_etc(aspace_hash_sem, READ_COUNT, 0, 0);
	aspace = hash_lookup(aspace_table, &aid);
	if (aspace)
		atomic_add(&aspace->ref_count, 1);
	release_sem_etc(aspace_hash_sem, READ_COUNT, 0);

	return aspace;
}


vm_address_space *
vm_get_kernel_aspace(void)
{
	/* we can treat this one a little differently since it can't be deleted */
	acquire_sem_etc(aspace_hash_sem, READ_COUNT, 0, 0);
	atomic_add(&kernel_aspace->ref_count, 1);
	release_sem_etc(aspace_hash_sem, READ_COUNT, 0);
	return kernel_aspace;
}


aspace_id
vm_get_kernel_aspace_id(void)
{
	return kernel_aspace->id;
}


vm_address_space *
vm_get_current_user_aspace(void)
{
	return vm_get_aspace_by_id(vm_get_current_user_aspace_id());
}


aspace_id
vm_get_current_user_aspace_id(void)
{
	struct thread *thread = thread_get_current_thread();

	if (thread != NULL && thread->team->aspace != NULL)
		return thread->team->aspace->id;

	return B_ERROR;
}


void
vm_put_aspace(vm_address_space *aspace)
{
	bool remove = false;

	acquire_sem_etc(aspace_hash_sem, WRITE_COUNT, 0, 0);
	if (atomic_add(&aspace->ref_count, -1) == 1) {
		hash_remove(aspace_table, aspace);
		remove = true;
	}
	release_sem_etc(aspace_hash_sem, WRITE_COUNT, 0);

	if (remove)
		delete_address_space(aspace);
}


/** Deletes all areas in the specified address space, and the address
 *	space by decreasing all reference counters. It also marks the
 *	address space of being in deletion state, so that no more areas
 *	can be created in it.
 *	After this, the address space is not operational anymore, but might
 *	still be in memory until the last reference has been released.
 */

void
vm_delete_aspace(vm_address_space *aspace)
{
	acquire_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0, 0);
	aspace->state = VM_ASPACE_STATE_DELETION;
	release_sem_etc(aspace->virtual_map.sem, WRITE_COUNT, 0);

	vm_delete_areas(aspace);
	vm_put_aspace(aspace);
}


status_t
vm_create_aspace(const char *name, team_id id, addr_t base, addr_t size, bool kernel, vm_address_space **_aspace)
{
	vm_address_space *aspace;
	status_t status;

	aspace = (vm_address_space *)malloc(sizeof(vm_address_space));
	if (aspace == NULL)
		return B_NO_MEMORY;

	TRACE(("vm_create_aspace: %s: %lx bytes starting at 0x%lx => %p\n", name, size, base, aspace));

	aspace->name = (char *)malloc(strlen(name) + 1);
	if (aspace->name == NULL ) {
		free(aspace);
		return B_NO_MEMORY;
	}
	strcpy(aspace->name, name);

	aspace->id = id;
	aspace->ref_count = 1;
	aspace->state = VM_ASPACE_STATE_NORMAL;
	aspace->fault_count = 0;
	aspace->scan_va = base;
	aspace->working_set_size = kernel ? DEFAULT_KERNEL_WORKING_SET : DEFAULT_WORKING_SET;
	aspace->max_working_set = DEFAULT_MAX_WORKING_SET;
	aspace->min_working_set = DEFAULT_MIN_WORKING_SET;
	aspace->last_working_set_adjust = system_time();

	// initialize the corresponding translation map
	status = arch_vm_translation_map_init_map(&aspace->translation_map, kernel);
	if (status < B_OK) {
		free(aspace->name);
		free(aspace);
		return status;
	}

	// initialize the virtual map
	aspace->virtual_map.base = base;
	aspace->virtual_map.size = size;
	aspace->virtual_map.areas = NULL;
	aspace->virtual_map.area_hint = NULL;
	aspace->virtual_map.change_count = 0;
	aspace->virtual_map.sem = create_sem(WRITE_COUNT, "aspacelock");
	aspace->virtual_map.aspace = aspace;

	// add the aspace to the global hash table
	acquire_sem_etc(aspace_hash_sem, WRITE_COUNT, 0, 0);
	hash_insert(aspace_table, aspace);
	release_sem_etc(aspace_hash_sem, WRITE_COUNT, 0);

	*_aspace = aspace;
	return B_OK;
}


int
vm_aspace_walk_start(struct hash_iterator *i)
{
	hash_open(aspace_table, i);
	return 0;
}


vm_address_space *
vm_aspace_walk_next(struct hash_iterator *i)
{
	vm_address_space *aspace;

	acquire_sem_etc(aspace_hash_sem, READ_COUNT, 0, 0);
	aspace = hash_next(aspace_table, i);
	if (aspace)
		atomic_add(&aspace->ref_count, 1);
	release_sem_etc(aspace_hash_sem, READ_COUNT, 0);
	return aspace;
}


status_t
vm_aspace_init(void)
{
	aspace_hash_sem = -1;

	// create the area and address space hash tables
	{
		vm_address_space *aspace;
		aspace_table = hash_init(ASPACE_HASH_TABLE_SIZE, (addr_t)&aspace->hash_next - (addr_t)aspace,
			&aspace_compare, &aspace_hash);
		if (aspace_table == NULL)
			panic("vm_init: error creating aspace hash table\n");
	}

	kernel_aspace = NULL;

	// create the initial kernel address space
	if (vm_create_aspace("kernel_land", 1, KERNEL_BASE, KERNEL_SIZE, true, &kernel_aspace) != B_OK)
		panic("vm_init: error creating kernel address space!\n");

	add_debugger_command("aspaces", &dump_aspace_list, "Dump a list of all address spaces");
	add_debugger_command("aspace", &dump_aspace, "Dump info about a particular address space");

	return B_OK;
}


status_t
vm_aspace_init_post_sem(void)
{
	status_t status = arch_vm_translation_map_init_kernel_map_post_sem(&kernel_aspace->translation_map);
	if (status < B_OK)
		return status;

	status = kernel_aspace->virtual_map.sem = create_sem(WRITE_COUNT, "kernel_aspacelock");
	if (status < B_OK)
		return status;

	status = aspace_hash_sem = create_sem(WRITE_COUNT, "aspace_hash_sem");
	if (status < B_OK)
		return status;

	return B_OK;
}
