/*
 * Copyright 2007-2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <new>

#include <KernelExport.h>
#include <NodeMonitor.h>
#include <Select.h>

#include <condition_variable.h>
#include <debug.h>
#include <khash.h>
#include <lock.h>
#include <select_sync_pool.h>
#include <team.h>
#include <util/DoublyLinkedList.h>
#include <util/AutoLock.h>
#include <util/ring_buffer.h>
#include <vfs.h>
#include <vm.h>

#include "fifo.h"


//#define TRACE_FIFO
#ifdef TRACE_FIFO
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


#define PIPEFS_HASH_SIZE		16
#define PIPEFS_MAX_BUFFER_SIZE	32768


// TODO: PIPE_BUF is supposed to be defined somewhere else.
#define PIPE_BUF	_POSIX_PIPE_BUF


namespace fifo {

class Inode;

class RingBuffer {
	public:
		RingBuffer();
		~RingBuffer();

		status_t CreateBuffer();
		void DeleteBuffer();

		size_t Write(const void *buffer, size_t length);
		size_t Read(void *buffer, size_t length);
		ssize_t UserWrite(const void *buffer, ssize_t length);
		ssize_t UserRead(void *buffer, ssize_t length);

		size_t Readable() const;
		size_t Writable() const;

	private:
		struct ring_buffer	*fBuffer;
};


class ReadRequest : public DoublyLinkedListLinkImpl<ReadRequest> {
	public:
		void SetUnnotified()	{ fNotified = false; }

		void Notify()
		{
			if (!fNotified) {
				fWaitCondition.NotifyOne();
				fNotified = true;
			}
		}

		ConditionVariable<>& WaitCondition() { return fWaitCondition; }

	private:
		ConditionVariable<>	fWaitCondition;
		bool				fNotified;
};


class WriteRequest : public DoublyLinkedListLinkImpl<WriteRequest> {
	public:
		WriteRequest(size_t minimalWriteCount)
			:
			fMinimalWriteCount(minimalWriteCount)
		{
		}

		size_t MinimalWriteCount() const
		{
			return fMinimalWriteCount;
		}

	private:
		size_t	fMinimalWriteCount;
};


typedef DoublyLinkedList<ReadRequest> ReadRequestList;
typedef DoublyLinkedList<WriteRequest> WriteRequestList;


class Inode {
	public:
		Inode();
		~Inode();

		status_t	InitCheck();

		bool		IsActive() const { return fActive; }
		time_t		CreationTime() const { return fCreationTime; }
		void		SetCreationTime(time_t creationTime)
						{ fCreationTime = creationTime; }
		time_t		ModificationTime() const { return fModificationTime; }
		void		SetModificationTime(time_t modificationTime)
						{ fModificationTime = modificationTime; }

		benaphore	*RequestLock() { return &fRequestLock; }

		status_t	WriteDataToBuffer(const void *data, size_t *_length,
						bool nonBlocking);
		status_t	ReadDataFromBuffer(void *data, size_t *_length,
						bool nonBlocking, ReadRequest &request);
		size_t		BytesAvailable() const { return fBuffer.Readable(); }
		size_t		BytesWritable() const { return fBuffer.Writable(); }

		void		AddReadRequest(ReadRequest &request);
		void		RemoveReadRequest(ReadRequest &request);
		status_t	WaitForReadRequest(ReadRequest &request);

		void		NotifyBytesRead(size_t bytes);
		void		NotifyReadDone();
		void		NotifyBytesWritten(size_t bytes);
		void		NotifyEndClosed(bool writer);

		void		Open(int openMode);
		void		Close(int openMode);
		int32		ReaderCount() const { return fReaderCount; }
		int32		WriterCount() const { return fWriterCount; }

		status_t	Select(uint8 event, uint32 ref, selectsync *sync,
						int openMode);
		status_t	Deselect(uint8 event, selectsync *sync, int openMode);

	private:
		time_t		fCreationTime;
		time_t		fModificationTime;

		RingBuffer	fBuffer;

		ReadRequestList		fReadRequests;
		WriteRequestList	fWriteRequests;

		benaphore	fRequestLock;

		ConditionVariable<> fWriteCondition;

		int32		fReaderCount;
		int32		fWriterCount;
		bool		fActive;

		select_sync_pool	*fReadSelectSyncPool;
		select_sync_pool	*fWriteSelectSyncPool;
};


class FIFOInode : public Inode {
public:
	FIFOInode(fs_vnode* vnode)
		:
		Inode(),
		fSuperVnode(*vnode)
	{
	}

	fs_vnode*	SuperVnode() { return &fSuperVnode; }

private:
	fs_vnode	fSuperVnode;
};


struct file_cookie {
	int				open_mode;
};


//---------------------


RingBuffer::RingBuffer()
	: fBuffer(NULL)
{
}


RingBuffer::~RingBuffer()
{
	DeleteBuffer();
}


status_t
RingBuffer::CreateBuffer()
{
	if (fBuffer != NULL)
		return B_OK;

	fBuffer = create_ring_buffer(PIPEFS_MAX_BUFFER_SIZE);
	return (fBuffer != NULL ? B_OK : B_NO_MEMORY);
}


void
RingBuffer::DeleteBuffer()
{
	if (fBuffer != NULL) {
		delete_ring_buffer(fBuffer);
		fBuffer = NULL;
	}
}


inline size_t
RingBuffer::Write(const void *buffer, size_t length)
{
	if (fBuffer == NULL)
		return B_NO_MEMORY;

	return ring_buffer_write(fBuffer, (const uint8 *)buffer, length);
}


inline size_t
RingBuffer::Read(void *buffer, size_t length)
{
	if (fBuffer == NULL)
		return B_NO_MEMORY;

	return ring_buffer_read(fBuffer, (uint8 *)buffer, length);
}


inline ssize_t
RingBuffer::UserWrite(const void *buffer, ssize_t length)
{
	if (fBuffer == NULL)
		return B_NO_MEMORY;

	return ring_buffer_user_write(fBuffer, (const uint8 *)buffer, length);
}


inline ssize_t
RingBuffer::UserRead(void *buffer, ssize_t length)
{
	if (fBuffer == NULL)
		return B_NO_MEMORY;

	return ring_buffer_user_read(fBuffer, (uint8 *)buffer, length);
}


inline size_t
RingBuffer::Readable() const
{
	return (fBuffer != NULL ? ring_buffer_readable(fBuffer) : 0);
}


inline size_t
RingBuffer::Writable() const
{
	return (fBuffer != NULL ? ring_buffer_writable(fBuffer) : 0);
}


//	#pragma mark -


Inode::Inode()
	:
	fReadRequests(),
	fWriteRequests(),
	fReaderCount(0),
	fWriterCount(0),
	fActive(false),
	fReadSelectSyncPool(NULL),
	fWriteSelectSyncPool(NULL)
{
	fWriteCondition.Publish(this, "pipe");
	benaphore_init(&fRequestLock, "pipe request");

	fCreationTime = fModificationTime = time(NULL);
}


Inode::~Inode()
{
	fWriteCondition.Unpublish();
	benaphore_destroy(&fRequestLock);
}


status_t 
Inode::InitCheck()
{
	if (fRequestLock.sem < B_OK)
		return B_ERROR;

	return B_OK;
}


/*!
	Writes the specified data bytes to the inode's ring buffer. The
	request lock must be held when calling this method.
	Notifies readers if necessary, so that blocking readers will get started.
	Returns B_OK for success, B_BAD_ADDRESS if copying from the buffer failed,
	and various semaphore errors (like B_WOULD_BLOCK in non-blocking mode). If
	the returned length is > 0, the returned error code can be ignored.
*/
status_t
Inode::WriteDataToBuffer(const void *_data, size_t *_length, bool nonBlocking)
{
	const uint8* data = (const uint8*)_data;
	size_t dataSize = *_length;
	size_t& written = *_length;
	written = 0;

	TRACE(("Inode::WriteDataToBuffer(data = %p, bytes = %lu)\n",
		data, dataSize));

	// According to the standard, request up to PIPE_BUF bytes shall not be
	// interleaved with other writer's data.
	size_t minToWrite = 1;
	if (dataSize <= PIPE_BUF)
		minToWrite = dataSize;

	while (dataSize > 0) {
		// Wait until enough space in the buffer is available.
		while (!fActive
				|| fBuffer.Writable() < minToWrite && fReaderCount > 0) {
			if (nonBlocking)
				return B_WOULD_BLOCK;

			ConditionVariableEntry<> entry;
			entry.Add(this);

			WriteRequest request(minToWrite);
			fWriteRequests.Add(&request);

			benaphore_unlock(&fRequestLock);
			status_t status = entry.Wait(B_CAN_INTERRUPT);
			benaphore_lock(&fRequestLock);

			fWriteRequests.Remove(&request);

			if (status != B_OK)
				return status;
		}

		// write only as long as there are readers left
		if (fReaderCount == 0 && fActive) {
			if (written == 0)
				send_signal(find_thread(NULL), SIGPIPE);
			return EPIPE;
		}

		// write as much as we can

		size_t toWrite = (fActive ? fBuffer.Writable() : 0);
		if (toWrite > dataSize)
			toWrite = dataSize;

		if (toWrite > 0 && fBuffer.UserWrite(data, toWrite) < B_OK)
			return B_BAD_ADDRESS;

		data += toWrite;
		dataSize -= toWrite;
		written += toWrite;

		NotifyBytesWritten(toWrite);
	}

	return B_OK;
}


