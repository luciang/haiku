/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include "Volume.h"
#include "Directory.h"

#include <boot/partitions.h>
#include <boot/platform.h>
#include <boot/vfs.h>
#include <util/kernel_cpp.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


#define TRACE_BFS 0
#if TRACE_BFS
#	define TRACE(x) printf x
#else
#	define TRACE(x) ;
#endif


using namespace BFS;


Volume::Volume(boot::Partition *partition)
	:
	fRootNode(NULL)
{
	fDevice = open_node(partition, O_RDONLY);
	if (fDevice < B_OK)
		return;

	if (read_pos(fDevice, 512, &fSuperBlock, sizeof(disk_super_block)) < B_OK)
		return;

	if (!IsValidSuperBlock()) {
#ifndef BFS_LITTLE_ENDIAN_ONLY
		// try block 0 again (can only happen on the big endian BFS)
		if (read_pos(fDevice, 0, &fSuperBlock, sizeof(disk_super_block)) < B_OK)
			return;
	
		if (!IsValidSuperBlock())
			return;
#else
		return;
#endif
	}

	TRACE(("bfs: we do have a valid super block (name = %s)!\n", fSuperBlock.name));

	fRootNode = new BFS::Directory(*this, Root());
	if (fRootNode == NULL)
		return;

	if (fRootNode->InitCheck() < B_OK) {
		TRACE(("bfs: init check for root node failed\n"));
		delete fRootNode;
		fRootNode = NULL;
		return;
	}
}


Volume::~Volume()
{
	close(fDevice);
}


status_t 
Volume::InitCheck()
{
	if (fDevice < B_OK)
		return B_ERROR;

	return fRootNode != NULL ? B_OK : B_ERROR;
}


bool
Volume::IsValidSuperBlock()
{
	if (fSuperBlock.Magic1() != (int32)SUPER_BLOCK_MAGIC1
		|| fSuperBlock.Magic2() != (int32)SUPER_BLOCK_MAGIC2
		|| fSuperBlock.Magic3() != (int32)SUPER_BLOCK_MAGIC3
		|| (int32)fSuperBlock.block_size != fSuperBlock.inode_size
		|| fSuperBlock.ByteOrder() != SUPER_BLOCK_FS_LENDIAN
		|| (1UL << fSuperBlock.BlockShift()) != fSuperBlock.BlockSize()
		|| fSuperBlock.AllocationGroups() < 1
		|| fSuperBlock.AllocationGroupShift() < 1
		|| fSuperBlock.BlocksPerAllocationGroup() < 1
		|| fSuperBlock.NumBlocks() < 10
		|| fSuperBlock.AllocationGroups() != divide_roundup(fSuperBlock.NumBlocks(), 1L << fSuperBlock.AllocationGroupShift()))
		return false;

	return true;
}


status_t
Volume::ValidateBlockRun(block_run run)
{
	if (run.AllocationGroup() < 0 || run.AllocationGroup() > (int32)AllocationGroups()
		|| run.Start() > (1UL << AllocationGroupShift())
		|| run.length == 0
		|| uint32(run.Length() + run.Start()) > (1UL << AllocationGroupShift())) {
		dprintf("bfs: invalid run(%ld,%d,%d)\n", run.AllocationGroup(), run.Start(), run.Length());
		return B_BAD_DATA;
	}
	return B_OK;
}


block_run 
Volume::ToBlockRun(off_t block) const
{
	block_run run;
	run.allocation_group = HOST_ENDIAN_TO_BFS_INT32(block >> fSuperBlock.AllocationGroupShift());
	run.start = HOST_ENDIAN_TO_BFS_INT16(block & ~((1LL << fSuperBlock.AllocationGroupShift()) - 1));
	run.length = HOST_ENDIAN_TO_BFS_INT16(1);
	return run;
}


//	#pragma mark -


static status_t
bfs_get_file_system(boot::Partition *partition, ::Directory **_root)
{
	Volume *volume = new Volume(partition);
	if (volume == NULL)
		return B_NO_MEMORY;

	if (volume->InitCheck() < B_OK) {
		delete volume;
		return B_ERROR;
	}

	*_root = volume->RootNode();
	return B_OK;
}


file_system_module_info gBFSFileSystemModule = {
	kPartitionTypeBFS,
	bfs_get_file_system
};

