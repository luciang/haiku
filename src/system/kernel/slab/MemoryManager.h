/*
 * Copyright 2010, Ingo Weinhold <ingo_weinhold@gmx.de>.
 * Distributed under the terms of the MIT License.
 */
#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H


#include <KernelExport.h>

#include <condition_variable.h>
#include <kernel.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>


struct kernel_args;
struct ObjectCache;
struct VMArea;


#define SLAB_CHUNK_SIZE_SMALL	B_PAGE_SIZE
#define SLAB_CHUNK_SIZE_MEDIUM	(16 * B_PAGE_SIZE)
#define SLAB_CHUNK_SIZE_LARGE	(128 * B_PAGE_SIZE)
#define SLAB_AREA_SIZE			(2048 * B_PAGE_SIZE)
	// TODO: These sizes have been chosen with 4 KB pages is mind.

#define SLAB_META_CHUNKS_PER_AREA	(SLAB_AREA_SIZE / SLAB_CHUNK_SIZE_LARGE)
#define SLAB_SMALL_CHUNKS_PER_AREA	(SLAB_AREA_SIZE / SLAB_CHUNK_SIZE_SMALL)


class MemoryManager {
public:
	static	void				Init(kernel_args* args);
	static	void				InitPostArea();

	static	status_t			Allocate(ObjectCache* cache, uint32 flags,
									void*& _pages);
	static	void				Free(void* pages, uint32 flags);

	static	size_t				AcceptableChunkSize(size_t size);
	static	ObjectCache*		CacheForAddress(void* address);

private:
			struct Area;

			struct Chunk {
				union {
					Chunk*		next;
					ObjectCache* cache;
				};
			};

			struct MetaChunk : DoublyLinkedListLinkImpl<MetaChunk> {
				size_t			chunkSize;
				addr_t			chunkBase;
				size_t			totalSize;
				uint16			chunkCount;
				uint16			usedChunkCount;
				Chunk*			chunks;
				Chunk*			freeChunks;

				Area*			GetArea() const;
			};

			typedef DoublyLinkedList<MetaChunk> MetaChunkList;

			struct Area : DoublyLinkedListLinkImpl<Area> {
				Area*			next;
				VMArea*			vmArea;
				size_t			reserved_memory_for_mapping;
				uint16			usedMetaChunkCount;
				bool			fullyMapped;
				MetaChunk		metaChunks[SLAB_META_CHUNKS_PER_AREA];
				Chunk			chunks[SLAB_SMALL_CHUNKS_PER_AREA];
			};

			typedef DoublyLinkedList<Area> AreaList;

			struct AreaHashDefinition {
				typedef addr_t		KeyType;
				typedef	Area		ValueType;

				size_t HashKey(addr_t key) const
				{
					return key / SLAB_AREA_SIZE;
				}

				size_t Hash(const Area* value) const
				{
					return HashKey((addr_t)value);
				}

				bool Compare(addr_t key, const Area* value) const
				{
					return key == (addr_t)value;
				}

				Area*& GetLink(Area* value) const
				{
					return value->next;
				}
			};

			typedef BOpenHashTable<AreaHashDefinition> AreaTable;

			struct AllocationEntry {
				ConditionVariable	condition;
				thread_id			thread;
			};

private:
	static	status_t			_AllocateChunk(size_t chunkSize, uint32 flags,
									MetaChunk*& _metaChunk, Chunk*& _chunk);
	static	bool				_GetChunk(MetaChunkList* metaChunkList,
									size_t chunkSize, MetaChunk*& _metaChunk,
									Chunk*& _chunk);
	static	void				_FreeChunk(Area* area, MetaChunk* metaChunk,
									Chunk* chunk, addr_t chunkAddress,
									bool alreadyUnmapped, uint32 flags);

	static	void				_PrepareMetaChunk(MetaChunk* metaChunk,
									size_t chunkSize);

	static	void				_AddArea(Area* area);
	static	status_t			_AllocateArea(size_t chunkSize, uint32 flags,
									Area*& _area);
	static	void				_FreeArea(Area* area, bool areaRemoved,
									uint32 flags);

	static	status_t			_MapChunk(VMArea* vmArea, addr_t address,
									size_t size, size_t reserveAdditionalMemory,
									uint32 flags);
	static	status_t			_UnmapChunk(VMArea* vmArea,addr_t address,
									size_t size, uint32 flags);

	static	void				_UnmapChunkEarly(addr_t address, size_t size);
	static	void				_UnmapFreeChunksEarly(Area* area);
	static	void				_ConvertEarlyArea(Area* area);

	static	uint32				_ChunkIndexForAddress(
									const MetaChunk* metaChunk, addr_t address);
	static	addr_t				_ChunkAddress(const MetaChunk* metaChunk,
									const Chunk* chunk);

	static	int					_DumpArea(int argc, char** argv);
	static	int					_DumpAreas(int argc, char** argv);

private:
	static	const size_t		kAreaAdminSize
									= ROUNDUP(sizeof(Area), B_PAGE_SIZE);

	static	mutex				sLock;
	static	rw_lock				sAreaTableLock;
	static	kernel_args*		sKernelArgs;
	static	AreaTable			sAreaTable;
	static	Area*				sFreeAreas;
	static	MetaChunkList		sFreeCompleteMetaChunks;
	static	MetaChunkList		sFreeShortMetaChunks;
	static	MetaChunkList		sPartialMetaChunksSmall;
	static	MetaChunkList		sPartialMetaChunksMedium;
	static	AllocationEntry*	sAllocationEntryCanWait;
	static	AllocationEntry*	sAllocationEntryDontWait;
};


/*static*/ inline uint32
MemoryManager::_ChunkIndexForAddress(const MetaChunk* metaChunk, addr_t address)
{
	return (address - metaChunk->chunkBase) / metaChunk->chunkSize;
}


/*static*/ inline addr_t
MemoryManager::_ChunkAddress(const MetaChunk* metaChunk, const Chunk* chunk)
{
	return metaChunk->chunkBase
		+ (chunk - metaChunk->chunks) * metaChunk->chunkSize;
}


inline MemoryManager::Area*
MemoryManager::MetaChunk::GetArea() const
{
	return (Area*)ROUNDDOWN((addr_t)this, SLAB_AREA_SIZE);
}


#endif	// MEMORY_MANAGER_H
