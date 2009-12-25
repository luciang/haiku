/*
 * Copyright 2008-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "IOScheduler.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <KernelExport.h>

#include <khash.h>
#include <lock.h>
#include <thread_types.h>
#include <thread.h>
#include <util/AutoLock.h>


//#define TRACE_IO_SCHEDULER
#ifdef TRACE_IO_SCHEDULER
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


// #pragma mark - IOCallback


IOCallback::~IOCallback()
{
}


status_t
IOCallback::DoIO(IOOperation* operation)
{
	return B_ERROR;
}


// #pragma mark -


void
IORequestOwner::Dump() const
{
	kprintf("IORequestOwner at %p\n", this);
	kprintf("  team:     %ld\n", team);
	kprintf("  thread:   %ld\n", thread);
	kprintf("  priority: %ld\n", priority);

	kprintf("  requests:");
	for (IORequestList::ConstIterator it = requests.GetIterator();
			IORequest* request = it.Next();) {
		kprintf(" %p", request);
	}
	kprintf("\n");

	kprintf("  completed requests:");
	for (IORequestList::ConstIterator it = completed_requests.GetIterator();
			IORequest* request = it.Next();) {
		kprintf(" %p", request);
	}
	kprintf("\n");

	kprintf("  operations:");
	for (IOOperationList::ConstIterator it = operations.GetIterator();
			IOOperation* operation = it.Next();) {
		kprintf(" %p", operation);
	}
	kprintf("\n");
}


// #pragma mark -


struct IOScheduler::RequestOwnerHashDefinition {
	typedef thread_id		KeyType;
	typedef IORequestOwner	ValueType;

	size_t HashKey(thread_id key) const				{ return key; }
	size_t Hash(const IORequestOwner* value) const	{ return value->thread; }
	bool Compare(thread_id key, const IORequestOwner* value) const
		{ return value->thread == key; }
	IORequestOwner*& GetLink(IORequestOwner* value) const
		{ return value->hash_link; }
};

struct IOScheduler::RequestOwnerHashTable
		: BOpenHashTable<RequestOwnerHashDefinition, false> {
};


IOScheduler::IOScheduler(DMAResource* resource)
	:
	fDMAResource(resource),
	fName(NULL),
	fID(IOSchedulerRoster::Default()->NextID()),
	fSchedulerThread(-1),
	fRequestNotifierThread(-1),
	fOperationArray(NULL),
	fAllocatedRequestOwners(NULL),
	fRequestOwners(NULL),
	fBlockSize(0),
	fPendingOperations(0),
	fTerminating(false)
{
	mutex_init(&fLock, "I/O scheduler");
	B_INITIALIZE_SPINLOCK(&fFinisherLock);

	fNewRequestCondition.Init(this, "I/O new request");
	fFinishedOperationCondition.Init(this, "I/O finished operation");
	fFinishedRequestCondition.Init(this, "I/O finished request");

}


IOScheduler::~IOScheduler()
{
	if (InitCheck() == B_OK)
		IOSchedulerRoster::Default()->RemoveScheduler(this);

	// shutdown threads
	MutexLocker locker(fLock);
	InterruptsSpinLocker finisherLocker(fFinisherLock);
	fTerminating = true;

	fNewRequestCondition.NotifyAll();
	fFinishedOperationCondition.NotifyAll();
	fFinishedRequestCondition.NotifyAll();

	finisherLocker.Unlock();
	locker.Unlock();

	if (fSchedulerThread >= 0)
		wait_for_thread(fSchedulerThread, NULL);

	if (fRequestNotifierThread >= 0)
		wait_for_thread(fRequestNotifierThread, NULL);

	// destroy our belongings
	mutex_lock(&fLock);
	mutex_destroy(&fLock);

	while (IOOperation* operation = fUnusedOperations.RemoveHead())
		delete operation;

	delete[] fOperationArray;

	delete fRequestOwners;
	delete[] fAllocatedRequestOwners;

	free(fName);
}


status_t
IOScheduler::Init(const char* name)
{
	fName = strdup(name);
	if (fName == NULL)
		return B_NO_MEMORY;

	size_t count = fDMAResource != NULL ? fDMAResource->BufferCount() : 16;
	for (size_t i = 0; i < count; i++) {
		IOOperation* operation = new(std::nothrow) IOOperation;
		if (operation == NULL)
			return B_NO_MEMORY;

		fUnusedOperations.Add(operation);
	}

	fOperationArray = new(std::nothrow) IOOperation*[count];

	if (fDMAResource != NULL)
		fBlockSize = fDMAResource->BlockSize();
	if (fBlockSize == 0)
		fBlockSize = 512;

	fAllocatedRequestOwnerCount = thread_max_threads();
	fAllocatedRequestOwners
		= new(std::nothrow) IORequestOwner[fAllocatedRequestOwnerCount];
	if (fAllocatedRequestOwners == NULL)
		return B_NO_MEMORY;

	for (int32 i = 0; i < fAllocatedRequestOwnerCount; i++) {
		IORequestOwner& owner = fAllocatedRequestOwners[i];
		owner.team = -1;
		owner.thread = -1;
		owner.priority = B_IDLE_PRIORITY;
		fUnusedRequestOwners.Add(&owner);
	}

	fRequestOwners = new(std::nothrow) RequestOwnerHashTable;
	if (fRequestOwners == NULL)
		return B_NO_MEMORY;

	status_t error = fRequestOwners->Init(fAllocatedRequestOwnerCount);
	if (error != B_OK)
		return error;

	// TODO: Use a device speed dependent bandwidths!
	fIterationBandwidth = fBlockSize * 8192;
	fMinOwnerBandwidth = fBlockSize * 1024;
	fMaxOwnerBandwidth = fBlockSize * 4096;

	// start threads
	char buffer[B_OS_NAME_LENGTH];
	strlcpy(buffer, name, sizeof(buffer));
	strlcat(buffer, " scheduler ", sizeof(buffer));
	size_t nameLength = strlen(buffer);
	snprintf(buffer + nameLength, sizeof(buffer) - nameLength, "%" B_PRId32,
		fID);
	fSchedulerThread = spawn_kernel_thread(&_SchedulerThread, buffer,
		B_NORMAL_PRIORITY + 2, (void *)this);
	if (fSchedulerThread < B_OK)
		return fSchedulerThread;

	strlcpy(buffer, name, sizeof(buffer));
	strlcat(buffer, " notifier ", sizeof(buffer));
	nameLength = strlen(buffer);
	snprintf(buffer + nameLength, sizeof(buffer) - nameLength, "%" B_PRId32,
		fID);
	fRequestNotifierThread = spawn_kernel_thread(&_RequestNotifierThread,
		buffer, B_NORMAL_PRIORITY + 2, (void *)this);
	if (fRequestNotifierThread < B_OK)
		return fRequestNotifierThread;

	resume_thread(fSchedulerThread);
	resume_thread(fRequestNotifierThread);

	IOSchedulerRoster::Default()->AddScheduler(this);

	return B_OK;
}


status_t
IOScheduler::InitCheck() const
{
	return fRequestNotifierThread >= 0 ? B_OK : B_NO_INIT;
}


void
IOScheduler::SetCallback(IOCallback& callback)
{
	SetCallback(&_IOCallbackWrapper, &callback);
}


void
IOScheduler::SetCallback(io_callback callback, void* data)
{
	fIOCallback = callback;
	fIOCallbackData = data;
}


status_t
IOScheduler::ScheduleRequest(IORequest* request)
{
	TRACE("%p->IOScheduler::ScheduleRequest(%p)\n", this, request);

	IOBuffer* buffer = request->Buffer();

	// TODO: it would be nice to be able to lock the memory later, but we can't
	// easily do it in the I/O scheduler without being able to asynchronously
	// lock memory (via another thread or a dedicated call).

	if (buffer->IsVirtual()) {
		status_t status = buffer->LockMemory(request->Team(),
			request->IsWrite());
		if (status != B_OK) {
			request->SetStatusAndNotify(status);
			return status;
		}
	}

	MutexLocker locker(fLock);

	IORequestOwner* owner = _GetRequestOwner(request->Team(), request->Thread(),
		true);
	if (owner == NULL) {
		panic("IOScheduler: Out of request owners!\n");
		locker.Unlock();
		if (buffer->IsVirtual())
			buffer->UnlockMemory(request->Team(), request->IsWrite());
		request->SetStatusAndNotify(B_NO_MEMORY);
		return B_NO_MEMORY;
	}

	bool wasActive = owner->IsActive();
	request->SetOwner(owner);
	owner->requests.Add(request);

	int32 priority = thread_get_io_priority(request->Thread());
	if (priority >= 0)
		owner->priority = priority;
//dprintf("  request %p -> owner %p (thread %ld, active %d)\n", request, owner, owner->thread, wasActive);

	if (!wasActive)
		fActiveRequestOwners.Add(owner);

	IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_REQUEST_SCHEDULED, this,
		request);

	fNewRequestCondition.NotifyAll();

	return B_OK;
}


void
IOScheduler::AbortRequest(IORequest* request, status_t status)
{
	// TODO:...
//B_CANCELED
}


void
IOScheduler::OperationCompleted(IOOperation* operation, status_t status,
	size_t transferredBytes)
{
	InterruptsSpinLocker _(fFinisherLock);

	// finish operation only once
	if (operation->Status() <= 0)
		return;

	operation->SetStatus(status);

	// set the bytes transferred (of the net data)
	size_t partialBegin = operation->OriginalOffset() - operation->Offset();
	operation->SetTransferredBytes(
		transferredBytes > partialBegin ? transferredBytes - partialBegin : 0);

	fCompletedOperations.Add(operation);
	fFinishedOperationCondition.NotifyAll();
}


void
IOScheduler::Dump() const
{
	kprintf("IOScheduler at %p\n", this);
	kprintf("  DMA resource:   %p\n", fDMAResource);

	kprintf("  active request owners:");
	for (RequestOwnerList::ConstIterator it
				= fActiveRequestOwners.GetIterator();
			IORequestOwner* owner = it.Next();) {
		kprintf(" %p", owner);
	}
	kprintf("\n");
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

		TRACE("IOScheduler::_Finisher(): operation: %p\n", operation);

		bool operationFinished = operation->Finish();

		IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_OPERATION_FINISHED,
			this, operation->Parent(), operation);
			// Notify for every time the operation is passed to the I/O hook,
			// not only when it is fully finished.

		if (!operationFinished) {
			TRACE("  operation: %p not finished yet\n", operation);
			MutexLocker _(fLock);
			operation->SetTransferredBytes(0);
			operation->Parent()->Owner()->operations.Add(operation);
			fPendingOperations--;
			continue;
		}

		// notify request and remove operation
		IORequest* request = operation->Parent();
		if (request != NULL) {
			size_t operationOffset = operation->OriginalOffset()
				- request->Offset();
			request->OperationFinished(operation, operation->Status(),
				operation->TransferredBytes() < operation->OriginalLength(),
				operation->Status() == B_OK
					? operationOffset + operation->OriginalLength()
					: operationOffset);
		}

		// recycle the operation
		MutexLocker _(fLock);
		if (fDMAResource != NULL)
			fDMAResource->RecycleBuffer(operation->Buffer());

		fPendingOperations--;
		fUnusedOperations.Add(operation);

		// If the request is done, we need to perform its notifications.
		if (request->IsFinished()) {
			if (request->Status() == B_OK && request->RemainingBytes() > 0) {
				// The request has been processed OK so far, but it isn't really
				// finished yet.
				request->SetUnfinished();
			} else {
				// Remove the request from the request owner.
				IORequestOwner* owner = request->Owner();
				owner->requests.MoveFrom(&owner->completed_requests);
				owner->requests.Remove(request);
				request->SetOwner(NULL);

				if (!owner->IsActive()) {
					fActiveRequestOwners.Remove(owner);
					fUnusedRequestOwners.Add(owner);
				}

				if (request->HasCallbacks()) {
					// The request has callbacks that may take some time to
					// perform, so we hand it over to the request notifier.
					fFinishedRequests.Add(request);
					fFinishedRequestCondition.NotifyAll();
				} else {
					// No callbacks -- finish the request right now.
					IOSchedulerRoster::Default()->Notify(
						IO_SCHEDULER_REQUEST_FINISHED, this, request);
					request->NotifyFinished();
				}
			}
		}
	}
}


/*!	Called with \c fFinisherLock held.
*/
bool
IOScheduler::_FinisherWorkPending()
{
	return !fCompletedOperations.IsEmpty();
}


