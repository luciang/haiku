/*
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */


#include "Inode.h"

#include <fs_cache.h>
#include <string.h>

#include "CachedBlock.h"


//#define TRACE_EXT2
#ifdef TRACE_EXT2
#	define TRACE(x...) dprintf("\33[34mext2:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif


Inode::Inode(Volume* volume, ino_t id)
	:
	fVolume(volume),
	fID(id),
	fCache(NULL),
	fMap(NULL),
	fNode(NULL),
	fAttributesBlock(NULL)
{
	rw_lock_init(&fLock, "ext2 inode");

	uint32 block;
	if (volume->GetInodeBlock(id, block) == B_OK) {
		TRACE("inode %Ld at block %lu\n", ID(), block);
		uint8* inodeBlock = (uint8*)block_cache_get(volume->BlockCache(),
			block);
		if (inodeBlock != NULL) {
			fNode = (ext2_inode*)(inodeBlock + volume->InodeBlockIndex(id)
				* volume->InodeSize());
		}
	}

	if (fNode != NULL) {
		// TODO: we don't need a cache for short symlinks
		fCache = file_cache_create(fVolume->ID(), ID(), Size());
		fMap = file_map_create(fVolume->ID(), ID(), Size());
	}
}


Inode::~Inode()
{
	file_cache_delete(FileCache());
	file_map_delete(Map());

	if (fAttributesBlock) {
		uint32 block = B_LENDIAN_TO_HOST_INT32(Node().file_access_control);
		block_cache_put(fVolume->BlockCache(), block);
	}

	if (fNode != NULL) {
		uint32 block;
		if (fVolume->GetInodeBlock(ID(), block) == B_OK)
			block_cache_put(fVolume->BlockCache(), block);
	}
}


status_t
Inode::InitCheck()
{
	return fNode != NULL ? B_OK : B_ERROR;
}


