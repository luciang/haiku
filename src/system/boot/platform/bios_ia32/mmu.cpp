/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Based on code written by Travis Geiselbrecht for NewOS.
 *
 * Distributed under the terms of the MIT License.
 */


#include "mmu.h"
#include "bios.h"

#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/stage2.h>
#include <arch/cpu.h>
#include <arch_kernel.h>
#include <kernel.h>

#include <OS.h>

#include <string.h>


/*!	The (physical) memory layout of the boot loader is currently as follows:
	  0x0500 - 0x10000	protected mode stack
	  0x0500 - 0x09000	real mode stack
	 0x10000 - ?		code (up to ~500 kB)
	 0x90000			1st temporary page table (identity maps 0-4 MB)
	 0x91000			2nd (4-8 MB)
	 0x92000 - 0x92000	further page tables
	 0x9e000 - 0xa0000	SMP trampoline code
	[0xa0000 - 0x100000	BIOS/ROM/reserved area]
	0x100000			page directory
	     ...			boot loader heap (32 kB)
	     ...			free physical memory

	The first 8 MB are identity mapped (0x0 - 0x0800000); paging is turned
	on. The kernel is mapped at 0x80000000, all other stuff mapped by the
	loader (kernel args, modules, driver settings, ...) comes after
	0x80020000 which means that there is currently only 2 MB reserved for
	the kernel itself (see kMaxKernelSize).

	The layout in PXE mode differs a bit from this, see definitions below.
*/

//#define TRACE_MMU
#ifdef TRACE_MMU
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


//#define TRACE_MEMORY_MAP
	// Define this to print the memory map to serial debug,
	// You also need to define ENABLE_SERIAL in serial.cpp
	// for output to work.


struct gdt_idt_descr {
	uint16 limit;
	uint32 *base;
} _PACKED;

// memory structure returned by int 0x15, ax 0xe820
struct extended_memory {
	uint64 base_addr;
	uint64 length;
	uint32 type;
};


static const uint32 kDefaultPageTableFlags = 0x07;	// present, user, R/W
static const size_t kMaxKernelSize = 0x200000;		// 2 MB for the kernel

// working page directory and page table
static uint32 *sPageDirectory = 0;

#ifdef _PXE_ENV

static addr_t sNextPhysicalAddress = 0x112000;
static addr_t sNextVirtualAddress = KERNEL_BASE + kMaxKernelSize;
static addr_t sMaxVirtualAddress = KERNEL_BASE + 0x400000;

static addr_t sNextPageTableAddress = 0x7d000;
static const uint32 kPageTableRegionEnd = 0x8b000;
	// we need to reserve 2 pages for the SMP trampoline code

#else

static addr_t sNextPhysicalAddress = 0x100000;
static addr_t sNextVirtualAddress = KERNEL_BASE + kMaxKernelSize;
static addr_t sMaxVirtualAddress = KERNEL_BASE + 0x400000;

static addr_t sNextPageTableAddress = 0x90000;
static const uint32 kPageTableRegionEnd = 0x9e000;
	// we need to reserve 2 pages for the SMP trampoline code

#endif


static addr_t
get_next_virtual_address(size_t size)
{
	addr_t address = sNextVirtualAddress;
	sNextVirtualAddress += size;

	return address;
}


static addr_t
get_next_physical_address(size_t size)
{
	addr_t address = sNextPhysicalAddress;
	sNextPhysicalAddress += size;

	return address;
}


static addr_t
get_next_virtual_page()
{
	return get_next_virtual_address(B_PAGE_SIZE);
}


static addr_t
get_next_physical_page()
{
	return get_next_physical_address(B_PAGE_SIZE);
}


static uint32 *
get_next_page_table()
{
	TRACE("get_next_page_table, sNextPageTableAddress %#" B_PRIxADDR
		", kPageTableRegionEnd %#" B_PRIxADDR "\n", sNextPageTableAddress,
		kPageTableRegionEnd);

	addr_t address = sNextPageTableAddress;
	if (address >= kPageTableRegionEnd)
		return (uint32 *)get_next_physical_page();

	sNextPageTableAddress += B_PAGE_SIZE;
	return (uint32 *)address;
}


