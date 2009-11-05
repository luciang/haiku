/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef VALUE_LOADER_H
#define VALUE_LOADER_H


#include <String.h>

#include <Variant.h>


class Architecture;
class CpuState;
class TeamMemory;
class ValueLocation;


class ValueLoader {
public:
								ValueLoader(Architecture* architecture,
									TeamMemory* teamMemory, CpuState* cpuState);
									// cpuState can be NULL
								~ValueLoader();

			Architecture*		GetArchitecture() const
									{ return fArchitecture; }

			status_t			LoadValue(ValueLocation* location,
									type_code valueType, bool shortValueIsFine,
									BVariant& _value);

private:
			Architecture*		fArchitecture;
			TeamMemory*			fTeamMemory;
			CpuState*			fCpuState;
};


#endif	// VALUE_LOADER_H
