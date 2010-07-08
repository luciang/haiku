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


typedef struct lklfs_partition_id {
	lh_off_t content_size;
	lh_u32 block_size;
	char * content_name;
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
extern int lklfs_identify_partition_impl(int fd, lh_off_t size, void** _cookie);
extern void * lklfs_mount_impl(int fd, lh_off_t size, int readonly);
extern int lklfs_umount_impl(void * vol_);
extern lh_ino_t lklfs_get_ino(void * vol_, const char * path);
extern int lklfs_read_fs_info_impl(void * vol_, struct lh_fs_info * fi);

#endif // LKL_HAIKU_BRIDGE_H__
