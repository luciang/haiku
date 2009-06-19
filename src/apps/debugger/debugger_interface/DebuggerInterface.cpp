/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "DebuggerInterface.h"

#include <new>

#include <stdio.h>

#include <Locker.h>

#include <AutoLocker.h>
#include <util/DoublyLinkedList.h>

#include "debug_utils.h"

#include "ArchitectureX86.h"
#include "CpuState.h"
#include "DebugEvent.h"
#include "ImageInfo.h"
#include "ThreadInfo.h"


// number of debug contexts the pool does initially create
static const int kInitialDebugContextCount = 3;

// maximum number of debug contexts in the pool
static const int kMaxDebugContextCount = 10;


struct DebuggerInterface::DebugContext : debug_context,
		DoublyLinkedListLinkImpl<DebugContext> {
	DebugContext()
	{
		team = -1;
		nub_port = -1;
		reply_port = -1;
	}

	~DebugContext()
	{
		if (reply_port >= 0)
			destroy_debug_context(this);
	}

	status_t Init(team_id team, port_id nubPort)
	{
		return init_debug_context(this, team, nubPort);
	}

	void Close()
	{
		if (reply_port >= 0) {
			destroy_debug_context(this);
			team = -1;
			nub_port = -1;
			reply_port = -1;
		}
	}
};


struct DebuggerInterface::DebugContextPool {
	DebugContextPool(team_id team, port_id nubPort)
		:
		fLock("debug context pool"),
		fTeam(team),
		fNubPort(nubPort),
		fBlockSem(-1),
		fContextCount(0),
		fWaiterCount(0),
		fClosed(false)
	{
	}

	~DebugContextPool()
	{
		AutoLocker<BLocker> locker(fLock);

		while (DebugContext* context = fFreeContexts.RemoveHead())
			delete context;

		if (fBlockSem >= 0)
			delete_sem(fBlockSem);
	}

	status_t Init()
	{
		status_t error = fLock.InitCheck();
		if (error != B_OK)
			return error;

		fBlockSem = create_sem(0, "debug context pool block");
		if (fBlockSem < 0)
			return fBlockSem;

		for (int i = 0; i < kInitialDebugContextCount; i++) {
			DebugContext* context;
			error = _CreateDebugContext(context);
			if (error != B_OK)
				return error;

			fFreeContexts.Add(context);
		}

		return B_OK;
	}

	void Close()
	{
		AutoLocker<BLocker> locker(fLock);
		fClosed = true;

		for (DebugContextList::Iterator it = fFreeContexts.GetIterator();
				DebugContext* context = it.Next();) {
			context->Close();
		}

		for (DebugContextList::Iterator it = fUsedContexts.GetIterator();
				DebugContext* context = it.Next();) {
			context->Close();
		}
	}

	DebugContext* GetContext()
	{
		AutoLocker<BLocker> locker(fLock);
		DebugContext* context = fFreeContexts.RemoveHead();

		if (context == NULL) {
			if (fContextCount >= kMaxDebugContextCount
				|| _CreateDebugContext(context) != B_OK) {
				// wait for a free context
				while (context == NULL) {
					fWaiterCount++;
					locker.Unlock();
					while (acquire_sem(fBlockSem) != B_OK);
					locker.Lock();
					context = fFreeContexts.RemoveHead();
				}
			}
		}

		fUsedContexts.Add(context);

		return context;
	}

	void PutContext(DebugContext* context)
	{
		AutoLocker<BLocker> locker(fLock);
		fUsedContexts.Remove(context);
		fFreeContexts.Add(context);

		if (fWaiterCount > 0)
			release_sem(fBlockSem);
	}

private:
	typedef DoublyLinkedList<DebugContext> DebugContextList;

private:
	status_t _CreateDebugContext(DebugContext*& _context)
	{
		DebugContext* context = new(std::nothrow) DebugContext;
		if (context == NULL)
			return B_NO_MEMORY;

		if (!fClosed) {
			status_t error = context->Init(fTeam, fNubPort);
			if (error != B_OK) {
				delete context;
				return error;
			}
		}

		fContextCount++;

		_context = context;
		return B_OK;
	}

private:
	BLocker				fLock;
	team_id				fTeam;
	port_id				fNubPort;
	sem_id				fBlockSem;
	int32				fContextCount;
	int32				fWaiterCount;
	DebugContextList	fFreeContexts;
	DebugContextList	fUsedContexts;
	bool				fClosed;
};


