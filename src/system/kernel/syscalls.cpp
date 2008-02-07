/*
 * Copyright 2004-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/* Big case statement for dispatching syscalls */

#include <kernel.h>
#include <ksyscalls.h>
#include <syscalls.h>
#include <generic_syscall.h>
#include <debug.h>
#include <int.h>
#include <elf.h>
#include <vfs.h>
#include <vm.h>
#include <thread.h>
#include <sem.h>
#include <port.h>
#include <cpu.h>
#include <arch_config.h>
#include <disk_device_manager/ddm_userland_interface.h>
#include <sys/resource.h>
#include <fs/fd.h>
#include <fs/node_monitor.h>
#include <kimage.h>
#include <ksignal.h>
#include <real_time_clock.h>
#include <safemode.h>
#include <system_info.h>
#include <tracing.h>
#include <user_atomic.h>
#include <arch/system_info.h>
#include <messaging.h>
#include <frame_buffer_console.h>
#include <wait_for_objects.h>

#include <malloc.h>
#include <string.h>

#include "syscall_numbers.h"


typedef struct generic_syscall generic_syscall;

struct generic_syscall {
	list_link		link;
	char			subsystem[B_FILE_NAME_LENGTH];
	syscall_hook	hook;
	uint32			version;
	uint32			flags;
	generic_syscall	*previous;
};

static struct mutex sGenericSyscallLock;
static struct list sGenericSyscalls;


static generic_syscall *
find_generic_syscall(const char *subsystem)
{
	generic_syscall *syscall = NULL;

	ASSERT_LOCKED_MUTEX(&sGenericSyscallLock);

	while ((syscall = (generic_syscall*)list_get_next_item(&sGenericSyscalls,
			syscall)) != NULL) {
		if (!strcmp(syscall->subsystem, subsystem))
			return syscall;
	}

	return NULL;
}


/**	Calls the generic syscall subsystem if any.
 *	Also handles the special generic syscall function \c B_SYSCALL_INFO.
 *	Returns \c B_NAME_NOT_FOUND if either the subsystem was not found, or
 *	the subsystem does not support the requested function.
 *	All other return codes are depending on the generic syscall implementation.
 */

static inline status_t
_user_generic_syscall(const char *userSubsystem, uint32 function,
	void *buffer, size_t bufferSize)
{
	char subsystem[B_FILE_NAME_LENGTH];
	generic_syscall *syscall;
	status_t status = B_NAME_NOT_FOUND;

	if (!IS_USER_ADDRESS(userSubsystem)
		|| user_strlcpy(subsystem, userSubsystem, sizeof(subsystem)) < B_OK)
		return B_BAD_ADDRESS;

	//dprintf("generic_syscall(subsystem = \"%s\", function = %lu)\n", subsystem, function);

	mutex_lock(&sGenericSyscallLock);

	syscall = find_generic_syscall(subsystem);
	if (syscall == NULL)
		goto out;

	if (function >= B_RESERVED_SYSCALL_BASE) {
		if (function != B_SYSCALL_INFO) {
			// this is all we know
			status = B_NAME_NOT_FOUND;
			goto out;
		}

		// special info syscall
		if (bufferSize != sizeof(uint32))
			status = B_BAD_VALUE;
		else {
			uint32 requestedVersion;

			// retrieve old version
			status = user_memcpy(&requestedVersion, buffer, sizeof(uint32));
			if (status == B_OK && requestedVersion != 0 && requestedVersion < syscall->version)
				status = B_BAD_TYPE;

			// return current version
			if (status == B_OK)
				status = user_memcpy(buffer, &syscall->version, sizeof(uint32));
		}
	} else {
		while (syscall != NULL) {
			generic_syscall *next;

			mutex_unlock(&sGenericSyscallLock);

			status = syscall->hook(subsystem, function, buffer, bufferSize);

			mutex_lock(&sGenericSyscallLock);
			if (status != B_BAD_HANDLER)
				break;

			// the syscall may have been removed in the mean time
			next = find_generic_syscall(subsystem);
			if (next == syscall)
				syscall = syscall->previous;
			else
				syscall = next;
		}

		if (syscall == NULL)
			status = B_NAME_NOT_FOUND;
	}

out:
	mutex_unlock(&sGenericSyscallLock);
	return status;
}


