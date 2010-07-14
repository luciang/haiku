/*
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "File.h"

#include <string.h>

#include <algorithm>
#include <new>

#include <AutoDeleter.h>

#include "Block.h"
#include "BlockAllocator.h"
#include "DebugSupport.h"
#include "Volume.h"


static const size_t kFileRootBlockOffset	= sizeof(checksumfs_node);
static const size_t kFileRootBlockSize		= B_PAGE_SIZE
												- kFileRootBlockOffset;
static const uint32 kFileRootBlockMaxCount	= kFileRootBlockSize / 8;
static const uint32 kFileBlockMaxCount		= B_PAGE_SIZE / 8;
static const uint32 kFileBlockShift			= 9;
static const uint32 kFileMaxTreeDepth		= (64 + kFileBlockShift - 1)
												/ kFileBlockShift + 1;


#define BLOCK_ROUND_UP(value)	(((value) + B_PAGE_SIZE - 1) / B_PAGE_SIZE \
									* B_PAGE_SIZE)


struct File::LevelInfo {
	uint64	addressableShift;	// 1 << addressableShift is the number of
								// descendent data blocks a child block (and its
								// descendents) can address
	uint32	childCount;			// number of child blocks of the last block of
								// this level
	Block	block;
	uint64*	blockData;
	int32	index;
};


File::File(Volume* volume, uint64 blockIndex, const checksumfs_node& nodeData)
	:
	Node(volume, blockIndex, nodeData),
	fFileCache(NULL)
{
	STATIC_ASSERT(kFileBlockMaxCount == (uint32)1 << kFileBlockShift);
}


File::File(Volume* volume, uint64 blockIndex, mode_t mode)
	:
	Node(volume, blockIndex, mode),
	fFileCache(NULL),
	fFileMap(NULL)
{
}


File::~File()
{
	if (fFileCache != NULL)
		file_cache_delete(fFileCache);
	if (fFileMap != NULL)
		file_map_delete(fFileMap);
}


status_t
File::InitForVFS()
{
	// create the file map
	fFileMap = file_map_create(GetVolume()->ID(), BlockIndex(), Size());
	if (fFileMap == NULL)
		RETURN_ERROR(B_NO_MEMORY);

	// create the file cache
	fFileCache = file_cache_create(GetVolume()->ID(), BlockIndex(), Size());
	if (fFileCache == NULL)
		RETURN_ERROR(B_NO_MEMORY);

	return B_OK;
}


status_t
File::DeletingNode(Transaction& transaction)
{
	return Resize(0, false, transaction);
}


status_t
File::Resize(uint64 newSize, bool fillWithZeroes, Transaction& transaction)
{
	uint64 size = Size();
	if (newSize == size)
		return B_OK;

	FUNCTION("%" B_PRIu64 " -> %" B_PRIu64 "\n", size, newSize);

	uint64 blockCount = BLOCK_ROUND_UP(size) / B_PAGE_SIZE;
	uint64 newBlockCount = BLOCK_ROUND_UP(newSize) / B_PAGE_SIZE;

	if (newBlockCount != blockCount) {
		status_t error;
		if (newBlockCount < blockCount)
			error = _ShrinkTree(blockCount, newBlockCount, transaction);
		else
			error = _GrowTree(blockCount, newBlockCount, transaction);

		if (error != B_OK)
			RETURN_ERROR(error);
	}

	SetSize(newSize);

	if (newSize > size && fillWithZeroes) {
		status_t error = _WriteZeroes(size, newSize - size);
		if (error != B_OK)
			RETURN_ERROR(error);
	}

	file_cache_set_size(fFileCache, newSize);
	file_map_set_size(fFileMap, newSize);

	return B_OK;
}


status_t
File::Read(off_t pos, void* buffer, size_t size, size_t& _bytesRead)
{
	if (pos < 0)
		return B_BAD_VALUE;

	if (size == 0) {
		_bytesRead = 0;
		return B_OK;
	}

	NodeReadLocker locker(this);

	uint64 fileSize = Size();
	if ((uint64)pos >= fileSize) {
		_bytesRead = 0;
		return B_OK;
	}

	if (fileSize - pos < size)
		size = fileSize - pos;

	locker.Unlock();

	size_t bytesRead = size;
	status_t error = file_cache_read(fFileCache, NULL, pos, buffer, &bytesRead);
	if (error != B_OK)
		RETURN_ERROR(error);

	_bytesRead = bytesRead;
	return B_OK;
}


status_t
File::Write(off_t pos, const void* buffer, size_t size, size_t& _bytesWritten)
{
	if (size == 0) {
		_bytesWritten = 0;
		return B_OK;
	}

	NodeWriteLocker locker(this);

	uint64 fileSize = Size();
	if (pos < 0)
		pos = fileSize;

	uint64 newFileSize = (uint64)pos + size;

	if (newFileSize > fileSize) {
		// we have to resize the file
		Transaction transaction(GetVolume());
		status_t error = transaction.Start();
		if (error != B_OK)
			RETURN_ERROR(error);

		// attach the node to the transaction (write locks it, too)
		error = transaction.AddNode(this,
			TRANSACTION_NODE_ALREADY_LOCKED | TRANSACTION_KEEP_NODE_LOCKED);
		if (error != B_OK)
			RETURN_ERROR(error);

		// resize
		error = Resize((uint64)pos + size, false, transaction);
		if (error != B_OK)
			RETURN_ERROR(error);

		SetSize(newFileSize);

		// commit the transaction
		error = transaction.Commit();
		if (error != B_OK)
			RETURN_ERROR(error);
	}

	// now the file has the right size -- do the write
	locker.Unlock();

	if (fileSize < (uint64)pos) {
		// fill the gap between old file end and write position with zeroes
		_WriteZeroes(fileSize, pos - fileSize);
	}

	size_t bytesWritten = size;
	status_t error = file_cache_write(fFileCache, NULL, pos, buffer,
		&bytesWritten);
	if (error != B_OK)
		RETURN_ERROR(error);

	// update the file times
	Transaction transaction(GetVolume());
	if (transaction.Start() == B_OK && transaction.AddNode(this) == B_OK) {
		// note: we don't fail, if we only couldn't update the times
		Touched(NODE_MODIFIED);
		transaction.Commit();
	}

	_bytesWritten = bytesWritten;
	return B_OK;
}


void
File::RevertNodeData(const checksumfs_node& nodeData)
{
	Node::RevertNodeData(nodeData);

	// in case the file size was reverted, reset file cache and map
	uint64 size = Size();
	file_cache_set_size(fFileCache, size);
	file_map_set_size(fFileMap, size);
}


status_t
File::GetFileVecs(uint64 offset, size_t size, file_io_vec* vecs, size_t count,
	size_t& _count)
{
	// Round size to block size, but restrict to file size. This semantics is
	// fine with the caller (the file map) and it will help avoiding partial
	// block I/O.
	uint32 inBlockOffset = offset % B_PAGE_SIZE;
	offset -= inBlockOffset;
	size = BLOCK_ROUND_UP(size + inBlockOffset);

	uint64 fileSize = BLOCK_ROUND_UP(Size());
	if (offset >= fileSize) {
		_count = 0;
		return B_OK;
	}

	if (offset + size > fileSize)
		size = fileSize - offset;

	uint64 blockCount = fileSize / B_PAGE_SIZE;

	// get the level infos
	int32 depth;
	LevelInfo* infos = _GetLevelInfos(blockCount, depth);
	if (infos == NULL)
		RETURN_ERROR(B_NO_MEMORY);
	ArrayDeleter<LevelInfo> infosDeleter(infos);

	// prepare for the iteration
	uint64 firstBlock = offset / B_PAGE_SIZE;
	uint64 blockIndex = BlockIndex();
	for (int32 i = 0; i < depth; i++) {
		LevelInfo& info = infos[i];
		if (!info.block.GetReadable(GetVolume(), blockIndex))
			RETURN_ERROR(B_ERROR);

		if (i == 0) {
			info.blockData = (uint64*)((uint8*)info.block.Data()
				+ kFileRootBlockOffset);
		} else
			info.blockData = (uint64*)info.block.Data();

		info.index = firstBlock >> info.addressableShift;
		firstBlock -= (uint64)info.index << info.addressableShift;

		blockIndex = info.blockData[info.index];
	}

	// and iterate
	int32 level = depth - 1;
	uint64 neededBlockCount = size / B_PAGE_SIZE;
	size_t countAdded = 0;

	while (true) {
		LevelInfo& info = infos[level];

		if (info.index == (int32)kFileBlockMaxCount) {
			// end of block -- back track to next greater branch
			level--;
			infos[level].index++;
			continue;
		}

		blockIndex = info.blockData[info.index];

		if (level < depth - 1) {
			// descend to next level
			level++;

			if (!infos[level].block.GetReadable(GetVolume(), blockIndex))
				RETURN_ERROR(B_ERROR);

			infos[level].blockData = (uint64*)infos[level].block.Data();
			infos[level].index = 0;
			continue;
		}

		// add the block
		uint64 blockOffset = blockIndex * B_PAGE_SIZE;
		if (countAdded > 0
			&& blockOffset
				== (uint64)vecs[countAdded - 1].offset
					+ vecs[countAdded - 1].length) {
			// the block continues where the previous block ends -- just extend
			// the vector
			vecs[countAdded - 1].length += B_PAGE_SIZE;
		} else {
			// we need a new block
			if (countAdded == count)
				break;

			vecs[countAdded].offset = blockOffset;
			vecs[countAdded].length = B_PAGE_SIZE;
			countAdded++;
		}

		if (--neededBlockCount == 0)
			break;
	}

	_count = countAdded;
	return B_OK;
}


/*static*/ uint32
File::_DepthForBlockCount(uint64 blockCount)
{
	uint64 addressableBlocks = kFileRootBlockMaxCount;

	uint32 depth = 1;
	while (blockCount > addressableBlocks) {
		addressableBlocks *= kFileBlockMaxCount;
		depth++;
	}

	return depth;
}


