/*
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include <vm.h>
#include <int.h>
#include <thread.h>
#include <smp.h>
#include <vm_priv.h>
#include <ksyscalls.h>

#include <arch/cpu.h>
#include <arch/int.h>
#include <arch/faults.h>
#include <arch/vm.h>
#include <arch/smp.h>
#include <arch/user_debugger.h>

#include <arch/x86/interrupts.h>
#include <arch/x86/faults.h>
#include <arch/x86/descriptors.h>

#include <string.h>

const char *kInterruptNames[] = {
	/*  0 */ "Divide Error Exception",
	/*  1 */ "Debug Exception",
	/*  2 */ "NMI Interrupt",
	/*  3 */ "Breakpoint Exception",
	/*  4 */ "Overflow Exception",
	/*  5 */ "BOUND Range Exceeded Exception",
	/*  6 */ "Invalid Opcode Exception",
	/*  7 */ "Device Not Available Exception",
	/*  8 */ "Double Fault Exception",
	/*  9 */ "Coprocessor Segment Overrun",
	/* 10 */ "Invalid TSS Exception",
	/* 11 */ "Segment Not Present",
	/* 12 */ "Stack Fault Exception",
	/* 13 */ "General Protection Exception",
	/* 14 */ "Page-Fault Exception",
	/* 15 */ "-",
	/* 16 */ "x87 FPU Floating-Point Error",
	/* 17 */ "Alignment Check Exception",
	/* 18 */ "Machine-Check Exception",
	/* 19 */ "SIMD Floating-Point Exception",
};

#define MAX_ARGS 16

typedef struct {
	uint32 a, b;
} desc_table;
static desc_table *idt = NULL;

struct iframe_stack gBootFrameStack;


static void
interrupt_ack(int n)
{
	if (n >= 0x20 && n < 0x30) {
		// 8239 controlled interrupt
		if (n > 0x27)
			out8(0x20, 0xa0);	// EOI to pic 2
		out8(0x20, 0x20);	// EOI to pic 1
	}
}


static void
set_gate(desc_table *gate_addr, addr_t addr, int type, int dpl)
{
	unsigned int gate1; // first byte of gate desc
	unsigned int gate2; // second byte of gate desc

	gate1 = (KERNEL_CODE_SEG << 16) | (0x0000ffff & addr);
	gate2 = (0xffff0000 & addr) | 0x8000 | (dpl << 13) | (type << 8);

	gate_addr->a = gate1;
	gate_addr->b = gate2;
}


static void
set_intr_gate(int n, void *addr)
{
	set_gate(&idt[n], (addr_t)addr, 14, DPL_KERNEL);
}


static void
set_system_gate(int n, void *addr)
{
	set_gate(&idt[n], (unsigned int)addr, 15, DPL_USER);
}


void
x86_set_task_gate(int32 n, int32 segment)
{
	idt[n].a = (segment << 16);
	idt[n].b = 0x8000 | (0 << 13) | (0x5 << 8); // present, dpl 0, type 5
}


void
arch_int_enable_io_interrupt(int irq)
{
	if (irq < 0 || irq >= 0x10)
		return;

	//dprintf("arch_int_enable_io_interrupt: irq %d\n", irq);

	/* if this is a external interrupt via 8239, enable it here */
	if (irq < 8)
		out8(in8(0x21) & ~(1 << irq), 0x21);
	else
		out8(in8(0xa1) & ~(1 << (irq - 8)), 0xa1);
}


void
arch_int_disable_io_interrupt(int irq)
{
	/* never disable slave pic line IRQ 2 */
	if (irq < 0 || irq >= 0x10 || irq == 2)
		return;

	/* if this is a external interrupt via 8239, disable it here */
	if (irq < 8)
		out8(in8(0x21) | (1 << irq), 0x21);
	else
		out8(in8(0xa1) | (1 << (irq - 8)), 0xa1);
}


void
arch_int_enable_interrupts(void)
{
	asm("sti");
}


int
arch_int_disable_interrupts(void)
{
	int flags;

	asm("pushfl;\n"
		"popl %0;\n"
		"cli" : "=g" (flags));
	return flags & 0x200 ? 1 : 0;
}


