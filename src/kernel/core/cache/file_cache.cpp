/*
 * Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "vnode_store.h"

#include <KernelExport.h>
#include <fs_cache.h>

#include <util/kernel_cpp.h>
#include <file_cache.h>
#include <vfs.h>
#include <vm.h>
#include <vm_page.h>
#include <vm_cache.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>


//#define TRACE_FILE_CACHE
#ifdef TRACE_FILE_CACHE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define MAX_IO_VECS 32


struct file_cache_ref {
	vm_cache_ref	*cache;
	void			*vnode;
	void			*device;
	void			*cookie;
};


static struct cache_module_info *sCacheModule;


static void
add_to_iovec(iovec *vecs, int32 &index, int32 max, addr_t address, size_t size)
{
	if (index > 0 && (addr_t)vecs[index - 1].iov_base + vecs[index - 1].iov_len == address) {
		// the iovec can be combined with the previous one
		vecs[index - 1].iov_len += size;
		return;
	}

	// we need to start a new iovec
	vecs[index].iov_base = (void *)address;
	vecs[index].iov_len = size;
	index++;
}


static status_t
pages_io(file_cache_ref *ref, off_t offset, const iovec *vecs, size_t count,
	size_t *_numBytes, bool doWrite)
{
	TRACE(("pages_io: ref = %p, offset = %Ld, size = %lu, %s\n", ref, offset,
		*_numBytes, doWrite ? "write" : "read"));

	// translate the iovecs into direct device accesses
	file_io_vec fileVecs[16];
	size_t fileVecCount = 16;
	size_t numBytes = *_numBytes;

	status_t status = vfs_get_file_map(ref->vnode, offset, numBytes, fileVecs, &fileVecCount);
	if (status < B_OK)
		return status;

	// ToDo: handle array overflow gracefully!

#ifdef TRACE_FILE_CACHE
	dprintf("got %lu file vecs:\n", fileVecCount);
	for (size_t i = 0; i < fileVecCount; i++)
		dprintf("[%lu] offset = %Ld, size = %Ld\n", i, fileVecs[i].offset, fileVecs[i].length);
#endif

	uint32 fileVecIndex;
	size_t size;

	if (!doWrite) {
		// now directly read the data from the device
		// the first file_io_vec can be read directly

		size = fileVecs[0].length;
		if (size > numBytes)
			size = numBytes;

		status = vfs_read_pages(ref->device, ref->cookie, fileVecs[0].offset, vecs, count, &size);
		if (status < B_OK)
			return status;

		// ToDo: this is a work-around for buggy device drivers!
		//	When our own drivers honour the length, we can:
		//	a) also use this direct I/O for writes (otherwise, it would overwrite precious data)
		//	b) panic if the term below is true (at least for writes)
		if (size > fileVecs[0].length) {
			dprintf("warning: device driver %p doesn't respect total length in read_pages() call!\n", ref->device);
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
	} else {
		fileVecIndex = 0;
		size = 0;
	}

	// Too bad, let's process the rest of the file_io_vecs

	size_t totalSize = size;

	// first, find out where we have to continue in our iovecs
	uint32 i = 0;
	for (; i < count; i++) {
		if (size <= vecs[i].iov_len)
			break;

		size -= vecs[i].iov_len;
	}

	size_t vecOffset = size;

	for (; fileVecIndex < fileVecCount; fileVecIndex++) {
		file_io_vec &fileVec = fileVecs[fileVecIndex];
		iovec tempVecs[8];
		uint32 tempCount = 1;

		tempVecs[0].iov_base = (void *)((addr_t)vecs[i].iov_base + vecOffset);

		size = min_c(vecs[i].iov_len - vecOffset, fileVec.length);
		tempVecs[0].iov_len = size;

		TRACE(("fill vec %ld, offset = %lu, size = %lu\n", i, vecOffset, size));

		if (size >= fileVec.length)
			vecOffset += size;
		else
			vecOffset = 0;

		while (size < fileVec.length && ++i < count) {
			tempVecs[tempCount].iov_base = vecs[i].iov_base;
			tempCount++;

			// is this iovec larger than the file_io_vec?
			if (vecs[i].iov_len + size > fileVec.length) {
				size += tempVecs[tempCount].iov_len = vecOffset = fileVec.length - size;
				break;
			}

			size += tempVecs[tempCount].iov_len = vecs[i].iov_len;
		}

		size_t bytes = size;
		if (doWrite)
			status = vfs_write_pages(ref->device, ref->cookie, fileVec.offset, tempVecs, tempCount, &bytes);
		else
			status = vfs_read_pages(ref->device, ref->cookie, fileVec.offset, tempVecs, tempCount, &bytes);
		if (status < B_OK)
			return status;

		totalSize += size;

		if (size != bytes) {
			// there are no more bytes, let's bail out
			*_numBytes = totalSize;
			return B_OK;
		}
	}

	return B_OK;
}


/**	This function reads \a size bytes directly from the file into the cache.
 *	If \a bufferSize does not equal zero, the data read in is also copied to
 *	the provided buffer.
 *	This function always allocates all pages; it is the responsibility of the
 *	calling function to only ask for yet uncached ranges.
 */

