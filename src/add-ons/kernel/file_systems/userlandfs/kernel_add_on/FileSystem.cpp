/*
 * Copyright 2001-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "Volume.h"

#include <util/AutoLock.h>

#include "AutoLocker.h"
#include "Compatibility.h"
#include "Debug.h"
#include "FileSystem.h"
#include "HashMap.h"
#include "kernel_interface.h"
#include "KernelRequestHandler.h"
#include "PortReleaser.h"
#include "RequestAllocator.h"
#include "Requests.h"
#include "Settings.h"
#include "SingleReplyRequestHandler.h"

// The time after which the notification thread times out at the port and
// restarts the loop. Of interest only when the FS is deleted. It is the
// maximal time the destructor has to wait for the thread.
static const bigtime_t kNotificationRequestTimeout = 50000;	// 50 ms

// SelectSyncMap
struct FileSystem::SelectSyncMap
	: public SynchronizedHashMap<HashKey32<selectsync*>, int32*> {
};

// constructor
FileSystem::FileSystem()
	:
	fVolumes(),
	fName(),
	fTeam(-1),
	fNotificationPort(NULL),
	fNotificationThread(-1),
	fPortPool(),
	fSelectSyncs(NULL),
	fSettings(NULL),
	fUserlandServerTeam(-1),
	fInitialized(false),
	fTerminating(false)
{
	mutex_init(&fVolumeLock, "userlandfs volumes");
	mutex_init(&fVNodeOpsLock, "userlandfs vnode ops");
}

// destructor
FileSystem::~FileSystem()
{
	fTerminating = true;

	// wait for the notification thread to terminate
	if (fNotificationThread >= 0) {
		int32 result;
		wait_for_thread(fNotificationThread, &result);
	}

	// delete our data structures
	if (fSelectSyncs) {
		for (SelectSyncMap::Iterator it = fSelectSyncs->GetIterator();
			 it.HasNext();) {
			SelectSyncMap::Entry entry = it.Next();
			delete entry.value;
		}
		delete fSelectSyncs;
	}
	delete fSettings;

	// delete vnode ops vectors -- there shouldn't be any left, though
	VNodeOps* ops = fVNodeOps.Clear();
	int32 count = 0;
	while (ops != NULL) {
		count++;
		VNodeOps* next = ops->fNext;
		free(ops);
		ops = next;
	}
	if (count > 0)
		WARN(("Deleted %ld vnode ops vectors!\n", count));
}

// Init
status_t
FileSystem::Init(const char* name, team_id team, Port::Info* infos, int32 count,
	const FSCapabilities& capabilities)
{
	PRINT(("FileSystem::Init(\"%s\", %p, %ld)\n", name, infos, count));
	capabilities.Dump();

	// check parameters
	if (!name || !infos || count < 2)
		RETURN_ERROR(B_BAD_VALUE);

	// set the name
	if (!fName.SetTo(name))
		return B_NO_MEMORY;

	// init VNodeOps map
	status_t error = fVNodeOps.Init();
	if (error != B_OK)
		return error;

	fTeam = team;
	fCapabilities = capabilities;

	// create the select sync entry map
	fSelectSyncs = new(nothrow) SelectSyncMap;
	if (!fSelectSyncs)
		return B_NO_MEMORY;

	// create the request ports
	// the notification port
	fNotificationPort = new(nothrow) RequestPort(infos);
	if (!fNotificationPort)
		RETURN_ERROR(B_NO_MEMORY);
	error = fNotificationPort->InitCheck();
	if (error != B_OK)
		return error;

	// the other request ports
	for (int32 i = 1; i < count; i++) {
		RequestPort* port = new(nothrow) RequestPort(infos + i);
		if (!port)
			RETURN_ERROR(B_NO_MEMORY);
		error = port->InitCheck();
		if (error == B_OK)
			error = fPortPool.AddPort(port);
		if (error != B_OK) {
			delete port;
			RETURN_ERROR(error);
		}
	}

	// get the userland team
	port_info portInfo;
	error = get_port_info(infos[0].owner_port, &portInfo);
	if (error != B_OK)
		RETURN_ERROR(error);
	fUserlandServerTeam = portInfo.team;

	// print some info about the userland team
	D(
		PRINT(("  userland team is: %ld\n", fUserlandServerTeam));
		int32 cookie = 0;
		thread_info threadInfo;
		while (get_next_thread_info(fUserlandServerTeam, &cookie, &threadInfo)
			   == B_OK) {
			PRINT(("    userland thread: %ld: `%s'\n", threadInfo.thread,
				threadInfo.name));
		}
	);

	// load the settings
	fSettings = new(nothrow) Settings;
	if (fSettings) {
		status_t settingsError = fSettings->SetTo(fName.GetString());
		if (settingsError != B_OK) {
			PRINT(("Failed to load settings: %s\n", strerror(settingsError)));
			delete fSettings;
			fSettings = NULL;
		} else
			fSettings->Dump();
	} else
		ERROR(("Failed to allocate settings.\n"));

	// spawn the notification thread
	#if USER
		fNotificationThread = spawn_thread(_NotificationThreadEntry,
			"UFS notification thread", B_NORMAL_PRIORITY, this);
	#else
		fNotificationThread = spawn_kernel_thread(_NotificationThreadEntry,
			"UFS notification thread", B_NORMAL_PRIORITY, this);
	#endif
	if (fNotificationThread < 0)
		RETURN_ERROR(fNotificationThread);
	resume_thread(fNotificationThread);

	fInitialized = (error == B_OK);
	RETURN_ERROR(error);
}

// GetName
const char*
FileSystem::GetName() const
{
	return fName.GetString();
}

// GetCapabilities
const FSCapabilities&
FileSystem::GetCapabilities() const
{
	return fCapabilities;
}

// GetPortPool
RequestPortPool*
FileSystem::GetPortPool()
{
	return &fPortPool;
}

// Mount
status_t
FileSystem::Mount(fs_volume* fsVolume, const char* device, uint32 flags,
	const char* parameters, Volume** _volume)
{
	// check initialization and parameters
	if (!fInitialized || !_volume)
		return B_BAD_VALUE;

	// create volume
	Volume* volume = new(nothrow) Volume(this, fsVolume);
	if (!volume)
		return B_NO_MEMORY;

	// add volume to the volume list
	MutexLocker locker(fVolumeLock);
	status_t error = fVolumes.PushBack(volume);
	locker.Unlock();
	if (error != B_OK)
		return error;

	// mount volume
	error = volume->Mount(device, flags, parameters);
	if (error != B_OK) {
		MutexLocker locker(fVolumeLock);
		fVolumes.Remove(volume);
		locker.Unlock();
		volume->RemoveReference();
		return error;
	}

	*_volume = volume;
	return error;
}

// Initialize
/*status_t
FileSystem::Initialize(const char* deviceName, const char* parameters,
	size_t len)
{
	// get a free port
	RequestPort* port = fPortPool.AcquirePort();
	if (!port)
		return B_ERROR;
	PortReleaser _(&fPortPool, port);
	// prepare the request
	RequestAllocator allocator(port->GetPort());
	MountVolumeRequest* request;
	status_t error = AllocateRequest(allocator, &request);
	if (error != B_OK)
		return error;
	error = allocator.AllocateString(request->device, deviceName);
	if (error == B_OK)
		error = allocator.AllocateData(request->parameters, parameters, len, 1);
	if (error != B_OK)
		return error;
	// send the request
	SingleReplyRequestHandler handler(MOUNT_VOLUME_REPLY);
	InitializeVolumeReply* reply;
	error = port->SendRequest(&allocator, &handler, (Request**)&reply);
	if (error != B_OK)
		return error;
	RequestReleaser requestReleaser(port, reply);
	// process the reply
	if (reply->error != B_OK)
		return reply->error;
	return error;
}*/

