/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <arch/debug.h>

#include <stdio.h>
#include <stdlib.h>

#include <cpu.h>
#include <debug.h>
#include <debug_heap.h>
#include <elf.h>
#include <kernel.h>
#include <kimage.h>
#include <thread.h>
#include <vm.h>
#include <vm_types.h>

#include <arch_cpu.h>


struct stack_frame {
	struct stack_frame	*previous;
	addr_t				return_address;
};

#define NUM_PREVIOUS_LOCATIONS 32


static bool
already_visited(uint32 *visited, int32 *_last, int32 *_num, uint32 ebp)
{
	int32 last = *_last;
	int32 num = *_num;
	int32 i;

	for (i = 0; i < num; i++) {
		if (visited[(NUM_PREVIOUS_LOCATIONS + last - i) % NUM_PREVIOUS_LOCATIONS] == ebp)
			return true;
	}

	*_last = last = (last + 1) % NUM_PREVIOUS_LOCATIONS;
	visited[last] = ebp;

	if (num < NUM_PREVIOUS_LOCATIONS)
		*_num = num + 1;

	return false;
}


/*!	Safe to be called only from outside the debugger.
*/
static status_t
get_next_frame_no_debugger(addr_t ebp, addr_t *_next, addr_t *_eip)
{
	// TODO: Do this more efficiently in assembly.
	stack_frame frame;
	if (user_memcpy(&frame, (void*)ebp, sizeof(frame)) != B_OK)
		return B_BAD_ADDRESS;

	*_eip = frame.return_address;
	*_next = (addr_t)frame.previous;

	return B_OK;
}


/*!	Safe to be called only from inside the debugger.
*/
static status_t
get_next_frame_debugger(addr_t ebp, addr_t *_next, addr_t *_eip)
{
	stack_frame frame;
	if (debug_memcpy(&frame, (void*)ebp, sizeof(frame)) != B_OK)
		return B_BAD_ADDRESS;

	*_eip = frame.return_address;
	*_next = (addr_t)frame.previous;

	return B_OK;
}


static status_t
lookup_symbol(struct thread* thread, addr_t address, addr_t *_baseAddress,
	const char **_symbolName, const char **_imageName, bool *_exactMatch)
{
	status_t status = B_ENTRY_NOT_FOUND;

	if (IS_KERNEL_ADDRESS(address)) {
		// a kernel symbol
		status = elf_debug_lookup_symbol_address(address, _baseAddress,
			_symbolName, _imageName, _exactMatch);
	} else if (thread != NULL && thread->team != NULL) {
		// try a lookup using the userland runtime loader structures
		status = elf_debug_lookup_user_symbol_address(thread->team, address,
			_baseAddress, _symbolName, _imageName, _exactMatch);

		if (status != B_OK) {
			// try to locate the image in the images loaded into user space
			status = image_debug_lookup_user_symbol_address(thread->team,
				address, _baseAddress, _symbolName, _imageName, _exactMatch);
		}
	}

	return status;
}


static void
set_debug_argument_variable(int32 index, uint64 value)
{
	char name[8];
	snprintf(name, sizeof(name), "_arg%ld", index);
	set_debug_variable(name, value);
}


