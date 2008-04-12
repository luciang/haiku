/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

//! Operations on file descriptors

#include <fd.h>

#include <stdlib.h>
#include <string.h>

#include <OS.h>

#include <AutoDeleter.h>

#include <syscalls.h>
#include <syscall_restart.h>
#include <util/AutoLock.h>
#include <vfs.h>
#include <wait_for_objects.h>


//#define TRACE_FD
#ifdef TRACE_FD
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


static struct file_descriptor* get_fd_locked(struct io_context* context,
	int fd);
static struct file_descriptor *remove_fd(struct io_context *context, int fd);
static void deselect_select_infos(file_descriptor* descriptor,
	select_info* infos);


struct FDGetterLocking {
	inline bool Lock(file_descriptor* /*lockable*/)
	{
		return false;
	}

	inline void Unlock(file_descriptor* lockable)
	{
		put_fd(lockable);
	}
};

class FDGetter : public AutoLocker<file_descriptor, FDGetterLocking> {
public:
	inline FDGetter()
		: AutoLocker<file_descriptor, FDGetterLocking>()
	{
	}

	inline FDGetter(io_context* context, int fd, bool contextLocked = false)
		: AutoLocker<file_descriptor, FDGetterLocking>(
			contextLocked ? get_fd_locked(context, fd) : get_fd(context, fd))
	{
	}

	inline file_descriptor* SetTo(io_context* context, int fd,
		bool contextLocked = false)
	{
		file_descriptor* descriptor
			= contextLocked ? get_fd_locked(context, fd) : get_fd(context, fd);
		AutoLocker<file_descriptor, FDGetterLocking>::SetTo(descriptor, true);
		return descriptor;
	}

	inline file_descriptor* SetTo(int fd, bool kernel,
		bool contextLocked = false)
	{
		return SetTo(get_current_io_context(kernel), fd, contextLocked);
	}

	inline file_descriptor* FD() const
	{
		return fLockable;
	}
};


/*** General fd routines ***/


#ifdef DEBUG
void dump_fd(int fd, struct file_descriptor *descriptor);

void
dump_fd(int fd,struct file_descriptor *descriptor)
{
	dprintf("fd[%d] = %p: type = %ld, ref_count = %ld, ops = %p, u.vnode = %p, u.mount = %p, cookie = %p, open_mode = %lx, pos = %Ld\n",
		fd, descriptor, descriptor->type, descriptor->ref_count, descriptor->ops,
		descriptor->u.vnode, descriptor->u.mount, descriptor->cookie, descriptor->open_mode, descriptor->pos);
}
#endif


/** Allocates and initializes a new file_descriptor */

struct file_descriptor *
alloc_fd(void)
{
	file_descriptor *descriptor
		= (file_descriptor*)malloc(sizeof(struct file_descriptor));
	if (descriptor == NULL)
		return NULL;

	descriptor->u.vnode = NULL;
	descriptor->cookie = NULL;
	descriptor->ref_count = 1;
	descriptor->open_count = 0;
	descriptor->open_mode = 0;
	descriptor->pos = 0;

	return descriptor;
}


bool
fd_close_on_exec(struct io_context *context, int fd)
{
	return CHECK_BIT(context->fds_close_on_exec[fd / 8], fd & 7) ? true : false;
}


void
fd_set_close_on_exec(struct io_context *context, int fd, bool closeFD)
{
	if (closeFD)
		context->fds_close_on_exec[fd / 8] |= (1 << (fd & 7));
	else
		context->fds_close_on_exec[fd / 8] &= ~(1 << (fd & 7));
}


/** Searches a free slot in the FD table of the provided I/O context, and inserts
 *	the specified descriptor into it.
 */

int
new_fd_etc(struct io_context *context, struct file_descriptor *descriptor, int firstIndex)
{
	int fd = -1;
	uint32 i;

	mutex_lock(&context->io_mutex);

	for (i = firstIndex; i < context->table_size; i++) {
		if (!context->fds[i]) {
			fd = i;
			break;
		}
	}
	if (fd < 0) {
		fd = B_NO_MORE_FDS;
		goto err;
	}

	context->fds[fd] = descriptor;
	context->num_used_fds++;
	atomic_add(&descriptor->open_count, 1);

err:
	mutex_unlock(&context->io_mutex);

	return fd;
}