// VolumeUnmounted
void
FileSystem::VolumeUnmounted(Volume* volume)
{
	MutexLocker locker(fVolumeLock);
	fVolumes.Remove(volume);
}

// GetVolume
Volume*
FileSystem::GetVolume(dev_t id)
{
	MutexLocker _(fVolumeLock);
	for (Vector<Volume*>::Iterator it = fVolumes.Begin();
		 it != fVolumes.End();
		 it++) {
		 Volume* volume = *it;
		 if (volume->GetID() == id) {
		 	volume->AddReference();
		 	return volume;
		 }
	}
	return NULL;
}

// GetIOCtlInfo
const IOCtlInfo*
FileSystem::GetIOCtlInfo(int command) const
{
	return (fSettings ? fSettings->GetIOCtlInfo(command) : NULL);
}

// AddSelectSyncEntry
status_t
FileSystem::AddSelectSyncEntry(selectsync* sync)
{
	AutoLocker<SelectSyncMap> _(fSelectSyncs);
	int32* count = fSelectSyncs->Get(sync);
	if (!count) {
		count = new(nothrow) int32(0);
		if (!count)
			return B_NO_MEMORY;
		status_t error = fSelectSyncs->Put(sync, count);
		if (error != B_OK) {
			delete count;
			return error;
		}
	}
	(*count)++;
	return B_OK;
}