struct DebuggerInterface::DebugContextGetter {
	DebugContextGetter(DebugContextPool* pool)
		:
		fPool(pool),
		fContext(pool->GetContext())
	{
	}

	~DebugContextGetter()
	{
		fPool->PutContext(fContext);
	}

	DebugContext* Context() const
	{
		return fContext;
	}

private:
	DebugContextPool*	fPool;
	DebugContext*		fContext;
};

// #pragma mark - DebuggerInterface

DebuggerInterface::DebuggerInterface(team_id teamID)
	:
	fTeamID(teamID),
	fDebuggerPort(-1),
	fNubPort(-1),
	fDebugContextPool(NULL),
	fArchitecture(NULL)
{
}


DebuggerInterface::~DebuggerInterface()
{
	fArchitecture->RemoveReference();

	Close();

	delete fDebugContextPool;
}


status_t
DebuggerInterface::Init()
{
	// create the architecture
#ifdef ARCH_x86
	fArchitecture = new(std::nothrow) ArchitectureX86(this);
#else
	return B_UNSUPPORTED;
#endif

	if (fArchitecture == NULL)
		return B_NO_MEMORY;

	status_t error = fArchitecture->Init();
	if (error != B_OK)
		return error;

	// create debugger port
	char buffer[128];
	snprintf(buffer, sizeof(buffer), "team %ld debugger", fTeamID);
	fDebuggerPort = create_port(100, buffer);
	if (fDebuggerPort < 0)
		return fDebuggerPort;

	// install as team debugger
	fNubPort = install_team_debugger(fTeamID, fDebuggerPort);
	if (fNubPort < 0)
		return fNubPort;

	// create debug context pool
	fDebugContextPool = new(std::nothrow) DebugContextPool(fTeamID, fNubPort);
	if (fDebugContextPool == NULL)
		return B_NO_MEMORY;

	error = fDebugContextPool->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


void
DebuggerInterface::Close()
{
	if (fDebuggerPort >= 0)
		delete_port(fDebuggerPort);
}


status_t
DebuggerInterface::GetNextDebugEvent(DebugEvent*& _event)
{
	while (true) {
		debug_debugger_message_data message;
		int32 messageCode;
		ssize_t size = read_port(fDebuggerPort, &messageCode, &message,
			sizeof(message));
		if (size < 0) {
			if (size == B_INTERRUPTED)
				continue;
			return size;
		}

		if (message.origin.team != fTeamID)
			continue;

		bool ignore = false;
		status_t error = _CreateDebugEvent(messageCode, message, ignore,
			_event);
		if (error != B_OK)
			return error;

		if (ignore) {
			if (message.origin.thread >= 0 && message.origin.nub_port >= 0)
				continue_thread(message.origin.nub_port, message.origin.thread);
			continue;
		}

		return B_OK;
	}
}


status_t
DebuggerInterface::SetTeamDebuggingFlags(uint32 flags)
{
	set_team_debugging_flags(fNubPort, flags);
	return B_OK;
}


status_t
DebuggerInterface::ContinueThread(thread_id thread)
{
	continue_thread(fNubPort, thread);
	return B_OK;
}


status_t
DebuggerInterface::StopThread(thread_id thread)
{
	return debug_thread(thread);
}


status_t
DebuggerInterface::SingleStepThread(thread_id thread)
{
	debug_nub_continue_thread continueMessage;
	continueMessage.thread = thread;
	continueMessage.handle_event = B_THREAD_DEBUG_HANDLE_EVENT;
	continueMessage.single_step = true;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CONTINUE_THREAD,
		&continueMessage, sizeof(continueMessage));
}