static status_t
print_demangled_call(const char* image, const char* symbol, addr_t args,
	bool noObjectMethod, bool addDebugVariables)
{
	static const size_t kBufferSize = 256;
	char* buffer = (char*)debug_malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	bool isObjectMethod;
	const char* name = debug_demangle_symbol(symbol, buffer, kBufferSize,
		&isObjectMethod);
	if (name == NULL) {
		debug_free(buffer);
		return B_ERROR;
	}

	uint32* arg = (uint32*)args;

	if (noObjectMethod)
		isObjectMethod = false;
	if (isObjectMethod) {
		const char* lastName = strrchr(name, ':') - 1;
		int namespaceLength = lastName - name;

		kprintf("<%s> %.*s<\33[32m%p\33[0m>%s", image, namespaceLength, name,
			*(uint32 **)arg, lastName);
		if (addDebugVariables)
			set_debug_variable("_this", *(uint32 *)arg);
		arg++;
	} else
		kprintf("<%s> %s", image, name);

	kprintf("(");

	size_t length;
	int32 type, i = 0;
	uint32 cookie = 0;
	while (debug_get_next_demangled_argument(&cookie, symbol, buffer,
			kBufferSize, &type, &length) == B_OK) {
		if (i++ > 0)
			kprintf(", ");

		// retrieve value and type identifier

		uint64 value;

		switch (type) {
			case B_INT64_TYPE:
				value = *(int64*)arg;
				kprintf("int64: \33[34m%Ld\33[0m", value);
				break;
			case B_INT32_TYPE:
				value = *(int32*)arg;
				kprintf("int32: \33[34m%ld\33[0m", (int32)value);
				break;
			case B_INT16_TYPE:
				value = *(int16*)arg;
				kprintf("int16: \33[34m%d\33[0m", (int16)value);
				break;
			case B_INT8_TYPE:
				value = *(int8*)arg;
				kprintf("int8: \33[34m%d\33[0m", (int8)value);
				break;
			case B_UINT64_TYPE:
				value = *(uint64*)arg;
				kprintf("uint64: \33[34m%#Lx\33[0m", value);
				if (value < 0x100000)
					kprintf(" (\33[34m%Lu\33[0m)", value);
				break;
			case B_UINT32_TYPE:
				value = *(uint32*)arg;
				kprintf("uint32: \33[34m%#lx\33[0m", (uint32)value);
				if (value < 0x100000)
					kprintf(" (\33[34m%lu\33[0m)", (uint32)value);
				break;
			case B_UINT16_TYPE:
				value = *(uint16*)arg;
				kprintf("uint16: \33[34m%#x\33[0m (\33[34m%u\33[0m)",
					(uint16)value, (uint16)value);
				break;
			case B_UINT8_TYPE:
				value = *(uint8*)arg;
				kprintf("uint8: \33[34m%#x\33[0m (\33[34m%u\33[0m)",
					(uint8)value, (uint8)value);
				break;
			case B_BOOL_TYPE:
				value = *(uint8*)arg;
				kprintf("\33[34m%s\33[0m", value ? "true" : "false");
				break;
			default:
				if (buffer[0])
					kprintf("%s: ", buffer);

				if (length == 4) {
					value = *(uint32*)arg;
					if (value == 0
						&& (type == B_POINTER_TYPE || type == B_REF_TYPE))
						kprintf("NULL");
					else
						kprintf("\33[34m%#lx\33[0m", (uint32)value);
					break;
				}

				if (length == 8)
					value = *(uint64*)arg;
				else
					value = (uint64)arg;
				kprintf("\33[34m%#Lx\33[0m", value);
				break;
		}

		if (type == B_STRING_TYPE) {
			if (value == 0)
				kprintf(" \33[31m\"<NULL>\"\33[0m");
			else if (debug_strlcpy(buffer, (char*)value, kBufferSize) < B_OK)
				kprintf(" \33[31m\"<???>\"\33[0m");
			else
				kprintf(" \33[36m\"%s\"\33[0m", buffer);
		}

		if (addDebugVariables)
			set_debug_argument_variable(i, value);
		arg = (uint32*)((uint8*)arg + length);
	}

	debug_free(buffer);

	kprintf(")");
	return B_OK;
}