// RemoveSelectSyncEntry
void
FileSystem::RemoveSelectSyncEntry(selectsync* sync)
{
	AutoLocker<SelectSyncMap> _(fSelectSyncs);
	if (int32* count = fSelectSyncs->Get(sync)) {
		if (--(*count) <= 0) {
			fSelectSyncs->Remove(sync);
			delete count;
		}
	}
}


// KnowsSelectSyncEntry
bool
FileSystem::KnowsSelectSyncEntry(selectsync* sync)
{
	return fSelectSyncs->ContainsKey(sync);
}


// GetVNodeOps
VNodeOps*
FileSystem::GetVNodeOps(const FSVNodeCapabilities& capabilities)
{
	MutexLocker locker(fVNodeOpsLock);

	// do we already have ops for those capabilities
	VNodeOps* ops = fVNodeOps.Lookup(capabilities);
	if (ops != NULL) {
		ops->refCount++;
		return ops;
	}

	// no, create a new object
	fs_vnode_ops* opsVector = new(std::nothrow) fs_vnode_ops;
	if (opsVector == NULL)
		return NULL;

	// set the operations
	_InitVNodeOpsVector(opsVector, capabilities);

	// create the VNodeOps object
	ops = new(std::nothrow) VNodeOps(capabilities, opsVector);
	if (ops == NULL) {
		delete opsVector;
		return NULL;
	}

	fVNodeOps.Insert(ops);

	return ops;
}


// PutVNodeOps
void
FileSystem::PutVNodeOps(VNodeOps* ops)
{
	MutexLocker locker(fVNodeOpsLock);

	if (--ops->refCount == 0) {
		fVNodeOps.Remove(ops);
		delete ops;
	}
}


// IsUserlandServerThread
bool
FileSystem::IsUserlandServerThread() const
{
	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	return (info.team == fUserlandServerTeam);
}


