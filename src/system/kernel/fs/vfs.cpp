/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*! Virtual File System and File System Interface Layer */


#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs_info.h>
#include <fs_interface.h>
#include <fs_volume.h>
#include <OS.h>
#include <StorageDefs.h>

#include <util/AutoLock.h>

#include <block_cache.h>
#include <fd.h>
#include <file_cache.h>
#include <khash.h>
#include <KPath.h>
#include <lock.h>
#include <syscalls.h>
#include <syscall_restart.h>
#include <vfs.h>
#include <vm.h>
#include <vm_cache.h>
#include <vm_low_memory.h>

#include <boot/kernel_args.h>
#include <disk_device_manager/KDiskDevice.h>
#include <disk_device_manager/KDiskDeviceManager.h>
#include <disk_device_manager/KDiskDeviceUtils.h>
#include <disk_device_manager/KDiskSystem.h>
#include <fs/node_monitor.h>


//#define TRACE_VFS
#ifdef TRACE_VFS
#	define TRACE(x) dprintf x
#	define FUNCTION(x) dprintf x
#else
#	define TRACE(x) ;
#	define FUNCTION(x) ;
#endif

#define ADD_DEBUGGER_COMMANDS

const static uint32 kMaxUnusedVnodes = 8192;
	// This is the maximum number of unused vnodes that the system
	// will keep around (weak limit, if there is enough memory left,
	// they won't get flushed even when hitting that limit).
	// It may be chosen with respect to the available memory or enhanced
	// by some timestamp/frequency heurism.

struct vnode {
	struct vnode	*next;
	vm_cache		*cache;
	dev_t			device;
	list_link		mount_link;
	list_link		unused_link;
	ino_t			id;
	fs_vnode		private_node;
	struct fs_mount	*mount;
	struct vnode	*covered_by;
	int32			ref_count;
	uint8			remove : 1;
	uint8			busy : 1;
	uint8			unpublished : 1;
	struct advisory_locking	*advisory_locking;
	struct file_descriptor *mandatory_locked_by;
};

struct vnode_hash_key {
	dev_t	device;
	ino_t	vnode;
};

#define FS_CALL(vnode, op) (vnode->mount->fs->op)
#define FS_MOUNT_CALL(mount, op) (mount->fs->op)

/*!	\brief Structure to manage a mounted file system

	Note: The root_vnode and covers_vnode fields (what others?) are
	initialized in fs_mount() and not changed afterwards. That is as soon
	as the mount is mounted and it is made sure it won't be unmounted
	(e.g. by holding a reference to a vnode of that mount) (read) access
	to those fields is always safe, even without additional locking. Morever
	while mounted the mount holds a reference to the covers_vnode, and thus
	making the access path vnode->mount->covers_vnode->mount->... safe if a
	reference to vnode is held (note that for the root mount covers_vnode
	is NULL, though).
*/
struct fs_mount {
	struct fs_mount	*next;
	file_system_module_info *fs;
	dev_t			id;
	void			*cookie;
	char			*device_name;
	char			*fs_name;
	recursive_lock	rlock;	// guards the vnodes list
	struct vnode	*root_vnode;
	struct vnode	*covers_vnode;
	KPartition		*partition;
	struct list		vnodes;
	bool			unmounting;
	bool			owns_file_device;
};

struct advisory_lock : public DoublyLinkedListLinkImpl<advisory_lock> {
	list_link		link;
	team_id			team;
	pid_t			session;
	off_t			start;
	off_t			end;
	bool			shared;
};

typedef DoublyLinkedList<advisory_lock> LockList;

struct advisory_locking {
	sem_id			lock;
	sem_id			wait_sem;
	LockList		locks;
};

static mutex sFileSystemsMutex;

/*!	\brief Guards sMountsTable.

	The holder is allowed to read/write access the sMountsTable.
	Manipulation of the fs_mount structures themselves
	(and their destruction) requires different locks though.
*/
static mutex sMountMutex;

/*!	\brief Guards mount/unmount operations.

	The fs_mount() and fs_unmount() hold the lock during their whole operation.
	That is locking the lock ensures that no FS is mounted/unmounted. In
	particular this means that
	- sMountsTable will not be modified,
	- the fields immutable after initialization of the fs_mount structures in
	  sMountsTable will not be modified,
	- vnode::covered_by of any vnode in sVnodeTable will not be modified.

	The thread trying to lock the lock must not hold sVnodeMutex or
	sMountMutex.
*/
static recursive_lock sMountOpLock;

/*!	\brief Guards the vnode::covered_by field of any vnode

	The holder is allowed to read access the vnode::covered_by field of any
	vnode. Additionally holding sMountOpLock allows for write access.

	The thread trying to lock the must not hold sVnodeMutex.
*/
static mutex sVnodeCoveredByMutex;

/*!	\brief Guards sVnodeTable.

	The holder is allowed to read/write access sVnodeTable and to
	any unbusy vnode in that table, save to the immutable fields (device, id,
	private_node, mount) to which
	only read-only access is allowed, and to the field covered_by, which is
	guarded by sMountOpLock and sVnodeCoveredByMutex.

	The thread trying to lock the mutex must not hold sMountMutex.
	You must not have this mutex held when calling create_sem(), as this
	might call vfs_free_unused_vnodes().
*/
static mutex sVnodeMutex;

#define VNODE_HASH_TABLE_SIZE 1024
static hash_table *sVnodeTable;
static list sUnusedVnodeList;
static uint32 sUnusedVnodes = 0;
static struct vnode *sRoot;

#define MOUNTS_HASH_TABLE_SIZE 16
static hash_table *sMountsTable;
static dev_t sNextMountID = 1;

#define MAX_TEMP_IO_VECS 8

mode_t __gUmask = 022;

/* function declarations */

// file descriptor operation prototypes
static status_t file_read(struct file_descriptor *, off_t pos, void *buffer, size_t *);
static status_t file_write(struct file_descriptor *, off_t pos, const void *buffer, size_t *);
static off_t file_seek(struct file_descriptor *, off_t pos, int seek_type);
static void file_free_fd(struct file_descriptor *);
static status_t file_close(struct file_descriptor *);
static status_t file_select(struct file_descriptor *, uint8 event,
	struct selectsync *sync);
static status_t file_deselect(struct file_descriptor *, uint8 event,
	struct selectsync *sync);
static status_t dir_read(struct file_descriptor *, struct dirent *buffer, size_t bufferSize, uint32 *_count);
static status_t dir_read(struct vnode *vnode, fs_cookie cookie, struct dirent *buffer, size_t bufferSize, uint32 *_count);
static status_t dir_rewind(struct file_descriptor *);
static void dir_free_fd(struct file_descriptor *);
static status_t dir_close(struct file_descriptor *);
static status_t attr_dir_read(struct file_descriptor *, struct dirent *buffer, size_t bufferSize, uint32 *_count);
static status_t attr_dir_rewind(struct file_descriptor *);
static void attr_dir_free_fd(struct file_descriptor *);
static status_t attr_dir_close(struct file_descriptor *);
static status_t attr_read(struct file_descriptor *, off_t pos, void *buffer, size_t *);
static status_t attr_write(struct file_descriptor *, off_t pos, const void *buffer, size_t *);
static off_t attr_seek(struct file_descriptor *, off_t pos, int seek_type);
static void attr_free_fd(struct file_descriptor *);
static status_t attr_close(struct file_descriptor *);
static status_t attr_read_stat(struct file_descriptor *, struct stat *);
static status_t attr_write_stat(struct file_descriptor *, const struct stat *, int statMask);
static status_t index_dir_read(struct file_descriptor *, struct dirent *buffer, size_t bufferSize, uint32 *_count);
static status_t index_dir_rewind(struct file_descriptor *);
static void index_dir_free_fd(struct file_descriptor *);
static status_t index_dir_close(struct file_descriptor *);
static status_t query_read(struct file_descriptor *, struct dirent *buffer, size_t bufferSize, uint32 *_count);
static status_t query_rewind(struct file_descriptor *);
static void query_free_fd(struct file_descriptor *);
static status_t query_close(struct file_descriptor *);

static status_t common_ioctl(struct file_descriptor *, ulong, void *buf, size_t len);
static status_t common_read_stat(struct file_descriptor *, struct stat *);
static status_t common_write_stat(struct file_descriptor *, const struct stat *, int statMask);

static status_t vnode_path_to_vnode(struct vnode *vnode, char *path,
	bool traverseLeafLink, int count, struct vnode **_vnode, ino_t *_parentID, int *_type);
static status_t dir_vnode_to_path(struct vnode *vnode, char *buffer, size_t bufferSize);
static status_t fd_and_path_to_vnode(int fd, char *path, bool traverseLeafLink,
	struct vnode **_vnode, ino_t *_parentID, bool kernel);
static void inc_vnode_ref_count(struct vnode *vnode);
static status_t dec_vnode_ref_count(struct vnode *vnode, bool reenter);
static inline void put_vnode(struct vnode *vnode);
static status_t fs_unmount(char *path, dev_t mountID, uint32 flags,
	bool kernel);


static struct fd_ops sFileOps = {
	file_read,
	file_write,
	file_seek,
	common_ioctl,
	file_select,
	file_deselect,
	NULL,		// read_dir()
	NULL,		// rewind_dir()
	common_read_stat,
	common_write_stat,
	file_close,
	file_free_fd
};

static struct fd_ops sDirectoryOps = {
	NULL,		// read()
	NULL,		// write()
	NULL,		// seek()
	common_ioctl,
	NULL,		// select()
	NULL,		// deselect()
	dir_read,
	dir_rewind,
	common_read_stat,
	common_write_stat,
	dir_close,
	dir_free_fd
};

static struct fd_ops sAttributeDirectoryOps = {
	NULL,		// read()
	NULL,		// write()
	NULL,		// seek()
	common_ioctl,
	NULL,		// select()
	NULL,		// deselect()
	attr_dir_read,
	attr_dir_rewind,
	common_read_stat,
	common_write_stat,
	attr_dir_close,
	attr_dir_free_fd
};

static struct fd_ops sAttributeOps = {
	attr_read,
	attr_write,
	attr_seek,
	common_ioctl,
	NULL,		// select()
	NULL,		// deselect()
	NULL,		// read_dir()
	NULL,		// rewind_dir()
	attr_read_stat,
	attr_write_stat,
	attr_close,
	attr_free_fd
};

static struct fd_ops sIndexDirectoryOps = {
	NULL,		// read()
	NULL,		// write()
	NULL,		// seek()
	NULL,		// ioctl()
	NULL,		// select()
	NULL,		// deselect()
	index_dir_read,
	index_dir_rewind,
	NULL,		// read_stat()
	NULL,		// write_stat()
	index_dir_close,
	index_dir_free_fd
};

#if 0
static struct fd_ops sIndexOps = {
	NULL,		// read()
	NULL,		// write()
	NULL,		// seek()
	NULL,		// ioctl()
	NULL,		// select()
	NULL,		// deselect()
	NULL,		// dir_read()
	NULL,		// dir_rewind()
	index_read_stat,	// read_stat()
	NULL,		// write_stat()
	NULL,		// dir_close()
	NULL		// free_fd()
};
#endif

static struct fd_ops sQueryOps = {
	NULL,		// read()
	NULL,		// write()
	NULL,		// seek()
	NULL,		// ioctl()
	NULL,		// select()
	NULL,		// deselect()
	query_read,
	query_rewind,
	NULL,		// read_stat()
	NULL,		// write_stat()
	query_close,
	query_free_fd
};


// VNodePutter
class VNodePutter {
public:
	VNodePutter(struct vnode *vnode = NULL) : fVNode(vnode) {}

	~VNodePutter()
	{
		Put();
	}

	void SetTo(struct vnode *vnode)
	{
		Put();
		fVNode = vnode;
	}

	void Put()
	{
		if (fVNode) {
			put_vnode(fVNode);
			fVNode = NULL;
		}
	}

	struct vnode *Detach()
	{
		struct vnode *vnode = fVNode;
		fVNode = NULL;
		return vnode;
	}

private:
	struct vnode *fVNode;
};


class FDCloser {
public:
	FDCloser() : fFD(-1), fKernel(true) {}

	FDCloser(int fd, bool kernel) : fFD(fd), fKernel(kernel) {}

	~FDCloser()
	{
		Close();
	}

	void SetTo(int fd, bool kernel)
	{
		Close();
		fFD = fd;
		fKernel = kernel;
	}

	void Close()
	{
		if (fFD >= 0) {
			if (fKernel)
				_kern_close(fFD);
			else
				_user_close(fFD);
			fFD = -1;
		}
	}

	int Detach()
	{
		int fd = fFD;
		fFD = -1;
		return fd;
	}

private:
	int		fFD;
	bool	fKernel;
};


static int
mount_compare(void *_m, const void *_key)
{
	struct fs_mount *mount = (fs_mount *)_m;
	const dev_t *id = (dev_t *)_key;

	if (mount->id == *id)
		return 0;

	return -1;
}


static uint32
mount_hash(void *_m, const void *_key, uint32 range)
{
	struct fs_mount *mount = (fs_mount *)_m;
	const dev_t *id = (dev_t *)_key;

	if (mount)
		return mount->id % range;

	return (uint32)*id % range;
}


/*! Finds the mounted device (the fs_mount structure) with the given ID.
	Note, you must hold the gMountMutex lock when you call this function.
*/
static struct fs_mount *
find_mount(dev_t id)
{
	ASSERT_LOCKED_MUTEX(&sMountMutex);

	return (fs_mount *)hash_lookup(sMountsTable, (void *)&id);
}


static status_t
get_mount(dev_t id, struct fs_mount **_mount)
{
	struct fs_mount *mount;
	status_t status;

	MutexLocker nodeLocker(sVnodeMutex);
	MutexLocker mountLocker(sMountMutex);

	mount = find_mount(id);
	if (mount == NULL)
		return B_BAD_VALUE;

	struct vnode* rootNode = mount->root_vnode;
	if (rootNode == NULL || rootNode->busy || rootNode->ref_count == 0) {
		// might have been called during a mount/unmount operation
		return B_BUSY;
	}

	inc_vnode_ref_count(mount->root_vnode);
	*_mount = mount;
	return B_OK;
}


static void
put_mount(struct fs_mount *mount)
{
	if (mount)
		put_vnode(mount->root_vnode);
}


static status_t
put_file_system(file_system_module_info *fs)
{
	return put_module(fs->info.name);
}


/*!	Tries to open the specified file system module.
	Accepts a file system name of the form "bfs" or "file_systems/bfs/v1".
	Returns a pointer to file system module interface, or NULL if it
	could not open the module.
*/
static file_system_module_info *
get_file_system(const char *fsName)
{
	char name[B_FILE_NAME_LENGTH];
	if (strncmp(fsName, "file_systems/", strlen("file_systems/"))) {
		// construct module name if we didn't get one
		// (we currently support only one API)
		snprintf(name, sizeof(name), "file_systems/%s/v1", fsName);
		fsName = NULL;
	}

	file_system_module_info *info;
	if (get_module(fsName ? fsName : name, (module_info **)&info) != B_OK)
		return NULL;

	return info;
}


/*!	Accepts a file system name of the form "bfs" or "file_systems/bfs/v1"
	and returns a compatible fs_info.fsh_name name ("bfs" in both cases).
	The name is allocated for you, and you have to free() it when you're
	done with it.
	Returns NULL if the required memory is no available.
*/
static char *
get_file_system_name(const char *fsName)
{
	const size_t length = strlen("file_systems/");

	if (strncmp(fsName, "file_systems/", length)) {
		// the name already seems to be the module's file name
		return strdup(fsName);
	}

	fsName += length;
	const char *end = strchr(fsName, '/');
	if (end == NULL) {
		// this doesn't seem to be a valid name, but well...
		return strdup(fsName);
	}

	// cut off the trailing /v1

	char *name = (char *)malloc(end + 1 - fsName);
	if (name == NULL)
		return NULL;

	strlcpy(name, fsName, end + 1 - fsName);
	return name;
}


static int
vnode_compare(void *_vnode, const void *_key)
{
	struct vnode *vnode = (struct vnode *)_vnode;
	const struct vnode_hash_key *key = (vnode_hash_key *)_key;

	if (vnode->device == key->device && vnode->id == key->vnode)
		return 0;

	return -1;
}


static uint32
vnode_hash(void *_vnode, const void *_key, uint32 range)
{
	struct vnode *vnode = (struct vnode *)_vnode;
	const struct vnode_hash_key *key = (vnode_hash_key *)_key;

#define VHASH(mountid, vnodeid) (((uint32)((vnodeid) >> 32) + (uint32)(vnodeid)) ^ (uint32)(mountid))

	if (vnode != NULL)
		return VHASH(vnode->device, vnode->id) % range;

	return VHASH(key->device, key->vnode) % range;

#undef VHASH
}


static void
add_vnode_to_mount_list(struct vnode *vnode, struct fs_mount *mount)
{
	recursive_lock_lock(&mount->rlock);

	list_add_link_to_head(&mount->vnodes, &vnode->mount_link);

	recursive_lock_unlock(&mount->rlock);
}


static void
remove_vnode_from_mount_list(struct vnode *vnode, struct fs_mount *mount)
{
	recursive_lock_lock(&mount->rlock);

	list_remove_link(&vnode->mount_link);
	vnode->mount_link.next = vnode->mount_link.prev = NULL;

	recursive_lock_unlock(&mount->rlock);
}


static status_t
create_new_vnode(struct vnode **_vnode, dev_t mountID, ino_t vnodeID)
{
	FUNCTION(("create_new_vnode()\n"));

	struct vnode *vnode = (struct vnode *)malloc(sizeof(struct vnode));
	if (vnode == NULL)
		return B_NO_MEMORY;

	// initialize basic values
	memset(vnode, 0, sizeof(struct vnode));
	vnode->device = mountID;
	vnode->id = vnodeID;

	// add the vnode to the mount structure
	mutex_lock(&sMountMutex);
	vnode->mount = find_mount(mountID);
	if (!vnode->mount || vnode->mount->unmounting) {
		mutex_unlock(&sMountMutex);
		free(vnode);
		return B_ENTRY_NOT_FOUND;
	}

	hash_insert(sVnodeTable, vnode);
	add_vnode_to_mount_list(vnode, vnode->mount);

	mutex_unlock(&sMountMutex);

	vnode->ref_count = 1;
	*_vnode = vnode;

	return B_OK;
}


/*!	Frees the vnode and all resources it has acquired, and removes
	it from the vnode hash as well as from its mount structure.
	Will also make sure that any cache modifications are written back.
*/
static void
free_vnode(struct vnode *vnode, bool reenter)
{
	ASSERT_PRINT(vnode->ref_count == 0 && vnode->busy, "vnode: %p\n", vnode);

	// write back any changes in this vnode's cache -- but only
	// if the vnode won't be deleted, in which case the changes
	// will be discarded

	if (!vnode->remove && FS_CALL(vnode, fsync) != NULL)
		FS_CALL(vnode, fsync)(vnode->mount->cookie, vnode->private_node);

	// Note: If this vnode has a cache attached, there will still be two
	// references to that cache at this point. The last one belongs to the vnode
	// itself (cf. vfs_get_vnode_cache()) and one belongs to the node's file
	// cache. Each but the last reference to a cache also includes a reference
	// to the vnode. The file cache, however, released its reference (cf.
	// file_cache_create()), so that this vnode's ref count has the chance to
	// ever drop to 0. Deleting the file cache now, will cause the next to last
	// cache reference to be released, which will also release a (no longer
	// existing) vnode reference. To avoid problems, we set the vnode's ref
	// count, so that it will neither become negative nor 0.
	vnode->ref_count = 2;

	// TODO: Usually, when the vnode is unreferenced, no one can get hold of the
	// cache either (i.e. no one can get a cache reference while we're deleting
	// the vnode).. This is, however, not the case for the page daemon. It gets
	// its cache references via the pages it scans, so it can in fact get a
	// vnode reference while we're deleting the vnode.

	if (!vnode->unpublished) {
		if (vnode->remove) {
			FS_CALL(vnode, remove_vnode)(vnode->mount->cookie,
				vnode->private_node, reenter);
		} else {
			FS_CALL(vnode, put_vnode)(vnode->mount->cookie, vnode->private_node,
				reenter);
		}
	}

	// The file system has removed the resources of the vnode now, so we can
	// make it available again (and remove the busy vnode from the hash)
	mutex_lock(&sVnodeMutex);
	hash_remove(sVnodeTable, vnode);
	mutex_unlock(&sVnodeMutex);

	// if we have a vm_cache attached, remove it
	if (vnode->cache)
		vm_cache_release_ref(vnode->cache);

	vnode->cache = NULL;

	remove_vnode_from_mount_list(vnode, vnode->mount);

	free(vnode);
}


/*!	\brief Decrements the reference counter of the given vnode and deletes it,
	if the counter dropped to 0.

	The caller must, of course, own a reference to the vnode to call this
	function.
	The caller must not hold the sVnodeMutex or the sMountMutex.

	\param vnode the vnode.
	\param reenter \c true, if this function is called (indirectly) from within
		   a file system.
	\return \c B_OK, if everything went fine, an error code otherwise.
*/
static status_t
dec_vnode_ref_count(struct vnode *vnode, bool reenter)
{
	mutex_lock(&sVnodeMutex);

	int32 oldRefCount = atomic_add(&vnode->ref_count, -1);

	ASSERT_PRINT(oldRefCount > 0, "vnode %p\n", vnode);

	TRACE(("dec_vnode_ref_count: vnode %p, ref now %ld\n", vnode, vnode->ref_count));

	if (oldRefCount == 1) {
		if (vnode->busy)
			panic("dec_vnode_ref_count: called on busy vnode %p\n", vnode);

		bool freeNode = false;

		// Just insert the vnode into an unused list if we don't need
		// to delete it
		if (vnode->remove) {
			vnode->busy = true;
			freeNode = true;
		} else {
			list_add_item(&sUnusedVnodeList, vnode);
			if (++sUnusedVnodes > kMaxUnusedVnodes
				&& vm_low_memory_state() != B_NO_LOW_MEMORY) {
				// there are too many unused vnodes so we free the oldest one
				// ToDo: evaluate this mechanism
				vnode = (struct vnode *)list_remove_head_item(&sUnusedVnodeList);
				vnode->busy = true;
				freeNode = true;
				sUnusedVnodes--;
			}
		}

		mutex_unlock(&sVnodeMutex);

		if (freeNode)
			free_vnode(vnode, reenter);
	} else
		mutex_unlock(&sVnodeMutex);

	return B_OK;
}


/*!	\brief Increments the reference counter of the given vnode.

	The caller must either already have a reference to the vnode or hold
	the sVnodeMutex.

	\param vnode the vnode.
*/
static void
inc_vnode_ref_count(struct vnode *vnode)
{
	atomic_add(&vnode->ref_count, 1);
	TRACE(("inc_vnode_ref_count: vnode %p, ref now %ld\n", vnode, vnode->ref_count));
}


/*!	\brief Looks up a vnode by mount and node ID in the sVnodeTable.

	The caller must hold the sVnodeMutex.

	\param mountID the mount ID.
	\param vnodeID the node ID.

	\return The vnode structure, if it was found in the hash table, \c NULL
			otherwise.
*/
static struct vnode *
lookup_vnode(dev_t mountID, ino_t vnodeID)
{
	struct vnode_hash_key key;

	key.device = mountID;
	key.vnode = vnodeID;

	return (vnode *)hash_lookup(sVnodeTable, &key);
}


/*!	\brief Retrieves a vnode for a given mount ID, node ID pair.

	If the node is not yet in memory, it will be loaded.

	The caller must not hold the sVnodeMutex or the sMountMutex.

	\param mountID the mount ID.
	\param vnodeID the node ID.
	\param _vnode Pointer to a vnode* variable into which the pointer to the
		   retrieved vnode structure shall be written.
	\param reenter \c true, if this function is called (indirectly) from within
		   a file system.
	\return \c B_OK, if everything when fine, an error code otherwise.
*/
static status_t
get_vnode(dev_t mountID, ino_t vnodeID, struct vnode **_vnode, bool canWait,
	int reenter)
{
	FUNCTION(("get_vnode: mountid %ld vnid 0x%Lx %p\n", mountID, vnodeID, _vnode));

	mutex_lock(&sVnodeMutex);

	int32 tries = 1000;
		// try for 10 secs
restart:
	struct vnode *vnode = lookup_vnode(mountID, vnodeID);
	if (vnode && vnode->busy) {
		mutex_unlock(&sVnodeMutex);
		if (!canWait || --tries < 0) {
			// vnode doesn't seem to become unbusy
			dprintf("vnode %ld:%Ld is not becoming unbusy!\n", mountID, vnodeID);
			return B_BUSY;
		}
		snooze(10000); // 10 ms
		mutex_lock(&sVnodeMutex);
		goto restart;
	}

	TRACE(("get_vnode: tried to lookup vnode, got %p\n", vnode));

	status_t status;

	if (vnode) {
		if (vnode->ref_count == 0) {
			// this vnode has been unused before
			list_remove_item(&sUnusedVnodeList, vnode);
			sUnusedVnodes--;
		}
		inc_vnode_ref_count(vnode);
	} else {
		// we need to create a new vnode and read it in
		status = create_new_vnode(&vnode, mountID, vnodeID);
		if (status < B_OK)
			goto err;

		vnode->busy = true;
		mutex_unlock(&sVnodeMutex);

		status = FS_CALL(vnode, get_vnode)(vnode->mount->cookie, vnodeID,
			&vnode->private_node, reenter);
		if (status == B_OK && vnode->private_node == NULL)
			status = B_BAD_VALUE;

		mutex_lock(&sVnodeMutex);

		if (status < B_OK)
			goto err1;

		vnode->busy = false;
	}

	mutex_unlock(&sVnodeMutex);

	TRACE(("get_vnode: returning %p\n", vnode));

	*_vnode = vnode;
	return B_OK;

err1:
	hash_remove(sVnodeTable, vnode);
	remove_vnode_from_mount_list(vnode, vnode->mount);
err:
	mutex_unlock(&sVnodeMutex);
	if (vnode)
		free(vnode);

	return status;
}


/*!	\brief Decrements the reference counter of the given vnode and deletes it,
	if the counter dropped to 0.

	The caller must, of course, own a reference to the vnode to call this
	function.
	The caller must not hold the sVnodeMutex or the sMountMutex.

	\param vnode the vnode.
*/
static inline void
put_vnode(struct vnode *vnode)
{
	dec_vnode_ref_count(vnode, false);
}


