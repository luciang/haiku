// ddm_userland_interface.cpp

#include <KDiskDevice.h>
#include <KDiskDeviceManager.h>
#include <KDiskDeviceUtils.h>
#include <KFileDiskDevice.h>
#include <KDiskSystem.h>

#include "ddm_userland_interface.h"
#include "UserDataWriter.h"

// get_current_team
static
team_id
get_current_team()
{
	// TODO: There must be a straighter way in kernelland.
	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	return info.team;
}

// check_shadow_partition
static
bool
check_shadow_partition(const KPartition *partition, int32 changeCounter)
{
	return (partition && partition->Device() && partition->IsShadowPartition()
			&& partition->Device()->ShadowOwner() == get_current_team()
			&& changeCounter == partition->ChangeCounter());
}

// get_unmovable_descendants
static
bool
get_unmovable_descendants(KPartition *partition, partition_id *&unmovable,
						  size_t &unmovableSize, partition_id *&needUnmounting,
						  size_t &needUnmountingSize)
{
	// check parameters
	if (!partition || !unmovable || !needUnmounting || unmovableSize == 0
		|| needUnmountingSize) {
		return false;
	}
	// check partition
	KDiskSystem *diskSystem = partition->DiskSystem();
	bool isNoOp = true;
	bool supports = (diskSystem
					 && diskSystem->SupportsMoving(partition, &isNoOp));
	if (supports) {
		unmovable[0] = partition->ID();
		unmovableSize--;
	}
	if (supports && !isNoOp && diskSystem->IsFileSystem()) {
		needUnmounting[0] = partition->ID();
		needUnmountingSize--;
	}
	// check child partitions
	for (int32 i = 0; KPartition *child = partition->ChildAt(i); i++) {
		if (!get_unmovable_descendants(child, unmovable, unmovableSize,
									   needUnmounting, needUnmountingSize)) {
			return false;
		}
	}
	return true;
}

// validate_move_descendants
static
status_t
validate_move_descendants(KPartition *partition, off_t moveBy, bool force)
{
	if (!partition)
		return B_BAD_VALUE;
	// check partition
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (diskSystem || diskSystem->SupportsMoving(partition, NULL)) {
		off_t offset = partition->Offset() + moveBy;
		off_t newOffset = offset;
		if (!diskSystem->ValidateMove(partition, &newOffset)
			|| newOffset != offset) {
			return B_ERROR;
		}
	} else if (!force)
		return B_ERROR;
	// check children
	for (int32 i = 0; KPartition *child = partition->ChildAt(i); i++) {
		status_t error = validate_move_descendants(child, moveBy, force);
		if (error != B_OK)
			return error;
	}
	return B_OK;
}

// validate_resize_partition
static
status_t
validate_resize_partition(KPartition *partition, int32 changeCounter,
						  off_t *size, bool resizeContents)
{
	if (!partition || !size)
		return B_BAD_VALUE;
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return B_BAD_VALUE;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return B_BUSY;
	}
	// get the parent disk system and let it check the value
	KDiskSystem *parentDiskSystem = partition->Parent()->DiskSystem();
	if (!parentDiskSystem)
		return B_ENTRY_NOT_FOUND;
	if (!parentDiskSystem->ValidateResizeChild(partition, size))
		return B_ERROR;
	// get the child disk system and let it check the value
	if (resizeContents) {
		KDiskSystem *childDiskSystem = partition->DiskSystem();
		if (!childDiskSystem)
			return B_ENTRY_NOT_FOUND;
		off_t childSize = *size;
		// don't worry, if the content system desires to be smaller
		if (!childDiskSystem->ValidateResize(partition, &childSize)
			|| childSize > *size) {
			return B_ERROR;
		}
	}
	return B_OK;
}

// _kern_get_next_disk_device_id
partition_id
_kern_get_next_disk_device_id(int32 *cookie, size_t *neededSize)
{
	if (!cookie)
		return B_BAD_VALUE;
	partition_id id = B_ENTRY_NOT_FOUND;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the next device
	if (KDiskDevice *device = manager->RegisterNextDevice(cookie)) {
		PartitionRegistrar _(device, true);
		id = device->ID();
		if (neededSize) {
			if (DeviceReadLocker locker = device) {
				// get the needed size
				UserDataWriter writer;
				device->WriteUserData(writer, false);
				*neededSize = writer.AllocatedSize();
			} else
				return B_ERROR;
		}
	}
	return id;
}

