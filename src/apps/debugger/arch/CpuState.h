/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef CPU_STATE_H
#define CPU_STATE_H

#include <OS.h>

#include <Referenceable.h>
#include <Variant.h>

#include "Types.h"


class Register;


class CpuState : public Referenceable {
public:
	virtual						~CpuState();

	virtual	target_addr_t		InstructionPointer() const = 0;
	virtual	bool				GetRegisterValue(const Register* reg,
									BVariant& _value) = 0;
};


#endif	// CPU_STATE_H
