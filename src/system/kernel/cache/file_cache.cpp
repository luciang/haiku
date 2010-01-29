/*
 * Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "vnode_store.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <fs_cache.h>

#include <condition_variable.h>
#include <file_cache.h>
#include <generic_syscall.h>
#include <low_resource_manager.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/kernel_cpp.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/VMCache.h>

#include "IORequest.h"


//#define TRACE_FILE_CACHE
#ifdef TRACE_FILE_CACHE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

// maximum number of iovecs per request
#define MAX_IO_VECS			32	// 128 kB
#define MAX_FILE_IO_VECS	32

#define BYPASS_IO_SIZE		65536
#define LAST_ACCESSES		3

struct file_cache_ref {
	VMCache			*cache;
	struct vnode	*vnode;
	off_t			last_access[LAST_ACCESSES];
		// TODO: it would probably be enough to only store the least
		//	significant 31 bits, and make this uint32 (one bit for
		//	write vs. read)
	int32			last_access_index;
	uint16			disabled_count;

	inline void SetLastAccess(int32 index, off_t access, bool isWrite)
	{
		// we remember writes as negative offsets
		last_access[index] = isWrite ? -access : access;
	}

	inline off_t LastAccess(int32 index, bool isWrite)
	{
		return isWrite ? -last_access[index] : last_access[index];
	}

	inline uint32 LastAccessPageOffset(int32 index, bool isWrite)
	{
		return LastAccess(index, isWrite) >> PAGE_SHIFT;
	}
};

class PrecacheIO : public AsyncIOCallback {
public:
								PrecacheIO(file_cache_ref* ref, off_t offset,
									size_t size);
								~PrecacheIO();

			status_t			Prepare();
			void				ReadAsync();

	virtual	void				IOFinished(status_t status,
									bool partialTransfer,
									size_t bytesTransferred);

private:
			file_cache_ref*		fRef;
			VMCache*			fCache;
			vm_page**			fPages;
			size_t				fPageCount;
			ConditionVariable*	fBusyConditions;
			iovec*				fVecs;
			off_t				fOffset;
			uint32				fVecCount;
			size_t				fSize;
#if DEBUG_PAGE_ACCESS
			thread_id			fAllocatingThread;
#endif
};

typedef status_t (*cache_func)(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	size_t lastReservedPages, size_t reservePages);

static void add_to_iovec(iovec* vecs, uint32 &index, uint32 max, addr_t address,
	size_t size);


static struct cache_module_info* sCacheModule;


static const uint32 kZeroVecCount = 32;
static const size_t kZeroVecSize = kZeroVecCount * B_PAGE_SIZE;
static addr_t sZeroPage;	// physical address
static iovec sZeroVecs[kZeroVecCount];


//	#pragma mark -


PrecacheIO::PrecacheIO(file_cache_ref* ref, off_t offset, size_t size)
	:
	fRef(ref),
	fCache(ref->cache),
	fPages(NULL),
	fVecs(NULL),
	fOffset(offset),
	fVecCount(0),
	fSize(size)
{
	fPageCount = (size + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
	fCache->AcquireRefLocked();
}


PrecacheIO::~PrecacheIO()
{
	delete[] fPages;
	delete[] fVecs;
	fCache->ReleaseRefLocked();
}


status_t
PrecacheIO::Prepare()
{
	if (fPageCount == 0)
		return B_BAD_VALUE;

	fPages = new(std::nothrow) vm_page*[fPageCount];
	if (fPages == NULL)
		return B_NO_MEMORY;

	fVecs = new(std::nothrow) iovec[fPageCount];
	if (fVecs == NULL)
		return B_NO_MEMORY;

	// allocate pages for the cache and mark them busy
	uint32 i = 0;
	for (size_t pos = 0; pos < fSize; pos += B_PAGE_SIZE) {
		vm_page* page = vm_page_allocate_page(
			PAGE_STATE_ACTIVE | VM_PAGE_ALLOC_BUSY);

		fCache->InsertPage(page, fOffset + pos);

		add_to_iovec(fVecs, fVecCount, fPageCount,
			page->physical_page_number * B_PAGE_SIZE, B_PAGE_SIZE);
		fPages[i++] = page;
	}

#if DEBUG_PAGE_ACCESS
	fAllocatingThread = find_thread(NULL);
#endif

	return B_OK;
}


void
PrecacheIO::ReadAsync()
{
	// This object is going to be deleted after the I/O request has been
	// fulfilled
	vfs_asynchronous_read_pages(fRef->vnode, NULL, fOffset, fVecs, fVecCount,
		fSize, B_PHYSICAL_IO_REQUEST, this);
}


void
PrecacheIO::IOFinished(status_t status, bool partialTransfer,
	size_t bytesTransferred)
{
	AutoLocker<VMCache> locker(fCache);

	// Make successfully loaded pages accessible again (partially
	// transferred pages are considered failed)
	size_t pagesTransferred
		= (bytesTransferred + B_PAGE_SIZE - 1) / B_PAGE_SIZE;

	if (fOffset + bytesTransferred > fCache->virtual_end)
		bytesTransferred = fCache->virtual_end - fOffset;

	for (uint32 i = 0; i < pagesTransferred; i++) {
		if (i == pagesTransferred - 1
			&& (bytesTransferred % B_PAGE_SIZE) != 0) {
			// clear partial page
			size_t bytesTouched = bytesTransferred % B_PAGE_SIZE;
			vm_memset_physical((fPages[i]->physical_page_number << PAGE_SHIFT)
				+ bytesTouched, 0, B_PAGE_SIZE - bytesTouched);
		}

		DEBUG_PAGE_ACCESS_TRANSFER(fPages[i], fAllocatingThread);

		fCache->MarkPageUnbusy(fPages[i]);

		DEBUG_PAGE_ACCESS_END(fPages[i]);
	}

	// Free pages after failed I/O
	for (uint32 i = pagesTransferred; i < fPageCount; i++) {
		DEBUG_PAGE_ACCESS_TRANSFER(fPages[i], fAllocatingThread);
		fCache->NotifyPageEvents(fPages[i], PAGE_EVENT_NOT_BUSY);
		fCache->RemovePage(fPages[i]);
		vm_page_set_state(fPages[i], PAGE_STATE_FREE);
	}

	delete this;
}


//	#pragma mark -


static void
add_to_iovec(iovec* vecs, uint32 &index, uint32 max, addr_t address,
	size_t size)
{
	if (index > 0 && (addr_t)vecs[index - 1].iov_base
			+ vecs[index - 1].iov_len == address) {
		// the iovec can be combined with the previous one
		vecs[index - 1].iov_len += size;
		return;
	}

	if (index == max)
		panic("no more space for iovecs!");

	// we need to start a new iovec
	vecs[index].iov_base = (void*)address;
	vecs[index].iov_len = size;
	index++;
}


static inline bool
access_is_sequential(file_cache_ref* ref)
{
	return ref->last_access[ref->last_access_index] != 0;
}


static inline void
push_access(file_cache_ref* ref, off_t offset, size_t bytes, bool isWrite)
{
	TRACE(("%p: push %Ld, %ld, %s\n", ref, offset, bytes,
		isWrite ? "write" : "read"));

	int32 index = ref->last_access_index;
	int32 previous = index - 1;
	if (previous < 0)
		previous = LAST_ACCESSES - 1;

	if (offset != ref->LastAccess(previous, isWrite))
		ref->last_access[previous] = 0;

	ref->SetLastAccess(index, offset + bytes, isWrite);

	if (++index >= LAST_ACCESSES)
		index = 0;
	ref->last_access_index = index;
}


static void
reserve_pages(file_cache_ref* ref, size_t reservePages, bool isWrite)
{
	if (low_resource_state(B_KERNEL_RESOURCE_PAGES) != B_NO_LOW_RESOURCE) {
		VMCache* cache = ref->cache;
		cache->Lock();

		if (list_is_empty(&cache->consumers) && cache->areas == NULL
			&& access_is_sequential(ref)) {
			// we are not mapped, and we're accessed sequentially

			if (isWrite) {
				// just schedule some pages to be written back
				int32 index = ref->last_access_index;
				int32 previous = index - 1;
				if (previous < 0)
					previous = LAST_ACCESSES - 1;

				vm_page_schedule_write_page_range(cache,
					ref->LastAccessPageOffset(previous, true),
					ref->LastAccessPageOffset(index, true));
			} else {
				// free some pages from our cache
				// TODO: start with oldest
				uint32 left = reservePages;
				vm_page* page;
				for (VMCachePagesTree::Iterator it = cache->pages.GetIterator();
						(page = it.Next()) != NULL && left > 0;) {
					if (page->state != PAGE_STATE_MODIFIED && !page->busy) {
						DEBUG_PAGE_ACCESS_START(page);
						cache->RemovePage(page);
						vm_page_set_state(page, PAGE_STATE_FREE);
						left--;
					}
				}
			}
		}
		cache->Unlock();
	}

	vm_page_reserve_pages(reservePages, VM_PRIORITY_USER);
}


static inline status_t
read_pages_and_clear_partial(file_cache_ref* ref, void* cookie, off_t offset,
	const iovec* vecs, size_t count, uint32 flags, size_t* _numBytes)
{
	size_t bytesUntouched = *_numBytes;

	status_t status = vfs_read_pages(ref->vnode, cookie, offset, vecs, count,
		flags, _numBytes);

	size_t bytesEnd = *_numBytes;

	if (offset + bytesEnd > ref->cache->virtual_end)
		bytesEnd = ref->cache->virtual_end - offset;

	if (status == B_OK && bytesEnd < bytesUntouched) {
		// Clear out any leftovers that were not touched by the above read.
		// We're doing this here so that not every file system/device has to
		// implement this.
		bytesUntouched -= bytesEnd;

		for (int32 i = count; i-- > 0 && bytesUntouched != 0; ) {
			size_t length = min_c(bytesUntouched, vecs[i].iov_len);
			vm_memset_physical((addr_t)vecs[i].iov_base + vecs[i].iov_len
				- length, 0, length);

			bytesUntouched -= length;
		}
	}

	return status;
}


/*!	Reads the requested amount of data into the cache, and allocates
	pages needed to fulfill that request. This function is called by cache_io().
	It can only handle a certain amount of bytes, and the caller must make
	sure that it matches that criterion.
	The cache_ref lock must be held when calling this function; during
	operation it will unlock the cache, though.
*/
static status_t
read_into_cache(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	size_t lastReservedPages, size_t reservePages)
{
	TRACE(("read_into_cache(offset = %Ld, pageOffset = %ld, buffer = %#lx, "
		"bufferSize = %lu\n", offset, pageOffset, buffer, bufferSize));

	VMCache* cache = ref->cache;

	// TODO: We're using way too much stack! Rather allocate a sufficiently
	// large chunk on the heap.
	iovec vecs[MAX_IO_VECS];
	uint32 vecCount = 0;

	size_t numBytes = PAGE_ALIGN(pageOffset + bufferSize);
	vm_page* pages[MAX_IO_VECS];
	int32 pageIndex = 0;

	// allocate pages for the cache and mark them busy
	for (size_t pos = 0; pos < numBytes; pos += B_PAGE_SIZE) {
		vm_page* page = pages[pageIndex++] = vm_page_allocate_page(
			PAGE_STATE_ACTIVE | VM_PAGE_ALLOC_BUSY);

		cache->InsertPage(page, offset + pos);

		add_to_iovec(vecs, vecCount, MAX_IO_VECS,
			page->physical_page_number * B_PAGE_SIZE, B_PAGE_SIZE);
			// TODO: check if the array is large enough (currently panics)!
	}

	push_access(ref, offset, bufferSize, false);
	cache->Unlock();
	vm_page_unreserve_pages(lastReservedPages);

	// read file into reserved pages
	status_t status = read_pages_and_clear_partial(ref, cookie, offset, vecs,
		vecCount, B_PHYSICAL_IO_REQUEST, &numBytes);
	if (status != B_OK) {
		// reading failed, free allocated pages

		dprintf("file_cache: read pages failed: %s\n", strerror(status));

		cache->Lock();

		for (int32 i = 0; i < pageIndex; i++) {
			cache->NotifyPageEvents(pages[i], PAGE_EVENT_NOT_BUSY);
			cache->RemovePage(pages[i]);
			vm_page_set_state(pages[i], PAGE_STATE_FREE);
		}

		return status;
	}

	// copy the pages if needed and unmap them again

	for (int32 i = 0; i < pageIndex; i++) {
		if (useBuffer && bufferSize != 0) {
			size_t bytes = min_c(bufferSize, (size_t)B_PAGE_SIZE - pageOffset);

			vm_memcpy_from_physical((void*)buffer,
				pages[i]->physical_page_number * B_PAGE_SIZE + pageOffset,
				bytes, true);

			buffer += bytes;
			bufferSize -= bytes;
			pageOffset = 0;
		}
	}

	reserve_pages(ref, reservePages, false);
	cache->Lock();

	// make the pages accessible in the cache
	for (int32 i = pageIndex; i-- > 0;) {
		DEBUG_PAGE_ACCESS_END(pages[i]);

		cache->MarkPageUnbusy(pages[i]);
	}

	return B_OK;
}