static void
vnode_low_memory_handler(void */*data*/, int32 level)
{
	TRACE(("vnode_low_memory_handler(level = %ld)\n", level));

	uint32 count = 1;
	switch (level) {
		case B_NO_LOW_MEMORY:
			return;
		case B_LOW_MEMORY_NOTE:
			count = sUnusedVnodes / 100;
			break;
		case B_LOW_MEMORY_WARNING:
			count = sUnusedVnodes / 10;
			break;
		case B_LOW_MEMORY_CRITICAL:
			count = sUnusedVnodes;
			break;
	}

	if (count > sUnusedVnodes)
		count = sUnusedVnodes;

	// first, write back the modified pages of some unused vnodes

	uint32 freeCount = count;

	for (uint32 i = 0; i < count; i++) {
		mutex_lock(&sVnodeMutex);
		struct vnode *vnode = (struct vnode *)list_remove_head_item(
			&sUnusedVnodeList);
		if (vnode == NULL) {
			mutex_unlock(&sVnodeMutex);
			break;
		}

		inc_vnode_ref_count(vnode);
		sUnusedVnodes--;

		mutex_unlock(&sVnodeMutex);

		if (vnode->cache != NULL)
			vm_cache_write_modified(vnode->cache, false);

		dec_vnode_ref_count(vnode, false);
	}

	// and then free them

	for (uint32 i = 0; i < freeCount; i++) {
		mutex_lock(&sVnodeMutex);

		// We're removing vnodes from the tail of the list - hoping it's
		// one of those we have just written back; otherwise we'll write
		// back the vnode with the busy flag turned on, and that might
		// take some time.
		struct vnode *vnode = (struct vnode *)list_remove_tail_item(
			&sUnusedVnodeList);
		if (vnode == NULL) {
			mutex_unlock(&sVnodeMutex);
			break;
		}
		TRACE(("  free vnode %ld:%Ld (%p)\n", vnode->device, vnode->id, vnode));

		vnode->busy = true;
		sUnusedVnodes--;

		mutex_unlock(&sVnodeMutex);

		free_vnode(vnode, false);
	}
}


static inline void
put_advisory_locking(struct advisory_locking *locking)
{
	release_sem(locking->lock);
}


/*!	Returns the advisory_locking object of the \a vnode in case it
	has one, and locks it.
	You have to call put_advisory_locking() when you're done with
	it.
	Note, you must not have the vnode mutex locked when calling
	this function.
*/
static struct advisory_locking *
get_advisory_locking(struct vnode *vnode)
{
	mutex_lock(&sVnodeMutex);

	struct advisory_locking *locking = vnode->advisory_locking;
	sem_id lock = locking != NULL ? locking->lock : B_ERROR;

	mutex_unlock(&sVnodeMutex);

	if (lock >= B_OK)
		lock = acquire_sem(lock);
	if (lock < B_OK) {
		// This means the locking has been deleted in the mean time
		// or had never existed in the first place - otherwise, we
		// would get the lock at some point.
		return NULL;
	}

	return locking;
}


/*!	Creates a locked advisory_locking object, and attaches it to the
	given \a vnode.
	Returns B_OK in case of success - also if the vnode got such an
	object from someone else in the mean time, you'll still get this
	one locked then.
*/
static status_t
create_advisory_locking(struct vnode *vnode)
{
	if (vnode == NULL)
		return B_FILE_ERROR;

	struct advisory_locking *locking = new(std::nothrow) advisory_locking;
	if (locking == NULL)
		return B_NO_MEMORY;

	status_t status;

	locking->wait_sem = create_sem(0, "advisory lock");
	if (locking->wait_sem < B_OK) {
		status = locking->wait_sem;
		goto err1;
	}

	locking->lock = create_sem(0, "advisory locking");
	if (locking->lock < B_OK) {
		status = locking->lock;
		goto err2;
	}

	// We need to set the locking structure atomically - someone
	// else might set one at the same time
	do {
		if (atomic_test_and_set((vint32 *)&vnode->advisory_locking,
				(addr_t)locking, (addr_t)NULL) == (addr_t)NULL)
			return B_OK;
	} while (get_advisory_locking(vnode) == NULL);

	status = B_OK;
		// we delete the one we've just created, but nevertheless, the vnode
		// does have a locking structure now

	delete_sem(locking->lock);
err2:
	delete_sem(locking->wait_sem);
err1:
	delete locking;
	return status;
}


/*!	Retrieves the first lock that has been set by the current team.
*/
static status_t
get_advisory_lock(struct vnode *vnode, struct flock *flock)
{
	struct advisory_locking *locking = get_advisory_locking(vnode);
	if (locking == NULL)
		return B_BAD_VALUE;

	// TODO: this should probably get the flock by its file descriptor!
	team_id team = team_get_current_team_id();
	status_t status = B_BAD_VALUE;

	LockList::Iterator iterator = locking->locks.GetIterator();
	while (iterator.HasNext()) {
		struct advisory_lock *lock = iterator.Next();

		if (lock->team == team) {
			flock->l_start = lock->start;
			flock->l_len = lock->end - lock->start + 1;
			status = B_OK;
			break;
		}
	}

	put_advisory_locking(locking);
	return status;
}


/*! Returns \c true when either \a flock is \c NULL or the \a flock intersects
	with the advisory_lock \a lock.
*/
static bool
advisory_lock_intersects(struct advisory_lock *lock, struct flock *flock)
{
	if (flock == NULL)
		return true;

	return lock->start <= flock->l_start - 1 + flock->l_len
		&& lock->end >= flock->l_start;
}


/*!	Removes the specified lock, or all locks of the calling team
	if \a flock is NULL.
*/
static status_t
release_advisory_lock(struct vnode *vnode, struct flock *flock)
{
	FUNCTION(("release_advisory_lock(vnode = %p, flock = %p)\n", vnode, flock));

	struct advisory_locking *locking = get_advisory_locking(vnode);
	if (locking == NULL)
		return B_OK;

	// TODO: use the thread ID instead??
	team_id team = team_get_current_team_id();
	pid_t session = thread_get_current_thread()->team->session_id;

	// find matching lock entries

	LockList::Iterator iterator = locking->locks.GetIterator();
	while (iterator.HasNext()) {
		struct advisory_lock *lock = iterator.Next();
		bool removeLock = false;

		if (lock->session == session)
			removeLock = true;
		else if (lock->team == team && advisory_lock_intersects(lock, flock)) {
			bool endsBeyond = false;
			bool startsBefore = false;
			if (flock != NULL) {
				startsBefore = lock->start < flock->l_start;
				endsBeyond = lock->end > flock->l_start - 1 + flock->l_len;
			}

			if (!startsBefore && !endsBeyond) {
				// lock is completely contained in flock
				removeLock = true;
			} else if (startsBefore && !endsBeyond) {
				// cut the end of the lock
				lock->end = flock->l_start - 1;
			} else if (!startsBefore && endsBeyond) {
				// cut the start of the lock
				lock->start = flock->l_start + flock->l_len;
			} else {
				// divide the lock into two locks
				struct advisory_lock *secondLock = new advisory_lock;
				if (secondLock == NULL) {
					// TODO: we should probably revert the locks we already
					// changed... (ie. allocate upfront)
					put_advisory_locking(locking);
					return B_NO_MEMORY;
				}

				lock->end = flock->l_start - 1;

				secondLock->team = lock->team;
				secondLock->session = lock->session;
				// values must already be normalized when getting here
				secondLock->start = flock->l_start + flock->l_len;
				secondLock->end = lock->end;
				secondLock->shared = lock->shared;

				locking->locks.Add(secondLock);
			}
		}

		if (removeLock) {
			// this lock is no longer used
			iterator.Remove();
			free(lock);
		}
	}

	bool removeLocking = locking->locks.IsEmpty();
	release_sem_etc(locking->wait_sem, 1, B_RELEASE_ALL);

	put_advisory_locking(locking);

	if (removeLocking) {
		// We can remove the whole advisory locking structure; it's no
		// longer used
		locking = get_advisory_locking(vnode);
		if (locking != NULL) {
			// the locking could have been changed in the mean time
			if (locking->locks.IsEmpty()) {
				vnode->advisory_locking = NULL;

				// we've detached the locking from the vnode, so we can
				// safely delete it
				delete_sem(locking->lock);
				delete_sem(locking->wait_sem);
				delete locking;
			} else {
				// the locking is in use again
				release_sem_etc(locking->lock, 1, B_DO_NOT_RESCHEDULE);
			}
		}
	}

	return B_OK;
}


/*!	Acquires an advisory lock for the \a vnode. If \a wait is \c true, it
	will wait for the lock to become available, if there are any collisions
	(it will return B_PERMISSION_DENIED in this case if \a wait is \c false).

	If \a session is -1, POSIX semantics are used for this lock. Otherwise,
	BSD flock() semantics are used, that is, all children can unlock the file
	in question (we even allow parents to remove the lock, though, but that
	seems to be in line to what the BSD's are doing).
*/
static status_t
acquire_advisory_lock(struct vnode *vnode, pid_t session, struct flock *flock,
	bool wait)
{
	FUNCTION(("acquire_advisory_lock(vnode = %p, flock = %p, wait = %s)\n",
		vnode, flock, wait ? "yes" : "no"));

	bool shared = flock->l_type == F_RDLCK;
	status_t status = B_OK;

	// TODO: do deadlock detection!

restart:
	// if this vnode has an advisory_locking structure attached,
	// lock that one and search for any colliding file lock
	struct advisory_locking *locking = get_advisory_locking(vnode);
	team_id team = team_get_current_team_id();
	sem_id waitForLock = -1;

	if (locking != NULL) {
		// test for collisions
		LockList::Iterator iterator = locking->locks.GetIterator();
		while (iterator.HasNext()) {
			struct advisory_lock *lock = iterator.Next();

			// TODO: locks from the same team might be joinable!
			if (lock->team != team && advisory_lock_intersects(lock, flock)) {
				// locks do overlap
				if (!shared || !lock->shared) {
					// we need to wait
					waitForLock = locking->wait_sem;
					break;
				}
			}
		}

		if (waitForLock < B_OK || !wait)
			put_advisory_locking(locking);
	}

	// wait for the lock if we have to, or else return immediately

	if (waitForLock >= B_OK) {
		if (!wait)
			status = session != -1 ? B_WOULD_BLOCK : B_PERMISSION_DENIED;
		else {
			status = switch_sem_etc(locking->lock, waitForLock, 1,
				B_CAN_INTERRUPT, 0);
			if (status == B_OK) {
				// see if we're still colliding
				goto restart;
			}
		}
	}

	if (status < B_OK)
		return status;

	// install new lock

	locking = get_advisory_locking(vnode);
	if (locking == NULL) {
		// we need to create a new locking object
		status = create_advisory_locking(vnode);
		if (status < B_OK)
			return status;

		locking = vnode->advisory_locking;
			// we own the locking object, so it can't go away
	}

	struct advisory_lock *lock = (struct advisory_lock *)malloc(
		sizeof(struct advisory_lock));
	if (lock == NULL) {
		if (waitForLock >= B_OK)
			release_sem_etc(waitForLock, 1, B_RELEASE_ALL);
		release_sem(locking->lock);
		return B_NO_MEMORY;
	}

	lock->team = team_get_current_team_id();
	lock->session = session;
	// values must already be normalized when getting here
	lock->start = flock->l_start;
	lock->end = flock->l_start - 1 + flock->l_len;
	lock->shared = shared;

	locking->locks.Add(lock);
	put_advisory_locking(locking);

	return status;
}


/*!	Normalizes the \a flock structure to make it easier to compare the
	structure with others. The l_start and l_len fields are set to absolute
	values according to the l_whence field.
*/
static status_t
normalize_flock(struct file_descriptor *descriptor, struct flock *flock)
{
	switch (flock->l_whence) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			flock->l_start += descriptor->pos;
			break;
		case SEEK_END:
		{
			struct vnode *vnode = descriptor->u.vnode;
			struct stat stat;
			status_t status;

			if (FS_CALL(vnode, read_stat) == NULL)
				return EOPNOTSUPP;

			status = FS_CALL(vnode, read_stat)(vnode->mount->cookie,
				vnode->private_node, &stat);
			if (status < B_OK)
				return status;

			flock->l_start += stat.st_size;
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	if (flock->l_start < 0)
		flock->l_start = 0;
	if (flock->l_len == 0)
		flock->l_len = OFF_MAX;

	// don't let the offset and length overflow
	if (flock->l_start > 0 && OFF_MAX - flock->l_start < flock->l_len)
		flock->l_len = OFF_MAX - flock->l_start;

	if (flock->l_len < 0) {
		// a negative length reverses the region
		flock->l_start += flock->l_len;
		flock->l_len = -flock->l_len;
	}

	return B_OK;
}


/*!	Disconnects all file descriptors that are associated with the
	\a vnodeToDisconnect, or if this is NULL, all vnodes of the specified
	\a mount object.

	Note, after you've called this function, there might still be ongoing
	accesses - they won't be interrupted if they already happened before.
	However, any subsequent access will fail.

	This is not a cheap function and should be used with care and rarely.
	TODO: there is currently no means to stop a blocking read/write!
*/
void
disconnect_mount_or_vnode_fds(struct fs_mount *mount,
	struct vnode *vnodeToDisconnect)
{
	// iterate over all teams and peek into their file descriptors
	int32 nextTeamID = 0;

	while (true) {
		struct io_context *context = NULL;
		sem_id contextMutex = -1;
		struct team *team = NULL;
		team_id lastTeamID;

		cpu_status state = disable_interrupts();
		GRAB_TEAM_LOCK();

		lastTeamID = peek_next_thread_id();
		if (nextTeamID < lastTeamID) {
			// get next valid team
			while (nextTeamID < lastTeamID
				&& !(team = team_get_team_struct_locked(nextTeamID))) {
				nextTeamID++;
			}

			if (team) {
				context = (io_context *)team->io_context;
				contextMutex = context->io_mutex.sem;
				nextTeamID++;
			}
		}

		RELEASE_TEAM_LOCK();
		restore_interrupts(state);

		if (context == NULL)
			break;

		// we now have a context - since we couldn't lock it while having
		// safe access to the team structure, we now need to lock the mutex
		// manually

		if (acquire_sem(contextMutex) != B_OK) {
			// team seems to be gone, go over to the next team
			continue;
		}

		// the team cannot be deleted completely while we're owning its
		// io_context mutex, so we can safely play with it now

		context->io_mutex.holder = thread_get_current_thread_id();

		if (context->cwd != NULL && context->cwd->mount == mount
			&& (vnodeToDisconnect == NULL
				|| vnodeToDisconnect == context->cwd)) {
			put_vnode(context->cwd);
				// Note: We're only accessing the pointer, not the vnode itself
				// in the lines below.

			if (context->cwd == mount->root_vnode) {
				// redirect the current working directory to the covered vnode
				context->cwd = mount->covers_vnode;
				inc_vnode_ref_count(context->cwd);
			} else
				context->cwd = NULL;
		}

		for (uint32 i = 0; i < context->table_size; i++) {
			if (struct file_descriptor *descriptor = context->fds[i]) {
				inc_fd_ref_count(descriptor);

				// if this descriptor points at this mount, we
				// need to disconnect it to be able to unmount
				struct vnode *vnode = fd_vnode(descriptor);
				if (vnodeToDisconnect != NULL) {
					if (vnode == vnodeToDisconnect)
						disconnect_fd(descriptor);
				} else if (vnode != NULL && vnode->mount == mount
					|| vnode == NULL && descriptor->u.mount == mount)
					disconnect_fd(descriptor);

				put_fd(descriptor);
			}
		}

		mutex_unlock(&context->io_mutex);
	}
}


/*!	\brief Resolves a mount point vnode to the volume root vnode it is covered
		   by.

	Given an arbitrary vnode, the function checks, whether the node is covered
	by the root of a volume. If it is the function obtains a reference to the
	volume root node and returns it.

	\param vnode The vnode in question.
	\return The volume root vnode the vnode cover is covered by, if it is
			indeed a mount point, or \c NULL otherwise.
*/
static struct vnode *
resolve_mount_point_to_volume_root(struct vnode *vnode)
{
	if (!vnode)
		return NULL;

	struct vnode *volumeRoot = NULL;

	mutex_lock(&sVnodeCoveredByMutex);
	if (vnode->covered_by) {
		volumeRoot = vnode->covered_by;
		inc_vnode_ref_count(volumeRoot);
	}
	mutex_unlock(&sVnodeCoveredByMutex);

	return volumeRoot;
}


/*!	\brief Resolves a mount point vnode to the volume root vnode it is covered
		   by.

	Given an arbitrary vnode (identified by mount and node ID), the function
	checks, whether the node is covered by the root of a volume. If it is the
	function returns the mount and node ID of the volume root node. Otherwise
	it simply returns the supplied mount and node ID.

	In case of error (e.g. the supplied node could not be found) the variables
	for storing the resolved mount and node ID remain untouched and an error
	code is returned.

	\param mountID The mount ID of the vnode in question.
	\param nodeID The node ID of the vnode in question.
	\param resolvedMountID Pointer to storage for the resolved mount ID.
	\param resolvedNodeID Pointer to storage for the resolved node ID.
	\return
	- \c B_OK, if everything went fine,
	- another error code, if something went wrong.
*/
status_t
resolve_mount_point_to_volume_root(dev_t mountID, ino_t nodeID,
	dev_t *resolvedMountID, ino_t *resolvedNodeID)
{
	// get the node
	struct vnode *node;
	status_t error = get_vnode(mountID, nodeID, &node, true, false);
	if (error != B_OK)
		return error;

	// resolve the node
	struct vnode *resolvedNode = resolve_mount_point_to_volume_root(node);
	if (resolvedNode) {
		put_vnode(node);
		node = resolvedNode;
	}

	// set the return values
	*resolvedMountID = node->device;
	*resolvedNodeID = node->id;

	put_vnode(node);

	return B_OK;
}


/*!	\brief Resolves a volume root vnode to the underlying mount point vnode.

	Given an arbitrary vnode, the function checks, whether the node is the
	root of a volume. If it is (and if it is not "/"), the function obtains
	a reference to the underlying mount point node and returns it.

	\param vnode The vnode in question (caller must have a reference).
	\return The mount point vnode the vnode covers, if it is indeed a volume
			root and not "/", or \c NULL otherwise.
*/
static struct vnode *
resolve_volume_root_to_mount_point(struct vnode *vnode)
{
	if (!vnode)
		return NULL;

	struct vnode *mountPoint = NULL;

	struct fs_mount *mount = vnode->mount;
	if (vnode == mount->root_vnode && mount->covers_vnode) {
		mountPoint = mount->covers_vnode;
		inc_vnode_ref_count(mountPoint);
	}

	return mountPoint;
}


/*!	\brief Gets the directory path and leaf name for a given path.

	The supplied \a path is transformed to refer to the directory part of
	the entry identified by the original path, and into the buffer \a filename
	the leaf name of the original entry is written.
	Neither the returned path nor the leaf name can be expected to be
	canonical.

	\param path The path to be analyzed. Must be able to store at least one
		   additional character.
	\param filename The buffer into which the leaf name will be written.
		   Must be of size B_FILE_NAME_LENGTH at least.
	\return \c B_OK, if everything went fine, \c B_NAME_TOO_LONG, if the leaf
		   name is longer than \c B_FILE_NAME_LENGTH, or \c B_ENTRY_NOT_FOUND,
		   if the given path name is empty.
*/
static status_t
get_dir_path_and_leaf(char *path, char *filename)
{
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;

	char *p = strrchr(path, '/');
		// '/' are not allowed in file names!

	FUNCTION(("get_dir_path_and_leaf(path = %s)\n", path));

	if (!p) {
		// this path is single segment with no '/' in it
		// ex. "foo"
		if (strlcpy(filename, path, B_FILE_NAME_LENGTH) >= B_FILE_NAME_LENGTH)
			return B_NAME_TOO_LONG;
		strcpy(path, ".");
	} else {
		p++;
		if (*p == '\0') {
			// special case: the path ends in '/'
			strcpy(filename, ".");
		} else {
			// normal leaf: replace the leaf portion of the path with a '.'
			if (strlcpy(filename, p, B_FILE_NAME_LENGTH)
				>= B_FILE_NAME_LENGTH) {
				return B_NAME_TOO_LONG;
			}
		}
		p[0] = '.';
		p[1] = '\0';
	}
	return B_OK;
}


static status_t
entry_ref_to_vnode(dev_t mountID, ino_t directoryID, const char *name,
	bool traverse, struct vnode **_vnode)
{
	char clonedName[B_FILE_NAME_LENGTH + 1];
	if (strlcpy(clonedName, name, B_FILE_NAME_LENGTH) >= B_FILE_NAME_LENGTH)
		return B_NAME_TOO_LONG;

	// get the directory vnode and let vnode_path_to_vnode() do the rest
	struct vnode *directory;

	status_t status = get_vnode(mountID, directoryID, &directory, true, false);
	if (status < 0)
		return status;

	return vnode_path_to_vnode(directory, clonedName, traverse, 0, _vnode, NULL,
		NULL);
}


/*!	Returns the vnode for the relative path starting at the specified \a vnode.
	\a path must not be NULL.
	If it returns successfully, \a path contains the name of the last path
	component. This function clobbers the buffer pointed to by \a path only
	if it does contain more than one component.
	Note, this reduces the ref_count of the starting \a vnode, no matter if
	it is successful or not!
*/
static status_t
vnode_path_to_vnode(struct vnode *vnode, char *path, bool traverseLeafLink,
	int count, struct vnode **_vnode, ino_t *_parentID, int *_type)
{
	status_t status = 0;
	ino_t lastParentID = vnode->id;
	int type = 0;

	FUNCTION(("vnode_path_to_vnode(vnode = %p, path = %s)\n", vnode, path));

	if (path == NULL) {
		put_vnode(vnode);
		return B_BAD_VALUE;
	}

	if (*path == '\0') {
		put_vnode(vnode);
		return B_ENTRY_NOT_FOUND;
	}

	while (true) {
		struct vnode *nextVnode;
		ino_t vnodeID;
		char *nextPath;

		TRACE(("vnode_path_to_vnode: top of loop. p = %p, p = '%s'\n", path, path));

		// done?
		if (path[0] == '\0')
			break;

		// walk to find the next path component ("path" will point to a single
		// path component), and filter out multiple slashes
		for (nextPath = path + 1; *nextPath != '\0' && *nextPath != '/'; nextPath++);

		if (*nextPath == '/') {
			*nextPath = '\0';
			do
				nextPath++;
			while (*nextPath == '/');
		}

		// See if the '..' is at the root of a mount and move to the covered
		// vnode so we pass the '..' path to the underlying filesystem
		if (!strcmp("..", path)
			&& vnode->mount->root_vnode == vnode
			&& vnode->mount->covers_vnode) {
			nextVnode = vnode->mount->covers_vnode;
			inc_vnode_ref_count(nextVnode);
			put_vnode(vnode);
			vnode = nextVnode;
		}

		// Check if we have the right to search the current directory vnode.
		// If a file system doesn't have the access() function, we assume that
		// searching a directory is always allowed
		if (FS_CALL(vnode, access))
			status = FS_CALL(vnode, access)(vnode->mount->cookie, vnode->private_node, X_OK);

		// Tell the filesystem to get the vnode of this path component (if we got the
		// permission from the call above)
		if (status >= B_OK)
			status = FS_CALL(vnode, lookup)(vnode->mount->cookie, vnode->private_node, path, &vnodeID, &type);

		if (status < B_OK) {
			put_vnode(vnode);
			return status;
		}

		// Lookup the vnode, the call to fs_lookup should have caused a get_vnode to be called
		// from inside the filesystem, thus the vnode would have to be in the list and it's
		// ref count incremented at this point
		mutex_lock(&sVnodeMutex);
		nextVnode = lookup_vnode(vnode->device, vnodeID);
		mutex_unlock(&sVnodeMutex);

		if (!nextVnode) {
			// pretty screwed up here - the file system found the vnode, but the hash
			// lookup failed, so our internal structures are messed up
			panic("vnode_path_to_vnode: could not lookup vnode (mountid 0x%lx vnid 0x%Lx)\n",
				vnode->device, vnodeID);
			put_vnode(vnode);
			return B_ENTRY_NOT_FOUND;
		}

		// If the new node is a symbolic link, resolve it (if we've been told to do it)
		if (S_ISLNK(type) && !(!traverseLeafLink && nextPath[0] == '\0')) {
			size_t bufferSize;
			char *buffer;

			TRACE(("traverse link\n"));

			// it's not exactly nice style using goto in this way, but hey, it works :-/
			if (count + 1 > B_MAX_SYMLINKS) {
				status = B_LINK_LIMIT;
				goto resolve_link_error;
			}

			buffer = (char *)malloc(bufferSize = B_PATH_NAME_LENGTH);
			if (buffer == NULL) {
				status = B_NO_MEMORY;
				goto resolve_link_error;
			}

			if (FS_CALL(nextVnode, read_symlink) != NULL) {
				bufferSize--;
				status = FS_CALL(nextVnode, read_symlink)(
					nextVnode->mount->cookie, nextVnode->private_node, buffer,
					&bufferSize);
				// null-terminate
				if (status >= 0)
					buffer[bufferSize] = '\0';
			} else
				status = B_BAD_VALUE;

			if (status < B_OK) {
				free(buffer);

		resolve_link_error:
				put_vnode(vnode);
				put_vnode(nextVnode);

				return status;
			}
			put_vnode(nextVnode);

			// Check if we start from the root directory or the current
			// directory ("vnode" still points to that one).
			// Cut off all leading slashes if it's the root directory
			path = buffer;
			bool absoluteSymlink = false;
			if (path[0] == '/') {
				// we don't need the old directory anymore
				put_vnode(vnode);

				while (*++path == '/')
					;
				vnode = sRoot;
				inc_vnode_ref_count(vnode);

				absoluteSymlink = true;
			}

			inc_vnode_ref_count(vnode);
				// balance the next recursion - we will decrement the
				// ref_count of the vnode, no matter if we succeeded or not

			if (absoluteSymlink && *path == '\0') {
				// symlink was just "/"
				nextVnode = vnode;
			} else {
				status = vnode_path_to_vnode(vnode, path, traverseLeafLink,
					count + 1, &nextVnode, &lastParentID, _type);
			}

			free(buffer);

			if (status < B_OK) {
				put_vnode(vnode);
				return status;
			}
		} else
			lastParentID = vnode->id;

		// decrease the ref count on the old dir we just looked up into
		put_vnode(vnode);

		path = nextPath;
		vnode = nextVnode;

		// see if we hit a mount point
		struct vnode *mountPoint = resolve_mount_point_to_volume_root(vnode);
		if (mountPoint) {
			put_vnode(vnode);
			vnode = mountPoint;
		}
	}

	*_vnode = vnode;
	if (_type)
		*_type = type;
	if (_parentID)
		*_parentID = lastParentID;

	return B_OK;
}


static status_t
path_to_vnode(char *path, bool traverseLink, struct vnode **_vnode,
	ino_t *_parentID, bool kernel)
{
	struct vnode *start = NULL;

	FUNCTION(("path_to_vnode(path = \"%s\")\n", path));

	if (!path)
		return B_BAD_VALUE;

	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;

	// figure out if we need to start at root or at cwd
	if (*path == '/') {
		if (sRoot == NULL) {
			// we're a bit early, aren't we?
			return B_ERROR;
		}

		while (*++path == '/')
			;
		start = sRoot;
		inc_vnode_ref_count(start);

		if (*path == '\0') {
			*_vnode = start;
			return B_OK;
		}

	} else {
		struct io_context *context = get_current_io_context(kernel);

		mutex_lock(&context->io_mutex);
		start = context->cwd;
		if (start != NULL)
			inc_vnode_ref_count(start);
		mutex_unlock(&context->io_mutex);

		if (start == NULL)
			return B_ERROR;
	}

	return vnode_path_to_vnode(start, path, traverseLink, 0, _vnode, _parentID, NULL);
}


/*! Returns the vnode in the next to last segment of the path, and returns
	the last portion in filename.
	The path buffer must be able to store at least one additional character.
*/
static status_t
path_to_dir_vnode(char *path, struct vnode **_vnode, char *filename, bool kernel)
{
	status_t status = get_dir_path_and_leaf(path, filename);
	if (status != B_OK)
		return status;

	return path_to_vnode(path, true, _vnode, NULL, kernel);
}


/*!	\brief Retrieves the directory vnode and the leaf name of an entry referred
		   to by a FD + path pair.

	\a path must be given in either case. \a fd might be omitted, in which
	case \a path is either an absolute path or one relative to the current
	directory. If both a supplied and \a path is relative it is reckoned off
	of the directory referred to by \a fd. If \a path is absolute \a fd is
	ignored.

	The caller has the responsibility to call put_vnode() on the returned
	directory vnode.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL. The buffer
	       is modified by this function. It must have at least room for a
	       string one character longer than the path it contains.
	\param _vnode A pointer to a variable the directory vnode shall be written
		   into.
	\param filename A buffer of size B_FILE_NAME_LENGTH or larger into which
		   the leaf name of the specified entry will be written.
	\param kernel \c true, if invoked from inside the kernel, \c false if
		   invoked from userland.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
static status_t
fd_and_path_to_dir_vnode(int fd, char *path, struct vnode **_vnode,
	char *filename, bool kernel)
{
	if (!path)
		return B_BAD_VALUE;
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;
	if (fd < 0)
		return path_to_dir_vnode(path, _vnode, filename, kernel);

	status_t status = get_dir_path_and_leaf(path, filename);
	if (status != B_OK)
		return status;

	return fd_and_path_to_vnode(fd, path, true, _vnode, NULL, kernel);
}


/*!	\brief Retrieves the directory vnode and the leaf name of an entry referred
		   to by a vnode + path pair.

	\a path must be given in either case. \a vnode might be omitted, in which
	case \a path is either an absolute path or one relative to the current
	directory. If both a supplied and \a path is relative it is reckoned off
	of the directory referred to by \a vnode. If \a path is absolute \a vnode is
	ignored.

	The caller has the responsibility to call put_vnode() on the returned
	directory vnode.

	\param vnode The vnode. May be \c NULL.
	\param path The absolute or relative path. Must not be \c NULL. The buffer
	       is modified by this function. It must have at least room for a
	       string one character longer than the path it contains.
	\param _vnode A pointer to a variable the directory vnode shall be written
		   into.
	\param filename A buffer of size B_FILE_NAME_LENGTH or larger into which
		   the leaf name of the specified entry will be written.
	\param kernel \c true, if invoked from inside the kernel, \c false if
		   invoked from userland.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
static status_t
vnode_and_path_to_dir_vnode(struct vnode* vnode, char *path,
	struct vnode **_vnode, char *filename, bool kernel)
{
	if (!path)
		return B_BAD_VALUE;
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;
	if (vnode == NULL || path[0] == '/')
		return path_to_dir_vnode(path, _vnode, filename, kernel);

	status_t status = get_dir_path_and_leaf(path, filename);
	if (status != B_OK)
		return status;

	inc_vnode_ref_count(vnode);
		// vnode_path_to_vnode() always decrements the ref count

	return vnode_path_to_vnode(vnode, path, true, 0, _vnode, NULL, NULL);
}


/*! Returns a vnode's name in the d_name field of a supplied dirent buffer.
*/
static status_t
get_vnode_name(struct vnode *vnode, struct vnode *parent, struct dirent *buffer,
	size_t bufferSize)
{
	if (bufferSize < sizeof(struct dirent))
		return B_BAD_VALUE;

	// See if vnode is the root of a mount and move to the covered
	// vnode so we get the underlying file system
	VNodePutter vnodePutter;
	if (vnode->mount->root_vnode == vnode && vnode->mount->covers_vnode != NULL) {
		vnode = vnode->mount->covers_vnode;
		inc_vnode_ref_count(vnode);
		vnodePutter.SetTo(vnode);
	}

	if (FS_CALL(vnode, get_vnode_name)) {
		// The FS supports getting the name of a vnode.
		return FS_CALL(vnode, get_vnode_name)(vnode->mount->cookie,
			vnode->private_node, buffer->d_name,
			(char*)buffer + bufferSize - buffer->d_name);
	}

	// The FS doesn't support getting the name of a vnode. So we search the
	// parent directory for the vnode, if the caller let us.

	if (parent == NULL)
		return EOPNOTSUPP;

	fs_cookie cookie;

	status_t status = FS_CALL(parent, open_dir)(parent->mount->cookie,
		parent->private_node, &cookie);
	if (status >= B_OK) {
		while (true) {
			uint32 num = 1;
			status = dir_read(parent, cookie, buffer, bufferSize, &num);
			if (status < B_OK)
				break;
			if (num == 0) {
				status = B_ENTRY_NOT_FOUND;
				break;
			}

			if (vnode->id == buffer->d_ino) {
				// found correct entry!
				break;
			}
		}

		FS_CALL(vnode, close_dir)(vnode->mount->cookie, vnode->private_node,
			cookie);
		FS_CALL(vnode, free_dir_cookie)(vnode->mount->cookie,
			vnode->private_node, cookie);
	}
	return status;
}


static status_t
get_vnode_name(struct vnode *vnode, struct vnode *parent, char *name,
	size_t nameSize)
{
	char buffer[sizeof(struct dirent) + B_FILE_NAME_LENGTH];
	struct dirent *dirent = (struct dirent *)buffer;

	status_t status = get_vnode_name(vnode, parent, buffer, sizeof(buffer));
	if (status != B_OK)
		return status;

	if (strlcpy(name, dirent->d_name, nameSize) >= nameSize)
		return B_BUFFER_OVERFLOW;

	return B_OK;
}


/*!	Gets the full path to a given directory vnode.
	It uses the fs_get_vnode_name() call to get the name of a vnode; if a
	file system doesn't support this call, it will fall back to iterating
	through the parent directory to get the name of the child.

	To protect against circular loops, it supports a maximum tree depth
	of 256 levels.

	Note that the path may not be correct the time this function returns!
	It doesn't use any locking to prevent returning the correct path, as
	paths aren't safe anyway: the path to a file can change at any time.

	It might be a good idea, though, to check if the returned path exists
	in the calling function (it's not done here because of efficiency)
*/
static status_t
dir_vnode_to_path(struct vnode *vnode, char *buffer, size_t bufferSize)
{
	FUNCTION(("dir_vnode_to_path(%p, %p, %lu)\n", vnode, buffer, bufferSize));

	if (vnode == NULL || buffer == NULL)
		return B_BAD_VALUE;

	/* this implementation is currently bound to B_PATH_NAME_LENGTH */
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();
	int32 insert = pathBuffer.BufferSize();
	int32 maxLevel = 256;
	int32 length;
	status_t status;

	// we don't use get_vnode() here because this call is more
	// efficient and does all we need from get_vnode()
	inc_vnode_ref_count(vnode);

	// resolve a volume root to its mount point
	struct vnode *mountPoint = resolve_volume_root_to_mount_point(vnode);
	if (mountPoint) {
		put_vnode(vnode);
		vnode = mountPoint;
	}

	path[--insert] = '\0';

	while (true) {
		// the name buffer is also used for fs_read_dir()
		char nameBuffer[sizeof(struct dirent) + B_FILE_NAME_LENGTH];
		char *name = &((struct dirent *)nameBuffer)->d_name[0];
		struct vnode *parentVnode;
		ino_t parentID;
		int type;

		// lookup the parent vnode
		status = FS_CALL(vnode, lookup)(vnode->mount->cookie, vnode->private_node, "..",
			&parentID, &type);
		if (status < B_OK)
			goto out;

		mutex_lock(&sVnodeMutex);
		parentVnode = lookup_vnode(vnode->device, parentID);
		mutex_unlock(&sVnodeMutex);

		if (parentVnode == NULL) {
			panic("dir_vnode_to_path: could not lookup vnode (mountid 0x%lx vnid 0x%Lx)\n",
				vnode->device, parentID);
			status = B_ENTRY_NOT_FOUND;
			goto out;
		}

		// get the node's name
		status = get_vnode_name(vnode, parentVnode, (struct dirent*)nameBuffer,
			sizeof(nameBuffer));

		// resolve a volume root to its mount point
		mountPoint = resolve_volume_root_to_mount_point(parentVnode);
		if (mountPoint) {
			put_vnode(parentVnode);
			parentVnode = mountPoint;
			parentID = parentVnode->id;
		}

		bool hitRoot = (parentVnode == vnode);

		// release the current vnode, we only need its parent from now on
		put_vnode(vnode);
		vnode = parentVnode;

		if (status < B_OK)
			goto out;

		if (hitRoot) {
			// we have reached "/", which means we have constructed the full
			// path
			break;
		}

		// ToDo: add an explicit check for loops in about 10 levels to do
		// real loop detection

		// don't go deeper as 'maxLevel' to prevent circular loops
		if (maxLevel-- < 0) {
			status = ELOOP;
			goto out;
		}

		// add the name in front of the current path
		name[B_FILE_NAME_LENGTH - 1] = '\0';
		length = strlen(name);
		insert -= length;
		if (insert <= 0) {
			status = ENOBUFS;
			goto out;
		}
		memcpy(path + insert, name, length);
		path[--insert] = '/';
	}

	// the root dir will result in an empty path: fix it
	if (path[insert] == '\0')
		path[--insert] = '/';

	TRACE(("  path is: %s\n", path + insert));

	// copy the path to the output buffer
	length = pathBuffer.BufferSize() - insert;
	if (length <= (int)bufferSize)
		memcpy(buffer, path + insert, length);
	else
		status = ENOBUFS;

out:
	put_vnode(vnode);
	return status;
}


/*!	Checks the length of every path component, and adds a '.'
	if the path ends in a slash.
	The given path buffer must be able to store at least one
	additional character.
*/
static status_t
check_path(char *to)
{
	int32 length = 0;

	// check length of every path component

	while (*to) {
		char *begin;
		if (*to == '/')
			to++, length++;

		begin = to;
		while (*to != '/' && *to)
			to++, length++;

		if (to - begin > B_FILE_NAME_LENGTH)
			return B_NAME_TOO_LONG;
	}

	if (length == 0)
		return B_ENTRY_NOT_FOUND;

	// complete path if there is a slash at the end

	if (*(to - 1) == '/') {
		if (length > B_PATH_NAME_LENGTH - 2)
			return B_NAME_TOO_LONG;

		to[0] = '.';
		to[1] = '\0';
	}

	return B_OK;
}


static struct file_descriptor *
get_fd_and_vnode(int fd, struct vnode **_vnode, bool kernel)
{
	struct file_descriptor *descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return NULL;

	if (fd_vnode(descriptor) == NULL) {
		put_fd(descriptor);
		return NULL;
	}

	// ToDo: when we can close a file descriptor at any point, investigate
	//	if this is still valid to do (accessing the vnode without ref_count
	//	or locking)
	*_vnode = descriptor->u.vnode;
	return descriptor;
}


static struct vnode *
get_vnode_from_fd(int fd, bool kernel)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return NULL;

	vnode = fd_vnode(descriptor);
	if (vnode != NULL)
		inc_vnode_ref_count(vnode);

	put_fd(descriptor);
	return vnode;
}