/*!	Adds a new page table for the specified base address */
static void
add_page_table(addr_t base)
{
	// Get new page table and clear it out
	uint32 *pageTable = get_next_page_table();
	if (pageTable > (uint32 *)(8 * 1024 * 1024)) {
		panic("tried to add page table beyond the identity mapped 8 MB "
			"region\n");
	}

	TRACE("add_page_table(base = %p), got page: %p\n", (void*)base, pageTable);

	gKernelArgs.arch_args.pgtables[gKernelArgs.arch_args.num_pgtables++]
		= (uint32)pageTable;

	for (int32 i = 0; i < 1024; i++)
		pageTable[i] = 0;

	// put the new page table into the page directory
	sPageDirectory[base / (4 * 1024 * 1024)]
		= (uint32)pageTable | kDefaultPageTableFlags;
}


static void
unmap_page(addr_t virtualAddress)
{
	TRACE("unmap_page(virtualAddress = %p)\n", (void *)virtualAddress);

	if (virtualAddress < KERNEL_BASE) {
		panic("unmap_page: asked to unmap invalid page %p!\n",
			(void *)virtualAddress);
	}

	// unmap the page from the correct page table
	uint32 *pageTable = (uint32 *)(sPageDirectory[virtualAddress
		/ (B_PAGE_SIZE * 1024)] & 0xfffff000);
	pageTable[(virtualAddress % (B_PAGE_SIZE * 1024)) / B_PAGE_SIZE] = 0;

	asm volatile("invlpg (%0)" : : "r" (virtualAddress));
}


/*!	Creates an entry to map the specified virtualAddress to the given
	physicalAddress.
	If the mapping goes beyond the current page table, it will allocate
	a new one. If it cannot map the requested page, it panics.
*/
static void
map_page(addr_t virtualAddress, addr_t physicalAddress, uint32 flags)
{
	TRACE("map_page: vaddr 0x%lx, paddr 0x%lx\n", virtualAddress,
		physicalAddress);

	if (virtualAddress < KERNEL_BASE) {
		panic("map_page: asked to map invalid page %p!\n",
			(void *)virtualAddress);
	}

	if (virtualAddress >= sMaxVirtualAddress) {
		// we need to add a new page table
		add_page_table(sMaxVirtualAddress);
		sMaxVirtualAddress += B_PAGE_SIZE * 1024;

		if (virtualAddress >= sMaxVirtualAddress) {
			panic("map_page: asked to map a page to %p\n",
				(void *)virtualAddress);
		}
	}

	physicalAddress &= ~(B_PAGE_SIZE - 1);

	// map the page to the correct page table
	uint32 *pageTable = (uint32 *)(sPageDirectory[virtualAddress
		/ (B_PAGE_SIZE * 1024)] & 0xfffff000);
	uint32 tableEntry = (virtualAddress % (B_PAGE_SIZE * 1024)) / B_PAGE_SIZE;

	TRACE("map_page: inserting pageTable %p, tableEntry %" B_PRIu32
		", physicalAddress %#" B_PRIxADDR "\n", pageTable, tableEntry,
		physicalAddress);

	pageTable[tableEntry] = physicalAddress | flags;

	asm volatile("invlpg (%0)" : : "r" (virtualAddress));

	TRACE("map_page: done\n");
}


static void
sort_addr_range(addr_range *range, int count)
{
	addr_range tempRange;
	bool done;
	int i;

	do {
		done = true;
		for (i = 1; i < count; i++) {
			if (range[i].start < range[i - 1].start) {
				done = false;
				memcpy(&tempRange, &range[i], sizeof(addr_range));
				memcpy(&range[i], &range[i - 1], sizeof(addr_range));
				memcpy(&range[i - 1], &tempRange, sizeof(addr_range));
			}
		}
	} while (!done);
}



#ifdef TRACE_MEMORY_MAP
static const char *
e820_memory_type(uint32 type)
{
	switch (type) {
		case 1: return "memory";
		case 2: return "reserved";
		case 3: return "ACPI reclaim";
		case 4: return "ACPI NVS";
		default: return "unknown/reserved";
	}
}
#endif