static status_t
read_into_cache(file_cache_ref *ref, off_t offset, size_t size, addr_t buffer, size_t bufferSize)
{
	TRACE(("read_from_cache: ref = %p, offset = %Ld, size = %lu, buffer = %p, bufferSize = %lu\n", ref, offset, size, (void *)buffer, bufferSize));

	vm_cache_ref *cache = ref->cache;
	iovec vecs[MAX_IO_VECS];
	int32 vecCount = 0;

	// make sure "offset" is page aligned - but also remember the page offset
	int32 pageOffset = offset & (B_PAGE_SIZE - 1);
	size = PAGE_ALIGN(size + pageOffset);
	offset -= pageOffset;

	vm_page *pages[32];
	int32 pageIndex = 0;

	// ToDo: fix this
	if (size > 32 * B_PAGE_SIZE)
		panic("cannot handle large I/O - fix me!\n");

	// allocate pages for the cache and mark them busy
	for (size_t pos = 0; pos < size; pos += B_PAGE_SIZE) {
		vm_page *page = pages[pageIndex++] = vm_page_allocate_page(PAGE_STATE_FREE);
		page->state = PAGE_STATE_BUSY;

		vm_cache_insert_page(ref->cache, page, offset + pos);

		addr_t virtualAddress;
		vm_get_physical_page(page->ppn * B_PAGE_SIZE, &virtualAddress, PHYSICAL_PAGE_CAN_WAIT);

		add_to_iovec(vecs, vecCount, MAX_IO_VECS, virtualAddress, B_PAGE_SIZE);
		// ToDo: check if the array is large enough!
	}

	mutex_unlock(&cache->lock);

	// read file into reserved pages
	status_t status = pages_io(ref, offset, vecs, vecCount, &size, false);
	if (status < B_OK) {
		// ToDo: remove allocated pages...
		panic("file_cache: remove allocated pages! read pages failed: %s\n", strerror(status));
		mutex_lock(&cache->lock);
		return status;
	}

	// copy the pages and unmap them again

	for (int32 i = 0; i < vecCount; i++) {
		addr_t base = (addr_t)vecs[i].iov_base;
		size_t size = vecs[i].iov_len;

		// copy to user buffer if necessary
		if (bufferSize != 0) {
			size_t bytes = min_c(bufferSize, size - pageOffset);

			user_memcpy((void *)buffer, (void *)(base + pageOffset), bytes);
			buffer += bytes;
			bufferSize -= bytes;
		}

		for (size_t pos = 0; pos < size; pos += B_PAGE_SIZE, base += B_PAGE_SIZE)
			vm_put_physical_page(base);
	}

	mutex_lock(&cache->lock);

	// make the pages accessible in the cache
	for (int32 i = pageIndex; i-- > 0;)
		pages[i]->state = PAGE_STATE_ACTIVE;

	return B_OK;
}


