/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "ModelLoader.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <DebugEventStream.h>

#include <system_profiler_defs.h>
#include <thread_defs.h>

#include "DataSource.h"
#include "MessageCodes.h"
#include "Model.h"


// add a scheduling state snapshot every x events
static const uint32 kSchedulingSnapshotInterval = 1024;


struct SimpleWaitObjectInfo : system_profiler_wait_object_info {
	SimpleWaitObjectInfo(uint32 type)
	{
		this->type = type;
		object = 0;
		referenced_object = 0;
		name[0] = '\0';
	}
};


static const SimpleWaitObjectInfo kSnoozeWaitObjectInfo(
	THREAD_BLOCK_TYPE_SNOOZE);
static const SimpleWaitObjectInfo kSignalWaitObjectInfo(
	THREAD_BLOCK_TYPE_SIGNAL);


// #pragma mark -


inline void
ModelLoader::_UpdateLastEventTime(bigtime_t time)
{
	if (fBaseTime < 0) {
		fBaseTime = time;
		fModel->SetBaseTime(time);
	}

	fState.SetLastEventTime(time - fBaseTime);
}


ModelLoader::ModelLoader(DataSource* dataSource,
	const BMessenger& target, void* targetCookie)
	:
	AbstractModelLoader(target, targetCookie),
	fModel(NULL),
	fDataSource(dataSource)
{
}


ModelLoader::~ModelLoader()
{
	delete fDataSource;
	delete fModel;
}


Model*
ModelLoader::DetachModel()
{
	AutoLocker<BLocker> locker(fLock);

	if (fModel == NULL || fLoading)
		return NULL;

	Model* model = fModel;
	fModel = NULL;

	return model;
}


