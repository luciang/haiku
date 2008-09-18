/*
 * Copyright 2003-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold, bonefish@cs.tu-berlin.de
 */


#ifndef _USER_MODE
#	include <KernelExport.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <new>

#include "PartitionMap.h"
#include "PartitionMapParser.h"


//#define TRACE_ENABLED
#ifdef TRACE_ENABLED
#	ifdef _USER_MODE
#		define TRACE(x) printf x
#	else
#		define TRACE(x) dprintf x
#	endif
#else
#	define TRACE(x) ;
#endif

using std::nothrow;

// Maximal number of logical partitions per extended partition we allow.
static const int32 kMaxLogicalPartitionCount = 128;

// constructor
PartitionMapParser::PartitionMapParser(int deviceFD, off_t sessionOffset,
		off_t sessionSize)
	: fDeviceFD(deviceFD),
	  fSessionOffset(sessionOffset),
	  fSessionSize(sessionSize),
	  fPTS(NULL),
	  fMap(NULL)
{
}

// destructor
PartitionMapParser::~PartitionMapParser()
{
}

// Parse
status_t
PartitionMapParser::Parse(const uint8 *block, PartitionMap *map)
{
	status_t error = (map ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		fMap = map;
		fMap->Unset();
		if (block) {
			const partition_table_sector *pts
				= (const partition_table_sector*)block;
			error = _ParsePrimary(pts);
		} else {
			partition_table_sector pts;
			error = _ReadPTS(0, &pts);
			if (error == B_OK)
				error = _ParsePrimary(&pts);
		}

		if (error == B_OK && !fMap->Check(fSessionSize))
			error = B_BAD_DATA;

		fMap = NULL;
	}
	return error;
}

// _ParsePrimary
status_t
PartitionMapParser::_ParsePrimary(const partition_table_sector *pts)
{
	if (pts == NULL)
		return B_BAD_VALUE;

	// check the signature
	if (pts->signature != kPartitionTableSectorSignature) {
		TRACE(("intel: _ParsePrimary(): invalid PTS signature: %lx\n",
			(uint32)pts->signature));
		return B_BAD_DATA;
	}

	// examine the table
	for (int32 i = 0; i < 4; i++) {
		const partition_descriptor *descriptor = &pts->table[i];
		PrimaryPartition *partition = fMap->PrimaryPartitionAt(i);
		partition->SetTo(descriptor, 0);

#ifdef _BOOT_MODE
		// work-around potential BIOS problems
		partition->AdjustSize(fSessionSize);
#endif
		// ignore, if location is bad
		if (!partition->CheckLocation(fSessionSize)) {
			TRACE(("intel: _ParsePrimary(): partition %ld: bad location, "
				"ignoring\n", i));
			partition->Unset();
		}
	}

	// allocate a PTS buffer
	fPTS = new(nothrow) partition_table_sector;
	if (fPTS == NULL)
		return B_NO_MEMORY;

	// parse extended partitions
	status_t error = B_OK;
	for (int32 i = 0; error == B_OK && i < 4; i++) {
		PrimaryPartition *primary = fMap->PrimaryPartitionAt(i);
		if (primary->IsExtended())
			error = _ParseExtended(primary, primary->Offset());
	}

	// cleanup
	delete fPTS;
	fPTS = NULL;

	return error;
}

// _ParseExtended
status_t
PartitionMapParser::_ParseExtended(PrimaryPartition *primary, off_t offset)
{
	status_t error = B_OK;
	int32 partitionCount = 0;
	while (error == B_OK) {
		// check for cycles
		if (++partitionCount > kMaxLogicalPartitionCount) {
			TRACE(("intel: _ParseExtended(): Maximal number of logical "
				   "partitions for extended partition reached. Cycle?\n"));
			error = B_BAD_DATA;
		}

		// read the PTS
		if (error == B_OK)
			error = _ReadPTS(offset);

		// check the signature
		if (error == B_OK
			&& fPTS->signature != kPartitionTableSectorSignature) {
			TRACE(("intel: _ParseExtended(): invalid PTS signature: %lx\n",
				(uint32)fPTS->signature));
			error = B_BAD_DATA;
		}

		// ignore the PTS, if any error occured till now
		if (error != B_OK) {
			TRACE(("intel: _ParseExtended(): ignoring this PTS\n"));
			error = B_OK;
			break;
		}

		// Examine the table, there is exactly one extended and one
		// non-extended logical partition. All four table entries are
		// examined though. If there is no inner extended partition,
		// the end of the linked list is reached.
		// The first PTS describing both an "inner extended" parition and a
		// "data" partition (non extended and not empty) is the start sector
		// of the primary extended partition. The next PTS in the linked list
		// is the start sector of the inner extended partition described in
		// this PTS.
		LogicalPartition extended;
		LogicalPartition nonExtended;
		for (int32 i = 0; error == B_OK && i < 4; i++) {
			const partition_descriptor *descriptor = &fPTS->table[i];
			if (descriptor->is_empty())
				continue;

			LogicalPartition *partition = NULL;
			if (descriptor->is_extended()) {
				if (extended.IsEmpty()) {
					extended.SetTo(descriptor, offset, primary);
					partition = &extended;
				} else {
					// only one extended partition allowed
					error = B_BAD_DATA;
					TRACE(("intel: _ParseExtended(): "
						   "only one extended partition allowed\n"));
				}
			} else {
				if (nonExtended.IsEmpty()) {
					nonExtended.SetTo(descriptor, offset, primary);
					partition = &nonExtended;
				} else {
					// only one non-extended partition allowed
					error = B_BAD_DATA;
					TRACE(("intel: _ParseExtended(): only one "
						   "non-extended partition allowed\n"));
				}
			}
			if (partition == NULL)
				break;
#ifdef _BOOT_MODE
			// work-around potential BIOS problems
			partition->AdjustSize(fSessionSize);
#endif
			// check the partition's location
			if (!partition->CheckLocation(fSessionSize)) {
				error = B_BAD_DATA;
				TRACE(("intel: _ParseExtended(): Invalid partition "
					"location: pts: %lld, offset: %lld, size: %lld\n",
					partition->PTSOffset(), partition->Offset(),
					partition->Size()));
			}
		}

		// add non-extended partition to list
		if (error == B_OK && !nonExtended.IsEmpty()) {
			LogicalPartition *partition
				= new(nothrow) LogicalPartition(nonExtended);
			if (partition)
				primary->AddLogicalPartition(partition);
			else
				error = B_NO_MEMORY;
		}

		// prepare to parse next extended/non-extended partition pair
		if (error == B_OK && !extended.IsEmpty())
			offset = extended.Offset();
		else
			break;
	}

	return error;
}

// _ReadPTS
status_t
PartitionMapParser::_ReadPTS(off_t offset, partition_table_sector *pts)
{
	status_t error = B_OK;
	if (!pts)
		pts = fPTS;
	int32 toRead = sizeof(partition_table_sector);
	// check the offset
	if (offset < 0 || offset + toRead > fSessionSize) {
		error = B_BAD_VALUE;
		TRACE(("intel: _ReadPTS(): bad offset: %Ld\n", offset));
	// read
	} else if (read_pos(fDeviceFD, fSessionOffset + offset, pts, toRead)
						!= toRead) {
#ifndef _BOOT_MODE
		error = errno;
		if (error == B_OK)
			error = B_IO_ERROR;
#else
		error = B_IO_ERROR;
#endif
		TRACE(("intel: _ReadPTS(): reading the PTS failed: %lx\n", error));
	}
	return error;
}