static status_t
write_to_cache(file_cache_ref *ref, off_t offset, size_t size, addr_t buffer, size_t bufferSize)
{
	TRACE(("write_to_cache: ref = %p, offset = %Ld, size = %lu\n", ref, offset, bufferSize));

	iovec vecs[MAX_IO_VECS];
	int32 vecCount = 0;

	// make sure "offset" is page aligned - but also remember the page offset
	int32 pageOffset = offset & (B_PAGE_SIZE - 1);
	size = PAGE_ALIGN(size + pageOffset);
	offset -= pageOffset;

	vm_page *pages[32];
	int32 pageIndex = 0;

	// ToDo: fix this
	if (size > 32 * B_PAGE_SIZE)
		panic("cannot handle large I/O - fix me!\n");

	// allocate pages for the cache and mark them busy
	for (size_t pos = 0; pos < size; pos += B_PAGE_SIZE) {
		vm_page *page = pages[pageIndex++] = vm_page_allocate_page(PAGE_STATE_FREE);
		page->state = PAGE_STATE_BUSY;

		vm_cache_insert_page(ref->cache, page, offset + pos);

		addr_t virtualAddress;
		vm_get_physical_page(page->ppn * B_PAGE_SIZE, &virtualAddress, PHYSICAL_PAGE_CAN_WAIT);

		add_to_iovec(vecs, vecCount, MAX_IO_VECS, virtualAddress, B_PAGE_SIZE);
		// ToDo: check if the array is large enough!

		size_t bytes = min_c(bufferSize, size_t(B_PAGE_SIZE - pageOffset));
		if (bytes != B_PAGE_SIZE) {
			// This is only a partial write, so we have to read the rest of the page
			// from the file to have consistent data in the cache
			size_t bytesRead = B_PAGE_SIZE;
			iovec readVec = { (void *)virtualAddress, B_PAGE_SIZE };

			pages_io(ref, offset + pos, &readVec, 1, &bytesRead, false);
			// ToDo: handle errors!
		}

		// copy data from user buffer if necessary
		if (bufferSize != 0) {
			user_memcpy((void *)(virtualAddress + pageOffset), (void *)buffer, bytes);
			buffer += bytes;
			bufferSize -= bytes;

			vm_page_set_state(page, PAGE_STATE_MODIFIED);
		}
	}

	// ToDo: we only have to write the pages back immediately if write-back mode
	//	is disabled, which is not possible right now
#if 0
	// write cached pages back to the file if we were asked to do that
	status_t status = readwrite_pages(ref, offset, vecs, vecCount, &size, true);
	if (status < B_OK) {
		// ToDo: remove allocated pages...
		panic("file_cache: remove allocated pages! write pages failed: %s\n", strerror(status));
		return status;
	}
#endif

	// unmap the pages again

	for (int32 i = 0; i < vecCount; i++) {
		addr_t base = (addr_t)vecs[i].iov_base;
		size_t size = vecs[i].iov_len;
		for (size_t pos = 0; pos < size; pos += B_PAGE_SIZE, base += B_PAGE_SIZE)
			vm_put_physical_page(base);
	}

	// make the pages accessible in the cache
	for (int32 i = pageIndex; i-- > 0;) {
		if (pages[i]->state == PAGE_STATE_BUSY)
			pages[i]->state = PAGE_STATE_ACTIVE;
	}

	return B_OK;
}


static status_t
cache_io(void *_cacheRef, off_t offset, addr_t bufferBase, size_t *_size, bool doWrite)
{
	if (_cacheRef == NULL)
		panic("cache_io() called with NULL ref!\n");

	file_cache_ref *ref = (file_cache_ref *)_cacheRef;
	vm_cache_ref *cache = ref->cache;
	off_t fileSize = cache->cache->virtual_size;

	TRACE(("cache_io(ref = %p, offset = %Ld, buffer = %p, size = %lu, %s)\n",
		ref, offset, (void *)bufferBase, *_size, doWrite ? "write" : "read"));

	// out of bounds access?
	if (offset >= fileSize || offset < 0) {
		*_size = 0;
		return B_OK;
	}

	int32 pageOffset = offset & (B_PAGE_SIZE - 1);
	size_t size = *_size + pageOffset;
	offset -= pageOffset;

	if (offset + size > fileSize) {
		// adapt size to be within the file's offsets
		size = fileSize - offset;
		*_size = size;
	}

	size_t bytesLeft = size, lastLeft = size;
	addr_t buffer = bufferBase;
	off_t lastOffset = offset;

	mutex_lock(&cache->lock);

	for (; bytesLeft > 0; offset += B_PAGE_SIZE) {
		// check if this page is already in memory
		addr_t virtualAddress;
	restart:
		vm_page *page = vm_cache_lookup_page(cache, offset);
		if (page != NULL && page->state == PAGE_STATE_BUSY) {
			// ToDo: don't wait forever!
			mutex_unlock(&cache->lock);
			snooze(20000);
			mutex_lock(&cache->lock);
			goto restart;
		}

		TRACE(("lookup page from offset %Ld: %p\n", offset, page));
		if (page != NULL
			&& vm_get_physical_page(page->ppn * B_PAGE_SIZE, &virtualAddress, PHYSICAL_PAGE_CAN_WAIT) == B_OK) {
			// it is, so let's satisfy in the first part of the request
			if (bufferBase != buffer) {
				size_t requestSize = buffer - bufferBase;
				if ((doWrite && write_to_cache(ref, lastOffset + pageOffset, requestSize, bufferBase, requestSize) != B_OK)
					|| (!doWrite && read_into_cache(ref, lastOffset + pageOffset, requestSize, bufferBase, requestSize) != B_OK)) {
					vm_put_physical_page(virtualAddress);
					mutex_unlock(&cache->lock);
					return B_IO_ERROR;
				}
				bufferBase += requestSize;
			}

			// and copy the contents of the page already in memory
			if (doWrite)
				user_memcpy((void *)(virtualAddress + pageOffset), (void *)buffer, min_c(B_PAGE_SIZE, bytesLeft) - pageOffset);
			else
				user_memcpy((void *)buffer, (void *)(virtualAddress + pageOffset), min_c(B_PAGE_SIZE, bytesLeft) - pageOffset);

			vm_put_physical_page(virtualAddress);

			bufferBase += B_PAGE_SIZE - pageOffset;
			pageOffset = 0;

			if (bytesLeft <= B_PAGE_SIZE) {
				// we've read the last page, so we're done!
				mutex_unlock(&cache->lock);
				return B_OK;
			}

			// prepare a potential gap request
			lastOffset = offset + B_PAGE_SIZE;
			lastLeft = bytesLeft - B_PAGE_SIZE;
		}

		if (bytesLeft <= B_PAGE_SIZE)
			break;

		buffer += B_PAGE_SIZE;
		bytesLeft -= B_PAGE_SIZE;
	}

	// fill the last remainding bytes of the request (either write or read)

	lastOffset += pageOffset;

	status_t status;
	if (doWrite)
		status = write_to_cache(ref, lastOffset, lastLeft, bufferBase, lastLeft);
	else
		status = read_into_cache(ref, lastOffset, lastLeft, bufferBase, lastLeft);

	mutex_unlock(&cache->lock);
	return status;
}


