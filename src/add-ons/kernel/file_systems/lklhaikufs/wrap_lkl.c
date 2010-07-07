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

extern int dprintf(const char*, ...);
extern char* strdup(const char* str);
extern void* malloc(int len);
extern void* memcpy(void* dst, const void* src, int size);
extern int cookie_lklfs_identify_partition(int fd, __s64 size, void** _cookie);
extern int cookie_lklfs_scan_partition(void* _cookie, __s64* p_content_size, __u32* p_block_size,
								char** p_content_name);


typedef struct lklfs_partition_id {
	__s64 content_size;
	__u32 block_size;
	char* content_name;
} lklfs_partition_id;


/*
 * return 0 - in case of error
 * return a non-0 dev_t on success
 */
static __kernel_dev_t
mount_disk(int fd, unsigned long size, unsigned long flags,
	char *mntpath, unsigned long mntpath_size)
{
	int rc;
	__kernel_dev_t dev;
	dev = lkl_disk_add_disk((void*) fd, size);
	dprintf("mount_disk> dev=%d\n", (int) dev);
	if (dev == 0)
		return 0;


	rc = lkl_mount_dev(dev, NULL, flags, NULL, mntpath, mntpath_size);
	if (rc != 0) {
		dprintf("mount_disk> lkl_mount_dev rc=%d\n", rc);
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
	fd = lkl_sys_open(path, O_RDONLY|O_LARGEFILE|O_DIRECTORY, 0);
	if (fd >= 0) {
		char x[4096];
		int count, reclen;
		struct __kernel_dirent *de;

		count = lkl_sys_getdents(fd, (struct __kernel_dirent*) x, sizeof(x));

		de = (struct __kernel_dirent*) x;
		while (count > 0) {
			reclen = de->d_reclen;
			dprintf("[list_files] %s %ld\n", de->d_name, de->d_ino);
			de = (struct __kernel_dirent*) ((char*) de+reclen);
			count-=reclen;
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
	dprintf("[[cookie_lklfs_scan_partition]]: lkl_sys_statfs(%s) rc=%d\n", mnt_path, rc);
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
cookie_lklfs_identify_partition(int fd, __s64 size, void** _cookie)
{
	int rc = 0;
	__kernel_dev_t dev;
	char mnt_str[] = { "/mnt/xxxxxxxxxxxxxxxx" };
	struct lklfs_partition_id part;


	// we only want some info about the partition, mount it read-only.
	dev = mount_disk(fd, size, MS_RDONLY, mnt_str, sizeof(mnt_str));
	if (dev == 0)
		return -1;

	// debug
	dprintf("wrap_lkl_identify_partition:: mnt_str=%s\n", mnt_str);
	list_files(mnt_str);
	list_files("."); // should be equal to '/'

	// save some information for later (for lklfs_scan_partition)
	rc = fill_partition_id(mnt_str, &part);
	if (rc != 0)
		dprintf("wrap_lkl_identify_partition: fill_partition_id returned rc=%d\n", rc);

	// umount the partition as we've gathered all needed info and this
	// fd won't be valid in other fs calls (e.g. scan_partition) anyway.
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