// _kern_find_disk_device
partition_id
_kern_find_disk_device(const char *filename, size_t *neededSize)
{
	if (!filename)
		return B_BAD_VALUE;
	partition_id id = B_ENTRY_NOT_FOUND;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// find the device
	if (KDiskDevice *device = manager->RegisterDevice(filename)) {
		PartitionRegistrar _(device, true);
		id = device->ID();
		if (neededSize) {
			if (DeviceReadLocker locker = device) {
				// get the needed size
				UserDataWriter writer;
				device->WriteUserData(writer, false);
				*neededSize = writer.AllocatedSize();
			} else
				return B_ERROR;
		}
	}
	return id;
}

// _kern_find_partition
partition_id
_kern_find_partition(const char *filename, size_t *neededSize)
{
	if (!filename)
		return B_BAD_VALUE;
	partition_id id = B_ENTRY_NOT_FOUND;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// find the partition
	if (KPartition *partition = manager->RegisterPartition(filename)) {
		PartitionRegistrar _(partition, true);
		id = partition->ID();
		if (neededSize) {
			// get and lock the partition's device
			KDiskDevice *device = manager->RegisterDevice(partition->ID());
			if (!device)
				return B_ENTRY_NOT_FOUND;
			PartitionRegistrar _2(device, true);
			if (DeviceReadLocker locker = device) {
				// get the needed size
				UserDataWriter writer;
				device->WriteUserData(writer, false);
				*neededSize = writer.AllocatedSize();
			} else
				return B_ERROR;
		}
	}
	return id;
}

// _kern_get_disk_device_data
status_t
_kern_get_disk_device_data(partition_id id, bool deviceOnly, bool shadow,
						   user_disk_device_data *buffer, size_t bufferSize,
						   size_t *neededSize)
{
	if (!buffer && bufferSize > 0)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the device
	if (KDiskDevice *device = manager->RegisterDevice(id, deviceOnly)) {
		PartitionRegistrar _(device, true);
		if (DeviceReadLocker locker = device) {
			// write the device data into the buffer
			UserDataWriter writer(buffer, bufferSize);
			device->WriteUserData(writer, shadow);
			if (neededSize)
				*neededSize = writer.AllocatedSize();
			if (writer.AllocatedSize() <= bufferSize)
				return B_OK;
			return B_BUFFER_OVERFLOW;
		}
	}
	return B_ENTRY_NOT_FOUND;
}

// _kern_get_partitionable_spaces
status_t
_kern_get_partitionable_spaces(partition_id partitionID, int32 changeCounter,
							   partitionable_space_data *buffer,
							   int32 count, int32 *actualCount)
{
	if (!buffer && count > 0)
		return B_BAD_VALUE;
	// get the partition
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return B_BAD_VALUE;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	// get the info
	return diskSystem->GetPartitionableSpaces(partition, buffer, count,
											  actualCount);
}

// _kern_register_file_device
partition_id
_kern_register_file_device(const char *filename)
{
	if (!filename)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KFileDiskDevice *device = manager->FindFileDevice(filename))
			return device->ID();
		return manager->CreateFileDevice(filename);
	}
	return B_ERROR;
}

// _kern_unregister_file_device
status_t
_kern_unregister_file_device(partition_id deviceID, const char *filename)
{
	if (deviceID < 0 && !filename)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (deviceID >= 0)
		return manager->DeleteFileDevice(deviceID);
	return manager->DeleteFileDevice(filename);
}

// _kern_get_disk_system_info
status_t
_kern_get_disk_system_info(disk_system_id id, user_disk_system_info *info)
{
	if (!info)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskSystem *diskSystem = manager->FindDiskSystem(id)) {
			DiskSystemLoader _(diskSystem, true);
			diskSystem->GetInfo(info);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}

// _kern_get_next_disk_system_info
status_t
_kern_get_next_disk_system_info(int32 *cookie, user_disk_system_info *info)
{
	if (!cookie || !info)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskSystem *diskSystem = manager->NextDiskSystem(cookie)) {
			DiskSystemLoader _(diskSystem, true);
			diskSystem->GetInfo(info);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}

// _kern_find_disk_system
status_t
_kern_find_disk_system(const char *name, user_disk_system_info *info)
{
	if (!name || !info)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		if (KDiskSystem *diskSystem = manager->FindDiskSystem(name)) {
			DiskSystemLoader _(diskSystem, true);
			diskSystem->GetInfo(info);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}

// _kern_supports_defragmenting_partition
bool
_kern_supports_defragmenting_partition(partition_id partitionID,
									   int32 changeCounter, bool *whileMounted)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return false;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsDefragmenting(partition, whileMounted);
}

// _kern_supports_repairing_partition
bool
_kern_supports_repairing_partition(partition_id partitionID,
								   int32 changeCounter, bool checkOnly,
								   bool *whileMounted)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return false;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsRepairing(partition, checkOnly, whileMounted);
}