/*!	Gets the vnode from an FD + path combination. If \a fd is lower than zero,
	only the path will be considered. In this case, the \a path must not be
	NULL.
	If \a fd is a valid file descriptor, \a path may be NULL for directories,
	and should be NULL for files.
*/
static status_t
fd_and_path_to_vnode(int fd, char *path, bool traverseLeafLink,
	struct vnode **_vnode, ino_t *_parentID, bool kernel)
{
	if (fd < 0 && !path)
		return B_BAD_VALUE;

	if (path != NULL && *path == '\0')
		return B_ENTRY_NOT_FOUND;

	if (fd < 0 || (path != NULL && path[0] == '/')) {
		// no FD or absolute path
		return path_to_vnode(path, traverseLeafLink, _vnode, _parentID, kernel);
	}

	// FD only, or FD + relative path
	struct vnode *vnode = get_vnode_from_fd(fd, kernel);
	if (!vnode)
		return B_FILE_ERROR;

	if (path != NULL) {
		return vnode_path_to_vnode(vnode, path, traverseLeafLink, 0,
			_vnode, _parentID, NULL);
	}

	// there is no relative path to take into account

	*_vnode = vnode;
	if (_parentID)
		*_parentID = -1;

	return B_OK;
}


static int
get_new_fd(int type, struct fs_mount *mount, struct vnode *vnode,
	fs_cookie cookie, int openMode, bool kernel)
{
	struct file_descriptor *descriptor;
	int fd;

	// if the vnode is locked, we don't allow creating a new file descriptor for it
	if (vnode && vnode->mandatory_locked_by != NULL)
		return B_BUSY;

	descriptor = alloc_fd();
	if (!descriptor)
		return B_NO_MEMORY;

	if (vnode)
		descriptor->u.vnode = vnode;
	else
		descriptor->u.mount = mount;
	descriptor->cookie = cookie;

	switch (type) {
		// vnode types
		case FDTYPE_FILE:
			descriptor->ops = &sFileOps;
			break;
		case FDTYPE_DIR:
			descriptor->ops = &sDirectoryOps;
			break;
		case FDTYPE_ATTR:
			descriptor->ops = &sAttributeOps;
			break;
		case FDTYPE_ATTR_DIR:
			descriptor->ops = &sAttributeDirectoryOps;
			break;

		// mount types
		case FDTYPE_INDEX_DIR:
			descriptor->ops = &sIndexDirectoryOps;
			break;
		case FDTYPE_QUERY:
			descriptor->ops = &sQueryOps;
			break;

		default:
			panic("get_new_fd() called with unknown type %d\n", type);
			break;
	}
	descriptor->type = type;
	descriptor->open_mode = openMode;

	fd = new_fd(get_current_io_context(kernel), descriptor);
	if (fd < 0) {
		free(descriptor);
		return B_NO_MORE_FDS;
	}

	return fd;
}

#ifdef ADD_DEBUGGER_COMMANDS


static void
_dump_advisory_locking(advisory_locking *locking)
{
	if (locking == NULL)
		return;

	kprintf("   lock:        %ld", locking->lock);
	kprintf("   wait_sem:    %ld", locking->wait_sem);

	int32 index = 0;
	LockList::Iterator iterator = locking->locks.GetIterator();
	while (iterator.HasNext()) {
		struct advisory_lock *lock = iterator.Next();

		kprintf("   [%2ld] team:   %ld\n", index++, lock->team);
		kprintf("        start:  %Ld\n", lock->start);
		kprintf("        end:    %Ld\n", lock->end);
		kprintf("        shared? %s\n", lock->shared ? "yes" : "no");
	}
}


static void
_dump_mount(struct fs_mount *mount)
{
	kprintf("MOUNT: %p\n", mount);
	kprintf(" id:            %ld\n", mount->id);
	kprintf(" device_name:   %s\n", mount->device_name);
	kprintf(" fs_name:       %s\n", mount->fs_name);
	kprintf(" cookie:        %p\n", mount->cookie);
	kprintf(" root_vnode:    %p\n", mount->root_vnode);
	kprintf(" covers_vnode:  %p\n", mount->covers_vnode);
	kprintf(" partition:     %p\n", mount->partition);
	kprintf(" lock:          %ld\n", mount->rlock.sem);
	kprintf(" flags:        %s%s\n", mount->unmounting ? " unmounting" : "",
		mount->owns_file_device ? " owns_file_device" : "");

	set_debug_variable("_cookie", (addr_t)mount->cookie);
	set_debug_variable("_root", (addr_t)mount->root_vnode);
	set_debug_variable("_covers", (addr_t)mount->covers_vnode);
	set_debug_variable("_partition", (addr_t)mount->partition);
}


static void
_dump_vnode(struct vnode *vnode)
{
	kprintf("VNODE: %p\n", vnode);
	kprintf(" device:        %ld\n", vnode->device);
	kprintf(" id:            %Ld\n", vnode->id);
	kprintf(" ref_count:     %ld\n", vnode->ref_count);
	kprintf(" private_node:  %p\n", vnode->private_node);
	kprintf(" mount:         %p\n", vnode->mount);
	kprintf(" covered_by:    %p\n", vnode->covered_by);
	kprintf(" cache:         %p\n", vnode->cache);
	kprintf(" flags:         %s%s%s\n", vnode->remove ? "r" : "-",
		vnode->busy ? "b" : "-", vnode->unpublished ? "u" : "-");
	kprintf(" advisory_lock: %p\n", vnode->advisory_locking);

	_dump_advisory_locking(vnode->advisory_locking);

	set_debug_variable("_node", (addr_t)vnode->private_node);
	set_debug_variable("_mount", (addr_t)vnode->mount);
	set_debug_variable("_covered_by", (addr_t)vnode->covered_by);
	set_debug_variable("_adv_lock", (addr_t)vnode->advisory_locking);
}


static int
dump_mount(int argc, char **argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [id|address]\n", argv[0]);
		return 0;
	}

	uint32 id = parse_expression(argv[1]);
	struct fs_mount *mount = NULL;

	mount = (fs_mount *)hash_lookup(sMountsTable, (void *)&id);
	if (mount == NULL) {
		if (IS_USER_ADDRESS(id)) {
			kprintf("fs_mount not found\n");
			return 0;
		}
		mount = (fs_mount *)id;
	}

	_dump_mount(mount);
	return 0;
}


static int
dump_mounts(int argc, char **argv)
{
	if (argc != 1) {
		kprintf("usage: %s\n", argv[0]);
		return 0;
	}

	kprintf("address     id root       covers     cookie     fs_name\n");

	struct hash_iterator iterator;
	struct fs_mount *mount;

	hash_open(sMountsTable, &iterator);
	while ((mount = (struct fs_mount *)hash_next(sMountsTable, &iterator)) != NULL) {
		kprintf("%p%4ld %p %p %p %s\n", mount, mount->id, mount->root_vnode,
			mount->covers_vnode, mount->cookie, mount->fs_name);
	}

	hash_close(sMountsTable, &iterator, false);
	return 0;
}


static int
dump_vnode(int argc, char **argv)
{
	if (argc < 2 || argc > 3 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <device> <id>\n"
			"   or: %s <address>\n", argv[0], argv[0]);
		return 0;
	}

	struct vnode *vnode = NULL;

	if (argc == 2) {
		vnode = (struct vnode *)parse_expression(argv[1]);
		if (IS_USER_ADDRESS(vnode)) {
			kprintf("invalid vnode address\n");
			return 0;
		}
		_dump_vnode(vnode);
		return 0;
	}

	struct hash_iterator iterator;
	dev_t device = parse_expression(argv[1]);
	ino_t id = atoll(argv[2]);

	hash_open(sVnodeTable, &iterator);
	while ((vnode = (struct vnode *)hash_next(sVnodeTable, &iterator)) != NULL) {
		if (vnode->id != id || vnode->device != device)
			continue;

		_dump_vnode(vnode);
	}

	hash_close(sVnodeTable, &iterator, false);
	return 0;
}


static int
dump_vnodes(int argc, char **argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [device]\n", argv[0]);
		return 0;
	}

	// restrict dumped nodes to a certain device if requested
	dev_t device = parse_expression(argv[1]);

	struct hash_iterator iterator;
	struct vnode *vnode;

	kprintf("address    dev     inode  ref cache      fs-node    locking    "
		"flags\n");

	hash_open(sVnodeTable, &iterator);
	while ((vnode = (struct vnode *)hash_next(sVnodeTable, &iterator)) != NULL) {
		if (vnode->device != device)
			continue;

		kprintf("%p%4ld%10Ld%5ld %p %p %p %s%s%s\n", vnode, vnode->device,
			vnode->id, vnode->ref_count, vnode->cache, vnode->private_node,
			vnode->advisory_locking, vnode->remove ? "r" : "-",
			vnode->busy ? "b" : "-", vnode->unpublished ? "u" : "-");
	}

	hash_close(sVnodeTable, &iterator, false);
	return 0;
}


static int
dump_vnode_caches(int argc, char **argv)
{
	struct hash_iterator iterator;
	struct vnode *vnode;

	if (argc > 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [device]\n", argv[0]);
		return 0;
	}

	// restrict dumped nodes to a certain device if requested
	dev_t device = -1;
	if (argc > 1)
		device = atoi(argv[1]);

	kprintf("address    dev     inode cache          size   pages\n");

	hash_open(sVnodeTable, &iterator);
	while ((vnode = (struct vnode *)hash_next(sVnodeTable, &iterator)) != NULL) {
		if (vnode->cache == NULL)
			continue;
		if (device != -1 && vnode->device != device)
			continue;

		// count pages in cache
		size_t numPages = 0;
		for (struct vm_page *page = vnode->cache->page_list;
				page != NULL; page = page->cache_next) {
			numPages++;
		}

		kprintf("%p%4ld%10Ld %p %8Ld%8ld\n", vnode, vnode->device, vnode->id,
			vnode->cache, (vnode->cache->virtual_size + B_PAGE_SIZE - 1)
				/ B_PAGE_SIZE, numPages);
	}

	hash_close(sVnodeTable, &iterator, false);
	return 0;
}


int
dump_io_context(int argc, char **argv)
{
	if (argc > 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [team-id|address]\n", argv[0]);
		return 0;
	}

	struct io_context *context = NULL;

	if (argc > 1) {
		uint32 num = parse_expression(argv[1]);
		if (IS_KERNEL_ADDRESS(num))
			context = (struct io_context *)num;
		else {
			struct team *team = team_get_team_struct_locked(num);
			if (team == NULL) {
				kprintf("could not find team with ID %ld\n", num);
				return 0;
			}
			context = (struct io_context *)team->io_context;
		}
	} else
		context = get_current_io_context(true);

	kprintf("I/O CONTEXT: %p\n", context);
	kprintf(" cwd vnode:\t%p\n", context->cwd);
	kprintf(" used fds:\t%lu\n", context->num_used_fds);
	kprintf(" max fds:\t%lu\n", context->table_size);

	if (context->num_used_fds)
		kprintf("   no. type     ops ref open mode        pos cookie\n");

	for (uint32 i = 0; i < context->table_size; i++) {
		struct file_descriptor *fd = context->fds[i];
		if (fd == NULL)
			continue;

		kprintf("  %3lu: %ld %p %3ld %4ld %4lx %10Ld %p %s %p\n", i, fd->type, fd->ops,
			fd->ref_count, fd->open_count, fd->open_mode, fd->pos, fd->cookie,
			fd->type >= FDTYPE_INDEX && fd->type <= FDTYPE_QUERY ? "mount" : "vnode",
			fd->u.vnode);
	}

	kprintf(" used monitors:\t%lu\n", context->num_monitors);
	kprintf(" max monitors:\t%lu\n", context->max_monitors);

	set_debug_variable("_cwd", (addr_t)context->cwd);

	return 0;
}


int
dump_vnode_usage(int argc, char **argv)
{
	if (argc != 1) {
		kprintf("usage: %s\n", argv[0]);
		return 0;
	}

	kprintf("Unused vnodes: %ld (max unused %ld)\n", sUnusedVnodes,
		kMaxUnusedVnodes);

	struct hash_iterator iterator;
	hash_open(sVnodeTable, &iterator);

	uint32 count = 0;
	struct vnode *vnode;
	while ((vnode = (struct vnode *)hash_next(sVnodeTable, &iterator)) != NULL) {
		count++;
	}

	hash_close(sVnodeTable, &iterator, false);

	kprintf("%lu vnodes total (%ld in use).\n", count, count - sUnusedVnodes);
	return 0;
}

#endif	// ADD_DEBUGGER_COMMANDS

/*!	Does the dirty work of combining the file_io_vecs with the iovecs
	and calls the file system hooks to read/write the request to disk.
*/
static status_t
common_file_io_vec_pages(struct vnode *vnode, void *cookie,
	const file_io_vec *fileVecs, size_t fileVecCount, const iovec *vecs,
	size_t vecCount, uint32 *_vecIndex, size_t *_vecOffset, size_t *_numBytes,
	bool doWrite)
{
	if (fileVecCount == 0) {
		// There are no file vecs at this offset, so we're obviously trying
		// to access the file outside of its bounds
		return B_BAD_VALUE;
	}

	size_t numBytes = *_numBytes;
	uint32 fileVecIndex;
	size_t vecOffset = *_vecOffset;
	uint32 vecIndex = *_vecIndex;
	status_t status;
	size_t size;

	if (!doWrite && vecOffset == 0) {
		// now directly read the data from the device
		// the first file_io_vec can be read directly

		size = fileVecs[0].length;
		if (size > numBytes)
			size = numBytes;

		status = FS_CALL(vnode, read_pages)(vnode->mount->cookie,
			vnode->private_node, cookie, fileVecs[0].offset, &vecs[vecIndex],
			vecCount - vecIndex, &size, false);
		if (status < B_OK)
			return status;

		// TODO: this is a work-around for buggy device drivers!
		//	When our own drivers honour the length, we can:
		//	a) also use this direct I/O for writes (otherwise, it would
		//	   overwrite precious data)
		//	b) panic if the term below is true (at least for writes)
		if (size > fileVecs[0].length) {
			//dprintf("warning: device driver %p doesn't respect total length in read_pages() call!\n", ref->device);
			size = fileVecs[0].length;
		}

		ASSERT(size <= fileVecs[0].length);

		// If the file portion was contiguous, we're already done now
		if (size == numBytes)
			return B_OK;

		// if we reached the end of the file, we can return as well
		if (size != fileVecs[0].length) {
			*_numBytes = size;
			return B_OK;
		}

		fileVecIndex = 1;

		// first, find out where we have to continue in our iovecs
		for (; vecIndex < vecCount; vecIndex++) {
			if (size < vecs[vecIndex].iov_len)
				break;

			size -= vecs[vecIndex].iov_len;
		}

		vecOffset = size;
	} else {
		fileVecIndex = 0;
		size = 0;
	}

	// Too bad, let's process the rest of the file_io_vecs

	size_t totalSize = size;
	size_t bytesLeft = numBytes - size;

	for (; fileVecIndex < fileVecCount; fileVecIndex++) {
		const file_io_vec &fileVec = fileVecs[fileVecIndex];
		off_t fileOffset = fileVec.offset;
		off_t fileLeft = min_c(fileVec.length, bytesLeft);

		TRACE(("FILE VEC [%lu] length %Ld\n", fileVecIndex, fileLeft));

		// process the complete fileVec
		while (fileLeft > 0) {
			iovec tempVecs[MAX_TEMP_IO_VECS];
			uint32 tempCount = 0;

			// size tracks how much of what is left of the current fileVec
			// (fileLeft) has been assigned to tempVecs
			size = 0;

			// assign what is left of the current fileVec to the tempVecs
			for (size = 0; size < fileLeft && vecIndex < vecCount
					&& tempCount < MAX_TEMP_IO_VECS;) {
				// try to satisfy one iovec per iteration (or as much as
				// possible)

				// bytes left of the current iovec
				size_t vecLeft = vecs[vecIndex].iov_len - vecOffset;
				if (vecLeft == 0) {
					vecOffset = 0;
					vecIndex++;
					continue;
				}

				TRACE(("fill vec %ld, offset = %lu, size = %lu\n",
					vecIndex, vecOffset, size));

				// actually available bytes
				size_t tempVecSize = min_c(vecLeft, fileLeft - size);

				tempVecs[tempCount].iov_base
					= (void *)((addr_t)vecs[vecIndex].iov_base + vecOffset);
				tempVecs[tempCount].iov_len = tempVecSize;
				tempCount++;

				size += tempVecSize;
				vecOffset += tempVecSize;
			}

			size_t bytes = size;
			if (doWrite) {
				status = FS_CALL(vnode, write_pages)(vnode->mount->cookie,
					vnode->private_node, cookie, fileOffset, tempVecs,
					tempCount, &bytes, false);
			} else {
				status = FS_CALL(vnode, read_pages)(vnode->mount->cookie,
					vnode->private_node, cookie, fileOffset, tempVecs,
					tempCount, &bytes, false);
			}
			if (status < B_OK)
				return status;

			totalSize += bytes;
			bytesLeft -= size;
			fileOffset += size;
			fileLeft -= size;
			//dprintf("-> file left = %Lu\n", fileLeft);

			if (size != bytes || vecIndex >= vecCount) {
				// there are no more bytes or iovecs, let's bail out
				*_numBytes = totalSize;
				return B_OK;
			}
		}
	}

	*_vecIndex = vecIndex;
	*_vecOffset = vecOffset;
	*_numBytes = totalSize;
	return B_OK;
}


