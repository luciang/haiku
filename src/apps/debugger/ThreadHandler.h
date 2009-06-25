/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include <Referenceable.h>
#include <util/OpenHashTable.h>

#include "Breakpoint.h"
#include "DebugEvent.h"
#include "ImageDebugInfoProvider.h"
#include "Thread.h"


class BreakpointManager;
class DebuggerInterface;
class StackFrame;
class Statement;
class TeamDebugModel;
class Worker;


class ThreadHandler : public Referenceable,
	public HashTableLink<ThreadHandler>, private ImageDebugInfoProvider,
	private BreakpointClient {
public:
								ThreadHandler(TeamDebugModel* debugModel,
									Thread* thread, Worker* worker,
									DebuggerInterface* debuggerInterface,
									BreakpointManager* breakpointManager);
								~ThreadHandler();

			void				Init();

			thread_id			ThreadID() const	{ return fThread->ID(); }
			Thread*				GetThread() const	{ return fThread; }

			// All Handle*() methods are invoked in team debugger thread,
			// looper lock held.
			bool				HandleThreadDebugged(
									ThreadDebuggedEvent* event);
			bool				HandleDebuggerCall(
									DebuggerCallEvent* event);
			bool				HandleBreakpointHit(
									BreakpointHitEvent* event);
			bool				HandleWatchpointHit(
									WatchpointHitEvent* event);
			bool				HandleSingleStep(
									SingleStepEvent* event);
			bool				HandleExceptionOccurred(
									ExceptionOccurredEvent* event);

			void				HandleThreadAction(uint32 action);

			void				HandleThreadStateChanged();
			void				HandleCpuStateChanged();
			void				HandleStackTraceChanged();

private:
	// ImageDebugInfoProvider
	virtual	status_t			GetImageDebugInfo(Image* image,
									ImageDebugInfo*& _info);

			bool				_HandleThreadStopped(CpuState* cpuState);

			void				_SetThreadState(uint32 state,
									CpuState* cpuState);

			Statement*			_GetStatementAtInstructionPointer(
									StackFrame* frame);

			void				_StepFallback();
			bool				_DoStepOver(CpuState* cpuState);

			status_t			_InstallTemporaryBreakpoint(
									target_addr_t address);
			void				_UninstallTemporaryBreakpoint();
			void				_ClearContinuationState();
			void				_RunThread(target_addr_t instructionPointer);
			void				_SingleStepThread(
									target_addr_t instructionPointer);


			bool				_HandleBreakpointHitStep(CpuState* cpuState);
			bool				_HandleSingleStepStep(CpuState* cpuState);

private:
			TeamDebugModel*		fDebugModel;
			Thread*				fThread;
			Worker*				fWorker;
			DebuggerInterface*	fDebuggerInterface;
			BreakpointManager*	fBreakpointManager;
			uint32				fStepMode;
			Statement*			fStepStatement;
			target_addr_t		fBreakpointAddress;
			target_addr_t		fPreviousInstructionPointer;
			bool				fSingleStepping;
};


struct ThreadHandlerHashDefinition {
	typedef thread_id		KeyType;
	typedef	ThreadHandler	ValueType;

	size_t HashKey(thread_id key) const
	{
		return (size_t)key;
	}

	size_t Hash(const ThreadHandler* value) const
	{
		return HashKey(value->ThreadID());
	}

	bool Compare(thread_id key, ThreadHandler* value) const
	{
		return value->ThreadID() == key;
	}

	HashTableLink<ThreadHandler>* GetLink(ThreadHandler* value) const
	{
		return value;
	}
};

typedef OpenHashTable<ThreadHandlerHashDefinition> ThreadHandlerTable;


#endif	// THREAD_HANDLER_H
