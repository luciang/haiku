/*
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include <SupportDefs.h>

#include <fs_info.h>
#include <libroot_private.h>
#include <posix/realtime_sem_defs.h>
#include <user_group.h>

bool _kern_cpu_enabled(int cpu);

int
getdtablesize(void)
{
	struct rlimit rlimit;
	if (getrlimit(RLIMIT_NOFILE, &rlimit) < 0)
		return 0;

	return rlimit.rlim_cur;
}


long
sysconf(int name)
{
	int err;
	// TODO: This is about what BeOS does, better POSIX conformance would be nice, though

	switch (name) {
		case _SC_ARG_MAX:
			return ARG_MAX;
		case _SC_CHILD_MAX:
			return CHILD_MAX;
		case _SC_CLK_TCK:
			return CLK_TCK;
		case _SC_JOB_CONTROL:
			return 1;
		case _SC_NGROUPS_MAX:
			return NGROUPS_MAX;
		case _SC_OPEN_MAX:
			return OPEN_MAX;
		case _SC_SAVED_IDS:
			return 1;
		case _SC_STREAM_MAX:
			return STREAM_MAX;
		case _SC_TZNAME_MAX:
			return TZNAME_MAX;
		case _SC_VERSION:
			return _POSIX_VERSION;
		case _SC_GETGR_R_SIZE_MAX:
			return MAX_GROUP_BUFFER_SIZE;
		case _SC_GETPW_R_SIZE_MAX:
			return MAX_PASSWD_BUFFER_SIZE;
		case _SC_PAGE_SIZE:
			return B_PAGE_SIZE;
		case _SC_SEM_NSEMS_MAX:
			return MAX_POSIX_SEMS;
		case _SC_SEM_VALUE_MAX:
			return MAX_POSIX_SEM_VALUE;
		case _SC_SEMAPHORES:
			return _POSIX_SEMAPHORES;
		case _SC_THREADS:
			return _POSIX_THREADS;
		case _SC_IOV_MAX:
			return IOV_MAX;
		case _SC_NPROCESSORS_MAX:
			return B_MAX_CPU_COUNT;
		case _SC_NPROCESSORS_CONF:
		{
			system_info info;
			err = get_system_info(&info);
			if (err < B_OK) {
				errno = err;
				return -1;
			}
			return info.cpu_count;
		}
		case _SC_NPROCESSORS_ONLN:
		{
			system_info info;
			int i;
			int count = 0;
			err = get_system_info(&info);
			if (err < B_OK) {
				errno = err;
				return -1;
			}
			for (i = 0; i < info.cpu_count; i++)
				if (_kern_cpu_enabled(i))
					count++;
			return count;
		}
		case _SC_CPUID_MAX:
			return B_MAX_CPU_COUNT - 1;
		case _SC_ATEXIT_MAX:
			return ATEXIT_MAX;
		case _SC_PASS_MAX:
			break;
			//XXX:return PASS_MAX;
		case _SC_PHYS_PAGES:
		{
			system_info info;
			err = get_system_info(&info);
			if (err < B_OK) {
				errno = err;
				return -1;
			}
			return info.max_pages;
		}
		case _SC_AVPHYS_PAGES:
		{
			system_info info;
			err = get_system_info(&info);
			if (err < B_OK) {
				errno = err;
				return -1;
			}
			return info.max_pages - info.used_pages;
		}
		/*
		case _SC_PIPE:
			return 1;
		case _SC_SELECT:
			return 1;
		case _SC_POLL:
			return 1;
		*/
	}

	errno = EINVAL;
	return -1;
}


enum {
	FS_BFS,
	FS_FAT,
	FS_EXT,
	FS_UNKNOWN
};


static int
fstype(const char *fsh_name)
{
	if (!strncmp(fsh_name, "bfs", B_OS_NAME_LENGTH))
		return FS_BFS;
	if (!strncmp(fsh_name, "dos", B_OS_NAME_LENGTH))
		return FS_FAT;
	if (!strncmp(fsh_name, "fat", B_OS_NAME_LENGTH))
		return FS_FAT;
	if (!strncmp(fsh_name, "ext2", B_OS_NAME_LENGTH))
		return FS_EXT;
	if (!strncmp(fsh_name, "ext3", B_OS_NAME_LENGTH))
		return FS_EXT;
	return FS_UNKNOWN;
}