//	#pragma mark - public API for file systems


extern "C" status_t
new_vnode(dev_t mountID, ino_t vnodeID, fs_vnode privateNode)
{
	FUNCTION(("new_vnode(mountID = %ld, vnodeID = %Ld, node = %p)\n",
		mountID, vnodeID, privateNode));

	if (privateNode == NULL)
		return B_BAD_VALUE;

	mutex_lock(&sVnodeMutex);

	// file system integrity check:
	// test if the vnode already exists and bail out if this is the case!

	// ToDo: the R5 implementation obviously checks for a different cookie
	//	and doesn't panic if they are equal

	struct vnode *vnode = lookup_vnode(mountID, vnodeID);
	if (vnode != NULL)
		panic("vnode %ld:%Ld already exists (node = %p, vnode->node = %p)!", mountID, vnodeID, privateNode, vnode->private_node);

	status_t status = create_new_vnode(&vnode, mountID, vnodeID);
	if (status == B_OK) {
		vnode->private_node = privateNode;
		vnode->busy = true;
		vnode->unpublished = true;
	}

	TRACE(("returns: %s\n", strerror(status)));

	mutex_unlock(&sVnodeMutex);
	return status;
}


extern "C" status_t
publish_vnode(dev_t mountID, ino_t vnodeID, fs_vnode privateNode)
{
	FUNCTION(("publish_vnode()\n"));

	mutex_lock(&sVnodeMutex);

	struct vnode *vnode = lookup_vnode(mountID, vnodeID);
	status_t status = B_OK;

	if (vnode != NULL && vnode->busy && vnode->unpublished
		&& vnode->private_node == privateNode) {
		vnode->busy = false;
		vnode->unpublished = false;
	} else if (vnode == NULL && privateNode != NULL) {
		status = create_new_vnode(&vnode, mountID, vnodeID);
		if (status == B_OK)
			vnode->private_node = privateNode;
	} else
		status = B_BAD_VALUE;

	TRACE(("returns: %s\n", strerror(status)));

	mutex_unlock(&sVnodeMutex);
	return status;
}


extern "C" status_t
get_vnode(dev_t mountID, ino_t vnodeID, fs_vnode *_fsNode)
{
	struct vnode *vnode;

	status_t status = get_vnode(mountID, vnodeID, &vnode, true, true);
	if (status < B_OK)
		return status;

	*_fsNode = vnode->private_node;
	return B_OK;
}


extern "C" status_t
put_vnode(dev_t mountID, ino_t vnodeID)
{
	struct vnode *vnode;

	mutex_lock(&sVnodeMutex);
	vnode = lookup_vnode(mountID, vnodeID);
	mutex_unlock(&sVnodeMutex);

	if (vnode)
		dec_vnode_ref_count(vnode, true);

	return B_OK;
}


extern "C" status_t
remove_vnode(dev_t mountID, ino_t vnodeID)
{
	struct vnode *vnode;
	bool remove = false;

	MutexLocker locker(sVnodeMutex);

	vnode = lookup_vnode(mountID, vnodeID);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (vnode->covered_by != NULL) {
		// this vnode is in use
		mutex_unlock(&sVnodeMutex);
		return B_BUSY;
	}

	vnode->remove = true;
	if (vnode->unpublished) {
		// prepare the vnode for deletion
		vnode->busy = true;
		remove = true;
	}

	locker.Unlock();

	if (remove) {
		// if the vnode hasn't been published yet, we delete it here
		atomic_add(&vnode->ref_count, -1);
		free_vnode(vnode, true);
	}

	return B_OK;
}


extern "C" status_t
unremove_vnode(dev_t mountID, ino_t vnodeID)
{
	struct vnode *vnode;

	mutex_lock(&sVnodeMutex);

	vnode = lookup_vnode(mountID, vnodeID);
	if (vnode)
		vnode->remove = false;

	mutex_unlock(&sVnodeMutex);
	return B_OK;
}


extern "C" status_t
get_vnode_removed(dev_t mountID, ino_t vnodeID, bool* removed)
{
	mutex_lock(&sVnodeMutex);

	status_t result;

	if (struct vnode* vnode = lookup_vnode(mountID, vnodeID)) {
		if (removed)
			*removed = vnode->remove;
		result = B_OK;
	} else
		result = B_BAD_VALUE;

	mutex_unlock(&sVnodeMutex);
	return result;
}


extern "C" status_t
read_pages(int fd, off_t pos, const iovec *vecs, size_t count,
	size_t *_numBytes, bool fsReenter)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = FS_CALL(vnode, read_pages)(vnode->mount->cookie,
		vnode->private_node, descriptor->cookie, pos, vecs, count, _numBytes,
		fsReenter);

	put_fd(descriptor);
	return status;
}


extern "C" status_t
write_pages(int fd, off_t pos, const iovec *vecs, size_t count,
	size_t *_numBytes, bool fsReenter)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = FS_CALL(vnode, write_pages)(vnode->mount->cookie,
		vnode->private_node, descriptor->cookie, pos, vecs, count, _numBytes,
		fsReenter);

	put_fd(descriptor);
	return status;
}


extern "C" status_t
read_file_io_vec_pages(int fd, const file_io_vec *fileVecs, size_t fileVecCount,
	const iovec *vecs, size_t vecCount, uint32 *_vecIndex, size_t *_vecOffset,
	size_t *_bytes)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = common_file_io_vec_pages(vnode, descriptor->cookie,
		fileVecs, fileVecCount, vecs, vecCount, _vecIndex, _vecOffset, _bytes,
		false);

	put_fd(descriptor);
	return status;
}


extern "C" status_t
write_file_io_vec_pages(int fd, const file_io_vec *fileVecs, size_t fileVecCount,
	const iovec *vecs, size_t vecCount, uint32 *_vecIndex, size_t *_vecOffset,
	size_t *_bytes)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = common_file_io_vec_pages(vnode, descriptor->cookie,
		fileVecs, fileVecCount, vecs, vecCount, _vecIndex, _vecOffset, _bytes,
		true);

	put_fd(descriptor);
	return status;
}


//	#pragma mark - private VFS API
//	Functions the VFS exports for other parts of the kernel


/*! Acquires another reference to the vnode that has to be released
	by calling vfs_put_vnode().
*/
void
vfs_acquire_vnode(struct vnode *vnode)
{
	inc_vnode_ref_count(vnode);
}


/*! This is currently called from file_cache_create() only.
	It's probably a temporary solution as long as devfs requires that
	fs_read_pages()/fs_write_pages() are called with the standard
	open cookie and not with a device cookie.
	If that's done differently, remove this call; it has no other
	purpose.
*/
extern "C" status_t
vfs_get_cookie_from_fd(int fd, void **_cookie)
{
	struct file_descriptor *descriptor;

	descriptor = get_fd(get_current_io_context(true), fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	*_cookie = descriptor->cookie;
	return B_OK;
}


extern "C" int
vfs_get_vnode_from_fd(int fd, bool kernel, struct vnode **vnode)
{
	*vnode = get_vnode_from_fd(fd, kernel);

	if (*vnode == NULL)
		return B_FILE_ERROR;

	return B_NO_ERROR;
}


extern "C" status_t
vfs_get_vnode_from_path(const char *path, bool kernel, struct vnode **_vnode)
{
	TRACE(("vfs_get_vnode_from_path: entry. path = '%s', kernel %d\n",
		path, kernel));

	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *buffer = pathBuffer.LockBuffer();
	strlcpy(buffer, path, pathBuffer.BufferSize());

	struct vnode *vnode;
	status_t status = path_to_vnode(buffer, true, &vnode, NULL, kernel);
	if (status < B_OK)
		return status;

	*_vnode = vnode;
	return B_OK;
}


extern "C" status_t
vfs_get_vnode(dev_t mountID, ino_t vnodeID, bool canWait, struct vnode **_vnode)
{
	struct vnode *vnode;

	status_t status = get_vnode(mountID, vnodeID, &vnode, canWait, false);
	if (status < B_OK)
		return status;

	*_vnode = vnode;
	return B_OK;
}


extern "C" status_t
vfs_entry_ref_to_vnode(dev_t mountID, ino_t directoryID,
	const char *name, struct vnode **_vnode)
{
	return entry_ref_to_vnode(mountID, directoryID, name, false, _vnode);
}


extern "C" void
vfs_vnode_to_node_ref(struct vnode *vnode, dev_t *_mountID, ino_t *_vnodeID)
{
	*_mountID = vnode->device;
	*_vnodeID = vnode->id;
}


/*!	Looks up a vnode with the given mount and vnode ID.
	Must only be used with "in-use" vnodes as it doesn't grab a reference
	to the node.
	It's currently only be used by file_cache_create().
*/
extern "C" status_t
vfs_lookup_vnode(dev_t mountID, ino_t vnodeID, struct vnode **_vnode)
{
	mutex_lock(&sVnodeMutex);
	struct vnode *vnode = lookup_vnode(mountID, vnodeID);
	mutex_unlock(&sVnodeMutex);

	if (vnode == NULL)
		return B_ERROR;

	*_vnode = vnode;
	return B_OK;
}


extern "C" status_t
vfs_get_fs_node_from_path(dev_t mountID, const char *path, bool kernel,
	void **_node)
{
	TRACE(("vfs_get_fs_node_from_path(mountID = %ld, path = \"%s\", kernel %d)\n",
		mountID, path, kernel));

	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	fs_mount *mount;
	status_t status = get_mount(mountID, &mount);
	if (status < B_OK)
		return status;

	char *buffer = pathBuffer.LockBuffer();
	strlcpy(buffer, path, pathBuffer.BufferSize());

	struct vnode *vnode = mount->root_vnode;

	if (buffer[0] == '/')
		status = path_to_vnode(buffer, true, &vnode, NULL, true);
	else {
		inc_vnode_ref_count(vnode);
			// vnode_path_to_vnode() releases a reference to the starting vnode
		status = vnode_path_to_vnode(vnode, buffer, true, 0, &vnode, NULL, NULL);
	}

	put_mount(mount);

	if (status < B_OK)
		return status;

	if (vnode->device != mountID) {
		// wrong mount ID - must not gain access on foreign file system nodes
		put_vnode(vnode);
		return B_BAD_VALUE;
	}

	*_node = vnode->private_node;
	return B_OK;
}


/*!	Finds the full path to the file that contains the module \a moduleName,
	puts it into \a pathBuffer, and returns B_OK for success.
	If \a pathBuffer was too small, it returns \c B_BUFFER_OVERFLOW,
	\c B_ENTRY_NOT_FOUNT if no file could be found.
	\a pathBuffer is clobbered in any case and must not be relied on if this
	functions returns unsuccessfully.
	\a basePath and \a pathBuffer must not point to the same space.
*/
status_t
vfs_get_module_path(const char *basePath, const char *moduleName,
	char *pathBuffer, size_t bufferSize)
{
	struct vnode *dir, *file;
	status_t status;
	size_t length;
	char *path;

	if (bufferSize == 0
		|| strlcpy(pathBuffer, basePath, bufferSize) >= bufferSize)
		return B_BUFFER_OVERFLOW;

	status = path_to_vnode(pathBuffer, true, &dir, NULL, true);
	if (status < B_OK)
		return status;

	// the path buffer had been clobbered by the above call
	length = strlcpy(pathBuffer, basePath, bufferSize);
	if (pathBuffer[length - 1] != '/')
		pathBuffer[length++] = '/';

	path = pathBuffer + length;
	bufferSize -= length;

	while (moduleName) {
		int type;

		char *nextPath = strchr(moduleName, '/');
		if (nextPath == NULL)
			length = strlen(moduleName);
		else {
			length = nextPath - moduleName;
			nextPath++;
		}

		if (length + 1 >= bufferSize) {
			status = B_BUFFER_OVERFLOW;
			goto err;
		}

		memcpy(path, moduleName, length);
		path[length] = '\0';
		moduleName = nextPath;

		status = vnode_path_to_vnode(dir, path, true, 0, &file, NULL, &type);
		if (status < B_OK) {
			// vnode_path_to_vnode() has already released the reference to dir
			return status;
		}

		if (S_ISDIR(type)) {
			// goto the next directory
			path[length] = '/';
			path[length + 1] = '\0';
			path += length + 1;
			bufferSize -= length + 1;

			dir = file;
		} else if (S_ISREG(type)) {
			// it's a file so it should be what we've searched for
			put_vnode(file);

			return B_OK;
		} else {
			TRACE(("vfs_get_module_path(): something is strange here: %d...\n", type));
			status = B_ERROR;
			dir = file;
			goto err;
		}
	}

	// if we got here, the moduleName just pointed to a directory, not to
	// a real module - what should we do in this case?
	status = B_ENTRY_NOT_FOUND;

err:
	put_vnode(dir);
	return status;
}


/*!	\brief Normalizes a given path.

	The path must refer to an existing or non-existing entry in an existing
	directory, that is chopping off the leaf component the remaining path must
	refer to an existing directory.

	The returned will be canonical in that it will be absolute, will not
	contain any "." or ".." components or duplicate occurrences of '/'s,
	and none of the directory components will by symbolic links.

	Any two paths referring to the same entry, will result in the same
	normalized path (well, that is pretty much the definition of `normalized',
	isn't it :-).

	\param path The path to be normalized.
	\param buffer The buffer into which the normalized path will be written.
		   May be the same one as \a path.
	\param bufferSize The size of \a buffer.
	\param kernel \c true, if the IO context of the kernel shall be used,
		   otherwise that of the team this thread belongs to. Only relevant,
		   if the path is relative (to get the CWD).
	\return \c B_OK if everything went fine, another error code otherwise.
*/
status_t
vfs_normalize_path(const char *path, char *buffer, size_t bufferSize,
	bool kernel)
{
	if (!path || !buffer || bufferSize < 1)
		return B_BAD_VALUE;

	TRACE(("vfs_normalize_path(`%s')\n", path));

	// copy the supplied path to the stack, so it can be modified
	KPath mutablePathBuffer(B_PATH_NAME_LENGTH + 1);
	if (mutablePathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *mutablePath = mutablePathBuffer.LockBuffer();
	if (strlcpy(mutablePath, path, B_PATH_NAME_LENGTH) >= B_PATH_NAME_LENGTH)
		return B_NAME_TOO_LONG;

	// get the dir vnode and the leaf name
	struct vnode *dirNode;
	char leaf[B_FILE_NAME_LENGTH];
	status_t error = path_to_dir_vnode(mutablePath, &dirNode, leaf, kernel);
	if (error != B_OK) {
		TRACE(("vfs_normalize_path(): failed to get dir vnode: %s\n", strerror(error)));
		return error;
	}

	// if the leaf is "." or "..", we directly get the correct directory
	// vnode and ignore the leaf later
	bool isDir = (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0);
	if (isDir)
		error = vnode_path_to_vnode(dirNode, leaf, false, 0, &dirNode, NULL, NULL);
	if (error != B_OK) {
		TRACE(("vfs_normalize_path(): failed to get dir vnode for \".\" or \"..\": %s\n",
			strerror(error)));
		return error;
	}

	// get the directory path
	error = dir_vnode_to_path(dirNode, buffer, bufferSize);
	put_vnode(dirNode);
	if (error < B_OK) {
		TRACE(("vfs_normalize_path(): failed to get dir path: %s\n", strerror(error)));
		return error;
	}

	// append the leaf name
	if (!isDir) {
		// insert a directory separator only if this is not the file system root
		if ((strcmp(buffer, "/") != 0
			 && strlcat(buffer, "/", bufferSize) >= bufferSize)
			|| strlcat(buffer, leaf, bufferSize) >= bufferSize) {
			return B_NAME_TOO_LONG;
		}
	}

	TRACE(("vfs_normalize_path() -> `%s'\n", buffer));
	return B_OK;
}


extern "C" void
vfs_put_vnode(struct vnode *vnode)
{
	put_vnode(vnode);
}


extern "C" status_t
vfs_get_cwd(dev_t *_mountID, ino_t *_vnodeID)
{
	// Get current working directory from io context
	struct io_context *context = get_current_io_context(false);
	status_t status = B_OK;

	mutex_lock(&context->io_mutex);

	if (context->cwd != NULL) {
		*_mountID = context->cwd->device;
		*_vnodeID = context->cwd->id;
	} else
		status = B_ERROR;

	mutex_unlock(&context->io_mutex);
	return status;
}


status_t
vfs_unmount(dev_t mountID, uint32 flags)
{
	return fs_unmount(NULL, mountID, flags, true);
}


extern "C" status_t
vfs_disconnect_vnode(dev_t mountID, ino_t vnodeID)
{
	struct vnode *vnode;

	status_t status = get_vnode(mountID, vnodeID, &vnode, true, true);
	if (status < B_OK)
		return status;

	disconnect_mount_or_vnode_fds(vnode->mount, vnode);
	put_vnode(vnode);
	return B_OK;
}


extern "C" void
vfs_free_unused_vnodes(int32 level)
{
	vnode_low_memory_handler(NULL, level);
}


extern "C" bool
vfs_can_page(struct vnode *vnode, void *cookie)
{
	FUNCTION(("vfs_canpage: vnode 0x%p\n", vnode));

	if (FS_CALL(vnode, can_page)) {
		return FS_CALL(vnode, can_page)(vnode->mount->cookie,
			vnode->private_node, cookie);
	}
	return false;
}


extern "C" status_t
vfs_read_pages(struct vnode *vnode, void *cookie, off_t pos, const iovec *vecs,
	size_t count, size_t *_numBytes, bool fsReenter)
{
	FUNCTION(("vfs_read_pages: vnode %p, vecs %p, pos %Ld\n", vnode, vecs, pos));

	return FS_CALL(vnode, read_pages)(vnode->mount->cookie, vnode->private_node,
		cookie, pos, vecs, count, _numBytes, fsReenter);
}


extern "C" status_t
vfs_write_pages(struct vnode *vnode, void *cookie, off_t pos, const iovec *vecs,
	size_t count, size_t *_numBytes, bool fsReenter)
{
	FUNCTION(("vfs_write_pages: vnode %p, vecs %p, pos %Ld\n", vnode, vecs, pos));

	return FS_CALL(vnode, write_pages)(vnode->mount->cookie, vnode->private_node,
		cookie, pos, vecs, count, _numBytes, fsReenter);
}


/*!	Gets the vnode's vm_cache object. If it didn't have one, it will be
	created if \a allocate is \c true.
	In case it's successful, it will also grab a reference to the cache
	it returns.
*/
extern "C" status_t
vfs_get_vnode_cache(struct vnode *vnode, vm_cache **_cache, bool allocate)
{
	if (vnode->cache != NULL) {
		vm_cache_acquire_ref(vnode->cache);
		*_cache = vnode->cache;
		return B_OK;
	}

	mutex_lock(&sVnodeMutex);

	status_t status = B_OK;

	// The cache could have been created in the meantime
	if (vnode->cache == NULL) {
		if (allocate) {
			// TODO: actually the vnode need to be busy already here, or
			//	else this won't work...
			bool wasBusy = vnode->busy;
			vnode->busy = true;
			mutex_unlock(&sVnodeMutex);

			status = vm_create_vnode_cache(vnode, &vnode->cache);

			mutex_lock(&sVnodeMutex);
			vnode->busy = wasBusy;
		} else
			status = B_BAD_VALUE;
	}

	if (status == B_OK) {
		vm_cache_acquire_ref(vnode->cache);
		*_cache = vnode->cache;
	}

	mutex_unlock(&sVnodeMutex);
	return status;
}


status_t
vfs_get_file_map(struct vnode *vnode, off_t offset, size_t size,
	file_io_vec *vecs, size_t *_count)
{
	FUNCTION(("vfs_get_file_map: vnode %p, vecs %p, offset %Ld, size = %lu\n", vnode, vecs, offset, size));

	return FS_CALL(vnode, get_file_map)(vnode->mount->cookie,
		vnode->private_node, offset, size, vecs, _count);
}


status_t
vfs_stat_vnode(struct vnode *vnode, struct stat *stat)
{
	status_t status = FS_CALL(vnode, read_stat)(vnode->mount->cookie,
		vnode->private_node, stat);

	// fill in the st_dev and st_ino fields
	if (status == B_OK) {
		stat->st_dev = vnode->device;
		stat->st_ino = vnode->id;
	}

	return status;
}


status_t
vfs_get_vnode_name(struct vnode *vnode, char *name, size_t nameSize)
{
	return get_vnode_name(vnode, NULL, name, nameSize);
}


status_t
vfs_entry_ref_to_path(dev_t device, ino_t inode, const char *leaf,
	char *path, size_t pathLength)
{
	struct vnode *vnode;
	status_t status;

	// filter invalid leaf names
	if (leaf != NULL && (leaf[0] == '\0' || strchr(leaf, '/')))
		return B_BAD_VALUE;

	// get the vnode matching the dir's node_ref
	if (leaf && (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0)) {
		// special cases "." and "..": we can directly get the vnode of the
		// referenced directory
		status = entry_ref_to_vnode(device, inode, leaf, false, &vnode);
		leaf = NULL;
	} else
		status = get_vnode(device, inode, &vnode, true, false);
	if (status < B_OK)
		return status;

	// get the directory path
	status = dir_vnode_to_path(vnode, path, pathLength);
	put_vnode(vnode);
		// we don't need the vnode anymore
	if (status < B_OK)
		return status;

	// append the leaf name
	if (leaf) {
		// insert a directory separator if this is not the file system root
		if ((strcmp(path, "/") && strlcat(path, "/", pathLength)
				>= pathLength)
			|| strlcat(path, leaf, pathLength) >= pathLength) {
			return B_NAME_TOO_LONG;
		}
	}

	return B_OK;
}


/*!	If the given descriptor locked its vnode, that lock will be released. */
void
vfs_unlock_vnode_if_locked(struct file_descriptor *descriptor)
{
	struct vnode *vnode = fd_vnode(descriptor);

	if (vnode != NULL && vnode->mandatory_locked_by == descriptor)
		vnode->mandatory_locked_by = NULL;
}


/*!	Closes all file descriptors of the specified I/O context that
	have the O_CLOEXEC flag set.
*/
void
vfs_exec_io_context(void *_context)
{
	struct io_context *context = (struct io_context *)_context;
	uint32 i;

	for (i = 0; i < context->table_size; i++) {
		mutex_lock(&context->io_mutex);

		struct file_descriptor *descriptor = context->fds[i];
		bool remove = false;

		if (descriptor != NULL && fd_close_on_exec(context, i)) {
			context->fds[i] = NULL;
			context->num_used_fds--;

			remove = true;
		}

		mutex_unlock(&context->io_mutex);

		if (remove) {
			close_fd(descriptor);
			put_fd(descriptor);
		}
	}
}


/*! Sets up a new io_control structure, and inherits the properties
	of the parent io_control if it is given.
*/
void *
vfs_new_io_context(void *_parentContext)
{
	size_t tableSize;
	struct io_context *context;
	struct io_context *parentContext;

	context = (io_context *)malloc(sizeof(struct io_context));
	if (context == NULL)
		return NULL;

	memset(context, 0, sizeof(struct io_context));

	parentContext = (struct io_context *)_parentContext;
	if (parentContext)
		tableSize = parentContext->table_size;
	else
		tableSize = DEFAULT_FD_TABLE_SIZE;

	// allocate space for FDs and their close-on-exec flag
	context->fds = (file_descriptor**)malloc(
		sizeof(struct file_descriptor*) * tableSize
		+ sizeof(struct select_sync*) * tableSize
		+ (tableSize + 7) / 8);
	if (context->fds == NULL) {
		free(context);
		return NULL;
	}

	context->select_infos = (select_info**)(context->fds + tableSize);
	context->fds_close_on_exec = (uint8 *)(context->select_infos + tableSize);

	memset(context->fds, 0, sizeof(struct file_descriptor*) * tableSize
		+ sizeof(struct select_sync*) * tableSize
		+ (tableSize + 7) / 8);

	if (mutex_init(&context->io_mutex, "I/O context") < 0) {
		free(context->fds);
		free(context);
		return NULL;
	}

	// Copy all parent file descriptors

	if (parentContext) {
		size_t i;

		mutex_lock(&parentContext->io_mutex);

		context->cwd = parentContext->cwd;
		if (context->cwd)
			inc_vnode_ref_count(context->cwd);

		for (i = 0; i < tableSize; i++) {
			struct file_descriptor *descriptor = parentContext->fds[i];

			if (descriptor != NULL) {
				context->fds[i] = descriptor;
				context->num_used_fds++;
				atomic_add(&descriptor->ref_count, 1);
				atomic_add(&descriptor->open_count, 1);

				if (fd_close_on_exec(parentContext, i))
					fd_set_close_on_exec(context, i, true);
			}
		}

		mutex_unlock(&parentContext->io_mutex);
	} else {
		context->cwd = sRoot;

		if (context->cwd)
			inc_vnode_ref_count(context->cwd);
	}

	context->table_size = tableSize;

	list_init(&context->node_monitors);
	context->max_monitors = DEFAULT_NODE_MONITORS;

	return context;
}


status_t
vfs_free_io_context(void *_ioContext)
{
	struct io_context *context = (struct io_context *)_ioContext;
	uint32 i;

	if (context->cwd)
		dec_vnode_ref_count(context->cwd, false);

	mutex_lock(&context->io_mutex);

	for (i = 0; i < context->table_size; i++) {
		if (struct file_descriptor *descriptor = context->fds[i]) {
			close_fd(descriptor);
			put_fd(descriptor);
		}
	}

	mutex_destroy(&context->io_mutex);

	remove_node_monitors(context);
	free(context->fds);
	free(context);

	return B_OK;
}


static status_t
vfs_resize_fd_table(struct io_context *context, const int newSize)
{
	struct file_descriptor **fds;

	if (newSize <= 0 || newSize > MAX_FD_TABLE_SIZE)
		return EINVAL;

	MutexLocker(context->io_mutex);

	int oldSize = context->table_size;
	int oldCloseOnExitBitmapSize = (oldSize + 7) / 8;
	int newCloseOnExitBitmapSize = (newSize + 7) / 8;

	// If the tables shrink, make sure none of the fds being dropped are in use.
	if (newSize < oldSize) {
		for (int i = oldSize; i-- > newSize;) {
			if (context->fds[i])
				return EBUSY;
		}
	}

	// store pointers to the old tables
	file_descriptor** oldFDs = context->fds;
	select_info** oldSelectInfos = context->select_infos;
	uint8* oldCloseOnExecTable = context->fds_close_on_exec;

	// allocate new tables
	file_descriptor** newFDs = (file_descriptor**)malloc(
		sizeof(struct file_descriptor*) * newSize
		+ sizeof(struct select_sync*) * newSize
		+ newCloseOnExitBitmapSize);
	if (newFDs == NULL)
		return ENOMEM;

	context->fds = newFDs;
	context->select_infos = (select_info**)(context->fds + newSize);
	context->fds_close_on_exec = (uint8 *)(context->select_infos + newSize);
	context->table_size = newSize;

	// copy entries from old tables
	int toCopy = min_c(oldSize, newSize);

	memcpy(context->fds, oldFDs, sizeof(void*) * toCopy);
	memcpy(context->select_infos, oldSelectInfos, sizeof(void*) * toCopy);
	memcpy(context->fds_close_on_exec, oldCloseOnExecTable,
		min_c(oldCloseOnExitBitmapSize, newCloseOnExitBitmapSize));

	// clear additional entries, if the tables grow
	if (newSize > oldSize) {
		memset(context->fds + oldSize, 0, sizeof(void *) * (newSize - oldSize));
		memset(context->select_infos + oldSize, 0,
			sizeof(void *) * (newSize - oldSize));
		memset(context->fds_close_on_exec + oldCloseOnExitBitmapSize, 0,
			newCloseOnExitBitmapSize - oldCloseOnExitBitmapSize);
	}

	free(oldFDs);

	return B_OK;
}


static status_t
vfs_resize_monitor_table(struct io_context *context, const int newSize)
{
	void *fds;
	int	status = B_OK;

	if (newSize <= 0 || newSize > MAX_NODE_MONITORS)
		return EINVAL;

	mutex_lock(&context->io_mutex);

	if ((size_t)newSize < context->num_monitors) {
		status = EBUSY;
		goto out;
	}
	context->max_monitors = newSize;

out:
	mutex_unlock(&context->io_mutex);
	return status;
}


int
vfs_getrlimit(int resource, struct rlimit * rlp)
{
	if (!rlp)
		return B_BAD_ADDRESS;

	switch (resource) {
		case RLIMIT_NOFILE:
		{
			struct io_context *ioctx = get_current_io_context(false);

			mutex_lock(&ioctx->io_mutex);

			rlp->rlim_cur = ioctx->table_size;
			rlp->rlim_max = MAX_FD_TABLE_SIZE;

			mutex_unlock(&ioctx->io_mutex);

			return 0;
		}

		case RLIMIT_NOVMON:
		{
			struct io_context *ioctx = get_current_io_context(false);

			mutex_lock(&ioctx->io_mutex);

			rlp->rlim_cur = ioctx->max_monitors;
			rlp->rlim_max = MAX_NODE_MONITORS;

			mutex_unlock(&ioctx->io_mutex);

			return 0;
		}

		default:
			return EINVAL;
	}
}


int
vfs_setrlimit(int resource, const struct rlimit * rlp)
{
	if (!rlp)
		return B_BAD_ADDRESS;

	switch (resource) {
		case RLIMIT_NOFILE:
			/* TODO: check getuid() */
			if (rlp->rlim_max != RLIM_SAVED_MAX
				&& rlp->rlim_max != MAX_FD_TABLE_SIZE)
				return EPERM;
			return vfs_resize_fd_table(get_current_io_context(false), rlp->rlim_cur);

		case RLIMIT_NOVMON:
			/* TODO: check getuid() */
			if (rlp->rlim_max != RLIM_SAVED_MAX
				&& rlp->rlim_max != MAX_NODE_MONITORS)
				return EPERM;
			return vfs_resize_monitor_table(get_current_io_context(false), rlp->rlim_cur);

		default:
			return EINVAL;
	}
}


status_t
vfs_init(kernel_args *args)
{
	sVnodeTable = hash_init(VNODE_HASH_TABLE_SIZE, offsetof(struct vnode, next),
		&vnode_compare, &vnode_hash);
	if (sVnodeTable == NULL)
		panic("vfs_init: error creating vnode hash table\n");

	list_init_etc(&sUnusedVnodeList, offsetof(struct vnode, unused_link));

	sMountsTable = hash_init(MOUNTS_HASH_TABLE_SIZE, offsetof(struct fs_mount, next),
		&mount_compare, &mount_hash);
	if (sMountsTable == NULL)
		panic("vfs_init: error creating mounts hash table\n");

	node_monitor_init();

	sRoot = NULL;

	if (mutex_init(&sFileSystemsMutex, "vfs_lock") < 0)
		panic("vfs_init: error allocating file systems lock\n");

	if (recursive_lock_init(&sMountOpLock, "vfs_mount_op_lock") < 0)
		panic("vfs_init: error allocating mount op lock\n");

	if (mutex_init(&sMountMutex, "vfs_mount_lock") < 0)
		panic("vfs_init: error allocating mount lock\n");

	if (mutex_init(&sVnodeCoveredByMutex, "vfs_vnode_covered_by_lock") < 0)
		panic("vfs_init: error allocating vnode::covered_by lock\n");

	if (mutex_init(&sVnodeMutex, "vfs_vnode_lock") < 0)
		panic("vfs_init: error allocating vnode lock\n");

	if (block_cache_init() != B_OK)
		return B_ERROR;

#ifdef ADD_DEBUGGER_COMMANDS
	// add some debugger commands
	add_debugger_command("vnode", &dump_vnode, "info about the specified vnode");
	add_debugger_command("vnodes", &dump_vnodes, "list all vnodes (from the specified device)");
	add_debugger_command("vnode_caches", &dump_vnode_caches, "list all vnode caches");
	add_debugger_command("mount", &dump_mount, "info about the specified fs_mount");
	add_debugger_command("mounts", &dump_mounts, "list all fs_mounts");
	add_debugger_command("io_context", &dump_io_context, "info about the I/O context");
	add_debugger_command("vnode_usage", &dump_vnode_usage, "info about vnode usage");
#endif

	register_low_memory_handler(&vnode_low_memory_handler, NULL, 0);

	return file_cache_init();
}


//	#pragma mark - fd_ops implementations


/*!
	Calls fs_open() on the given vnode and returns a new
	file descriptor for it
*/
static int
create_vnode(struct vnode *directory, const char *name, int openMode,
	int perms, bool kernel)
{
	struct vnode *vnode;
	fs_cookie cookie;
	ino_t newID;
	int status;

	if (FS_CALL(directory, create) == NULL)
		return EROFS;

	status = FS_CALL(directory, create)(directory->mount->cookie,
		directory->private_node, name, openMode, perms, &cookie, &newID);
	if (status < B_OK)
		return status;

	mutex_lock(&sVnodeMutex);
	vnode = lookup_vnode(directory->device, newID);
	mutex_unlock(&sVnodeMutex);

	if (vnode == NULL) {
		panic("vfs: fs_create() returned success but there is no vnode, mount ID %ld!\n",
			directory->device);
		return B_BAD_VALUE;
	}

	if ((status = get_new_fd(FDTYPE_FILE, NULL, vnode, cookie, openMode, kernel)) >= 0)
		return status;

	// something went wrong, clean up

	FS_CALL(vnode, close)(vnode->mount->cookie, vnode->private_node, cookie);
	FS_CALL(vnode, free_cookie)(vnode->mount->cookie, vnode->private_node, cookie);
	put_vnode(vnode);

	FS_CALL(directory, unlink)(directory->mount->cookie, directory->private_node, name);

	return status;
}


/*!
	Calls fs_open() on the given vnode and returns a new
	file descriptor for it
*/
static int
open_vnode(struct vnode *vnode, int openMode, bool kernel)
{
	fs_cookie cookie;
	int status;

	status = FS_CALL(vnode, open)(vnode->mount->cookie, vnode->private_node, openMode, &cookie);
	if (status < 0)
		return status;

	status = get_new_fd(FDTYPE_FILE, NULL, vnode, cookie, openMode, kernel);
	if (status < 0) {
		FS_CALL(vnode, close)(vnode->mount->cookie, vnode->private_node, cookie);
		FS_CALL(vnode, free_cookie)(vnode->mount->cookie, vnode->private_node, cookie);
	}
	return status;
}


/*! Calls fs open_dir() on the given vnode and returns a new
	file descriptor for it
*/
static int
open_dir_vnode(struct vnode *vnode, bool kernel)
{
	fs_cookie cookie;
	int status;

	status = FS_CALL(vnode, open_dir)(vnode->mount->cookie, vnode->private_node, &cookie);
	if (status < B_OK)
		return status;

	// file is opened, create a fd
	status = get_new_fd(FDTYPE_DIR, NULL, vnode, cookie, 0, kernel);
	if (status >= 0)
		return status;

	FS_CALL(vnode, close_dir)(vnode->mount->cookie, vnode->private_node, cookie);
	FS_CALL(vnode, free_dir_cookie)(vnode->mount->cookie, vnode->private_node, cookie);

	return status;
}


/*! Calls fs open_attr_dir() on the given vnode and returns a new
	file descriptor for it.
	Used by attr_dir_open(), and attr_dir_open_fd().
*/
static int
open_attr_dir_vnode(struct vnode *vnode, bool kernel)
{
	fs_cookie cookie;
	int status;

	if (FS_CALL(vnode, open_attr_dir) == NULL)
		return EOPNOTSUPP;

	status = FS_CALL(vnode, open_attr_dir)(vnode->mount->cookie, vnode->private_node, &cookie);
	if (status < 0)
		return status;

	// file is opened, create a fd
	status = get_new_fd(FDTYPE_ATTR_DIR, NULL, vnode, cookie, 0, kernel);
	if (status >= 0)
		return status;

	FS_CALL(vnode, close_attr_dir)(vnode->mount->cookie, vnode->private_node, cookie);
	FS_CALL(vnode, free_attr_dir_cookie)(vnode->mount->cookie, vnode->private_node, cookie);

	return status;
}


static int
file_create_entry_ref(dev_t mountID, ino_t directoryID, const char *name,
	int openMode, int perms, bool kernel)
{
	struct vnode *directory;
	int status;

	FUNCTION(("file_create_entry_ref: name = '%s', omode %x, perms %d, kernel %d\n", name, openMode, perms, kernel));

	// get directory to put the new file in
	status = get_vnode(mountID, directoryID, &directory, true, false);
	if (status < B_OK)
		return status;

	status = create_vnode(directory, name, openMode, perms, kernel);
	put_vnode(directory);

	return status;
}


static int
file_create(int fd, char *path, int openMode, int perms, bool kernel)
{
	char name[B_FILE_NAME_LENGTH];
	struct vnode *directory;
	int status;

	FUNCTION(("file_create: path '%s', omode %x, perms %d, kernel %d\n", path, openMode, perms, kernel));

	// get directory to put the new file in
	status = fd_and_path_to_dir_vnode(fd, path, &directory, name, kernel);
	if (status < 0)
		return status;

	status = create_vnode(directory, name, openMode, perms, kernel);

	put_vnode(directory);
	return status;
}


static int
file_open_entry_ref(dev_t mountID, ino_t directoryID, const char *name,
	int openMode, bool kernel)
{
	bool traverse = ((openMode & O_NOTRAVERSE) == 0);
	struct vnode *vnode;
	int status;

	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	FUNCTION(("file_open_entry_ref(ref = (%ld, %Ld, %s), openMode = %d)\n",
		mountID, directoryID, name, openMode));

	// get the vnode matching the entry_ref
	status = entry_ref_to_vnode(mountID, directoryID, name, traverse, &vnode);
	if (status < B_OK)
		return status;

	status = open_vnode(vnode, openMode, kernel);
	if (status < B_OK)
		put_vnode(vnode);

	cache_node_opened(vnode, FDTYPE_FILE, vnode->cache, mountID, directoryID,
		vnode->id, name);
	return status;
}


static int
file_open(int fd, char *path, int openMode, bool kernel)
{
	int status = B_OK;
	bool traverse = ((openMode & O_NOTRAVERSE) == 0);

	FUNCTION(("file_open: fd: %d, entry path = '%s', omode %d, kernel %d\n",
		fd, path, openMode, kernel));

	// get the vnode matching the vnode + path combination
	struct vnode *vnode = NULL;
	ino_t parentID;
	status = fd_and_path_to_vnode(fd, path, traverse, &vnode, &parentID, kernel);
	if (status != B_OK)
		return status;

	// open the vnode
	status = open_vnode(vnode, openMode, kernel);
	// put only on error -- otherwise our reference was transferred to the FD
	if (status < B_OK)
		put_vnode(vnode);

	cache_node_opened(vnode, FDTYPE_FILE, vnode->cache,
		vnode->device, parentID, vnode->id, NULL);

	return status;
}


static status_t
file_close(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;
	status_t status = B_OK;

	FUNCTION(("file_close(descriptor = %p)\n", descriptor));

	cache_node_closed(vnode, FDTYPE_FILE, vnode->cache, vnode->device, vnode->id);
	if (FS_CALL(vnode, close))
		status = FS_CALL(vnode, close)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);

	if (status == B_OK) {
		// remove all outstanding locks for this team
		release_advisory_lock(vnode, NULL);
	}
	return status;
}


static void
file_free_fd(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_cookie)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);
		put_vnode(vnode);
	}
}