void
arch_int_restore_interrupts(int oldstate)
{
	int flags = oldstate ? 0x200 : 0;

	asm("pushfl;\n"
		"popl	%1;\n"
		"andl	$0xfffffdff,%1;\n"
		"orl	%0,%1;\n"
		"pushl	%1;\n"
		"popfl\n"
		: : "r" (flags), "r" (0));
}


bool
arch_int_are_interrupts_enabled(void)
{
	int flags;

	asm("pushfl;\n"
		"popl %0;\n" : "=g" (flags));
	return flags & 0x200 ? 1 : 0;
}


/* keep the compiler happy, this function must be called only from assembly */
void i386_handle_trap(struct iframe frame);

void
i386_handle_trap(struct iframe frame)
{
	struct thread *thread = thread_get_current_thread();
	int ret = B_HANDLED_INTERRUPT;

	if (thread)
		x86_push_iframe(&thread->arch_info.iframes, &frame);
	else
		x86_push_iframe(&gBootFrameStack, &frame);

	if (frame.cs == USER_CODE_SEG) {
		i386_exit_user_debug_at_kernel_entry();
		thread_at_kernel_entry();
	}

//	if(frame.vector != 0x20)
//		dprintf("i386_handle_trap: vector 0x%x, ip 0x%x, cpu %d\n", frame.vector, frame.eip, smp_get_current_cpu());

	switch (frame.vector) {
		case 1:
			ret = i386_handle_debug_exception(&frame);
			break;
		case 3:
			ret = i386_handle_breakpoint_exception(&frame);
			break;
		case 8:		// double fault
			ret = i386_double_fault(frame.error_code);
			break;
		case 13:	// general protection fault
			ret = i386_general_protection_fault(frame.error_code);
			break;
		case 14:	// page fault
		{
			unsigned int cr2;
			addr_t newip;

			asm("movl %%cr2, %0" : "=r" (cr2));

			if ((frame.flags & 0x200) == 0 && !kernel_startup) {
				// if the interrupts were disabled, and we are not running the kernel startup
				// the page fault was not allowed to happen and we must panic
				panic("page fault, but interrupts were disabled. Touching address %p from eip %p\n", (void *)cr2, (void *)frame.eip);
			} else if (thread != NULL && thread->page_faults_allowed < 1)
				panic("page fault not allowed at this place. Touching address %p from eip %p\n", (void *)cr2, (void *)frame.eip);

			enable_interrupts();

			ret = vm_page_fault(cr2, frame.eip,
				(frame.error_code & 0x2) != 0,	// write access
				(frame.error_code & 0x4) != 0,	// userland
				&newip);
			if (newip != 0) {
				// the page fault handler wants us to modify the iframe to set the
				// IP the cpu will return to to be this ip
				frame.eip = newip;
			}
			break;
		}

		case 99:	// syscall
		{
			uint64 retcode;
			unsigned int args[MAX_ARGS];

#if 0
{
			int i;
			dprintf("i386_handle_trap: syscall %d, count %d, ptr 0x%x\n", frame.eax, frame.ecx, frame.edx);
			dprintf(" call stack:\n");
			for(i=0; i<frame.ecx; i++)
				dprintf("\t0x%x\n", ((unsigned int *)frame.edx)[i]);
}
#endif
			/* syscall interface works as such:
			 *  %eax has syscall #
			 *  %ecx has number of args (0-16)
			 *  %edx has pointer to buffer containing args from first to last
			 * each is verified to make sure someone doesn't try to clobber it
			 */
			if (frame.ecx <= MAX_ARGS) {
				if (IS_KERNEL_ADDRESS(frame.edx)
					|| user_memcpy(args, (void *)frame.edx, frame.ecx * sizeof(unsigned int)) < B_OK) {
					retcode =  B_BAD_ADDRESS;
				} else
					ret = syscall_dispatcher(frame.eax, (void *)args, &retcode);
			} else {
				// want to pass too many args into the system
				retcode = EINVAL;
			}
			frame.eax = retcode & 0xffffffff;
			frame.edx = retcode >> 32;
			break;
		}

		default:
			if (frame.vector >= 0x20) {
				interrupt_ack(frame.vector); // ack the 8239 (if applicable)
				ret = int_io_interrupt_handler(frame.vector);
			} else {
				panic("i386_handle_trap: unhandled trap 0x%x (%s) at ip 0x%x, thread 0x%x!\n",
					frame.vector, kInterruptNames[frame.vector], frame.eip, thread ? thread->id : -1);
				ret = B_HANDLED_INTERRUPT;
			}
			break;
	}

	if (ret == B_INVOKE_SCHEDULER) {
		cpu_status state = disable_interrupts();
		GRAB_THREAD_LOCK();

		scheduler_reschedule();

		RELEASE_THREAD_LOCK();
		restore_interrupts(state);
	}

	if (frame.cs == USER_CODE_SEG) {
		thread_at_kernel_exit();
		i386_init_user_debug_at_kernel_exit(&frame);
	}

//	dprintf("0x%x cpu %d!\n", thread_get_current_thread_id(), smp_get_current_cpu());

	if (thread)
		x86_pop_iframe(&thread->arch_info.iframes);
	else
		x86_pop_iframe(&gBootFrameStack);
}