static long
__pathconf_common(struct statvfs *fs, struct stat *st,
	int name)
{
	fs_info info;
	int ret;
	ret = fs_stat_dev(fs->f_fsid, &info);
	if (ret < 0) {
		errno = ret;
		return -1;
	}

	// TODO: many cases should check for file type from st.
	switch (name) {
		case _PC_CHOWN_RESTRICTED:
			return _POSIX_CHOWN_RESTRICTED;

		case _PC_MAX_CANON:
			return MAX_CANON;

		case _PC_MAX_INPUT:
			return MAX_INPUT;

		case _PC_NAME_MAX:
			return fs->f_namemax;
			//return NAME_MAX;

		case _PC_NO_TRUNC:
			return _POSIX_NO_TRUNC;

		case _PC_PATH_MAX:
			return PATH_MAX;

		case _PC_PIPE_BUF:
			return 4096;

		case _PC_LINK_MAX:
			return LINK_MAX;

		case _PC_VDISABLE:
			return _POSIX_VDISABLE;

		case _PC_FILESIZEBITS:
		{
			int type = fstype(info.fsh_name);
			switch (type) {
				case FS_BFS:
				case FS_EXT:
					return 64;
				case FS_FAT:
					return 32;
			}
			// XXX: add fs ? add to statvfs/fs_info ?
			return FILESIZEBITS;
		}

		case _PC_SYMLINK_MAX:
			return SYMLINK_MAX;

		case _PC_2_SYMLINKS:
		{
			int type = fstype(info.fsh_name);
			switch (type) {
				case FS_BFS:
				case FS_EXT:
					return 1;
				case FS_FAT:
					return 0;
			}
			// XXX: there should be an HAS_SYMLINKS flag
			// to fs_info...
			return 1;
		}

		case _PC_XATTR_EXISTS:
		case _PC_XATTR_ENABLED:
		{
#if 0
			/* those seem to be Solaris specific,
			 * else we should return 1 I suppose.
			 * we don't yet map POSIX xattrs
			 * to BFS ones anyway.
			 */
			if (info.flags & B_FS_HAS_ATTR)
				return 1;
			return -1;
#endif
			errno = EINVAL;
			return -1;
		}

		case _PC_SYNC_IO:
		case _PC_ASYNC_IO:
		case _PC_PRIO_IO:
		case _PC_SOCK_MAXBUF:
		case _PC_REC_INCR_XFER_SIZE:
		case _PC_REC_MAX_XFER_SIZE:
		case _PC_REC_MIN_XFER_SIZE:
		case _PC_REC_XFER_ALIGN:
		case _PC_ALLOC_SIZE_MIN:
			/* not yet supported */
			errno = EINVAL;
			return -1;

	}

	errno = EINVAL;
	return -1;
}


long
fpathconf(int fd, int name)
{
	struct statvfs fs;
	struct stat st;
	int ret;
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	ret = fstat(fd, &st);
	if (ret < 0)
		return ret;
	ret = fstatvfs(fd, &fs);
	if (ret < 0)
		return ret;
	return __pathconf_common(&fs, &st, name);
}


long
pathconf(const char *path, int name)
{
	struct statvfs fs;
	struct stat st;
	int ret;
	if (path == NULL) {
		errno = EFAULT;
		return -1;
	}
	ret = lstat(path, &st);
	if (ret < 0)
		return ret;
	ret = statvfs(path, &fs);
	if (ret < 0)
		return ret;
	return __pathconf_common(&fs, &st, name);
}


size_t
confstr(int name, char *buffer, size_t length)
{
	size_t stringLength = 0;
	char *string = "";

	if (!length || !buffer) {
		errno = EINVAL;
		return 0;
	}

	switch (name) {
		case _CS_PATH:
			string = "/bin:/boot/beos/apps:" \
				"/boot/common/bin:/boot/develop/bin";
			break;
		default:
			errno = EINVAL;
			return 0;
	}

	if (buffer != NULL) {
		stringLength = strlen(string) + 1;
		strlcpy(buffer, string,
			min_c(length - 1, stringLength));
	}

	return stringLength;
}

