/*
 * Copyright 2007, Ingo Weinhold, bonefish@users.sf.net.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DISK_SYSTEM_ADD_ON_MANAGER_H
#define _DISK_SYSTEM_ADD_ON_MANAGER_H

#include <List.h>
#include <Locker.h>


class BDiskSystemAddOn;


namespace BPrivate {


class DiskSystemAddOnManager {
public:
	static	DiskSystemAddOnManager* Default();

			bool				Lock();
			void				Unlock();

			// load/unload all disk system add-ons
			void				LoadDiskSystems();
			void				UnloadDiskSystems();

			// manager must be locked
			int32				CountAddOns() const;
			BDiskSystemAddOn*	AddOnAt(int32 index) const;

			// manager will be locked
			BDiskSystemAddOn*	GetAddOn(const char* name);
			void				PutAddOn(BDiskSystemAddOn* addOn);

private:
			struct AddOnImage;
			struct AddOn;

								DiskSystemAddOnManager();

			AddOn*				_AddOnAt(int32 index) const;

private:
			mutable BLocker		fLock;
			BList				fAddOns;
			int32				fLoadCount;

	static	DiskSystemAddOnManager* sManager;
};


}	// namespace BPrivate

using BPrivate::DiskSystemAddOnManager;

#endif	// _DISK_SYSTEM_ADD_ON_MANAGER_H
