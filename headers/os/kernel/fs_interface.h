/* File System Interface Layer Definition
** 
** Distributed under the terms of the OpenBeOS License.
*/

#ifndef _FS_INTERFACE_H
#define _FS_INTERFACE_H

#include <ktypes.h>
#include <vfs_types.h>

struct dirent;
struct stat;
struct fs_info;

typedef dev_t mount_id;
typedef ino_t vnode_id;

/* the file system's private data structures */
typedef void *fs_volume;
typedef void *fs_cookie;
typedef void *fs_vnode;

/* passed to write_stat() */
enum write_stat_mask {
	FS_WRITE_STAT_MODE		= 0x0001,
	FS_WRITE_STAT_UID		= 0x0002,
	FS_WRITE_STAT_GID		= 0x0004,
	FS_WRITE_STAT_SIZE		= 0x0008,
	FS_WRITE_STAT_ATIME		= 0x0010,
	FS_WRITE_STAT_MTIME		= 0x0020,
	FS_WRITE_STAT_CRTIME	= 0x0040
};

/* passed to write_fs_info() */
#define	FS_WRITE_FSINFO_NAME	0x0001

#define	B_CURRENT_FS_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif 

typedef struct fs_ops {
	/* general operations */
	status_t (*mount)(mount_id id, const char *device, void *args, fs_volume *_fs, vnode_id *_rootVnodeID);
	status_t (*unmount)(fs_volume fs);

	status_t (*read_fs_info)(fs_volume fs, struct fs_info *info);
	status_t (*write_fs_info)(fs_volume fs, const struct fs_info *info, int mask);
	status_t (*sync)(fs_volume fs);

	/* vnode operations */
	status_t (*lookup)(fs_volume fs, fs_vnode dir, const char *name, vnode_id *_id, int *_type);
	status_t (*get_vnode_name)(fs_volume fs, fs_vnode vnode, char *buffer, size_t bufferSize);

	status_t (*get_vnode)(fs_volume fs, vnode_id id, fs_vnode *_vnode, bool reenter);
	status_t (*put_vnode)(fs_volume fs, fs_vnode vnode, bool reenter);
	status_t (*remove_vnode)(fs_volume fs, fs_vnode vnode, bool reenter);

	/* VM file access */
	status_t (*can_page)(fs_volume fs, fs_vnode v);
	ssize_t (*read_page)(fs_volume fs, fs_vnode v, iovecs *vecs, off_t pos);
	ssize_t (*write_page)(fs_volume fs, fs_vnode v, iovecs *vecs, off_t pos);

	/* common operations */
	status_t (*ioctl)(fs_volume fs, fs_vnode v, fs_cookie cookie, ulong op, void *buffer, size_t length);
	status_t (*fsync)(fs_volume fs, fs_vnode v);

	status_t (*read_link)(fs_volume fs, fs_vnode link, char *buffer, size_t bufferSize);
	status_t (*write_link)(fs_volume fs, fs_vnode link, char *toPath);
	status_t (*create_symlink)(fs_volume fs, fs_vnode dir, const char *name, const char *path, int mode);

	status_t (*link)(fs_volume fs, fs_vnode dir, const char *name, fs_vnode vnode);
	status_t (*unlink)(fs_volume fs, fs_vnode dir, const char *name);
	status_t (*rename)(fs_volume fs, fs_vnode fromDir, const char *fromName, fs_vnode toDir, const char *toName);

	status_t (*access)(fs_volume fs, fs_vnode vnode, int mode);
	status_t (*read_stat)(fs_volume fs, fs_vnode vnode, struct stat *stat);
	status_t (*write_stat)(fs_volume fs, fs_vnode vnode, const struct stat *stat, int statMask);

	/* file operations */
	status_t (*create)(fs_volume fs, fs_vnode dir, const char *name, int openMode, int perms, fs_cookie *_cookie, vnode_id *_newVnodeID);
	status_t (*open)(fs_volume fs, fs_vnode v, int openMode, fs_cookie *_cookie);
	status_t (*close)(fs_volume fs, fs_vnode v, fs_cookie cookie);
	status_t (*free_cookie)(fs_volume fs, fs_vnode v, fs_cookie cookie);
	ssize_t (*read)(fs_volume fs, fs_vnode v, fs_cookie cookie, off_t pos, void *buffer, size_t *length);
	ssize_t (*write)(fs_volume fs, fs_vnode v, fs_cookie cookie, off_t pos, const void *buffer, size_t *length);
	off_t (*seek)(fs_volume fs, fs_vnode v, fs_cookie cookie, off_t pos, int seekType);

	/* directory operations */
	status_t (*create_dir)(fs_volume fs, fs_vnode parent, const char *name, int perms, vnode_id *_newVnodeID);
	status_t (*remove_dir)(fs_volume fs, fs_vnode parent, const char *name);
	status_t (*open_dir)(fs_volume fs, fs_vnode vnode, fs_cookie *_cookie);
	status_t (*close_dir)(fs_volume fs, fs_vnode vnode, fs_cookie cookie);
	status_t (*free_dir_cookie)(fs_volume fs, fs_vnode vnode, fs_cookie cookie);
	status_t (*read_dir)(fs_volume fs, fs_vnode vnode, fs_cookie cookie, struct dirent *buffer, size_t bufferSize, uint32 *_num);
	status_t (*rewind_dir)(fs_volume fs, fs_vnode vnode, fs_cookie cookie);

	/* attribute directory operations */
	status_t (*open_attr_dir)(fs_volume fs, fs_vnode vnode, fs_cookie *_cookie);
	status_t (*close_attr_dir)(fs_volume fs, fs_vnode vnode, fs_cookie cookie);
	status_t (*free_attr_dir_cookie)(fs_volume fs, fs_vnode vnode, fs_cookie cookie);
	status_t (*read_attr_dir)(fs_volume fs, fs_vnode vnode, fs_cookie cookie, struct dirent *buffer, size_t bufferSize, uint32 *_num);
	status_t (*rewind_attr_dir)(fs_volume fs, fs_vnode vnode, fs_cookie cookie);

	/* attribute operations */
	status_t (*create_attr)(fs_volume fs, fs_vnode file, const char *name, uint32 type, int openMode, fs_cookie *_cookie);
	status_t (*open_attr)(fs_volume fs, fs_vnode file, const char *name, int openMode, fs_cookie *_cookie);
	status_t (*close_attr)(fs_volume fs, fs_vnode file, fs_cookie cookie);
	status_t (*free_attr_cookie)(fs_volume fs, fs_vnode file, fs_cookie cookie);
	ssize_t (*read_attr)(fs_volume fs, fs_vnode file, fs_cookie cookie, off_t pos, void *buffer, size_t *len);
	ssize_t (*write_attr)(fs_volume fs, fs_vnode file, fs_cookie cookie, off_t pos, const void *buffer, size_t *len);
	status_t (*seek_attr)(fs_volume fs, fs_vnode file, fs_cookie cookie, off_t pos, int seekType);

	status_t (*read_attr_stat)(fs_volume fs, fs_vnode file, fs_cookie cookie, struct stat *stat);
	status_t (*write_attr_stat)(fs_volume fs, fs_vnode file, fs_cookie cookie, const struct stat *stat, int statMask);
	status_t (*rename_attr)(fs_volume fs, fs_vnode fromFile, const char *fromName, fs_vnode toFile, const char *toName);
	status_t (*remove_attr)(fs_volume fs, fs_vnode file, const char *name);

	/* index directory & index operations */
	status_t (*open_index_dir)(fs_volume fs, fs_cookie *cookie);
	status_t (*close_index_dir)(fs_volume fs, fs_cookie cookie);
	status_t (*free_index_dir_cookie)(fs_volume fs, fs_cookie cookie);
	status_t (*read_index_dir)(fs_volume fs, fs_cookie cookie, struct dirent *buffer, size_t bufferSize, uint32 *_num);
	status_t (*rewind_index_dir)(fs_volume fs, fs_cookie cookie);

	status_t (*create_index)(fs_volume fs, const char *name, uint32 type, int flags);
	status_t (*remove_index)(fs_volume fs, const char *name);
	status_t (*read_index_stat)(fs_volume fs, const char *name, struct stat *stat);

	/* query operations */
	status_t (*open_query)(fs_volume fs, const char *query, uint32 flags, port_id port, uint32 token, fs_cookie *_cookie);
	status_t (*close_query)(fs_volume fs, fs_cookie cookie);
	status_t (*free_query_cookie)(fs_volume fs, fs_cookie cookie);
	status_t (*read_query)(fs_volume fs, fs_cookie cookie, struct dirent *buffer, size_t bufferSize, uint32 *_num);
	status_t (*rewind_query)(fs_volume fs, fs_cookie cookie);
} fs_ops;

#ifdef __cplusplus
}
#endif

/* variables exported by the file system add-on */
extern fs_ops	fs_entry;
extern int32	api_version;

#endif	/* _FS_INTERFACE_H */
