/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPILATION_UNIT_H
#define COMPILATION_UNIT_H

#include <String.h>

#include <ObjectList.h>

#include "Array.h"
#include "DwarfTypes.h"
#include "LineNumberProgram.h"


class AbbreviationTable;
class DebugInfoEntry;
class DIECompileUnitBase;


class CompilationUnit {
public:
								CompilationUnit(dwarf_off_t headerOffset,
									dwarf_off_t contentOffset,
									dwarf_off_t totalSize,
									dwarf_off_t abbreviationOffset);
								~CompilationUnit();

			dwarf_off_t			HeaderOffset() const { return fHeaderOffset; }
			dwarf_off_t			ContentOffset() const { return fContentOffset; }
			dwarf_off_t 		RelativeContentOffset() const
									{ return fContentOffset - fHeaderOffset; }
			dwarf_off_t			TotalSize() const	{ return fTotalSize; }
			dwarf_off_t			ContentSize() const
									{ return fTotalSize
										- RelativeContentOffset(); }
			dwarf_off_t			AbbreviationOffset() const
									{ return fAbbreviationOffset; }

			AbbreviationTable*	GetAbbreviationTable() const
									{ return fAbbreviationTable; }
			void				SetAbbreviationTable(
									AbbreviationTable* abbreviationTable);

			DIECompileUnitBase*	UnitEntry() const	{ return fUnitEntry; }
			void				SetUnitEntry(DIECompileUnitBase* entry);

			LineNumberProgram&	GetLineNumberProgram()
									{ return fLineNumberProgram; }

			status_t			AddDebugInfoEntry(DebugInfoEntry* entry,
									dwarf_off_t offset);
			int					CountEntries() const;
			void				GetEntryAt(int index, DebugInfoEntry*& entry,
									dwarf_off_t& offset) const;
			DebugInfoEntry*		EntryForOffset(dwarf_off_t offset) const;

			bool				AddDirectory(const char* directory);
			int32				CountDirectories() const;
			const char*			DirectoryAt(int32 index) const;

			bool				AddFile(const char* fileName, int32 dirIndex);
			int32				CountFiles() const;
			const char*			FileAt(int32 index,
									const char** _directory = NULL) const;

private:
			struct File;
			typedef BObjectList<BString> DirectoryList;
			typedef BObjectList<File> FileList;

private:
			dwarf_off_t			fHeaderOffset;
			dwarf_off_t			fContentOffset;
			dwarf_off_t			fTotalSize;
			dwarf_off_t			fAbbreviationOffset;
			AbbreviationTable*	fAbbreviationTable;
			DIECompileUnitBase*	fUnitEntry;
			Array<DebugInfoEntry*> fEntries;
			Array<dwarf_off_t>	fEntryOffsets;
			DirectoryList		fDirectories;
			FileList			fFiles;
			LineNumberProgram	fLineNumberProgram;
};


#endif	// COMPILATION_UNIT_H
