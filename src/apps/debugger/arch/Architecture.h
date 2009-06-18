/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef ARCHITECTURE_H
#define ARCHITECTURE_H

#include <OS.h>

#include <Referenceable.h>


class CpuState;
class DebuggerInterface;


class Architecture : public Referenceable {
public:
								Architecture(
									DebuggerInterface* debuggerInterface);
	virtual						~Architecture();

	virtual	status_t			CreateCpuState(const void* cpuStateData,
									size_t size, CpuState*& _state) = 0;

protected:
			DebuggerInterface*	fDebuggerInterface;
};


#endif	// ARCHITECTURE_H
