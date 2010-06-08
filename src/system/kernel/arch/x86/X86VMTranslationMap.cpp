/*
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include "X86VMTranslationMap.h"

#include <thread.h>
#include <smp.h>

#include "X86PagingStructures.h"


//#define TRACE_X86_VM_TRANSLATION_MAP
#ifdef TRACE_X86_VM_TRANSLATION_MAP
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


X86VMTranslationMap::X86VMTranslationMap()
	:
	fPageMapper(NULL),
	fInvalidPagesCount(0)
{
}


X86VMTranslationMap::~X86VMTranslationMap()
{
}


status_t
X86VMTranslationMap::Init(bool kernel)
{
	fIsKernelMap = kernel;
	return B_OK;
}


/*!	Acquires the map's recursive lock, and resets the invalidate pages counter
	in case it's the first locking recursion.
*/
bool
X86VMTranslationMap::Lock()
{
	TRACE("%p->X86VMTranslationMap::Lock()\n", this);

	recursive_lock_lock(&fLock);
	if (recursive_lock_get_recursion(&fLock) == 1) {
		// we were the first one to grab the lock
		TRACE("clearing invalidated page count\n");
		fInvalidPagesCount = 0;
	}

	return true;
}


/*!	Unlocks the map, and, if we are actually losing the recursive lock,
	flush all pending changes of this map (ie. flush TLB caches as
	needed).
*/
void
X86VMTranslationMap::Unlock()
{
	TRACE("%p->X86VMTranslationMap::Unlock()\n", this);

	if (recursive_lock_get_recursion(&fLock) == 1) {
		// we're about to release it for the last time
		Flush();
	}

	recursive_lock_unlock(&fLock);
}


addr_t
X86VMTranslationMap::MappedSize() const
{
	return fMapCount;
}


void
X86VMTranslationMap::Flush()
{
	if (fInvalidPagesCount <= 0)
		return;

	struct thread* thread = thread_get_current_thread();
	thread_pin_to_current_cpu(thread);

	if (fInvalidPagesCount > PAGE_INVALIDATE_CACHE_SIZE) {
		// invalidate all pages
		TRACE("flush_tmap: %d pages to invalidate, invalidate all\n",
			fInvalidPagesCount);

		if (fIsKernelMap) {
			arch_cpu_global_TLB_invalidate();
			smp_send_broadcast_ici(SMP_MSG_GLOBAL_INVALIDATE_PAGES, 0, 0, 0,
				NULL, SMP_MSG_FLAG_SYNC);
		} else {
			cpu_status state = disable_interrupts();
			arch_cpu_user_TLB_invalidate();
			restore_interrupts(state);

			int cpu = smp_get_current_cpu();
			uint32 cpuMask = PagingStructures()->active_on_cpus
				& ~((uint32)1 << cpu);
			if (cpuMask != 0) {
				smp_send_multicast_ici(cpuMask, SMP_MSG_USER_INVALIDATE_PAGES,
					0, 0, 0, NULL, SMP_MSG_FLAG_SYNC);
			}
		}
	} else {
		TRACE("flush_tmap: %d pages to invalidate, invalidate list\n",
			fInvalidPagesCount);

		arch_cpu_invalidate_TLB_list(fInvalidPages, fInvalidPagesCount);

		if (fIsKernelMap) {
			smp_send_broadcast_ici(SMP_MSG_INVALIDATE_PAGE_LIST,
				(uint32)fInvalidPages, fInvalidPagesCount, 0, NULL,
				SMP_MSG_FLAG_SYNC);
		} else {
			int cpu = smp_get_current_cpu();
			uint32 cpuMask = PagingStructures()->active_on_cpus
				& ~((uint32)1 << cpu);
			if (cpuMask != 0) {
				smp_send_multicast_ici(cpuMask, SMP_MSG_INVALIDATE_PAGE_LIST,
					(uint32)fInvalidPages, fInvalidPagesCount, 0, NULL,
					SMP_MSG_FLAG_SYNC);
			}
		}
	}
	fInvalidPagesCount = 0;

	thread_unpin_from_current_cpu(thread);
}