static void
print_stack_frame(struct thread *thread, addr_t eip, addr_t ebp, addr_t nextEbp,
	int32 callIndex, bool demangle)
{
	const char *symbol, *image;
	addr_t baseAddress;
	bool exactMatch;
	status_t status;
	addr_t diff;

	diff = nextEbp - ebp;

	// kernel space/user space switch
	if (diff & 0x80000000)
		diff = 0;

	status = lookup_symbol(thread, eip, &baseAddress, &symbol, &image,
		&exactMatch);

	kprintf("%2ld %08lx (+%4ld) %08lx   ", callIndex, ebp, diff, eip);

	if (status == B_OK) {
		if (exactMatch && demangle) {
			status = print_demangled_call(image, symbol, nextEbp + 8, false,
				false);
		}

		if (!exactMatch || !demangle || status != B_OK) {
			if (symbol != NULL) {
				kprintf("<%s>:%s%s", image, symbol,
					exactMatch ? "" : " (nearest)");
			} else
				kprintf("<%s@%p>:unknown", image, (void *)baseAddress);
		}

		kprintf(" + 0x%04lx\n", eip - baseAddress);
	} else {
		vm_area *area = NULL;
		if (thread != NULL && thread->team != NULL
			&& thread->team->address_space != NULL) {
			area = vm_area_lookup(thread->team->address_space, eip);
		}
		if (area != NULL) {
			kprintf("%ld:%s@%p + %#lx\n", area->id, area->name,
				(void*)area->base, eip - area->base);
		} else
			kprintf("\n");
	}
}


static void
print_iframe(struct iframe *frame)
{
	bool isUser = IFRAME_IS_USER(frame);
	kprintf("%s iframe at %p (end = %p)\n", isUser ? "user" : "kernel", frame,
		isUser ? (uint32*)(frame + 1) : &frame->user_esp);

	kprintf(" eax 0x%-9lx    ebx 0x%-9lx     ecx 0x%-9lx  edx 0x%lx\n",
		frame->eax, frame->ebx, frame->ecx, frame->edx);
	kprintf(" esi 0x%-9lx    edi 0x%-9lx     ebp 0x%-9lx  esp 0x%lx\n",
		frame->esi, frame->edi, frame->ebp, frame->esp);
	kprintf(" eip 0x%-9lx eflags 0x%-9lx", frame->eip, frame->flags);
	if (isUser) {
		// from user space
		kprintf("user esp 0x%lx", frame->user_esp);
	}
	kprintf("\n");
	kprintf(" vector: 0x%lx, error code: 0x%lx\n", frame->vector,
		frame->error_code);
}


static void
setup_for_thread(char *arg, struct thread **_thread, uint32 *_ebp,
	uint32 *_oldPageDirectory)
{
	struct thread *thread = NULL;

	if (arg != NULL) {
		thread_id id = strtoul(arg, NULL, 0);
		thread = thread_get_thread_struct_locked(id);
		if (thread == NULL) {
			kprintf("could not find thread %ld\n", id);
			return;
		}

		if (id != thread_get_current_thread_id()) {
			// switch to the page directory of the new thread to be
			// able to follow the stack trace into userland
			addr_t newPageDirectory = (addr_t)x86_next_page_directory(
				thread_get_current_thread(), thread);

			if (newPageDirectory != 0) {
				read_cr3(*_oldPageDirectory);
				write_cr3(newPageDirectory);
			}

			// read %ebp from the thread's stack stored by a pushad
			*_ebp = thread->arch_info.current_stack.esp[2];
		} else
			thread = NULL;
	}

	if (thread == NULL) {
		// if we don't have a thread yet, we want the current one
		// (ebp has been set by the caller for this case already)
		thread = thread_get_current_thread();
	}

	*_thread = thread;
}


static bool
is_double_fault_stack_address(int32 cpu, addr_t address)
{
	size_t size;
	addr_t bottom = (addr_t)x86_get_double_fault_stack(cpu, &size);
	return address >= bottom && address < bottom + size;
}


static bool
is_kernel_stack_address(struct thread* thread, addr_t address)
{
	// We don't have a thread pointer in the early boot process, but then we are
	// on the kernel stack for sure.
	if (thread == NULL)
		return IS_KERNEL_ADDRESS(address);

	return (address >= thread->kernel_stack_base
			&& address < thread->kernel_stack_top)
		|| (thread->cpu != NULL
			&& is_double_fault_stack_address(thread->cpu->cpu_num, address));
}


