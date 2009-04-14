/*
 * Copyright 2003-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold, bonefish@cs.tu-berlin.de
 */

/*!	\file PartitionMap.cpp
	\brief Definitions for "intel" style partitions and implementation
		   of related classes.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _USER_MODE
#	include <util/kernel_cpp.h>
#	include <KernelExport.h>
#else
#	include <new>
#endif
#ifndef _BOOT_MODE
#	include <DiskDeviceTypes.h>
#else
#	include <boot/partitions.h>
#endif

#include "PartitionMap.h"


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

// partition_type
struct partition_type {
	uint8		type;
	const char	*name;
};

static const struct partition_type kPartitionTypes[] = {
	// these entries must be sorted by type (currently not)
// TODO: Standardize naming.
	{ 0x00, "empty" },
	{ 0x01, "FAT 12-bit" },
	{ 0x02, "Xenix root" },
	{ 0x03, "Xenix user" },
	{ 0x04, "FAT 16-bit (dos 3.0)" },
	{ 0x05, /*"Extended Partition"*/INTEL_EXTENDED_PARTITION_NAME },
	{ 0x06, "FAT 16-bit (dos 3.31)" },
	{ 0x07, "OS/2 IFS, Windows NT, Advanced Unix" },
	{ 0x0b, "FAT 32-bit" },
	{ 0x0c, "FAT 32-bit, LBA-mapped" },
	{ 0x0d, "FAT 16-bit, LBA-mapped" },
	{ 0x0f, /*"Extended Partition, LBA-mapped"*/INTEL_EXTENDED_PARTITION_NAME },
	{ 0x42, "Windows 2000 marker (switches to a proprietary partition table)" },
	{ 0x4d, "QNX 4" },
	{ 0x4e, "QNX 4 2nd part" },
	{ 0x4f, "QNX 4 3rd part" },
	{ 0x78, "XOSL boot loader" },
	{ 0x82, "Linux swapfile" },
	{ 0x83, "Linux native" },
	{ 0x85, /*"Linux extendend partition"*/INTEL_EXTENDED_PARTITION_NAME },
	{ 0xa5, "FreeBSD" },
	{ 0xa6, "OpenBSD" },
	{ 0xa7, "NextSTEP" },
	{ 0xa8, "MacOS X" },
	{ 0xa9, "NetBSD" },
	{ 0xab, "MacOS X boot" },
	{ 0xaf, "MacOS X HFS" },
	{ 0xbe, "Solaris 8 boot" },
	{ 0xeb, /*"BeOS"*/ BFS_NAME },
	{ 0, NULL }
};

static const struct partition_type kPartitionContentTypes[] = {
#ifndef _USER_MODE
	{ 0x01, kPartitionTypeFAT12 },
	{ 0x0c, kPartitionTypeFAT32 },
	{ 0x0f, kPartitionTypeIntelExtended },
	{ 0x83, kPartitionTypeEXT2 },
	{ 0x83, kPartitionTypeEXT3 },
	{ 0x83, kPartitionTypeReiser },
	{ 0xaf, kPartitionTypeHFS },
	{ 0xaf, kPartitionTypeHFSPlus },
	{ 0xeb, kPartitionTypeBFS },
#endif
	{ 0, NULL }
};


static const char*
partition_type_string(uint8 type)
{
	int32 i;
	for (i = 0; kPartitionTypes[i].name ; i++)
	{
		if (type == kPartitionTypes[i].type)
			return kPartitionTypes[i].name;
	}
	return NULL;
}


void
get_partition_type_string(uint8 type, char* buffer)
{
	if (buffer) {
		if (const char* typeString = partition_type_string(type))
			strcpy(buffer, typeString);
		else
			sprintf(buffer, "Unrecognized Type 0x%x", type);
	}
}


static int
cmp_partition_offset(const void* p1, const void* p2)
{
	const Partition* partition1 = *(const Partition**)p1;
	const Partition* partition2 = *(const Partition**)p2;

	if (partition1->Offset() < partition2->Offset())
		return -1;
	if (partition1->Offset() > partition2->Offset())
		return 1;

	return 0;
}