static status_t
read_from_file(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	size_t lastReservedPages, size_t reservePages)
{
	TRACE(("read_from_file(offset = %Ld, pageOffset = %ld, buffer = %#lx, "
		"bufferSize = %lu\n", offset, pageOffset, buffer, bufferSize));

	if (!useBuffer)
		return B_OK;

	iovec vec;
	vec.iov_base = (void*)buffer;
	vec.iov_len = bufferSize;

	push_access(ref, offset, bufferSize, false);
	ref->cache->Unlock();
	vm_page_unreserve_pages(lastReservedPages);

	status_t status = vfs_read_pages(ref->vnode, cookie, offset + pageOffset,
		&vec, 1, 0, &bufferSize);

	if (status == B_OK)
		reserve_pages(ref, reservePages, false);

	ref->cache->Lock();

	return status;
}


/*!	Like read_into_cache() but writes data into the cache.
	To preserve data consistency, it might also read pages into the cache,
	though, if only a partial page gets written.
	The same restrictions apply.
*/
static status_t
write_to_cache(file_cache_ref* ref, void* cookie, off_t offset,
	int32 pageOffset, addr_t buffer, size_t bufferSize, bool useBuffer,
	size_t lastReservedPages, size_t reservePages)
{
	// TODO: We're using way too much stack! Rather allocate a sufficiently
	// large chunk on the heap.
	iovec vecs[MAX_IO_VECS];
	uint32 vecCount = 0;
	size_t numBytes = PAGE_ALIGN(pageOffset + bufferSize);
	vm_page* pages[MAX_IO_VECS];
	int32 pageIndex = 0;
	status_t status = B_OK;

	// ToDo: this should be settable somewhere
	bool writeThrough = false;

	// allocate pages for the cache and mark them busy
	for (size_t pos = 0; pos < numBytes; pos += B_PAGE_SIZE) {
		// TODO: if space is becoming tight, and this cache is already grown
		//	big - shouldn't we better steal the pages directly in that case?
		//	(a working set like approach for the file cache)
		// TODO: the pages we allocate here should have been reserved upfront
		//	in cache_io()
		vm_page* page = pages[pageIndex++] = vm_page_allocate_page(
			PAGE_STATE_ACTIVE | VM_PAGE_ALLOC_BUSY);

		ref->cache->InsertPage(page, offset + pos);

		add_to_iovec(vecs, vecCount, MAX_IO_VECS,
			page->physical_page_number * B_PAGE_SIZE, B_PAGE_SIZE);
	}

	push_access(ref, offset, bufferSize, true);
	ref->cache->Unlock();
	vm_page_unreserve_pages(lastReservedPages);

	// copy contents (and read in partially written pages first)

	if (pageOffset != 0) {
		// This is only a partial write, so we have to read the rest of the page
		// from the file to have consistent data in the cache
		iovec readVec = { vecs[0].iov_base, B_PAGE_SIZE };
		size_t bytesRead = B_PAGE_SIZE;

		status = vfs_read_pages(ref->vnode, cookie, offset, &readVec, 1,
			B_PHYSICAL_IO_REQUEST, &bytesRead);
		// ToDo: handle errors for real!
		if (status < B_OK)
			panic("1. vfs_read_pages() failed: %s!\n", strerror(status));
	}

	addr_t lastPageOffset = (pageOffset + bufferSize) & (B_PAGE_SIZE - 1);
	if (lastPageOffset != 0) {
		// get the last page in the I/O vectors
		addr_t last = (addr_t)vecs[vecCount - 1].iov_base
			+ vecs[vecCount - 1].iov_len - B_PAGE_SIZE;

		if (offset + pageOffset + bufferSize == ref->cache->virtual_end) {
			// the space in the page after this write action needs to be cleaned
			vm_memset_physical(last + lastPageOffset, 0,
				B_PAGE_SIZE - lastPageOffset);
		} else {
			// the end of this write does not happen on a page boundary, so we
			// need to fetch the last page before we can update it
			iovec readVec = { (void*)last, B_PAGE_SIZE };
			size_t bytesRead = B_PAGE_SIZE;

			status = vfs_read_pages(ref->vnode, cookie,
				PAGE_ALIGN(offset + pageOffset + bufferSize) - B_PAGE_SIZE,
				&readVec, 1, B_PHYSICAL_IO_REQUEST, &bytesRead);
			// ToDo: handle errors for real!
			if (status < B_OK)
				panic("vfs_read_pages() failed: %s!\n", strerror(status));

			if (bytesRead < B_PAGE_SIZE) {
				// the space beyond the file size needs to be cleaned
				vm_memset_physical(last + bytesRead, 0,
					B_PAGE_SIZE - bytesRead);
			}
		}
	}

	for (uint32 i = 0; i < vecCount; i++) {
		addr_t base = (addr_t)vecs[i].iov_base;
		size_t bytes = min_c(bufferSize,
			size_t(vecs[i].iov_len - pageOffset));

		if (useBuffer) {
			// copy data from user buffer
			vm_memcpy_to_physical(base + pageOffset, (void*)buffer, bytes,
				true);
		} else {
			// clear buffer instead
			vm_memset_physical(base + pageOffset, 0, bytes);
		}

		bufferSize -= bytes;
		if (bufferSize == 0)
			break;

		buffer += bytes;
		pageOffset = 0;
	}

	if (writeThrough) {
		// write cached pages back to the file if we were asked to do that
		status_t status = vfs_write_pages(ref->vnode, cookie, offset, vecs,
			vecCount, B_PHYSICAL_IO_REQUEST, &numBytes);
		if (status < B_OK) {
			// ToDo: remove allocated pages, ...?
			panic("file_cache: remove allocated pages! write pages failed: %s\n",
				strerror(status));
		}
	}

	if (status == B_OK)
		reserve_pages(ref, reservePages, true);

	ref->cache->Lock();

	// make the pages accessible in the cache
	for (int32 i = pageIndex; i-- > 0;) {
		ref->cache->MarkPageUnbusy(pages[i]);

		if (!writeThrough)
			vm_page_set_state(pages[i], PAGE_STATE_MODIFIED);

		DEBUG_PAGE_ACCESS_END(pages[i]);
	}

	return status;
}