static bool
is_iframe(struct thread* thread, addr_t frame)
{
	if (!is_kernel_stack_address(thread, frame))
		return false;

	addr_t previousFrame = *(addr_t*)frame;
	return ((previousFrame & ~IFRAME_TYPE_MASK) == 0 && previousFrame != 0);
}


static struct iframe *
find_previous_iframe(struct thread *thread, addr_t frame)
{
	// iterate backwards through the stack frames, until we hit an iframe
	while (is_kernel_stack_address(thread, frame)) {
		if (is_iframe(thread, frame))
			return (struct iframe*)frame;

		frame = *(addr_t*)frame;
	}

	return NULL;
}


static struct iframe*
get_previous_iframe(struct thread* thread, struct iframe* frame)
{
	if (frame == NULL)
		return NULL;

	return find_previous_iframe(thread, frame->ebp);
}


static struct iframe*
get_current_iframe(struct thread* thread)
{
	if (thread == thread_get_current_thread())
		return i386_get_current_iframe();

	addr_t ebp = thread->arch_info.current_stack.esp[2];
		// NOTE: This doesn't work, if the thread is running (on another CPU).
	return find_previous_iframe(thread, ebp);
}


uint32*
find_debug_variable(const char* variableName, bool& settable)
{
	struct iframe* frame = get_current_iframe(debug_get_debugged_thread());
	if (frame == NULL)
		return NULL;

	settable = false;

	if (strcmp(variableName, "gs") == 0) {
		return &frame->gs;
	} else if (strcmp(variableName, "fs") == 0) {
		return &frame->fs;
	} else if (strcmp(variableName, "es") == 0) {
		return &frame->es;
	} else if (strcmp(variableName, "ds") == 0) {
		return &frame->ds;
	} else if (strcmp(variableName, "cs") == 0) {
		return &frame->cs;
	} else if (strcmp(variableName, "edi") == 0) {
		settable = true;
		return &frame->edi;
	} else if (strcmp(variableName, "esi") == 0) {
		settable = true;
		return &frame->esi;
	} else if (strcmp(variableName, "ebp") == 0) {
		settable = true;
		return &frame->ebp;
	} else if (strcmp(variableName, "esp") == 0) {
		settable = true;
		return &frame->esp;
	} else if (strcmp(variableName, "ebx") == 0) {
		settable = true;
		return &frame->ebx;
	} else if (strcmp(variableName, "edx") == 0) {
		settable = true;
		return &frame->edx;
	} else if (strcmp(variableName, "ecx") == 0) {
		settable = true;
		return &frame->ecx;
	} else if (strcmp(variableName, "eax") == 0) {
		settable = true;
		return &frame->eax;
	} else if (strcmp(variableName, "orig_eax") == 0) {
		settable = true;
		return &frame->orig_eax;
	} else if (strcmp(variableName, "orig_edx") == 0) {
		settable = true;
		return &frame->orig_edx;
	} else if (strcmp(variableName, "eip") == 0) {
		settable = true;
		return &frame->eip;
	} else if (strcmp(variableName, "eflags") == 0) {
		settable = true;
		return &frame->flags;
	}

	if (IFRAME_IS_USER(frame)) {
		if (strcmp(variableName, "user_esp") == 0) {
			settable = true;
			return &frame->user_esp;
		} else if (strcmp(variableName, "user_ss") == 0) {
			return &frame->user_ss;
		}
	}

	return NULL;
}