status_t
Inode::ReadDataFromBuffer(void *data, size_t *_length, bool nonBlocking,
	ReadRequest &request)
{
	size_t dataSize = *_length;
	*_length = 0;

	// wait until our request is first in queue
	status_t error;
	if (fReadRequests.Head() != &request) {
		if (nonBlocking)
			return B_WOULD_BLOCK;

		error = WaitForReadRequest(request);
		if (error != B_OK)
			return error;
	}

	// wait until data are available
	while (fBuffer.Readable() == 0) {
		if (nonBlocking)
			return B_WOULD_BLOCK;

		if (fActive && fWriterCount == 0)
			return B_OK;

		error = WaitForReadRequest(request);
		if (error != B_OK)
			return error;
	}

	// read as much as we can
	size_t toRead = fBuffer.Readable();
	if (toRead > dataSize)
		toRead = dataSize;

	if (fBuffer.UserRead(data, toRead) < B_OK)
		return B_BAD_ADDRESS;

	NotifyBytesRead(toRead);

	*_length = toRead;

	return B_OK;
}


void
Inode::AddReadRequest(ReadRequest &request)
{
	fReadRequests.Add(&request);
}


void
Inode::RemoveReadRequest(ReadRequest &request)
{
	fReadRequests.Remove(&request);
}


