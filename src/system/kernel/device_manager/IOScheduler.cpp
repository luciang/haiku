/*
 * Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "IOScheduler.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

#include <khash.h>
#include <lock.h>
#include <thread_types.h>
#include <thread.h>
#include <util/AutoLock.h>


IOScheduler::IOScheduler(DMAResource* resource)
	:
	fDMAResource(resource)
{
	mutex_init(&fLock, "I/O scheduler");
	B_INITIALIZE_SPINLOCK(&fFinisherLock);
}


IOScheduler::~IOScheduler()
{
	mutex_lock(&fLock);
	mutex_destroy(&fLock);

	while (IOOperation* operation = fUnusedOperations.RemoveHead())
		delete operation;
}


status_t
IOScheduler::Init(const char* name)
{
	fNewRequestCondition.Init(this, "I/O new request");
	fFinishedOperationCondition.Init(this, "I/O finished operation");

	size_t count = fDMAResource != NULL ? fDMAResource->BufferCount() : 16;
	for (size_t i = 0; i < count; i++) {
		IOOperation* operation = new(std::nothrow) IOOperation;
		if (operation == NULL)
			return B_NO_MEMORY;

		fUnusedOperations.Add(operation);
	}

	// start thread for device
	fThread = spawn_kernel_thread(&_SchedulerThread, name, B_NORMAL_PRIORITY,
		(void *)this);
	if (fThread < B_OK)
		return fThread;

	resume_thread(fThread);
	return B_OK;
}


status_t
IOScheduler::ScheduleRequest(IORequest* request)
{
	IOBuffer* buffer = request->Buffer();

	// TODO: it would be nice to be able to lock the memory later, but we can't
	// easily do it in the I/O scheduler without being able to asynchronously
	// lock memory (via another thread or a dedicated call).

	if (buffer->IsVirtual()) {
		status_t status = buffer->LockMemory(request->IsWrite());
		if (status != B_OK)
			return status;
	}

	MutexLocker _(fLock);
	fUnscheduledRequests.Add(request);

	return B_OK;
}


void
IOScheduler::AbortRequest(IORequest* request, status_t status)
{
	// TODO:...
//B_CANCELED
}


void
IOScheduler::OperationCompleted(IOOperation* operation, status_t status)
{
	InterruptsLocker _;
	SpinLocker locker(fFinisherLock);

	// finish operation only once
	if (operation->Status() <= 0)
		return;

	operation->SetStatus(status);

	fCompletedOperations.Add(operation);
	locker.Unlock();

	locker.SetTo(thread_spinlock, false);
	thread_interrupt(thread_get_thread_struct_locked(fThread), false);
}


/*!	Must not be called with the fLock held. */
void
IOScheduler::_Finisher()
{
	while (true) {
		InterruptsSpinLocker locker(fFinisherLock);
		IOOperation* operation = fCompletedOperations.RemoveHead();
		if (operation == NULL)
			return;

		locker.Unlock();

		if (!operation->Finish()) {
			// TODO: This must be done differently once the scheduler implements
			// an actual scheduling policy (other than no-op).
			fIOCallback(fIOCallbackData, operation);
		} else {
			MutexLocker _(fLock);
			operation->Parent()->RemoveOperation(operation);

			fUnusedOperations.Add(operation);
		}
	}
}


IOOperation*
IOScheduler::_GetOperation()
{
	while (true) {
		MutexLocker locker(fLock);

		IOOperation* operation = fUnusedOperations.RemoveHead();
		if (operation != NULL)
			return operation;

		ConditionVariableEntry entry;
		fFinishedOperationCondition.Add(&entry);

		locker.Unlock();

		entry.Wait();
		_Finisher();
	}
}


status_t
IOScheduler::_Scheduler()
{
// TODO: This is a no-op scheduler. Implement something useful!
	while (true) {
		MutexLocker locker(fLock);
		IORequest* request = fUnscheduledRequests.RemoveHead();

		if (request == NULL) {
			ConditionVariableEntry entry;
			fNewRequestCondition.Add(&entry);
			locker.Unlock();

			if (entry.Wait(B_CAN_INTERRUPT) != B_OK)
				_Finisher();

			continue;
		}

		locker.Unlock();

		if (fDMAResource != NULL) {
			while (request->RemainingBytes() > 0) {
				IOOperation* operation = _GetOperation();

				status_t status = fDMAResource->TranslateNext(request,
					operation);
				if (status != B_OK) {
					AbortRequest(request, status);
					break;
				}

				fIOCallback(fIOCallbackData, operation);
			}
		} else {
// TODO: If the device has block size restrictions, we might need to use a
// bounce buffer.
			IOOperation* operation = _GetOperation();
			operation->SetRequest(request);
			operation->SetOriginalRange(request->Offset(), request->Length());
			fIOCallback(fIOCallbackData, operation);
		}
	}

	return B_OK;
}


status_t
IOScheduler::_SchedulerThread(void *_self)
{
	IOScheduler *self = (IOScheduler *)_self;
	return self->_Scheduler();
}