static int
cmp_offset(const void* o1, const void* o2)
{
	off_t offset1 = *static_cast<const off_t*>(o1);
	off_t offset2 = *static_cast<const off_t*>(o2);

	if (offset1 < offset2)
		return -1;
	if (offset1 > offset2)
		return 1;

	return 0;
}


static bool
is_inside_partitions(off_t location, const Partition** partitions, int32 count)
{
	bool result = false;
	if (count > 0) {
		// binary search
		int32 lower = 0;
		int32 upper = count - 1;
		while (lower < upper) {
			int32 mid = (lower + upper) / 2;
			const Partition *midPartition = partitions[mid];
			if (location >= midPartition->Offset() + midPartition->Size())
				lower = mid + 1;
			else
				upper = mid;
		}
		const Partition *partition = partitions[lower];
		result = (location >= partition->Offset() &&
				  location < partition->Offset() + partition->Size());
	}
	return result;
}


//	#pragma mark - PartitionType


PartitionType::PartitionType()
	:
	fType(0),
	fValid(false)
{
}


/*!	\brief Sets the \a type via its ID.
	\param type ID of the partition type, it is in the range [0..255].
*/
bool
PartitionType::SetType(uint8 type)
{
	fType = type;
	fValid = partition_type_string(type);
	return fValid;
}


/*!	\brief Sets the type via its string name.
	\param typeName Name of the partition type.
*/
bool
PartitionType::SetType(const char *typeName)
{
	for (int32 i = 0; kPartitionTypes[i].name ; i++) {
		if (!strcmp(typeName, kPartitionTypes[i].name)) {
			fType = kPartitionTypes[i].type;
			fValid = true;
			return fValid;
		}
	}
	fValid = false;
	return fValid;
}


/*!	\brief Converts content type to the partition type that fits best.
	\param content_type Name of the content type, it is standardized by system.
*/
bool
PartitionType::SetContentType(const char *contentType)
{
	for (int32 i = 0; kPartitionContentTypes[i].name ; i++) {
		if (!strcmp(contentType, kPartitionContentTypes[i].name)) {
			fType = kPartitionContentTypes[i].type;
			fValid = true;
			return fValid;
		}
	}
	fValid = false;
	return fValid;
}


/*!	\brief Finds next supported partition.
*/
bool
PartitionType::FindNext()
{
	for (int32 i = 0; kPartitionTypes[i].name; i++) {
		if (fType < kPartitionTypes[i].type) {
			fType = kPartitionTypes[i].type;
			fValid = true;
			return true;
		}
	}
	fValid = false;
	return false;
}


/*!	\fn bool PartitionType::IsValid() const
	\brief Check whether the current type is valid.
*/

/*!	\fn bool PartitionType::IsEmpty() const
	\brief Check whether the current type describes empty type.
*/

/*!	\fn bool PartitionType::IsExtended() const
	\brief Check whether the current type describes extended partition type.
*/

/*!	\fn uint8 PartitionType::Type() const
	\brief Returns ID of the current type.
*/

/*!	\fn void PartitionType::GetTypeString(char *buffer) const
	\brief Returns string name of the current type.
	\param buffer Buffer where the name is stored, has to be allocated with
	sufficient length.
*/


//	#pragma mark - Partition


Partition::Partition()
	:
	fPartitionTableOffset(0),
	fOffset(0),
	fSize(0),
	fType(0),
	fActive(false)
{
}


Partition::Partition(const partition_descriptor* descriptor, off_t tableOffset,
		off_t baseOffset, uint32 blockSize)
	:
	fPartitionTableOffset(0),
	fOffset(0),
	fSize(0),
	fType(0),
	fActive(false)
{
	SetTo(descriptor, tableOffset, baseOffset, blockSize);
}


void
Partition::SetTo(const partition_descriptor *descriptor, off_t tableOffset,
	off_t baseOffset, uint32 blockSize)
{
	TRACE(("Partition::SetTo(): active: %x\n", descriptor->active));
	SetTo(baseOffset + (off_t)descriptor->start * blockSize,
		(off_t)descriptor->size * blockSize, descriptor->type,
		descriptor->active, tableOffset, blockSize);
}


void
Partition::SetTo(off_t offset, off_t size, uint8 type, bool active,
	off_t tableOffset, uint32 blockSize)
{
	fPartitionTableOffset = tableOffset;
	fOffset = offset;
	fSize = size;
	fType = type;
	fActive = active;
	fBlockSize = blockSize;

	if (fSize == 0)
		Unset();
}