bool
IOScheduler::_PrepareRequestOperations(IORequest* request,
	IOOperationList& operations, int32& operationsPrepared, off_t quantum,
	off_t& usedBandwidth)
{
//dprintf("IOScheduler::_PrepareRequestOperations(%p)\n", request);
	usedBandwidth = 0;

	if (fDMAResource != NULL) {
		while (quantum >= fBlockSize && request->RemainingBytes() > 0) {
			IOOperation* operation = fUnusedOperations.RemoveHead();
			if (operation == NULL)
				return false;

			status_t status = fDMAResource->TranslateNext(request, operation,
				quantum);
			if (status != B_OK) {
				operation->SetParent(NULL);
				fUnusedOperations.Add(operation);

				// B_BUSY means some resource (DMABuffers or
				// DMABounceBuffers) was temporarily unavailable. That's OK,
				// we'll retry later.
				if (status == B_BUSY)
					return false;

				AbortRequest(request, status);
				return true;
			}
//dprintf("  prepared operation %p\n", operation);

			off_t bandwidth = operation->Length();
			quantum -= bandwidth;
			usedBandwidth += bandwidth;

			operations.Add(operation);
			operationsPrepared++;
		}
	} else {
		// TODO: If the device has block size restrictions, we might need to use
		// a bounce buffer.
		IOOperation* operation = fUnusedOperations.RemoveHead();
		if (operation == NULL)
			return false;

		status_t status = operation->Prepare(request);
		if (status != B_OK) {
			operation->SetParent(NULL);
			fUnusedOperations.Add(operation);
			AbortRequest(request, status);
			return true;
		}

		operation->SetOriginalRange(request->Offset(), request->Length());
		request->Advance(request->Length());

		off_t bandwidth = operation->Length();
		quantum -= bandwidth;
		usedBandwidth += bandwidth;

		operations.Add(operation);
		operationsPrepared++;
	}

	return true;
}