static uint32
get_memory_map(extended_memory **_extendedMemory)
{
	extended_memory *block = (extended_memory *)kExtraSegmentScratch;
	bios_regs regs = {0, 0, sizeof(extended_memory), 0, 0, (uint32)block, 0, 0};
	uint32 count = 0;

	TRACE("get_memory_map()\n");

	do {
		regs.eax = 0xe820;
		regs.edx = 'SMAP';

		call_bios(0x15, &regs);
		if (regs.flags & CARRY_FLAG)
			return 0;

		regs.edi += sizeof(extended_memory);
		count++;
	} while (regs.ebx != 0);

	*_extendedMemory = block;

#ifdef TRACE_MEMORY_MAP
	dprintf("extended memory info (from 0xe820):\n");
	for (uint32 i = 0; i < count; i++) {
		dprintf("    base 0x%08Lx, len 0x%08Lx, type %lu (%s)\n",
			block[i].base_addr, block[i].length,
			block[i].type, e820_memory_type(block[i].type));
	}
#endif

	return count;
}


static void
init_page_directory(void)
{
	TRACE("init_page_directory\n");

	// allocate a new pgdir
	sPageDirectory = (uint32 *)get_next_physical_page();
	gKernelArgs.arch_args.phys_pgdir = (uint32)sPageDirectory;

	// clear out the pgdir
	for (int32 i = 0; i < 1024; i++) {
		sPageDirectory[i] = 0;
	}

	// Identity map the first 8 MB of memory so that their
	// physical and virtual address are the same.
	// These page tables won't be taken over into the kernel.

	// make the first page table at the first free spot
	uint32 *pageTable = get_next_page_table();

	for (int32 i = 0; i < 1024; i++) {
		pageTable[i] = (i * 0x1000) | kDefaultPageFlags;
	}

	sPageDirectory[0] = (uint32)pageTable | kDefaultPageFlags;

	// make the second page table
	pageTable = get_next_page_table();

	for (int32 i = 0; i < 1024; i++) {
		pageTable[i] = (i * 0x1000 + 0x400000) | kDefaultPageFlags;
	}

	sPageDirectory[1] = (uint32)pageTable | kDefaultPageFlags;

	gKernelArgs.arch_args.num_pgtables = 0;
	add_page_table(KERNEL_BASE);

	// switch to the new pgdir and enable paging
	asm("movl %0, %%eax;"
		"movl %%eax, %%cr3;" : : "m" (sPageDirectory) : "eax");
	// Important.  Make sure supervisor threads can fault on read only pages...
	asm("movl %%eax, %%cr0" : : "a" ((1 << 31) | (1 << 16) | (1 << 5) | 1));
}


//	#pragma mark -


/*!
	Neither \a virtualAddress nor \a size need to be aligned, but the function
	will map all pages the range intersects with.
	If physicalAddress is not page-aligned, the returned virtual address will
	have the same "misalignment".
*/
extern "C" addr_t
mmu_map_physical_memory(addr_t physicalAddress, size_t size, uint32 flags)
{
	addr_t address = sNextVirtualAddress;
	addr_t pageOffset = physicalAddress & (B_PAGE_SIZE - 1);

	physicalAddress -= pageOffset;
	size += pageOffset;

	for (addr_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
		map_page(get_next_virtual_page(), physicalAddress + offset, flags);
	}

	return address + pageOffset;
}


