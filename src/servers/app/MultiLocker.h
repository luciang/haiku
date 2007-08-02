/*
 * Copyright 2005-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 *
 * Copyright 1999, Be Incorporated.   All Rights Reserved.
 * This file may be used under the terms of the Be Sample Code License.
 */

/*! multiple-reader single-writer locking class */

// IMPORTANT:
//             * nested read locks are not supported
//             * a reader becomming the write is not supported
//             * nested write locks are supported
//             * a writer can do read locks, even nested ones
//             * in case of problems, #define DEBUG 1 in the .cpp

#ifndef MULTI_LOCKER_H
#define MULTI_LOCKER_H


#include <OS.h>


#define MULTI_LOCKER_TIMING	0
#if DEBUG
#	define MULTI_LOCKER_DEBUG	DEBUG
#else
#	define MULTI_LOCKER_DEBUG	0
#endif

class MultiLocker {
	public:
		MultiLocker(const char* baseName);
		virtual	~MultiLocker();

		status_t		InitCheck();
		
		// locking for reading or writing
		bool			ReadLock();
		bool			WriteLock();

		// unlocking after reading or writing
		bool			ReadUnlock();
		bool			WriteUnlock();

		// does the current thread hold a write lock ?
		bool			IsWriteLocked(uint32 *stackBase = NULL,
							thread_id *thread = NULL);

#if MULTI_LOCKER_DEBUG
		// in DEBUG mode returns whether the lock is held
		// in non-debug mode returns true
		bool			IsReadLocked();
#endif

	private:
#if MULTI_LOCKER_DEBUG
		// functions for managing the DEBUG reader array
		void			_RegisterThread();
		void			_UnregisterThread();

		sem_id			fLock;
		int32*			fDebugArray;
		int32			fMaxThreads;
#else
		// readers adjust count and block on fReadSem when a writer
		// hold the lock
		int32			fReadCount;
		sem_id			fReadSem;
		// writers adjust the count and block on fWriteSem
		// when readers hold the lock
		int32			fWriteCount;
		sem_id 			fWriteSem;
		// writers must acquire fWriterLock when acquiring a write lock
		int32			fLockCount;
		sem_id			fWriterLock;
#endif	// MULTI_LOCKER_DEBUG

		status_t		fInit;
		int32			fWriterNest;
		thread_id		fWriterThread;
		uint32			fWriterStackBase;

#if MULTI_LOCKER_TIMING
		uint32 			rl_count;
		bigtime_t 		rl_time;
		uint32 			ru_count;
		bigtime_t 		ru_time;
		uint32			wl_count;
		bigtime_t		wl_time;
		uint32			wu_count;
		bigtime_t		wu_time;
		uint32			islock_count;
		bigtime_t		islock_time;
#endif
};

class AutoWriteLocker {
	public:
		AutoWriteLocker(MultiLocker* lock)
			:
			fLock(*lock)
		{
			fLock.WriteLock();
		}

		AutoWriteLocker(MultiLocker& lock)
			:
			fLock(lock)
		{
			fLock.WriteLock();
		}

		bool IsLocked() const
		{
			return fLock.IsWriteLocked();
		}

		~AutoWriteLocker()
		{
			fLock.WriteUnlock();
		}

	private:
	 	MultiLocker&	fLock;
};

class AutoReadLocker {
	public:
		AutoReadLocker(MultiLocker* lock)
			:
			fLock(*lock)
		{
			fLocked = fLock.ReadLock();
		}

		AutoReadLocker(MultiLocker& lock)
			:
			fLock(lock)
		{
			fLocked = fLock.ReadLock();
		}

		~AutoReadLocker()
		{
			Unlock();
		}

		void
		Unlock()
		{
			if (fLocked) {
				fLock.ReadUnlock();
				fLocked = false;
			}
		}

	private:
	 	MultiLocker&	fLock;
	 	bool			fLocked;
};

#endif	// MULTI_LOCKER_H