// _kern_supports_resizing_partition
bool
_kern_supports_resizing_partition(partition_id partitionID,
								  int32 changeCounter, bool *canResizeContents,
								  bool *whileMounted)
{
	if (!canResizeContents)
		return false;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return false;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return false;
	}
	// get the parent disk system
	KDiskSystem *parentDiskSystem = partition->Parent()->DiskSystem();
	if (!parentDiskSystem)
		return false;
	bool result = parentDiskSystem->SupportsResizingChild(partition);
	if (!result)
		return false;
	// get the child disk system
	KDiskSystem *childDiskSystem = partition->DiskSystem();
	*canResizeContents = (childDiskSystem
		&& childDiskSystem->SupportsResizing(partition, whileMounted));
// TODO: Currently we report that we cannot resize the contents, if the
// partition's disk system is unknown. I found this more logical. It doesn't
// really matter, though, since the API user can check for this situation.
	return result;
}

// _kern_supports_moving_partition
bool
_kern_supports_moving_partition(partition_id partitionID, int32 changeCounter,
								partition_id *unmovable,
								partition_id *needUnmounting,
								size_t bufferSize)
{
	if ((!unmovable || needUnmounting) && bufferSize > 0)
		return false;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return false;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return false;
	}
	// get the parent disk system
	KDiskSystem *parentDiskSystem = partition->Parent()->DiskSystem();
	if (!parentDiskSystem)
		return false;
	bool result = parentDiskSystem->SupportsMovingChild(partition);
	if (!result)
		return false;
	// check the movability of the descendants' contents
	size_t unmovableSize = bufferSize; 
	size_t needUnmountingSize = bufferSize; 
	if (!get_unmovable_descendants(partition, unmovable, unmovableSize,
								   needUnmounting, needUnmountingSize)) {
		return false;
	}
	return result;
}

// _kern_supports_setting_partition_name
bool
_kern_supports_setting_partition_name(partition_id partitionID,
									  int32 changeCounter)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return false;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return false;
	}
	// get the disk system
	KDiskSystem *diskSystem = partition->Parent()->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsSettingName(partition);
}

// _kern_supports_setting_partition_content_name
bool
_kern_supports_setting_partition_content_name(partition_id partitionID,
											  int32 changeCounter,
											  bool *whileMounted)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return false;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsSettingContentName(partition, whileMounted);
}

// _kern_supports_setting_partition_type
bool
_kern_supports_setting_partition_type(partition_id partitionID,
									  int32 changeCounter)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return false;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return false;
	}
	// get the disk system
	KDiskSystem *diskSystem = partition->Parent()->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsSettingType(partition);
}

// _kern_supports_setting_partition_parameters
bool
_kern_supports_setting_partition_parameters(partition_id partitionID,
											int32 changeCounter)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return false;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return false;
	}
	// get the disk system
	KDiskSystem *diskSystem = partition->Parent()->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsSettingParameters(partition);
}

// _kern_supports_setting_partition_content_parameters
bool
_kern_supports_setting_partition_content_parameters(partition_id partitionID,
													int32 changeCounter,
													bool *whileMounted)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return false;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsSettingContentParameters(partition,
														whileMounted);
}

// _kern_supports_initializing_partition
bool
_kern_supports_initializing_partition(partition_id partitionID,
									  int32 changeCounter,
									  const char *diskSystemName)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return false;
	// get the disk system
	KDiskSystem *diskSystem = manager->LoadDiskSystem(diskSystemName);
	if (!diskSystem)
		return false;
	DiskSystemLoader loader(diskSystem, true);
	// get the info
	return diskSystem->SupportsInitializing(partition);
// TODO: Ask the parent partitioning system as well.
}

// _kern_supports_creating_child_partition
bool
_kern_supports_creating_child_partition(partition_id partitionID,
										int32 changeCounter)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return false;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsCreatingChild(partition);
}

// _kern_supports_deleting_child_partition
bool
_kern_supports_deleting_child_partition(partition_id partitionID,
										int32 changeCounter)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return false;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return false;
	}
	// get the disk system
	KDiskSystem *diskSystem = partition->Parent()->DiskSystem();
	if (!diskSystem)
		return false;
	// get the info
	return diskSystem->SupportsDeletingChild(partition);
}

// _kern_is_sub_disk_system_for
bool
_kern_is_sub_disk_system_for(disk_system_id diskSystemID,
							 partition_id partitionID, int32 changeCounter)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return false;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return false;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem || diskSystem->ID() != diskSystemID)
		return false;
	// get the info
	return diskSystem->IsSubSystemFor(partition);
}

