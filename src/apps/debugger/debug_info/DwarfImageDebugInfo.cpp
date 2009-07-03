/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "DwarfImageDebugInfo.h"

#include <stdio.h>
#include <unistd.h>

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "Architecture.h"
#include "CompilationUnit.h"
#include "DebugInfoEntries.h"
#include "Dwarf.h"
#include "DwarfFile.h"
#include "DwarfFunctionDebugInfo.h"
#include "DwarfUtils.h"
#include "ElfFile.h"
#include "FileManager.h"
#include "FileSourceCode.h"
#include "LocatableFile.h"
#include "SourceFile.h"
#include "Statement.h"
#include "StringUtils.h"


// #pragma mark - SourceCodeEntry


struct DwarfImageDebugInfo::SourceCodeKey {
	CompilationUnit*	unit;
	BString				filePath;

	SourceCodeKey(CompilationUnit* unit, const BString& filePath)
		:
		unit(unit),
		filePath(filePath)
	{
	}

	SourceCodeKey(CompilationUnit* unit, LocatableFile* file)
		:
		unit(unit)
	{
		file->GetLocatedPath(filePath);
	}

	uint32 HashValue() const
	{
		return (uint32)(addr_t)unit ^ StringUtils::HashValue(filePath);
	}

	bool operator==(const SourceCodeKey& other) const
	{
		return unit == other.unit && filePath == other.filePath;
	}
};


struct DwarfImageDebugInfo::SourceCodeEntry : SourceCodeKey,
		HashTableLink<SourceCodeEntry> {
	FileSourceCode*		sourceCode;

	SourceCodeEntry(CompilationUnit* unit, LocatableFile* file,
		FileSourceCode* sourceCode)
		:
		SourceCodeKey(unit, file),
		sourceCode(sourceCode)
	{
	}
};


// #pragma mark - SourceCodeHashDefinition


struct DwarfImageDebugInfo::SourceCodeHashDefinition {
	typedef SourceCodeKey	KeyType;
	typedef	SourceCodeEntry	ValueType;

	size_t HashKey(const SourceCodeKey& key) const
	{
		return key.HashValue();
	}

	size_t Hash(const SourceCodeEntry* value) const
	{
		return value->HashValue();
	}

	bool Compare(const SourceCodeKey& key, const SourceCodeEntry* value) const
	{
		return key == *value;
	}

	HashTableLink<SourceCodeEntry>* GetLink(SourceCodeEntry* value) const
	{
		return value;
	}
};


// #pragma mark - DwarfImageDebugInfo


DwarfImageDebugInfo::DwarfImageDebugInfo(const ImageInfo& imageInfo,
	Architecture* architecture, FileManager* fileManager, DwarfFile* file)
	:
	fLock("dwarf image debug info"),
	fImageInfo(imageInfo),
	fArchitecture(architecture),
	fFileManager(fileManager),
	fFile(file),
	fTextSegment(NULL),
	fRelocationDelta(0),
	fSourceCodes(NULL)
{
}


DwarfImageDebugInfo::~DwarfImageDebugInfo()
{
	SourceCodeEntry* entry = fSourceCodes->Clear(true);
	while (entry != NULL) {
		SourceCodeEntry* next = entry->fNext;
		entry->sourceCode->ReleaseReference();
		entry = next;
	}

	delete fSourceCodes;
}


status_t
DwarfImageDebugInfo::Init()
{
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	fSourceCodes = new (std::nothrow) SourceCodeTable;
	if (fSourceCodes == NULL)
		return B_NO_MEMORY;

	error = fSourceCodes->Init();
	if (error != B_OK)
		return error;

	fTextSegment = fFile->GetElfFile()->TextSegment();
	if (fTextSegment == NULL)
		return B_ENTRY_NOT_FOUND;

	fRelocationDelta = fImageInfo.TextBase() - fTextSegment->LoadAddress();

	return B_OK;
}