static status_t
file_read(struct file_descriptor *descriptor, off_t pos, void *buffer, size_t *length)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("file_read: buf %p, pos %Ld, len %p = %ld\n", buffer, pos, length, *length));
	return FS_CALL(vnode, read)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, pos, buffer, length);
}


static status_t
file_write(struct file_descriptor *descriptor, off_t pos, const void *buffer, size_t *length)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("file_write: buf %p, pos %Ld, len %p\n", buffer, pos, length));
	return FS_CALL(vnode, write)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, pos, buffer, length);
}


static off_t
file_seek(struct file_descriptor *descriptor, off_t pos, int seekType)
{
	off_t offset;

	FUNCTION(("file_seek(pos = %Ld, seekType = %d)\n", pos, seekType));

	// stat() the node
	struct vnode *vnode = descriptor->u.vnode;
	if (FS_CALL(vnode, read_stat) == NULL)
		return EOPNOTSUPP;

	struct stat stat;
	status_t status = FS_CALL(vnode, read_stat)(vnode->mount->cookie,
		vnode->private_node, &stat);
	if (status < B_OK)
		return status;

	// some kinds of files are not seekable
	switch (stat.st_mode & S_IFMT) {
		case S_IFIFO:
			return ESPIPE;
// TODO: We don't catch sockets here, but they are not seekable either (ESPIPE)!
		// The Open Group Base Specs don't mention any file types besides pipes,
		// fifos, and sockets specially, so we allow seeking them.
		case S_IFREG:
		case S_IFBLK:
		case S_IFDIR:
		case S_IFLNK:
		case S_IFCHR:
			break;
	}

	switch (seekType) {
		case SEEK_SET:
			offset = 0;
			break;
		case SEEK_CUR:
			offset = descriptor->pos;
			break;
		case SEEK_END:
			offset = stat.st_size;
			break;
		default:
			return B_BAD_VALUE;
	}

	// assumes off_t is 64 bits wide
	if (offset > 0 && LONGLONG_MAX - offset < pos)
		return EOVERFLOW;

	pos += offset;
	if (pos < 0)
		return B_BAD_VALUE;

	return descriptor->pos = pos;
}


static status_t
file_select(struct file_descriptor *descriptor, uint8 event,
	struct selectsync *sync)
{
	FUNCTION(("file_select(%p, %u, %p)\n", descriptor, event, sync));

	struct vnode *vnode = descriptor->u.vnode;

	// If the FS has no select() hook, notify select() now.
	if (FS_CALL(vnode, select) == NULL)
		return notify_select_event(sync, event);

	return FS_CALL(vnode, select)(vnode->mount->cookie, vnode->private_node,
		descriptor->cookie, event, 0, sync);
}


static status_t
file_deselect(struct file_descriptor *descriptor, uint8 event,
	struct selectsync *sync)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (FS_CALL(vnode, deselect) == NULL)
		return B_OK;

	return FS_CALL(vnode, deselect)(vnode->mount->cookie, vnode->private_node,
		descriptor->cookie, event, sync);
}


static status_t
dir_create_entry_ref(dev_t mountID, ino_t parentID, const char *name, int perms, bool kernel)
{
	struct vnode *vnode;
	ino_t newID;
	status_t status;

	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	FUNCTION(("dir_create_entry_ref(dev = %ld, ino = %Ld, name = '%s', perms = %d)\n", mountID, parentID, name, perms));

	status = get_vnode(mountID, parentID, &vnode, true, false);
	if (status < B_OK)
		return status;

	if (FS_CALL(vnode, create_dir))
		status = FS_CALL(vnode, create_dir)(vnode->mount->cookie, vnode->private_node, name, perms, &newID);
	else
		status = EROFS;

	put_vnode(vnode);
	return status;
}


static status_t
dir_create(int fd, char *path, int perms, bool kernel)
{
	char filename[B_FILE_NAME_LENGTH];
	struct vnode *vnode;
	ino_t newID;
	status_t status;

	FUNCTION(("dir_create: path '%s', perms %d, kernel %d\n", path, perms, kernel));

	status = fd_and_path_to_dir_vnode(fd, path, &vnode, filename, kernel);
	if (status < 0)
		return status;

	if (FS_CALL(vnode, create_dir))
		status = FS_CALL(vnode, create_dir)(vnode->mount->cookie, vnode->private_node, filename, perms, &newID);
	else
		status = EROFS;

	put_vnode(vnode);
	return status;
}


static int
dir_open_entry_ref(dev_t mountID, ino_t parentID, const char *name, bool kernel)
{
	struct vnode *vnode;
	int status;

	FUNCTION(("dir_open_entry_ref()\n"));

	if (name && *name == '\0')
		return B_BAD_VALUE;

	// get the vnode matching the entry_ref/node_ref
	if (name)
		status = entry_ref_to_vnode(mountID, parentID, name, true, &vnode);
	else
		status = get_vnode(mountID, parentID, &vnode, true, false);
	if (status < B_OK)
		return status;

	status = open_dir_vnode(vnode, kernel);
	if (status < B_OK)
		put_vnode(vnode);

	cache_node_opened(vnode, FDTYPE_DIR, vnode->cache, mountID, parentID,
		vnode->id, name);
	return status;
}


static int
dir_open(int fd, char *path, bool kernel)
{
	int status = B_OK;

	FUNCTION(("dir_open: fd: %d, entry path = '%s', kernel %d\n", fd, path, kernel));

	// get the vnode matching the vnode + path combination
	struct vnode *vnode = NULL;
	ino_t parentID;
	status = fd_and_path_to_vnode(fd, path, true, &vnode, &parentID, kernel);
	if (status != B_OK)
		return status;

	// open the dir
	status = open_dir_vnode(vnode, kernel);
	if (status < B_OK)
		put_vnode(vnode);

	cache_node_opened(vnode, FDTYPE_DIR, vnode->cache, vnode->device, parentID, vnode->id, NULL);
	return status;
}


static status_t
dir_close(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("dir_close(descriptor = %p)\n", descriptor));

	cache_node_closed(vnode, FDTYPE_DIR, vnode->cache, vnode->device, vnode->id);
	if (FS_CALL(vnode, close_dir))
		return FS_CALL(vnode, close_dir)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);

	return B_OK;
}


static void
dir_free_fd(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_dir_cookie)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);
		put_vnode(vnode);
	}
}


static status_t
dir_read(struct file_descriptor *descriptor, struct dirent *buffer, size_t bufferSize, uint32 *_count)
{
	return dir_read(descriptor->u.vnode, descriptor->cookie, buffer, bufferSize, _count);
}


static void
fix_dirent(struct vnode *parent, struct dirent *entry)
{
	// set d_pdev and d_pino
	entry->d_pdev = parent->device;
	entry->d_pino = parent->id;

	// If this is the ".." entry and the directory is the root of a FS,
	// we need to replace d_dev and d_ino with the actual values.
	if (strcmp(entry->d_name, "..") == 0
		&& parent->mount->root_vnode == parent
		&& parent->mount->covers_vnode) {
		inc_vnode_ref_count(parent);
			// vnode_path_to_vnode() puts the node

		// ".." is guaranteed to to be clobbered by this call
		struct vnode *vnode;
		status_t status = vnode_path_to_vnode(parent, (char*)"..", false, 0,
			&vnode, NULL, NULL);

		if (status == B_OK) {
			entry->d_dev = vnode->device;
			entry->d_ino = vnode->id;
		}
	} else {
		// resolve mount points
		struct vnode *vnode = NULL;
		status_t status = get_vnode(entry->d_dev, entry->d_ino, &vnode, true,
			false);
		if (status != B_OK)
			return;

		mutex_lock(&sVnodeCoveredByMutex);
		if (vnode->covered_by) {
			entry->d_dev = vnode->covered_by->device;
			entry->d_ino = vnode->covered_by->id;
		}
		mutex_unlock(&sVnodeCoveredByMutex);

		put_vnode(vnode);
	}
}


static status_t
dir_read(struct vnode *vnode, fs_cookie cookie, struct dirent *buffer, size_t bufferSize, uint32 *_count)
{
	if (!FS_CALL(vnode, read_dir))
		return EOPNOTSUPP;

	status_t error = FS_CALL(vnode, read_dir)(vnode->mount->cookie,vnode->private_node,cookie,buffer,bufferSize,_count);
	if (error != B_OK)
		return error;

	// we need to adjust the read dirents
	if (*_count > 0) {
		// XXX: Currently reading only one dirent is supported. Make this a loop!
		fix_dirent(vnode, buffer);
	}

	return error;
}


static status_t
dir_rewind(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (FS_CALL(vnode, rewind_dir))
		return FS_CALL(vnode, rewind_dir)(vnode->mount->cookie,vnode->private_node,descriptor->cookie);

	return EOPNOTSUPP;
}


static status_t
dir_remove(int fd, char *path, bool kernel)
{
	char name[B_FILE_NAME_LENGTH];
	struct vnode *directory;
	status_t status;

	if (path != NULL) {
		// we need to make sure our path name doesn't stop with "/", ".", or ".."
		char *lastSlash = strrchr(path, '/');
		if (lastSlash != NULL) {
			char *leaf = lastSlash + 1;
			if (!strcmp(leaf, ".."))
				return B_NOT_ALLOWED;

			// omit multiple slashes
			while (lastSlash > path && lastSlash[-1] == '/') {
				lastSlash--;
			}

			if (!leaf[0]
				|| !strcmp(leaf, ".")) {
				// "name/" -> "name", or "name/." -> "name"
				lastSlash[0] = '\0';
			}
		} else if (!strcmp(path, ".."))
			return B_NOT_ALLOWED;
	}

	status = fd_and_path_to_dir_vnode(fd, path, &directory, name, kernel);
	if (status < B_OK)
		return status;

	if (FS_CALL(directory, remove_dir)) {
		status = FS_CALL(directory, remove_dir)(directory->mount->cookie,
			directory->private_node, name);
	} else
		status = EROFS;

	put_vnode(directory);
	return status;
}


static status_t
common_ioctl(struct file_descriptor *descriptor, ulong op, void *buffer,
	size_t length)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (FS_CALL(vnode, ioctl)) {
		return FS_CALL(vnode, ioctl)(vnode->mount->cookie, vnode->private_node,
			descriptor->cookie, op, buffer, length);
	}

	return EOPNOTSUPP;
}


static status_t
common_fcntl(int fd, int op, uint32 argument, bool kernel)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;
	struct flock flock;

	FUNCTION(("common_fcntl(fd = %d, op = %d, argument = %lx, %s)\n",
		fd, op, argument, kernel ? "kernel" : "user"));

	descriptor = get_fd_and_vnode(fd, &vnode, kernel);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = B_OK;

	if (op == F_SETLK || op == F_SETLKW || op == F_GETLK) {
		if (descriptor->type != FDTYPE_FILE)
			status = B_BAD_VALUE;
		else if (user_memcpy(&flock, (struct flock *)argument,
				sizeof(struct flock)) < B_OK)
			status = B_BAD_ADDRESS;

		if (status != B_OK) {
			put_fd(descriptor);
			return status;
		}
	}

	switch (op) {
		case F_SETFD:
		{
			struct io_context *context = get_current_io_context(kernel);
			// Set file descriptor flags

			// O_CLOEXEC is the only flag available at this time
			mutex_lock(&context->io_mutex);
			fd_set_close_on_exec(context, fd, (argument & FD_CLOEXEC) != 0);
			mutex_unlock(&context->io_mutex);

			status = B_OK;
			break;
		}

		case F_GETFD:
		{
			struct io_context *context = get_current_io_context(kernel);

			// Get file descriptor flags
			mutex_lock(&context->io_mutex);
			status = fd_close_on_exec(context, fd) ? FD_CLOEXEC : 0;
			mutex_unlock(&context->io_mutex);
			break;
		}

		case F_SETFL:
			// Set file descriptor open mode
			if (FS_CALL(vnode, set_flags)) {
				// we only accept changes to O_APPEND and O_NONBLOCK
				argument &= O_APPEND | O_NONBLOCK;

				status = FS_CALL(vnode, set_flags)(vnode->mount->cookie,
					vnode->private_node, descriptor->cookie, (int)argument);
				if (status == B_OK) {
					// update this descriptor's open_mode field
					descriptor->open_mode = (descriptor->open_mode
						& ~(O_APPEND | O_NONBLOCK)) | argument;
				}
			} else
				status = EOPNOTSUPP;
			break;

		case F_GETFL:
			// Get file descriptor open mode
			status = descriptor->open_mode;
			break;

		case F_DUPFD:
		{
			struct io_context *context = get_current_io_context(kernel);

			status = new_fd_etc(context, descriptor, (int)argument);
			if (status >= 0) {
				mutex_lock(&context->io_mutex);
				fd_set_close_on_exec(context, fd, false);
				mutex_unlock(&context->io_mutex);

				atomic_add(&descriptor->ref_count, 1);
			}
			break;
		}

		case F_GETLK:
			status = get_advisory_lock(descriptor->u.vnode, &flock);
			if (status == B_OK) {
				// copy back flock structure
				status = user_memcpy((struct flock *)argument, &flock,
					sizeof(struct flock));
			}
			break;

		case F_SETLK:
		case F_SETLKW:
			status = normalize_flock(descriptor, &flock);
			if (status < B_OK)
				break;

			if (flock.l_type == F_UNLCK)
				status = release_advisory_lock(descriptor->u.vnode, &flock);
			else {
				// the open mode must match the lock type
				if ((descriptor->open_mode & O_RWMASK) == O_RDONLY
						&& flock.l_type == F_WRLCK
					|| (descriptor->open_mode & O_RWMASK) == O_WRONLY
						&& flock.l_type == F_RDLCK)
					status = B_FILE_ERROR;
				else {
					status = acquire_advisory_lock(descriptor->u.vnode, -1,
						&flock, op == F_SETLKW);
				}
			}
			break;

		// ToDo: add support for more ops?

		default:
			status = B_BAD_VALUE;
	}

	put_fd(descriptor);
	return status;
}


