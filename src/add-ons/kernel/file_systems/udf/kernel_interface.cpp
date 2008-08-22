/*
 * Copyright 2008, Salvatore Benedetto, salvatore.benedetto@gmail.com.
 * Copyright 2003, Tyler Dauwalder, tyler@dauwalder.net.
 * Distributed under the terms of the MIT License.
 */

/*! \file kernel_interface.cpp */

#include <Drivers.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <util/kernel_cpp.h>

#include "Icb.h"
#include "Recognition.h"
#include "Utils.h"
#include "Volume.h"


#undef TRACE
#undef TRACE_ERROR
#define UDF_KERNEL_INTERFACE_DEBUG
#ifdef UDF_KERNEL_INTERFACE_DEBUG
#	define TRACE(x)			dprintf x
#	define TRACE_ERROR(x)	dprintf x
#else
#	define TRACE(x)			/* nothing */
#	define TRACE_ERROR(x)	dprintf x
#endif

extern fs_volume_ops gUDFVolumeOps;
extern fs_vnode_ops gUDFVnodeOps;


//	#pragma mark - fs_volume_ops fuctions


static float
udf_identify_partition(int fd, partition_data *partition, void **_cookie)
{
	TRACE(("udf_identify_partition: fd = %d, id = %d, offset = %d, size = %d "
		"content_size = %d, block_size = %d\n", fd, partition->id,
		partition->offset, partition->size, partition->content_size,
		partition->block_size));

	logical_volume_descriptor logicalVolumeDescriptor;
	partition_descriptor partitionDescriptors[kMaxPartitionDescriptors];
	uint8 descriptorCount = kMaxPartitionDescriptors;
	uint32 blockShift;
	status_t error = udf_recognize(fd, partition->offset, partition->size,
		partition->block_size, blockShift, logicalVolumeDescriptor,
		partitionDescriptors, descriptorCount);
	if (error != B_OK)
		return -1;

	return 0.8f;
}


static status_t
udf_scan_partition(int fd, partition_data *partition, void *_cookie)
{
	TRACE(("udf_scan_partition: fd = %d\n", fd));

#if 0
	UdfString name(logicalVolumeDescriptor.logical_volume_identifier());
	partition->content_name = strdup(name.Utf8());
#endif
	return B_ERROR;
}


static status_t
udf_unmount(fs_volume *_volume)
{
	TRACE(("udb_unmount: _volume = %p\n", _volume));
	Volume *volume = (Volume *)_volume->private_volume;
	delete volume;
	return B_OK;
}


static status_t
udf_read_fs_stat(fs_volume *_volume, struct fs_info *info)
{
	TRACE(("udf_read_fs_stat: _volume = %p, info = %p\n", _volume, info));

	Volume *volume = (Volume *)_volume->private_volume;

	// File system flags.
	info->flags = B_FS_IS_PERSISTENT | B_FS_IS_READONLY;

	info->io_size = 65536;
		// whatever is appropriate here? Just use the same value as BFS (and iso9660) for now

	info->block_size = volume->BlockSize();
	info->total_blocks = volume->Length();
	info->free_blocks = 0;

	// Volume name
	sprintf(info->volume_name, "%s", volume->Name());

	// File system name
	strcpy(info->fsh_name, "udf");

	return B_OK;
}


static status_t
udf_get_vnode(fs_volume *_volume, ino_t id, fs_vnode *_node, int *_type,
	uint32 *_flags, bool reenter)
{
	TRACE(("udf_get_vnode: _volume = %p, _node = %p, reenter = %s\n",
		_volume, _node, (reenter ? "true" : "false")));

	Volume *volume = (Volume *)_volume->private_volume;

	// Convert the given vnode id to an address, and create
	// and return a corresponding Icb object for it.
	TRACE(("udf_get_vnode: id = %d, blockSize = %d\n", id, volume->BlockSize()));
	Icb *icb = new(std::nothrow) Icb(volume,
		to_long_address(id, volume->BlockSize()));
	if (icb) {
		if(icb->InitCheck() == B_OK) {
			if (_node)
				_node->private_node = icb;
				_node->ops = &gUDFVnodeOps;
				_flags = 0;
		} else {
			TRACE_ERROR(("udf_get_vnode: InitCheck failed\n"));
			delete icb;
			return B_ERROR;
		}
	} else
		return B_NO_MEMORY;

	return B_OK;
}


