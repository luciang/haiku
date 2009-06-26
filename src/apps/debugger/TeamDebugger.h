/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef TEAM_DEBUGGER_H
#define TEAM_DEBUGGER_H

#include <debugger.h>
#include <Looper.h>

#include <debug_support.h>

#include "DebugEvent.h"
#include "Team.h"
#include "TeamWindow.h"
#include "ThreadHandler.h"
#include "Worker.h"


class DebuggerInterface;
class TeamDebugModel;


class TeamDebugger : public BLooper, private TeamWindow::Listener,
	private JobListener, private Team::Listener {
public:
	class Listener;

public:
								TeamDebugger(Listener* listener);
								~TeamDebugger();

			status_t			Init(team_id teamID, thread_id threadID,
									bool stopInMain);

			team_id				TeamID() const	{ return fTeamID; }

	virtual	void				MessageReceived(BMessage* message);

private:
	// TeamWindow::Listener
	virtual	void				FunctionSourceCodeRequested(TeamWindow* window,
									FunctionDebugInfo* function);
	virtual	void				ThreadActionRequested(TeamWindow* window,
									thread_id threadID, uint32 action);
	virtual	void				SetBreakpointRequested(target_addr_t address,
									bool enabled);
	virtual	void				ClearBreakpointRequested(target_addr_t address);
	virtual	bool				TeamWindowQuitRequested(TeamWindow* window);

	// JobListener
	virtual	void				JobDone(Job* job);
	virtual	void				JobFailed(Job* job);
	virtual	void				JobAborted(Job* job);

	// Team::Listener
	virtual	void				ThreadStateChanged(
									const ::Team::ThreadEvent& event);
	virtual	void				ThreadCpuStateChanged(
									const ::Team::ThreadEvent& event);
	virtual	void				ThreadStackTraceChanged(
									const ::Team::ThreadEvent& event);

private:
	static	status_t			_DebugEventListenerEntry(void* data);
			status_t			_DebugEventListener();

			void				_HandleDebuggerMessage(DebugEvent* event);

			bool				_HandleThreadCreated(
									ThreadCreatedEvent* event);
			bool				_HandleThreadDeleted(
									ThreadDeletedEvent* event);
			bool				_HandleImageCreated(
									ImageCreatedEvent* event);
			bool				_HandleImageDeleted(
									ImageDeletedEvent* event);

			void				_HandleSetUserBreakpoint(target_addr_t address,
									bool enabled);
			void				_HandleClearUserBreakpoint(
									target_addr_t address);


			ThreadHandler*		_GetThreadHandler(thread_id threadID);

			void				_NotifyUser(const char* title,
									const char* text,...);

private:
			Listener*			fListener;
			::Team*				fTeam;
			TeamDebugModel*		fDebugModel;
			team_id				fTeamID;
			ThreadHandlerTable	fThreadHandlers;
									// protected by the team lock
			DebuggerInterface*	fDebuggerInterface;
			Worker*				fWorker;
			BreakpointManager*	fBreakpointManager;
			thread_id			fDebugEventListener;
			TeamWindow*			fTeamWindow;
	volatile bool				fTerminating;
			bool				fKillTeamOnQuit;
};


class TeamDebugger::Listener {
public:
	virtual						~Listener();

	virtual	void				TeamDebuggerQuit(TeamDebugger* debugger) = 0;
};


#endif	// TEAM_DEBUGGER_H