status_t
DebuggerInterface::GetThreadInfos(BObjectList<ThreadInfo>& infos)
{
	thread_info threadInfo;
	int32 cookie = 0;
	while (get_next_thread_info(fTeamID, &cookie, &threadInfo) == B_OK) {
		ThreadInfo* info = new(std::nothrow) ThreadInfo(threadInfo.team,
			threadInfo.thread, threadInfo.name);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
DebuggerInterface::GetImageInfos(BObjectList<ImageInfo>& infos)
{
	image_info imageInfo;
	int32 cookie = 0;
	while (get_next_image_info(fTeamID, &cookie, &imageInfo) == B_OK) {
		ImageInfo* info = new(std::nothrow) ImageInfo(fTeamID, imageInfo.id,
			imageInfo.name);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
DebuggerInterface::GetThreadInfo(thread_id thread, ThreadInfo& info)
{
	thread_info threadInfo;
	status_t error = get_thread_info(thread, &threadInfo);
	if (error != B_OK)
		return error;

	info.SetTo(threadInfo.team, threadInfo.thread, threadInfo.name);
	return B_OK;
}


status_t
DebuggerInterface::GetCpuState(thread_id thread, CpuState*& _state)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_get_cpu_state message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.thread = thread;

	debug_nub_get_cpu_state_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_GET_CPU_STATE, &message, sizeof(message), &reply,
		sizeof(reply));
	if (error != B_OK)
		return error;
	if (reply.error != B_OK)
		return reply.error;

	return fArchitecture->CreateCpuState(&reply.cpu_state,
		sizeof(debug_cpu_state), _state);
}


ssize_t
DebuggerInterface::ReadMemory(target_addr_t address, void* buffer, size_t size)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	return debug_read_memory(contextGetter.Context(),
		(const void*)(addr_t)address, buffer, size);
}


status_t
DebuggerInterface::_CreateDebugEvent(int32 messageCode,
	const debug_debugger_message_data& message, bool& _ignore,
	DebugEvent*& _event)
{
	DebugEvent* event = NULL;

	switch (messageCode) {
		case B_DEBUGGER_MESSAGE_THREAD_DEBUGGED:
			event = new(std::nothrow) ThreadDebuggedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_DEBUGGER_CALL:
			event = new(std::nothrow) DebuggerCallEvent(message.origin.team,
				message.origin.thread,
				(target_addr_t)message.debugger_call.message);
			break;
		case B_DEBUGGER_MESSAGE_BREAKPOINT_HIT:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.breakpoint_hit.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) BreakpointHitEvent(message.origin.team,
				message.origin.thread, state);
			state->RemoveReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_WATCHPOINT_HIT:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.watchpoint_hit.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) WatchpointHitEvent(message.origin.team,
				message.origin.thread, state);
			state->RemoveReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_SINGLE_STEP:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.single_step.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) SingleStepEvent(message.origin.team,
				message.origin.thread, state);
			state->RemoveReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED:
			event = new(std::nothrow) ExceptionOccurredEvent(
				message.origin.team, message.origin.thread,
				message.exception_occurred.exception);
			break;
		case B_DEBUGGER_MESSAGE_TEAM_DELETED:
			if (message.origin.team != fTeamID) {
				_ignore = true;
				return B_OK;
			}
			event = new(std::nothrow) TeamDeletedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_TEAM_EXEC:
			if (message.origin.team != fTeamID) {
				_ignore = true;
				return B_OK;
			}
			event = new(std::nothrow) TeamExecEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_THREAD_CREATED:
			event = new(std::nothrow) ThreadCreatedEvent(message.origin.team,
				message.origin.thread, message.thread_created.new_thread);
			break;
		case B_DEBUGGER_MESSAGE_THREAD_DELETED:
			event = new(std::nothrow) ThreadDeletedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_IMAGE_CREATED:
		{
			const image_info& info = message.image_created.info;
			event = new(std::nothrow) ImageCreatedEvent(message.origin.team,
				message.origin.thread, ImageInfo(fTeamID, info.id, info.name));
			break;
		}
		case B_DEBUGGER_MESSAGE_IMAGE_DELETED:
		{
			const image_info& info = message.image_deleted.info;
			event = new(std::nothrow) ImageDeletedEvent(message.origin.team,
				message.origin.thread, ImageInfo(fTeamID, info.id, info.name));
			break;
		}
		default:
			printf("DebuggerInterface for team %ld: unknown message from "
				"kernel: %ld\n", fTeamID, messageCode);
			// fall through...
		case B_DEBUGGER_MESSAGE_TEAM_CREATED:
		case B_DEBUGGER_MESSAGE_PRE_SYSCALL:
		case B_DEBUGGER_MESSAGE_POST_SYSCALL:
		case B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED:
		case B_DEBUGGER_MESSAGE_PROFILER_UPDATE:
		case B_DEBUGGER_MESSAGE_HANDED_OVER:
			_ignore = true;
			return B_OK;
	}

	if (event == NULL)
		return B_NO_MEMORY;

	if (message.origin.thread >= 0 && message.origin.nub_port >= 0)
		event->SetThreadStopped(true);

	_ignore = false;
	_event = event;

	return B_OK;
}