status_t
ModelLoader::PrepareForLoading()
{
	if (fModel != NULL || fDataSource == NULL)
		return B_BAD_VALUE;

	// init the state
	status_t error = fState.Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


status_t
ModelLoader::Load()
{
	try {
		return _Load();
	} catch(...) {
		return B_ERROR;
	}
}


void
ModelLoader::FinishLoading(bool success)
{
	fState.Clear();

	if (!success) {
		delete fModel;
		fModel = NULL;
	}
}


status_t
ModelLoader::_Load()
{
	// read the complete data into memory
	void* eventData;
	size_t eventDataSize;
	status_t error = _ReadDebugEvents(&eventData, &eventDataSize);
	if (error != B_OK)
		return error;

	// get the data source name
	BString dataSourceName;
	fDataSource->GetName(dataSourceName);

	// create a model
	fModel = new(std::nothrow) Model(dataSourceName.String(), eventData,
		eventDataSize);
	if (fModel == NULL) {
		free(eventData);
		return B_NO_MEMORY;
	}

	// create a debug input stream
	BDebugEventInputStream* input = new(std::nothrow) BDebugEventInputStream;
	if (input == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<BDebugEventInputStream> inputDeleter(input);

	error = input->SetTo(eventData, eventDataSize, false);
	if (error != B_OK)
		return error;

	// add the snooze and signal wait objects to the model
	if (fModel->AddWaitObject(&kSnoozeWaitObjectInfo, NULL) == NULL
		|| fModel->AddWaitObject(&kSignalWaitObjectInfo, NULL) == NULL) {
		return B_NO_MEMORY;
	}

	// process the events
	fState.Clear();
	fBaseTime = -1;
	uint64 count = 0;

	while (true) {
		// get next event
		uint32 event;
		uint32 cpu;
		const void* buffer;
		off_t offset;
		ssize_t bufferSize = input->ReadNextEvent(&event, &cpu, &buffer,
			&offset);
		if (bufferSize < 0)
			return bufferSize;
		if (buffer == NULL)
			break;

		// process the event
		status_t error = _ProcessEvent(event, cpu, buffer, bufferSize);
		if (error != B_OK)
			return error;

		// periodically check whether we're supposed to abort
		if (++count % 32 == 0) {
			AutoLocker<BLocker> locker(fLock);
			if (fAborted)
				return B_ERROR;
		}

		// periodically add scheduling snapshots
		if (count % kSchedulingSnapshotInterval == 0)
			fModel->AddSchedulingStateSnapshot(fState, offset);
	}

	fModel->SetLastEventTime(fState.LastEventTime());
	fModel->LoadingFinished();

	return B_OK;
}


status_t
ModelLoader::_ReadDebugEvents(void** _eventData, size_t* _size)
{
	// get a BDataIO from the data source
	BDataIO* io;
	status_t error = fDataSource->CreateDataIO(&io);
	if (error != B_OK)
		return error;
	ObjectDeleter<BDataIO> dataIOtDeleter(io);

	// First we need to find out how large a buffer to allocate.
	size_t size;

	if (BPositionIO* positionIO = dynamic_cast<BPositionIO*>(io)) {
		// it's a BPositionIO -- this makes things easier, since we know how
		// many bytes to read
		off_t currentPos = positionIO->Position();
		if (currentPos < 0)
			return currentPos;

		off_t fileSize;
		error = positionIO->GetSize(&fileSize);
		if (error != B_OK)
			return error;

		size = fileSize - currentPos;
	} else {
		// no BPositionIO -- we need to determine the total size by iteratively
		// reading the whole data one time

		// allocate a dummy buffer for reading
		const size_t kBufferSize = 1024 * 1024;
		void* buffer = malloc(kBufferSize);
		if (buffer == NULL)
			return B_NO_MEMORY;
		MemoryDeleter bufferDeleter(buffer);

		size = 0;
		while (true) {
			ssize_t bytesRead = io->Read(buffer, kBufferSize);
			if (bytesRead < 0)
				return bytesRead;
			if (bytesRead == 0)
				break;

			size += bytesRead;
		}

		// we've got the size -- recreate the BDataIO
		dataIOtDeleter.Delete();
		error = fDataSource->CreateDataIO(&io);
		if (error != B_OK)
			return error;
		dataIOtDeleter.SetTo(io);
	}

	// allocate the data buffer
	void* data = malloc(size);
	if (data == NULL)
		return B_NO_MEMORY;
	MemoryDeleter dataDeleter(data);

	// read the data
	ssize_t bytesRead = io->Read(data, size);
	if (bytesRead < 0)
		return bytesRead;
	if ((size_t)bytesRead != size)
		return B_FILE_ERROR;

	dataDeleter.Detach();
	*_eventData = data;
	*_size = size;
	return B_OK;
}


status_t
ModelLoader::_ProcessEvent(uint32 event, uint32 cpu, const void* buffer,
	size_t size)
{
	switch (event) {
		case B_SYSTEM_PROFILER_TEAM_ADDED:
			_HandleTeamAdded((system_profiler_team_added*)buffer);
			break;

		case B_SYSTEM_PROFILER_TEAM_REMOVED:
			_HandleTeamRemoved((system_profiler_team_removed*)buffer);
			break;

		case B_SYSTEM_PROFILER_TEAM_EXEC:
			_HandleTeamExec((system_profiler_team_exec*)buffer);
			break;

		case B_SYSTEM_PROFILER_THREAD_ADDED:
			_HandleThreadAdded((system_profiler_thread_added*)buffer);
			break;

		case B_SYSTEM_PROFILER_THREAD_REMOVED:
			_HandleThreadRemoved((system_profiler_thread_removed*)buffer);
			break;

		case B_SYSTEM_PROFILER_THREAD_SCHEDULED:
			_HandleThreadScheduled((system_profiler_thread_scheduled*)buffer);
			break;

		case B_SYSTEM_PROFILER_THREAD_ENQUEUED_IN_RUN_QUEUE:
			_HandleThreadEnqueuedInRunQueue(
				(thread_enqueued_in_run_queue*)buffer);
			break;

		case B_SYSTEM_PROFILER_THREAD_REMOVED_FROM_RUN_QUEUE:
			_HandleThreadRemovedFromRunQueue(
				(thread_removed_from_run_queue*)buffer);
			break;

		case B_SYSTEM_PROFILER_WAIT_OBJECT_INFO:
			_HandleWaitObjectInfo((system_profiler_wait_object_info*)buffer);
			break;

		default:
printf("unsupported event type %lu, size: %lu\n", event, size);
return B_BAD_DATA;
			break;
	}

	return B_OK;
}


void
ModelLoader::_HandleTeamAdded(system_profiler_team_added* event)
{
	if (fModel->AddTeam(event, fState.LastEventTime()) == NULL)
		throw std::bad_alloc();
}


void
ModelLoader::_HandleTeamRemoved(system_profiler_team_removed* event)
{
	if (Model::Team* team = fModel->TeamByID(event->team))
		team->SetDeletionTime(fState.LastEventTime());
	else
		printf("Removed event for unknown team: %ld\n", event->team);
}


void
ModelLoader::_HandleTeamExec(system_profiler_team_exec* event)
{
	// TODO:...
}


void
ModelLoader::_HandleThreadAdded(system_profiler_thread_added* event)
{
	if (_AddThread(event) == NULL)
		throw std::bad_alloc();
}


void
ModelLoader::_HandleThreadRemoved(system_profiler_thread_removed* event)
{
	if (Model::Thread* thread = fModel->ThreadByID(event->thread))
		thread->SetDeletionTime(fState.LastEventTime());
	else
		printf("Removed event for unknown team: %ld\n", event->thread);
}


void
ModelLoader::_HandleThreadScheduled(system_profiler_thread_scheduled* event)
{
	_UpdateLastEventTime(event->time);

	Model::ThreadSchedulingState* thread = fState.LookupThread(event->thread);
	if (thread == NULL) {
		printf("Schedule event for unknown thread: %ld\n", event->thread);
		return;
	}

	bigtime_t diffTime = fState.LastEventTime() - thread->lastTime;

	if (thread->state == READY) {
		// thread scheduled after having been woken up
		thread->thread->AddLatency(diffTime);
	} else if (thread->state == PREEMPTED) {
		// thread scheduled after having been preempted before
		thread->thread->AddRerun(diffTime);
	}

	if (thread->state == STILL_RUNNING) {
		// Thread was running and continues to run.
		thread->state = RUNNING;
	}

	if (thread->state != RUNNING) {
		thread->lastTime = fState.LastEventTime();
		thread->state = RUNNING;
	}

	// unscheduled thread

	if (event->thread == event->previous_thread)
		return;

	thread = fState.LookupThread(event->previous_thread);
	if (thread == NULL) {
		printf("Schedule event for unknown previous thread: %ld\n",
			event->previous_thread);
		return;
	}

	diffTime = fState.LastEventTime() - thread->lastTime;

	if (thread->state == STILL_RUNNING) {
		// thread preempted
		thread->thread->AddPreemption(diffTime);
		thread->thread->AddRun(diffTime);

		thread->lastTime = fState.LastEventTime();
		thread->state = PREEMPTED;
	} else if (thread->state == RUNNING) {
		// thread starts waiting (it hadn't been added to the run
		// queue before being unscheduled)
		thread->thread->AddRun(diffTime);

		if (event->previous_thread_state == B_THREAD_WAITING) {
			addr_t waitObject = event->previous_thread_wait_object;
			switch (event->previous_thread_wait_object_type) {
				case THREAD_BLOCK_TYPE_SNOOZE:
				case THREAD_BLOCK_TYPE_SIGNAL:
					waitObject = 0;
					break;
				case THREAD_BLOCK_TYPE_SEMAPHORE:
				case THREAD_BLOCK_TYPE_CONDITION_VARIABLE:
				case THREAD_BLOCK_TYPE_MUTEX:
				case THREAD_BLOCK_TYPE_RW_LOCK:
				case THREAD_BLOCK_TYPE_OTHER:
				default:
					break;
			}

			_AddThreadWaitObject(thread,
				event->previous_thread_wait_object_type, waitObject);
		}

		thread->lastTime = fState.LastEventTime();
		thread->state = WAITING;
	} else if (thread->state == UNKNOWN) {
		uint32 threadState = event->previous_thread_state;
		if (threadState == B_THREAD_WAITING
			|| threadState == B_THREAD_SUSPENDED) {
			thread->lastTime = fState.LastEventTime();
			thread->state = WAITING;
		} else if (threadState == B_THREAD_READY) {
			thread->lastTime = fState.LastEventTime();
			thread->state = PREEMPTED;
		}
	}
}


void
ModelLoader::_HandleThreadEnqueuedInRunQueue(
	thread_enqueued_in_run_queue* event)
{
	_UpdateLastEventTime(event->time);

	Model::ThreadSchedulingState* thread = fState.LookupThread(event->thread);
	if (thread == NULL) {
		printf("Enqueued in run queue event for unknown thread: %ld\n",
			event->thread);
		return;
	}

	if (thread->state == RUNNING || thread->state == STILL_RUNNING) {
		// Thread was running and is reentered into the run queue. This
		// is done by the scheduler, if the thread remains ready.
		thread->state = STILL_RUNNING;
	} else {
		// Thread was waiting and is ready now.
		bigtime_t diffTime = fState.LastEventTime() - thread->lastTime;
		if (thread->waitObject != NULL) {
			thread->waitObject->AddWait(diffTime);
			thread->waitObject = NULL;
			thread->thread->AddWait(diffTime);
		} else if (thread->state != UNKNOWN)
			thread->thread->AddUnspecifiedWait(diffTime);

		thread->lastTime = fState.LastEventTime();
		thread->state = READY;
	}
}


void
ModelLoader::_HandleThreadRemovedFromRunQueue(
	thread_removed_from_run_queue* event)
{
	_UpdateLastEventTime(event->time);

	Model::ThreadSchedulingState* thread = fState.LookupThread(event->thread);
	if (thread == NULL) {
		printf("Removed from run queue event for unknown thread: %ld\n",
			event->thread);
		return;
	}

	// This really only happens when the thread priority is changed
	// while the thread is ready.

	bigtime_t diffTime = fState.LastEventTime() - thread->lastTime;
	if (thread->state == RUNNING) {
		// This should never happen.
		thread->thread->AddRun(diffTime);
	} else if (thread->state == READY || thread->state == PREEMPTED) {
		// Not really correct, but the case is rare and we keep it
		// simple.
		thread->thread->AddUnspecifiedWait(diffTime);
	}

	thread->lastTime = fState.LastEventTime();
	thread->state = WAITING;
}


void
ModelLoader::_HandleWaitObjectInfo(system_profiler_wait_object_info* event)
{
	if (fModel->AddWaitObject(event, NULL) == NULL)
		throw std::bad_alloc();
}


Model::ThreadSchedulingState*
ModelLoader::_AddThread(system_profiler_thread_added* event)
{
	// do we know the thread already?
	Model::ThreadSchedulingState* info = fState.LookupThread(event->thread);
	if (info != NULL) {
		// TODO: ?
		return info;
	}

	// add the thread to the model
	Model::Thread* thread = fModel->AddThread(event, fState.LastEventTime());
	if (thread == NULL)
		return NULL;

	// create and add a ThreadSchedulingState
	info = new(std::nothrow) Model::ThreadSchedulingState(thread);
	if (info == NULL)
		return NULL;

	fState.InsertThread(info);

	return info;
}


void
ModelLoader::_AddThreadWaitObject(Model::ThreadSchedulingState* thread,
	uint32 type, addr_t object)
{
	Model::WaitObjectGroup* waitObjectGroup
		= fModel->WaitObjectGroupFor(type, object);
	if (waitObjectGroup == NULL) {
		// The algorithm should prevent this case.
printf("ModelLoader::_AddThreadWaitObject(): Unknown wait object: type: %lu, "
"object: %#lx\n", type, object);
		return;
	}

	Model::WaitObject* waitObject = waitObjectGroup->MostRecentWaitObject();

	Model::ThreadWaitObjectGroup* threadWaitObjectGroup
		= fModel->ThreadWaitObjectGroupFor(thread->ID(), type, object);

	if (threadWaitObjectGroup == NULL
		|| threadWaitObjectGroup->MostRecentWaitObject() != waitObject) {
		Model::ThreadWaitObject* threadWaitObject
			= fModel->AddThreadWaitObject(thread->ID(), waitObject,
				&threadWaitObjectGroup);
		if (threadWaitObject == NULL)
			throw std::bad_alloc();
	}

	thread->waitObject = threadWaitObjectGroup->MostRecentThreadWaitObject();
}