static int
stack_trace(int argc, char **argv)
{
	static const char* usage = "usage: %s [-d] [ <thread id> ]\n"
		"Prints a stack trace for the current, respectively the specified\n"
		"thread.\n"
		"  -d           -  Disables the demangling of the symbols.\n"
		"  <thread id>  -  The ID of the thread for which to print the stack\n"
		"                  trace.\n";
	bool demangle = true;
	int32 threadIndex = 1;
	if (argc > 1 && !strcmp(argv[1], "-d")) {
		demangle = false;
		threadIndex++;
	}

	if (argc > threadIndex + 1
		|| (argc == 2 && strcmp(argv[1], "--help") == 0)) {
		kprintf(usage, argv[0]);
		return 0;
	}

	uint32 previousLocations[NUM_PREVIOUS_LOCATIONS];
	struct thread *thread = NULL;
	addr_t oldPageDirectory = 0;
	uint32 ebp = x86_read_ebp();
	int32 num = 0, last = 0;

	setup_for_thread(argc == threadIndex + 1 ? argv[threadIndex] : NULL,
		&thread, &ebp, &oldPageDirectory);

	if (thread != NULL) {
		kprintf("stack trace for thread %ld \"%s\"\n", thread->id,
			thread->name);

		kprintf("    kernel stack: %p to %p\n",
			(void *)thread->kernel_stack_base,
			(void *)(thread->kernel_stack_top));
		if (thread->user_stack_base != 0) {
			kprintf("      user stack: %p to %p\n",
				(void *)thread->user_stack_base,
				(void *)(thread->user_stack_base + thread->user_stack_size));
		}
	}

	kprintf("frame               caller     <image>:function + offset\n");

	bool onKernelStack = true;

	for (int32 callIndex = 0;; callIndex++) {
		onKernelStack = onKernelStack
			&& is_kernel_stack_address(thread, ebp);

		if (onKernelStack && is_iframe(thread, ebp)) {
			struct iframe *frame = (struct iframe *)ebp;

			print_iframe(frame);
			print_stack_frame(thread, frame->eip, ebp, frame->ebp, callIndex,
				demangle);

 			ebp = frame->ebp;
		} else {
			addr_t eip, nextEbp;

			if (get_next_frame_debugger(ebp, &nextEbp, &eip) != B_OK) {
				kprintf("%08lx -- read fault\n", ebp);
				break;
			}

			if (eip == 0 || ebp == 0)
				break;

			print_stack_frame(thread, eip, ebp, nextEbp, callIndex, demangle);
			ebp = nextEbp;
		}

		if (already_visited(previousLocations, &last, &num, ebp)) {
			kprintf("circular stack frame: %p!\n", (void *)ebp);
			break;
		}
		if (ebp == 0)
			break;
	}

	if (oldPageDirectory != 0) {
		// switch back to the previous page directory to no cause any troubles
		write_cr3(oldPageDirectory);
	}

	return 0;
}


static void
print_call(struct thread *thread, addr_t eip, addr_t ebp, addr_t nextEbp,
	int32 argCount)
{
	const char *symbol, *image;
	addr_t baseAddress;
	bool exactMatch;
	status_t status;
	bool demangled = false;
	int32 *arg = (int32 *)(nextEbp + 8);

	status = lookup_symbol(thread, eip, &baseAddress, &symbol, &image,
		&exactMatch);

	kprintf("%08lx %08lx   ", ebp, eip);

	if (status == B_OK) {
		if (symbol != NULL) {
			if (exactMatch && (argCount == 0 || argCount == -1)) {
				status = print_demangled_call(image, symbol, (addr_t)arg,
					argCount == -1, true);
				if (status == B_OK)
					demangled = true;
			}
			if (!demangled) {
				kprintf("<%s>:%s%s", image, symbol,
					exactMatch ? "" : " (nearest)");
			}
		} else {
			kprintf("<%s@%p>:unknown + 0x%04lx", image,
				(void *)baseAddress, eip - baseAddress);
		}
	} else {
		vm_area *area = NULL;
		if (thread->team->address_space != NULL)
			area = vm_area_lookup(thread->team->address_space, eip);
		if (area != NULL) {
			kprintf("%ld:%s@%p + %#lx", area->id, area->name,
				(void *)area->base, eip - area->base);
		}
	}

	if (!demangled) {
		kprintf("(");

		for (int32 i = 0; i < argCount; i++) {
			if (i > 0)
				kprintf(", ");
			kprintf("%#lx", *arg);
			if (*arg > -0x10000 && *arg < 0x10000)
				kprintf(" (%ld)", *arg);

			set_debug_argument_variable(i + 1, *(uint32 *)arg);
			arg++;
		}

		kprintf(")\n");
	} else
		kprintf("\n");

	set_debug_variable("_frame", nextEbp);
}