status_t
Inode::WaitForReadRequest(ReadRequest &request)
{
	request.SetUnnotified();

	// publish the condition variable
	ConditionVariable<>& conditionVariable = request.WaitCondition();
	conditionVariable.Publish(&request, "pipe request");

	// add the entry to wait on
	ConditionVariableEntry<> entry;
	entry.Add(&request);

	// wait
	benaphore_unlock(&fRequestLock);
	status_t status = entry.Wait(B_CAN_INTERRUPT);
	benaphore_lock(&fRequestLock);

	// unpublish the condition variable
	conditionVariable.Unpublish();

	return status;
}


void
Inode::NotifyBytesRead(size_t bytes)
{
	// notify writer, if something can be written now
	size_t writable = fBuffer.Writable();
	if (bytes > 0) {
		// notify select()ors only, if nothing was writable before
		if (writable == bytes) {
			if (fWriteSelectSyncPool)
				notify_select_event_pool(fWriteSelectSyncPool, B_SELECT_WRITE);
		}

		// If any of the waiting writers has a minimal write count that has
		// now become satisfied, we notify all of them (condition variables
		// don't support doing that selectively).
		WriteRequest *request;
		WriteRequestList::Iterator iterator = fWriteRequests.GetIterator();
		while ((request = iterator.Next()) != NULL) {
			size_t minWriteCount = request->MinimalWriteCount();
			if (minWriteCount > 0 && minWriteCount <= writable
					&& minWriteCount > writable - bytes) {
				fWriteCondition.NotifyAll();
				break;
			}
		}
	}
}