static status_t
common_sync(int fd, bool kernel)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;
	status_t status;

	FUNCTION(("common_fsync: entry. fd %d kernel %d\n", fd, kernel));

	descriptor = get_fd_and_vnode(fd, &vnode, kernel);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (FS_CALL(vnode, fsync) != NULL)
		status = FS_CALL(vnode, fsync)(vnode->mount->cookie, vnode->private_node);
	else
		status = EOPNOTSUPP;

	put_fd(descriptor);
	return status;
}


static status_t
common_lock_node(int fd, bool kernel)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, kernel);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = B_OK;

	// We need to set the locking atomically - someone
	// else might set one at the same time
	if (atomic_test_and_set((vint32 *)&vnode->mandatory_locked_by,
			(addr_t)descriptor, (addr_t)NULL) != (addr_t)NULL)
		status = B_BUSY;

	put_fd(descriptor);
	return status;
}


static status_t
common_unlock_node(int fd, bool kernel)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, kernel);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = B_OK;

	// We need to set the locking atomically - someone
	// else might set one at the same time
	if (atomic_test_and_set((vint32 *)&vnode->mandatory_locked_by,
			(addr_t)NULL, (addr_t)descriptor) != (int32)descriptor)
		status = B_BAD_VALUE;

	put_fd(descriptor);
	return status;
}


static status_t
common_read_link(int fd, char *path, char *buffer, size_t *_bufferSize,
	bool kernel)
{
	struct vnode *vnode;
	status_t status;

	status = fd_and_path_to_vnode(fd, path, false, &vnode, NULL, kernel);
	if (status < B_OK)
		return status;

	if (FS_CALL(vnode, read_symlink) != NULL) {
		status = FS_CALL(vnode, read_symlink)(vnode->mount->cookie,
			vnode->private_node, buffer, _bufferSize);
	} else
		status = B_BAD_VALUE;

	put_vnode(vnode);
	return status;
}


static status_t
common_create_symlink(int fd, char *path, const char *toPath, int mode,
	bool kernel)
{
	// path validity checks have to be in the calling function!
	char name[B_FILE_NAME_LENGTH];
	struct vnode *vnode;
	status_t status;

	FUNCTION(("common_create_symlink(fd = %d, path = %s, toPath = %s, mode = %d, kernel = %d)\n", fd, path, toPath, mode, kernel));

	status = fd_and_path_to_dir_vnode(fd, path, &vnode, name, kernel);
	if (status < B_OK)
		return status;

	if (FS_CALL(vnode, create_symlink) != NULL)
		status = FS_CALL(vnode, create_symlink)(vnode->mount->cookie, vnode->private_node, name, toPath, mode);
	else
		status = EROFS;

	put_vnode(vnode);

	return status;
}


static status_t
common_create_link(char *path, char *toPath, bool kernel)
{
	// path validity checks have to be in the calling function!
	char name[B_FILE_NAME_LENGTH];
	struct vnode *directory, *vnode;
	status_t status;

	FUNCTION(("common_create_link(path = %s, toPath = %s, kernel = %d)\n", path, toPath, kernel));

	status = path_to_dir_vnode(path, &directory, name, kernel);
	if (status < B_OK)
		return status;

	status = path_to_vnode(toPath, true, &vnode, NULL, kernel);
	if (status < B_OK)
		goto err;

	if (directory->mount != vnode->mount) {
		status = B_CROSS_DEVICE_LINK;
		goto err1;
	}

	if (FS_CALL(vnode, link) != NULL)
		status = FS_CALL(vnode, link)(directory->mount->cookie, directory->private_node, name, vnode->private_node);
	else
		status = EROFS;

err1:
	put_vnode(vnode);
err:
	put_vnode(directory);

	return status;
}


static status_t
common_unlink(int fd, char *path, bool kernel)
{
	char filename[B_FILE_NAME_LENGTH];
	struct vnode *vnode;
	status_t status;

	FUNCTION(("common_unlink: fd: %d, path '%s', kernel %d\n", fd, path, kernel));

	status = fd_and_path_to_dir_vnode(fd, path, &vnode, filename, kernel);
	if (status < 0)
		return status;

	if (FS_CALL(vnode, unlink) != NULL)
		status = FS_CALL(vnode, unlink)(vnode->mount->cookie, vnode->private_node, filename);
	else
		status = EROFS;

	put_vnode(vnode);

	return status;
}


static status_t
common_access(char *path, int mode, bool kernel)
{
	struct vnode *vnode;
	status_t status;

	status = path_to_vnode(path, true, &vnode, NULL, kernel);
	if (status < B_OK)
		return status;

	if (FS_CALL(vnode, access) != NULL)
		status = FS_CALL(vnode, access)(vnode->mount->cookie, vnode->private_node, mode);
	else
		status = B_OK;

	put_vnode(vnode);

	return status;
}


static status_t
common_rename(int fd, char *path, int newFD, char *newPath, bool kernel)
{
	struct vnode *fromVnode, *toVnode;
	char fromName[B_FILE_NAME_LENGTH];
	char toName[B_FILE_NAME_LENGTH];
	status_t status;

	FUNCTION(("common_rename(fd = %d, path = %s, newFD = %d, newPath = %s, kernel = %d)\n", fd, path, newFD, newPath, kernel));

	status = fd_and_path_to_dir_vnode(fd, path, &fromVnode, fromName, kernel);
	if (status < 0)
		return status;

	status = fd_and_path_to_dir_vnode(newFD, newPath, &toVnode, toName, kernel);
	if (status < 0)
		goto err;

	if (fromVnode->device != toVnode->device) {
		status = B_CROSS_DEVICE_LINK;
		goto err1;
	}

	if (FS_CALL(fromVnode, rename) != NULL)
		status = FS_CALL(fromVnode, rename)(fromVnode->mount->cookie, fromVnode->private_node, fromName, toVnode->private_node, toName);
	else
		status = EROFS;

err1:
	put_vnode(toVnode);
err:
	put_vnode(fromVnode);

	return status;
}


static status_t
common_read_stat(struct file_descriptor *descriptor, struct stat *stat)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("common_read_stat: stat %p\n", stat));

	status_t status = FS_CALL(vnode, read_stat)(vnode->mount->cookie,
		vnode->private_node, stat);

	// fill in the st_dev and st_ino fields
	if (status == B_OK) {
		stat->st_dev = vnode->device;
		stat->st_ino = vnode->id;
	}

	return status;
}


static status_t
common_write_stat(struct file_descriptor *descriptor, const struct stat *stat, int statMask)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("common_write_stat(vnode = %p, stat = %p, statMask = %d)\n", vnode, stat, statMask));
	if (!FS_CALL(vnode, write_stat))
		return EROFS;

	return FS_CALL(vnode, write_stat)(vnode->mount->cookie, vnode->private_node, stat, statMask);
}


static status_t
common_path_read_stat(int fd, char *path, bool traverseLeafLink,
	struct stat *stat, bool kernel)
{
	struct vnode *vnode;
	status_t status;

	FUNCTION(("common_path_read_stat: fd: %d, path '%s', stat %p,\n", fd, path, stat));

	status = fd_and_path_to_vnode(fd, path, traverseLeafLink, &vnode, NULL, kernel);
	if (status < 0)
		return status;

	status = FS_CALL(vnode, read_stat)(vnode->mount->cookie, vnode->private_node, stat);

	// fill in the st_dev and st_ino fields
	if (status == B_OK) {
		stat->st_dev = vnode->device;
		stat->st_ino = vnode->id;
	}

	put_vnode(vnode);
	return status;
}


static status_t
common_path_write_stat(int fd, char *path, bool traverseLeafLink,
	const struct stat *stat, int statMask, bool kernel)
{
	struct vnode *vnode;
	status_t status;

	FUNCTION(("common_write_stat: fd: %d, path '%s', stat %p, stat_mask %d, kernel %d\n", fd, path, stat, statMask, kernel));

	status = fd_and_path_to_vnode(fd, path, traverseLeafLink, &vnode, NULL, kernel);
	if (status < 0)
		return status;

	if (FS_CALL(vnode, write_stat))
		status = FS_CALL(vnode, write_stat)(vnode->mount->cookie, vnode->private_node, stat, statMask);
	else
		status = EROFS;

	put_vnode(vnode);

	return status;
}


static int
attr_dir_open(int fd, char *path, bool kernel)
{
	struct vnode *vnode;
	int status;

	FUNCTION(("attr_dir_open(fd = %d, path = '%s', kernel = %d)\n", fd, path, kernel));

	status = fd_and_path_to_vnode(fd, path, true, &vnode, NULL, kernel);
	if (status < B_OK)
		return status;

	status = open_attr_dir_vnode(vnode, kernel);
	if (status < 0)
		put_vnode(vnode);

	return status;
}


static status_t
attr_dir_close(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_dir_close(descriptor = %p)\n", descriptor));

	if (FS_CALL(vnode, close_attr_dir))
		return FS_CALL(vnode, close_attr_dir)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);

	return B_OK;
}


static void
attr_dir_free_fd(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_attr_dir_cookie)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);
		put_vnode(vnode);
	}
}


static status_t
attr_dir_read(struct file_descriptor *descriptor, struct dirent *buffer, size_t bufferSize, uint32 *_count)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_dir_read(descriptor = %p)\n", descriptor));

	if (FS_CALL(vnode, read_attr_dir))
		return FS_CALL(vnode, read_attr_dir)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, buffer, bufferSize, _count);

	return EOPNOTSUPP;
}


static status_t
attr_dir_rewind(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_dir_rewind(descriptor = %p)\n", descriptor));

	if (FS_CALL(vnode, rewind_attr_dir))
		return FS_CALL(vnode, rewind_attr_dir)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);

	return EOPNOTSUPP;
}


static int
attr_create(int fd, const char *name, uint32 type, int openMode, bool kernel)
{
	struct vnode *vnode;
	fs_cookie cookie;
	int status;

	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	vnode = get_vnode_from_fd(fd, kernel);
	if (vnode == NULL)
		return B_FILE_ERROR;

	if (FS_CALL(vnode, create_attr) == NULL) {
		status = EROFS;
		goto err;
	}

	status = FS_CALL(vnode, create_attr)(vnode->mount->cookie, vnode->private_node, name, type, openMode, &cookie);
	if (status < B_OK)
		goto err;

	if ((status = get_new_fd(FDTYPE_ATTR, NULL, vnode, cookie, openMode, kernel)) >= 0)
		return status;

	FS_CALL(vnode, close_attr)(vnode->mount->cookie, vnode->private_node, cookie);
	FS_CALL(vnode, free_attr_cookie)(vnode->mount->cookie, vnode->private_node, cookie);

	FS_CALL(vnode, remove_attr)(vnode->mount->cookie, vnode->private_node, name);

err:
	put_vnode(vnode);

	return status;
}


static int
attr_open(int fd, const char *name, int openMode, bool kernel)
{
	struct vnode *vnode;
	fs_cookie cookie;
	int status;

	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	vnode = get_vnode_from_fd(fd, kernel);
	if (vnode == NULL)
		return B_FILE_ERROR;

	if (FS_CALL(vnode, open_attr) == NULL) {
		status = EOPNOTSUPP;
		goto err;
	}

	status = FS_CALL(vnode, open_attr)(vnode->mount->cookie, vnode->private_node, name, openMode, &cookie);
	if (status < B_OK)
		goto err;

	// now we only need a file descriptor for this attribute and we're done
	if ((status = get_new_fd(FDTYPE_ATTR, NULL, vnode, cookie, openMode, kernel)) >= 0)
		return status;

	FS_CALL(vnode, close_attr)(vnode->mount->cookie, vnode->private_node, cookie);
	FS_CALL(vnode, free_attr_cookie)(vnode->mount->cookie, vnode->private_node, cookie);

err:
	put_vnode(vnode);

	return status;
}


static status_t
attr_close(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_close(descriptor = %p)\n", descriptor));

	if (FS_CALL(vnode, close_attr))
		return FS_CALL(vnode, close_attr)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);

	return B_OK;
}


static void
attr_free_fd(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_attr_cookie)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);
		put_vnode(vnode);
	}
}


static status_t
attr_read(struct file_descriptor *descriptor, off_t pos, void *buffer, size_t *length)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_read: buf %p, pos %Ld, len %p = %ld\n", buffer, pos, length, *length));
	if (!FS_CALL(vnode, read_attr))
		return EOPNOTSUPP;

	return FS_CALL(vnode, read_attr)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, pos, buffer, length);
}


static status_t
attr_write(struct file_descriptor *descriptor, off_t pos, const void *buffer, size_t *length)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_write: buf %p, pos %Ld, len %p\n", buffer, pos, length));
	if (!FS_CALL(vnode, write_attr))
		return EOPNOTSUPP;

	return FS_CALL(vnode, write_attr)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, pos, buffer, length);
}


static off_t
attr_seek(struct file_descriptor *descriptor, off_t pos, int seekType)
{
	off_t offset;

	switch (seekType) {
		case SEEK_SET:
			offset = 0;
			break;
		case SEEK_CUR:
			offset = descriptor->pos;
			break;
		case SEEK_END:
		{
			struct vnode *vnode = descriptor->u.vnode;
			struct stat stat;
			status_t status;

			if (FS_CALL(vnode, read_stat) == NULL)
				return EOPNOTSUPP;

			status = FS_CALL(vnode, read_attr_stat)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, &stat);
			if (status < B_OK)
				return status;

			offset = stat.st_size;
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	// assumes off_t is 64 bits wide
	if (offset > 0 && LONGLONG_MAX - offset < pos)
		return EOVERFLOW;

	pos += offset;
	if (pos < 0)
		return B_BAD_VALUE;

	return descriptor->pos = pos;
}


static status_t
attr_read_stat(struct file_descriptor *descriptor, struct stat *stat)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_read_stat: stat 0x%p\n", stat));

	if (!FS_CALL(vnode, read_attr_stat))
		return EOPNOTSUPP;

	return FS_CALL(vnode, read_attr_stat)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, stat);
}


static status_t
attr_write_stat(struct file_descriptor *descriptor, const struct stat *stat, int statMask)
{
	struct vnode *vnode = descriptor->u.vnode;

	FUNCTION(("attr_write_stat: stat = %p, statMask %d\n", stat, statMask));

	if (!FS_CALL(vnode, write_attr_stat))
		return EROFS;

	return FS_CALL(vnode, write_attr_stat)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, stat, statMask);
}


static status_t
attr_remove(int fd, const char *name, bool kernel)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;
	status_t status;

	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	FUNCTION(("attr_remove: fd = %d, name = \"%s\", kernel %d\n", fd, name, kernel));

	descriptor = get_fd_and_vnode(fd, &vnode, kernel);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (FS_CALL(vnode, remove_attr))
		status = FS_CALL(vnode, remove_attr)(vnode->mount->cookie, vnode->private_node, name);
	else
		status = EROFS;

	put_fd(descriptor);

	return status;
}


static status_t
attr_rename(int fromfd, const char *fromName, int tofd, const char *toName, bool kernel)
{
	struct file_descriptor *fromDescriptor, *toDescriptor;
	struct vnode *fromVnode, *toVnode;
	status_t status;

	if (fromName == NULL || *fromName == '\0' || toName == NULL || *toName == '\0')
		return B_BAD_VALUE;

	FUNCTION(("attr_rename: from fd = %d, from name = \"%s\", to fd = %d, to name = \"%s\", kernel %d\n", fromfd, fromName, tofd, toName, kernel));

	fromDescriptor = get_fd_and_vnode(fromfd, &fromVnode, kernel);
	if (fromDescriptor == NULL)
		return B_FILE_ERROR;

	toDescriptor = get_fd_and_vnode(tofd, &toVnode, kernel);
	if (toDescriptor == NULL) {
		status = B_FILE_ERROR;
		goto err;
	}

	// are the files on the same volume?
	if (fromVnode->device != toVnode->device) {
		status = B_CROSS_DEVICE_LINK;
		goto err1;
	}

	if (FS_CALL(fromVnode, rename_attr))
		status = FS_CALL(fromVnode, rename_attr)(fromVnode->mount->cookie, fromVnode->private_node, fromName, toVnode->private_node, toName);
	else
		status = EROFS;

err1:
	put_fd(toDescriptor);
err:
	put_fd(fromDescriptor);

	return status;
}


static status_t
index_dir_open(dev_t mountID, bool kernel)
{
	struct fs_mount *mount;
	fs_cookie cookie;

	FUNCTION(("index_dir_open(mountID = %ld, kernel = %d)\n", mountID, kernel));

	status_t status = get_mount(mountID, &mount);
	if (status < B_OK)
		return status;

	if (FS_MOUNT_CALL(mount, open_index_dir) == NULL) {
		status = EOPNOTSUPP;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, open_index_dir)(mount->cookie, &cookie);
	if (status < B_OK)
		goto out;

	// get fd for the index directory
	status = get_new_fd(FDTYPE_INDEX_DIR, mount, NULL, cookie, 0, kernel);
	if (status >= 0)
		goto out;

	// something went wrong
	FS_MOUNT_CALL(mount, close_index_dir)(mount->cookie, cookie);
	FS_MOUNT_CALL(mount, free_index_dir_cookie)(mount->cookie, cookie);

out:
	put_mount(mount);
	return status;
}


static status_t
index_dir_close(struct file_descriptor *descriptor)
{
	struct fs_mount *mount = descriptor->u.mount;

	FUNCTION(("index_dir_close(descriptor = %p)\n", descriptor));

	if (FS_MOUNT_CALL(mount, close_index_dir))
		return FS_MOUNT_CALL(mount, close_index_dir)(mount->cookie, descriptor->cookie);

	return B_OK;
}


static void
index_dir_free_fd(struct file_descriptor *descriptor)
{
	struct fs_mount *mount = descriptor->u.mount;

	if (mount != NULL) {
		FS_MOUNT_CALL(mount, free_index_dir_cookie)(mount->cookie, descriptor->cookie);
		// ToDo: find a replacement ref_count object - perhaps the root dir?
		//put_vnode(vnode);
	}
}


static status_t
index_dir_read(struct file_descriptor *descriptor, struct dirent *buffer, size_t bufferSize, uint32 *_count)
{
	struct fs_mount *mount = descriptor->u.mount;

	if (FS_MOUNT_CALL(mount, read_index_dir))
		return FS_MOUNT_CALL(mount, read_index_dir)(mount->cookie, descriptor->cookie, buffer, bufferSize, _count);

	return EOPNOTSUPP;
}


static status_t
index_dir_rewind(struct file_descriptor *descriptor)
{
	struct fs_mount *mount = descriptor->u.mount;

	if (FS_MOUNT_CALL(mount, rewind_index_dir))
		return FS_MOUNT_CALL(mount, rewind_index_dir)(mount->cookie, descriptor->cookie);

	return EOPNOTSUPP;
}


static status_t
index_create(dev_t mountID, const char *name, uint32 type, uint32 flags, bool kernel)
{
	FUNCTION(("index_create(mountID = %ld, name = %s, kernel = %d)\n", mountID, name, kernel));

	struct fs_mount *mount;
	status_t status = get_mount(mountID, &mount);
	if (status < B_OK)
		return status;

	if (FS_MOUNT_CALL(mount, create_index) == NULL) {
		status = EROFS;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, create_index)(mount->cookie, name, type, flags);

out:
	put_mount(mount);
	return status;
}


#if 0
static status_t
index_read_stat(struct file_descriptor *descriptor, struct stat *stat)
{
	struct vnode *vnode = descriptor->u.vnode;

	// ToDo: currently unused!
	FUNCTION(("index_read_stat: stat 0x%p\n", stat));
	if (!FS_CALL(vnode, read_index_stat))
		return EOPNOTSUPP;

	return EOPNOTSUPP;
	//return FS_CALL(vnode, read_index_stat)(vnode->mount->cookie, vnode->private_node, descriptor->cookie, stat);
}


static void
index_free_fd(struct file_descriptor *descriptor)
{
	struct vnode *vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_index_cookie)(vnode->mount->cookie, vnode->private_node, descriptor->cookie);
		put_vnode(vnode);
	}
}
#endif


static status_t
index_name_read_stat(dev_t mountID, const char *name, struct stat *stat, bool kernel)
{
	FUNCTION(("index_remove(mountID = %ld, name = %s, kernel = %d)\n", mountID, name, kernel));

	struct fs_mount *mount;
	status_t status = get_mount(mountID, &mount);
	if (status < B_OK)
		return status;

	if (FS_MOUNT_CALL(mount, read_index_stat) == NULL) {
		status = EOPNOTSUPP;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, read_index_stat)(mount->cookie, name, stat);

out:
	put_mount(mount);
	return status;
}


static status_t
index_remove(dev_t mountID, const char *name, bool kernel)
{
	FUNCTION(("index_remove(mountID = %ld, name = %s, kernel = %d)\n", mountID, name, kernel));

	struct fs_mount *mount;
	status_t status = get_mount(mountID, &mount);
	if (status < B_OK)
		return status;

	if (FS_MOUNT_CALL(mount, remove_index) == NULL) {
		status = EROFS;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, remove_index)(mount->cookie, name);

out:
	put_mount(mount);
	return status;
}


/*!	ToDo: the query FS API is still the pretty much the same as in R5.
		It would be nice if the FS would find some more kernel support
		for them.
		For example, query parsing should be moved into the kernel.
*/
static int
query_open(dev_t device, const char *query, uint32 flags,
	port_id port, int32 token, bool kernel)
{
	struct fs_mount *mount;
	fs_cookie cookie;

	FUNCTION(("query_open(device = %ld, query = \"%s\", kernel = %d)\n", device, query, kernel));

	status_t status = get_mount(device, &mount);
	if (status < B_OK)
		return status;

	if (FS_MOUNT_CALL(mount, open_query) == NULL) {
		status = EOPNOTSUPP;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, open_query)(mount->cookie, query, flags, port, token, &cookie);
	if (status < B_OK)
		goto out;

	// get fd for the index directory
	status = get_new_fd(FDTYPE_QUERY, mount, NULL, cookie, 0, kernel);
	if (status >= 0)
		goto out;

	// something went wrong
	FS_MOUNT_CALL(mount, close_query)(mount->cookie, cookie);
	FS_MOUNT_CALL(mount, free_query_cookie)(mount->cookie, cookie);

out:
	put_mount(mount);
	return status;
}


static status_t
query_close(struct file_descriptor *descriptor)
{
	struct fs_mount *mount = descriptor->u.mount;

	FUNCTION(("query_close(descriptor = %p)\n", descriptor));

	if (FS_MOUNT_CALL(mount, close_query))
		return FS_MOUNT_CALL(mount, close_query)(mount->cookie, descriptor->cookie);

	return B_OK;
}


static void
query_free_fd(struct file_descriptor *descriptor)
{
	struct fs_mount *mount = descriptor->u.mount;

	if (mount != NULL) {
		FS_MOUNT_CALL(mount, free_query_cookie)(mount->cookie, descriptor->cookie);
		// ToDo: find a replacement ref_count object - perhaps the root dir?
		//put_vnode(vnode);
	}
}


static status_t
query_read(struct file_descriptor *descriptor, struct dirent *buffer, size_t bufferSize, uint32 *_count)
{
	struct fs_mount *mount = descriptor->u.mount;

	if (FS_MOUNT_CALL(mount, read_query))
		return FS_MOUNT_CALL(mount, read_query)(mount->cookie, descriptor->cookie, buffer, bufferSize, _count);

	return EOPNOTSUPP;
}


static status_t
query_rewind(struct file_descriptor *descriptor)
{
	struct fs_mount *mount = descriptor->u.mount;

	if (FS_MOUNT_CALL(mount, rewind_query))
		return FS_MOUNT_CALL(mount, rewind_query)(mount->cookie, descriptor->cookie);

	return EOPNOTSUPP;
}


//	#pragma mark - General File System functions