static inline int
_user_is_computer_on(void)
{
	return 1;
}

// map to the arch specific call

static inline int64
_user_restore_signal_frame()
{
	syscall_64_bit_return_value();

	return arch_restore_signal_frame();
}


// TODO: Replace when networking code is added to the build. 

static inline int
_user_socket(int family, int type, int proto)
{
	return 0;
}


//	#pragma mark -


int32
syscall_dispatcher(uint32 call_num, void *args, uint64 *call_ret)
{
	bigtime_t startTime;

//	dprintf("syscall_dispatcher: thread 0x%x call 0x%x, arg0 0x%x, arg1 0x%x arg2 0x%x arg3 0x%x arg4 0x%x\n",
//		thread_get_current_thread_id(), call_num, arg0, arg1, arg2, arg3, arg4);

	user_debug_pre_syscall(call_num, args);

	startTime = system_time();

	switch (call_num) {
		// the cases are auto-generated
		#include "syscall_dispatcher.h"

		default:
			*call_ret = (uint64)B_BAD_VALUE;
	}

	user_debug_post_syscall(call_num, args, *call_ret, startTime);

//	dprintf("syscall_dispatcher: done with syscall 0x%x\n", call_num);

	return B_HANDLED_INTERRUPT;
}


status_t
generic_syscall_init(void)
{
	list_init(&sGenericSyscalls);
	return mutex_init(&sGenericSyscallLock, "generic syscall");
}


//	#pragma mark -
//	public API


status_t
register_generic_syscall(const char *subsystem, syscall_hook hook,
	uint32 version, uint32 flags)
{
	struct generic_syscall *previous, *syscall;
	status_t status;

	if (hook == NULL)
		return B_BAD_VALUE;

	mutex_lock(&sGenericSyscallLock);

	previous = find_generic_syscall(subsystem);
	if (previous != NULL) {
		if ((flags & B_DO_NOT_REPLACE_SYSCALL) != 0
			|| version < previous->version) {
			status = B_NAME_IN_USE;
			goto out;
		}
		if (previous->flags & B_SYSCALL_NOT_REPLACEABLE) {
			status = B_NOT_ALLOWED;
			goto out;
		}
	}

	syscall = (generic_syscall *)malloc(sizeof(struct generic_syscall));
	if (syscall == NULL) {
		status = B_NO_MEMORY;
		goto out;
	}

	strlcpy(syscall->subsystem, subsystem, sizeof(syscall->subsystem));
	syscall->hook = hook;
	syscall->version = version;
	syscall->flags = flags;
	syscall->previous = previous;
	list_add_item(&sGenericSyscalls, syscall);

	if (previous != NULL)
		list_remove_link(&previous->link);

	status = B_OK;

out:
	mutex_unlock(&sGenericSyscallLock);
	return status;
}


status_t
unregister_generic_syscall(const char *subsystem, uint32 version)
{
	// ToDo: we should only remove the syscall with the matching version
	generic_syscall *syscall;
	status_t status;

	mutex_lock(&sGenericSyscallLock);

	syscall = find_generic_syscall(subsystem);
	if (syscall != NULL) {
		if (syscall->previous != NULL) {
			// reestablish the old syscall
			list_add_item(&sGenericSyscalls, syscall->previous);
		}
		list_remove_link(&syscall->link);
		free(syscall);
		status = B_OK;
	} else
		status = B_NAME_NOT_FOUND;

	mutex_unlock(&sGenericSyscallLock);
	return status;
}


// #pragma mark - syscall tracing


#ifdef SYSCALL_TRACING

