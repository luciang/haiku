//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------

#include <new>

#include <Partition.h>

#include <DiskDevice.h>
#include <DiskDevicePrivate.h>
#include <DiskDeviceVisitor.h>
#include <DiskSystem.h>
#include <Message.h>
#include <PartitioningInfo.h>
#include <Volume.h>

#include "ddm_userland_interface.h"

/*!	\class BPartition
	\brief A BPartition object represent a partition and provides a lot of
		   methods to retrieve information about it and some to manipulate it.

	Not all BPartitions represent actual on-disk partitions. Some exist only
	to make all devices fit smoothly into the framework (e.g. for floppies,
	\see IsVirtual()), others represents merely partition slots
	(\see IsEmpty()).
*/

// constructor
BPartition::BPartition()
	: fDevice(NULL),
	  fParent(NULL),
	  fPartitionData(NULL)
{
}

// destructor
/*!	\brief Frees all resources associated with this object.
*/
BPartition::~BPartition()
{
	_Unset();
}

// Offset
/*!	\brief Returns the partition's offset relative to the beginning of the
		   device it resides on.
	\return The partition's offset in bytes relative to the beginning of the
			device it resides on.
*/
off_t
BPartition::Offset() const
{
	return (fPartitionData ? fPartitionData->offset : 0);
}

// Size
/*!	\brief Returns the size of the partition.
	\return The size of the partition in bytes.
*/
off_t
BPartition::Size() const
{
	return (fPartitionData ? fPartitionData->size : 0);
}

// BlockSize
/*!	\brief Returns the block size of the device.
	\return The block size of the device in bytes.
*/
uint32
BPartition::BlockSize() const
{
	return (fPartitionData ? fPartitionData->block_size : 0);
}

// Index
/*!	\brief Returns the index of the partition in its session's list of
		   partitions.
	\return The index of the partition in its session's list of partitions.
*/
int32
BPartition::Index() const
{
	return (fPartitionData ? fPartitionData->index : -1);
}

// Status
uint32
BPartition::Status() const
{
	return (fPartitionData ? fPartitionData->status : 0);
}

// ContainsFileSystem
bool
BPartition::ContainsFileSystem() const
{
	return (fPartitionData
			&& (fPartitionData->flags & B_PARTITION_FILE_SYSTEM));
}

// ContainsPartitioningSystem
bool
BPartition::ContainsPartitioningSystem() const
{
	return (fPartitionData
			&& (fPartitionData->flags & B_PARTITION_PARTITIONING_SYSTEM));
}

// IsDevice
bool
BPartition::IsDevice() const
{
	return (fPartitionData
			&& (fPartitionData->flags & B_PARTITION_IS_DEVICE));
}

// IsReadOnly
bool
BPartition::IsReadOnly() const
{
	return (fPartitionData
			&& (fPartitionData->flags & B_PARTITION_READ_ONLY));
}

// IsMounted
/*!	\brief Returns whether the volume is mounted.
	\return \c true, if the volume is mounted, \c false otherwise.
*/
bool
BPartition::IsMounted() const
{
	return (fPartitionData
			&& (fPartitionData->flags & B_PARTITION_MOUNTED));
	// alternatively:
	// return (fPartitionData && fPartitionData->volume >= 0);
}

// IsBusy
bool
BPartition::IsBusy() const
{
	return (fPartitionData
			&& (fPartitionData->flags
				& (B_PARTITION_BUSY | B_PARTITION_DESCENDANT_BUSY)));
}

// Flags
/*!	\brief Returns the flags for this partitions.

	The partition flags are a bitwise combination of:
	- \c B_HIDDEN_PARTITION: The partition can not contain a file system.
	- \c B_VIRTUAL_PARTITION: There exists no on-disk partition this object
	  represents. E.g. for floppies there will be a BPartition object spanning
	  the whole floppy disk.
	- \c B_EMPTY_PARTITION: The partition represents no physical partition,
	  but merely an empty slot. This mainly used to keep the indexing of
	  partitions more persistent. This flag implies also \c B_HIDDEN_PARTITION.

	\return The flags for this partition.
*/
uint32
BPartition::Flags() const
{
	return (fPartitionData ? fPartitionData->flags : 0);
}