status_t
DwarfImageDebugInfo::GetFunctions(BObjectList<FunctionDebugInfo>& functions)
{
printf("DwarfImageDebugInfo::GetFunctions()\n");
printf("  %ld compilation units\n", fFile->CountCompilationUnits());

	for (int32 i = 0; CompilationUnit* unit = fFile->CompilationUnitAt(i);
			i++) {
		DIECompileUnitBase* unitEntry = unit->UnitEntry();
//		printf("  %s:\n", unitEntry->Name());
//		printf("    address ranges:\n");
//		TargetAddressRangeList* rangeList = unitEntry->AddressRanges();
//		if (rangeList != NULL) {
//			int32 count = rangeList->CountRanges();
//			for (int32 i = 0; i < count; i++) {
//				TargetAddressRange range = rangeList->RangeAt(i);
//				printf("      %#llx - %#llx\n", range.Start(), range.End());
//			}
//		} else {
//			printf("      %#llx - %#llx\n", (target_addr_t)unitEntry->LowPC(),
//				(target_addr_t)unitEntry->HighPC());
//		}

//		printf("    functions:\n");
		for (DebugInfoEntryList::ConstIterator it
					= unitEntry->OtherChildren().GetIterator();
				DebugInfoEntry* entry = it.Next();) {
			if (entry->Tag() != DW_TAG_subprogram)
				continue;

			DIESubprogram* subprogramEntry = static_cast<DIESubprogram*>(entry);

			// ignore declarations, prototypes, and inlined functions
			if (subprogramEntry->IsDeclaration()
				|| subprogramEntry->IsPrototyped()
				|| subprogramEntry->Inline() == DW_INL_inlined
				|| subprogramEntry->Inline() == DW_INL_declared_inlined
				|| subprogramEntry->AbstractOrigin() != NULL) {
				continue;
			}

			// get the name
			BString name;
			DwarfUtils::GetFullyQualifiedDIEName(subprogramEntry, name);
			if (name.Length() == 0)
				continue;

			// get the address ranges
			TargetAddressRangeList* rangeList
				= subprogramEntry->AddressRanges();
			Reference<TargetAddressRangeList> rangeListReference(rangeList);
			if (rangeList == NULL) {
				target_addr_t lowPC = subprogramEntry->LowPC();
				target_addr_t highPC = subprogramEntry->HighPC();
				if (lowPC >= highPC)
					continue;

				rangeList = new(std::nothrow) TargetAddressRangeList(
					TargetAddressRange(lowPC, highPC - lowPC));
				if (rangeList == NULL)
					return B_NO_MEMORY;
						// TODO: Clean up already added functions!
				rangeListReference.SetTo(rangeList, true);
			}

			// get the source location
			const char* directoryPath = NULL;
			const char* fileName = NULL;
			uint32 line = -1;
			uint32 column = -1;
			DwarfUtils::GetDeclarationLocation(fFile, subprogramEntry,
				directoryPath, fileName, line, column);

			LocatableFile* file = NULL;
			if (fileName != NULL) {
				file = fFileManager->GetSourceFile(directoryPath,
					fileName);
			}
			Reference<LocatableFile> fileReference(file, true);

			// create and add the functions
			DwarfFunctionDebugInfo* function
				= new(std::nothrow) DwarfFunctionDebugInfo(this, unit,
					subprogramEntry, rangeList, name, file,
					SourceLocation(line, column));
			if (function == NULL || !functions.AddItem(function)) {
				delete function;
				return B_NO_MEMORY;
					// TODO: Clean up already added functions!
			}

//			BString name;
//			DwarfUtils::GetFullyQualifiedDIEName(subprogramEntry, name);
//			printf("      subprogram entry: %p, name: %s, declaration: %d\n",
//				subprogramEntry, name.String(),
//				subprogramEntry->IsDeclaration());
//
//			rangeList = subprogramEntry->AddressRanges();
//			if (rangeList != NULL) {
//				int32 count = rangeList->CountRanges();
//				for (int32 i = 0; i < count; i++) {
//					TargetAddressRange range = rangeList->RangeAt(i);
//					printf("        %#llx - %#llx\n", range.Start(), range.End());
//				}
//			} else {
//				printf("        %#llx - %#llx\n",
//					(target_addr_t)subprogramEntry->LowPC(),
//					(target_addr_t)subprogramEntry->HighPC());
//			}
		}
	}

	return B_OK;
}


status_t
DwarfImageDebugInfo::CreateFrame(Image* image, FunctionDebugInfo* function,
	CpuState* cpuState, StackFrame*& _previousFrame,
	CpuState*& _previousCpuState)
{
	// TODO:...
	return B_UNSUPPORTED;
}


