/*
 * Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef BLOCK_CACHE_PRIVATE_H
#define BLOCK_CACHE_PRIVATE_H


#include <lock.h>
#include <util/DoublyLinkedList.h>

struct hash_table;
struct vm_page;


#define DEBUG_CHANGED


static const size_t kBlockAddressSize = 128 * 1024 * 1024;	// 128 MB
static const size_t kBlockRangeSize = 64 * 1024;			// 64 kB
static const size_t kBlockRangeShift = 16;
static const size_t kNumBlockRangePages = kBlockRangeSize / B_PAGE_SIZE;

struct cache_transaction;
struct cached_block;
struct block_chunk;
struct block_cache;
typedef DoublyLinkedListLink<cached_block> block_link;


struct cached_block {
	cached_block	*next;			// next in hash
	cached_block	*transaction_next;
	block_link		previous_transaction_link;
	cached_block	*chunk_next;
	block_chunk		*chunk;
	off_t			block_number;
	void			*data;
	void			*original;
#ifdef DEBUG_CHANGED
	void			*compare;
#endif
	int32			ref_count;
	int32			lock;
	bool			busy : 1;
	bool			is_writing : 1;
	bool			is_dirty : 1;
	bool			unmapped : 1;
	cache_transaction *transaction;
	cache_transaction *previous_transaction;

	static int Compare(void *_cacheEntry, const void *_block);
	static uint32 Hash(void *_cacheEntry, const void *_block, uint32 range);
};

typedef DoublyLinkedList<cached_block,
	DoublyLinkedListMemberGetLink<cached_block,
		&cached_block::previous_transaction_link> > block_list;

struct block_chunk {
	cached_block	*blocks;
	bool			mapped;
	uint16			used_mask;		// min chunk size is 4096 bytes; min block size is 256 bytes
};

struct block_range {
	block_range		*next;			// next in hash
	block_range		*free_next;
	addr_t			base;
	uint32			used_mask;
	vm_page			*pages[kNumBlockRangePages];
	block_chunk		chunks[0];

	static status_t NewBlockRange(block_cache *cache, block_range **_range);

	status_t Allocate(block_cache *cache, cached_block *block);
	void Free(block_cache *cache, cached_block *block);

	void *Allocate(block_cache *cache, block_chunk **_chunk = NULL);
	void Free(block_cache *cache, void *address);

	uint32 BlockIndex(block_cache *cache, void *address);
	uint32 ChunkIndex(block_cache *cache, void *address);
	block_chunk *Chunk(block_cache *cache, void *address);

	static int Compare(void *_blockRange, const void *_address);
	static uint32 Hash(void *_blockRange, const void *_address, uint32 range);

	//bool HasFreeBlocks() const { return (1L << num_chunks) - 1 != used_mask; }
};

struct block_cache {

	hash_table	*hash;
	benaphore	lock;
	int			fd;
	off_t		max_blocks;
	size_t		block_size;
	int32		next_transaction_id;
	hash_table	*transaction_hash;

	hash_table	*ranges_hash;
	block_range	*free_ranges;
	uint32		chunks_per_range;
	size_t		chunk_size;
	uint32		range_mask;
	uint32		chunk_mask;
	block_list	unmapped_blocks;

	block_cache(int fd, off_t numBlocks, size_t blockSize);
	~block_cache();

	status_t InitCheck();

	block_range *GetFreeRange();
	block_range *GetRange(void *address);
	void FreeBlock(cached_block *block);
	cached_block *NewBlock(off_t blockNumber);
	void Free(void *address);
	void *Allocate();
};

class BlockAddressPool {
	public:
		BlockAddressPool();
		~BlockAddressPool();

		status_t InitCheck() const { return fArea >= B_OK ? B_OK : fArea; }

		size_t RangeSize() const { return kBlockRangeSize; }
		size_t RangeShift() const { return kBlockRangeShift; }
		addr_t BaseAddress() const { return fBase; }

		addr_t Get();
		void Put(addr_t address);

	private:
		benaphore	fLock;
		area_id		fArea;
		addr_t		fBase;
		addr_t		fFirstFree;
		int32		fNextFree;
		int32		fFreeList[kBlockAddressSize / kBlockRangeSize];
};


#ifdef __cplusplus
extern "C" {
#endif

status_t init_block_allocator();

#ifdef __cplusplus
}
#endif

#endif	/* BLOCK_CACHE_PRIVATE_H */