// Name
/*!	\brief Returns the name of the partition.

	Note, that not all partitioning system support names. The method returns
	\c NULL, if the partition doesn't have a name.

	\return The name of the partition, or \c NULL, if the partitioning system
			does not support names.
*/
const char *
BPartition::Name() const
{
	return (fPartitionData ? fPartitionData->name : NULL);
}

// ContentName
const char *
BPartition::ContentName() const
{
	return (fPartitionData ? fPartitionData->content_name : NULL);
}

// Type
/*!	\brief Returns a human readable string for the type of the partition.
	\return A human readable string for the type of the partition.
*/
const char *
BPartition::Type() const
{
	return (fPartitionData ? fPartitionData->type : NULL);
}

// ContentType
const char *
BPartition::ContentType() const
{
	return (fPartitionData ? fPartitionData->content_type : NULL);
}

// ID
/*!	\brief Returns a unique identifier for this partition.

	The ID is not persistent, i.e. in general won't be the same after
	rebooting.

	\see BDiskDeviceRoster::GetPartitionWithID().

	\return A unique identifier for this partition.
*/
int32
BPartition::ID() const
{
	return (fPartitionData ? fPartitionData->id : -1);
}

// GetDiskSystem
status_t
BPartition::GetDiskSystem(BDiskSystem *diskSystem) const
{
	if (!fPartitionData || !diskSystem)
		return B_BAD_VALUE;
	if (fPartitionData->disk_system < 0)
		return B_ENTRY_NOT_FOUND;
	return diskSystem->_SetTo(fPartitionData->disk_system);
}

// GetPath
status_t
BPartition::GetPath(BPath *path) const
{
	// not implemented
	return B_ERROR;
}

// GetVolume
/*!	\brief Returns a BVolume for the partition.

	The can succeed only, if the partition is mounted.

	\param volume Pointer to a pre-allocated BVolume, to be initialized to
		   represent the volume.
	\return \c B_OK, if the volume is mounted and the parameter could be set
			accordingly, another error code otherwise.
*/
status_t
BPartition::GetVolume(BVolume *volume) const
{
	status_t error = (fPartitionData && volume ? B_OK : B_BAD_VALUE);
	if (error == B_OK)
		error = volume->SetTo(fPartitionData->volume);
	return error;
}

// GetIcon
/*!	\brief Returns an icon for this partition.

	Note, that currently there are only per-device icons, i.e. the method
	returns the same icon for each partition of a device. But this may change
	in the future.

	\param icon Pointer to a pre-allocated BBitmap to be set to the icon of
		   the partition.
	\param which Size of the icon to be retrieved. Can be \c B_MINI_ICON or
		   \c B_LARGE_ICON.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t
BPartition::GetIcon(BBitmap *icon, icon_size which) const
{
/*
	status_t error = (icon ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		if (IsMounted()) {
			// mounted: get the icon from the volume
			BVolume volume;
			error = GetVolume(&volume);
			if (error == B_OK)
				error = volume.GetIcon(icon, which);
		} else {
			// not mounted: retrieve the icon ourselves
			if (BDiskDevice *device = Device()) {
				// get the icon
				if (error == B_OK)
					error = get_device_icon(device->Path(), icon, which);
			} else 
				error = B_ERROR;
		}
	}
	return error;
*/
	// not implemented
	return B_ERROR;
}

// Mount
/*!	\brief Mounts the volume.

	The volume can only be mounted, if the partition contains a recognized
	file system (\see ContainsFileSystem()) and it is not already mounted.

	\param mountFlags Currently only \c B_MOUNT_READ_ONLY is defined, which
		   forces the volume to be mounted read-only.
	\param parameters File system specific mount parameters.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t
BPartition::Mount(uint32 mountFlags, const char *parameters)
{
	return B_ERROR;	// not implemented
}

// Unmount
/*!	\brief Unmounts the volume.

	The volume can of course only be unmounted, if it currently is mounted.

	\return \c B_OK, if everything went fine, another error code otherwise.
*/
status_t
BPartition::Unmount()
{
	return B_ERROR;	// not implemented
}

// Device
/*!	\brief Returns the device this partition resides on.
	\return The device this partition resides on.
*/
BDiskDevice *
BPartition::Device() const
{
	return fDevice;
}

// Parent
BPartition *
BPartition::Parent() const
{
	return fParent;
}

