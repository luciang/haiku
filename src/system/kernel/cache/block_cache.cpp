/*
 * Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "block_cache_private.h"

#include <KernelExport.h>
#include <fs_cache.h>

#include <block_cache.h>
#include <lock.h>
#include <util/kernel_cpp.h>
#include <util/DoublyLinkedList.h>
#include <util/AutoLock.h>
#include <util/khash.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


// ToDo: this is a naive but growing implementation to test the API:
//	1) block reading/writing is not at all optimized for speed, it will
//	   just read and write single blocks.
//	2) the locking could be improved; getting a block should not need to
//	   wait for blocks to be written
//	3) dirty blocks are only written back if asked for
//	4) blocks are never removed yet

//#define TRACE_BLOCK_CACHE
#ifdef TRACE_BLOCK_CACHE
#	define TRACE(x)	dprintf x
#else
#	define TRACE(x) ;
#endif


struct cache_transaction {
	cache_transaction *next;
	int32			id;
	int32			num_blocks;
	cached_block	*first_block;
	block_list		blocks;
	transaction_notification_hook notification_hook;
	void			*notification_data;
	bool			open;
};

static status_t write_cached_block(block_cache *cache, cached_block *block, bool deleteTransaction = true);


//	#pragma mark - private transaction


static int
transaction_compare(void *_transaction, const void *_id)
{
	cache_transaction *transaction = (cache_transaction *)_transaction;
	const int32 *id = (const int32 *)_id;

	return transaction->id - *id;
}


static uint32
transaction_hash(void *_transaction, const void *_id, uint32 range)
{
	cache_transaction *transaction = (cache_transaction *)_transaction;
	const int32 *id = (const int32 *)_id;

	if (transaction != NULL)
		return transaction->id % range;

	return *id % range;
}


static void
delete_transaction(block_cache *cache, cache_transaction *transaction)
{
	hash_remove(cache->transaction_hash, transaction);
	if (cache->last_transaction == transaction)
		cache->last_transaction = NULL;

	delete transaction;
}


static cache_transaction *
lookup_transaction(block_cache *cache, int32 id)
{
	return (cache_transaction *)hash_lookup(cache->transaction_hash, &id);
}


//	#pragma mark - private block_cache


/* static */
int
cached_block::Compare(void *_cacheEntry, const void *_block)
{
	cached_block *cacheEntry = (cached_block *)_cacheEntry;
	const off_t *block = (const off_t *)_block;

	return cacheEntry->block_number - *block;
}



/* static */
uint32
cached_block::Hash(void *_cacheEntry, const void *_block, uint32 range)
{
	cached_block *cacheEntry = (cached_block *)_cacheEntry;
	const off_t *block = (const off_t *)_block;

	if (cacheEntry != NULL)
		return cacheEntry->block_number % range;

	return *block % range;
}


//	#pragma mark -


block_cache::block_cache(int _fd, off_t numBlocks, size_t blockSize)
	:
	hash(NULL),
	fd(_fd),
	max_blocks(numBlocks),
	block_size(blockSize),
	next_transaction_id(1),
	last_transaction(NULL),
	transaction_hash(NULL),
	ranges_hash(NULL),
	free_ranges(NULL)
{
	hash = hash_init(32, 0, &cached_block::Compare, &cached_block::Hash);
	if (hash == NULL)
		return;

	transaction_hash = hash_init(16, 0, &transaction_compare, &::transaction_hash);
	if (transaction_hash == NULL)
		return;

	ranges_hash = hash_init(16, 0, &block_range::Compare, &block_range::Hash);
	if (ranges_hash == NULL)
		return;

	if (benaphore_init(&lock, "block cache") < B_OK)
		return;

	chunk_size = max_c(blockSize, B_PAGE_SIZE);
	chunks_per_range = kBlockRangeSize / chunk_size;
	range_mask = (1UL << chunks_per_range) - 1;
	chunk_mask = (1UL << (chunk_size / blockSize)) - 1;
}


block_cache::~block_cache()
{
	benaphore_destroy(&lock);

	hash_uninit(ranges_hash);
	hash_uninit(transaction_hash);
	hash_uninit(hash);
}