status_t
DwarfImageDebugInfo::LoadSourceCode(FunctionDebugInfo* function,
	SourceCode*& _sourceCode)
{
	AutoLocker<BLocker> locker(fLock);

	// TODO: Load the actual source code!
	status_t error = _LoadSourceCode(function, _sourceCode);
	if (error == B_OK)
		return B_OK;

	// fall back to disassembling

	static const target_size_t kMaxBufferSize = 64 * 1024;
	target_size_t bufferSize = std::min(function->Size(), kMaxBufferSize);
	void* buffer = malloc(bufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	// read the function code
	target_addr_t functionOffset = function->Address() - fRelocationDelta
		- fTextSegment->LoadAddress() + fTextSegment->FileOffset();
	ssize_t bytesRead = pread(fFile->GetElfFile()->FD(), buffer, bufferSize,
		functionOffset);
	if (bytesRead < 0)
		return bytesRead;

	return fArchitecture->DisassembleCode(function, buffer, bytesRead,
		_sourceCode);
}


status_t
DwarfImageDebugInfo::GetStatement(FunctionDebugInfo* function,
	target_addr_t address, Statement*& _statement)
{
	// TODO:...
	return fArchitecture->GetStatement(function, address, _statement);
}


status_t
DwarfImageDebugInfo::_LoadSourceCode(FunctionDebugInfo* _function,
	SourceCode*& _sourceCode)
{
	DwarfFunctionDebugInfo* function
		= dynamic_cast<DwarfFunctionDebugInfo*>(_function);
	if (function == NULL)
		return B_BAD_VALUE;

	// get the source file
	LocatableFile* file = function->SourceFile();
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;

	// maybe it's already loaded
	CompilationUnit* unit = function->GetCompilationUnit();
	FileSourceCode* sourceCode = _LookupSourceCode(unit, file);
	if (sourceCode) {
		sourceCode->AcquireReference();
		_sourceCode = sourceCode;
		return B_OK;
	}

	// get the index of the source file in the compilation unit for cheaper
	// comparison below
	const char* directory;
	int32 fileIndex = -1;
	for (int32 i = 0; const char* fileName = unit->FileAt(i, &directory); i++) {
		LocatableFile* file = fFileManager->GetSourceFile(directory, fileName);
		if (file != NULL) {
			file->ReleaseReference();
			if (file == function->SourceFile()) {
				fileIndex = i + 1;
					// indices are one-based
				break;
			}
		}
	}
printf("DwarfImageDebugInfo::_LoadSourceCode(), file: %ld, function at: %#llx\n", fileIndex, function->Address());

for (int32 i = 0; const char* fileName = unit->FileAt(i, &directory); i++) {
printf("  file %ld: %s\n", i, fileName);
}


	// not loaded yet -- get the source file
	SourceFile* sourceFile;
	status_t error = fFileManager->LoadSourceFile(file, sourceFile);
	if (error != B_OK)
		return error;

	// create the source code
	sourceCode = new(std::nothrow) FileSourceCode(sourceFile);
	sourceFile->ReleaseReference();
	if (sourceCode == NULL)
		return B_NO_MEMORY;

	error = sourceCode->Init();
	if (error != B_OK)
		return error;
	ObjectDeleter<FileSourceCode> sourceCodeDeleter(sourceCode);

	// Get the statements by executing the line number program for the
	// compilation unit and filtering the rows for our source file.
	LineNumberProgram& program = unit->GetLineNumberProgram();
	if (!program.IsValid())
		return B_BAD_DATA;

	LineNumberProgram::State state;
	program.GetInitialState(state);

	target_addr_t statementAddress = 0;
	int32 statementLine = -1;
	int32 statementColumn = -1;
	while (program.GetNextRow(state)) {
printf("  %#lx  (%ld, %ld, %ld)  %d\n", state.address, state.file, state.line, state.column, state.isStatement);
		bool isOurFile = state.file == fileIndex;

		if (statementAddress != 0
			&& (!isOurFile || state.isStatement || state.isSequenceEnd)) {
			target_addr_t endAddress = state.address;
			if (endAddress >  statementAddress) {
				// add the statement
				ContiguousStatement* statement = new(std::nothrow)
					ContiguousStatement(
						SourceLocation(statementLine, statementColumn),
						SourceLocation(statementLine, statementColumn),
						TargetAddressRange(fRelocationDelta + statementAddress,
							endAddress - statementAddress), true);
				if (statement == NULL)
					return B_NO_MEMORY;

				error = sourceCode->AddStatement(statement);
				if (error != B_OK) {
					delete statement;
					return error;
				}
printf("  -> statement: %#llx - %#llx, line: %ld\n", statement->AddressRange().Start(),
statement->AddressRange().End(), statementLine);
			}

			statementAddress = 0;
		}

		// skip statements of other files
		if (!isOurFile)
			continue;

		if (state.isStatement) {
			statementAddress = state.address;
			statementLine = state.line - 1;
			statementColumn = state.column - 1;
		}
	}

	SourceCodeEntry* entry = new(std::nothrow) SourceCodeEntry(unit, file,
		sourceCode);
	if (entry == NULL)
		return B_NO_MEMORY;

	fSourceCodes->Insert(entry);

	_sourceCode = sourceCodeDeleter.Detach();
	_sourceCode->AcquireReference();
	return B_OK;
}


FileSourceCode*
DwarfImageDebugInfo::_LookupSourceCode(CompilationUnit* unit,
	LocatableFile* file)
{
	SourceCodeEntry* entry = fSourceCodes->Lookup(SourceCodeKey(unit, file));
	return entry != NULL ? entry->sourceCode : NULL;
}
