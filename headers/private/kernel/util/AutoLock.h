/* 
 * Copyright 2005, Ingo Weinhold, bonefish@users.sf.net. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef KERNEL_UTIL_AUTO_LOCKER_H
#define KERNEL_UTIL_AUTO_LOCKER_H

#include <lock.h>

namespace BPrivate {

// AutoLockerStandardLocking
template<typename Lockable>
class AutoLockerStandardLocking {
public:
	inline bool Lock(Lockable *lockable)
	{
		return lockable->Lock();
	}

	inline void Unlock(Lockable *lockable)
	{
		lockable->Unlock();
	}
};

// AutoLockerReadLocking
template<typename Lockable>
class AutoLockerReadLocking {
public:
	inline bool Lock(Lockable *lockable)
	{
		return lockable->ReadLock();
	}

	inline void Unlock(Lockable *lockable)
	{
		lockable->ReadUnlock();
	}
};

// AutoLockerWriteLocking
template<typename Lockable>
class AutoLockerWriteLocking {
public:
	inline bool Lock(Lockable *lockable)
	{
		return lockable->WriteLock();
	}

	inline void Unlock(Lockable *lockable)
	{
		lockable->WriteUnlock();
	}
};

// AutoLocker
template<typename Lockable,
		 typename Locking = AutoLockerStandardLocking<Lockable> >
class AutoLocker {
private:
	typedef AutoLocker<Lockable, Locking>	ThisClass;
public:
	inline AutoLocker(Lockable *lockable, bool alreadyLocked = false)
		: fLockable(lockable),
		  fLocked(fLockable && alreadyLocked)
	{
		if (!fLocked)
			_Lock();
	}

	inline AutoLocker(Lockable &lockable, bool alreadyLocked = false)
		: fLockable(&lockable),
		  fLocked(fLockable && alreadyLocked)
	{
		if (!fLocked)
			_Lock();
	}

	inline ~AutoLocker()
	{
		_Unlock();
	}

	inline void SetTo(Lockable *lockable, bool alreadyLocked)
	{
		_Unlock();
		fLockable = lockable;
		fLocked = alreadyLocked;
		if (!fLocked)
			_Lock();
	}

	inline void SetTo(Lockable &lockable, bool alreadyLocked)
	{
		SetTo(&lockable, alreadyLocked);
	}

	inline void Unset()
	{
		_Unlock();
	}

	inline void Unlock()
	{
		_Unlock();
	}

	inline void Detach()
	{
		fLockable = NULL;
		fLocked = false;
	}

	inline AutoLocker<Lockable, Locking> &operator=(Lockable *lockable)
	{
		SetTo(lockable);
		return *this;
	}

	inline AutoLocker<Lockable, Locking> &operator=(Lockable &lockable)
	{
		SetTo(&lockable);
		return *this;
	}

	inline bool IsLocked() const	{ return fLocked; }

	inline operator bool() const	{ return fLocked; }

private:
	inline void _Lock()
	{
		if (fLockable)
			fLocked = fLocking.Lock(fLockable);
	}

	inline void _Unlock()
	{
		if (fLockable && fLocked) {
			fLocking.Unlock(fLockable);
			fLocked = false;
		}
	}

private:
	Lockable	*fLockable;
	bool		fLocked;
	Locking		fLocking;
};


// #pragma mark -
// #pragma mark ----- instantiations -----

// MutexLocking
class MutexLocking {
public:
	inline bool Lock(mutex *lockable)
	{
		mutex_lock(lockable);
		return true;
	}

	inline void Unlock(mutex *lockable)
	{
		mutex_unlock(lockable);
	}
};

// MutexLocker
typedef AutoLocker<mutex, MutexLocking> MutexLocker;

// RecursiveLockLocking
class RecursiveLockLocking {
public:
	inline bool Lock(recursive_lock *lockable)
	{
		recursive_lock_lock(lockable);
		return true;
	}

	inline void Unlock(recursive_lock *lockable)
	{
		recursive_lock_unlock(lockable);
	}
};

// RecursiveLocker
typedef AutoLocker<recursive_lock, RecursiveLockLocking> RecursiveLocker;

}	// namespace BPrivate

using BPrivate::AutoLocker;
using BPrivate::MutexLocker;
using BPrivate::RecursiveLocker;

#endif	// KERNEL_UTIL_AUTO_LOCKER_H