// _kern_validate_resize_partition
status_t
_kern_validate_resize_partition(partition_id partitionID, int32 changeCounter,
								off_t *size, bool resizeContents)
{
	if (!size)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	return validate_resize_partition(partition, changeCounter, size,
									 resizeContents);
}

// _kern_validate_move_partition
status_t
_kern_validate_move_partition(partition_id partitionID, int32 changeCounter,
							  off_t *newOffset, bool force)
{
	if (!newOffset)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return B_BAD_VALUE;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return B_BUSY;
	}
	// get the parent disk system and let it check the value
	KDiskSystem *parentDiskSystem = partition->Parent()->DiskSystem();
	if (!parentDiskSystem)
		return B_ENTRY_NOT_FOUND;
	if (!parentDiskSystem->ValidateMoveChild(partition, newOffset))
		return B_ERROR;
	// let the concerned content disk systems check the value
	return validate_move_descendants(partition,
									 partition->Offset() - *newOffset, force);
}

// _kern_validate_set_partition_name
status_t
_kern_validate_set_partition_name(partition_id partitionID,
								  int32 changeCounter, char *name)
{
	if (!name)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return B_BAD_VALUE;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return B_BUSY;
	}
	// get the disk system
	KDiskSystem *diskSystem = partition->Parent()->DiskSystem();
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	// get the info
	if (diskSystem->ValidateSetName(partition, name))
		return B_OK;
	return B_ERROR;
}

// _kern_validate_set_partition_content_name
status_t
_kern_validate_set_partition_content_name(partition_id partitionID,
										  int32 changeCounter, char *name)
{
	if (!name)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return B_BAD_VALUE;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return B_BUSY;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	// get the info
	if (diskSystem->ValidateSetContentName(partition, name))
		return B_OK;
	return B_ERROR;
}

// _kern_validate_set_partition_type
status_t
_kern_validate_set_partition_type(partition_id partitionID,
								  int32 changeCounter, const char *type)
{
	if (!type)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter)
		|| !partition->Parent()) {
		return B_BAD_VALUE;
	}
	if (partition->Parent()->IsBusy()
		|| partition->Parent()->IsDescendantBusy()) {
		return B_BUSY;
	}
	// get the disk system
	KDiskSystem *diskSystem = partition->Parent()->DiskSystem();
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	// get the info
	if (diskSystem->ValidateSetType(partition, type))
		return B_OK;
	return B_ERROR;
}

// _kern_validate_initialize_partition
status_t
_kern_validate_initialize_partition(partition_id partitionID,
									int32 changeCounter,
									const char *diskSystemName, char *name,
									const char *parameters)
{
	if (!diskSystemName || !name)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return B_BAD_VALUE;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return B_BUSY;
	// get the disk system
	KDiskSystem *diskSystem = manager->LoadDiskSystem(diskSystemName);
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	DiskSystemLoader loader(diskSystem, true);
	// get the info
	if (diskSystem->ValidateInitialize(partition, name, parameters))
		return B_OK;
	return B_ERROR;
}

// _kern_validate_create_child_partition
status_t
_kern_validate_create_child_partition(partition_id partitionID,
									  int32 changeCounter, off_t *offset,
									  off_t *size, const char *type,
									  const char *parameters)
{
	if (!offset || !size || !type)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return B_BAD_VALUE;
	if (partition->IsBusy() || partition->IsDescendantBusy())
		return B_BUSY;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	// get the info
	if (diskSystem->ValidateCreateChild(partition, offset, size, type,
										parameters)) {
		return B_OK;
	}
	return B_ERROR;
}

// _kern_get_next_supported_partition_type
status_t
_kern_get_next_supported_partition_type(partition_id partitionID,
										int32 changeCounter, int32 *cookie,
										char *type)
{
	if (!cookie || !type)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->ReadLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceReadLocker locker(partition->Device(), true);
	if (!check_shadow_partition(partition, changeCounter))
		return B_BAD_VALUE;
	// get the disk system
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (!diskSystem)
		return B_ENTRY_NOT_FOUND;
	// get the info
	if (diskSystem->GetNextSupportedType(partition, cookie, type))
		return B_OK;
	return B_ERROR;
}

// _kern_get_partition_type_for_content_type
status_t
_kern_get_partition_type_for_content_type(disk_system_id diskSystemID,
										  const char *contentType, char *type)
{
	if (!contentType || !type)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the disk system
	KDiskSystem *diskSystem = manager->LoadDiskSystem(diskSystemID);
	if (!diskSystem)
		return false;
	DiskSystemLoader loader(diskSystem, true);
	// get the info
	if (diskSystem->GetTypeForContentType(contentType, type))
		return B_OK;
	return B_ERROR;
}