static status_t
write_to_file(file_cache_ref* ref, void* cookie, off_t offset, int32 pageOffset,
	addr_t buffer, size_t bufferSize, bool useBuffer, size_t lastReservedPages,
	size_t reservePages)
{
	push_access(ref, offset, bufferSize, true);
	ref->cache->Unlock();
	vm_page_unreserve_pages(lastReservedPages);

	status_t status = B_OK;

	if (!useBuffer) {
		while (bufferSize > 0) {
			size_t written = min_c(bufferSize, kZeroVecSize);
			status = vfs_write_pages(ref->vnode, cookie, offset + pageOffset,
				sZeroVecs, kZeroVecCount, B_PHYSICAL_IO_REQUEST, &written);
			if (status != B_OK)
				return status;
			if (written == 0)
				return B_ERROR;

			bufferSize -= written;
			pageOffset += written;
		}
	} else {
		iovec vec;
		vec.iov_base = (void*)buffer;
		vec.iov_len = bufferSize;
		status = vfs_write_pages(ref->vnode, cookie, offset + pageOffset,
			&vec, 1, 0, &bufferSize);
	}

	if (status == B_OK)
		reserve_pages(ref, reservePages, true);

	ref->cache->Lock();

	return status;
}


static inline status_t
satisfy_cache_io(file_cache_ref* ref, void* cookie, cache_func function,
	off_t offset, addr_t buffer, bool useBuffer, int32 &pageOffset,
	size_t bytesLeft, size_t &reservePages, off_t &lastOffset,
	addr_t &lastBuffer, int32 &lastPageOffset, size_t &lastLeft,
	size_t &lastReservedPages)
{
	if (lastBuffer == buffer)
		return B_OK;

	size_t requestSize = buffer - lastBuffer;
	reservePages = min_c(MAX_IO_VECS, (lastLeft - requestSize
		+ lastPageOffset + B_PAGE_SIZE - 1) >> PAGE_SHIFT);

	status_t status = function(ref, cookie, lastOffset, lastPageOffset,
		lastBuffer, requestSize, useBuffer, lastReservedPages, reservePages);
	if (status == B_OK) {
		lastReservedPages = reservePages;
		lastBuffer = buffer;
		lastLeft = bytesLeft;
		lastOffset = offset;
		lastPageOffset = 0;
		pageOffset = 0;
	}
	return status;
}