void
Partition::Unset()
{
	fPartitionTableOffset = 0;
	fOffset = 0;
	fSize = 0;
	fType = 0;
	fActive = false;
}


void
Partition::GetPartitionDescriptor(partition_descriptor *descriptor,
	off_t baseOffset) const
{
	descriptor->start = (fOffset - baseOffset) / fBlockSize;
	descriptor->size = fSize / fBlockSize;
	descriptor->type = fType;
	descriptor->active = fActive ? 0x80 : 0x00;
	descriptor->begin.Unset();
	descriptor->end.Unset();
}


#ifdef _BOOT_MODE
void
Partition::AdjustSize(off_t sessionSize)
{
	// To work around buggy (or older) BIOS, we shrink the partition size to
	// always fit into its session - this should improve detection of boot
	// partitions (see bug #238 for more information)
	if (sessionSize < fOffset + fSize && sessionSize > fOffset)
		fSize = sessionSize - fOffset;
}
#endif


bool
Partition::CheckLocation(off_t sessionSize) const
{
	// offsets and size must be block aligned, partition table and partition must
	// lie within the session
	if (fPartitionTableOffset % fBlockSize != 0) {
		TRACE(("Partition::CheckLocation() - bad partition table offset: %lld "
			"(session: %lld)\n", fPartitionTableOffset, sessionSize));
		return false;
	}
	if (fOffset % fBlockSize != 0) {
		TRACE(("Partition::CheckLocation() - bad offset: %lld "
			"(session: %lld)\n", fOffset, sessionSize));
		return false;
	}
	if (fSize % fBlockSize != 0) {
		TRACE(("Partition::CheckLocation() - bad size: %lld "
			"(session: %lld)\n", fSize, sessionSize));
		return false;
	}
	if (fPartitionTableOffset < 0 || fPartitionTableOffset >= sessionSize) {
		TRACE(("Partition::CheckLocation() - partition table offset outside "
			"session: %lld (session size: %lld)\n", fPartitionTableOffset,
			sessionSize));
		return false;
	}
	if (fOffset < 0) {
		TRACE(("Partition::CheckLocation() - offset before session: %lld "
			"(session: %lld)\n", fOffset, sessionSize));
		return false;
	}
	if (fOffset + fSize > sessionSize) {
		TRACE(("Partition::CheckLocation() - end after session: %lld "
			"(session: %lld)\n", fOffset + fSize, sessionSize));
		return false;
	}
	return true;
}


//	#pragma mark - PrimaryPartition


PrimaryPartition::PrimaryPartition()
	: Partition(),
	fHead(NULL),
	fTail(NULL),
	fLogicalPartitionCount(0)
{
}


void
PrimaryPartition::SetTo(const partition_descriptor* descriptor,
	off_t tableOffset, uint32 blockSize)
{
	Unset();
	Partition::SetTo(descriptor, tableOffset, 0, blockSize);
}


void
PrimaryPartition::SetTo(off_t offset, off_t size, uint8 type, bool active,
	uint32 blockSize)
{
	Unset();
	Partition::SetTo(offset, size, type, active, 0, blockSize);
}


void
PrimaryPartition::Unset()
{
	while (LogicalPartition *partition = fHead) {
		fHead = partition->Next();
		delete partition;
	}
	fHead = NULL;
	fTail = NULL;
	fLogicalPartitionCount = 0;
	Partition::Unset();
}


status_t
PrimaryPartition::Assign(const PrimaryPartition& other)
{
	partition_descriptor descriptor;
	other.GetPartitionDescriptor(&descriptor, 0);
	SetTo(&descriptor, 0, other.BlockSize());

	const LogicalPartition* otherLogical = other.fHead;
	while (otherLogical) {
		off_t tableOffset = otherLogical->PartitionTableOffset();
		otherLogical->GetPartitionDescriptor(&descriptor, tableOffset);

		LogicalPartition* logical = new(nothrow) LogicalPartition(
			&descriptor, tableOffset, this);
		if (!logical)
			return B_NO_MEMORY;

		AddLogicalPartition(logical);

		otherLogical = otherLogical->Next();
	}

	return B_OK;
}