off_t
IOScheduler::_ComputeRequestOwnerBandwidth(int32 priority) const
{
// TODO: Use a priority dependent quantum!
	return fMinOwnerBandwidth;
}


bool
IOScheduler::_NextActiveRequestOwner(IORequestOwner*& owner, off_t& quantum)
{
	while (true) {
		if (fTerminating)
			return false;

		if (owner != NULL)
			owner = fActiveRequestOwners.GetNext(owner);
		if (owner == NULL)
			owner = fActiveRequestOwners.Head();

		if (owner != NULL) {
			quantum = _ComputeRequestOwnerBandwidth(owner->priority);
			return true;
		}

		// Wait for new requests owners. First check whether any finisher work
		// has to be done.
		InterruptsSpinLocker finisherLocker(fFinisherLock);
		if (_FinisherWorkPending()) {
			finisherLocker.Unlock();
			mutex_unlock(&fLock);
			_Finisher();
			mutex_lock(&fLock);
			continue;
		}

		// Wait for new requests.
		ConditionVariableEntry entry;
		fNewRequestCondition.Add(&entry);

		finisherLocker.Unlock();
		mutex_unlock(&fLock);

		entry.Wait(B_CAN_INTERRUPT);
		_Finisher();
		mutex_lock(&fLock);
	}
}