// _kern_prepare_disk_device_modifications
status_t
_kern_prepare_disk_device_modifications(partition_id deviceID)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the device
	if (KDiskDevice *device = manager->RegisterDevice(deviceID, true)) {
		PartitionRegistrar _(device, true);
		if (DeviceWriteLocker locker = device) {
			if (device->ShadowOwner() >= 0)
				return B_BUSY;
			// create shadow device
			return device->CreateShadowDevice(get_current_team());
		}
	}
	return B_ENTRY_NOT_FOUND;
}

// _kern_commit_disk_device_modifications
status_t
_kern_commit_disk_device_modifications(partition_id deviceID, port_id port,
									   int32 token, bool completeProgress)
{
	// not implemented
	return B_ERROR;
}

// _kern_cancel_disk_device_modifications
status_t
_kern_cancel_disk_device_modifications(partition_id deviceID)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the device
	if (KDiskDevice *device = manager->RegisterDevice(deviceID, true)) {
		PartitionRegistrar _(device, true);
		if (DeviceWriteLocker locker = device) {
			if (device->ShadowOwner() != get_current_team())
				return B_BAD_VALUE;
			// delete shadow device
			return device->DeleteShadowDevice();
		}
	}
	return B_ENTRY_NOT_FOUND;
}

// _kern_is_disk_device_modified
bool
_kern_is_disk_device_modified(partition_id device)
{
	// not implemented
	return false;
}

// _kern_defragment_partition
status_t
_kern_defragment_partition(partition_id partitionID, int32 changeCounter)
{
	// not implemented
	return B_ERROR;
}

// _kern_repair_partition
status_t
_kern_repair_partition(partition_id partitionID, int32 changeCounter,
					   bool checkOnly)
{
	// not implemented
	return B_ERROR;
}

// _kern_resize_partition
status_t
_kern_resize_partition(partition_id partitionID, int32 changeCounter,
					   off_t size, bool resizeContents)
{
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->WriteLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);
	// check the size
	if (size == partition->Size())
		return B_OK;
	off_t proposedSize = size;
	status_t error = validate_resize_partition(partition, changeCounter,
											   &proposedSize, resizeContents);
	if (error != B_OK)
		return error;
	if (proposedSize != size)
		return B_BAD_VALUE;
	// new size is fine -- resize the thing
	partition->SetSize(size);
	partition->Changed(B_PARTITION_CHANGED_SIZE);
	if (!resizeContents) {
		partition->UninitializeContents();
		partition->Changed(B_PARTITION_CHANGED_SIZE);
	}
	return B_OK;
}

// _kern_move_partition
status_t
_kern_move_partition(partition_id partitionID, int32 changeCounter,
					 off_t newOffset, bool force)
{
	// not implemented
	return B_ERROR;
}

// _kern_set_partition_name
status_t
_kern_set_partition_name(partition_id partitionID, int32 changeCounter,
						 const char *name)
{
	// not implemented
	return B_ERROR;
}

// _kern_set_partition_content_name
status_t
_kern_set_partition_content_name(partition_id partitionID, int32 changeCounter,
								 const char *name)
{
	// not implemented
	return B_ERROR;
}

// _kern_set_partition_type
status_t
_kern_set_partition_type(partition_id partitionID, int32 changeCounter,
						 const char *type)
{
	// not implemented
	return B_ERROR;
}

// _kern_set_partition_parameters
status_t
_kern_set_partition_parameters(partition_id partitionID, int32 changeCounter,
							   const char *parameters)
{
	// not implemented
	return B_ERROR;
}

// _kern_set_partition_content_parameters
status_t
_kern_set_partition_content_parameters(partition_id partitionID,
									   int32 changeCounter,
									   const char *parameters)
{
	// not implemented
	return B_ERROR;
}

// _kern_initialize_partition
status_t
_kern_initialize_partition(partition_id partitionID, int32 changeCounter,
						   const char *diskSystem, const char *name,
						   const char *parameters)
{
	// not implemented
	return B_ERROR;
}

// _kern_create_child_partition
status_t
_kern_create_child_partition(partition_id partitionID, int32 changeCounter,
							 off_t offset, off_t size, const char *type,
							 const char *parameters, partition_id *child)
{
	// not implemented
	return B_ERROR;
}

// _kern_delete_partition
status_t
_kern_delete_partition(partition_id partitionID, int32 changeCounter)
{
	// not implemented
	return B_ERROR;
}