int
new_fd(struct io_context *context, struct file_descriptor *descriptor)
{
	return new_fd_etc(context, descriptor, 0);
}


/**	Reduces the descriptor's reference counter, and frees all resources
 *	when it's no longer used.
 */

void
put_fd(struct file_descriptor *descriptor)
{
	int32 previous = atomic_add(&descriptor->ref_count, -1);

	TRACE(("put_fd(descriptor = %p [ref = %ld, cookie = %p])\n",
		descriptor, descriptor->ref_count, descriptor->cookie));

	// free the descriptor if we don't need it anymore
	if (previous == 1) {
		// free the underlying object
		if (descriptor->ops != NULL && descriptor->ops->fd_free != NULL)
			descriptor->ops->fd_free(descriptor);

		free(descriptor);
	} else if ((descriptor->open_mode & O_DISCONNECTED) != 0
		&& previous - 1 == descriptor->open_count
		&& descriptor->ops != NULL) {
		// the descriptor has been disconnected - it cannot
		// be accessed anymore, let's close it (no one is
		// currently accessing this descriptor)

		if (descriptor->ops->fd_close)
			descriptor->ops->fd_close(descriptor);
		if (descriptor->ops->fd_free)
			descriptor->ops->fd_free(descriptor);

		// prevent this descriptor from being closed/freed again
		descriptor->open_count = -1;
		descriptor->ref_count = -1;
		descriptor->ops = NULL;
		descriptor->u.vnode = NULL;

		// the file descriptor is kept intact, so that it's not
		// reused until someone explicetly closes it
	}
}


/**	Decrements the open counter of the file descriptor and invokes
 *	its close hook when appropriate.
 */

void
close_fd(struct file_descriptor *descriptor)
{
	if (atomic_add(&descriptor->open_count, -1) == 1) {
		vfs_unlock_vnode_if_locked(descriptor);

		if (descriptor->ops != NULL && descriptor->ops->fd_close != NULL)
			descriptor->ops->fd_close(descriptor);
	}
}


status_t
close_fd_index(struct io_context *context, int fd)
{
	struct file_descriptor *descriptor = remove_fd(context, fd);

	if (descriptor == NULL)
		return B_FILE_ERROR;

	close_fd(descriptor);
	put_fd(descriptor);
		// the reference associated with the slot

	return B_OK;
}


/**	This descriptor's underlying object will be closed and freed
 *	as soon as possible (in one of the next calls to put_fd() -
 *	get_fd() will no longer succeed on this descriptor).
 *	This is useful if the underlying object is gone, for instance
 *	when a (mounted) volume got removed unexpectedly.
 */

void
disconnect_fd(struct file_descriptor *descriptor)
{
	descriptor->open_mode |= O_DISCONNECTED;
}


void
inc_fd_ref_count(struct file_descriptor *descriptor)
{
	atomic_add(&descriptor->ref_count, 1);
}


static struct file_descriptor *
get_fd_locked(struct io_context *context, int fd)
{
	if (fd < 0 || (uint32)fd >= context->table_size)
		return NULL;

	struct file_descriptor *descriptor = context->fds[fd];

	if (descriptor != NULL) {
		// Disconnected descriptors cannot be accessed anymore
		if (descriptor->open_mode & O_DISCONNECTED)
			descriptor = NULL;
		else
			inc_fd_ref_count(descriptor);
	}

	return descriptor;
}


struct file_descriptor *
get_fd(struct io_context *context, int fd)
{
	MutexLocker(context->io_mutex);

	return get_fd_locked(context, fd);
}


/**	Removes the file descriptor from the specified slot.
 */