void
Inode::NotifyReadDone()
{
	// notify next reader, if there's still something to be read
	if (fBuffer.Readable() > 0) {
		if (ReadRequest* request = fReadRequests.First())
			request->Notify();
	}
}


void
Inode::NotifyBytesWritten(size_t bytes)
{
	// notify reader, if something can be read now
	if (bytes > 0 && fBuffer.Readable() == bytes) {
		if (fReadSelectSyncPool)
			notify_select_event_pool(fReadSelectSyncPool, B_SELECT_READ);

		if (ReadRequest* request = fReadRequests.First())
			request->Notify();
	}
}


void
Inode::NotifyEndClosed(bool writer)
{
	if (writer) {
		// Our last writer has been closed; if the pipe
		// contains no data, unlock all waiting readers
		if (fBuffer.Readable() == 0) {
			ReadRequest *request;
			ReadRequestList::Iterator iterator = fReadRequests.GetIterator();
			while ((request = iterator.Next()) != NULL)
				request->Notify();

			if (fReadSelectSyncPool)
				notify_select_event_pool(fReadSelectSyncPool, B_SELECT_READ);
		}
	} else {
		// Last reader is gone. Wake up all writers.
		fWriteCondition.NotifyAll();

		if (fWriteSelectSyncPool) {
			notify_select_event_pool(fWriteSelectSyncPool, B_SELECT_WRITE);
			notify_select_event_pool(fWriteSelectSyncPool, B_SELECT_ERROR);
		}
	}
}


void
Inode::Open(int openMode)
{
	BenaphoreLocker locker(RequestLock());

	if ((openMode & O_ACCMODE) == O_WRONLY)
		fWriterCount++;

	if ((openMode & O_ACCMODE) == O_RDONLY || (openMode & O_ACCMODE) == O_RDWR)
		fReaderCount++;

	if (fReaderCount > 0 && fWriterCount > 0) {
		fBuffer.CreateBuffer();
		fActive = true;

		// notify all waiting writers that they can start
		if (fWriteSelectSyncPool)
			notify_select_event_pool(fWriteSelectSyncPool, B_SELECT_WRITE);
		fWriteCondition.NotifyAll();
	}
}


void
Inode::Close(int openMode)
{
	TRACE(("Inode::Close(openMode = %d)\n", openMode));

	BenaphoreLocker locker(RequestLock());

	if ((openMode & O_ACCMODE) == O_WRONLY && --fWriterCount == 0)
		NotifyEndClosed(true);

	if ((openMode & O_ACCMODE) == O_RDONLY || (openMode & O_ACCMODE) == O_RDWR) {
		if (--fReaderCount == 0)
			NotifyEndClosed(false);
	}

	if (fReaderCount == 0 && fWriterCount == 0) {
		fActive = false;
		fBuffer.DeleteBuffer();
	}
}


