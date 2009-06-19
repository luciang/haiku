/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "ArchitectureX86.h"

#include <new>

#include "CpuStateX86.h"


ArchitectureX86::ArchitectureX86(DebuggerInterface* debuggerInterface)
	:
	Architecture(debuggerInterface)
{
}


ArchitectureX86::~ArchitectureX86()
{
}


status_t
ArchitectureX86::Init()
{
	try {
		_AddIntegerRegister(X86_REGISTER_EIP, "eip", 32,
			REGISTER_TYPE_PROGRAM_COUNTER);
		_AddIntegerRegister(X86_REGISTER_ESP, "esp", 32,
			REGISTER_TYPE_STACK_POINTER);
		_AddIntegerRegister(X86_REGISTER_EBP, "ebp", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);

		_AddIntegerRegister(X86_REGISTER_EAX, "eax", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_EBX, "ebx", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_ECX, "ecx", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_EDX, "edx", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);

		_AddIntegerRegister(X86_REGISTER_ESI, "esi", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_EDI, "edi", 32,
			REGISTER_TYPE_GENERAL_PURPOSE);

		_AddIntegerRegister(X86_REGISTER_CS, "cs", 16,
			REGISTER_TYPE_SPECIAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_DS, "ds", 16,
			REGISTER_TYPE_SPECIAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_ES, "es", 16,
			REGISTER_TYPE_SPECIAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_FS, "fs", 16,
			REGISTER_TYPE_SPECIAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_GS, "gs", 16,
			REGISTER_TYPE_SPECIAL_PURPOSE);
		_AddIntegerRegister(X86_REGISTER_SS, "ss", 16,
			REGISTER_TYPE_SPECIAL_PURPOSE);
	} catch (std::bad_alloc) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


int32
ArchitectureX86::CountRegisters() const
{
	return fRegisters.Count();
}


const Register*
ArchitectureX86::Registers() const
{
	return fRegisters.Elements();
}


status_t
ArchitectureX86::CreateCpuState(const void* cpuStateData, size_t size,
	CpuState*& _state)
{
	if (size != sizeof(debug_cpu_state_x86))
		return B_BAD_VALUE;

	CpuStateX86* state = new(std::nothrow) CpuStateX86(
		*(const debug_cpu_state_x86*)cpuStateData);
	if (state == NULL)
		return B_NO_MEMORY;

	_state = state;
	return B_OK;
}


void
ArchitectureX86::_AddRegister(int32 index, const char* name,
	register_format format, uint32 bitSize, register_type type)
{
	if (!fRegisters.Add(Register(index, name, format, bitSize, type)))
		throw std::bad_alloc();
}


void
ArchitectureX86::_AddIntegerRegister(int32 index, const char* name,
	uint32 bitSize, register_type type)
{
	_AddRegister(index, name, REGISTER_FORMAT_INTEGER, bitSize, type);
}
