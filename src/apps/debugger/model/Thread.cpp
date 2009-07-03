/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "Thread.h"

#include "CpuState.h"
#include "StackTrace.h"
#include "Team.h"


Thread::Thread(Team* team, thread_id threadID)
	:
	fTeam(team),
	fID(threadID),
	fState(THREAD_STATE_UNKNOWN),
	fCpuState(NULL),
	fStackTrace(NULL)
{
}


Thread::~Thread()
{
	if (fCpuState != NULL)
		fCpuState->RemoveReference();
	if (fStackTrace != NULL)
		fStackTrace->RemoveReference();
}


status_t
Thread::Init()
{
	return B_OK;
}


bool
Thread::IsMainThread() const
{
	return fID == fTeam->ID();
}


void
Thread::SetName(const BString& name)
{
	fName = name;
}


void
Thread::SetState(uint32 state)
{
	if (state == fState)
		return;

	fState = state;

	// unset CPU state and stack trace, if the thread isn't stopped
	if (fState != THREAD_STATE_STOPPED) {
		SetCpuState(NULL);
		SetStackTrace(NULL);
	}

	fTeam->NotifyThreadStateChanged(this);
}


void
Thread::SetCpuState(CpuState* state)
{
	if (state == fCpuState)
		return;

	if (fCpuState != NULL)
		fCpuState->RemoveReference();

	fCpuState = state;

	if (fCpuState != NULL)
		fCpuState->AddReference();

	fTeam->NotifyThreadCpuStateChanged(this);
}


void
Thread::SetStackTrace(StackTrace* trace)
{
	if (trace == fStackTrace)
		return;

	if (fStackTrace != NULL)
		fStackTrace->RemoveReference();

	fStackTrace = trace;

	if (fStackTrace != NULL)
		fStackTrace->AddReference();

	fTeam->NotifyThreadStackTraceChanged(this);
}
