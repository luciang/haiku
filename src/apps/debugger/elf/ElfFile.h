/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef ELF_FILE_H
#define ELF_FILE_H

#include <sys/types.h>

#include <SupportDefs.h>

#include <elf32.h>
#include <util/DoublyLinkedList.h>

#include "Types.h"


class ElfSection : public DoublyLinkedListLinkImpl<ElfSection> {
public:
								ElfSection(const char* name, int fd,
									off_t offset, off_t size);
								~ElfSection();

			const char*			Name() const	{ return fName; }
			off_t				Offset() const	{ return fOffset; }
			off_t				Size() const	{ return fSize; }
			const void*			Data() const	{ return fData; }

			status_t			Load();
			void				Unload();

private:
			const char*			fName;
			int					fFD;
			off_t				fOffset;
			off_t				fSize;
			void*				fData;
			int32				fLoadCount;
};


class ElfSegment : public DoublyLinkedListLinkImpl<ElfSegment> {
public:
								ElfSegment(off_t fileOffset, off_t fileSize,
									target_addr_t loadAddress,
									target_size_t loadSize, bool writable);
								~ElfSegment();

			off_t				FileOffset() const	{ return fFileOffset; }
			off_t				FileSize() const	{ return fFileSize; }
			target_addr_t		LoadAddress() const	{ return fLoadAddress; }
			target_size_t		LoadSize() const	{ return fLoadSize; }
			bool				IsWritable() const	{ return fWritable; }

private:
			off_t				fFileOffset;
			off_t				fFileSize;
			target_addr_t		fLoadAddress;
			target_size_t		fLoadSize;
			bool				fWritable;
};


class ElfFile {
public:
								ElfFile();
								~ElfFile();

			status_t			Init(const char* fileName);

			int					FD() const	{ return fFD; }

			ElfSection*			GetSection(const char* name);
			void				PutSection(ElfSection* section);

			ElfSegment*			TextSegment() const;
			ElfSegment*			DataSegment() const;

private:
			typedef DoublyLinkedList<ElfSection> SectionList;
			typedef DoublyLinkedList<ElfSegment> SegmentList;

private:
			bool				_CheckRange(off_t offset, off_t size) const;
			bool				_CheckElfHeader() const;

private:
			off_t				fFileSize;
			int					fFD;
			Elf32_Ehdr*			fElfHeader;
			SectionList			fSections;
			SegmentList			fSegments;
};


#endif	// ELF_FILE_H