status_t
block_cache::InitCheck()
{
	if (lock.sem < B_OK)
		return lock.sem;

	if (hash == NULL || transaction_hash == NULL || ranges_hash == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


block_range *
block_cache::GetFreeRange()
{
	if (free_ranges != NULL)
		return free_ranges;

	// we need to allocate a new range
	block_range *range;
	if (block_range::NewBlockRange(this, &range) != B_OK) {
		// ToDo: free up space in existing ranges
		// We may also need to free ranges from other caches to get a free one
		// (if not, an active volume might have stolen all free ranges already)
		return NULL;
	}

	hash_insert(ranges_hash, range);
	return range;
}


block_range *
block_cache::GetRange(void *address)
{
	return (block_range *)hash_lookup(ranges_hash, address);
}


void
block_cache::Free(void *address)
{
	if (address == NULL)
		return;

	block_range *range = GetRange(address);
	ASSERT(range != NULL);
	range->Free(this, address);
}


void *
block_cache::Allocate()
{
	block_range *range = GetFreeRange();
	if (range == NULL)
		return NULL;

	return range->Allocate(this);
}


void
block_cache::FreeBlock(cached_block *block)
{
	block_range *range = GetRange(block->data);
	ASSERT(range != NULL);
	range->Free(this, block);

	Free(block->original);
#ifdef DEBUG_CHANGED
	Free(block->compare);
#endif

	free(block);
}


cached_block *
block_cache::NewBlock(off_t blockNumber)
{
	cached_block *block = (cached_block *)malloc(sizeof(cached_block));
	if (block == NULL)
		return NULL;

	block_range *range = GetFreeRange();
	if (range == NULL) {
		free(block);
		return NULL;
	}

	range->Allocate(this, block);

	block->block_number = blockNumber;
	block->lock = 0;
	block->transaction_next = NULL;
	block->transaction = block->previous_transaction = NULL;
	block->original = NULL;
	block->is_dirty = false;
#ifdef DEBUG_CHANGED
	block->compare = NULL;
#endif

	hash_insert(hash, block);

	return block;
}


#ifdef DEBUG_CHANGED

#define DUMPED_BLOCK_SIZE 16

void
dumpBlock(const char *buffer, int size, const char *prefix)
{
	int i;
	
	for (i = 0; i < size;) {
		int start = i;

		dprintf(prefix);
		for (; i < start+DUMPED_BLOCK_SIZE; i++) {
			if (!(i % 4))
				dprintf(" ");

			if (i >= size)
				dprintf("  ");
			else
				dprintf("%02x", *(unsigned char *)(buffer + i));
		}
		dprintf("  ");

		for (i = start; i < start + DUMPED_BLOCK_SIZE; i++) {
			if (i < size) {
				char c = buffer[i];

				if (c < 30)
					dprintf(".");
				else
					dprintf("%c", c);
			} else
				break;
		}
		dprintf("\n");
	}
}
#endif


static void
put_cached_block(block_cache *cache, cached_block *block)
{
#ifdef DEBUG_CHANGED
	if (!block->is_dirty && block->compare != NULL && memcmp(block->data, block->compare, cache->block_size)) {
		dprintf("new block:\n");
		dumpBlock((const char *)block->data, 256, "  ");
		dprintf("unchanged block:\n");
		dumpBlock((const char *)block->compare, 256, "  ");
		write_cached_block(cache, block);
		panic("block_cache: supposed to be clean block was changed!\n");

		cache->Free(block->compare);
		block->compare = NULL;
	}
#endif

	if (--block->lock == 0)
		;
//		block->data = cache->allocator->Release(block->data);
}


static void
put_cached_block(block_cache *cache, off_t blockNumber)
{
	cached_block *block = (cached_block *)hash_lookup(cache->hash, &blockNumber);
	if (block != NULL)
		put_cached_block(cache, block);
}


static cached_block *
get_cached_block(block_cache *cache, off_t blockNumber, bool &allocated, bool readBlock = true)
{
	cached_block *block = (cached_block *)hash_lookup(cache->hash, &blockNumber);
	allocated = false;

	if (block == NULL) {
		// read block into cache
		block = cache->NewBlock(blockNumber);
		if (block == NULL)
			return NULL;

		allocated = true;
	} else {
/*
		if (block->lock == 0 && block->data != NULL) {
			// see if the old block can be resurrected
			block->data = cache->allocator->Acquire(block->data);
		}

		if (block->data == NULL) {
			// there is no block yet, but we need one
			block->data = cache->allocator->Get();
			if (block->data == NULL)
				return NULL;

			allocated = true;
		}
*/
	}

	if (allocated && readBlock) {
		int32 blockSize = cache->block_size;

		if (read_pos(cache->fd, blockNumber * blockSize, block->data, blockSize) < blockSize) {
			cache->FreeBlock(block);
			return NULL;
		}
	}

	block->lock++;
	return block;
}


static void *
get_writable_cached_block(block_cache *cache, off_t blockNumber, off_t base, off_t length,
	int32 transactionID, bool cleared)
{
	BenaphoreLocker locker(&cache->lock);

	TRACE(("get_writable_cached_block(blockNumber = %Ld, transaction = %ld)\n", blockNumber, transactionID));

	bool allocated;
	cached_block *block = get_cached_block(cache, blockNumber, allocated, !cleared);
	if (block == NULL)
		return NULL;

	// if there is no transaction support, we just return the current block
	if (transactionID == -1) {
		if (cleared)
			memset(block->data, 0, cache->block_size);

		block->is_dirty = true;
			// mark the block as dirty

		return block->data;
	}

	// ToDo: note, even if we panic, we should probably put the cached block
	//	back before we return

	if (block->transaction != NULL && block->transaction->id != transactionID) {
		// ToDo: we have to wait here until the other transaction is done.
		//	Maybe we should even panic, since we can't prevent any deadlocks.
		panic("get_writable_cached_block(): asked to get busy writable block (transaction %ld)\n", block->transaction->id);
		return NULL;
	}
	if (block->transaction == NULL && transactionID != -1) {
		// get new transaction
		cache_transaction *transaction = lookup_transaction(cache, transactionID);
		if (transaction == NULL) {
			panic("get_writable_cached_block(): invalid transaction %ld!\n", transactionID);
			return NULL;
		}
		if (!transaction->open) {
			panic("get_writable_cached_block(): transaction already done!\n");
			return NULL;
		}

		block->transaction = transaction;

		// attach the block to the transaction block list
		block->transaction_next = transaction->first_block;
		transaction->first_block = block;
		transaction->num_blocks++;
	}

	if (!(allocated && cleared) && block->original == NULL) {
		// we already have data, so we need to save it
		block->original = cache->Allocate();
		if (block->original == NULL) {
			put_cached_block(cache, block);
			return NULL;
		}

		memcpy(block->original, block->data, cache->block_size);
	}

	if (cleared)
		memset(block->data, 0, cache->block_size);

	block->is_dirty = true;

	return block->data;
}


static status_t
write_cached_block(block_cache *cache, cached_block *block, bool deleteTransaction)
{
	cache_transaction *previous = block->previous_transaction;
	int32 blockSize = cache->block_size;

	void *data = previous && block->original ? block->original : block->data;
		// we first need to write back changes from previous transactions

	TRACE(("write_cached_block(block %Ld)\n", block->block_number));

	ssize_t written = write_pos(cache->fd, block->block_number * blockSize, data, blockSize);

	if (written < blockSize) {
		dprintf("could not write back block %Ld (%s)\n", block->block_number, strerror(errno));
		return B_IO_ERROR;
	}

	if (data == block->data)
		block->is_dirty = false;

	if (previous != NULL) {
		previous->blocks.Remove(block);
		block->previous_transaction = NULL;

		// Has the previous transation been finished with that write?
		if (--previous->num_blocks == 0) {
			TRACE(("cache transaction %ld finished!\n", previous->id));

			if (previous->notification_hook != NULL)
				previous->notification_hook(previous->id, previous->notification_data);

			if (deleteTransaction)
				delete_transaction(cache, previous);
		}
	}

	return B_OK;
}


extern "C" status_t
block_cache_init(void)
{
	return init_block_allocator();
}


//	#pragma mark - public transaction


extern "C" int32
cache_start_transaction(void *_cache)
{
	block_cache *cache = (block_cache *)_cache;

	if (cache->last_transaction && cache->last_transaction->open)
		panic("last transaction (%ld) still open!\n", cache->last_transaction->id);

	cache_transaction *transaction = new cache_transaction;
	if (transaction == NULL)
		return B_NO_MEMORY;

	transaction->id = atomic_add(&cache->next_transaction_id, 1);
	transaction->num_blocks = 0;
	transaction->first_block = NULL;
	transaction->notification_hook = NULL;
	transaction->notification_data = NULL;
	transaction->open = true;
	cache->last_transaction = transaction;

	TRACE(("cache_transaction_start(): id %ld started\n", transaction->id));

	BenaphoreLocker locker(&cache->lock);
	hash_insert(cache->transaction_hash, transaction);

	return transaction->id;
}


extern "C" status_t
cache_sync_transaction(void *_cache, int32 id)
{
	block_cache *cache = (block_cache *)_cache;
	BenaphoreLocker locker(&cache->lock);
	status_t status = B_ENTRY_NOT_FOUND;

	hash_iterator iterator;
	hash_open(cache->transaction_hash, &iterator);

	cache_transaction *transaction;
	while ((transaction = (cache_transaction *)hash_next(cache->transaction_hash, &iterator)) != NULL) {
		// ToDo: fix hash interface to make this easier

		if (transaction->id <= id && !transaction->open) {
			while (transaction->num_blocks > 0) {
				status = write_cached_block(cache, transaction->blocks.Head(), false);
				if (status != B_OK)
					return status;
			}
			delete_transaction(cache, transaction);
			hash_rewind(cache->transaction_hash, &iterator);
		}
	}

	hash_close(cache->transaction_hash, &iterator, false);
	return B_OK;
}


extern "C" status_t
cache_end_transaction(void *_cache, int32 id, transaction_notification_hook hook, void *data)
{
	block_cache *cache = (block_cache *)_cache;
	BenaphoreLocker locker(&cache->lock);

	TRACE(("cache_end_transaction(id = %ld)\n", id));

	cache_transaction *transaction = lookup_transaction(cache, id);
	if (transaction == NULL) {
		panic("cache_end_transaction(): invalid transaction ID\n");
		return B_BAD_VALUE;
	}

	transaction->notification_hook = hook;
	transaction->notification_data = data;

	// iterate through all blocks and free the unchanged original contents

	cached_block *block = transaction->first_block, *next;
	for (; block != NULL; block = next) {
		next = block->transaction_next;

		if (block->previous_transaction != NULL) {
			// need to write back pending changes
			write_cached_block(cache, block);
		}

		if (block->original != NULL) {
			cache->Free(block->original);
			block->original = NULL;
		}

		// move the block to the previous transaction list
		transaction->blocks.Add(block);

		block->previous_transaction = transaction;
		block->transaction_next = NULL;
		block->transaction = NULL;
	}

	transaction->open = false;

	return B_OK;
}


extern "C" status_t
cache_abort_transaction(void *_cache, int32 id)
{
	block_cache *cache = (block_cache *)_cache;
	BenaphoreLocker locker(&cache->lock);

	TRACE(("cache_abort_transaction(id = %ld)\n", id));

	cache_transaction *transaction = lookup_transaction(cache, id);
	if (transaction == NULL) {
		panic("cache_abort_transaction(): invalid transaction ID\n");
		return B_BAD_VALUE;
	}

	// iterate through all blocks and restore their original contents

	cached_block *block = transaction->first_block, *next;
	for (; block != NULL; block = next) {
		next = block->transaction_next;

		if (block->original != NULL) {
			TRACE(("cache_abort_transaction(id = %ld): restored contents of block %Ld\n",
				transaction->id, block->block_number));
			memcpy(block->data, block->original, cache->block_size);
			cache->Free(block->original);
			block->original = NULL;
		}

		block->transaction_next = NULL;
		block->transaction = NULL;
	}

	delete_transaction(cache, transaction);
	return B_OK;
}


extern "C" int32
cache_detach_sub_transaction(void *_cache, int32 id)
{
	return B_ERROR;
}


extern "C" status_t
cache_abort_sub_transaction(void *_cache, int32 id)
{
	return B_ERROR;
}


extern "C" status_t
cache_start_sub_transaction(void *_cache, int32 id)
{
	return B_ERROR;
}


extern "C" status_t
cache_next_block_in_transaction(void *_cache, int32 id, uint32 *_cookie, off_t *_blockNumber,
	void **_data, void **_unchangedData)
{
	cached_block *block = (cached_block *)*_cookie;
	block_cache *cache = (block_cache *)_cache;

	BenaphoreLocker locker(&cache->lock);

	cache_transaction *transaction = lookup_transaction(cache, id);
	if (transaction == NULL)
		return B_BAD_VALUE;

	if (block == NULL)
		block = transaction->first_block;
	else
		block = block->transaction_next;

	if (block == NULL)
		return B_ENTRY_NOT_FOUND;

	if (_blockNumber)
		*_blockNumber = block->block_number;
	if (_data)
		*_data = block->data;
	if (_unchangedData)
		*_unchangedData = block->original;

	*_cookie = (uint32)block;
	return B_OK;	
}


//	#pragma mark - public block cache
//	public interface


extern "C" void
block_cache_delete(void *_cache, bool allowWrites)
{
	block_cache *cache = (block_cache *)_cache;

	if (allowWrites)
		block_cache_sync(cache);

	// free all blocks

	uint32 cookie = 0;
	cached_block *block;
	while ((block = (cached_block *)hash_remove_first(cache->hash, &cookie)) != NULL) {
		cache->FreeBlock(block);
	}

	// free all transactions (they will all be aborted)	

	cookie = 0;
	cache_transaction *transaction;
	while ((transaction = (cache_transaction *)hash_remove_first(cache->transaction_hash, &cookie)) != NULL) {
		delete transaction;
	}

	delete cache;
}


extern "C" void *
block_cache_create(int fd, off_t numBlocks, size_t blockSize)
{
	block_cache *cache = new block_cache(fd, numBlocks, blockSize);
	if (cache == NULL)
		return NULL;

	if (cache->InitCheck() != B_OK) {
		delete cache;
		return NULL;
	}

	return cache;
}


extern "C" status_t
block_cache_sync(void *_cache)
{
	block_cache *cache = (block_cache *)_cache;

	// we will sync all dirty blocks to disk that have a completed
	// transaction or no transaction only

	BenaphoreLocker locker(&cache->lock);
	hash_iterator iterator;
	hash_open(cache->hash, &iterator);

	cached_block *block;
	while ((block = (cached_block *)hash_next(cache->hash, &iterator)) != NULL) {
		if (block->previous_transaction != NULL
			|| (block->transaction == NULL && block->is_dirty)) {
			status_t status = write_cached_block(cache, block);
			if (status != B_OK)
				return status;
		}
	}

	hash_close(cache->hash, &iterator, false);
	return B_OK;
}


extern "C" status_t
block_cache_make_writable(void *_cache, off_t blockNumber, int32 transaction)
{
	// ToDo: this can be done better!
	void *block = block_cache_get_writable_etc(_cache, blockNumber, blockNumber, 1, transaction);
	if (block != NULL) {
		put_cached_block((block_cache *)_cache, blockNumber);
		return B_OK;
	}

	return B_ERROR;
}


extern "C" void *
block_cache_get_writable_etc(void *_cache, off_t blockNumber, off_t base, off_t length,
	int32 transaction)
{
	TRACE(("block_cache_get_writable_etc(block = %Ld, transaction = %ld)\n", blockNumber, transaction));

	return get_writable_cached_block((block_cache *)_cache, blockNumber,
				base, length, transaction, false);
}


extern "C" void *
block_cache_get_writable(void *_cache, off_t blockNumber, int32 transaction)
{
	return block_cache_get_writable_etc(_cache, blockNumber, blockNumber, 1, transaction);
}


extern "C" void *
block_cache_get_empty(void *_cache, off_t blockNumber, int32 transaction)
{
	TRACE(("block_cache_get_empty(block = %Ld, transaction = %ld)\n", blockNumber, transaction));

	return get_writable_cached_block((block_cache *)_cache, blockNumber,
				blockNumber, 1, transaction, true);
}


extern "C" const void *
block_cache_get_etc(void *_cache, off_t blockNumber, off_t base, off_t length)
{
	block_cache *cache = (block_cache *)_cache;
	BenaphoreLocker locker(&cache->lock);
	bool allocated;

	cached_block *block = get_cached_block(cache, blockNumber, allocated);
	if (block == NULL)
		return NULL;

#ifdef DEBUG_CHANGED
	if (block->compare == NULL)
		block->compare = cache->Allocate();
	if (block->compare != NULL)
		memcpy(block->compare, block->data, cache->block_size);
#endif
	return block->data;
}


extern "C" const void *
block_cache_get(void *_cache, off_t blockNumber)
{
	return block_cache_get_etc(_cache, blockNumber, blockNumber, 1);
}


extern "C" status_t
block_cache_set_dirty(void *_cache, off_t blockNumber, bool isDirty, int32 transaction)
{
	// not yet implemented
	// Note, you must only use this function on blocks that were acquired writable!
	if (isDirty)
		panic("block_cache_set_dirty(): not yet implemented that way!\n");

	return B_OK;
}


extern "C" void
block_cache_put(void *_cache, off_t blockNumber)
{
	block_cache *cache = (block_cache *)_cache;
	BenaphoreLocker locker(&cache->lock);

	put_cached_block(cache, blockNumber);
}