static dev_t
fs_mount(char *path, const char *device, const char *fsName, uint32 flags,
	const char *args, bool kernel)
{
	struct fs_mount *mount;
	status_t status = 0;

	FUNCTION(("fs_mount: entry. path = '%s', fs_name = '%s'\n", path, fsName));

	// The path is always safe, we just have to make sure that fsName is
	// almost valid - we can't make any assumptions about args, though.
	// A NULL fsName is OK, if a device was given and the FS is not virtual.
	// We'll get it from the DDM later.
	if (fsName == NULL) {
		if (!device || flags & B_MOUNT_VIRTUAL_DEVICE)
			return B_BAD_VALUE;
	} else if (fsName[0] == '\0')
		return B_BAD_VALUE;

	RecursiveLocker mountOpLocker(sMountOpLock);

	// Helper to delete a newly created file device on failure.
	// Not exactly beautiful, but helps to keep the code below cleaner.
	struct FileDeviceDeleter {
		FileDeviceDeleter() : id(-1) {}
		~FileDeviceDeleter()
		{
			KDiskDeviceManager::Default()->DeleteFileDevice(id);
		}

		partition_id id;
	} fileDeviceDeleter;

	// If the file system is not a "virtual" one, the device argument should
	// point to a real file/device (if given at all).
	// get the partition
	KDiskDeviceManager *ddm = KDiskDeviceManager::Default();
	KPartition *partition = NULL;
	KPath normalizedDevice;
	bool newlyCreatedFileDevice = false;

	if (!(flags & B_MOUNT_VIRTUAL_DEVICE) && device) {
		// normalize the device path
		status = normalizedDevice.SetTo(device, true);
		if (status != B_OK)
			return status;

		// get a corresponding partition from the DDM
		partition = ddm->RegisterPartition(normalizedDevice.Path());

		if (!partition) {
			// Partition not found: This either means, the user supplied
			// an invalid path, or the path refers to an image file. We try
			// to let the DDM create a file device for the path.
			partition_id deviceID = ddm->CreateFileDevice(normalizedDevice.Path(),
				&newlyCreatedFileDevice);
			if (deviceID >= 0) {
				partition = ddm->RegisterPartition(deviceID);
				if (newlyCreatedFileDevice)
					fileDeviceDeleter.id = deviceID;
			}
		}

		if (!partition) {
			TRACE(("fs_mount(): Partition `%s' not found.\n",
				normalizedDevice.Path()));
			return B_ENTRY_NOT_FOUND;
		}

		device = normalizedDevice.Path();
			// correct path to file device
	}
	PartitionRegistrar partitionRegistrar(partition, true);

	// Write lock the partition's device. For the time being, we keep the lock
	// until we're done mounting -- not nice, but ensure, that no-one is
	// interfering.
	// TODO: Find a better solution.
	KDiskDevice *diskDevice = NULL;
	if (partition) {
		diskDevice = ddm->WriteLockDevice(partition->Device()->ID());
		if (!diskDevice) {
			TRACE(("fs_mount(): Failed to lock disk device!\n"));
			return B_ERROR;
		}
	}

	DeviceWriteLocker writeLocker(diskDevice, true);
		// this takes over the write lock acquired before

	if (partition) {
		// make sure, that the partition is not busy
		if (partition->IsBusy()) {
			TRACE(("fs_mount(): Partition is busy.\n"));
			return B_BUSY;
		}

		// if no FS name had been supplied, we get it from the partition
		if (!fsName) {
			KDiskSystem *diskSystem = partition->DiskSystem();
			if (!diskSystem) {
				TRACE(("fs_mount(): No FS name was given, and the DDM didn't "
					"recognize it.\n"));
				return B_BAD_VALUE;
			}

			if (!diskSystem->IsFileSystem()) {
				TRACE(("fs_mount(): No FS name was given, and the DDM found a "
					"partitioning system.\n"));
				return B_BAD_VALUE;
			}

			// The disk system name will not change, and the KDiskSystem
			// object will not go away while the disk device is locked (and
			// the partition has a reference to it), so this is safe.
			fsName = diskSystem->Name();
		}
	}

	mount = (struct fs_mount *)malloc(sizeof(struct fs_mount));
	if (mount == NULL)
		return B_NO_MEMORY;

	list_init_etc(&mount->vnodes, offsetof(struct vnode, mount_link));

	mount->fs_name = get_file_system_name(fsName);
	if (mount->fs_name == NULL) {
		status = B_NO_MEMORY;
		goto err1;
	}

	mount->device_name = strdup(device);
		// "device" can be NULL

	mount->fs = get_file_system(fsName);
	if (mount->fs == NULL) {
		status = ENODEV;
		goto err3;
	}

	status = recursive_lock_init(&mount->rlock, "mount rlock");
	if (status < B_OK)
		goto err4;

	// initialize structure
	mount->id = sNextMountID++;
	mount->partition = NULL;
	mount->root_vnode = NULL;
	mount->covers_vnode = NULL;
	mount->cookie = NULL;
	mount->unmounting = false;
	mount->owns_file_device = false;

	// insert mount struct into list before we call FS's mount() function
	// so that vnodes can be created for this mount
	mutex_lock(&sMountMutex);
	hash_insert(sMountsTable, mount);
	mutex_unlock(&sMountMutex);

	ino_t rootID;

	if (!sRoot) {
		// we haven't mounted anything yet
		if (strcmp(path, "/") != 0) {
			status = B_ERROR;
			goto err5;
		}

		status = FS_MOUNT_CALL(mount, mount)(mount->id, device, flags, args, &mount->cookie, &rootID);
		if (status < 0) {
			// ToDo: why should we hide the error code from the file system here?
			//status = ERR_VFS_GENERAL;
			goto err5;
		}
	} else {
		struct vnode *coveredVnode;
		status = path_to_vnode(path, true, &coveredVnode, NULL, kernel);
		if (status < B_OK)
			goto err5;

		// make sure covered_vnode is a DIR
		struct stat coveredNodeStat;
		status = FS_CALL(coveredVnode, read_stat)(coveredVnode->mount->cookie,
			coveredVnode->private_node, &coveredNodeStat);
		if (status < B_OK)
			goto err5;

		if (!S_ISDIR(coveredNodeStat.st_mode)) {
			status = B_NOT_A_DIRECTORY;
			goto err5;
		}

		if (coveredVnode->mount->root_vnode == coveredVnode) {
			// this is already a mount point
			status = B_BUSY;
			goto err5;
		}

		mount->covers_vnode = coveredVnode;

		// mount it
		status = FS_MOUNT_CALL(mount, mount)(mount->id, device, flags, args, &mount->cookie, &rootID);
		if (status < B_OK)
			goto err6;
	}

	// the root node is supposed to be owned by the file system - it must
	// exist at this point
	mount->root_vnode = lookup_vnode(mount->id, rootID);
	if (mount->root_vnode == NULL || mount->root_vnode->ref_count != 1) {
		panic("fs_mount: file system does not own its root node!\n");
		status = B_ERROR;
		goto err7;
	}

	// No race here, since fs_mount() is the only function changing
	// covers_vnode (and holds sMountOpLock at that time).
	mutex_lock(&sVnodeCoveredByMutex);
	if (mount->covers_vnode)
		mount->covers_vnode->covered_by = mount->root_vnode;
	mutex_unlock(&sVnodeCoveredByMutex);

	if (!sRoot)
		sRoot = mount->root_vnode;

	// supply the partition (if any) with the mount cookie and mark it mounted
	if (partition) {
		partition->SetMountCookie(mount->cookie);
		partition->SetVolumeID(mount->id);

		// keep a partition reference as long as the partition is mounted
		partitionRegistrar.Detach();
		mount->partition = partition;
		mount->owns_file_device = newlyCreatedFileDevice;
		fileDeviceDeleter.id = -1;
	}

	notify_mount(mount->id, mount->covers_vnode ? mount->covers_vnode->device : -1,
		mount->covers_vnode ? mount->covers_vnode->id : -1);

	return mount->id;

err7:
	FS_MOUNT_CALL(mount, unmount)(mount->cookie);
err6:
	if (mount->covers_vnode)
		put_vnode(mount->covers_vnode);
err5:
	mutex_lock(&sMountMutex);
	hash_remove(sMountsTable, mount);
	mutex_unlock(&sMountMutex);

	recursive_lock_destroy(&mount->rlock);
err4:
	put_file_system(mount->fs);
	free(mount->device_name);
err3:
	free(mount->fs_name);
err1:
	free(mount);

	return status;
}


static status_t
fs_unmount(char *path, dev_t mountID, uint32 flags, bool kernel)
{
	struct vnode *vnode = NULL;
	struct fs_mount *mount;
	status_t err;

	FUNCTION(("fs_unmount(path '%s', dev %ld, kernel %d\n", path, mountID,
		kernel));

	if (path != NULL) {
		err = path_to_vnode(path, true, &vnode, NULL, kernel);
		if (err != B_OK)
			return B_ENTRY_NOT_FOUND;
	}

	RecursiveLocker mountOpLocker(sMountOpLock);

	mount = find_mount(path != NULL ? vnode->device : mountID);
	if (mount == NULL) {
		panic("fs_unmount: find_mount() failed on root vnode @%p of mount\n",
			vnode);
	}

	if (path != NULL) {
		put_vnode(vnode);

		if (mount->root_vnode != vnode) {
			// not mountpoint
			return B_BAD_VALUE;
		}
	}

	// if the volume is associated with a partition, lock the device of the
	// partition as long as we are unmounting
	KDiskDeviceManager* ddm = KDiskDeviceManager::Default();
	KPartition *partition = mount->partition;
	KDiskDevice *diskDevice = NULL;
	if (partition) {
		if (partition->Device() == NULL) {
			dprintf("fs_unmount(): There is no device!\n");
			return B_ERROR;
		}
		diskDevice = ddm->WriteLockDevice(partition->Device()->ID());
		if (!diskDevice) {
			TRACE(("fs_unmount(): Failed to lock disk device!\n"));
			return B_ERROR;
		}
	}
	DeviceWriteLocker writeLocker(diskDevice, true);

	// make sure, that the partition is not busy
	if (partition) {
		if ((flags & B_UNMOUNT_BUSY_PARTITION) == 0 && partition->IsBusy()) {
			TRACE(("fs_unmount(): Partition is busy.\n"));
			return B_BUSY;
		}
	}

	// grab the vnode master mutex to keep someone from creating
	// a vnode while we're figuring out if we can continue
	mutex_lock(&sVnodeMutex);

	bool disconnectedDescriptors = false;

	while (true) {
		bool busy = false;

		// cycle through the list of vnodes associated with this mount and
		// make sure all of them are not busy or have refs on them
		vnode = NULL;
		while ((vnode = (struct vnode *)list_get_next_item(&mount->vnodes,
				vnode)) != NULL) {
			// The root vnode ref_count needs to be 1 here (the mount has a
			// reference).
			if (vnode->busy
				|| ((vnode->ref_count != 0 && mount->root_vnode != vnode)
					|| (vnode->ref_count != 1 && mount->root_vnode == vnode))) {
				// there are still vnodes in use on this mount, so we cannot
				// unmount yet
				busy = true;
				break;
			}
		}

		if (!busy)
			break;

		if ((flags & B_FORCE_UNMOUNT) == 0) {
			mutex_unlock(&sVnodeMutex);
			put_vnode(mount->root_vnode);

			return B_BUSY;
		}

		if (disconnectedDescriptors) {
			// wait a bit until the last access is finished, and then try again
			mutex_unlock(&sVnodeMutex);
			snooze(100000);
			// TODO: if there is some kind of bug that prevents the ref counts
			//	from getting back to zero, this will fall into an endless loop...
			mutex_lock(&sVnodeMutex);
			continue;
		}

		// the file system is still busy - but we're forced to unmount it,
		// so let's disconnect all open file descriptors

		mount->unmounting = true;
			// prevent new vnodes from being created

		mutex_unlock(&sVnodeMutex);

		disconnect_mount_or_vnode_fds(mount, NULL);
		disconnectedDescriptors = true;

		mutex_lock(&sVnodeMutex);
	}

	// we can safely continue, mark all of the vnodes busy and this mount
	// structure in unmounting state
	mount->unmounting = true;

	while ((vnode = (struct vnode *)list_get_next_item(&mount->vnodes, vnode)) != NULL) {
		vnode->busy = true;

		if (vnode->ref_count == 0) {
			// this vnode has been unused before
			list_remove_item(&sUnusedVnodeList, vnode);
			sUnusedVnodes--;
		}
	}

	// The ref_count of the root node is 1 at this point, see above why this is
	mount->root_vnode->ref_count--;

	mutex_unlock(&sVnodeMutex);

	mutex_lock(&sVnodeCoveredByMutex);
	mount->covers_vnode->covered_by = NULL;
	mutex_unlock(&sVnodeCoveredByMutex);
	put_vnode(mount->covers_vnode);

	// Free all vnodes associated with this mount.
	// They will be removed from the mount list by free_vnode(), so
	// we don't have to do this.
	while ((vnode = (struct vnode *)list_get_first_item(&mount->vnodes))
			!= NULL) {
		free_vnode(vnode, false);
	}

	// remove the mount structure from the hash table
	mutex_lock(&sMountMutex);
	hash_remove(sMountsTable, mount);
	mutex_unlock(&sMountMutex);

	mountOpLocker.Unlock();

	FS_MOUNT_CALL(mount, unmount)(mount->cookie);
	notify_unmount(mount->id);

	// release the file system
	put_file_system(mount->fs);

	// dereference the partition and mark it unmounted
	if (partition) {
		partition->SetVolumeID(-1);
		partition->SetMountCookie(NULL);

		if (mount->owns_file_device)
			KDiskDeviceManager::Default()->DeleteFileDevice(partition->ID());
		partition->Unregister();
	}

	free(mount->device_name);
	free(mount->fs_name);
	free(mount);

	return B_OK;
}


static status_t
fs_sync(dev_t device)
{
	struct fs_mount *mount;
	status_t status = get_mount(device, &mount);
	if (status < B_OK)
		return status;

	// First, synchronize all file caches

	struct vnode *previousVnode = NULL;
	while (true) {
		// synchronize access to vnode list
		recursive_lock_lock(&mount->rlock);

		struct vnode *vnode = previousVnode;
		do {
			// TODO: we could track writes (and writable mapped vnodes)
			//	and have a simple flag that we could test for here
			vnode = (struct vnode *)list_get_next_item(&mount->vnodes, vnode);
		} while (vnode != NULL && vnode->cache == NULL);

		ino_t id = -1;
		if (vnode != NULL)
			id = vnode->id;

		recursive_lock_unlock(&mount->rlock);

		if (vnode == NULL)
			break;

		// acquire a reference to the vnode

		if (get_vnode(mount->id, id, &vnode, true, false) == B_OK) {
			if (previousVnode != NULL)
				put_vnode(previousVnode);

			if (vnode->cache != NULL)
				vm_cache_write_modified(vnode->cache, false);

			// the next vnode might change until we lock the vnode list again,
			// but this vnode won't go away since we keep a reference to it.
			previousVnode = vnode;
		} else {
			dprintf("syncing of mount %ld stopped due to vnode %Ld.\n",
				mount->id, id);
			break;
		}
	}

	if (previousVnode != NULL)
		put_vnode(previousVnode);

	// And then, let the file systems do their synchronizing work

	mutex_lock(&sMountMutex);

	if (FS_MOUNT_CALL(mount, sync))
		status = FS_MOUNT_CALL(mount, sync)(mount->cookie);

	mutex_unlock(&sMountMutex);

	put_mount(mount);
	return status;
}


static status_t
fs_read_info(dev_t device, struct fs_info *info)
{
	struct fs_mount *mount;
	status_t status = get_mount(device, &mount);
	if (status < B_OK)
		return status;

	memset(info, 0, sizeof(struct fs_info));

	if (FS_MOUNT_CALL(mount, read_fs_info))
		status = FS_MOUNT_CALL(mount, read_fs_info)(mount->cookie, info);

	// fill in info the file system doesn't (have to) know about
	if (status == B_OK) {
		info->dev = mount->id;
		info->root = mount->root_vnode->id;
		strlcpy(info->fsh_name, mount->fs_name, sizeof(info->fsh_name));
		if (mount->device_name != NULL) {
			strlcpy(info->device_name, mount->device_name,
				sizeof(info->device_name));
		}
	}

	// if the call is not supported by the file system, there are still
	// the parts that we filled out ourselves

	put_mount(mount);
	return status;
}


static status_t
fs_write_info(dev_t device, const struct fs_info *info, int mask)
{
	struct fs_mount *mount;
	status_t status = get_mount(device, &mount);
	if (status < B_OK)
		return status;

	if (FS_MOUNT_CALL(mount, write_fs_info))
		status = FS_MOUNT_CALL(mount, write_fs_info)(mount->cookie, info, mask);
	else
		status = EROFS;

	put_mount(mount);
	return status;
}


static dev_t
fs_next_device(int32 *_cookie)
{
	struct fs_mount *mount = NULL;
	dev_t device = *_cookie;

	mutex_lock(&sMountMutex);

	// Since device IDs are assigned sequentially, this algorithm
	// does work good enough. It makes sure that the device list
	// returned is sorted, and that no device is skipped when an
	// already visited device got unmounted.

	while (device < sNextMountID) {
		mount = find_mount(device++);
		if (mount != NULL && mount->cookie != NULL)
			break;
	}

	*_cookie = device;

	if (mount != NULL)
		device = mount->id;
	else
		device = B_BAD_VALUE;

	mutex_unlock(&sMountMutex);

	return device;
}


static status_t
get_cwd(char *buffer, size_t size, bool kernel)
{
	// Get current working directory from io context
	struct io_context *context = get_current_io_context(kernel);
	status_t status;

	FUNCTION(("vfs_get_cwd: buf %p, size %ld\n", buffer, size));

	mutex_lock(&context->io_mutex);

	if (context->cwd)
		status = dir_vnode_to_path(context->cwd, buffer, size);
	else
		status = B_ERROR;

	mutex_unlock(&context->io_mutex);
	return status;
}


static status_t
set_cwd(int fd, char *path, bool kernel)
{
	struct io_context *context;
	struct vnode *vnode = NULL;
	struct vnode *oldDirectory;
	struct stat stat;
	status_t status;

	FUNCTION(("set_cwd: path = \'%s\'\n", path));

	// Get vnode for passed path, and bail if it failed
	status = fd_and_path_to_vnode(fd, path, true, &vnode, NULL, kernel);
	if (status < 0)
		return status;

	status = FS_CALL(vnode, read_stat)(vnode->mount->cookie, vnode->private_node, &stat);
	if (status < 0)
		goto err;

	if (!S_ISDIR(stat.st_mode)) {
		// nope, can't cwd to here
		status = B_NOT_A_DIRECTORY;
		goto err;
	}

	// Get current io context and lock
	context = get_current_io_context(kernel);
	mutex_lock(&context->io_mutex);

	// save the old current working directory first
	oldDirectory = context->cwd;
	context->cwd = vnode;

	mutex_unlock(&context->io_mutex);

	if (oldDirectory)
		put_vnode(oldDirectory);

	return B_NO_ERROR;

err:
	put_vnode(vnode);
	return status;
}


//	#pragma mark - kernel mirrored syscalls


dev_t
_kern_mount(const char *path, const char *device, const char *fsName,
	uint32 flags, const char *args, size_t argsLength)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return fs_mount(pathBuffer.LockBuffer(), device, fsName, flags, args, true);
}


status_t
_kern_unmount(const char *path, uint32 flags)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return fs_unmount(pathBuffer.LockBuffer(), -1, flags, true);
}


status_t
_kern_read_fs_info(dev_t device, struct fs_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	return fs_read_info(device, info);
}


status_t
_kern_write_fs_info(dev_t device, const struct fs_info *info, int mask)
{
	if (info == NULL)
		return B_BAD_VALUE;

	return fs_write_info(device, info, mask);
}


status_t
_kern_sync(void)
{
	// Note: _kern_sync() is also called from _user_sync()
	int32 cookie = 0;
	dev_t device;
	while ((device = next_dev(&cookie)) >= 0) {
		status_t status = fs_sync(device);
		if (status != B_OK && status != B_BAD_VALUE)
			dprintf("sync: device %ld couldn't sync: %s\n", device, strerror(status));
	}

	return B_OK;
}


dev_t
_kern_next_device(int32 *_cookie)
{
	return fs_next_device(_cookie);
}


status_t
_kern_get_next_fd_info(team_id teamID, uint32 *_cookie, fd_info *info,
	size_t infoSize)
{
	if (infoSize != sizeof(fd_info))
		return B_BAD_VALUE;

	struct io_context *context = NULL;
	sem_id contextMutex = -1;
	struct team *team = NULL;

	cpu_status state = disable_interrupts();
	GRAB_TEAM_LOCK();

	team = team_get_team_struct_locked(teamID);
	if (team) {
		context = (io_context *)team->io_context;
		contextMutex = context->io_mutex.sem;
	}

	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	// we now have a context - since we couldn't lock it while having
	// safe access to the team structure, we now need to lock the mutex
	// manually

	if (context == NULL || acquire_sem(contextMutex) != B_OK) {
		// team doesn't exit or seems to be gone
		return B_BAD_TEAM_ID;
	}

	// the team cannot be deleted completely while we're owning its
	// io_context mutex, so we can safely play with it now

	context->io_mutex.holder = thread_get_current_thread_id();

	uint32 slot = *_cookie;

	struct file_descriptor *descriptor;
	while (slot < context->table_size && (descriptor = context->fds[slot]) == NULL)
		slot++;

	if (slot >= context->table_size) {
		mutex_unlock(&context->io_mutex);
		return B_ENTRY_NOT_FOUND;
	}

	info->number = slot;
	info->open_mode = descriptor->open_mode;

	struct vnode *vnode = fd_vnode(descriptor);
	if (vnode != NULL) {
		info->device = vnode->device;
		info->node = vnode->id;
	} else if (descriptor->u.mount != NULL) {
		info->device = descriptor->u.mount->id;
		info->node = -1;
	}

	mutex_unlock(&context->io_mutex);

	*_cookie = slot + 1;
	return B_OK;
}


int
_kern_open_entry_ref(dev_t device, ino_t inode, const char *name, int openMode, int perms)
{
	if (openMode & O_CREAT)
		return file_create_entry_ref(device, inode, name, openMode, perms, true);

	return file_open_entry_ref(device, inode, name, openMode, true);
}


/*!	\brief Opens a node specified by a FD + path pair.

	At least one of \a fd and \a path must be specified.
	If only \a fd is given, the function opens the node identified by this
	FD. If only a path is given, this path is opened. If both are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\param openMode The open mode.
	\return A FD referring to the newly opened node, or an error code,
			if an error occurs.
*/
int
_kern_open(int fd, const char *path, int openMode, int perms)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	if (openMode & O_CREAT)
		return file_create(fd, pathBuffer.LockBuffer(), openMode, perms, true);

	return file_open(fd, pathBuffer.LockBuffer(), openMode, true);
}


/*!	\brief Opens a directory specified by entry_ref or node_ref.

	The supplied name may be \c NULL, in which case directory identified
	by \a device and \a inode will be opened. Otherwise \a device and
	\a inode identify the parent directory of the directory to be opened
	and \a name its entry name.

	\param device If \a name is specified the ID of the device the parent
		   directory of the directory to be opened resides on, otherwise
		   the device of the directory itself.
	\param inode If \a name is specified the node ID of the parent
		   directory of the directory to be opened, otherwise node ID of the
		   directory itself.
	\param name The entry name of the directory to be opened. If \c NULL,
		   the \a device + \a inode pair identify the node to be opened.
	\return The FD of the newly opened directory or an error code, if
			something went wrong.
*/
int
_kern_open_dir_entry_ref(dev_t device, ino_t inode, const char *name)
{
	return dir_open_entry_ref(device, inode, name, true);
}


/*!	\brief Opens a directory specified by a FD + path pair.

	At least one of \a fd and \a path must be specified.
	If only \a fd is given, the function opens the directory identified by this
	FD. If only a path is given, this path is opened. If both are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\return A FD referring to the newly opened directory, or an error code,
			if an error occurs.
*/
int
_kern_open_dir(int fd, const char *path)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return dir_open(fd, pathBuffer.LockBuffer(), true);
}


status_t
_kern_fcntl(int fd, int op, uint32 argument)
{
	return common_fcntl(fd, op, argument, true);
}


status_t
_kern_fsync(int fd)
{
	return common_sync(fd, true);
}


status_t
_kern_lock_node(int fd)
{
	return common_lock_node(fd, true);
}


status_t
_kern_unlock_node(int fd)
{
	return common_unlock_node(fd, true);
}


status_t
_kern_create_dir_entry_ref(dev_t device, ino_t inode, const char *name, int perms)
{
	return dir_create_entry_ref(device, inode, name, perms, true);
}


/*!	\brief Creates a directory specified by a FD + path pair.

	\a path must always be specified (it contains the name of the new directory
	at least). If only a path is given, this path identifies the location at
	which the directory shall be created. If both \a fd and \a path are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\param perms The access permissions the new directory shall have.
	\return \c B_OK, if the directory has been created successfully, another
			error code otherwise.
*/
status_t
_kern_create_dir(int fd, const char *path, int perms)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return dir_create(fd, pathBuffer.LockBuffer(), perms, true);
}


status_t
_kern_remove_dir(int fd, const char *path)
{
	if (path) {
		KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		return dir_remove(fd, pathBuffer.LockBuffer(), true);
	}

	return dir_remove(fd, NULL, true);
}


/*!	\brief Reads the contents of a symlink referred to by a FD + path pair.

	At least one of \a fd and \a path must be specified.
	If only \a fd is given, the function the symlink to be read is the node
	identified by this FD. If only a path is given, this path identifies the
	symlink to be read. If both are given and the path is absolute, \a fd is
	ignored; a relative path is reckoned off of the directory (!) identified
	by \a fd.
	If this function fails with B_BUFFER_OVERFLOW, the \a _bufferSize pointer
	will still be updated to reflect the required buffer size.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\param buffer The buffer into which the contents of the symlink shall be
		   written.
	\param _bufferSize A pointer to the size of the supplied buffer.
	\return The length of the link on success or an appropriate error code
*/
status_t
_kern_read_link(int fd, const char *path, char *buffer, size_t *_bufferSize)
{
	status_t status;

	if (path) {
		KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		return common_read_link(fd, pathBuffer.LockBuffer(),
			buffer, _bufferSize, true);
	}

	return common_read_link(fd, NULL, buffer, _bufferSize, true);
}


/*!	\brief Creates a symlink specified by a FD + path pair.

	\a path must always be specified (it contains the name of the new symlink
	at least). If only a path is given, this path identifies the location at
	which the symlink shall be created. If both \a fd and \a path are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param toPath The absolute or relative path. Must not be \c NULL.
	\param mode The access permissions the new symlink shall have.
	\return \c B_OK, if the symlink has been created successfully, another
			error code otherwise.
*/
status_t
_kern_create_symlink(int fd, const char *path, const char *toPath, int mode)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_create_symlink(fd, pathBuffer.LockBuffer(),
		toPath, mode, true);
}


status_t
_kern_create_link(const char *path, const char *toPath)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	KPath toPathBuffer(toPath, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK || toPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_create_link(pathBuffer.LockBuffer(),
		toPathBuffer.LockBuffer(), true);
}


/*!	\brief Removes an entry specified by a FD + path pair from its directory.

	\a path must always be specified (it contains at least the name of the entry
	to be deleted). If only a path is given, this path identifies the entry
	directly. If both \a fd and \a path are given and the path is absolute,
	\a fd is ignored; a relative path is reckoned off of the directory (!)
	identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\return \c B_OK, if the entry has been removed successfully, another
			error code otherwise.
*/
status_t
_kern_unlink(int fd, const char *path)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_unlink(fd, pathBuffer.LockBuffer(), true);
}