status_t
Inode::Select(uint8 event, uint32 ref, selectsync *sync, int openMode)
{
	bool writer = true;
	select_sync_pool** pool;
	if ((openMode & O_RWMASK) == O_RDONLY) {
		pool = &fReadSelectSyncPool;
		writer = false;
	} else if ((openMode & O_RWMASK) == O_WRONLY) {
		pool = &fWriteSelectSyncPool;
	} else
		return B_NOT_ALLOWED;

	if (add_select_sync_pool_entry(pool, sync, event) != B_OK)
		return B_ERROR;

	// signal right away, if the condition holds already
	if (writer) {
		if (event == B_SELECT_WRITE
				&& (fBuffer.Writable() > 0 || fReaderCount == 0)
			|| event == B_SELECT_ERROR && fReaderCount == 0) {
			return notify_select_event(sync, event);
		}
	} else {
		if (event == B_SELECT_READ
				&& (fBuffer.Readable() > 0 || fWriterCount == 0)) {
			return notify_select_event(sync, event);
		}
	}

	return B_OK;
}


status_t
Inode::Deselect(uint8 event, selectsync *sync, int openMode)
{
	select_sync_pool** pool;
	if ((openMode & O_RWMASK) == O_RDONLY) {
		pool = &fReadSelectSyncPool;
	} else if ((openMode & O_RWMASK) == O_WRONLY) {
		pool = &fWriteSelectSyncPool;
	} else
		return B_NOT_ALLOWED;

	remove_select_sync_pool_entry(pool, sync, event);
	return B_OK;
}


//	#pragma mark -


static status_t
fifo_put_vnode(fs_volume *volume, fs_vnode *vnode, bool reenter)
{
	FIFOInode* fifo = (FIFOInode*)vnode->private_node;
	fs_vnode* superVnode = fifo->SuperVnode();

	status_t error = B_OK;
	if (superVnode->ops->put_vnode != NULL)
		error = superVnode->ops->put_vnode(volume, superVnode, reenter);

	delete fifo;

	return error;
}


static status_t
fifo_remove_vnode(fs_volume *volume, fs_vnode *vnode, bool reenter)
{
	FIFOInode* fifo = (FIFOInode*)vnode->private_node;
	fs_vnode* superVnode = fifo->SuperVnode();

	status_t error = B_OK;
	if (superVnode->ops->remove_vnode != NULL)
		error = superVnode->ops->remove_vnode(volume, superVnode, reenter);

	delete fifo;

	return error;
}


static status_t
fifo_open(fs_volume *_volume, fs_vnode *_node, int openMode,
	void **_cookie)
{
	Inode *inode = (Inode *)_node->private_node;

	TRACE(("fifo_open(): node = %p, openMode = %d\n", inode, openMode));

	file_cookie *cookie = (file_cookie *)malloc(sizeof(file_cookie));
	if (cookie == NULL)
		return B_NO_MEMORY;

	TRACE(("  open cookie = %p\n", cookie));
	cookie->open_mode = openMode;
	inode->Open(openMode);

	*_cookie = (void *)cookie;

	return B_OK;
}


static status_t
fifo_close(fs_volume *volume, fs_vnode *vnode, void *_cookie)
{
	file_cookie *cookie = (file_cookie *)_cookie;
	FIFOInode* fifo = (FIFOInode*)vnode->private_node;

	fifo->Close(cookie->open_mode);

	return B_OK;
}


static status_t
fifo_free_cookie(fs_volume *_volume, fs_vnode *_node, void *_cookie)
{
	file_cookie *cookie = (file_cookie *)_cookie;

	TRACE(("fifo_freecookie: entry vnode %p, cookie %p\n", _node, _cookie));

	free(cookie);

	return B_OK;
}


static status_t
fifo_fsync(fs_volume *_volume, fs_vnode *_v)
{
	return B_OK;
}