// ChildAt
BPartition *
BPartition::ChildAt(int32 index) const
{
	if (!fPartitionData || index < 0 || index >= fPartitionData->child_count)
		return NULL;
	return (BPartition*)fPartitionData->children[index]->user_data;
}

// CountChildren
int32
BPartition::CountChildren() const
{
	return (fPartitionData ? fPartitionData->child_count : 0);
}

// FindDescendant
BPartition *
BPartition::FindDescendant(partition_id id) const
{
	IDFinderVisitor visitor(id);
	return const_cast<BPartition*>(this)->VisitEachDescendant(&visitor);
}

// GetPartitioningInfo
status_t
BPartition::GetPartitioningInfo(BPartitioningInfo *info) const
{
	if (!info || !fPartitionData || !_IsShadow())
		return B_BAD_VALUE;
	return info->_SetTo(_ShadowID());
}

// VisitEachChild
BPartition *
BPartition::VisitEachChild(BDiskDeviceVisitor *visitor)
{
	if (visitor) {
		int32 level = _Level();
		for (int32 i = 0; BPartition *child = ChildAt(i); i++) {
			if (child->_AcceptVisitor(visitor, level))
				return child;
		}
	}
	return NULL;
}

// VisitEachDescendant
BPartition *
BPartition::VisitEachDescendant(BDiskDeviceVisitor *visitor)
{
	if (visitor)
		return _VisitEachDescendant(visitor);
	return NULL;
}

// CanDefragment
bool
BPartition::CanDefragment(bool *whileMounted) const
{
	return (fPartitionData && _IsShadow()
			&& _kern_supports_defragmenting_partition(_ShadowID(),
													  whileMounted));
}

// Defragment
status_t
BPartition::Defragment() const
{
	// not implemented
	return B_ERROR;
}

// CanRepair
bool
BPartition::CanRepair(bool checkOnly, bool *whileMounted) const
{
	return (fPartitionData && _IsShadow()
			&& _kern_supports_repairing_partition(_ShadowID(), checkOnly,
												  whileMounted));
}

// Repair
status_t
BPartition::Repair(bool checkOnly) const
{
	// not implemented
	return B_ERROR;
}

// CanResize
bool
BPartition::CanResize(bool *canResizeContents, bool *whileMounted) const
{
	return (fPartitionData && !IsDevice() && Parent() && _IsShadow()
			&& _kern_supports_resizing_partition(_ShadowID(),
					canResizeContents, whileMounted));
}

// ValidateResize
status_t
BPartition::ValidateResize(off_t *size, bool resizeContents) const
{
	if (!fPartitionData || IsDevice() || !Parent() || !_IsShadow() || !size)
		return B_BAD_VALUE;
	return _kern_validate_resize_partition(_ShadowID(), size, resizeContents);
}

// Resize
status_t
BPartition::Resize(off_t size, bool resizeContents)
{
	// not implemented
	return B_ERROR;
}

// CanMove
bool
BPartition::CanMove(BObjectList<BPartition> *unmovableDescendants,
					bool *whileMounted) const
{
	// check parameters
	if (!unmovableDescendants || !fPartitionData || IsDevice() || !Parent()
		|| !_IsShadow()) {
		return false;
	}
	// count descendants and allocate a partition_id array large enough
	int32 descendantCount = _CountDescendants();
	partition_id *descendants = NULL;
	if (descendantCount > 0) {
		descendants = new(nothrow) partition_id[descendantCount];
		if (!descendants)
			return false;
		for (int32 i = 0; i < descendantCount; i++)
			descendants[i] = -1;
	}
	// get the info
	bool result = _kern_supports_moving_partition(_ShadowID(), descendants,
						descendantCount, whileMounted);
	if (result) {
		// find BPartition objects for returned IDs
		for (int32 i = 0; i < descendantCount && descendants[i] != -1; i++) {
			BPartition *descendant = FindDescendant(descendants[i]);
			if (!descendant || !unmovableDescendants->AddItem(descendant)) {
				result = false;
				break;
			}
		}
	}
	// cleanup and return
	delete[] descendants;
	return result;
}

