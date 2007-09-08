/*
 * Copyright 2003-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */

#include <KernelExport.h>

#include <stdio.h>
#include <string.h>

#include "KDiskDevice.h"
#include "KDiskDeviceManager.h"
#include "KDiskDeviceUtils.h"
#include "KDiskSystem.h"
#include "KPartition.h"
#include "KPath.h"
#include "KScanPartitionJob.h"

// debugging
#define DBG(x)
#define OUT dprintf


/**
	Creates the job.

	\param partitionID the partition to scan
*/
KScanPartitionJob::KScanPartitionJob(partition_id partitionID)
	: KDiskDeviceJob(B_DISK_DEVICE_JOB_SCAN, partitionID)
{
	SetDescription("scanning partition");
}


KScanPartitionJob::~KScanPartitionJob()
{
}


status_t
KScanPartitionJob::Do()
{
	// get the partition
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	KPartition *partition = manager->WriteLockPartition(PartitionID());
	if (!partition)
		return B_ENTRY_NOT_FOUND;

	KDiskDevice *device = partition->Device();
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(device, true);
	DeviceWriteLocker locker(device, true);
	// scan the partition
	status_t error = B_OK;
	if (device->HasMedia()) {
		error = _ScanPartition(partition);
		if (error != B_OK) {
			// ...
			error = B_OK;
		}
	}
	return error;
}


status_t
KScanPartitionJob::_ScanPartition(KPartition *partition)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// the partition's device must be write-locked
	if (!partition)
		return B_BAD_VALUE;
	if (partition->DiskSystem() != NULL) {
		// TODO: this is more or less a hack to prevent rescanning a partition
		for (int32 i = 0; KPartition *child = partition->ChildAt(i); i++)
			_ScanPartition(child);
		return B_OK;
	}

	DBG(
		KPath partitionPath;
		partition->GetPath(&partitionPath);
		OUT("KDiskDeviceManager::_ScanPartition(%s)\n", partitionPath.Path());
	)

	// publish the partition
	status_t error;
	if (!partition->IsPublished()) {
		error = partition->PublishDevice();
		if (error != B_OK)
			return error;
	}

	// find the disk system that returns the best priority for this partition
	float bestPriority = -1;
	KDiskSystem *bestDiskSystem = NULL;
	void *bestCookie = NULL;
	int32 itCookie = 0;
	while (KDiskSystem *diskSystem = manager->LoadNextDiskSystem(&itCookie)) {
		DBG(OUT("  trying: %s\n", diskSystem->Name()));

		void *cookie = NULL;
		float priority = diskSystem->Identify(partition, &cookie);

		DBG(OUT("  returned: %ld/1000\n", int32(1000 * priority)));

		if (priority >= 0 && priority > bestPriority) {
			// new best disk system
			if (bestDiskSystem) {
				bestDiskSystem->FreeIdentifyCookie(partition, bestCookie);
				bestDiskSystem->Unload();
			}
			bestPriority = priority;
			bestDiskSystem = diskSystem;
			bestCookie = cookie;
		} else {
			// disk system doesn't identify the partition or worse than our
			// current favorite
			if (priority >= 0)
				diskSystem->FreeIdentifyCookie(partition, cookie);
			diskSystem->Unload();
		}
	}

	// now, if we have found a disk system, let it scan the partition
	if (bestDiskSystem) {
		DBG(OUT("  scanning with: %s\n", bestDiskSystem->Name()));
		error = bestDiskSystem->Scan(partition, bestCookie);
		bestDiskSystem->FreeIdentifyCookie(partition, bestCookie);
		if (error == B_OK) {
			partition->SetDiskSystem(bestDiskSystem);
			for (int32 i = 0; KPartition *child = partition->ChildAt(i); i++)
				_ScanPartition(child);
		} else {
			// TODO: Handle the error.
			DBG(OUT("  scanning failed: %s\n", strerror(error)));
		}
		// now we can safely unload the disk system -- it has been loaded by
		// the partition(s) and thus will not really be unloaded
		bestDiskSystem->Unload();
	} else {
		// contents not recognized
		// nothing to be done -- partitions are created as unrecognized
	}
	return error;
}