static status_t
fifo_read(fs_volume *_volume, fs_vnode *_node, void *_cookie,
	off_t /*pos*/, void *buffer, size_t *_length)
{
	file_cookie *cookie = (file_cookie *)_cookie;
	Inode *inode = (Inode *)_node->private_node;

	TRACE(("fifo_read(vnode = %p, cookie = %p, length = %lu, mode = %d)\n",
		inode, cookie, *_length, cookie->open_mode));

	if ((cookie->open_mode & O_RWMASK) != O_RDONLY)
		return B_NOT_ALLOWED;

	BenaphoreLocker locker(inode->RequestLock());

	if (inode->IsActive() && inode->WriterCount() == 0) {
		// as long there is no writer, and the pipe is empty,
		// we always just return 0 to indicate end of file
		if (inode->BytesAvailable() == 0) {
			*_length = 0;
			return B_OK;
		}
	}

	// issue read request

	ReadRequest request;
	inode->AddReadRequest(request);

	size_t length = *_length;
	status_t status = inode->ReadDataFromBuffer(buffer, &length,
		(cookie->open_mode & O_NONBLOCK) != 0, request);

	inode->RemoveReadRequest(request);
	inode->NotifyReadDone();

	if (length > 0)
		status = B_OK;

	*_length = length;
	return status;
}


static status_t
fifo_write(fs_volume *_volume, fs_vnode *_node, void *_cookie,
	off_t /*pos*/, const void *buffer, size_t *_length)
{
	file_cookie *cookie = (file_cookie *)_cookie;
	Inode *inode = (Inode *)_node->private_node;

	TRACE(("fifo_write(vnode = %p, cookie = %p, length = %lu)\n",
		_node, cookie, *_length));

	if ((cookie->open_mode & O_RWMASK) != O_WRONLY)
		return B_NOT_ALLOWED;

	BenaphoreLocker locker(inode->RequestLock());

	size_t length = *_length;
	if (length == 0)
		return B_OK;

	// copy data into ring buffer
	status_t status = inode->WriteDataToBuffer(buffer, &length,
		(cookie->open_mode & O_NONBLOCK) != 0);

	if (length > 0)
		status = B_OK;

	*_length = length;
	return status;
}


static status_t
fifo_read_stat(fs_volume *volume, fs_vnode *vnode, struct ::stat *st)
{
	FIFOInode* fifo = (FIFOInode*)vnode->private_node;
	fs_vnode* superVnode = fifo->SuperVnode();

	if (superVnode->ops->read_stat == NULL)
		return B_BAD_VALUE;

	status_t error = superVnode->ops->read_stat(volume, superVnode, st);
	if (error != B_OK)
		return error;


	BenaphoreLocker locker(fifo->RequestLock());

	st->st_size = fifo->BytesAvailable();

	st->st_blksize = 4096;

// TODO: Just pass the changes to our modification time on to the super node.
	st->st_atime = time(NULL);
	st->st_mtime = st->st_ctime = fifo->ModificationTime();
//	st->st_crtime = inode->CreationTime();

	return B_OK;
}


static status_t
fifo_write_stat(fs_volume *volume, fs_vnode *vnode, const struct ::stat *st,
	uint32 statMask)
{
	// we cannot change the size of anything
	if (statMask & B_STAT_SIZE)
		return B_BAD_VALUE;

	FIFOInode* fifo = (FIFOInode*)vnode->private_node;
	fs_vnode* superVnode = fifo->SuperVnode();

	if (superVnode->ops->write_stat == NULL)
		return B_BAD_VALUE;

	status_t error = superVnode->ops->write_stat(volume, superVnode, st,
		statMask);
	if (error != B_OK)
		return error;

	return B_OK;
}


static status_t
fifo_ioctl(fs_volume *_volume, fs_vnode *_vnode, void *_cookie, ulong op,
	void *buffer, size_t length)
{
	TRACE(("fifo_ioctl: vnode %p, cookie %p, op %ld, buf %p, len %ld\n",
		_vnode, _cookie, op, buffer, length));

	return EINVAL;
}


static status_t
fifo_set_flags(fs_volume *_volume, fs_vnode *_vnode, void *_cookie,
	int flags)
{
	file_cookie *cookie = (file_cookie *)_cookie;

	TRACE(("fifo_set_flags(vnode = %p, flags = %x)\n", _vnode, flags));
	cookie->open_mode = (cookie->open_mode & ~(O_APPEND | O_NONBLOCK)) | flags;
	return B_OK;
}


