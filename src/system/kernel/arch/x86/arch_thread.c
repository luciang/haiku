/*
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <thread.h>
#include <arch/thread.h>
#include <arch/user_debugger.h>
#include <arch_cpu.h>
#include <kernel.h>
#include <debug.h>
#include <int.h>
#include <vm_address_space.h>
#include <tls.h>

#include <string.h>


//#define TRACE_ARCH_THREAD
#ifdef TRACE_ARCH_THREAD
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// from arch_interrupts.S
extern void	i386_stack_init(struct farcall *interrupt_stack_offset);
extern void i386_restore_frame_from_syscall(struct iframe frame);


static struct arch_thread sInitialState;
	// ToDo:
	// __attribute__ ((aligned(16)));
	// the fpu_state must be aligned on a 16 byte boundary, so that fxsave can use it


status_t
arch_thread_init(struct kernel_args *args)
{
	// save one global valid FPU state; it will be copied in the arch dependent
	// part of each new thread

	asm("fninit");
	// ToDo: add MMX/SSE support (ie. use fxsave)
	i386_fsave(sInitialState.fpu_state);

	// let the asm function know the offset to the interrupt stack within struct thread
	// I know no better ( = static) way to tell the asm function the offset
	i386_stack_init(&((struct thread *)0)->arch_info.interrupt_stack);

	return B_OK;
}


void
x86_push_iframe(struct iframe_stack *stack, struct iframe *frame)
{
	ASSERT(stack->index < IFRAME_TRACE_DEPTH);
	stack->frames[stack->index++] = frame;
}


void
x86_pop_iframe(struct iframe_stack *stack)
{
	ASSERT(stack->index > 0);
	stack->index--;
}


/**	Returns the current iframe structure of the running thread.
 *	This function must only be called in a context where it's actually
 *	sure that such iframe exists; ie. from syscalls, but usually not
 *	from standard kernel threads.
 */

static struct iframe *
i386_get_current_iframe(void)
{
	struct thread *thread = thread_get_current_thread();

	ASSERT(thread->arch_info.iframes.index >= 0);
	return thread->arch_info.iframes.frames[thread->arch_info.iframes.index - 1];
}


/**	\brief Returns the current thread's topmost (i.e. most recent)
 *	userland->kernel transition iframe (usually the first one, save for
 *	interrupts in signal handlers).
 *	\return The iframe, or \c NULL, if there is no such iframe (e.g. when
 *			the thread is a kernel thread).
 */

struct iframe *
i386_get_user_iframe(void)
{
	struct thread *thread = thread_get_current_thread();
	int i;

	for (i = thread->arch_info.iframes.index - 1; i >= 0; i--) {
		struct iframe *frame = thread->arch_info.iframes.frames[i];
		if (frame->cs == USER_CODE_SEG)
			return frame;
	}

	return NULL;
}


inline void *
x86_next_page_directory(struct thread *from, struct thread *to)
{
	if (from->team->address_space != NULL && to->team->address_space != NULL) {
		// they are both user space threads
		if (from->team == to->team) {
			// dont change the pgdir, same address space
			return NULL;
		}
		// switching to a new address space
		return i386_translation_map_get_pgdir(&to->team->address_space->translation_map);
	} else if (from->team->address_space == NULL && to->team->address_space == NULL) {
		// they must both be kernel space threads
		return NULL;
	} else if (to->team->address_space == NULL) {
		// the one we're switching to is kernel space
		return i386_translation_map_get_pgdir(&vm_kernel_address_space()->translation_map);
	}
	
	return i386_translation_map_get_pgdir(&to->team->address_space->translation_map);
}


static inline void
set_fs_register(uint32 segment)
{
	asm("movl %0,%%fs" :: "r" (segment));
}


static void
set_tls_context(struct thread *thread)
{
	int entry = smp_get_current_cpu() + TLS_BASE_SEGMENT;

	set_segment_descriptor_base(&gGDT[entry], thread->user_local_storage);
	set_fs_register((entry << 3) | DPL_USER);
}