namespace SyscallTracing {


static const char*
get_syscall_name(uint32 syscall)
{
	if (syscall >= (uint32)kSyscallCount)
		return "<invalid syscall number>";

	return kExtendedSyscallInfos[syscall].name;
}


class PreSyscall : public AbstractTraceEntry {
	public:
		PreSyscall(uint32 syscall, const void* parameters)
			:
			fSyscall(syscall),
			fParameters(NULL)
		{
			if (syscall < (uint32)kSyscallCount) {
				fParameters = alloc_tracing_buffer_memcpy(parameters,
					kSyscallInfos[syscall].parameter_size, false);

				// copy string parameters, if any
				if (fParameters != NULL && syscall != SYSCALL_KTRACE_OUTPUT) {
					int32 stringIndex = 0;
					const extended_syscall_info& syscallInfo
						= kExtendedSyscallInfos[fSyscall];
					for (int i = 0; i < syscallInfo.parameter_count; i++) {
						const syscall_parameter_info& paramInfo
							= syscallInfo.parameters[i];
						if (paramInfo.type != B_STRING_TYPE)
							continue;

						const uint8* data
							= (uint8*)fParameters + paramInfo.offset;
						if (stringIndex < MAX_PARAM_STRINGS) {
							fParameterStrings[stringIndex++]
								= alloc_tracing_buffer_strcpy(
									*(const char**)data, 64, true);
						}
					}
				}
			}

			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("syscall pre:  %s(", get_syscall_name(fSyscall));

			if (fParameters != NULL) {
				int32 stringIndex = 0;
				const extended_syscall_info& syscallInfo
					= kExtendedSyscallInfos[fSyscall];
				for (int i = 0; i < syscallInfo.parameter_count; i++) {
					const syscall_parameter_info& paramInfo
						= syscallInfo.parameters[i];
					const uint8* data = (uint8*)fParameters + paramInfo.offset;
					uint64 value = 0;
					bool printValue = true;
					switch (paramInfo.type) {
						case B_INT8_TYPE:
							value = *(uint8*)data;
							break;
						case B_INT16_TYPE:
							value = *(uint16*)data;
							break;
						case B_INT32_TYPE:
							value = *(uint32*)data;
							break;
						case B_INT64_TYPE:
							value = *(uint64*)data;
							break;
						case B_POINTER_TYPE:
							value = (uint64)*(void**)data;
							break;
						case B_STRING_TYPE:
							if (stringIndex < MAX_PARAM_STRINGS
								&& fSyscall != SYSCALL_KTRACE_OUTPUT) {
								out.Print("%s\"%s\"",
									(i == 0 ? "" : ", "),
									fParameterStrings[stringIndex++]);
								printValue = false;
							} else
								value = (uint64)*(void**)data;
							break;
					}

					if (printValue)
						out.Print("%s0x%llx", (i == 0 ? "" : ", "), value);
				}
			}

			out.Print(")");
		}

	private:
		enum { MAX_PARAM_STRINGS = 3 };

		uint32		fSyscall;
		void*		fParameters;
		const char*	fParameterStrings[MAX_PARAM_STRINGS];
};


class PostSyscall : public AbstractTraceEntry {
	public:
		PostSyscall(uint32 syscall, uint64 returnValue)
			:
			fSyscall(syscall),
			fReturnValue(returnValue)
		{
			Initialized();
#if 0
			if (syscall < (uint32)kSyscallCount
				&&  returnValue != (returnValue & 0xffffffff)
				&& kExtendedSyscallInfos[syscall].return_type.size <= 4) {
				panic("syscall return value 64 bit although it should be 32 "
					"bit");
			}
#endif
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("syscall post: %s() -> 0x%llx",
				get_syscall_name(fSyscall), fReturnValue);
		}

	private:
		uint32	fSyscall;
		uint64	fReturnValue;
};

}	// namespace SyscallTracing


extern "C" void trace_pre_syscall(uint32 syscallNumber, const void* parameters);

void
trace_pre_syscall(uint32 syscallNumber, const void* parameters)
{
	new(std::nothrow) SyscallTracing::PreSyscall(syscallNumber, parameters);
}


extern "C" void trace_post_syscall(int syscallNumber, uint64 returnValue);

void
trace_post_syscall(int syscallNumber, uint64 returnValue)
{
	new(std::nothrow) SyscallTracing::PostSyscall(syscallNumber, returnValue);
}


#endif	// SYSCALL_TRACING


/*
 * kSyscallCount and kSyscallInfos here
 */
// generated by gensyscalls
#include "syscall_table.h"