static status_t
cache_io(void* _cacheRef, void* cookie, off_t offset, addr_t buffer,
	size_t* _size, bool doWrite)
{
	if (_cacheRef == NULL)
		panic("cache_io() called with NULL ref!\n");

	file_cache_ref* ref = (file_cache_ref*)_cacheRef;
	VMCache* cache = ref->cache;
	off_t fileSize = cache->virtual_end;
	bool useBuffer = buffer != 0;

	TRACE(("cache_io(ref = %p, offset = %Ld, buffer = %p, size = %lu, %s)\n",
		ref, offset, (void*)buffer, *_size, doWrite ? "write" : "read"));

	// out of bounds access?
	if (offset >= fileSize || offset < 0) {
		*_size = 0;
		return B_OK;
	}

	int32 pageOffset = offset & (B_PAGE_SIZE - 1);
	size_t size = *_size;
	offset -= pageOffset;

	if (offset + pageOffset + size > fileSize) {
		// adapt size to be within the file's offsets
		size = fileSize - pageOffset - offset;
		*_size = size;
	}
	if (size == 0)
		return B_OK;

	// "offset" and "lastOffset" are always aligned to B_PAGE_SIZE,
	// the "last*" variables always point to the end of the last
	// satisfied request part

	const uint32 kMaxChunkSize = MAX_IO_VECS * B_PAGE_SIZE;
	size_t bytesLeft = size, lastLeft = size;
	int32 lastPageOffset = pageOffset;
	addr_t lastBuffer = buffer;
	off_t lastOffset = offset;
	size_t lastReservedPages = min_c(MAX_IO_VECS, (pageOffset + bytesLeft
		+ B_PAGE_SIZE - 1) >> PAGE_SHIFT);
	size_t reservePages = 0;
	size_t pagesProcessed = 0;
	cache_func function = NULL;

	reserve_pages(ref, lastReservedPages, doWrite);
	AutoLocker<VMCache> locker(cache);

	while (bytesLeft > 0) {
		// Periodically reevaluate the low memory situation and select the
		// read/write hook accordingly
		if (pagesProcessed % 32 == 0) {
			if (size >= BYPASS_IO_SIZE
				&& low_resource_state(B_KERNEL_RESOURCE_PAGES)
					!= B_NO_LOW_RESOURCE) {
				// In low memory situations we bypass the cache beyond a
				// certain I/O size.
				function = doWrite ? write_to_file : read_from_file;
			} else
				function = doWrite ? write_to_cache : read_into_cache;
		}

		// check if this page is already in memory
		vm_page* page = cache->LookupPage(offset);
		if (page != NULL) {
			// The page may be busy - since we need to unlock the cache sometime
			// in the near future, we need to satisfy the request of the pages
			// we didn't get yet (to make sure no one else interferes in the
			// meantime).
			status_t status = satisfy_cache_io(ref, cookie, function, offset,
				buffer, useBuffer, pageOffset, bytesLeft, reservePages,
				lastOffset, lastBuffer, lastPageOffset, lastLeft,
				lastReservedPages);
			if (status != B_OK)
				return status;

			if (page->busy) {
				cache->WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);
				continue;
			}
		}

		size_t bytesInPage = min_c(size_t(B_PAGE_SIZE - pageOffset), bytesLeft);

		TRACE(("lookup page from offset %Ld: %p, size = %lu, pageOffset "
			"= %lu\n", offset, page, bytesLeft, pageOffset));

		if (page != NULL) {
			// Since we don't actually map pages as part of an area, we have
			// to manually maintain their usage_count
			page->usage_count = 2;
				// TODO: Just because this request comes from the FS API, it
				// doesn't mean the page is not mapped. We might actually
				// decrease the usage count of a hot page here.

			if (doWrite || useBuffer) {
				// Since the following user_mem{cpy,set}() might cause a page
				// fault, which in turn might cause pages to be reserved, we
				// need to unlock the cache temporarily to avoid a potential
				// deadlock. To make sure that our page doesn't go away, we mark
				// it busy for the time.
				page->busy = true;
				locker.Unlock();

				// copy the contents of the page already in memory
				addr_t pageAddress = page->physical_page_number * B_PAGE_SIZE
					+ pageOffset;
				if (doWrite) {
					if (useBuffer) {
						vm_memcpy_to_physical(pageAddress, (void*)buffer,
							bytesInPage, true);
					} else {
						vm_memset_physical(pageAddress, 0, bytesInPage);
					}
				} else if (useBuffer) {
					vm_memcpy_from_physical((void*)buffer, pageAddress,
						bytesInPage, true);
				}

				locker.Lock();

				if (doWrite && page->state != PAGE_STATE_MODIFIED) {
					DEBUG_PAGE_ACCESS_START(page);
					vm_page_set_state(page, PAGE_STATE_MODIFIED);
					DEBUG_PAGE_ACCESS_END(page);
				}

				cache->MarkPageUnbusy(page);
			}

			if (bytesLeft <= bytesInPage) {
				// we've read the last page, so we're done!
				locker.Unlock();
				vm_page_unreserve_pages(lastReservedPages);
				return B_OK;
			}

			// prepare a potential gap request
			lastBuffer = buffer + bytesInPage;
			lastLeft = bytesLeft - bytesInPage;
			lastOffset = offset + B_PAGE_SIZE;
			lastPageOffset = 0;
		}

		if (bytesLeft <= bytesInPage)
			break;

		buffer += bytesInPage;
		bytesLeft -= bytesInPage;
		pageOffset = 0;
		offset += B_PAGE_SIZE;
		pagesProcessed++;

		if (buffer - lastBuffer + lastPageOffset >= kMaxChunkSize) {
			status_t status = satisfy_cache_io(ref, cookie, function, offset,
				buffer, useBuffer, pageOffset, bytesLeft, reservePages,
				lastOffset, lastBuffer, lastPageOffset, lastLeft,
				lastReservedPages);
			if (status != B_OK)
				return status;
		}
	}

	// fill the last remaining bytes of the request (either write or read)

	return function(ref, cookie, lastOffset, lastPageOffset, lastBuffer,
		lastLeft, useBuffer, lastReservedPages, 0);
}


