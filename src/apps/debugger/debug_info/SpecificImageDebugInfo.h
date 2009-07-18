/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef SPECIFIC_IMAGE_DEBUG_INFO_H
#define SPECIFIC_IMAGE_DEBUG_INFO_H

#include <ObjectList.h>
#include <Referenceable.h>

#include "Types.h"


class Architecture;
class CpuState;
class DebuggerInterface;
class FileSourceCode;
class FunctionDebugInfo;
class FunctionInstance;
class Image;
class LocatableFile;
class SourceLanguage;
class SourceLocation;
class StackFrame;
class Statement;


class SpecificImageDebugInfo : public Referenceable {
public:
	virtual						~SpecificImageDebugInfo();

	virtual	status_t			GetFunctions(
									BObjectList<FunctionDebugInfo>& functions)
										= 0;
									// returns references

	virtual	status_t			CreateFrame(Image* image,
									FunctionInstance* functionInstance,
									CpuState* cpuState,
									StackFrame*& _previousFrame,
									CpuState*& _previousCpuState) = 0;
										// returns reference to previous frame
										// and CPU state; returned CPU state
										// can be NULL; can return B_UNSUPPORTED
	virtual	status_t			GetStatement(FunctionDebugInfo* function,
									target_addr_t address,
									Statement*& _statement) = 0;
										// returns reference
	virtual	status_t			GetStatementAtSourceLocation(
									FunctionDebugInfo* function,
									const SourceLocation& sourceLocation,
									Statement*& _statement) = 0;
										// returns reference

	virtual	status_t			GetSourceLanguage(FunctionDebugInfo* function,
									SourceLanguage*& _language) = 0;

	virtual	ssize_t				ReadCode(target_addr_t address, void* buffer,
									size_t size) = 0;

	virtual	status_t			AddSourceCodeInfo(LocatableFile* file,
									FileSourceCode* sourceCode) = 0;
};


#endif	// SPECIFIC_IMAGE_DEBUG_INFO_H
