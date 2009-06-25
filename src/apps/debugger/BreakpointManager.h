/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef BREAKPOINT_MANAGER_H
#define BREAKPOINT_MANAGER_H

#include <Locker.h>

#include "Breakpoint.h"


class DebuggerInterface;
class TeamDebugModel;


class BreakpointManager {
public:
								BreakpointManager(TeamDebugModel* debugModel,
									DebuggerInterface* debuggerInterface);
								~BreakpointManager();

			status_t			Init();

			status_t			InstallUserBreakpoint(target_addr_t address,
									bool enabled);
			void				UninstallUserBreakpoint(target_addr_t address);

			status_t			InstallTemporaryBreakpoint(
									target_addr_t address,
									BreakpointClient* client);
			void				UninstallTemporaryBreakpoint(
									target_addr_t address,
									BreakpointClient* client);

private:
			BLocker				fLock;	// used to synchronize un-/installing
			TeamDebugModel*		fDebugModel;
			DebuggerInterface*	fDebuggerInterface;
};


#endif	// BREAKPOINT_MANAGER_H