struct OperationComparator {
	inline bool operator()(const IOOperation* a, const IOOperation* b)
	{
		off_t offsetA = a->Offset();
		off_t offsetB = b->Offset();
		return offsetA < offsetB
			|| (offsetA == offsetB && a->Length() > b->Length());
	}
};


void
IOScheduler::_SortOperations(IOOperationList& operations, off_t& lastOffset)
{
// TODO: _Scheduler() could directly add the operations to the array.
	// move operations to an array and sort it
	int32 count = 0;
	while (IOOperation* operation = operations.RemoveHead())
		fOperationArray[count++] = operation;

	std::sort(fOperationArray, fOperationArray + count, OperationComparator());

	// move the sorted operations to a temporary list we can work with
//dprintf("operations after sorting:\n");
	IOOperationList sortedOperations;
	for (int32 i = 0; i < count; i++)
//{
//dprintf("  %3ld: %p: offset: %lld, length: %lu\n", i, fOperationArray[i], fOperationArray[i]->Offset(), fOperationArray[i]->Length());
		sortedOperations.Add(fOperationArray[i]);
//}

	// Sort the operations so that no two adjacent operations overlap. This
	// might result in several elevator runs.
	while (!sortedOperations.IsEmpty()) {
		IOOperation* operation = sortedOperations.Head();
		while (operation != NULL) {
			IOOperation* nextOperation = sortedOperations.GetNext(operation);
			if (operation->Offset() >= lastOffset) {
				sortedOperations.Remove(operation);
//dprintf("  adding operation %p\n", operation);
				operations.Add(operation);
				lastOffset = operation->Offset() + operation->Length();
			}

			operation = nextOperation;
		}

		if (!sortedOperations.IsEmpty())
			lastOffset = 0;
	}
}