static struct file_descriptor *
remove_fd(struct io_context *context, int fd)
{
	struct file_descriptor *descriptor = NULL;

	if (fd < 0)
		return NULL;

	mutex_lock(&context->io_mutex);

	if ((uint32)fd < context->table_size)
		descriptor = context->fds[fd];

	select_info* selectInfos = NULL;
	bool disconnected = false;

	if (descriptor)	{
		// fd is valid
		context->fds[fd] = NULL;
		fd_set_close_on_exec(context, fd, false);
		context->num_used_fds--;

		selectInfos = context->select_infos[fd];
		context->select_infos[fd] = NULL;

		disconnected = (descriptor->open_mode & O_DISCONNECTED);
	}

	mutex_unlock(&context->io_mutex);

	if (selectInfos != NULL)
		deselect_select_infos(descriptor, selectInfos);

	return disconnected ? NULL : descriptor;
}


static int
dup_fd(int fd, bool kernel)
{
	struct io_context *context = get_current_io_context(kernel);
	struct file_descriptor *descriptor;
	int status;

	TRACE(("dup_fd: fd = %d\n", fd));

	// Try to get the fd structure
	descriptor = get_fd(context, fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	// now put the fd in place
	status = new_fd(context, descriptor);
	if (status < 0)
		put_fd(descriptor);
	else {
		mutex_lock(&context->io_mutex);
		fd_set_close_on_exec(context, status, false);
		mutex_unlock(&context->io_mutex);
	}

	return status;
}


/*!	POSIX says this should be the same as:
		close(newfd);
		fcntl(oldfd, F_DUPFD, newfd);

	We do dup2() directly to be thread-safe.
*/
static int
dup2_fd(int oldfd, int newfd, bool kernel)
{
	struct file_descriptor *evicted = NULL;
	struct io_context *context;

	TRACE(("dup2_fd: ofd = %d, nfd = %d\n", oldfd, newfd));

	// quick check
	if (oldfd < 0 || newfd < 0)
		return B_FILE_ERROR;

	// Get current I/O context and lock it
	context = get_current_io_context(kernel);
	mutex_lock(&context->io_mutex);

	// Check if the fds are valid (mutex must be locked because
	// the table size could be changed)
	if ((uint32)oldfd >= context->table_size
		|| (uint32)newfd >= context->table_size
		|| context->fds[oldfd] == NULL) {
		mutex_unlock(&context->io_mutex);
		return B_FILE_ERROR;
	}

	// Check for identity, note that it cannot be made above
	// because we always want to return an error on invalid
	// handles
	select_info* selectInfos = NULL;
	if (oldfd != newfd) {
		// Now do the work
		evicted = context->fds[newfd];
		selectInfos = context->select_infos[newfd];
		context->select_infos[newfd] = NULL;
		atomic_add(&context->fds[oldfd]->ref_count, 1);
		atomic_add(&context->fds[oldfd]->open_count, 1);
		context->fds[newfd] = context->fds[oldfd];

		if (evicted == NULL)
			context->num_used_fds++;
	}

	fd_set_close_on_exec(context, newfd, false);

	mutex_unlock(&context->io_mutex);

	// Say bye bye to the evicted fd
	if (evicted) {
		deselect_select_infos(evicted, selectInfos);
		close_fd(evicted);
		put_fd(evicted);
	}

	return newfd;
}


static status_t
fd_ioctl(bool kernelFD, int fd, ulong op, void *buffer, size_t length)
{
	struct file_descriptor *descriptor;
	int status;

	descriptor = get_fd(get_current_io_context(kernelFD), fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->ops->fd_ioctl)
		status = descriptor->ops->fd_ioctl(descriptor, op, buffer, length);
	else
		status = EOPNOTSUPP;

	put_fd(descriptor);
	return status;
}


static void
deselect_select_infos(file_descriptor* descriptor, select_info* infos)
{
	TRACE(("deselect_select_infos(%p, %p)\n", descriptor, infos));

	select_info* info = infos;
	while (info != NULL) {
		select_sync* sync = info->sync;

		// deselect the selected events
		if (descriptor->ops->fd_deselect && info->selected_events) {
			for (uint16 event = 1; event < 16; event++) {
				if (info->selected_events & SELECT_FLAG(event)) {
					descriptor->ops->fd_deselect(descriptor, event,
						(selectsync*)info);
				}
			}
		}

		notify_select_events(info, B_EVENT_INVALID);
		info = info->next;
		put_select_sync(sync);
	}
}


status_t
select_fd(int32 fd, struct select_info* info, bool kernel)
{
	TRACE(("select_fd(fd = %d, info = %p (%p), 0x%x)\n", fd, info,
		info->sync, info.selected_events));

	FDGetter fdGetter;
		// define before the context locker, so it will be destroyed after it

	io_context* context = get_current_io_context(kernel);
	MutexLocker locker(context->io_mutex);

	struct file_descriptor* descriptor = fdGetter.SetTo(context, fd, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (info->selected_events == 0)
		return B_OK;

	if (!descriptor->ops->fd_select) {
		// if the I/O subsystem doesn't support select(), we will
		// immediately notify the select call
		return notify_select_events(info, info->selected_events);
	}

	// add the info to the IO context
	info->next = context->select_infos[fd];
	context->select_infos[fd] = info;

	// as long as the info is in the list, we keep a reference to the sync
	// object
	atomic_add(&info->sync->ref_count, 1);

	locker.Unlock();

	// select any events asked for
	uint32 selectedEvents = 0;

	for (uint16 event = 1; event < 16; event++) {
		if (info->selected_events & SELECT_FLAG(event)
			&& descriptor->ops->fd_select(descriptor, event,
				(selectsync*)info) == B_OK) {
			selectedEvents |= SELECT_FLAG(event);
		}
	}
	info->selected_events = selectedEvents;

	// if nothing has been selected, we deselect immediately
	if (selectedEvents == 0)
		deselect_fd(fd, info, kernel);

	return B_OK;
}


status_t
deselect_fd(int32 fd, struct select_info* info, bool kernel)
{
	TRACE(("deselect_fd(fd = %d, info = %p (%p), 0x%x)\n", fd, info,
		info->sync, info.selected_events));

	if (info->selected_events == 0)
		return B_OK;

	FDGetter fdGetter;
		// define before the context locker, so it will be destroyed after it

	io_context* context = get_current_io_context(kernel);
	MutexLocker locker(context->io_mutex);

	struct file_descriptor* descriptor = fdGetter.SetTo(context, fd, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	// remove the info from the IO context

	select_info** infoLocation = &context->select_infos[fd];
	while (*infoLocation != NULL && *infoLocation != info)
		infoLocation = &(*infoLocation)->next;

	// If not found, someone else beat us to it.
	if (*infoLocation != info)
		return B_OK;

	*infoLocation = info->next;

	locker.Unlock();

	// deselect the selected events
	if (descriptor->ops->fd_deselect && info->selected_events) {
		for (uint16 event = 1; event < 16; event++) {
			if (info->selected_events & SELECT_FLAG(event)) {
				descriptor->ops->fd_deselect(descriptor, event,
					(selectsync*)info);
			}
		}
	}

	put_select_sync(info->sync);

	return B_OK;
}


/** This function checks if the specified fd is valid in the current
 *	context. It can be used for a quick check; the fd is not locked
 *	so it could become invalid immediately after this check.
 */

bool
fd_is_valid(int fd, bool kernel)
{
	struct file_descriptor *descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return false;

	put_fd(descriptor);
	return true;
}


struct vnode *
fd_vnode(struct file_descriptor *descriptor)
{
	switch (descriptor->type) {
		case FDTYPE_FILE:
		case FDTYPE_DIR:
		case FDTYPE_ATTR_DIR:
		case FDTYPE_ATTR:
			return descriptor->u.vnode;
	}

	return NULL;
}


static status_t
common_close(int fd, bool kernel)
{
	return close_fd_index(get_current_io_context(kernel), fd);
}


static ssize_t
common_user_io(int fd, off_t pos, void *buffer, size_t length, bool write)
{
	if (!IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	if (pos < -1)
		return B_BAD_VALUE;

	FDGetter fdGetter;
	struct file_descriptor* descriptor = fdGetter.SetTo(fd, false);
	if (!descriptor)
		return B_FILE_ERROR;

	if (write ? (descriptor->open_mode & O_RWMASK) == O_RDONLY
			: (descriptor->open_mode & O_RWMASK) == O_WRONLY) {
		return B_FILE_ERROR;
	}

	bool movePosition = false;
	if (pos == -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (write ? descriptor->ops->fd_write == NULL
			: descriptor->ops->fd_read == NULL) {
		return B_BAD_VALUE;
	}

	SyscallRestartWrapper<status_t> status;

	if (write)
		status = descriptor->ops->fd_write(descriptor, pos, buffer, &length);
	else
		status = descriptor->ops->fd_read(descriptor, pos, buffer, &length);

	if (status < B_OK)
		return status;

	if (movePosition)
		descriptor->pos = pos + length;

	return length <= SSIZE_MAX ? (ssize_t)length : SSIZE_MAX;
}


static ssize_t
common_user_vector_io(int fd, off_t pos, const iovec *userVecs, size_t count,
	bool write)
{
	if (!IS_USER_ADDRESS(userVecs))
		return B_BAD_ADDRESS;

	if (pos < -1)
		return B_BAD_VALUE;

	/* prevent integer overflow exploit in malloc() */
	if (count > IOV_MAX)
		return B_BAD_VALUE;

	FDGetter fdGetter;
	struct file_descriptor* descriptor = fdGetter.SetTo(fd, false);
	if (!descriptor)
		return B_FILE_ERROR;

	if (write ? (descriptor->open_mode & O_RWMASK) == O_RDONLY
			: (descriptor->open_mode & O_RWMASK) == O_WRONLY) {
		return B_FILE_ERROR;
	}

	iovec* vecs = (iovec*)malloc(sizeof(iovec) * count);
	if (vecs == NULL)
		return B_NO_MEMORY;
	MemoryDeleter _(vecs);

	if (user_memcpy(vecs, userVecs, sizeof(iovec) * count) < B_OK)
		return B_BAD_ADDRESS;

	bool movePosition = false;
	if (pos == -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (write ? descriptor->ops->fd_write == NULL
			: descriptor->ops->fd_read == NULL) {
		return B_BAD_VALUE;
	}

	SyscallRestartWrapper<status_t> status;

	ssize_t bytesTransferred = 0;
	for (uint32 i = 0; i < count; i++) {
		size_t length = vecs[i].iov_len;
		if (write) {
			status = descriptor->ops->fd_write(descriptor, pos,
				vecs[i].iov_base, &length);
		} else {
			status = descriptor->ops->fd_read(descriptor, pos, vecs[i].iov_base,
				&length);
		}

		if (status < B_OK) {
			if (bytesTransferred == 0)
				return status;
			status = B_OK;
			break;
		}

		if ((uint64)bytesTransferred + length > SSIZE_MAX)
			bytesTransferred = SSIZE_MAX;
		else
			bytesTransferred += (ssize_t)length;

		pos += length;

		if (length < vecs[i].iov_len)
			break;
	}

	if (movePosition)
		descriptor->pos = pos;

	return bytesTransferred;
}


status_t
user_fd_kernel_ioctl(int fd, ulong op, void *buffer, size_t length)
{
	TRACE(("user_fd_kernel_ioctl: fd %d\n", fd));

	return fd_ioctl(false, fd, op, buffer, length);
}


//	#pragma mark - User syscalls


ssize_t
_user_read(int fd, off_t pos, void *buffer, size_t length)
{
	return common_user_io(fd, pos, buffer, length, false);
}


ssize_t
_user_readv(int fd, off_t pos, const iovec *userVecs, size_t count)
{
	return common_user_vector_io(fd, pos, userVecs, count, false);
}


ssize_t
_user_write(int fd, off_t pos, const void *buffer, size_t length)
{
	return common_user_io(fd, pos, (void*)buffer, length, true);
}


ssize_t
_user_writev(int fd, off_t pos, const iovec *userVecs, size_t count)
{
	return common_user_vector_io(fd, pos, userVecs, count, true);
}


off_t
_user_seek(int fd, off_t pos, int seekType)
{
	syscall_64_bit_return_value();

	struct file_descriptor *descriptor;

	descriptor = get_fd(get_current_io_context(false), fd);
	if (!descriptor)
		return B_FILE_ERROR;

	TRACE(("user_seek(descriptor = %p)\n", descriptor));

	if (descriptor->ops->fd_seek)
		pos = descriptor->ops->fd_seek(descriptor, pos, seekType);
	else
		pos = ESPIPE;

	put_fd(descriptor);
	return pos;
}


status_t
_user_ioctl(int fd, ulong op, void *buffer, size_t length)
{
	struct file_descriptor *descriptor;

	if (!IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	TRACE(("user_ioctl: fd %d\n", fd));

	SyscallRestartWrapper<status_t> status;

	return status = fd_ioctl(false, fd, op, buffer, length);
}


ssize_t
_user_read_dir(int fd, struct dirent *buffer, size_t bufferSize, uint32 maxCount)
{
	struct file_descriptor *descriptor;
	ssize_t retval;

	if (!IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	TRACE(("user_read_dir(fd = %d, buffer = %p, bufferSize = %ld, count = %lu)\n", fd, buffer, bufferSize, maxCount));

	struct io_context* ioContext = get_current_io_context(false);
	descriptor = get_fd(ioContext, fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->ops->fd_read_dir) {
		uint32 count = maxCount;
		retval = descriptor->ops->fd_read_dir(ioContext, descriptor, buffer,
			bufferSize, &count);
		if (retval >= 0)
			retval = count;
	} else
		retval = EOPNOTSUPP;

	put_fd(descriptor);
	return retval;
}


status_t
_user_rewind_dir(int fd)
{
	struct file_descriptor *descriptor;
	status_t status;

	TRACE(("user_rewind_dir(fd = %d)\n", fd));

	descriptor = get_fd(get_current_io_context(false), fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->ops->fd_rewind_dir)
		status = descriptor->ops->fd_rewind_dir(descriptor);
	else
		status = EOPNOTSUPP;

	put_fd(descriptor);
	return status;
}


status_t
_user_close(int fd)
{
	return common_close(fd, false);
}


int
_user_dup(int fd)
{
	return dup_fd(fd, false);
}


int
_user_dup2(int ofd, int nfd)
{
	return dup2_fd(ofd, nfd, false);
}


//	#pragma mark - Kernel calls


ssize_t
_kern_read(int fd, off_t pos, void *buffer, size_t length)
{
	if (pos < -1)
		return B_BAD_VALUE;

	FDGetter fdGetter;
	struct file_descriptor *descriptor = fdGetter.SetTo(fd, true);

	if (!descriptor)
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_WRONLY)
		return B_FILE_ERROR;

	bool movePosition = false;
	if (pos == -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	SyscallFlagUnsetter _;

	if (descriptor->ops->fd_read == NULL)
		return B_BAD_VALUE;

	ssize_t bytesRead = descriptor->ops->fd_read(descriptor, pos, buffer,
		&length);
	if (bytesRead >= B_OK) {
		if (length > SSIZE_MAX)
			bytesRead = SSIZE_MAX;
		else
			bytesRead = (ssize_t)length;

		if (movePosition)
			descriptor->pos = pos + length;
	}

	return bytesRead;
}


ssize_t
_kern_readv(int fd, off_t pos, const iovec *vecs, size_t count)
{
	bool movePosition = false;
	status_t status;
	uint32 i;

	if (pos < -1)
		return B_BAD_VALUE;

	FDGetter fdGetter;
	struct file_descriptor *descriptor = fdGetter.SetTo(fd, true);

	if (!descriptor)
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_WRONLY)
		return B_FILE_ERROR;

	if (pos == -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (descriptor->ops->fd_read == NULL)
		return B_BAD_VALUE;

	SyscallFlagUnsetter _;

	ssize_t bytesRead = 0;

	for (i = 0; i < count; i++) {
		size_t length = vecs[i].iov_len;
		status = descriptor->ops->fd_read(descriptor, pos, vecs[i].iov_base,
			&length);
		if (status < B_OK) {
			bytesRead = status;
			break;
		}

		if ((uint64)bytesRead + length > SSIZE_MAX)
			bytesRead = SSIZE_MAX;
		else
			bytesRead += (ssize_t)length;

		pos += vecs[i].iov_len;
	}

	if (movePosition)
		descriptor->pos = pos;

	return bytesRead;
}


ssize_t
_kern_write(int fd, off_t pos, const void *buffer, size_t length)
{
	if (pos < -1)
		return B_BAD_VALUE;

	FDGetter fdGetter;
	struct file_descriptor *descriptor = fdGetter.SetTo(fd, true);

	if (descriptor == NULL)
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_RDONLY)
		return B_FILE_ERROR;

	bool movePosition = false;
	if (pos == -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (descriptor->ops->fd_write == NULL)
		return B_BAD_VALUE;

	SyscallFlagUnsetter _;

	ssize_t bytesWritten = descriptor->ops->fd_write(descriptor, pos, buffer,
		&length);
	if (bytesWritten >= B_OK) {
		if (length > SSIZE_MAX)
			bytesWritten = SSIZE_MAX;
		else
			bytesWritten = (ssize_t)length;

		if (movePosition)
			descriptor->pos = pos + length;
	}

	return bytesWritten;
}


ssize_t
_kern_writev(int fd, off_t pos, const iovec *vecs, size_t count)
{
	bool movePosition = false;
	status_t status;
	uint32 i;

	if (pos < -1)
		return B_BAD_VALUE;

	FDGetter fdGetter;
	struct file_descriptor *descriptor = fdGetter.SetTo(fd, true);

	if (!descriptor)
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_RDONLY)
		return B_FILE_ERROR;

	if (pos == -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (descriptor->ops->fd_write == NULL)
		return B_BAD_VALUE;

	SyscallFlagUnsetter _;

	ssize_t bytesWritten = 0;

	for (i = 0; i < count; i++) {
		size_t length = vecs[i].iov_len;
		status = descriptor->ops->fd_write(descriptor, pos,
			vecs[i].iov_base, &length);
		if (status < B_OK) {
			bytesWritten = status;
			break;
		}

		if ((uint64)bytesWritten + length > SSIZE_MAX)
			bytesWritten = SSIZE_MAX;
		else
			bytesWritten += (ssize_t)length;

		pos += vecs[i].iov_len;
	}

	if (movePosition)
		descriptor->pos = pos;

	return bytesWritten;
}


off_t
_kern_seek(int fd, off_t pos, int seekType)
{
	struct file_descriptor *descriptor;

	descriptor = get_fd(get_current_io_context(true), fd);
	if (!descriptor)
		return B_FILE_ERROR;

	if (descriptor->ops->fd_seek)
		pos = descriptor->ops->fd_seek(descriptor, pos, seekType);
	else
		pos = ESPIPE;

	put_fd(descriptor);
	return pos;
}


status_t
_kern_ioctl(int fd, ulong op, void *buffer, size_t length)
{
	TRACE(("kern_ioctl: fd %d\n", fd));

	SyscallFlagUnsetter _;

	return fd_ioctl(true, fd, op, buffer, length);
}


ssize_t
_kern_read_dir(int fd, struct dirent *buffer, size_t bufferSize, uint32 maxCount)
{
	struct file_descriptor *descriptor;
	ssize_t retval;

	TRACE(("sys_read_dir(fd = %d, buffer = %p, bufferSize = %ld, count = %lu)\n",fd, buffer, bufferSize, maxCount));

	struct io_context* ioContext = get_current_io_context(true);
	descriptor = get_fd(ioContext, fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->ops->fd_read_dir) {
		uint32 count = maxCount;
		retval = descriptor->ops->fd_read_dir(ioContext, descriptor, buffer,
			bufferSize, &count);
		if (retval >= 0)
			retval = count;
	} else
		retval = EOPNOTSUPP;

	put_fd(descriptor);
	return retval;
}


status_t
_kern_rewind_dir(int fd)
{
	struct file_descriptor *descriptor;
	status_t status;

	TRACE(("sys_rewind_dir(fd = %d)\n",fd));

	descriptor = get_fd(get_current_io_context(true), fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->ops->fd_rewind_dir)
		status = descriptor->ops->fd_rewind_dir(descriptor);
	else
		status = EOPNOTSUPP;

	put_fd(descriptor);
	return status;
}


status_t
_kern_close(int fd)
{
	return common_close(fd, true);
}


int
_kern_dup(int fd)
{
	return dup_fd(fd, true);
}


int
_kern_dup2(int ofd, int nfd)
{
	return dup2_fd(ofd, nfd, true);
}

