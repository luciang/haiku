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

extern int dprintf(const char*, ...);
extern char* strdup(const char* str);
extern void* malloc(int len);
extern int cookie_lklfs_identify_partition(int fd, __s64 size, void** _cookie);
extern int cookie_lklfs_scan_partition(void* _cookie, __s64* p_content_size, __u32* p_block_size,
								char** p_content_name);
extern void cookie_lklfs_free_cookie(void* _cookie);


/* 
 * return 0 - in case of error
 * return a non-0 dev_t on success
 */
static __kernel_dev_t mount_disk(int fd, unsigned long size,
	char *mntpath, unsigned long mntpath_size)
{
        int rc;
	__kernel_dev_t dev;
	dev = lkl_disk_add_disk((void*) fd, size);
	dprintf( "mount_disk:: dev=%d\n", (int) dev);
	if (dev == 0)
		return 0;
        rc = lkl_mount_dev(dev, NULL, 0, NULL, mntpath, mntpath_size);
	if (rc) {
                dprintf( "mount_disk> lkl_mount_dev rc=%d\n", rc);
		lkl_disk_del_disk(dev);
		return 0;
	}
	dprintf( "mount_disk> success\n");
	return dev;
}

static int umount_disk(__kernel_dev_t dev)
{
	int rc;
	rc = lkl_umount_dev(dev, 0);
	if (rc)
		dprintf( "umount_disk> lkl_umount_dev rc=%d\n", rc);
	rc = lkl_disk_del_disk(dev);
        if (rc)
		dprintf( "umount_disk> lkl_disk_del_disk rc=%d\n", rc);

	dprintf( "umount_disk> exiting\n");
	return rc;
}

static void list_files(const char * path)
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


struct lklfs_partition_id {
	__kernel_dev_t dev; // the Linux identifier for the device block
	char mnt_str[25]; // this will be a string like "/dev/xxxxxxxxxxxxxxxx"
};

int cookie_lklfs_identify_partition(int fd, __s64 size, void** _cookie)
{
	struct lklfs_partition_id *part = malloc(sizeof(struct lklfs_partition_id));
	if (part == NULL)
		return -1;

	snprintf(part->mnt_str, sizeof(part->mnt_str), "/dev/xxxxxxxxxxxxxxxx");
	part->dev = mount_disk(fd, size, part->mnt_str, sizeof(part->mnt_str));
	if (part->dev == 0) {
		free(part);
		return -1;
	}

	// debug:
	dprintf("wrap_lkl_identify_partition:: mnt_str=%s\n", part->mnt_str);
	list_files(part->mnt_str);
	list_files("."); // should be equal to '/'

	*_cookie = part;
	return 0;
}

int cookie_lklfs_scan_partition(void *_cookie, __s64 * p_content_size, __u32 * p_block_size,
								char ** p_content_name)
{
	struct __kernel_statfs stat;
	struct lklfs_partition_id * part = (struct lklfs_partition_id *) _cookie;
	int rc;
	rc = lkl_sys_statfs(part->mnt_str, &stat);
	dprintf("[[cookie_lklfs_scan_partition]]: lkl_sys_statfs rc=%d\n", rc);
	if (rc < 0)
		return -1;
	*p_content_size = stat.f_blocks * stat.f_bsize;
	*p_block_size = stat.f_bsize;
	*p_content_name = strdup("mumu");
	if (*p_content_name == NULL)
		return -1;

	return 0;
}

void cookie_lklfs_free_cookie(void *_cookie)
{
	struct lklfs_partition_id * part = (struct lklfs_partition_id *) _cookie;
	umount_disk(part->dev);
	free(part);
}

