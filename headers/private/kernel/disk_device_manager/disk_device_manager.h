// disk_device_manager.h
//
// C API exported by the disk device manager.
// Currently only functions of interest for partition modules/FS add-ons
// are considered.

#ifndef _DISK_DEVICE_MANAGER_H
#define _DISK_DEVICE_MANAGER_H

#include <DiskDeviceDefs.h>
#include <Drivers.h>

#ifdef __cplusplus
extern "C" {
#endif

// C API partition representation
// Fields marked [sys] are set by the system and are not to be changed by
// the disk system modules.
typedef struct partition_data {
	partition_id	id;				// [sys]
	off_t			offset;
	off_t			size;
	uint32			block_size;
	int32			child_count;
	int32			index;			// [sys]
	uint32			status;			// [sys]
	uint32			flags;
	dev_t			volume;			// [sys]
	char			*name;			// max: B_FILE_NAME_LENGTH
	char			*content_name;	//
	char			*type;			//
	const char		*content_type;	// [sys]
	char			*parameters;
	char			*content_parameters;
	void			*cookie;
	void			*content_cookie;
} partition_data;

// C API disk device representation
typedef struct disk_device_data {
	partition_id	id;				// equal to that of the root partition
	uint32			flags;
	char			*path;
	device_geometry	geometry;
} disk_device_data;

// C API partitionable space representation
typedef struct partitionable_space_data {
	off_t	offset;
	off_t	size;
} partitionable_space_data;

// disk device locking
disk_device_data *write_lock_disk_device(partition_id partitionID);
void write_unlock_disk_device(partition_id partitionID);
disk_device_data *read_lock_disk_device(partition_id partitionID);
void read_unlock_disk_device(partition_id partitionID);
	// parameter is the ID of any partition on the device

// getting disk devices/partitions by path
// (no locking required)
int32 find_disk_device(const char *path);
int32 find_partition(const char *path);

// disk device/partition read access
// (read lock required)
disk_device_data *get_disk_device(partition_id partitionID);
partition_data *get_partition(partition_id partitionID);
partition_data *get_parent_partition(partition_id partitionID);
partition_data *get_child_partition(partition_id partitionID, int32 index);

// partition write access
// (write lock required)
partition_data *create_child_partition(partition_id partitionID, int32 index,
									   partition_id childID);
	// childID is an optional input parameter -- -1 to be ignored
bool delete_partition(partition_id partitionID);
void partition_modified(partition_id partitionID);
	// tells the disk device manager, that the parition has been modified

// disk systems
disk_system_id find_disk_system(const char *name);

// jobs
bool set_disk_device_job_status(disk_job_id job, uint32 status);
	// probably not needed
uint32 get_disk_device_job_status(disk_job_id job);
bool update_disk_device_job_progress(disk_job_id job, float progress);
bool update_disk_device_job_extra_progress(disk_job_id job, const char *info);

#ifdef __cplusplus
}
#endif

#endif	// _DISK_DEVICE_MANAGER_H
