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

/* 
 * return 0 - in case of error
 * return a non-0 dev_t on success
 */
__kernel_dev_t mount_disk(int fd, unsigned long size, char *mntpath, unsigned long mntpath_size)
{
        int rc;
	__kernel_dev_t dev;
        dev = lkl_disk_add_disk(fd, size);
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

int umount_disk(__kernel_dev_t dev)
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

void list_files(const char * path)
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

int wrap_lkl_identify_partition(int fd, unsigned int size)
{
	__kernel_dev_t dev;
	char mnt_str[]= { "/dev/xxxxxxxxxxxxxxxx" };
	dev = mount_disk(fd, size, mnt_str, sizeof(mnt_str));
	if (dev == 0)
		return -1;
	dprintf("wrap_lkl_identify_partition:: mnt_str=%s\n", mnt_str);
	list_files(mnt_str);
	list_files("."); // should be equal to '/'
	umount_disk(dev);
	return 0;
}
