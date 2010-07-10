/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#ifndef LKL_HAIKU_BRIDGE_H__
#define LKL_HAIKU_BRIDGE_H__

#if !defined(BRIDGE_HAIKU) && !defined(BRIDGE_LKL)
# error You must specify either BRIDGE_LKL or BRIDGE_HAIKU
#endif

#if defined(BRIDGE_HAIKU)
#define DEF(haiku_type, lkl_type, generic_name)	\
	typedef haiku_type generic_name;
#else
# if defined(BRIDGE_LKL)
# define DEF(haiku_type, lkl_type, generic_name)	\
	typedef lkl_type generic_name;
# else
# error You must define either BRIDGE_LKL or BRIDGE_HAIKU
# endif
#endif


DEF(off_t, __s64, lh_off_t);
DEF(uint32, __u32, lh_u32);
DEF(size_t, __u64, lh_size_t);
DEF(ino_t, __s64, lh_ino_t);
DEF(blksize_t, __s32, lh_blksize_t);
DEF(mode_t, __u32, lh_mode_t);
DEF(nlink_t, __kernel_nlink_t, lh_nlink_t);
DEF(uid_t, __kernel_uid_t, lh_uid_t);
DEF(gid_t, __kernel_gid_t, lh_gid_t);
DEF(blkcnt_t, __u64, lh_blkcnt_t);

typedef struct lklfs_partition_id {
	lh_off_t content_size;
	lh_u32 block_size;
	char* content_name;
} lklfs_partition_id;


// lh_fs_info.flags
#define LH_FS_READONLY 0x01

struct lh_fs_info {
	lh_u32		flags;			/* flags (see above) */
	lh_off_t	block_size;		/* fundamental block size */
	lh_off_t	io_size;		/* optimal i/o size */
	lh_off_t	total_blocks;	/* total number of blocks */
	lh_off_t	free_blocks;	/* number of free blocks */
	lh_off_t	total_nodes;	/* total number of nodes */
	lh_off_t	free_nodes;		/* number of free nodes */
	char		volume_name[20];	/* volume name */
};

struct lh_stat {
	lh_mode_t		st_mode;		/* file mode (rwx for user, group, etc) */
	lh_nlink_t		st_nlink;		/* number of hard links to this file */
	lh_uid_t		st_uid;			/* user ID of the owner of this file */
	lh_gid_t		st_gid;			/* group ID of the owner of this file */
	lh_off_t		st_size;		/* size in bytes of this file */
	lh_blksize_t	st_blksize;		/* preferred block size for I/O */
	lh_blkcnt_t		st_blocks;		/* number of blocks allocated for object */
	long			st_atim;		/* last access time (seconds) */
	long			st_atim_nsec;	/* last access time (nanoseconds) */
	long			st_mtim;		/* last modification (seconds) */
	long			st_mtim_nsec;	/* last modification time (nanoseconds) */
	long			st_ctim;		/* last change time (seconds) */
	long			st_ctim_nsec;	/* last change time (nanoseconds) */
	long			st_crtim;		/* creation time (seconds) */
	long			st_crtim_nsec;	/* creation time (nanoseconds) */
};

struct lh_dirent {
	lh_ino_t d_ino;
	char d_name[256];
};

extern int lklfs_identify_partition_impl(int fd, lh_off_t size, void** _cookie);
extern void* lklfs_mount_impl(int fd, lh_off_t size, int readonly);
extern int lklfs_umount_impl(void* vol_);
extern lh_ino_t lklfs_get_ino(void* vol_, const char* path);
extern int lklfs_read_fs_info_impl(void* vol_, struct lh_fs_info* fi);
extern int lklfs_read_stat_impl(void* vol_, void* vnode_, struct lh_stat* ls);
extern int lklfs_open_dir_impl(void* vol_, void* vnode_, void** _cookie);
extern int lklfs_close_dir_impl(void* _cookie);
extern int lklfs_read_dir_impl(void* _cookie, struct lh_dirent* ld, int bufferSize);
extern int lklfs_rewind_dir_impl(void* cookie);

#endif // LKL_HAIKU_BRIDGE_H__
