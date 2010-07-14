/*
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "Node.h"

#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "Block.h"
#include "DebugSupport.h"


static inline uint64
current_time_nanos()
{
	timeval time;
	gettimeofday(&time, NULL);

	return (uint64)time.tv_sec * 1000000000 + (uint64)time.tv_usec * 1000;
}


Node::Node(Volume* volume, uint64 blockIndex, const checksumfs_node& nodeData)
	:
	fVolume(volume),
	fBlockIndex(blockIndex),
	fNode(nodeData),
	fNodeDataDirty(false)
{
	_Init();

	fAccessedTime = ModificationTime();
}


Node::Node(Volume* volume, uint64 blockIndex, mode_t mode)
	:
	fVolume(volume),
	fBlockIndex(blockIndex),
	fNodeDataDirty(true)
{
	_Init();

	memset(&fNode, 0, sizeof(fNode));

	fNode.mode = mode;

	// set user/group
	fNode.uid = geteuid();
	fNode.gid = getegid();

	// set the times
	timeval time;
	gettimeofday(&time, NULL);

	fAccessedTime = current_time_nanos();
	fNode.creationTime = fAccessedTime;
	fNode.modificationTime = fAccessedTime;
	fNode.changeTime = fAccessedTime;
}


Node::~Node()
{
	rw_lock_destroy(&fLock);
}


status_t
Node::InitForVFS()
{
	return B_OK;
}


status_t
Node::DeletingNode(Transaction& transaction)
{
	return B_OK;
}


status_t
Node::Resize(uint64 newSize, bool fillWithZeroes, Transaction& transaction)
{
	RETURN_ERROR(B_BAD_VALUE);
}


status_t
Node::Read(off_t pos, void* buffer, size_t size, size_t& _bytesRead)
{
	RETURN_ERROR(B_BAD_VALUE);
}


status_t
Node::Write(off_t pos, const void* buffer, size_t size, size_t& _bytesWritten)
{
	RETURN_ERROR(B_BAD_VALUE);
}


void
Node::SetParentDirectory(uint32 blockIndex)
{
	fNode.parentDirectory = blockIndex;
	fNodeDataDirty = true;
}


void
Node::SetHardLinks(uint32 value)
{
	fNode.hardLinks = value;
	fNodeDataDirty = true;
}


void
Node::SetUID(uint32 uid)
{
	fNode.uid = uid;
	fNodeDataDirty = true;
}


void
Node::SetGID(uint32 gid)
{
	fNode.gid = gid;
	fNodeDataDirty = true;
}


void
Node::SetSize(uint64 size)
{
	fNode.size = size;
	fNodeDataDirty = true;
}


void
Node::SetAccessedTime(uint64 time)
{
	fAccessedTime = time;
}


void
Node::SetCreationTime(uint64 time)
{
	fNode.creationTime = time;
	fNodeDataDirty = true;
}


void
Node::SetModificationTime(uint64 time)
{
	fNode.modificationTime = time;
	fNodeDataDirty = true;
}


void
Node::SetChangeTime(uint64 time)
{
	fNode.changeTime = time;
	fNodeDataDirty = true;
}


void
Node::Touched(int32 mode)
{
	fAccessedTime = current_time_nanos();

	switch (mode) {
		default:
		case NODE_MODIFIED:
			fNode.modificationTime = fAccessedTime;
		case NODE_STAT_CHANGED:
			fNode.changeTime = fAccessedTime;
		case NODE_ACCESSED:
			break;
	}
}


void
Node::RevertNodeData(const checksumfs_node& nodeData)
{
	fNode = nodeData;
	fNodeDataDirty = false;
}


status_t
Node::Flush(Transaction& transaction)
{
	if (!fNodeDataDirty)
		return B_OK;

	Block block;
	if (!block.GetWritable(fVolume, fBlockIndex, transaction))
		return B_ERROR;

	memcpy(block.Data(), &fNode, sizeof(fNode));

	fNodeDataDirty = false;
	return B_OK;
}


void
Node::_Init()
{
	rw_lock_init(&fLock, "checkfs node");
}