//	#pragma mark - fs_vnode_ops functions


static status_t
udf_lookup(fs_volume *_volume, fs_vnode *_directory, const char *file,
	ino_t *vnodeID)
{
	TRACE(("udf_lookup: _directory = %p, filename = %s\n", _directory, file));

	Volume *volume = (Volume *)_volume->private_volume;
	Icb *dir = (Icb *)_directory->private_node;
	Icb *node = NULL;

	status_t status = B_OK;

	if (strcmp(file, ".") == 0) {
		TRACE(("udf_lookup: file = ./\n"));
		*vnodeID = dir->Id();
		status = get_vnode(volume->FSVolume(), *vnodeID, (void **)&node);
		if (status != B_OK)
			return B_ENTRY_NOT_FOUND;
	} else {
		status = dir->Find(file, vnodeID);
		if (status != B_OK)
			return status;

		Icb *icb;
		status = get_vnode(volume->FSVolume(), *vnodeID, (void **)&icb);
		if (status != B_OK)
			return B_ENTRY_NOT_FOUND;
	}
	TRACE(("udf_lookup: vnodeId = %Ld found!\n", *vnodeID));

	return B_OK;
}


static status_t
udf_put_vnode(fs_volume *volume, fs_vnode *node, bool reenter)
{
// No debug-to-file in release_vnode; can cause a deadlock in
// rare circumstances.
#if !DEBUG_TO_FILE
	DEBUG_INIT_ETC(NULL, ("node: %p", node));
#endif
	Icb *icb = reinterpret_cast<Icb*>(node);
	delete icb;
#if !DEBUG_TO_FILE
	RETURN(B_OK);
#else
	return B_OK;
#endif
}


static status_t
udf_read_stat(fs_volume *_volume, fs_vnode *node, struct stat *stat)
{
	TRACE(("udf_read_stat: _volume = %p, node = %p\n", _volume, node));

	if (!_volume || !node || !stat)
		return B_BAD_VALUE;

	Volume *volume = (Volume *)_volume->private_volume;
	Icb *icb = (Icb *)node->private_node;

	stat->st_dev = volume->ID();
	stat->st_ino = icb->Id();
	stat->st_nlink = icb->FileLinkCount();
	stat->st_blksize = volume->BlockSize();

	TRACE(("udf_read_stat: st_dev = %d, st_ino = %d, st_blksize = %d\n",
		stat->st_dev, stat->st_ino, stat->st_blksize));

	stat->st_uid = icb->Uid();
	stat->st_gid = icb->Gid();

	stat->st_mode = icb->Mode();
	stat->st_size = icb->Length();

	// File times. For now, treat the modification time as creation
	// time as well, since true creation time is an optional extended
	// attribute, and supporting EAs is going to be a PITA. ;-)
	stat->st_atime = icb->AccessTime();
	stat->st_mtime = stat->st_ctime = stat->st_crtime = icb->ModificationTime();

	TRACE(("udf_read_stat: mode = 0x%lx, st_ino: %Ld\n", stat->st_mode,
		stat->st_ino));

	return B_OK;
}


static status_t
udf_open(fs_volume* _volume, fs_vnode* _node, int openMode, void** _cookie)
{
	TRACE(("udf_open: _volume = %p, _node = %p\n", _volume, _node));
	return B_OK;
}


static status_t
udf_close(fs_volume* _volume, fs_vnode* _node, void* _cookie)
{
	TRACE(("udf_close: _volume = %p, _node = %p\n", _volume, _node));
	return B_OK;
}


static status_t
udf_free_cookie(fs_volume* _volume, fs_vnode* _node, void* _cookie)
{	
	TRACE(("udf_free_cookie: _volume = %p, _node = %p\n", _volume, _node));
	return B_OK;
}