static int
show_call(int argc, char **argv)
{
	static const char* usage
		= "usage: %s [ <thread id> ] <call index> [ -<arg count> ]\n"
		"Prints a function call with parameters of the current, respectively\n"
		"the specified thread.\n"
		"  <thread id>   -  The ID of the thread for which to print the call.\n"
		"  <call index>  -  The index of the call in the stack trace.\n"
		"  <arg count>   -  The number of call arguments to print (use 'c' to\n"
		"                   force the C++ demangler to use class methods,\n"
		"                   use 'd' to disable demangling).\n";
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		kprintf(usage, argv[0]);
		return 0;
	}

	struct thread *thread = NULL;
	addr_t oldPageDirectory = 0;
	addr_t ebp = x86_read_ebp();
	int32 argCount = 0;

	if (argc >= 2 && argv[argc - 1][0] == '-') {
		if (argv[argc - 1][1] == 'c')
			argCount = -1;
		else if (argv[argc - 1][1] == 'd')
			argCount = -2;
		else
			argCount = strtoul(argv[argc - 1] + 1, NULL, 0);

		if (argCount < -2 || argCount > 16) {
			kprintf("Invalid argument count \"%ld\".\n", argCount);
			return 0;
		}
		argc--;
	}

	if (argc < 2 || argc > 3) {
		kprintf(usage, argv[0]);
		return 0;
	}

	setup_for_thread(argc == 3 ? argv[1] : NULL, &thread, &ebp,
		&oldPageDirectory);

	int32 callIndex = strtoul(argv[argc == 3 ? 2 : 1], NULL, 0);

	if (thread != NULL)
		kprintf("thread %ld, %s\n", thread->id, thread->name);

	bool onKernelStack = true;

	for (int32 index = 0; index <= callIndex; index++) {
		onKernelStack = onKernelStack
			&& is_kernel_stack_address(thread, ebp);

		if (onKernelStack && is_iframe(thread, ebp)) {
			struct iframe *frame = (struct iframe *)ebp;

			if (index == callIndex)
				print_call(thread, frame->eip, ebp, frame->ebp, argCount);

 			ebp = frame->ebp;
		} else {
			addr_t eip, nextEbp;

			if (get_next_frame_debugger(ebp, &nextEbp, &eip) != B_OK) {
				kprintf("%08lx -- read fault\n", ebp);
				break;
			}

			if (eip == 0 || ebp == 0)
				break;

			if (index == callIndex)
				print_call(thread, eip, ebp, nextEbp, argCount);

			ebp = nextEbp;
		}

		if (ebp == 0)
			break;
	}

	if (oldPageDirectory != 0) {
		// switch back to the previous page directory to not cause any troubles
		write_cr3(oldPageDirectory);
	}

	return 0;
}


static int
dump_iframes(int argc, char **argv)
{
	static const char* usage = "usage: %s [ <thread id> ]\n"
		"Prints the iframe stack for the current, respectively the specified\n"
		"thread.\n"
		"  <thread id>  -  The ID of the thread for which to print the iframe\n"
		"                  stack.\n";
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		kprintf(usage, argv[0]);
		return 0;
	}

	struct thread *thread = NULL;

	if (argc < 2) {
		thread = thread_get_current_thread();
	} else if (argc == 2) {
		thread_id id = strtoul(argv[1], NULL, 0);
		thread = thread_get_thread_struct_locked(id);
		if (thread == NULL) {
			kprintf("could not find thread %ld\n", id);
			return 0;
		}
	} else if (argc > 2) {
		kprintf(usage, argv[0]);
		return 0;
	}

	if (thread != NULL)
		kprintf("iframes for thread %ld \"%s\"\n", thread->id, thread->name);

	struct iframe* frame = find_previous_iframe(thread, x86_read_ebp());
	while (frame != NULL) {
		print_iframe(frame);
		frame = get_previous_iframe(thread, frame);
	}

	return 0;
}


