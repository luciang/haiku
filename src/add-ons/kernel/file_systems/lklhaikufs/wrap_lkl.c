/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <asm/lkl.h>
#include <asm/disk.h>
#include <asm/env.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/stat.h>

#define BRIDGE_LKL
#include "lkl-haiku-bridge.h"


// we can't include Haiku's headers here because they define types
// already defined in lkl's headers. Because of this we have to
// manually declare functions defined by Haiku that we use here.
extern void * memcpy(void * dst, const void * src, int size);
extern int dprintf(const char * fmt, ...);
extern char * strdup(const char * str);
extern char * strncat(char *dest, const char *src, lh_size_t n);
extern void * malloc(int len);
extern void free(void * p);


#define LKL_MOUNT_PATH_TEMPLATE			"/mnt/xxxxxxxxxxxxxxxx"
#define LKL_MOUNT_PATH_TEMPLATE_LEN		sizeof(LKL_MOUNT_PATH_TEMPLATE)
typedef struct lklfs_fs_volume {
	int fd;
	int readonly;
	__kernel_dev_t dev;
	char mnt_path[LKL_MOUNT_PATH_TEMPLATE_LEN];
} lklfs_fs_volume;


static inline
char *
rel_to_abs_path(struct lklfs_fs_volume * vol, const char * rel_path)
{
	int len = strlen(vol->mnt_path) + sizeof('/') + strlen(rel_path) + sizeof('\0');
	char * abs_path = malloc(len);
	if (abs_path != NULL)
		snprintf(abs_path, len, "%s/%s", vol->mnt_path, rel_path);

	return abs_path;
}

/*! Create a Linux device and mount it

	Returns the Linux path to the mounted device in @mntpath.
	return 0 - in case of error
	return a non-zero dev_t on success
*/
static __kernel_dev_t
mount_disk(int fd, __u64 size, unsigned long flags,
	char * mntpath, unsigned long mntpath_size)
{
	int rc;
	__kernel_dev_t dev;
	dev = lkl_disk_add_disk((void*) fd, size / 512);
	dprintf("mount_disk> dev=%d\n", (int) dev);
	if (dev == 0)
		return 0;


	rc = lkl_mount_dev(dev, NULL, flags, NULL, mntpath, mntpath_size);
	if (rc != 0) {
		dprintf("mount_disk> lkl_mount_dev err rc=%d\n", rc);
		lkl_disk_del_disk(dev);
		return 0;
	}

	dprintf("mount_disk> success\n");
	return dev;
}


static int
umount_disk(__kernel_dev_t dev)
{
	int rc;
	rc = lkl_umount_dev(dev, 0);
	if (rc != 0)
		dprintf( "umount_disk> lkl_umount_dev rc=%d\n", rc);
	rc = lkl_disk_del_disk(dev);
	if (rc != 0)
		dprintf( "umount_disk> lkl_disk_del_disk rc=%d\n", rc);

	dprintf( "umount_disk> exiting\n");
	return rc;
}


static void
list_files(const char * path)
{
	int fd;
	dprintf("[list_files] -------- printing contents of [%s]\n", path);
	fd = lkl_sys_open(path, O_RDONLY | O_LARGEFILE | O_DIRECTORY, 0);
	if (fd >= 0) {
		char x[4096];
		int count, reclen;
		struct __kernel_dirent * de;

		count = lkl_sys_getdents(fd, (struct __kernel_dirent*) x, sizeof(x));

		de = (struct __kernel_dirent *) x;
		while (count > 0) {
			reclen = de->d_reclen;
			dprintf("[list_files] %s %ld\n", de->d_name, de->d_ino);
			de = (struct __kernel_dirent *) ((char *) de + reclen);
			count -= reclen;
		}

		lkl_sys_close(fd);
	}
	dprintf("[list_files] ++++++++ done printing contents of [%s]\n", path);
}