status_t
Inode::CheckPermissions(int accessMode) const
{
	uid_t user = geteuid();
	gid_t group = getegid();

	// you never have write access to a read-only volume
	if (accessMode & W_OK && fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	// root users always have full access (but they can't execute files without
	// any execute permissions set)
	if (user == 0) {
		if (!((accessMode & X_OK) != 0 && (Mode() & S_IXUSR) == 0)
			|| S_ISDIR(Mode()))
			return B_OK;
	}

	// shift mode bits, to check directly against accessMode
	mode_t mode = Mode();
	if (user == (uid_t)fNode->UserID())
		mode >>= 6;
	else if (group == (gid_t)fNode->GroupID())
		mode >>= 3;

	if (accessMode & ~(mode & S_IRWXO))
		return B_NOT_ALLOWED;

	return B_OK;
}


status_t
Inode::FindBlock(off_t offset, uint32& block)
{
	uint32 perBlock = fVolume->BlockSize() / 4;
	uint32 perIndirectBlock = perBlock * perBlock;
	uint32 index = offset >> fVolume->BlockShift();

	if (offset >= Size())
		return B_ENTRY_NOT_FOUND;

	// TODO: we could return the size of the sparse range, as this might be more
	// than just a block

	if (index < EXT2_DIRECT_BLOCKS) {
		// direct blocks
		block = B_LENDIAN_TO_HOST_INT32(Node().stream.direct[index]);
	} else if ((index -= EXT2_DIRECT_BLOCKS) < perBlock) {
		// indirect blocks
		CachedBlock cached(fVolume);
		uint32* indirectBlocks = (uint32*)cached.SetTo(B_LENDIAN_TO_HOST_INT32(
			Node().stream.indirect));
		if (indirectBlocks == NULL)
			return B_IO_ERROR;

		block = B_LENDIAN_TO_HOST_INT32(indirectBlocks[index]);
	} else if ((index -= perBlock) < perIndirectBlock) {
		// double indirect blocks
		CachedBlock cached(fVolume);
		uint32* indirectBlocks = (uint32*)cached.SetTo(B_LENDIAN_TO_HOST_INT32(
			Node().stream.double_indirect));
		if (indirectBlocks == NULL)
			return B_IO_ERROR;

		uint32 indirectIndex
			= B_LENDIAN_TO_HOST_INT32(indirectBlocks[index / perBlock]);
		if (indirectIndex == 0) {
			// a sparse indirect block
			block = 0;
		} else {
			indirectBlocks = (uint32*)cached.SetTo(indirectIndex);
			if (indirectBlocks == NULL)
				return B_IO_ERROR;

			block = B_LENDIAN_TO_HOST_INT32(
				indirectBlocks[index & (perBlock - 1)]);
		}
	} else if ((index -= perIndirectBlock) / perBlock < perIndirectBlock) {
		// triple indirect blocks
		CachedBlock cached(fVolume);
		uint32* indirectBlocks = (uint32*)cached.SetTo(B_LENDIAN_TO_HOST_INT32(
			Node().stream.triple_indirect));
		if (indirectBlocks == NULL)
			return B_IO_ERROR;

		uint32 indirectIndex
			= B_LENDIAN_TO_HOST_INT32(indirectBlocks[index / perIndirectBlock]);
		if (indirectIndex == 0) {
			// a sparse indirect block
			block = 0;
		} else {
			indirectBlocks = (uint32*)cached.SetTo(indirectIndex);
			if (indirectBlocks == NULL)
				return B_IO_ERROR;

			indirectIndex = B_LENDIAN_TO_HOST_INT32(
				indirectBlocks[(index / perBlock) & (perBlock - 1)]);
			if (indirectIndex == 0) {
				// a sparse indirect block
				block = 0;
			} else {
				indirectBlocks = (uint32*)cached.SetTo(indirectIndex);
				if (indirectBlocks == NULL)
					return B_IO_ERROR;

				block = B_LENDIAN_TO_HOST_INT32(
					indirectBlocks[index & (perBlock - 1)]);
			}
		}
	} else {
		// outside of the possible data stream
		dprintf("ext2: block outside datastream!\n");
		return B_ERROR;
	}

	TRACE("inode %Ld: FindBlock(offset %Ld): %lu\n", ID(), offset, block);
	return B_OK;
}


status_t
Inode::ReadAt(off_t pos, uint8* buffer, size_t* _length)
{
	size_t length = *_length;

	// set/check boundaries for pos/length
	if (pos < 0) {
		TRACE("inode %Ld: ReadAt failed(pos %Ld, length %lu)\n", ID(), pos, length);
		return B_BAD_VALUE;
	}

	if (pos >= Size() || length == 0) {
		TRACE("inode %Ld: ReadAt 0 (pos %Ld, length %lu)\n", ID(), pos, length);
		*_length = 0;
		return B_NO_ERROR;
	}

	return file_cache_read(FileCache(), NULL, pos, buffer, _length);
}


status_t
Inode::AttributeBlockReadAt(off_t pos, uint8* buffer, size_t* _length)
{
	TRACE("Inode::%s(%Ld, , %lu)\n", __FUNCTION__, pos, *_length);
	size_t length = *_length;

	if (!fAttributesBlock) {
		uint32 block = B_LENDIAN_TO_HOST_INT32(Node().file_access_control);

		if (block == 0)
			return B_ENTRY_NOT_FOUND;

		TRACE("inode %Ld attributes at block %lu\n", ID(), block);
		fAttributesBlock = (ext2_xattr_header*)block_cache_get(
			GetVolume()->BlockCache(), block);
	}

	if (!fAttributesBlock)
		return B_ENTRY_NOT_FOUND;

	if (pos < 0LL || ((uint32)pos + length) > GetVolume()->BlockSize())
		return ERANGE;

	memcpy(buffer, ((uint8 *)fAttributesBlock) + (uint32)pos, length);

	*_length = length;
	return B_NO_ERROR;
}