//	#pragma mark -
//	kernel public API


extern "C" void
cache_prefetch(mount_id mountID, vnode_id vnodeID)
{
	vm_cache_ref *cache;
	void *vnode;

	// ToDo: schedule prefetch
	// ToDo: maybe get 1) access type (random/sequential), 2) file vecs which blocks to prefetch
	//	for now, we just prefetch the first 64 kB

	TRACE(("cache_prefetch(vnode %ld:%Ld)\n", mountID, vnodeID));

	// get the vnode for the object, this also grabs a ref to it
	if (vfs_get_vnode(mountID, vnodeID, &vnode) != B_OK)
		return;

	if (vfs_get_vnode_cache(vnode, &cache) != B_OK) {
		vfs_vnode_release_ref(vnode);
		return;
	}

	file_cache_ref *ref = (struct file_cache_ref *)((vnode_store *)cache->cache->store)->file_cache_ref;
	off_t fileSize = cache->cache->virtual_size;

	size_t size = 65536;
	if (size > fileSize)
		size = fileSize;

	size_t bytesLeft = size, lastLeft = size;
	off_t lastOffset = 0;
	size_t lastSize = 0;

	mutex_lock(&cache->lock);

	for (off_t offset = 0; bytesLeft > 0; offset += B_PAGE_SIZE) {
		// check if this page is already in memory
		addr_t virtualAddress;
	restart:
		vm_page *page = vm_cache_lookup_page(cache, offset);
		if (page != NULL) {
			// it is, so let's satisfy in the first part of the request
			if (lastOffset < offset) {
				size_t requestSize = offset - lastOffset;
				read_into_cache(ref, lastOffset, requestSize, NULL, 0);
			}

			if (bytesLeft <= B_PAGE_SIZE) {
				// we've read the last page, so we're done!
				goto out;
			}

			// prepare a potential gap request
			lastOffset = offset + B_PAGE_SIZE;
			lastLeft = bytesLeft - B_PAGE_SIZE;
		}

		if (bytesLeft <= B_PAGE_SIZE)
			break;

		bytesLeft -= B_PAGE_SIZE;
	}

	// read in the last part
	read_into_cache(ref, lastOffset, lastLeft, NULL, 0);

out:
	mutex_unlock(&cache->lock);
	vfs_vnode_release_ref(vnode);
}