static int
fill_partition_id(const char * mnt_path, lklfs_partition_id * part)
{
	struct __kernel_statfs stat;
	int rc;
	rc = lkl_sys_statfs(mnt_path, &stat);
	if (rc < 0)
		return -1;

	part->content_size = stat.f_blocks * stat.f_bsize;
	part->block_size = stat.f_bsize;
	part->content_name = strdup("FIXME: LKL: label retrieval not implemented ");
	if (part->content_name == NULL)
		return -1;

	return 0;
}


int
lklfs_identify_partition_impl(int fd, lh_off_t size, void** _cookie)
{
	int rc = 0;
	__kernel_dev_t dev;
	char mnt_str[] = { LKL_MOUNT_PATH_TEMPLATE };
	struct lklfs_partition_id part;


	// we only want some info about the partition, mount it read-only.
	dev = mount_disk(fd, size, MS_RDONLY, mnt_str, sizeof(mnt_str));
	if (dev == 0)
		return -1;

	// sone debug routines, just checking if stuff works correctly.
	dprintf("wrap_lkl_identify_partition:: mnt_str=%s\n", mnt_str);
	list_files(mnt_str);
	list_files("."); // should be equal to '/'


	// save some information for a later call for lklfs_scan_partition
	rc = fill_partition_id(mnt_str, &part);
	if (rc != 0)
		dprintf("wrap_lkl_identify_partition: fill_partition_id returned rc=%d\n", rc);

	// umount the partition as we've gathered all needed info and this.
	// This fd won't be valid in other fs calls (e.g. scan_partition) anyway.
	rc |= umount_disk(dev);
	if (rc != 0) {
		dprintf("wrap_lkl_identify_partition: umount_disk failed rc=%d\n", rc);
		return -1;
	}

	*_cookie = malloc(sizeof(struct lklfs_partition_id));
	if (*_cookie == NULL)
		return -1;
	memcpy(*_cookie, &part, sizeof(part));

	return 0;
}




void *
lklfs_mount_impl(int fd, lh_off_t size, int readonly)
{
	lklfs_fs_volume * vol = (lklfs_fs_volume *) malloc(sizeof(lklfs_fs_volume));
	if (vol == NULL)
		return NULL;

	int flags = (readonly ? MS_RDONLY : 0);

	vol->fd = fd;
	vol->readonly = readonly;
	snprintf(vol->mnt_path, sizeof(vol->mnt_path), LKL_MOUNT_PATH_TEMPLATE);

	vol->dev = mount_disk(fd, size, flags, vol->mnt_path, sizeof(vol->mnt_path));
	if (vol->dev == 0) {
		free(vol);
		return NULL;
	}

	dprintf("wrap_lkl_mount:: success\n");
	return vol;
}


int
lklfs_umount_impl(void * vol_)
{
	lklfs_fs_volume * vol = (lklfs_fs_volume *) vol_;
	__kernel_dev_t dev = vol->dev;
	free(vol);
	return umount_disk(dev);
}


// Returns the inode corresponding to @path, relative to @vol_'s  mount point.
// Returns -1 on error.
lh_ino_t
lklfs_get_ino(void * vol_, const char * path)
{
	int rc;
	struct __kernel_stat stat;
	char * abs_path = rel_to_abs_path(vol_, path);
	if (abs_path == NULL)
		return -1;

	rc = lkl_sys_newstat(abs_path, &stat);
	free(abs_path);
	if (rc < 0) {
		dprintf("lklfs_get_ino:: cannot stat [%s], rc=%d\n", abs_path, rc);
		return -1;
	}

	return stat.st_ino;
}


int
lklfs_read_fs_info_impl(void * vol_, struct lh_fs_info * fi)
{
	lklfs_fs_volume * vol = (lklfs_fs_volume *) vol_;
	struct __kernel_statfs stat;
	int rc;

	rc = lkl_sys_statfs(vol->mnt_path, &stat);
	if (rc < 0)
		return -1;

	fi->flags		 = vol->readonly ? LH_FS_READONLY : 0;
	fi->block_size	 = stat.f_bsize;
	fi->io_size		 = stat.f_bsize;
		// TODO: is this the optimal I/O size?
	fi->total_blocks = stat.f_blocks;
	fi->free_blocks	 = stat.f_bfree;
	fi->total_nodes	 = stat.f_files;
	fi->free_nodes	 = stat.f_ffree;
	snprintf(fi->volume_name, sizeof(fi->volume_name),
		"FIXME: lklfs_read_fs_info_impl fi->volume_name");

	return 0;
}