status_t
IOScheduler::_Scheduler()
{
	IORequestOwner marker;
	marker.thread = -1;
	{
		MutexLocker locker(fLock);
		fActiveRequestOwners.Add(&marker, false);
	}

	off_t lastOffset = 0;

	IORequestOwner* owner = NULL;
	off_t quantum = 0;

	while (!fTerminating) {
//dprintf("IOScheduler::_Scheduler(): next iteration: request owner: %p, quantum: %lld\n", owner, quantum);
		MutexLocker locker(fLock);

		IOOperationList operations;
		int32 operationCount = 0;
		bool resourcesAvailable = true;
		off_t iterationBandwidth = fIterationBandwidth;

		if (owner == NULL) {
			owner = fActiveRequestOwners.GetPrevious(&marker);
			quantum = 0;
			fActiveRequestOwners.Remove(&marker);
		}

		if (owner == NULL || quantum < fBlockSize) {
			if (!_NextActiveRequestOwner(owner, quantum)) {
				// we've been asked to terminate
				return B_OK;
			}
		}

		while (resourcesAvailable && iterationBandwidth >= fBlockSize) {
//dprintf("IOScheduler::_Scheduler(): request owner: %p (thread %ld)\n",
//owner, owner->thread);
			// Prepare operations for the owner.

			// There might still be unfinished ones.
			while (IOOperation* operation = owner->operations.RemoveHead()) {
				// TODO: We might actually grant the owner more bandwidth than
				// it deserves.
				// TODO: We should make sure that after the first read operation
				// of a partial write, no other write operation to the same
				// location is scheduled!
				operations.Add(operation);
				operationCount++;
				off_t bandwidth = operation->Length();
				quantum -= bandwidth;
				iterationBandwidth -= bandwidth;

				if (quantum < fBlockSize || iterationBandwidth < fBlockSize)
					break;
			}

			while (resourcesAvailable && quantum >= fBlockSize
					&& iterationBandwidth >= fBlockSize) {
				IORequest* request = owner->requests.Head();
				if (request == NULL) {
					resourcesAvailable = false;
if (operationCount == 0)
panic("no more requests for owner %p (thread %ld)", owner, owner->thread);
					break;
				}

				off_t bandwidth = 0;
				resourcesAvailable = _PrepareRequestOperations(request,
					operations, operationCount, quantum, bandwidth);
				quantum -= bandwidth;
				iterationBandwidth -= bandwidth;
				if (request->RemainingBytes() == 0 || request->Status() <= 0) {
					// If the request has been completed, move it to the
					// completed list, so we don't pick it up again.
					owner->requests.Remove(request);
					owner->completed_requests.Add(request);
				}
			}

			// Get the next owner.
			if (resourcesAvailable)
				_NextActiveRequestOwner(owner, quantum);
		}

		// If the current owner doesn't have anymore requests, we have to
		// insert our marker, since the owner will be gone in the next
		// iteration.
		if (owner->requests.IsEmpty()) {
			fActiveRequestOwners.Insert(owner, &marker);
			owner = NULL;
		}

		if (operations.IsEmpty())
			continue;

		fPendingOperations = operationCount;

		locker.Unlock();

		// sort the operations
		_SortOperations(operations, lastOffset);

		// execute the operations
#ifdef TRACE_IO_SCHEDULER
		int32 i = 0;
#endif
		while (IOOperation* operation = operations.RemoveHead()) {
			TRACE("IOScheduler::_Scheduler(): calling callback for "
				"operation %ld: %p\n", i++, operation);

			IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_OPERATION_STARTED,
				this, operation->Parent(), operation);

			fIOCallback(fIOCallbackData, operation);

			_Finisher();
		}

		// wait for all operations to finish
		while (!fTerminating) {
			locker.Lock();

			if (fPendingOperations == 0)
				break;

			// Before waiting first check whether any finisher work has to be
			// done.
			InterruptsSpinLocker finisherLocker(fFinisherLock);
			if (_FinisherWorkPending()) {
				finisherLocker.Unlock();
				locker.Unlock();
				_Finisher();
				continue;
			}

			// wait for finished operations
			ConditionVariableEntry entry;
			fFinishedOperationCondition.Add(&entry);

			finisherLocker.Unlock();
			locker.Unlock();

			entry.Wait(B_CAN_INTERRUPT);
			_Finisher();
		}
	}

	return B_OK;
}