LogicalPartition*
PrimaryPartition::LogicalPartitionAt(int32 index) const
{
	LogicalPartition *partition = NULL;
	if (index >= 0 && index < fLogicalPartitionCount) {
		for (partition = fHead; index > 0; index--)
			partition = partition->Next();
	}
	return partition;
}


void
PrimaryPartition::AddLogicalPartition(LogicalPartition* partition)
{
	if (!partition)
		return;

	partition->SetPrimaryPartition(this);
	partition->SetPrevious(fTail);
	if (fTail) {
		fTail->SetNext(partition);
		fTail = partition;
	} else
		fHead = fTail = partition;

	partition->SetNext(NULL);

	fLogicalPartitionCount++;
}


void
PrimaryPartition::RemoveLogicalPartition(LogicalPartition* partition)
{
	if (!partition || partition->GetPrimaryPartition() != this)
		return;

	LogicalPartition *prev = partition->Previous();
	LogicalPartition *next = partition->Next();

	if (prev)
		prev->SetNext(next);
	else
		fHead = next;
	if (next)
		next->SetPrevious(prev);
	else
		fTail = prev;

	fLogicalPartitionCount--;

	partition->SetNext(NULL);
	partition->SetPrevious(NULL);
	partition->SetPrimaryPartition(NULL);
}


//	#pragma mark - LogicalPartition


LogicalPartition::LogicalPartition()
	: Partition(),
	fPrimary(NULL),
	fNext(NULL),
	fPrevious(NULL)
{
}


LogicalPartition::LogicalPartition(const partition_descriptor* descriptor,
		off_t tableOffset, PrimaryPartition* primary)
	: Partition(),
	fPrimary(NULL),
	fNext(NULL),
	fPrevious(NULL)
{
	SetTo(descriptor, tableOffset, primary);
}


void
LogicalPartition::SetTo(const partition_descriptor* descriptor,
	off_t tableOffset, PrimaryPartition* primary)
{
	Unset();
	if (descriptor && primary) {
		// There are two types of LogicalPartitions. There are so called
		// "inner extended" partitions and the "real" logical partitions
		// which contain data. The "inner extended" partitions don't contain
		// data and are only used to point to the next partition table in the
		// linked list of logical partitions. For "inner extended" partitions,
		// the baseOffset is in relation to the (first sector of the)
		// "primary extended" partition, in another words, all inner extended
		// partitions use the same base offset for reference.
		// The data containing, real logical partitions use the offset of the
		// partition table that contains their partition descriptor as their
		// baseOffset.
		off_t baseOffset = descriptor->is_extended()
			? primary->Offset() : tableOffset;
		Partition::SetTo(descriptor, tableOffset, baseOffset,
			primary->BlockSize());
		fPrimary = primary;
	}
}


void
LogicalPartition::SetTo(off_t offset, off_t size, uint8 type, bool active,
	off_t tableOffset, PrimaryPartition *primary)
{
	Unset();
	if (primary) {
		Partition::SetTo(offset, size, type, active, tableOffset,
			primary->BlockSize());
		fPrimary = primary;
	}
}


void
LogicalPartition::Unset()
{
	fPrimary = NULL;
	fNext = NULL;
	fPrevious = NULL;
	Partition::Unset();
}


//	#pragma mark - PartitionMap


PartitionMap::PartitionMap()
{
	for (int32 i = 0; i < 4; i++)
		fPrimaries[i].SetIndex(i);
}


PartitionMap::~PartitionMap()
{
}


void
PartitionMap::Unset()
{
	for (int32 i = 0; i < 4; i++)
		fPrimaries[i].Unset();
}