/*!	\brief Moves an entry specified by a FD + path pair to a an entry specified
		   by another FD + path pair.

	\a oldPath and \a newPath must always be specified (they contain at least
	the name of the entry). If only a path is given, this path identifies the
	entry directly. If both a FD and a path are given and the path is absolute,
	the FD is ignored; a relative path is reckoned off of the directory (!)
	identified by the respective FD.

	\param oldFD The FD of the old location. May be < 0.
	\param oldPath The absolute or relative path of the old location. Must not
		   be \c NULL.
	\param newFD The FD of the new location. May be < 0.
	\param newPath The absolute or relative path of the new location. Must not
		   be \c NULL.
	\return \c B_OK, if the entry has been moved successfully, another
			error code otherwise.
*/
status_t
_kern_rename(int oldFD, const char *oldPath, int newFD, const char *newPath)
{
	KPath oldPathBuffer(oldPath, false, B_PATH_NAME_LENGTH + 1);
	KPath newPathBuffer(newPath, false, B_PATH_NAME_LENGTH + 1);
	if (oldPathBuffer.InitCheck() != B_OK || newPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_rename(oldFD, oldPathBuffer.LockBuffer(),
		newFD, newPathBuffer.LockBuffer(), true);
}


status_t
_kern_access(const char *path, int mode)
{
	KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_access(pathBuffer.LockBuffer(), mode, true);
}


/*!	\brief Reads stat data of an entity specified by a FD + path pair.

	If only \a fd is given, the stat operation associated with the type
	of the FD (node, attr, attr dir etc.) is performed. If only \a path is
	given, this path identifies the entry for whose node to retrieve the
	stat data. If both \a fd and \a path are given and the path is absolute,
	\a fd is ignored; a relative path is reckoned off of the directory (!)
	identified by \a fd and specifies the entry whose stat data shall be
	retrieved.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\param traverseLeafLink If \a path is given, \c true specifies that the
		   function shall not stick to symlinks, but traverse them.
	\param stat The buffer the stat data shall be written into.
	\param statSize The size of the supplied stat buffer.
	\return \c B_OK, if the the stat data have been read successfully, another
			error code otherwise.
*/
status_t
_kern_read_stat(int fd, const char *path, bool traverseLeafLink,
	struct stat *stat, size_t statSize)
{
	struct stat completeStat;
	struct stat *originalStat = NULL;
	status_t status;

	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	// this supports different stat extensions
	if (statSize < sizeof(struct stat)) {
		originalStat = stat;
		stat = &completeStat;
	}

	if (path) {
		// path given: get the stat of the node referred to by (fd, path)
		KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		status = common_path_read_stat(fd, pathBuffer.LockBuffer(),
			traverseLeafLink, stat, true);
	} else {
		// no path given: get the FD and use the FD operation
		struct file_descriptor *descriptor
			= get_fd(get_current_io_context(true), fd);
		if (descriptor == NULL)
			return B_FILE_ERROR;

		if (descriptor->ops->fd_read_stat)
			status = descriptor->ops->fd_read_stat(descriptor, stat);
		else
			status = EOPNOTSUPP;

		put_fd(descriptor);
	}

	if (status == B_OK && originalStat != NULL)
		memcpy(originalStat, stat, statSize);

	return status;
}


/*!	\brief Writes stat data of an entity specified by a FD + path pair.

	If only \a fd is given, the stat operation associated with the type
	of the FD (node, attr, attr dir etc.) is performed. If only \a path is
	given, this path identifies the entry for whose node to write the
	stat data. If both \a fd and \a path are given and the path is absolute,
	\a fd is ignored; a relative path is reckoned off of the directory (!)
	identified by \a fd and specifies the entry whose stat data shall be
	written.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\param traverseLeafLink If \a path is given, \c true specifies that the
		   function shall not stick to symlinks, but traverse them.
	\param stat The buffer containing the stat data to be written.
	\param statSize The size of the supplied stat buffer.
	\param statMask A mask specifying which parts of the stat data shall be
		   written.
	\return \c B_OK, if the the stat data have been written successfully,
			another error code otherwise.
*/
status_t
_kern_write_stat(int fd, const char *path, bool traverseLeafLink,
	const struct stat *stat, size_t statSize, int statMask)
{
	struct stat completeStat;

	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	// this supports different stat extensions
	if (statSize < sizeof(struct stat)) {
		memset((uint8 *)&completeStat + statSize, 0, sizeof(struct stat) - statSize);
		memcpy(&completeStat, stat, statSize);
		stat = &completeStat;
	}

	status_t status;

	if (path) {
		// path given: write the stat of the node referred to by (fd, path)
		KPath pathBuffer(path, false, B_PATH_NAME_LENGTH + 1);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		status = common_path_write_stat(fd, pathBuffer.LockBuffer(),
			traverseLeafLink, stat, statMask, true);
	} else {
		// no path given: get the FD and use the FD operation
		struct file_descriptor *descriptor
			= get_fd(get_current_io_context(true), fd);
		if (descriptor == NULL)
			return B_FILE_ERROR;

		if (descriptor->ops->fd_write_stat)
			status = descriptor->ops->fd_write_stat(descriptor, stat, statMask);
		else
			status = EOPNOTSUPP;

		put_fd(descriptor);
	}

	return status;
}


int
_kern_open_attr_dir(int fd, const char *path)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	if (path != NULL)
		pathBuffer.SetTo(path);

	return attr_dir_open(fd, path ? pathBuffer.LockBuffer() : NULL, true);
}


int
_kern_create_attr(int fd, const char *name, uint32 type, int openMode)
{
	return attr_create(fd, name, type, openMode, true);
}


int
_kern_open_attr(int fd, const char *name, int openMode)
{
	return attr_open(fd, name, openMode, true);
}


status_t
_kern_remove_attr(int fd, const char *name)
{
	return attr_remove(fd, name, true);
}


status_t
_kern_rename_attr(int fromFile, const char *fromName, int toFile, const char *toName)
{
	return attr_rename(fromFile, fromName, toFile, toName, true);
}


int
_kern_open_index_dir(dev_t device)
{
	return index_dir_open(device, true);
}


status_t
_kern_create_index(dev_t device, const char *name, uint32 type, uint32 flags)
{
	return index_create(device, name, type, flags, true);
}


status_t
_kern_read_index_stat(dev_t device, const char *name, struct stat *stat)
{
	return index_name_read_stat(device, name, stat, true);
}


status_t
_kern_remove_index(dev_t device, const char *name)
{
	return index_remove(device, name, true);
}


status_t
_kern_getcwd(char *buffer, size_t size)
{
	TRACE(("_kern_getcwd: buf %p, %ld\n", buffer, size));

	// Call vfs to get current working directory
	return get_cwd(buffer, size, true);
}


status_t
_kern_setcwd(int fd, const char *path)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	if (path != NULL)
		pathBuffer.SetTo(path);

	return set_cwd(fd, path != NULL ? pathBuffer.LockBuffer() : NULL, true);
}


//	#pragma mark - userland syscalls


dev_t
_user_mount(const char *userPath, const char *userDevice, const char *userFileSystem,
	uint32 flags, const char *userArgs, size_t argsLength)
{
	char fileSystem[B_OS_NAME_LENGTH];
	KPath path, device;
	char *args = NULL;
	status_t status;

	if (!IS_USER_ADDRESS(userPath)
		|| !IS_USER_ADDRESS(userFileSystem)
		|| !IS_USER_ADDRESS(userDevice))
		return B_BAD_ADDRESS;

	if (path.InitCheck() != B_OK || device.InitCheck() != B_OK)
		return B_NO_MEMORY;

	if (user_strlcpy(path.LockBuffer(), userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	if (userFileSystem != NULL
		&& user_strlcpy(fileSystem, userFileSystem, sizeof(fileSystem)) < B_OK)
		return B_BAD_ADDRESS;

	if (userDevice != NULL
		&& user_strlcpy(device.LockBuffer(), userDevice, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	if (userArgs != NULL && argsLength > 0) {
		// this is a safety restriction
		if (argsLength >= 65536)
			return B_NAME_TOO_LONG;

		args = (char *)malloc(argsLength + 1);
		if (args == NULL)
			return B_NO_MEMORY;

		if (user_strlcpy(args, userArgs, argsLength + 1) < B_OK) {
			free(args);
			return B_BAD_ADDRESS;
		}
	}
	path.UnlockBuffer();
	device.UnlockBuffer();

	status = fs_mount(path.LockBuffer(), userDevice != NULL ? device.Path() : NULL,
		userFileSystem ? fileSystem : NULL, flags, args, false);

	free(args);
	return status;
}


status_t
_user_unmount(const char *userPath, uint32 flags)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return fs_unmount(path, -1, flags & ~B_UNMOUNT_BUSY_PARTITION, false);
}


status_t
_user_read_fs_info(dev_t device, struct fs_info *userInfo)
{
	struct fs_info info;
	status_t status;

	if (userInfo == NULL)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = fs_read_info(device, &info);
	if (status != B_OK)
		return status;

	if (user_memcpy(userInfo, &info, sizeof(struct fs_info)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


status_t
_user_write_fs_info(dev_t device, const struct fs_info *userInfo, int mask)
{
	struct fs_info info;

	if (userInfo == NULL)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&info, userInfo, sizeof(struct fs_info)) < B_OK)
		return B_BAD_ADDRESS;

	return fs_write_info(device, &info, mask);
}


dev_t
_user_next_device(int32 *_userCookie)
{
	int32 cookie;
	dev_t device;

	if (!IS_USER_ADDRESS(_userCookie)
		|| user_memcpy(&cookie, _userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	device = fs_next_device(&cookie);

	if (device >= B_OK) {
		// update user cookie
		if (user_memcpy(_userCookie, &cookie, sizeof(int32)) < B_OK)
			return B_BAD_ADDRESS;
	}

	return device;
}


status_t
_user_sync(void)
{
	return _kern_sync();
}


status_t
_user_get_next_fd_info(team_id team, uint32 *userCookie, fd_info *userInfo,
	size_t infoSize)
{
	struct fd_info info;
	uint32 cookie;

	// only root can do this (or should root's group be enough?)
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	if (infoSize != sizeof(fd_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(uint32)) < B_OK)
		return B_BAD_ADDRESS;

	status_t status = _kern_get_next_fd_info(team, &cookie, &info, infoSize);
	if (status < B_OK)
		return status;

	if (user_memcpy(userCookie, &cookie, sizeof(uint32)) < B_OK
		|| user_memcpy(userInfo, &info, infoSize) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_entry_ref_to_path(dev_t device, ino_t inode, const char *leaf,
	char *userPath, size_t pathLength)
{
	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;

	KPath path(B_PATH_NAME_LENGTH + 1);
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	// copy the leaf name onto the stack
	char stackLeaf[B_FILE_NAME_LENGTH];
	if (leaf) {
		if (!IS_USER_ADDRESS(leaf))
			return B_BAD_ADDRESS;

		int length = user_strlcpy(stackLeaf, leaf, B_FILE_NAME_LENGTH);
		if (length < 0)
			return length;
		if (length >= B_FILE_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		leaf = stackLeaf;
	}

	status_t status = vfs_entry_ref_to_path(device, inode, leaf,
		path.LockBuffer(), path.BufferSize());
	if (status < B_OK)
		return status;

	path.UnlockBuffer();

	int length = user_strlcpy(userPath, path.Path(), pathLength);
	if (length < 0)
		return length;
	if (length >= (int)pathLength)
		return B_BUFFER_OVERFLOW;

	return B_OK;
}


status_t
_user_normalize_path(const char* userPath, bool traverseLink, char* buffer)
{
	if (userPath == NULL || buffer == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userPath) || !IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	// copy path from userland
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;
	char* path = pathBuffer.LockBuffer();

	if (user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	// buffer for the leaf part
	KPath leafBuffer(B_PATH_NAME_LENGTH + 1);
	if (leafBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;
	char* leaf = leafBuffer.LockBuffer();

	VNodePutter dirPutter;
	struct vnode* dir = NULL;
	status_t error;

	for (int i = 0; i < B_MAX_SYMLINKS; i++) {
		// get dir vnode + leaf name
		struct vnode* nextDir;
		error = vnode_and_path_to_dir_vnode(dir, path, &nextDir, leaf, false);
		if (error != B_OK)
			return error;

		dir = nextDir;
		strcpy(path, leaf);
		dirPutter.SetTo(dir);

		// get file vnode
		inc_vnode_ref_count(dir);
		struct vnode* fileVnode;
		int type;
		error = vnode_path_to_vnode(dir, path, false, 0, &fileVnode, NULL,
			&type);
		if (error != B_OK)
			return error;
		VNodePutter fileVnodePutter(fileVnode);

		if (!traverseLink || !S_ISLNK(type)) {
			// we're done -- construct the path
			bool hasLeaf = true;
			if (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
				// special cases "." and ".." -- get the dir, forget the leaf
				inc_vnode_ref_count(dir);
				error = vnode_path_to_vnode(dir, leaf, false, 0, &nextDir, NULL,
					NULL);
				if (error != B_OK)
					return error;
				dir = nextDir;
				dirPutter.SetTo(dir);
				hasLeaf = false;
			}

			// get the directory path
			error = dir_vnode_to_path(dir, path, B_PATH_NAME_LENGTH);
			if (error != B_OK)
				return error;

			// append the leaf name
			if (hasLeaf) {
				// insert a directory separator if this is not the file system
				// root
				if ((strcmp(path, "/") != 0
					&& strlcat(path, "/", pathBuffer.BufferSize())
						>= pathBuffer.BufferSize())
					|| strlcat(path, leaf, pathBuffer.BufferSize())
						>= pathBuffer.BufferSize()) {
					return B_NAME_TOO_LONG;
				}
			}

			// copy back to userland
			int len = user_strlcpy(buffer, path, B_PATH_NAME_LENGTH);
			if (len < 0)
				return len;
			if (len >= B_PATH_NAME_LENGTH)
				return B_BUFFER_OVERFLOW;

			return B_OK;
		}

		// read link
		struct stat st;
		if (FS_CALL(fileVnode, read_symlink) != NULL) {
			size_t bufferSize = B_PATH_NAME_LENGTH - 1;
			error = FS_CALL(fileVnode, read_symlink)(fileVnode->mount->cookie,
				fileVnode->private_node, path, &bufferSize);
			if (error != B_OK)
				return error;
			path[bufferSize] = '\0';
		} else
			return B_BAD_VALUE;
	}

	return B_LINK_LIMIT;
}


int
_user_open_entry_ref(dev_t device, ino_t inode, const char *userName,
	int openMode, int perms)
{
	char name[B_FILE_NAME_LENGTH];

	if (userName == NULL || device < 0 || inode < 0)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, sizeof(name)) < B_OK)
		return B_BAD_ADDRESS;

	if (openMode & O_CREAT)
		return file_create_entry_ref(device, inode, name, openMode, perms, false);

	return file_open_entry_ref(device, inode, name, openMode, false);
}


int
_user_open(int fd, const char *userPath, int openMode, int perms)
{
	KPath path(B_PATH_NAME_LENGTH + 1);
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *buffer = path.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| user_strlcpy(buffer, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	if (openMode & O_CREAT)
		return file_create(fd, buffer, openMode, perms, false);

	return file_open(fd, buffer, openMode, false);
}


int
_user_open_dir_entry_ref(dev_t device, ino_t inode, const char *userName)
{
	if (userName != NULL) {
		char name[B_FILE_NAME_LENGTH];

		if (!IS_USER_ADDRESS(userName)
			|| user_strlcpy(name, userName, sizeof(name)) < B_OK)
			return B_BAD_ADDRESS;

		return dir_open_entry_ref(device, inode, name, false);
	}
	return dir_open_entry_ref(device, inode, NULL, false);
}


int
_user_open_dir(int fd, const char *userPath)
{
	KPath path(B_PATH_NAME_LENGTH + 1);
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *buffer = path.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| user_strlcpy(buffer, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return dir_open(fd, buffer, false);
}


/*!	\brief Opens a directory's parent directory and returns the entry name
		   of the former.

	Aside from that is returns the directory's entry name, this method is
	equivalent to \code _user_open_dir(fd, "..") \endcode. It really is
	equivalent, if \a userName is \c NULL.

	If a name buffer is supplied and the name does not fit the buffer, the
	function fails. A buffer of size \c B_FILE_NAME_LENGTH should be safe.

	\param fd A FD referring to a directory.
	\param userName Buffer the directory's entry name shall be written into.
		   May be \c NULL.
	\param nameLength Size of the name buffer.
	\return The file descriptor of the opened parent directory, if everything
			went fine, an error code otherwise.
*/
int
_user_open_parent_dir(int fd, char *userName, size_t nameLength)
{
	bool kernel = false;

	if (userName && !IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;

	// open the parent dir
	int parentFD = dir_open(fd, "..", kernel);
	if (parentFD < 0)
		return parentFD;
	FDCloser fdCloser(parentFD, kernel);

	if (userName) {
		// get the vnodes
		struct vnode *parentVNode = get_vnode_from_fd(parentFD, kernel);
		struct vnode *dirVNode = get_vnode_from_fd(fd, kernel);
		VNodePutter parentVNodePutter(parentVNode);
		VNodePutter dirVNodePutter(dirVNode);
		if (!parentVNode || !dirVNode)
			return B_FILE_ERROR;

		// get the vnode name
		char _buffer[sizeof(struct dirent) + B_FILE_NAME_LENGTH];
		struct dirent *buffer = (struct dirent*)_buffer;
		status_t status = get_vnode_name(dirVNode, parentVNode, buffer,
			sizeof(_buffer));
		if (status != B_OK)
			return status;

		// copy the name to the userland buffer
		int len = user_strlcpy(userName, buffer->d_name, nameLength);
		if (len < 0)
			return len;
		if (len >= (int)nameLength)
			return B_BUFFER_OVERFLOW;
	}

	return fdCloser.Detach();
}


status_t
_user_fcntl(int fd, int op, uint32 argument)
{
	status_t status = common_fcntl(fd, op, argument, false);
	if (op == F_SETLKW)
		syscall_restart_handle_post(status);

	return status;
}


status_t
_user_fsync(int fd)
{
	return common_sync(fd, false);
}


status_t
_user_flock(int fd, int op)
{
	struct file_descriptor *descriptor;
	struct vnode *vnode;
	struct flock flock;
	status_t status;

	FUNCTION(("_user_fcntl(fd = %d, op = %d)\n", fd, op));

	descriptor = get_fd_and_vnode(fd, &vnode, false);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->type != FDTYPE_FILE) {
		put_fd(descriptor);
		return B_BAD_VALUE;
	}

	flock.l_start = 0;
	flock.l_len = OFF_MAX;
	flock.l_whence = 0;
	flock.l_type = (op & LOCK_SH) != 0 ? F_RDLCK : F_WRLCK;

	if ((op & LOCK_UN) != 0)
		status = release_advisory_lock(descriptor->u.vnode, &flock);
	else {
		status = acquire_advisory_lock(descriptor->u.vnode,
			thread_get_current_thread()->team->session_id, &flock,
			(op & LOCK_NB) == 0);
	}

	syscall_restart_handle_post(status);

	put_fd(descriptor);
	return status;
}


status_t
_user_lock_node(int fd)
{
	return common_lock_node(fd, false);
}


status_t
_user_unlock_node(int fd)
{
	return common_unlock_node(fd, false);
}


status_t
_user_create_dir_entry_ref(dev_t device, ino_t inode, const char *userName, int perms)
{
	char name[B_FILE_NAME_LENGTH];
	status_t status;

	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;

	status = user_strlcpy(name, userName, sizeof(name));
	if (status < 0)
		return status;

	return dir_create_entry_ref(device, inode, name, perms, false);
}


status_t
_user_create_dir(int fd, const char *userPath, int perms)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return dir_create(fd, path, perms, false);
}


status_t
_user_remove_dir(int fd, const char *userPath)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath)
			|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
			return B_BAD_ADDRESS;
	}

	return dir_remove(fd, userPath ? path : NULL, false);
}


status_t
_user_read_link(int fd, const char *userPath, char *userBuffer, size_t *userBufferSize)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1), linkBuffer;
	if (pathBuffer.InitCheck() != B_OK || linkBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	size_t bufferSize;

	if (!IS_USER_ADDRESS(userBuffer) || !IS_USER_ADDRESS(userBufferSize)
		|| user_memcpy(&bufferSize, userBufferSize, sizeof(size_t)) < B_OK)
		return B_BAD_ADDRESS;

	char *path = pathBuffer.LockBuffer();
	char *buffer = linkBuffer.LockBuffer();

	if (userPath) {
		if (!IS_USER_ADDRESS(userPath)
			|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
			return B_BAD_ADDRESS;

		if (bufferSize > B_PATH_NAME_LENGTH)
			bufferSize = B_PATH_NAME_LENGTH;
	}

	status_t status = common_read_link(fd, userPath ? path : NULL, buffer,
		&bufferSize, false);

	// we also update the bufferSize in case of errors
	// (the real length will be returned in case of B_BUFFER_OVERFLOW)
	if (user_memcpy(userBufferSize, &bufferSize, sizeof(size_t)) < B_OK)
		return B_BAD_ADDRESS;

	if (status < B_OK)
		return status;

	if (user_memcpy(userBuffer, buffer, bufferSize) != B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


status_t
_user_create_symlink(int fd, const char *userPath, const char *userToPath,
	int mode)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	KPath toPathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK || toPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();
	char *toPath = toPathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| !IS_USER_ADDRESS(userToPath)
		|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK
		|| user_strlcpy(toPath, userToPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return common_create_symlink(fd, path, toPath, mode, false);
}


status_t
_user_create_link(const char *userPath, const char *userToPath)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	KPath toPathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK || toPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();
	char *toPath = toPathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| !IS_USER_ADDRESS(userToPath)
		|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK
		|| user_strlcpy(toPath, userToPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	status_t status = check_path(toPath);
	if (status < B_OK)
		return status;

	return common_create_link(path, toPath, false);
}


status_t
_user_unlink(int fd, const char *userPath)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return common_unlink(fd, path, false);
}


status_t
_user_rename(int oldFD, const char *userOldPath, int newFD,
	const char *userNewPath)
{
	KPath oldPathBuffer(B_PATH_NAME_LENGTH + 1);
	KPath newPathBuffer(B_PATH_NAME_LENGTH + 1);
	if (oldPathBuffer.InitCheck() != B_OK || newPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *oldPath = oldPathBuffer.LockBuffer();
	char *newPath = newPathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userOldPath) || !IS_USER_ADDRESS(userNewPath)
		|| user_strlcpy(oldPath, userOldPath, B_PATH_NAME_LENGTH) < B_OK
		|| user_strlcpy(newPath, userNewPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return common_rename(oldFD, oldPath, newFD, newPath, false);
}


status_t
_user_access(const char *userPath, int mode)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath)
		|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return common_access(path, mode, false);
}


status_t
_user_read_stat(int fd, const char *userPath, bool traverseLink,
	struct stat *userStat, size_t statSize)
{
	struct stat stat;
	status_t status;

	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userStat))
		return B_BAD_ADDRESS;

	if (userPath) {
		// path given: get the stat of the node referred to by (fd, path)
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;

		KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		char *path = pathBuffer.LockBuffer();

		ssize_t length = user_strlcpy(path, userPath, B_PATH_NAME_LENGTH);
		if (length < B_OK)
			return length;
		if (length >= B_PATH_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		status = common_path_read_stat(fd, path, traverseLink, &stat, false);
	} else {
		// no path given: get the FD and use the FD operation
		struct file_descriptor *descriptor
			= get_fd(get_current_io_context(false), fd);
		if (descriptor == NULL)
			return B_FILE_ERROR;

		if (descriptor->ops->fd_read_stat)
			status = descriptor->ops->fd_read_stat(descriptor, &stat);
		else
			status = EOPNOTSUPP;

		put_fd(descriptor);
	}

	if (status < B_OK)
		return status;

	return user_memcpy(userStat, &stat, statSize);
}


status_t
_user_write_stat(int fd, const char *userPath, bool traverseLeafLink,
	const struct stat *userStat, size_t statSize, int statMask)
{
	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	struct stat stat;

	if (!IS_USER_ADDRESS(userStat)
		|| user_memcpy(&stat, userStat, statSize) < B_OK)
		return B_BAD_ADDRESS;

	// clear additional stat fields
	if (statSize < sizeof(struct stat))
		memset((uint8 *)&stat + statSize, 0, sizeof(struct stat) - statSize);

	status_t status;

	if (userPath) {
		// path given: write the stat of the node referred to by (fd, path)
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;

		KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		char *path = pathBuffer.LockBuffer();

		ssize_t length = user_strlcpy(path, userPath, B_PATH_NAME_LENGTH);
		if (length < B_OK)
			return length;
		if (length >= B_PATH_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		status = common_path_write_stat(fd, path, traverseLeafLink, &stat,
			statMask, false);
	} else {
		// no path given: get the FD and use the FD operation
		struct file_descriptor *descriptor
			= get_fd(get_current_io_context(false), fd);
		if (descriptor == NULL)
			return B_FILE_ERROR;

		if (descriptor->ops->fd_write_stat)
			status = descriptor->ops->fd_write_stat(descriptor, &stat, statMask);
		else
			status = EOPNOTSUPP;

		put_fd(descriptor);
	}

	return status;
}


int
_user_open_attr_dir(int fd, const char *userPath)
{
	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath)
			|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
			return B_BAD_ADDRESS;
	}

	return attr_dir_open(fd, userPath ? path : NULL, false);
}


int
_user_create_attr(int fd, const char *userName, uint32 type, int openMode)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return attr_create(fd, name, type, openMode, false);
}


int
_user_open_attr(int fd, const char *userName, int openMode)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return attr_open(fd, name, openMode, false);
}


status_t
_user_remove_attr(int fd, const char *userName)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return attr_remove(fd, name, false);
}


status_t
_user_rename_attr(int fromFile, const char *userFromName, int toFile, const char *userToName)
{
	if (!IS_USER_ADDRESS(userFromName)
		|| !IS_USER_ADDRESS(userToName))
		return B_BAD_ADDRESS;

	KPath fromNameBuffer(B_FILE_NAME_LENGTH);
	KPath toNameBuffer(B_FILE_NAME_LENGTH);
	if (fromNameBuffer.InitCheck() != B_OK || toNameBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *fromName = fromNameBuffer.LockBuffer();
	char *toName = toNameBuffer.LockBuffer();

	if (user_strlcpy(fromName, userFromName, B_FILE_NAME_LENGTH) < B_OK
		|| user_strlcpy(toName, userToName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return attr_rename(fromFile, fromName, toFile, toName, false);
}


int
_user_open_index_dir(dev_t device)
{
	return index_dir_open(device, false);
}


status_t
_user_create_index(dev_t device, const char *userName, uint32 type, uint32 flags)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return index_create(device, name, type, flags, false);
}


status_t
_user_read_index_stat(dev_t device, const char *userName, struct stat *userStat)
{
	char name[B_FILE_NAME_LENGTH];
	struct stat stat;
	status_t status;

	if (!IS_USER_ADDRESS(userName)
		|| !IS_USER_ADDRESS(userStat)
		|| user_strlcpy(name, userName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	status = index_name_read_stat(device, name, &stat, false);
	if (status == B_OK) {
		if (user_memcpy(userStat, &stat, sizeof(stat)) < B_OK)
			return B_BAD_ADDRESS;
	}

	return status;
}


status_t
_user_remove_index(dev_t device, const char *userName)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return index_remove(device, name, false);
}


status_t
_user_getcwd(char *userBuffer, size_t size)
{
	if (!IS_USER_ADDRESS(userBuffer))
		return B_BAD_ADDRESS;

	KPath pathBuffer(B_PATH_NAME_LENGTH + 1);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	TRACE(("user_getcwd: buf %p, %ld\n", userBuffer, size));

	if (size > B_PATH_NAME_LENGTH)
		size = B_PATH_NAME_LENGTH;

	char *path = pathBuffer.LockBuffer();

	status_t status = get_cwd(path, size, false);
	if (status < B_OK)
		return status;

	// Copy back the result
	if (user_strlcpy(userBuffer, path, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_setcwd(int fd, const char *userPath)
{
	TRACE(("user_setcwd: path = %p\n", userPath));

	KPath pathBuffer(B_PATH_NAME_LENGTH);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char *path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath)
			|| user_strlcpy(path, userPath, B_PATH_NAME_LENGTH) < B_OK)
			return B_BAD_ADDRESS;
	}

	return set_cwd(fd, userPath != NULL ? path : NULL, false);
}


int
_user_open_query(dev_t device, const char *userQuery, size_t queryLength,
	uint32 flags, port_id port, int32 token)
{
	char *query;

	if (device < 0 || userQuery == NULL || queryLength == 0)
		return B_BAD_VALUE;

	// this is a safety restriction
	if (queryLength >= 65536)
		return B_NAME_TOO_LONG;

	query = (char *)malloc(queryLength + 1);
	if (query == NULL)
		return B_NO_MEMORY;
	if (user_strlcpy(query, userQuery, queryLength + 1) < B_OK) {
		free(query);
		return B_BAD_ADDRESS;
	}

	int fd = query_open(device, query, flags, port, token, false);

	free(query);
	return fd;
}