// ValidateMove
status_t
BPartition::ValidateMove(off_t *newOffset, bool force) const
{
	if (!fPartitionData || IsDevice() || !Parent() || !_IsShadow()
		|| !newOffset) {
		return B_BAD_VALUE;
	}
	return _kern_validate_move_partition(_ShadowID(), newOffset, force);
}

// Move
status_t
BPartition::Move(off_t newOffset, bool force)
{
	// not implemented
	return B_ERROR;
}

// CanSetName
bool
BPartition::CanSetName() const
{
	return (fPartitionData && Parent() && _IsShadow()
			&& _kern_supports_setting_partition_name(_ShadowID()));
}

// ValidateSetName
status_t
BPartition::ValidateSetName(char *name) const
{
	if (!fPartitionData || IsDevice() || !Parent() || !_IsShadow())
		return B_BAD_VALUE;
	return _kern_validate_set_partition_name(_ShadowID(), name);
}

// SetName
status_t
BPartition::SetName(const char *name)
{
	// not implemented
	return B_ERROR;
}

// CanSetContentName
bool
BPartition::CanSetContentName(bool *whileMounted) const
{
	return (fPartitionData && _IsShadow()
			&& _kern_supports_setting_partition_content_name(_ShadowID(),
															 whileMounted));
}

// ValidateSetContentName
status_t
BPartition::ValidateSetContentName(char *name) const
{
	if (!fPartitionData || !_IsShadow())
		return B_BAD_VALUE;
	return _kern_validate_set_partition_content_name(_ShadowID(), name);
}

// SetContentName
status_t
BPartition::SetContentName(const char *name)
{
	// not implemented
	return B_ERROR;
}

// CanSetType
bool
BPartition::CanSetType() const
{
	return (fPartitionData && Parent() && _IsShadow()
			&& _kern_supports_setting_partition_type(_ShadowID()));
}

// ValidateSetType
status_t
BPartition::ValidateSetType(const char *type) const
{
	if (!fPartitionData || IsDevice() || !Parent() || !_IsShadow() || !type)
		return B_BAD_VALUE;
	return _kern_validate_set_partition_type(_ShadowID(), type);
}

// SetType
status_t
BPartition::SetType(const char *type)
{
	// not implemented
	return B_ERROR;
}

// CanEditParameters
bool
BPartition::CanEditParameters() const
{
	return (fPartitionData && Parent() && _IsShadow()
			&& _kern_supports_setting_partition_parameters(_ShadowID()));
}

// GetParameterEditor
status_t
BPartition::GetParameterEditor(BDiskDeviceParameterEditor **editor)
{
	// not implemented
	return B_ERROR;
}

// SetParameters
status_t
BPartition::SetParameters(const char *parameters)
{
	// not implemented
	return B_ERROR;
}

// CanEditContentParameters
bool
BPartition::CanEditContentParameters(bool *whileMounted) const
{
	return (fPartitionData && _IsShadow()
			&& _kern_supports_setting_partition_content_parameters(
					_ShadowID(), whileMounted));
}

// GetContentParameterEditor
status_t
BPartition::GetContentParameterEditor(BDiskDeviceParameterEditor **editor)
{
	// not implemented
	return B_ERROR;
}

// SetParameters
status_t
BPartition::SetContentParameters(const char *parameters)
{
	// not implemented
	return B_ERROR;
}

// CanInitialize
bool
BPartition::CanInitialize(const char *diskSystem) const
{
	return (fPartitionData && diskSystem && _IsShadow()
			&& _kern_supports_initializing_partition(_ShadowID(), diskSystem));
}

// GetInitializationParameterEditor
status_t
BPartition::GetInitializationParameterEditor(const char *system,
	BDiskDeviceParameterEditor **editor) const
{
	// not implemented
	return B_ERROR;
}

// ValidateInitialize
status_t
BPartition::ValidateInitialize(const char *diskSystem, char *name,
							   const char *parameters)
{
	if (!fPartitionData || !_IsShadow() || !diskSystem)
		return B_BAD_VALUE;
	return _kern_validate_initialize_partition(_ShadowID(), diskSystem, name,
											   parameters);
}

// Initialize
status_t
BPartition::Initialize(const char *diskSystem, const char *name,
					   const char *parameters)
{
	// not implemented
	return B_ERROR;
}