static status_t
file_cache_control(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	switch (function) {
		case CACHE_CLEAR:
			// ToDo: clear the cache
			dprintf("cache_control: clear cache!\n");
			return B_OK;

		case CACHE_SET_MODULE:
		{
			cache_module_info* module = sCacheModule;

			// unset previous module

			if (sCacheModule != NULL) {
				sCacheModule = NULL;
				snooze(100000);	// 0.1 secs
				put_module(module->info.name);
			}

			// get new module, if any

			if (buffer == NULL)
				return B_OK;

			char name[B_FILE_NAME_LENGTH];
			if (!IS_USER_ADDRESS(buffer)
				|| user_strlcpy(name, (char*)buffer,
						B_FILE_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;

			if (strncmp(name, CACHE_MODULES_NAME, strlen(CACHE_MODULES_NAME)))
				return B_BAD_VALUE;

			dprintf("cache_control: set module %s!\n", name);

			status_t status = get_module(name, (module_info**)&module);
			if (status == B_OK)
				sCacheModule = module;

			return status;
		}
	}

	return B_BAD_HANDLER;
}


//	#pragma mark - private kernel API


extern "C" void
cache_prefetch_vnode(struct vnode* vnode, off_t offset, size_t size)
{
	if (size == 0)
		return;

	VMCache* cache;
	if (vfs_get_vnode_cache(vnode, &cache, false) != B_OK)
		return;

	file_cache_ref* ref = ((VMVnodeCache*)cache)->FileCacheRef();
	off_t fileSize = cache->virtual_end;

	if (offset + size > fileSize)
		size = fileSize - offset;
	size_t reservePages = size / B_PAGE_SIZE;

	// Don't do anything if we don't have the resources left, or the cache
	// already contains more than 2/3 of its pages
	if (offset >= fileSize || vm_page_num_unused_pages() < 2 * reservePages
		|| 3 * cache->page_count > 2 * fileSize / B_PAGE_SIZE) {
		cache->ReleaseRef();
		return;
	}

	// "offset" and "size" are always aligned to B_PAGE_SIZE,
	offset &= ~(B_PAGE_SIZE - 1);
	size = ROUNDUP(size, B_PAGE_SIZE);

	size_t bytesToRead = 0;
	off_t lastOffset = offset;

	vm_page_reserve_pages(reservePages, VM_PRIORITY_USER);

	cache->Lock();

	while (true) {
		// check if this page is already in memory
		if (size > 0) {
			vm_page* page = cache->LookupPage(offset);

			offset += B_PAGE_SIZE;
			size -= B_PAGE_SIZE;

			if (page == NULL) {
				bytesToRead += B_PAGE_SIZE;
				continue;
			}
		}
		if (bytesToRead != 0) {
			// read the part before the current page (or the end of the request)
			PrecacheIO* io
				= new(std::nothrow) PrecacheIO(ref, lastOffset, bytesToRead);
			if (io == NULL || io->Prepare() != B_OK) {
				delete io;
				break;
			}

			// we must not have the cache locked during I/O
			cache->Unlock();
			io->ReadAsync();
			cache->Lock();

			bytesToRead = 0;
		}

		if (size == 0) {
			// we have reached the end of the request
			break;
		}

		lastOffset = offset;
	}

	cache->ReleaseRefAndUnlock();
	vm_page_unreserve_pages(reservePages);
		// TODO: We should periodically unreserve as we go, so we don't
		// unnecessarily put pressure on the free page pool.
}


extern "C" void
cache_prefetch(dev_t mountID, ino_t vnodeID, off_t offset, size_t size)
{
	// ToDo: schedule prefetch

	TRACE(("cache_prefetch(vnode %ld:%Ld)\n", mountID, vnodeID));

	// get the vnode for the object, this also grabs a ref to it
	struct vnode* vnode;
	if (vfs_get_vnode(mountID, vnodeID, true, &vnode) != B_OK)
		return;

	cache_prefetch_vnode(vnode, offset, size);
	vfs_put_vnode(vnode);
}


extern "C" void
cache_node_opened(struct vnode* vnode, int32 fdType, VMCache* cache,
	dev_t mountID, ino_t parentID, ino_t vnodeID, const char* name)
{
	if (sCacheModule == NULL || sCacheModule->node_opened == NULL)
		return;

	off_t size = -1;
	if (cache != NULL) {
		file_cache_ref* ref = ((VMVnodeCache*)cache)->FileCacheRef();
		if (ref != NULL)
			size = cache->virtual_end;
	}

	sCacheModule->node_opened(vnode, fdType, mountID, parentID, vnodeID, name,
		size);
}


extern "C" void
cache_node_closed(struct vnode* vnode, int32 fdType, VMCache* cache,
	dev_t mountID, ino_t vnodeID)
{
	if (sCacheModule == NULL || sCacheModule->node_closed == NULL)
		return;

	int32 accessType = 0;
	if (cache != NULL) {
		// ToDo: set accessType
	}

	sCacheModule->node_closed(vnode, fdType, mountID, vnodeID, accessType);
}


extern "C" void
cache_node_launched(size_t argCount, char*  const* args)
{
	if (sCacheModule == NULL || sCacheModule->node_launched == NULL)
		return;

	sCacheModule->node_launched(argCount, args);
}


extern "C" status_t
file_cache_init_post_boot_device(void)
{
	// ToDo: get cache module out of driver settings

	if (get_module("file_cache/launch_speedup/v1",
			(module_info**)&sCacheModule) == B_OK) {
		dprintf("** opened launch speedup: %Ld\n", system_time());
	}
	return B_OK;
}


extern "C" status_t
file_cache_init(void)
{
	// allocate a clean page we can use for writing zeroes
	vm_page_reserve_pages(1, VM_PRIORITY_SYSTEM);
	vm_page* page = vm_page_allocate_page(
		PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR);
	vm_page_unreserve_pages(1);

	sZeroPage = (addr_t)page->physical_page_number * B_PAGE_SIZE;

	for (uint32 i = 0; i < kZeroVecCount; i++) {
		sZeroVecs[i].iov_base = (void*)sZeroPage;
		sZeroVecs[i].iov_len = B_PAGE_SIZE;
	}

	register_generic_syscall(CACHE_SYSCALLS, file_cache_control, 1, 0);
	return B_OK;
}


//	#pragma mark - public FS API


extern "C" void*
file_cache_create(dev_t mountID, ino_t vnodeID, off_t size)
{
	TRACE(("file_cache_create(mountID = %ld, vnodeID = %Ld, size = %Ld)\n",
		mountID, vnodeID, size));

	file_cache_ref* ref = new file_cache_ref;
	if (ref == NULL)
		return NULL;

	memset(ref->last_access, 0, sizeof(ref->last_access));
	ref->last_access_index = 0;
	ref->disabled_count = 0;

	// TODO: delay VMCache creation until data is
	//	requested/written for the first time? Listing lots of
	//	files in Tracker (and elsewhere) could be slowed down.
	//	Since the file_cache_ref itself doesn't have a lock,
	//	we would need to "rent" one during construction, possibly
	//	the vnode lock, maybe a dedicated one.
	//	As there shouldn't be too much contention, we could also
	//	use atomic_test_and_set(), and free the resources again
	//	when that fails...

	// Get the vnode for the object
	// (note, this does not grab a reference to the node)
	if (vfs_lookup_vnode(mountID, vnodeID, &ref->vnode) != B_OK)
		goto err1;

	// Gets (usually creates) the cache for the node
	if (vfs_get_vnode_cache(ref->vnode, &ref->cache, true) != B_OK)
		goto err1;

	ref->cache->virtual_end = size;
	((VMVnodeCache*)ref->cache)->SetFileCacheRef(ref);
	return ref;

err1:
	delete ref;
	return NULL;
}


extern "C" void
file_cache_delete(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	if (ref == NULL)
		return;

	TRACE(("file_cache_delete(ref = %p)\n", ref));

	ref->cache->ReleaseRef();
	delete ref;
}


extern "C" void
file_cache_enable(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	AutoLocker<VMCache> _(ref->cache);

	if (ref->disabled_count == 0) {
		panic("Unbalanced file_cache_enable()!");
		return;
	}

	ref->disabled_count--;
}


extern "C" status_t
file_cache_disable(void* _cacheRef)
{
	// TODO: This function only removes all pages from the cache and prevents
	// that the file cache functions add any new ones until re-enabled. The
	// VM (on page fault) can still add pages, if the file is mmap()ed. We
	// should mark the cache to prevent shared mappings of the file and fix
	// the page fault code to deal correctly with private mappings (i.e. only
	// insert pages in consumer caches).

	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	AutoLocker<VMCache> _(ref->cache);

	// If already disabled, there's nothing to do for us.
	if (ref->disabled_count > 0) {
		ref->disabled_count++;
		return B_OK;
	}

	// The file cache is not yet disabled. We need to evict all cached pages.
	status_t error = ref->cache->FlushAndRemoveAllPages();
	if (error != B_OK)
		return error;

	ref->disabled_count++;
	return B_OK;
}


extern "C" bool
file_cache_is_enabled(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;
	AutoLocker<VMCache> _(ref->cache);

	return ref->disabled_count == 0;
}


extern "C" status_t
file_cache_set_size(void* _cacheRef, off_t newSize)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	TRACE(("file_cache_set_size(ref = %p, size = %Ld)\n", ref, newSize));

	if (ref == NULL)
		return B_OK;

	VMCache* cache = ref->cache;
	AutoLocker<VMCache> _(cache);

	off_t oldSize = cache->virtual_end;
	status_t status = cache->Resize(newSize, VM_PRIORITY_USER);
		// Note, the priority doesn't really matter, since this cache doesn't
		// reserve any memory.
	if (status == B_OK && newSize < oldSize) {
		// We may have a new partial page at the end of the cache that must be
		// cleared.
		uint32 partialBytes = newSize % B_PAGE_SIZE;
		if (partialBytes != 0) {
			vm_page* page = cache->LookupPage(newSize - partialBytes);
			if (page != NULL) {
				vm_memset_physical(page->physical_page_number * B_PAGE_SIZE
					+ partialBytes, 0, B_PAGE_SIZE - partialBytes);
			}
		}
	}

	return status;
}


extern "C" status_t
file_cache_sync(void* _cacheRef)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;
	if (ref == NULL)
		return B_BAD_VALUE;

	return ref->cache->WriteModified();
}