static bool
is_calling(struct thread *thread, addr_t eip, const char *pattern,
	addr_t start, addr_t end)
{
	if (pattern == NULL)
		return eip >= start && eip < end;

	const char *symbol;
	if (lookup_symbol(thread, eip, NULL, &symbol, NULL, NULL) != B_OK)
		return false;

	return strstr(symbol, pattern);
}


static int
cmd_in_context(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	// get the thread ID
	const char* commandLine = argv[1];
	char threadIDString[16];
	if (parse_next_debug_command_argument(&commandLine, threadIDString,
			sizeof(threadIDString)) != B_OK) {
		kprintf("Failed to parse thread ID.\n");
		return 0;
	}

	if (commandLine == NULL) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	uint64 threadID;
	if (!evaluate_debug_expression(threadIDString, &threadID, false))
		return 0;

	// get the thread
	struct thread* thread = thread_get_thread_struct_locked(threadID);
	if (thread == NULL) {
		kprintf("Could not find thread with ID \"%s\".\n", threadIDString);
		return 0;
	}

	// switch the page directory, if necessary
	addr_t oldPageDirectory = 0;
	if (thread != thread_get_current_thread()) {
		addr_t newPageDirectory = (addr_t)x86_next_page_directory(
			thread_get_current_thread(), thread);

		if (newPageDirectory != 0) {
			read_cr3(oldPageDirectory);
			write_cr3(newPageDirectory);
		}
	}

	struct thread* previousThread = debug_set_debugged_thread(thread);

	// execute the command
	evaluate_debug_command(commandLine);

	debug_set_debugged_thread(previousThread);

	// reset the page directory
	if (oldPageDirectory)
		write_cr3(oldPageDirectory);

	return 0;
}


//	#pragma mark -


void
arch_debug_stack_trace(void)
{
	stack_trace(0, NULL);
}


bool
arch_debug_contains_call(struct thread *thread, const char *symbol,
	addr_t start, addr_t end)
{
	addr_t ebp;
	if (thread == thread_get_current_thread())
		ebp = x86_read_ebp();
	else
		ebp = thread->arch_info.current_stack.esp[2];

	for (;;) {
		if (!is_kernel_stack_address(thread, ebp))
			break;

		if (is_iframe(thread, ebp)) {
			struct iframe *frame = (struct iframe *)ebp;

			if (is_calling(thread, frame->eip, symbol, start, end))
				return true;

 			ebp = frame->ebp;
		} else {
			addr_t eip, nextEbp;

			if (get_next_frame_no_debugger(ebp, &nextEbp, &eip) != B_OK
				|| eip == 0 || ebp == 0)
				break;

			if (is_calling(thread, eip, symbol, start, end))
				return true;

			ebp = nextEbp;
		}

		if (ebp == 0)
			break;
	}

	return false;
}


void *
arch_debug_get_caller(void)
{
	struct stack_frame *frame = (struct stack_frame *)x86_read_ebp();
	return (void *)frame->previous->return_address;
}