status_t
PartitionMap::Assign(const PartitionMap& other)
{
	for (int32 i = 0; i < 4; i++) {
		status_t error = fPrimaries[i].Assign(other.fPrimaries[i]);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


PrimaryPartition*
PartitionMap::PrimaryPartitionAt(int32 index)
{
	PrimaryPartition *partition = NULL;
	if (index >= 0 && index < 4)
		partition = fPrimaries + index;
	return partition;
}


const PrimaryPartition*
PartitionMap::PrimaryPartitionAt(int32 index) const
{
	const PrimaryPartition *partition = NULL;
	if (index >= 0 && index < 4)
		partition = fPrimaries + index;
	return partition;
}


int32
PartitionMap::CountNonEmptyPrimaryPartitions() const
{
	int32 count = 0;
	for (int32 i = 0; i < 4; i++) {
		if (!fPrimaries[i].IsEmpty())
			count++;
	}

	return count;
}


int32
PartitionMap::ExtendedPartitionIndex() const
{
	for (int32 i = 0; i < 4; i++) {
		if (fPrimaries[i].IsExtended())
			return i;
	}

	return -1;
}


int32
PartitionMap::CountPartitions() const
{
	int32 count = 4;
	for (int32 i = 0; i < 4; i++)
		count += fPrimaries[i].CountLogicalPartitions();
	return count;
}


int32
PartitionMap::CountNonEmptyPartitions() const
{
	int32 count = 0;
	for (int32 i = CountPartitions() - 1; i >= 0; i--) {
		if (!PartitionAt(i)->IsEmpty())
			count++;
	}

	return count;
}


Partition*
PartitionMap::PartitionAt(int32 index)
{
	Partition *partition = NULL;
	int32 count = CountPartitions();
	if (index >= 0 && index < count) {
		if (index < 4)
			partition  = fPrimaries + index;
		else {
			index -= 4;
			int32 primary = 0;
			while (index >= fPrimaries[primary].CountLogicalPartitions()) {
				index -= fPrimaries[primary].CountLogicalPartitions();
				primary++;
			}
			partition = fPrimaries[primary].LogicalPartitionAt(index);
		}
	}
	return partition;
}


const Partition*
PartitionMap::PartitionAt(int32 index) const
{
	return const_cast<PartitionMap*>(this)->PartitionAt(index);
}


bool
PartitionMap::Check(off_t sessionSize) const
{
	int32 partitionCount = CountPartitions();

	// 1. check partition locations
	for (int32 i = 0; i < partitionCount; i++) {
		if (!PartitionAt(i)->CheckLocation(sessionSize))
			return false;
	}

	// 2. check overlapping of partitions and location of partition tables
	bool result = true;
	const Partition** byOffset = new(nothrow) const Partition*[partitionCount];
	off_t* tableOffsets = new(nothrow) off_t[partitionCount - 3];
	if (byOffset && tableOffsets) {
		// fill the arrays
		int32 byOffsetCount = 0;
		int32 tableOffsetCount = 1;	// primary partition table
		tableOffsets[0] = 0;			//
		for (int32 i = 0; i < partitionCount; i++) {
			const Partition *partition = PartitionAt(i);
			if (!partition->IsExtended())
				byOffset[byOffsetCount++] = partition;

			// add only logical partition partition table locations
			if (i >= 4) {
				tableOffsets[tableOffsetCount++]
					= partition->PartitionTableOffset();
			}
		}

		// sort the arrays
		qsort(byOffset, byOffsetCount, sizeof(const Partition*),
			cmp_partition_offset);
		qsort(tableOffsets, tableOffsetCount, sizeof(off_t), cmp_offset);

		// check for overlappings
		off_t nextOffset = 0;
		for (int32 i = 0; i < byOffsetCount; i++) {
			const Partition *partition = byOffset[i];
			if (partition->Offset() < nextOffset) {
				TRACE(("intel: PartitionMap::Check(): overlapping partitions!"
					"\n"));
				result = false;
				break;
			}
			nextOffset = partition->Offset() + partition->Size();
		}

		// check uniqueness of partition table offsets and whether they lie
		// outside of the non-extended partitions
		if (result) {
			for (int32 i = 0; i < tableOffsetCount; i++) {
				if (i > 0 && tableOffsets[i] == tableOffsets[i - 1]) {
					TRACE(("intel: PartitionMap::Check(): same partition talbe "
						"for different extended partitions!\n"));
					result = false;
					break;
				} else if (is_inside_partitions(tableOffsets[i], byOffset,
								byOffsetCount)) {
					TRACE(("intel: PartitionMap::Check(): a partition table "
						"lies inside a non-extended partition!\n"));
					result = false;
					break;
				}
			}
		}
	} else
		result = false;		// no memory: assume failure

	// cleanup
	delete[] byOffset;
	delete[] tableOffsets;

	return result;
}