status_t
arch_team_init_team_struct(struct team *p, bool kernel)
{
	return B_OK;
}


status_t
arch_thread_init_thread_struct(struct thread *thread)
{
	// set up an initial state (stack & fpu)
	memcpy(&thread->arch_info, &sInitialState, sizeof(struct arch_thread));
	return B_OK;
}


status_t
arch_thread_init_kthread_stack(struct thread *t, int (*start_func)(void), void (*entry_func)(void), void (*exit_func)(void))
{
	addr_t *kstack = (addr_t *)t->kernel_stack_base;
	addr_t *kstack_top = kstack + KERNEL_STACK_SIZE / sizeof(addr_t);
	int i;

	TRACE(("arch_thread_initialize_kthread_stack: kstack 0x%p, start_func 0x%p, entry_func 0x%p\n",
		kstack, start_func, entry_func));

	// clear the kernel stack
#ifdef DEBUG_KERNEL_STACKS
#	ifdef STACK_GROWS_DOWNWARDS
	memset((void *)((addr_t)kstack + KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE), 0,
		KERNEL_STACK_SIZE - KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE);
#	else
	memset(kstack, 0, KERNEL_STACK_SIZE - KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE);
#	endif
#else
	memset(kstack, 0, KERNEL_STACK_SIZE);
#endif

	// set the final return address to be thread_kthread_exit
	kstack_top--;
	*kstack_top = (unsigned int)exit_func;

	// set the return address to be the start of the first function
	kstack_top--;
	*kstack_top = (unsigned int)start_func;

	// set the return address to be the start of the entry (thread setup) function
	kstack_top--;
	*kstack_top = (unsigned int)entry_func;

	// simulate pushfl
//	kstack_top--;
//	*kstack_top = 0x00; // interrupts still disabled after the switch

	// simulate initial popad
	for (i = 0; i < 8; i++) {
		kstack_top--;
		*kstack_top = 0;
	}

	// save the stack position
	t->arch_info.current_stack.esp = kstack_top;
	t->arch_info.current_stack.ss = (addr_t *)KERNEL_DATA_SEG;

	return B_OK;
}


/** Initializes the user-space TLS local storage pointer in
 *	the thread structure, and the reserved TLS slots.
 *
 *	Is called from _create_user_thread_kentry().
 */

void
arch_thread_init_tls(struct thread *thread)
{
	uint32 *tls;

	thread->user_local_storage = thread->user_stack_base + thread->user_stack_size;
	tls = (uint32 *)thread->user_local_storage;

	tls[TLS_BASE_ADDRESS_SLOT] = thread->user_local_storage;
	tls[TLS_THREAD_ID_SLOT] = thread->id;
	tls[TLS_ERRNO_SLOT] = 0;
}


void
arch_thread_switch_kstack_and_call(struct thread *t, addr_t new_kstack, void (*func)(void *), void *arg)
{
	i386_switch_stack_and_call(new_kstack, func, arg);
}


void
arch_thread_context_switch(struct thread *from, struct thread *to)
{
	addr_t newPageDirectory;

#if 0
	int i;

	dprintf("arch_thread_context_switch: cpu %d 0x%x -> 0x%x, aspace 0x%x -> 0x%x, old stack = 0x%x:0x%x, stack = 0x%x:0x%x\n",
		smp_get_current_cpu(), t_from->id, t_to->id,
		t_from->team->address_space, t_to->team->address_space,
		t_from->arch_info.current_stack.ss, t_from->arch_info.current_stack.esp,
		t_to->arch_info.current_stack.ss, t_to->arch_info.current_stack.esp);
#endif
#if 0
	for (i = 0; i < 11; i++)
		dprintf("*esp[%d] (0x%x) = 0x%x\n", i, ((unsigned int *)new_at->esp + i), *((unsigned int *)new_at->esp + i));
#endif
	i386_set_tss_and_kstack(to->kernel_stack_base + KERNEL_STACK_SIZE);

	// set TLS GDT entry to the current thread - since this action is
	// dependent on the current CPU, we have to do it here
	if (to->user_local_storage != NULL)
		set_tls_context(to);

	newPageDirectory = (addr_t)x86_next_page_directory(from, to);

	if ((newPageDirectory % B_PAGE_SIZE) != 0)
		panic("arch_thread_context_switch: bad pgdir 0x%lx\n", newPageDirectory);

	// reinit debugging; necessary, if the thread was preempted after
	// initializing debugging before returning to userland
	if (to->team->address_space != NULL)
		i386_reinit_user_debug_after_context_switch(to);

	i386_fsave_swap(from->arch_info.fpu_state, to->arch_info.fpu_state);
	i386_context_switch(&from->arch_info, &to->arch_info, newPageDirectory);
}