int
lklfs_read_stat_impl(void * vol_, void * vnode_, struct lh_stat * ls)
{
	int rc;
	struct __kernel_stat stat;
	char * abs_path = rel_to_abs_path(vol_, (char *) vnode_);
	if (abs_path == NULL)
		return -1;

	rc = lkl_sys_newstat(abs_path, &stat);
	free(abs_path);
	if (rc != 0)
		return rc;

	ls->st_mode		  = stat.st_mode;
	ls->st_nlink	  = stat.st_nlink;
	ls->st_uid		  = stat.st_uid;
	ls->st_gid		  = stat.st_gid;
	ls->st_size		  = stat.st_size;
	ls->st_blksize	  = stat.st_blksize;
	ls->st_blocks	  = stat.st_blocks;
	ls->st_atim		  = stat.st_atime;
	ls->st_atim_nsec  = stat.st_atime_nsec;
	ls->st_mtim		  = stat.st_mtime;
	ls->st_mtim_nsec  = stat.st_mtime_nsec;
	ls->st_ctim		  = stat.st_ctime;
	ls->st_ctim_nsec  = stat.st_ctime_nsec;
	// FIXME: POSIX stat does not have creation time
	ls->st_crtim	  = stat.st_ctime;
	ls->st_crtim_nsec = stat.st_ctime_nsec;

	return 0;
}


int
lklfs_open_dir_impl(void * vol_, void * vnode_, void ** _cookie)
{
	char * abs_path = rel_to_abs_path(vol_, (char *) vnode_);
	if (abs_path == NULL)
		return -1;

	int fd = lkl_sys_open(abs_path, O_RDONLY | O_LARGEFILE | O_DIRECTORY, 0);
	free(abs_path);
	*_cookie = (void *) fd;
	if (fd < 0)
		return -1;

	return 0;
}


int
lklfs_close_dir_impl(void * _cookie)
{
	int fd = (int) _cookie;
	int rc = lkl_sys_close(fd);
	if (rc != 0)
		return -1;

	return 0;
}


// Returns the number of entries successfully read or -1 on error.
int
lklfs_read_dir_impl(void * _cookie, struct lh_dirent * ld, int bufferSize)
{
	int bytesRead;
	int fd = (int) _cookie;
	int ld_dirent_count = bufferSize / sizeof(struct lh_dirent);
	int i = 0;
	struct __kernel_dirent * de, * de_;


	if (ld_dirent_count == 0)
		return -1;

	// save a backup copy of the pointer for free()
	de_ = de = (struct __kernel_dirent *) malloc(bufferSize);
	if (de == NULL)
		return -1;


	bytesRead = lkl_sys_getdents(fd, de, bufferSize);
	if (bytesRead < 0) {
		free(de);
		return -1;
	}

	while (bytesRead > 0 && i < ld_dirent_count) {
		bytesRead -= de->d_reclen;
		ld[i].d_ino = de->d_ino;
		ld[i].d_name[0] = '\0';
		strncat(ld[i].d_name, de->d_name, sizeof(ld[i].d_name) - 1);
		de = (struct __kernel_dirent *) ((char *) de + de->d_reclen);
		i++;
	}

	// always remember to delete the pointer that was received from malloc()
	free(de_);
	return i;
}


int
lklfs_rewind_dir_impl(void * cookie)
{
	int fd = (int) cookie;
	int rc;

	rc = lkl_sys_lseek(fd, 0,  SEEK_SET);
	if (rc != 0)
		return -1;

	return 0;
}

