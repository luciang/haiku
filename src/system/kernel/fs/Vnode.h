/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef VNODE_H
#define VNODE_H


#include <fs_interface.h>

#include <util/DoublyLinkedList.h>
#include <util/list.h>

#include <lock.h>
#include <thread.h>


struct advisory_locking;
struct file_descriptor;
struct fs_mount;
struct VMCache;

typedef struct vnode Vnode;


struct vnode : fs_vnode, DoublyLinkedListLinkImpl<vnode> {
			struct vnode*		next;
			VMCache*			cache;
			struct fs_mount*	mount;
			struct vnode*		covered_by;
			struct advisory_locking* advisory_locking;
			struct file_descriptor* mandatory_locked_by;
			list_link			unused_link;
			ino_t				id;
			dev_t				device;
			int32				ref_count;

public:
	inline	bool				IsBusy() const;
	inline	void				SetBusy(bool busy);

	inline	bool				IsRemoved() const;
	inline	void				SetRemoved(bool removed);

	inline	bool				IsUnpublished() const;
	inline	void				SetUnpublished(bool unpublished);

	inline	uint32				Type() const;
	inline	void				SetType(uint32 type);

	inline	bool				Lock();
	inline	void				Unlock();

	static	void				StaticInit();

private:
	static	const uint32		kFlagsLocked		= 0x00000001;
	static	const uint32		kFlagsWaitingLocker	= 0x00000002;
	static	const uint32		kFlagsBusy			= 0x00000004;
	static	const uint32		kFlagsRemoved		= 0x00000008;
	static	const uint32		kFlagsUnpublished	= 0x00000010;
	static	const uint32		kFlagsType			= 0xfffff000;

	static	const uint32		kBucketCount		 = 32;

			struct LockWaiter : DoublyLinkedListLinkImpl<LockWaiter> {
				LockWaiter*		next;
				struct thread*	thread;
				struct vnode*	vnode;
			};

			typedef DoublyLinkedList<LockWaiter> LockWaiterList;

			struct Bucket {
				mutex			lock;
				LockWaiterList	waiters;

				Bucket();
			};

private:
	inline	Bucket&				_Bucket() const;

			void				_WaitForLock();
			void				_WakeUpLocker();

private:
			vint32				fFlags;

	static	Bucket				sBuckets[kBucketCount];
};


bool
vnode::IsBusy() const
{
	return (fFlags & kFlagsBusy) != 0;
}


void
vnode::SetBusy(bool busy)
{
	if (busy)
		atomic_or(&fFlags, kFlagsBusy);
	else
		atomic_and(&fFlags, ~kFlagsBusy);
}


bool
vnode::IsRemoved() const
{
	return (fFlags & kFlagsRemoved) != 0;
}


void
vnode::SetRemoved(bool removed)
{
	if (removed)
		atomic_or(&fFlags, kFlagsRemoved);
	else
		atomic_and(&fFlags, ~kFlagsRemoved);
}


bool
vnode::IsUnpublished() const
{
	return (fFlags & kFlagsUnpublished) != 0;
}


void
vnode::SetUnpublished(bool unpublished)
{
	if (unpublished)
		atomic_or(&fFlags, kFlagsUnpublished);
	else
		atomic_and(&fFlags, ~kFlagsUnpublished);
}


uint32
vnode::Type() const
{
	return (uint32)fFlags & kFlagsType;
}


void
vnode::SetType(uint32 type)
{
	atomic_and(&fFlags, ~kFlagsType);
	atomic_or(&fFlags, type & kFlagsType);
}


/*!	Locks the vnode.
	The caller must hold sVnodeLock (at least read locked) and must continue to
	hold it until calling Unlock(). After acquiring the lock the caller is
	allowed to write access the vnode's mutable fields, if it hasn't been marked
	busy by someone else.
	Due to the condition of holding sVnodeLock at least read locked, write
	locking it grants the same write access permission to *any* vnode.

	The vnode's lock should be held only for a short time. It can be held over
	sUnusedVnodesLock.

	\return Always \c true.
*/
bool
vnode::Lock()
{
	if ((atomic_or(&fFlags, kFlagsLocked)
			& (kFlagsLocked | kFlagsWaitingLocker)) != 0) {
		_WaitForLock();
	}

	return true;
}

void
vnode::Unlock()
{
	if ((atomic_and(&fFlags, ~kFlagsLocked) & kFlagsWaitingLocker) != 0)
		_WakeUpLocker();
}


vnode::Bucket&
vnode::_Bucket() const
{
	return sBuckets[((addr_t)this / 64) % kBucketCount];
		// The vnode structure is somewhat larger than 64 bytes (on 32 bit
		// archs), so subsequently allocated vnodes fall into different
		// buckets. How exactly the vnodes are distributed depends on the
		// allocator -- a dedicated slab would be perfect.
}


#endif	// VNODE_H