extern "C" void *
mmu_allocate(void *virtualAddress, size_t size)
{
	TRACE("mmu_allocate: requested vaddr: %p, next free vaddr: 0x%lx, size: "
		"%ld\n", virtualAddress, sNextVirtualAddress, size);

	size = (size + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
		// get number of pages to map

	if (virtualAddress != NULL) {
		// This special path is almost only useful for loading the
		// kernel into memory; it will only allow you to map the
		// 'kMaxKernelSize' bytes following the kernel base address.
		// Also, it won't check for already mapped addresses, so
		// you better know why you are here :)
		addr_t address = (addr_t)virtualAddress;

		// is the address within the valid range?
		if (address < KERNEL_BASE
			|| address + size >= KERNEL_BASE + kMaxKernelSize)
			return NULL;

		for (uint32 i = 0; i < size; i++) {
			map_page(address, get_next_physical_page(), kDefaultPageFlags);
			address += B_PAGE_SIZE;
		}

		return virtualAddress;
	}

	void *address = (void *)sNextVirtualAddress;

	for (uint32 i = 0; i < size; i++) {
		map_page(get_next_virtual_page(), get_next_physical_page(),
			kDefaultPageFlags);
	}

	return address;
}


/*!	This will unmap the allocated chunk of memory from the virtual
	address space. It might not actually free memory (as its implementation
	is very simple), but it might.
	Neither \a virtualAddress nor \a size need to be aligned, but the function
	will unmap all pages the range intersects with.
*/
extern "C" void
mmu_free(void *virtualAddress, size_t size)
{
	TRACE("mmu_free(virtualAddress = %p, size: %ld)\n", virtualAddress, size);

	addr_t address = (addr_t)virtualAddress;
	addr_t pageOffset = address % B_PAGE_SIZE;
	address -= pageOffset;
	size = (size + pageOffset + B_PAGE_SIZE - 1) / B_PAGE_SIZE * B_PAGE_SIZE;

	// is the address within the valid range?
	if (address < KERNEL_BASE || address + size > sNextVirtualAddress) {
		panic("mmu_free: asked to unmap out of range region (%p, size %lx)\n",
			(void *)address, size);
	}

	// unmap all pages within the range
	for (size_t i = 0; i < size; i += B_PAGE_SIZE) {
		unmap_page(address);
		address += B_PAGE_SIZE;
	}

	if (address + size == sNextVirtualAddress) {
		// we can actually reuse the virtual address space
		sNextVirtualAddress -= size;
	}
}


/*!	Sets up the final and kernel accessible GDT and IDT tables.
	BIOS calls won't work any longer after this function has
	been called.
*/
extern "C" void
mmu_init_for_kernel(void)
{
	TRACE("mmu_init_for_kernel\n");
	// set up a new idt
	{
		struct gdt_idt_descr idtDescriptor;
		uint32 *idt;

		// find a new idt
		idt = (uint32 *)get_next_physical_page();
		gKernelArgs.arch_args.phys_idt = (uint32)idt;

		TRACE("idt at %p\n", idt);

		// map the idt into virtual space
		gKernelArgs.arch_args.vir_idt = (uint32)get_next_virtual_page();
		map_page(gKernelArgs.arch_args.vir_idt, (uint32)idt, kDefaultPageFlags);

		// clear it out
		uint32* virtualIDT = (uint32*)gKernelArgs.arch_args.vir_idt;
		for (int32 i = 0; i < IDT_LIMIT / 4; i++) {
			virtualIDT[i] = 0;
		}

		// load the idt
		idtDescriptor.limit = IDT_LIMIT - 1;
		idtDescriptor.base = (uint32 *)gKernelArgs.arch_args.vir_idt;

		asm("lidt	%0;"
			: : "m" (idtDescriptor));

		TRACE("idt at virtual address 0x%lx\n", gKernelArgs.arch_args.vir_idt);
	}

	// set up a new gdt
	{
		struct gdt_idt_descr gdtDescriptor;
		segment_descriptor *gdt;

		// find a new gdt
		gdt = (segment_descriptor *)get_next_physical_page();
		gKernelArgs.arch_args.phys_gdt = (uint32)gdt;

		TRACE("gdt at %p\n", gdt);

		// map the gdt into virtual space
		gKernelArgs.arch_args.vir_gdt = (uint32)get_next_virtual_page();
		map_page(gKernelArgs.arch_args.vir_gdt, (uint32)gdt, kDefaultPageFlags);

		// put standard segment descriptors in it
		segment_descriptor* virtualGDT
			= (segment_descriptor*)gKernelArgs.arch_args.vir_gdt;
		clear_segment_descriptor(&virtualGDT[0]);

		// seg 0x08 - kernel 4GB code
		set_segment_descriptor(&virtualGDT[1], 0, 0xffffffff, DT_CODE_READABLE,
			DPL_KERNEL);

		// seg 0x10 - kernel 4GB data
		set_segment_descriptor(&virtualGDT[2], 0, 0xffffffff, DT_DATA_WRITEABLE,
			DPL_KERNEL);

		// seg 0x1b - ring 3 user 4GB code
		set_segment_descriptor(&virtualGDT[3], 0, 0xffffffff, DT_CODE_READABLE,
			DPL_USER);

		// seg 0x23 - ring 3 user 4GB data
		set_segment_descriptor(&virtualGDT[4], 0, 0xffffffff, DT_DATA_WRITEABLE,
			DPL_USER);

		// virtualGDT[5] and above will be filled later by the kernel
		// to contain the TSS descriptors, and for TLS (one for every CPU)

		// load the GDT
		gdtDescriptor.limit = GDT_LIMIT - 1;
		gdtDescriptor.base = (uint32 *)gKernelArgs.arch_args.vir_gdt;

		asm("lgdt	%0;"
			: : "m" (gdtDescriptor));

		TRACE("gdt at virtual address %p\n",
			(void*)gKernelArgs.arch_args.vir_gdt);
	}

	// save the memory we've physically allocated
	gKernelArgs.physical_allocated_range[0].size
		= sNextPhysicalAddress - gKernelArgs.physical_allocated_range[0].start;

	// Save the memory we've virtually allocated (for the kernel and other
	// stuff)
	gKernelArgs.virtual_allocated_range[0].start = KERNEL_BASE;
	gKernelArgs.virtual_allocated_range[0].size
		= sNextVirtualAddress - KERNEL_BASE;
	gKernelArgs.num_virtual_allocated_ranges = 1;

	// sort the address ranges
	sort_addr_range(gKernelArgs.physical_memory_range,
		gKernelArgs.num_physical_memory_ranges);
	sort_addr_range(gKernelArgs.physical_allocated_range,
		gKernelArgs.num_physical_allocated_ranges);
	sort_addr_range(gKernelArgs.virtual_allocated_range,
		gKernelArgs.num_virtual_allocated_ranges);

#ifdef TRACE_MEMORY_MAP
	{
		uint32 i;

		dprintf("phys memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
			dprintf("    base 0x%08lx, length 0x%08lx\n", gKernelArgs.physical_memory_range[i].start, gKernelArgs.physical_memory_range[i].size);
		}

		dprintf("allocated phys memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_physical_allocated_ranges; i++) {
			dprintf("    base 0x%08lx, length 0x%08lx\n", gKernelArgs.physical_allocated_range[i].start, gKernelArgs.physical_allocated_range[i].size);
		}

		dprintf("allocated virt memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_virtual_allocated_ranges; i++) {
			dprintf("    base 0x%08lx, length 0x%08lx\n", gKernelArgs.virtual_allocated_range[i].start, gKernelArgs.virtual_allocated_range[i].size);
		}
	}
#endif
}


extern "C" void
mmu_init(void)
{
	TRACE("mmu_init\n");

	gKernelArgs.physical_allocated_range[0].start = sNextPhysicalAddress;
	gKernelArgs.physical_allocated_range[0].size = 0;
	gKernelArgs.num_physical_allocated_ranges = 1;
		// remember the start of the allocated physical pages

	init_page_directory();

	// Map the page directory into kernel space at 0xffc00000-0xffffffff
	// this enables a mmu trick where the 4 MB region that this pgdir entry
	// represents now maps the 4MB of potential pagetables that the pgdir
	// points to. Thrown away later in VM bringup, but useful for now.
	sPageDirectory[1023] = (uint32)sPageDirectory | kDefaultPageFlags;

	// also map it on the next vpage
	gKernelArgs.arch_args.vir_pgdir = get_next_virtual_page();
	map_page(gKernelArgs.arch_args.vir_pgdir, (uint32)sPageDirectory,
		kDefaultPageFlags);

	// map in a kernel stack
	gKernelArgs.cpu_kstack[0].start = (addr_t)mmu_allocate(NULL,
		KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE);
	gKernelArgs.cpu_kstack[0].size = KERNEL_STACK_SIZE
		+ KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;

	TRACE("kernel stack at 0x%lx to 0x%lx\n", gKernelArgs.cpu_kstack[0].start,
		gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size);

	extended_memory *extMemoryBlock;
	uint32 extMemoryCount = get_memory_map(&extMemoryBlock);

	// figure out the memory map
	if (extMemoryCount > 0) {
		gKernelArgs.num_physical_memory_ranges = 0;

		for (uint32 i = 0; i < extMemoryCount; i++) {
			// Type 1 is available memory
			if (extMemoryBlock[i].type == 1) {
				// round everything up to page boundaries, exclusive of pages
				// it partially occupies
				if ((extMemoryBlock[i].base_addr % B_PAGE_SIZE) != 0) {
					extMemoryBlock[i].length -= B_PAGE_SIZE
						- extMemoryBlock[i].base_addr % B_PAGE_SIZE;
				}
				extMemoryBlock[i].base_addr
					= ROUNDUP(extMemoryBlock[i].base_addr, B_PAGE_SIZE);
				extMemoryBlock[i].length
					= ROUNDDOWN(extMemoryBlock[i].length, B_PAGE_SIZE);

				// we ignore all memory beyond 4 GB
				if (extMemoryBlock[i].base_addr > 0xffffffffULL)
					continue;

				if (extMemoryBlock[i].base_addr + extMemoryBlock[i].length
						> 0xffffffffULL) {
					extMemoryBlock[i].length
						= 0x100000000ULL - extMemoryBlock[i].base_addr;
				}

				if (gKernelArgs.num_physical_memory_ranges > 0) {
					// we might want to extend a previous hole
					addr_t previousEnd = gKernelArgs.physical_memory_range[
							gKernelArgs.num_physical_memory_ranges - 1].start
						+ gKernelArgs.physical_memory_range[
							gKernelArgs.num_physical_memory_ranges - 1].size;
					addr_t holeSize = extMemoryBlock[i].base_addr - previousEnd;

					// If the hole is smaller than 1 MB, we try to mark the
					// memory as allocated and extend the previous memory range
					if (previousEnd <= extMemoryBlock[i].base_addr
						&& holeSize < 0x100000
						&& insert_physical_allocated_range(previousEnd,
							extMemoryBlock[i].base_addr - previousEnd)
								== B_OK) {
						gKernelArgs.physical_memory_range[
							gKernelArgs.num_physical_memory_ranges - 1].size
								+= holeSize;
					}
				}

				insert_physical_memory_range(extMemoryBlock[i].base_addr,
					extMemoryBlock[i].length);
			}
		}
	} else {
		// TODO: for now!
		dprintf("No extended memory block - using 32 MB (fix me!)\n");
		uint32 memSize = 32 * 1024 * 1024;

		// We dont have an extended map, assume memory is contiguously mapped
		// at 0x0
		gKernelArgs.physical_memory_range[0].start = 0;
		gKernelArgs.physical_memory_range[0].size = memSize;
		gKernelArgs.num_physical_memory_ranges = 1;

		// mark the bios area allocated
		uint32 biosRange = gKernelArgs.num_physical_allocated_ranges++;

		gKernelArgs.physical_allocated_range[biosRange].start = 0x9f000;
			// 640k - 1 page
		gKernelArgs.physical_allocated_range[biosRange].size = 0x61000;
	}

	gKernelArgs.arch_args.page_hole = 0xffc00000;
}


//	#pragma mark -


extern "C" status_t
platform_allocate_region(void **_address, size_t size, uint8 protection,
	bool /*exactAddress*/)
{
	void *address = mmu_allocate(*_address, size);
	if (address == NULL)
		return B_NO_MEMORY;

	*_address = address;
	return B_OK;
}


extern "C" status_t
platform_free_region(void *address, size_t size)
{
	mmu_free(address, size);
	return B_OK;
}


void
platform_release_heap(struct stage2_args *args, void *base)
{
	// It will be freed automatically, since it is in the
	// identity mapped region, and not stored in the kernel's
	// page tables.
}


status_t
platform_init_heap(struct stage2_args *args, void **_base, void **_top)
{
	void *heap = (void *)get_next_physical_address(args->heap_size);
	if (heap == NULL)
		return B_NO_MEMORY;

	*_base = heap;
	*_top = (void *)((int8 *)heap + args->heap_size);
	return B_OK;
}