// CanCreateChild
bool
BPartition::CanCreateChild() const
{
	return (fPartitionData && _IsShadow()
			&& _kern_supports_creating_child_partition(_ShadowID()));
}

// GetChildCreationParameterEditor
status_t
BPartition::GetChildCreationParameterEditor(const char *system,	
	BDiskDeviceParameterEditor **editor) const
{
	// not implemented
	return B_ERROR;
}

// ValidateCreateChild
status_t
BPartition::ValidateCreateChild(off_t *offset, off_t *size, const char *type,
								const char *parameters) const
{
	if (!fPartitionData || !_IsShadow() || !offset || !size || !type)
		return B_BAD_VALUE;
	return _kern_validate_create_child_partition(_ShadowID(), offset, size,
												 type, parameters);
}

// CreateChild
status_t
BPartition::CreateChild(off_t start, off_t size, const char *type,
						const char *parameters, BPartition **child)
{
	// not implemented
	return B_ERROR;
}

// CanDeleteChild
bool
BPartition::CanDeleteChild(int32 index) const
{
	BPartition *child = ChildAt(index);
	return (child && child->_IsShadow()
			&& _kern_supports_deleting_child_partition(child->_ShadowID()));
}

// DeleteChild
status_t
BPartition::DeleteChild(int32 index)
{
	// not implemented
	return B_ERROR;
}

// constructor
/*!	\brief Privatized copy constructor to avoid usage.
*/
BPartition::BPartition(const BPartition &)
{
}

// =
/*!	\brief Privatized assignment operator to avoid usage.
*/
BPartition &
BPartition::operator=(const BPartition &)
{
	return *this;
}

// _SetTo
status_t
BPartition::_SetTo(BDiskDevice *device, BPartition *parent,
				   user_partition_data *data)
{
	_Unset();
	if (!device || !data)
		return B_BAD_VALUE;
	fPartitionData = data;
	fDevice = device;
	fParent = parent;
	fPartitionData->user_data = this;
	// create and init children
	status_t error = B_OK;
	for (int32 i = 0; error == B_OK && i < fPartitionData->child_count; i++) {
		BPartition *child = new(nothrow) BPartition;
		if (child) {
			error = child->_SetTo(fDevice, this, fPartitionData->children[i]);
			if (error != B_OK)
				delete child;
		} else
			error = B_NO_MEMORY;
	}
	// cleanup on error
	if (error != B_OK)
		_Unset();
	return error;
}

// _Unset
void
BPartition::_Unset()
{
	// delete children
	if (fPartitionData) {
		for (int32 i = 0; i < fPartitionData->child_count; i++) {
			if (BPartition *child = ChildAt(i))
				delete child;
		}
		fPartitionData->user_data = NULL;
	}
	fDevice = NULL;
	fParent = NULL;
	fPartitionData = NULL;
}

// _IsShadow
bool
BPartition::_IsShadow() const
{
	return (fPartitionData && fPartitionData->shadow_id >= 0);
}

// _ShadowID
partition_id
BPartition::_ShadowID() const
{
	return (fPartitionData ? fPartitionData->shadow_id : -1);
}

// _DiskSystem
disk_system_id
BPartition::_DiskSystem() const
{
	return (fPartitionData ? fPartitionData->disk_system : -1);
}

// _CountDescendants
int32
BPartition::_CountDescendants() const
{
	int32 count = 1;
	for (int32 i = 0; BPartition *child = ChildAt(i); i++)
		count += child->_CountDescendants();
	return count;
}

// _Level
int32
BPartition::_Level() const
{
	int32 level = 0;
	const BPartition *ancestor = this;
	while ((ancestor = ancestor->Parent()))
		level++;
	return level;
}

// _AcceptVisitor
bool
BPartition::_AcceptVisitor(BDiskDeviceVisitor *visitor, int32 level)
{
	return visitor->Visit(this, level);
}

// _VisitEachDescendant
BPartition *
BPartition::_VisitEachDescendant(BDiskDeviceVisitor *visitor, int32 level)
{
	if (level < 0)
		level = _Level();
	if (_AcceptVisitor(visitor, level))
		return this;
	for (int32 i = 0; BPartition *child = ChildAt(i); i++) {
		if (BPartition *result = child->_VisitEachDescendant(visitor,
															 level + 1)) {
			return result;
		}
	}
	return NULL;
}