static status_t
udf_access(fs_volume* _volume, fs_vnode* _node, int accessMode)
{	
	TRACE(("udf_access: _volume = %p, _node = %p\n", _volume, _node));
	return B_OK;
}


static status_t
udf_read(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	void *buffer, size_t *length)
{
	TRACE(("udf_read: ID = %ld, pos = %lld, length = %lu\n",
		((Volume *)volume->private_volume)->ID(), pos, *length));

	Icb *icb = (Icb *)vnode->private_node;

//	if (!inode->HasUserAccessableStream()) {
//		*_length = 0;
//		RETURN_ERROR(B_BAD_VALUE);
//	}

	RETURN(icb->Read(pos, buffer, length));
}


static status_t
udf_open_dir(fs_volume *volume, fs_vnode *vnode, void **cookie)
{
	TRACE(("udf_open_dir: volume = %p, vnode = %p\n", volume, vnode));

	if (!volume || !vnode || !cookie)
		RETURN(B_BAD_VALUE);

	Icb *dir = (Icb *)vnode->private_node;

	if (!dir->IsDirectory()) {
		TRACE_ERROR(("udf_open_dir: given Icb is not a directory (type: %d)\n",
			dir->Type()));
		return B_BAD_VALUE;
	}

	DirectoryIterator *iterator = NULL;
	status_t status = dir->GetDirectoryIterator(&iterator);
	if (status != B_OK) {
		TRACE_ERROR(("udf_open_dir: error getting directory iterator: 0x%lx, "
			"`%s'\n", status, strerror(status)));
		return status;
	}
	*cookie = (void *)iterator;
	TRACE(("udf_open_dir: *cookie = %p\n", *cookie));

	return B_OK;
}


static status_t
udf_close_dir(fs_volume *_volume, fs_vnode *node, void *_cookie)
{
	TRACE(("udf_close_dir: _volume = %p, node = %p\n", _volume, node));
	return B_OK;
}


static status_t
udf_free_dir_cookie(fs_volume *_volume, fs_vnode *node, void *_cookie)
{
	TRACE(("udf_free_dir_cookie: _volume = %p, node = %p\n", _volume, node));
	return B_OK;
}


static status_t
udf_read_dir(fs_volume *_volume, fs_vnode *vnode, void *cookie,
	struct dirent *dirent, size_t bufferSize, uint32 *_num)
{
	TRACE(("udf_read_dir: _volume = %p, vnode = %p, bufferSize = %ld\n",
		_volume, vnode, bufferSize));

	if (!_volume || !vnode || !cookie || !_num || bufferSize < sizeof(dirent))
		return B_BAD_VALUE;

	Volume *volume = (Volume *)_volume->private_volume;
	Icb *dir = (Icb *)vnode->private_node;
	DirectoryIterator *iterator = (DirectoryIterator *)cookie;

	if (dir != iterator->Parent()) {
		TRACE_ERROR(("udf_read_dir: Icb does not match parent Icb of given "
			"DirectoryIterator! (iterator->Parent = %p)\n", iterator->Parent()));
		return B_BAD_VALUE;
	}

	uint32 nameLength = bufferSize - sizeof(dirent) + 1;
	ino_t id;
	status_t status = iterator->GetNextEntry(dirent->d_name, &nameLength, &id);
	if (!status) {
		*_num = 1;
		dirent->d_dev = volume->ID();
		dirent->d_ino = id;
		dirent->d_reclen = sizeof(dirent) + nameLength - 1;
	} else {
		*_num = 0;
		// Clear the status for end of directory
		if (status == B_ENTRY_NOT_FOUND)
			status = B_OK;
	}

	RETURN(status);
}