/*static*/ void
File::_UpdateLevelInfos(LevelInfo* infos, int32 levelCount, uint64 blockCount)
{
	uint64 addressableShift = 0;
	for (int32 i = levelCount - 1; i >= 0; i--) {
		infos[i].addressableShift = addressableShift;
		infos[i].childCount = blockCount % kFileBlockMaxCount;
		addressableShift += kFileBlockShift;
		blockCount = (blockCount + kFileBlockMaxCount - 1) / kFileBlockMaxCount;
	}
}


/*static*/ File::LevelInfo*
File::_GetLevelInfos(uint64 blockCount, int32& _levelCount)
{
	LevelInfo* infos = new(std::nothrow) LevelInfo[kFileMaxTreeDepth];
// TODO: We need to allocate differently, if requested by the page writer!
	if (infos == NULL)
		return NULL;

	int32 levelCount = _DepthForBlockCount(blockCount);
	_UpdateLevelInfos(infos, levelCount, blockCount);

	_levelCount = levelCount;
	return infos;
}


status_t
File::_ShrinkTree(uint64 blockCount, uint64 newBlockCount,
	Transaction& transaction)
{
	FUNCTION("blockCount: %" B_PRIu64 " -> %" B_PRIu64 "\n", blockCount,
		newBlockCount);

	int32 depth;
	LevelInfo* infos = _GetLevelInfos(blockCount, depth);
	if (infos == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<LevelInfo> infosDeleter(infos);

	// load the root block
	if (!infos[0].block.GetWritable(GetVolume(), BlockIndex(), transaction))
		RETURN_ERROR(B_ERROR);
	infos[0].blockData = (uint64*)((uint8*)infos[0].block.Data()
		+ kFileRootBlockOffset);

	int32 level = 0;

	// remove blocks
	bool removeBlock = false;
	while (true) {
		PRINT("  level %" B_PRId32 ", child count: %" B_PRIu32 "\n", level,
			infos[level].childCount);

		// If the block is empty, remove it.
		if (infos[level].childCount == 0) {
			if (level == 0)
				break;

			// prepare for the next iteration
			infos[level].childCount = kFileBlockMaxCount;

			removeBlock = true;
			level--;
			continue;
		}

		// block not empty -- we might already be done
		if (blockCount == newBlockCount)
			break;

		uint64 blockIndex = infos[level].blockData[infos[level].childCount - 1];

		// unless we're in the last level or shall remove, descend
		if (level < depth - 1 && !removeBlock) {
			LevelInfo& info = infos[++level];
			if (!info.block.GetWritable(GetVolume(), blockIndex, transaction))
				RETURN_ERROR(B_ERROR);
			info.blockData = (uint64*)info.block.Data();
			continue;
		}

		// remove the block

		LevelInfo& info = infos[level];

		PRINT("  freeing block: %" B_PRId64 "\n", blockIndex);

		// clear the entry (not strictly necessary)
		info.blockData[info.childCount - 1] = 0;

		// free the block
		status_t error = GetVolume()->GetBlockAllocator()->Free(blockIndex, 1,
			transaction);
		if (error != B_OK)
			RETURN_ERROR(error);

		if (level == depth - 1)
			blockCount--;

		infos[level].childCount--;

		removeBlock = false;
	}

	// We got rid of all unnecessary data blocks and empty node blocks. We might
	// need to cull the lower levels of the tree, now.
	int32 newDepth = _DepthForBlockCount(newBlockCount);
	if (newDepth == depth)
		return B_OK;

	for (int32 i = 1; i <= depth - newDepth; i++) {
		uint64 blockIndex = infos[0].blockData[0];

		PRINT("  removing block %" B_PRIu64 " at level %" B_PRIi32 "\n",
			blockIndex, i);

		Block block;
		if (!block.GetReadable(GetVolume(), blockIndex))
			RETURN_ERROR(B_ERROR);

		// copy to the root block
		const uint64* blockData = (uint64*)infos[i].block.Data();
		memcpy(infos[0].blockData, blockData, infos[i].childCount * 8);

		// free the block
		block.Put();
		status_t error = GetVolume()->GetBlockAllocator()->Free(blockIndex, 1,
			transaction);
		if (error != B_OK)
			RETURN_ERROR(error);
	}

	return B_OK;
}


status_t
File::_GrowTree(uint64 blockCount, uint64 newBlockCount,
	Transaction& transaction)
{
	FUNCTION("blockCount: %" B_PRIu64 " -> %" B_PRIu64 "\n", blockCount,
		newBlockCount);

	int32 depth;
	LevelInfo* infos = _GetLevelInfos(blockCount, depth);
	if (infos == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<LevelInfo> infosDeleter(infos);

	int32 newDepth = _DepthForBlockCount(newBlockCount);

	Block& rootBlock = infos[0].block;
	if (!rootBlock.GetWritable(GetVolume(), BlockIndex(), transaction))
		RETURN_ERROR(B_ERROR);
	infos[0].blockData = (uint64*)((uint8*)rootBlock.Data()
		+ kFileRootBlockOffset);

	// add new levels, if necessary
	if (depth < newDepth) {
		uint32 childCount = infos[0].childCount;

		// update the level infos
		_UpdateLevelInfos(infos, newDepth, blockCount);

		// allocate a block per new level
		for (int32 i = newDepth - depth - 1; i >= 0; i--) {
			while (depth < newDepth) {
				// allocate a new block
				AllocatedBlock allocatedBlock(GetVolume()->GetBlockAllocator(),
					transaction);
				status_t error = allocatedBlock.Allocate(BlockIndex());
				if (error != B_OK)
					RETURN_ERROR(error);

				Block newBlock;
				if (!newBlock.GetZero(GetVolume(), allocatedBlock.Index(),
						transaction)) {
					RETURN_ERROR(B_ERROR);
				}

				allocatedBlock.Detach();

				PRINT("  inserting block %" B_PRIu64 " at level %" B_PRIi32
					"\n", newBlock.Index(), i + 1);

				// copy the root block
				memcpy(newBlock.Data(), infos[0].blockData, childCount * 8);

				// set the block in the root block
				infos[0].blockData[0] = newBlock.Index();
				childCount = 1;
			}
		}
	}

	depth = newDepth;

	// prepare the iteration
	int32 level = depth - 1;
	for (int32 i = 0; i < level; i++) {
		// get the block for the next level
		LevelInfo& info = infos[i];
		if (!infos[i + 1].block.GetWritable(GetVolume(),
				info.blockData[info.childCount - 1], transaction)) {
			RETURN_ERROR(B_ERROR);
		}
		infos[i + 1].blockData = (uint64*)infos[i + 1].block.Data();
	}

	// add the new blocks
	while (blockCount < newBlockCount) {
		PRINT("  level %" B_PRId32 ", child count: %" B_PRIu32 "\n", level,
			infos[level].childCount);

		if (infos[level].childCount >= (int32)kFileBlockMaxCount) {
			// block is full -- back track
			level--;
		}

		// allocate and insert block
		AllocatedBlock allocatedBlock(GetVolume()->GetBlockAllocator(),
			transaction);
		status_t error = allocatedBlock.Allocate(BlockIndex());
		if (error != B_OK)
			RETURN_ERROR(error);

		uint64 blockIndex = allocatedBlock.Index();
		infos[level].blockData[infos[level].childCount++] = blockIndex;

		PRINT("  allocated block: %" B_PRId64 "\n", blockIndex);

		if (level < depth - 1) {
			// descend to the next level
			level++;
			infos[level].childCount = 0;

			if (!infos[level].block.GetZero(GetVolume(), blockIndex,
					transaction)) {
				RETURN_ERROR(B_ERROR);
			}

			infos[level].blockData = (uint64*)infos[level].block.Data();
		} else {
			// That's a data block -- make the block cache forget it, so it
			// doesn't conflict with the file cache.
			block_cache_discard(GetVolume()->BlockCache(), blockIndex, 1);
			blockCount++;
		}

		allocatedBlock.Detach();
	}

	return B_OK;
}


status_t
File::_WriteZeroes(uint64 offset, uint64 size)
{
	while (size > 0) {
		size_t toWrite = std::min(size, (uint64)SIZE_MAX);
		status_t error = file_cache_write(fFileCache, NULL, offset, NULL,
			&toWrite);
		if (error != B_OK)
			RETURN_ERROR(error);

		size -= toWrite;
		offset += toWrite;
	}

	return B_OK;
}