extern "C" status_t
file_cache_read(void* _cacheRef, void* cookie, off_t offset, void* buffer,
	size_t* _size)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	TRACE(("file_cache_read(ref = %p, offset = %Ld, buffer = %p, size = %lu)\n",
		ref, offset, buffer, *_size));

	if (ref->disabled_count > 0) {
		// Caching is disabled -- read directly from the file.
		iovec vec;
		vec.iov_base = buffer;
		vec.iov_len = *_size;
		return vfs_read_pages(ref->vnode, cookie, offset, &vec, 1, 0, _size);
	}

	return cache_io(ref, cookie, offset, (addr_t)buffer, _size, false);
}


extern "C" status_t
file_cache_write(void* _cacheRef, void* cookie, off_t offset,
	const void* buffer, size_t* _size)
{
	file_cache_ref* ref = (file_cache_ref*)_cacheRef;

	if (ref->disabled_count > 0) {
		// Caching is disabled -- write directly to the file.

		if (buffer != NULL) {
			iovec vec;
			vec.iov_base = (void*)buffer;
			vec.iov_len = *_size;
			return vfs_write_pages(ref->vnode, cookie, offset, &vec, 1, 0,
				_size);
		}

		// NULL buffer -- use a dummy buffer to write zeroes
		size_t size = *_size;
		while (size > 0) {
			size_t toWrite = min_c(size, kZeroVecSize);
			size_t written = toWrite;
			status_t error = vfs_write_pages(ref->vnode, cookie, offset,
				sZeroVecs, kZeroVecCount, B_PHYSICAL_IO_REQUEST, &written);
			if (error != B_OK)
				return error;
			if (written == 0)
				break;

			offset += written;
			size -= written;
		}

		*_size -= size;
		return B_OK;
	}

	status_t status = cache_io(ref, cookie, offset,
		(addr_t)const_cast<void*>(buffer), _size, true);

	TRACE(("file_cache_write(ref = %p, offset = %Ld, buffer = %p, size = %lu)"
		" = %ld\n", ref, offset, buffer, *_size, status));

	return status;
}