// _InitVNodeOpsVector
void
FileSystem::_InitVNodeOpsVector(fs_vnode_ops* ops,
	const FSVNodeCapabilities& capabilities)
{
	memcpy(ops, &gUserlandFSVnodeOps, sizeof(fs_vnode_ops));

	#undef CLEAR_UNSUPPORTED
	#define CLEAR_UNSUPPORTED(capability, op) 	\
		if (!capabilities.Get(capability))				\
			ops->op = NULL

	// vnode operations
	// FS_VNODE_CAPABILITY_LOOKUP: lookup
	// FS_VNODE_CAPABILITY_GET_VNODE_NAME: get_vnode_name
		// emulated in userland
	// FS_VNODE_CAPABILITY_PUT_VNODE: put_vnode
	// FS_VNODE_CAPABILITY_REMOVE_VNODE: remove_vnode
		// needed by Volume to clean up

	// asynchronous I/O
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_IO, io);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_CANCEL_IO, cancel_io);

	// cache file access
	ops->get_file_map = NULL;	// never used

	// common operations
	// FS_VNODE_CAPABILITY_IOCTL: ioctl
		// needed by Volume
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_SET_FLAGS, set_flags);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_SELECT, select);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_DESELECT, deselect);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_FSYNC, fsync);

	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_READ_SYMLINK, read_symlink);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_CREATE_SYMLINK, create_symlink);

	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_LINK, link);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_UNLINK, unlink);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_RENAME, rename);

	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_ACCESS, access);
	// FS_VNODE_CAPABILITY_READ_STAT: read_stat
		// needed by Volume
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_WRITE_STAT, write_stat);

	// file operations
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_CREATE, create);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_OPEN, open);
	// FS_VNODE_CAPABILITY_CLOSE: close
		// needed by Volume
	// FS_VNODE_CAPABILITY_FREE_COOKIE: free_cookie
		// needed by Volume
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_READ, read);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_WRITE, write);

	// directory operations
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_CREATE_DIR, create_dir);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_REMOVE_DIR, remove_dir);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_OPEN_DIR, open_dir);
	// FS_VNODE_CAPABILITY_CLOSE_DIR: close_dir
		// needed by Volume
	// FS_VNODE_CAPABILITY_FREE_DIR_COOKIE: free_dir_cookie
		// needed by Volume
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_READ_DIR, read_dir);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_REWIND_DIR, rewind_dir);

	// attribute directory operations
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_OPEN_ATTR_DIR, open_attr_dir);
	// FS_VNODE_CAPABILITY_CLOSE_ATTR_DIR: close_attr_dir
		// needed by Volume
	// FS_VNODE_CAPABILITY_FREE_ATTR_DIR_COOKIE: free_attr_dir_cookie
		// needed by Volume
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_READ_ATTR_DIR, read_attr_dir);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_REWIND_ATTR_DIR, rewind_attr_dir);

	// attribute operations
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_CREATE_ATTR, create_attr);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_OPEN_ATTR, open_attr);
	// FS_VNODE_CAPABILITY_CLOSE_ATTR: close_attr
		// needed by Volume
	// FS_VNODE_CAPABILITY_FREE_ATTR_COOKIE: free_attr_cookie
		// needed by Volume
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_READ_ATTR, read_attr);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_WRITE_ATTR, write_attr);

	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_READ_ATTR_STAT, read_attr_stat);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_WRITE_ATTR_STAT, write_attr_stat);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_RENAME_ATTR, rename_attr);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_REMOVE_ATTR, remove_attr);

	// support for node and FS layers
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_CREATE_SPECIAL_NODE,
		create_special_node);
	CLEAR_UNSUPPORTED(FS_VNODE_CAPABILITY_GET_SUPER_VNODE, get_super_vnode);

	#undef CLEAR_UNSUPPORTED
}


// _NotificationThreadEntry
int32
FileSystem::_NotificationThreadEntry(void* data)
{
	return ((FileSystem*)data)->_NotificationThread();
}


// _NotificationThread
int32
FileSystem::_NotificationThread()
{
	// process the notification requests until the FS is deleted
	while (!fTerminating) {
		if (fNotificationPort->InitCheck() != B_OK)
			return fNotificationPort->InitCheck();
		KernelRequestHandler handler(this, NO_REQUEST);
		fNotificationPort->HandleRequests(&handler, NULL,
			kNotificationRequestTimeout);
	}
	// We eat all remaining notification requests, so that they aren't
	// presented to the file system, when it is mounted next time.
	// TODO: We should probably use a special handler that sends an ack reply,
	// but ignores the requests otherwise.
	KernelRequestHandler handler(this, NO_REQUEST);
	fNotificationPort->HandleRequests(&handler, NULL, 0);
	return 0;
}