status_t
arch_int_init(kernel_args *args)
{
	// set the global idt variable
	idt = (desc_table *)args->arch_args.vir_idt;

	// setup the interrupt controller
	out8(0x11, 0x20);	// Start initialization sequence for #1.
	out8(0x11, 0xa0);	// ...and #2.
	out8(0x20, 0x21);	// Set start of interrupts for #1 (0x20).
	out8(0x28, 0xa1);	// Set start of interrupts for #2 (0x28).
	out8(0x04, 0x21);	// Set #1 to be the master.
	out8(0x02, 0xa1);	// Set #2 to be the slave.
	out8(0x01, 0x21);	// Set both to operate in 8086 mode.
	out8(0x01, 0xa1);
	out8(0xfb, 0x21);	// Mask off all interrupts (except slave pic line IRQ 2).
	out8(0xff, 0xa1); 	// Mask off interrupts on the slave.

	set_intr_gate(0,  &trap0);
	set_intr_gate(1,  &trap1);
	set_intr_gate(2,  &trap2);
	set_system_gate(3,  &trap3);
	set_intr_gate(4,  &trap4);
	set_intr_gate(5,  &trap5);
	set_intr_gate(6,  &trap6);
	set_intr_gate(7,  &trap7);
	// trap8 (double fault) is set in arch_cpu.c
	set_intr_gate(9,  &trap9);
	set_intr_gate(10,  &trap10);
	set_intr_gate(11,  &trap11);
	set_intr_gate(12,  &trap12);
	set_intr_gate(13,  &trap13);
	set_intr_gate(14,  &trap14);
//	set_intr_gate(15,  &trap15);
	set_intr_gate(16,  &trap16);
	set_intr_gate(17,  &trap17);
	set_intr_gate(18,  &trap18);

	set_intr_gate(32,  &trap32);
	set_intr_gate(33,  &trap33);
	set_intr_gate(34,  &trap34);
	set_intr_gate(35,  &trap35);
	set_intr_gate(36,  &trap36);
	set_intr_gate(37,  &trap37);
	set_intr_gate(38,  &trap38);
	set_intr_gate(39,  &trap39);
	set_intr_gate(40,  &trap40);
	set_intr_gate(41,  &trap41);
	set_intr_gate(42,  &trap42);
	set_intr_gate(43,  &trap43);
	set_intr_gate(44,  &trap44);
	set_intr_gate(45,  &trap45);
	set_intr_gate(46,  &trap46);
	set_intr_gate(47,  &trap47);

	set_system_gate(99, &trap99);

	set_intr_gate(251, &trap251);
	set_intr_gate(252, &trap252);
	set_intr_gate(253, &trap253);
	set_intr_gate(254, &trap254);
	set_intr_gate(255, &trap255);

	return B_OK;
}


status_t
arch_int_init_post_vm(kernel_args *args)
{
	area_id area;

	idt = (desc_table *)args->arch_args.vir_idt;
	area = create_area("idt", (void *)&idt, B_EXACT_ADDRESS, B_PAGE_SIZE, B_ALREADY_WIRED,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);

	return area >= B_OK ? B_OK : area;
}