status_t
udf_rewind_dir(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	DEBUG_INIT_ETC(NULL,
		       ("dir: %p, iterator: %p", node, cookie));

	if (!volume || !vnode || !cookie)
		RETURN(B_BAD_VALUE);

	Icb *dir = (Icb *)vnode->private_node;
	DirectoryIterator *iterator = reinterpret_cast<DirectoryIterator*>(cookie);

	if (dir != iterator->Parent()) {
		PRINT(("Icb does not match parent Icb of given DirectoryIterator! (iterator->Parent = %p)\n",
		       iterator->Parent()));
		return B_BAD_VALUE;
	}

	iterator->Rewind();

	RETURN(B_OK);
}


//	#pragma mark -


/*! \brief mount

	\todo I'm using the B_GET_GEOMETRY ioctl() to find out where the end of the
	      partition is. This won't work for handling multi-session semantics correctly.
	      To support them correctly in R5 I need either:
	      - A way to get the proper info (best)
	      - To ignore trying to find anchor volume descriptor pointers at
	        locations N-256 and N. (acceptable, perhaps, but not really correct)
	      Either way we should address this problem properly for OBOS::R1.
	\todo Looks like B_GET_GEOMETRY doesn't work on non-device files (i.e.
	      disk images), so I need to use stat or something else for those
	      instances.
*/
static status_t
udf_mount(fs_volume *_volume, const char *_device, uint32 flags,
	const char *args, ino_t *_rootVnodeID)
{
	TRACE(("udf_mount: device = %s\n", _device));
	status_t status = B_OK;
	Volume *volume = NULL;
	off_t deviceOffset = 0;
	off_t numBlock = 0;
	partition_info info;
	device_geometry geometry;

	// Here we need to figure out the length of the device, and if we're
	// attempting to open a multisession volume, we need to figure out the
	// offset into the raw disk at which the volume begins, then open
	// the raw volume itself instead of the fake partition device the
	// kernel gives us, since multisession UDF volumes are allowed to access
	// the data in their own partition, as well as the data in any partitions
	// that precede them physically on the disc.
	int device = open(_device, O_RDONLY);
	status = device < B_OK ? device : B_OK;
	if (!status) {
		// First try to treat the device like a special partition device. If that's
		// what we have, then we can use the partition_info data to figure out the
		// name of the raw device (which we'll open instead), the offset into the
		// raw device at which the volume of interest will begin, and the total
		// length from the beginning of the raw device that we're allowed to access.
		//
		// If that fails, then we try to treat the device as an actual raw device,
		// and see if we can get the device size with B_GET_GEOMETRY syscall, since
		// stat()ing a raw device appears to not work.
		//
		// Finally, if that also fails, we're probably stuck with trying to mount
		// a regular file, so we just stat() it to get the device size.
		//
		// If that fails, you're just SOL.

		if (ioctl(device, B_GET_PARTITION_INFO, &info) == 0) {
			TRACE(("partition_info:\n"));
			TRACE(("\toffset:             %Ld\n", info.offset));
			TRACE(("\tsize:               %Ld\n", info.size));
			TRACE(("\tlogical_block_size: %ld\n", info.logical_block_size));
			TRACE(("\tsession:            %ld\n", info.session));
			TRACE(("\tpartition:          %ld\n", info.partition));
			TRACE(("\tdevice:             `%s'\n", info.device));
			_device = info.device;
			deviceOffset = info.offset / info.logical_block_size;
			numBlock = deviceOffset + info.size / info.logical_block_size;
		} else if (ioctl(device, B_GET_GEOMETRY, &geometry) == 0) {
			TRACE(("geometry_info:\n"));
			TRACE(("\tsectors_per_track: %ld\n", geometry.sectors_per_track));
			TRACE(("\tcylinder_count:    %ld\n", geometry.cylinder_count));
			TRACE(("\thead_count:        %ld\n", geometry.head_count));
			deviceOffset = 0;
			numBlock = (off_t)geometry.sectors_per_track
				* geometry.cylinder_count * geometry.head_count;
		} else {
			struct stat stat;
			status = fstat(device, &stat) < 0 ? B_ERROR : B_OK;
			if (!status) {
				TRACE(("stat_info:\n"));
				TRACE(("\tst_size: %Ld\n", stat.st_size));
				deviceOffset = 0;
				numBlock = stat.st_size / 2048;
			}
		}
		// Close the device
		close(device);
	}

	// Create and mount the volume
	volume = new(std::nothrow) Volume(_volume);
	status = volume->Mount(_device, deviceOffset, numBlock, 2048, flags);
	if (status != B_OK) {
		delete volume;
		return status;
	}

	_volume->private_volume = volume;
	_volume->ops = &gUDFVolumeOps;
	*_rootVnodeID = volume->RootIcb()->Id();

	TRACE(("udf_mount: succefully mounted the partition\n"));
	return B_OK;
}