/*static*/ status_t
IOScheduler::_SchedulerThread(void *_self)
{
	IOScheduler *self = (IOScheduler *)_self;
	return self->_Scheduler();
}


status_t
IOScheduler::_RequestNotifier()
{
	while (true) {
		MutexLocker locker(fLock);

		// get a request
		IORequest* request = fFinishedRequests.RemoveHead();

		if (request == NULL) {
			if (fTerminating)
				return B_OK;

			ConditionVariableEntry entry;
			fFinishedRequestCondition.Add(&entry);

			locker.Unlock();

			entry.Wait();
			continue;
		}

		locker.Unlock();

		IOSchedulerRoster::Default()->Notify(IO_SCHEDULER_REQUEST_FINISHED,
			this, request);

		// notify the request
		request->NotifyFinished();
	}

	// never can get here
	return B_OK;
}


/*static*/ status_t
IOScheduler::_RequestNotifierThread(void *_self)
{
	IOScheduler *self = (IOScheduler*)_self;
	return self->_RequestNotifier();
}


IORequestOwner*
IOScheduler::_GetRequestOwner(team_id team, thread_id thread, bool allocate)
{
	// lookup in table
	IORequestOwner* owner = fRequestOwners->Lookup(thread);
	if (owner != NULL && !owner->IsActive())
		fUnusedRequestOwners.Remove(owner);
	if (owner != NULL || !allocate)
		return owner;

	// not in table -- allocate an unused one
	RequestOwnerList existingOwners;

	while ((owner = fUnusedRequestOwners.RemoveHead()) != NULL) {
		if (owner->thread < 0
			|| thread_get_thread_struct(owner->thread) == NULL) {
			if (owner->thread >= 0)
				fRequestOwners->RemoveUnchecked(owner);
			owner->team = team;
			owner->thread = thread;
			owner->priority = B_IDLE_PRIORITY;
			fRequestOwners->InsertUnchecked(owner);
			break;
		}

		existingOwners.Add(owner);
	}

	fUnusedRequestOwners.MoveFrom(&existingOwners);
	return owner;
}


/*static*/ status_t
IOScheduler::_IOCallbackWrapper(void* data, io_operation* operation)
{
	return ((IOCallback*)data)->DoIO(operation);
}


// #pragma mark - IOSchedulerNotificationService


/*static*/ IOSchedulerRoster IOSchedulerRoster::sDefaultInstance;


/*static*/ void
IOSchedulerRoster::Init()
{
	new(&sDefaultInstance) IOSchedulerRoster;
}


void
IOSchedulerRoster::AddScheduler(IOScheduler* scheduler)
{
	AutoLocker<IOSchedulerRoster> locker(this);
	fSchedulers.Add(scheduler);
	locker.Unlock();

	Notify(IO_SCHEDULER_ADDED, scheduler);
}


void
IOSchedulerRoster::RemoveScheduler(IOScheduler* scheduler)
{
	AutoLocker<IOSchedulerRoster> locker(this);
	fSchedulers.Remove(scheduler);
	locker.Unlock();

	Notify(IO_SCHEDULER_REMOVED, scheduler);
}


void
IOSchedulerRoster::Notify(uint32 eventCode, const IOScheduler* scheduler,
	IORequest* request, IOOperation* operation)
{
	AutoLocker<DefaultNotificationService> locker(fNotificationService);

	if (!fNotificationService.HasListeners())
		return;

	KMessage event;
	event.SetTo(fEventBuffer, sizeof(fEventBuffer), IO_SCHEDULER_MONITOR);
	event.AddInt32("event", eventCode);
	event.AddPointer("scheduler", scheduler);
	if (request != NULL) {
		event.AddPointer("request", request);
		if (operation != NULL)
			event.AddPointer("operation", operation);
	}

	fNotificationService.NotifyLocked(event, eventCode);
}


int32
IOSchedulerRoster::NextID()
{
	AutoLocker<IOSchedulerRoster> locker(this);
	return fNextID++;
}


IOSchedulerRoster::IOSchedulerRoster()
	:
	fNextID(1),
	fNotificationService("I/O")
{
	mutex_init(&fLock, "IOSchedulerRoster");
}


IOSchedulerRoster::~IOSchedulerRoster()
{
	mutex_destroy(&fLock);
}