static status_t
fifo_select(fs_volume *_volume, fs_vnode *_node, void *_cookie,
	uint8 event, uint32 ref, selectsync *sync)
{
	file_cookie *cookie = (file_cookie *)_cookie;

	TRACE(("fifo_select(vnode = %p)\n", _node));
	Inode *inode = (Inode *)_node->private_node;
	if (!inode)
		return B_ERROR;

	BenaphoreLocker locker(inode->RequestLock());
	return inode->Select(event, ref, sync, cookie->open_mode);
}


static status_t
fifo_deselect(fs_volume *_volume, fs_vnode *_node, void *_cookie,
	uint8 event, selectsync *sync)
{
	file_cookie *cookie = (file_cookie *)_cookie;

	TRACE(("fifo_deselect(vnode = %p)\n", _node));
	Inode *inode = (Inode *)_node->private_node;
	if (!inode)
		return B_ERROR;
	
	BenaphoreLocker locker(inode->RequestLock());
	return inode->Deselect(event, sync, cookie->open_mode);
}


static bool
fifo_can_page(fs_volume *_volume, fs_vnode *_v, void *cookie)
{
	return false;
}


static status_t
fifo_read_pages(fs_volume *_volume, fs_vnode *_v, void *cookie, off_t pos,
	const iovec *vecs, size_t count, size_t *_numBytes, bool reenter)
{
	return B_NOT_ALLOWED;
}


static status_t
fifo_write_pages(fs_volume *_volume, fs_vnode *_v, void *cookie,
	off_t pos, const iovec *vecs, size_t count, size_t *_numBytes, bool reenter)
{
	return B_NOT_ALLOWED;
}


static status_t
fifo_get_super_vnode(fs_volume *volume, fs_vnode *vnode, fs_volume *superVolume,
	fs_vnode *_superVnode)
{
	FIFOInode* fifo = (FIFOInode*)vnode->private_node;
	fs_vnode* superVnode = fifo->SuperVnode();

	if (superVnode->ops->get_super_vnode != NULL) {
		return superVnode->ops->get_super_vnode(volume, superVnode, superVolume,
			_superVnode);
	}

	*_superVnode = *superVnode;

	return B_OK;
}


static fs_vnode_ops sFIFOVnodeOps = {
	NULL,	// lookup
	NULL,	// get_vnode_name
					// TODO: This is suboptimal! We'd need to forward the
					// super node's hook, if it has got one.

	&fifo_put_vnode,
	&fifo_remove_vnode,

	&fifo_can_page,
	&fifo_read_pages,
	&fifo_write_pages,

	NULL,	// get_file_map

	/* common */
	&fifo_ioctl,
	&fifo_set_flags,
	&fifo_select,
	&fifo_deselect,
	&fifo_fsync,

	NULL,	// fs_read_link
	NULL,	// fs_symlink
	NULL,	// fs_link
	NULL,	// unlink
	NULL,	// rename

	NULL,	// fs_access()
	&fifo_read_stat,
	&fifo_write_stat,

	/* file */
	NULL,	// create()
	&fifo_open,
	&fifo_close,
	&fifo_free_cookie,
	&fifo_read,
	&fifo_write,

	/* directory */
	NULL,	// create_dir
	NULL,	// remove_dir
	NULL,	// open_dir
	NULL,	// close_dir
	NULL,	// free_dir_cookie
	NULL,	// read_dir
	NULL,	// rewind_dir

	/* attribute directory operations */
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
	&fifo_get_super_vnode,
};


}	// namespace fifo

using namespace fifo;


// #pragma mark -


status_t
create_fifo_vnode(fs_volume* superVolume, fs_vnode* vnode)
{
	FIFOInode *fifo = new(std::nothrow) FIFOInode(vnode);
	if (fifo == NULL)
		return B_NO_MEMORY;

	status_t status = fifo->InitCheck();
	if (status != B_OK) {
		delete fifo;
		return status;
	}

	vnode->private_node = fifo;
	vnode->ops = &sFIFOVnodeOps;

	return B_OK;
}