void
arch_thread_dump_info(void *info)
{
	struct arch_thread *at = (struct arch_thread *)info;

	dprintf("\tesp: %p\n", at->current_stack.esp);
	dprintf("\tss: %p\n", at->current_stack.ss);
	dprintf("\tfpu_state at %p\n", at->fpu_state);
}


/** Sets up initial thread context and enters user space
 */

void
arch_thread_enter_uspace(struct thread *t, addr_t entry, void *args1, void *args2)
{
	addr_t ustack_top = t->user_stack_base + t->user_stack_size;

	TRACE(("arch_thread_enter_uspace: entry 0x%lx, args %p %p, ustack_top 0x%lx\n",
		entry, args1, args2, ustack_top));

	// access the new stack to make sure the memory page is present
	// while interrupts are disabled.
	// XXX does this belong there, should caller take care of it?
	*(uint32 *)(ustack_top - 8) = 0;

	disable_interrupts();

	i386_set_tss_and_kstack(t->kernel_stack_base + KERNEL_STACK_SIZE);

	// set the CPU dependent GDT entry for TLS
	set_tls_context(t);

	i386_enter_uspace(entry, args1, args2, ustack_top - 4);
}


status_t
arch_setup_signal_frame(struct thread *t, struct sigaction *sa, int sig, int sig_mask)
{
	struct iframe *frame = i386_get_current_iframe();
	uint32 *stack_ptr = (uint32 *)frame->user_esp;
	uint32 *code_ptr;
	uint32 *regs_ptr;
	struct vregs regs;
	uint32 stack_buf[6];
	status_t status;

	if (frame->orig_eax >= 0) {
		// we're coming from a syscall
		if ((status_t)frame->eax == EINTR && (sa->sa_flags & SA_RESTART) != 0) {
			TRACE(("### restarting syscall %d after signal %d\n", frame->orig_eax, sig));
			frame->eax = frame->orig_eax;
			frame->edx = frame->orig_edx;
			frame->eip -= 2;
				// undos the "int $99" syscall interrupt (so that it'll be called again)
		}
	}

	// start stuffing stuff on the user stack
	stack_ptr = (uint32 *)frame->user_esp;

	// store the saved regs onto the user stack
	stack_ptr -= ROUNDUP(sizeof(struct vregs) / 4, 4);
	regs_ptr = stack_ptr;
	regs.eip = frame->eip;
	regs.eflags = frame->flags;
	regs.eax = frame->eax;
	regs.ecx = frame->ecx;
	regs.edx = frame->edx;
	regs.esp = frame->esp;
	regs._reserved_1 = frame->user_esp;
	regs._reserved_2[0] = frame->edi;
	regs._reserved_2[1] = frame->esi;
	regs._reserved_2[2] = frame->ebp;
	i386_fsave((void *)(&regs.xregs));
	
	status = user_memcpy(stack_ptr, &regs, sizeof(regs));
	if (status < B_OK)
		return status;

	// now store a code snippet on the stack
	stack_ptr -= ((uint32)i386_end_return_from_signal - (uint32)i386_return_from_signal) / 4;
	code_ptr = stack_ptr;
	status = user_memcpy(code_ptr, i386_return_from_signal,
		((uint32)i386_end_return_from_signal - (uint32)i386_return_from_signal));
	if (status < 0)
		return status;

	// now set up the final part
	stack_buf[0] = (uint32)code_ptr;	// return address when sa_handler done
	stack_buf[1] = sig;					// first argument to sa_handler
	stack_buf[2] = (uint32)sa->sa_userdata;// second argument to sa_handler
	stack_buf[3] = (uint32)regs_ptr;	// third argument to sa_handler

	stack_buf[4] = sig_mask;			// Old signal mask to restore
	stack_buf[5] = (uint32)regs_ptr;	// Int frame + extra regs to restore

	stack_ptr -= sizeof(stack_buf) / 4;

	status = user_memcpy(stack_ptr, stack_buf, sizeof(stack_buf));
	if (status < 0)
		return status;

	frame->user_esp = (uint32)stack_ptr;
	frame->eip = (uint32)sa->sa_handler;

	return B_OK;
}


