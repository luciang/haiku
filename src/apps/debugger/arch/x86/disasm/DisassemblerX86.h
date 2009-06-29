/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef DISASSEMBLER_X86_H
#define DISASSEMBLER_X86_H

#include <String.h>

#include "Types.h"


class DisassemblerX86 {
public:
								DisassemblerX86();
	virtual						~DisassemblerX86();

	virtual	status_t			Init(target_addr_t address, const void* code,
									size_t codeSize);

	virtual	status_t			GetNextInstruction(BString& line,
									target_addr_t& _address,
									target_size_t& _size,
									bool& _breakpointAllowed);
	virtual	status_t			GetPreviousInstruction(
									target_addr_t nextAddress,
									target_addr_t& _address,
									target_size_t& _size);

private:
			struct UdisData;

private:
			target_addr_t		fAddress;
			const uint8*		fCode;
			size_t				fCodeSize;
			UdisData*			fUdisData;
};


#endif	// DISASSEMBLER_X86_H