extern "C" void
cache_node_opened(vm_cache_ref *cache, mount_id mountID, vnode_id vnodeID)
{
	if (cache == NULL)
		return;

	file_cache_ref *ref = (file_cache_ref *)((vnode_store *)cache->cache->store)->file_cache_ref;

	if (ref != NULL && sCacheModule != NULL)
		sCacheModule->node_opened(ref->cache->cache->virtual_size, mountID, vnodeID);
}


extern "C" void
cache_node_closed(vm_cache_ref *cache, mount_id mountID, vnode_id vnodeID)
{
	if (cache == NULL)
		return;

	file_cache_ref *ref = (file_cache_ref *)((vnode_store *)cache->cache->store)->file_cache_ref;

	if (ref != NULL && sCacheModule != NULL)
		sCacheModule->node_closed(mountID, vnodeID);
}


extern "C" status_t
file_cache_init(void)
{
	// ToDo: get cache module
	return B_OK;
}


//	#pragma mark -
//	public FS API


extern "C" void *
file_cache_create(mount_id mountID, vnode_id vnodeID, off_t size, int fd)
{
	TRACE(("file_cache_create(mountID = %ld, vnodeID = %Ld, size = %Ld, fd = %d)\n", mountID, vnodeID, size, fd));

	file_cache_ref *ref = new file_cache_ref;
	if (ref == NULL)
		return NULL;

	// get the vnode of the underlying device
	if (vfs_get_vnode_from_fd(fd, true, &ref->device) != B_OK)
		goto err1;

	// we also need the cookie of the underlying device to properly access it
	if (vfs_get_cookie_from_fd(fd, &ref->cookie) != B_OK)
		goto err2;

	// get the vnode for the object (note, this does not grab a reference to the node)
	if (vfs_lookup_vnode(mountID, vnodeID, &ref->vnode) != B_OK)
		goto err2;

	if (vfs_get_vnode_cache(ref->vnode, &ref->cache) != B_OK)
		goto err3;

	ref->cache->cache->virtual_size = size;
	((vnode_store *)ref->cache->cache->store)->file_cache_ref = ref;
	return ref;

err3:
	vfs_vnode_release_ref(ref->vnode);
err2:
	vfs_vnode_release_ref(ref->device);
err1:
	delete ref;
	return NULL;
}


extern "C" void
file_cache_delete(void *_cacheRef)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;

	if (ref == NULL)
		return;

	TRACE(("file_cache_delete(ref = %p)\n", ref));

	vfs_vnode_release_ref(ref->device);
	delete ref;
}


extern "C" status_t
file_cache_set_size(void *_cacheRef, off_t size)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;

	TRACE(("file_cache_set_size(ref = %p, size = %Ld)\n", ref, size));

	if (ref == NULL)
		return B_OK;

	mutex_lock(&ref->cache->lock);
	status_t status = vm_cache_resize(ref->cache, size);
	mutex_unlock(&ref->cache->lock);

	return status;
}


extern "C" status_t
file_cache_sync(void *_cacheRef)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;
	if (ref == NULL)
		return B_BAD_VALUE;

	return vm_cache_write_modified(ref->cache);
}


extern "C" status_t
file_cache_read_pages(void *_cacheRef, off_t offset, const iovec *vecs, size_t count, size_t *_numBytes)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;

	return pages_io(ref, offset, vecs, count, _numBytes, false);
}


extern "C" status_t
file_cache_write_pages(void *_cacheRef, off_t offset, const iovec *vecs, size_t count, size_t *_numBytes)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;

	status_t status = pages_io(ref, offset, vecs, count, _numBytes, true);
	TRACE(("file_cache_write_pages(ref = %p, offset = %Ld, vecs = %p, count = %lu, bytes = %lu) = %ld\n",
		ref, offset, vecs, count, *_numBytes, status));

	return status;
}


extern "C" status_t
file_cache_read(void *_cacheRef, off_t offset, void *bufferBase, size_t *_size)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;

	TRACE(("file_cache_read(ref = %p, offset = %Ld, buffer = %p, size = %lu)\n",
		ref, offset, bufferBase, *_size));

	return cache_io(ref, offset, (addr_t)bufferBase, _size, false);
}


extern "C" status_t
file_cache_write(void *_cacheRef, off_t offset, const void *buffer, size_t *_size)
{
	file_cache_ref *ref = (file_cache_ref *)_cacheRef;

	status_t status = cache_io(ref, offset, (addr_t)const_cast<void *>(buffer), _size, true);
	TRACE(("file_cache_write(ref = %p, offset = %Ld, buffer = %p, size = %lu) = %ld\n",
		ref, offset, buffer, *_size, status));

	return status;
}