//	#pragma mark -


static status_t
udf_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
		default:
			return B_ERROR;
	}
}

fs_volume_ops gUDFVolumeOps = {
	&udf_unmount,
	&udf_read_fs_stat,
	NULL,	// write_fs_stat
	NULL,	// sync
	&udf_get_vnode,

	/* index directory & index operations */
	NULL,	// open_index_dir
	NULL,	// close_index_dir
	NULL,	// free_index_dir_cookie
	NULL,	// read_index_dir
	NULL,	// rewind_index_dir
	NULL,	// create_index
	NULL,	// remove_index
	NULL,	// read_index_stat

	/* query operations */
	NULL,	// open_query
	NULL,	// close_query
	NULL,	// free_query_cookie
	NULL,	// read_query
	NULL,	// rewind_query

	/* support for FS layers */
	NULL,	// create_sub_vnode
	NULL,	// delete_sub_vnode
};

fs_vnode_ops gUDFVnodeOps = {
	/* vnode operatoins */
	&udf_lookup,
	NULL,	// get_vnode_name
	&udf_put_vnode,
	NULL,	// remove_vnode

	/* VM file access */
	NULL,	// can_page
	NULL,	// read_pages
	NULL,	// write_pages

	/* asynchronous I/O */
	NULL,	// io()
	NULL,	// cancel_io()

	/* cache file access */
	NULL,	// &udf_get_file_map,

	/* common operations */
	NULL,	// ioctl
	NULL,	// set_flags
	NULL,	// select
	NULL,	// deselect
	NULL,	// fsync
	NULL,	// read_symlink
	NULL,	// create_symlnk
	NULL,	// link
	NULL,	// unlink
	NULL,	// rename
	&udf_access,
	&udf_read_stat,
	NULL,	// write_stat

	/* file operations */
	NULL,	// create
	&udf_open,
	&udf_close,
	&udf_free_cookie,
	&udf_read,
	NULL,	// write

	/* directory operations */
	NULL,	// create_dir
	NULL,	// remove_dir
	&udf_open_dir,
	&udf_close_dir,
	&udf_free_dir_cookie,
	&udf_read_dir,
	&udf_rewind_dir,

	/* attribue directory operations */
	NULL,	// open_attr_dir
	NULL,	// close_attr_dir
	NULL,	// free_attr_dir_cookie
	NULL,	// read_attr_dir
	NULL,	// rewind_attr_dir

	/* attribute operations */
	NULL,	// create_attr
	NULL,	// open_attr
	NULL,	// close_attr
	NULL,	// free_attr_cookie
	NULL,	// read_attr
	NULL,	// write_attr
	NULL,	// read_attr_stat
	NULL,	// write_attr_stat
	NULL,	// rename_attr
	NULL,	// remove_attr

	/* support for node and FS layers */
	NULL,	// create_special_node
	NULL	// get_super_vnode

};

static file_system_module_info sUDFFileSystem = {
	{
		"file_systems/udf" B_CURRENT_FS_API_VERSION,
		0,
		udf_std_ops,
	},

	"udf",					// short_name
	"UDF File System",		// pretty_name
	0, // DDM flags

	&udf_identify_partition,
	&udf_scan_partition,
	NULL, // &udf_free_identify_patition_cookie,
	NULL,	// free_partition_content_cookie()

	&udf_mount,

	NULL,
};

module_info *modules[] = {
	(module_info *)&sUDFFileSystem,
	NULL,
};
