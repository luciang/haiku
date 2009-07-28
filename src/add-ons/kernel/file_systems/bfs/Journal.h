/*
 * Copyright 2001-2009, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */
#ifndef JOURNAL_H
#define JOURNAL_H


#include "system_dependencies.h"

#include "Volume.h"
#include "Utility.h"


struct run_array;
class Inode;
class LogEntry;
typedef DoublyLinkedList<LogEntry> LogEntryList;


class Journal {
public:
							Journal(Volume* volume);
							~Journal();

			status_t		InitCheck();

			status_t		Lock(Transaction* owner,
								bool separateSubTransactions);
			status_t		Unlock(Transaction* owner, bool success);

			status_t		ReplayLog();

			Transaction*	CurrentTransaction() const { return fOwner; }

			status_t		FlushLogAndBlocks();
			Volume*			GetVolume() const { return fVolume; }
			int32			TransactionID() const { return fTransactionID; }

	inline	uint32			FreeLogBlocks() const;

#ifdef BFS_DEBUGGER_COMMANDS
			void			Dump();
#endif

private:
			bool			_HasSubTransaction() { return fHasSubtransaction; }
			status_t		_FlushLog(bool canWait, bool flushBlocks);
			uint32			_TransactionSize() const;
			status_t		_WriteTransactionToLog();
			status_t		_CheckRunArray(const run_array* array);
			status_t		_ReplayRunArray(int32* start);
			status_t		_TransactionDone(bool success);

	static	void			_TransactionWritten(int32 transactionID,
								int32 event, void* _logEntry);
	static	void			_TransactionIdle(int32 transactionID, int32 event,
								void* _journal);

			Volume*			fVolume;
			recursive_lock	fLock;
			Transaction*	fOwner;
			uint32			fLogSize;
			uint32			fMaxTransactionSize;
			uint32			fUsed;
			int32			fUnwrittenTransactions;
			mutex			fEntriesLock;
			LogEntryList	fEntries;
			bigtime_t		fTimestamp;
			int32			fTransactionID;
			bool			fHasSubtransaction;
			bool			fSeparateSubTransactions;
};


inline uint32
Journal::FreeLogBlocks() const
{
	return fVolume->LogStart() <= fVolume->LogEnd()
		? fLogSize - fVolume->LogEnd() + fVolume->LogStart()
		: fVolume->LogStart() - fVolume->LogEnd();
}


class Transaction {
public:
	Transaction(Volume* volume, off_t refBlock)
		:
		fJournal(NULL)
	{
		Start(volume, refBlock);
	}

	Transaction(Volume* volume, block_run refRun)
		:
		fJournal(NULL)
	{
		Start(volume, volume->ToBlock(refRun));
	}

	Transaction()
		:
		fJournal(NULL)
	{
	}

	~Transaction()
	{
		if (fJournal != NULL) {
			fJournal->Unlock(this, false);
			_UnlockInodes(false);
		}
	}

	status_t Start(Volume* volume, off_t refBlock);
	bool IsStarted() const { return fJournal != NULL; }

	status_t Done()
	{
		status_t status = B_OK;
		if (fJournal != NULL) {
			_UnlockInodes(true);
			status = fJournal->Unlock(this, true);
			if (status == B_OK)
				fJournal = NULL;
		}
		return status;
	}

	bool HasParent()
	{
		if (fJournal != NULL)
			return fJournal->CurrentTransaction() == this;

		return false;
	}

	status_t WriteBlocks(off_t blockNumber, const uint8* buffer,
		size_t numBlocks = 1)
	{
		if (fJournal == NULL)
			return B_NO_INIT;

		void* cache = GetVolume()->BlockCache();
		size_t blockSize = GetVolume()->BlockSize();

		for (size_t i = 0; i < numBlocks; i++) {
			void* block = block_cache_get_empty(cache, blockNumber + i,
				ID());
			if (block == NULL)
				return B_ERROR;

			memcpy(block, buffer, blockSize);
			buffer += blockSize;

			block_cache_put(cache, blockNumber + i);
		}

		return B_OK;
	}

	Volume	*GetVolume()
		{ return fJournal != NULL ? fJournal->GetVolume() : NULL; }
	int32 ID() const
		{ return fJournal->TransactionID(); }

	void AddInode(Inode* inode);
	void RemoveInode(Inode* inode);

private:
	Transaction(const Transaction& other);
	Transaction& operator=(const Transaction& other);
		// no implementation

	void _UnlockInodes(bool success);

	Journal*	fJournal;
	InodeList	fLockedInodes;
};

#ifdef BFS_DEBUGGER_COMMANDS
int dump_journal(int argc, char** argv);
#endif

#endif	// JOURNAL_H