/*!	Captures a stack trace (the return addresses) of the current thread.
	\param returnAddresses The array the return address shall be written to.
	\param maxCount The maximum number of return addresses to be captured.
	\param skipIframes The number of interrupt frames that shall be skipped. If
		greater than 0, \a skipFrames is ignored.
	\param skipFrames The number of stack frames that shall be skipped.
	\param userOnly If \c true, only userland return addresses are captured.
	\return The number of return addresses written to the given array.
*/
int32
arch_debug_get_stack_trace(addr_t* returnAddresses, int32 maxCount,
	int32 skipIframes, int32 skipFrames, bool userOnly)
{
	// Keep skipping normal stack frames until we've skipped the iframes we're
	// supposed to skip.
	if (skipIframes > 0) {
		skipFrames = INT_MAX;
	} else {
		// always skip our own frame
		skipFrames++;
	}

	struct thread* thread = thread_get_current_thread();
	int32 count = 0;
	addr_t ebp = x86_read_ebp();
	bool onKernelStack = true;

	while (ebp != 0 && count < maxCount) {
		onKernelStack = onKernelStack
			&& is_kernel_stack_address(thread, ebp);

		addr_t eip;
		addr_t nextEbp;

		if (onKernelStack && is_iframe(thread, ebp)) {
			struct iframe *frame = (struct iframe*)ebp;
			eip = frame->eip;
 			nextEbp = frame->ebp;

			if (skipIframes > 0) {
				if (--skipIframes == 0)
					skipFrames = 0;
			}
		} else {
			if (get_next_frame_no_debugger(ebp, &nextEbp, &eip) != B_OK)
				break;
		}

		if (skipFrames <= 0 && (!userOnly || IS_USER_ADDRESS(ebp)))
			returnAddresses[count++] = eip;
		else
			skipFrames--;

		ebp = nextEbp;
	}

	return count;
}


/*!	Returns the program counter of the currently debugged (respectively this)
	thread where the innermost interrupts happened. \a _isSyscall, if specified,
	is set to whether this interrupt frame was created by a syscall. Returns
	\c NULL, if there's no such frame or a problem occurred retrieving it;
	\a _isSyscall won't be set in this case.
*/
void*
arch_debug_get_interrupt_pc(bool* _isSyscall)
{
	struct iframe* frame = get_current_iframe(debug_get_debugged_thread());
	if (frame == NULL)
		return NULL;

	if (_isSyscall != NULL)
		*_isSyscall = frame->vector == 99;

	return (void*)(addr_t)frame->eip;
}


/*!	Sets the current thread to \c NULL.
	Invoked in the kernel debugger only.
*/
void
arch_debug_unset_current_thread(void)
{
	write_dr3(NULL);
}


bool
arch_is_debug_variable_defined(const char* variableName)
{
	bool settable;
	return find_debug_variable(variableName, settable);
}


status_t
arch_set_debug_variable(const char* variableName, uint64 value)
{
	bool settable;
	uint32* variable = find_debug_variable(variableName, settable);
	if (variable == NULL)
		return B_ENTRY_NOT_FOUND;

	if (!settable)
		return B_NOT_ALLOWED;

	*variable = (uint32)value;
	return B_OK;
}


status_t
arch_get_debug_variable(const char* variableName, uint64* value)
{
	bool settable;
	uint32* variable = find_debug_variable(variableName, settable);
	if (variable == NULL)
		return B_ENTRY_NOT_FOUND;

	*value = *variable;
	return B_OK;
}


status_t
arch_debug_init(kernel_args *args)
{
	// at this stage, the debugger command system is alive

	add_debugger_command("where", &stack_trace, "Same as \"sc\"");
	add_debugger_command("bt", &stack_trace, "Same as \"sc\" (as in gdb)");
	add_debugger_command("sc", &stack_trace,
		"Stack crawl for current thread (or any other)");
	add_debugger_command("iframe", &dump_iframes,
		"Dump iframes for the specified thread");
	add_debugger_command("call", &show_call, "Show call with arguments");
	add_debugger_command_etc("in_context", &cmd_in_context,
		"Executes a command in the context of a given thread",
		"<thread ID> <command> ...\n"
		"Executes a command in the context of a given thread.\n",
		B_KDEBUG_DONT_PARSE_ARGUMENTS);

	return B_NO_ERROR;
}