int64
arch_restore_signal_frame(void)
{
	struct thread *thread = thread_get_current_thread();
	struct iframe *frame = i386_get_current_iframe();
	uint32 *stack;
	struct vregs *regs;

	TRACE(("### arch_restore_signal_frame: entry\n"));

	stack = (uint32 *)frame->user_esp;
	atomic_set(&thread->sig_block_mask, stack[0]);
	regs = (struct vregs *)stack[1];

	frame->eip = regs->eip;
	frame->flags = regs->eflags;
	frame->eax = regs->eax;
	frame->ecx = regs->ecx;
	frame->edx = regs->edx;
	frame->esp = regs->esp;
	frame->user_esp = regs->_reserved_1;
	frame->edi = regs->_reserved_2[0];
	frame->esi = regs->_reserved_2[1];
	frame->ebp = regs->_reserved_2[2];

	i386_frstor((void *)(&regs->xregs));

	TRACE(("### arch_restore_signal_frame: exit\n"));

	frame->orig_eax = -1;	/* disable syscall checks */

	return (int64)frame->eax | ((int64)frame->edx << 32);
}


void
arch_check_syscall_restart(struct thread *thread)
{
	struct iframe *frame = i386_get_current_iframe();
	if (frame == NULL) {
		// this thread is obviously new; we didn't come from an interrupt
		return;
	}

	if ((status_t)frame->orig_eax >= 0 && (status_t)frame->eax == EINTR) {
		frame->eax = frame->orig_eax;
		frame->edx = frame->orig_edx;
		frame->eip -= 2;
			// undoes the "int $99" syscall interrupt (so that it'll be called again)
	}
}


/**	Saves everything needed to restore the frame in the child fork in the
 *	arch_fork_arg structure to be passed to arch_restore_fork_frame().
 *	Also makes sure to return the right value.
 */

void
arch_store_fork_frame(struct arch_fork_arg *arg)
{
	struct iframe *frame = i386_get_current_iframe();

	// we need to copy the threads current iframe
	arg->iframe = *frame;

	// we also want fork() to return 0 for the child
	arg->iframe.eax = 0;
}


/** Restores the frame from a forked team as specified by the provided
 *	arch_fork_arg structure.
 *	Needs to be called from within the child team, ie. instead of
 *	arch_thread_enter_uspace() as thread "starter".
 *	This function does not return to the caller, but will enter userland
 *	in the child team at the same position where the parent team left of.
 */

void
arch_restore_fork_frame(struct arch_fork_arg *arg)
{
	struct thread *thread = thread_get_current_thread();

	disable_interrupts();

	i386_set_tss_and_kstack(thread->kernel_stack_base + KERNEL_STACK_SIZE);

	// set the CPU dependent GDT entry for TLS (set the current %fs register)
	set_tls_context(thread);

	i386_restore_frame_from_syscall(arg->iframe);
}
