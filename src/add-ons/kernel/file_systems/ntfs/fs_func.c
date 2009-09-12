/*
 * Copyright (c) 2000-2004 Anton Altaparmakov
 * Copyright (c) 2002-2006 Szabolcs Szakacsits
 *
 * Copyright (c) 2006-2007 Troeglazov Gerasim (3dEyes**)
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program/include file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program (in the main directory of the Linux-NTFS distribution in the
 * file COPYING); if not, write to the Free Software Foundation,Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <KernelExport.h>
#include <time.h>
#include <malloc.h>
#include <driver_settings.h>

#include "ntfs.h"
#include "attributes.h"
#include "lock.h"
#include "volume_util.h"
#include "fs_func.h"


typedef struct identify_cookie {
	NTFS_BOOT_SECTOR boot;
	char label[256];
} identify_cookie;


static status_t
get_node_type(ntfs_inode* ni, int* _type)
{
	ntfs_attr* na;

	if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
		// Directory
		*_type = S_IFDIR;
		return B_OK;
	} else {
		// Regular or Interix (INTX) file
		*_type = S_IFREG;
				
		if (ni->flags & FILE_ATTR_SYSTEM) {
			na = ntfs_attr_open(ni, AT_DATA, NULL,0);
			if (!na) {
				return ENOENT;
			}
			// Check whether it's Interix symbolic link
			if (na->data_size <= sizeof(INTX_FILE_TYPES) + 
			    sizeof(ntfschar) * PATH_MAX && 
			    na->data_size > sizeof(INTX_FILE_TYPES)) {
				INTX_FILE *intx_file;

				intx_file = ntfs_malloc(na->data_size);
				if (!intx_file) {
					ntfs_attr_close(na);
					return EINVAL;
				}
				if (ntfs_attr_pread(na, 0, na->data_size,
						intx_file) != na->data_size) {
					free(intx_file);
					ntfs_attr_close(na);
					return EINVAL;
				}
				if (intx_file->magic == INTX_SYMBOLIC_LINK)
					*_type = FS_SLNK_MODE;
				free(intx_file);
			}
			ntfs_attr_close(na);
		}
	}
		
	return B_OK;
}


float
fs_identify_partition(int fd, partition_data *partition, void **_cookie)
{
	NTFS_BOOT_SECTOR boot;
	identify_cookie *cookie;
	ntfs_volume *ntVolume;
	uint8 *buf = (uint8*)&boot;
	char devpath[256];

	// read in the boot sector
	ERRPRINT("fs_identify_partition: read in the boot sector\n");
	if (read_pos(fd, 0, (void*)&boot, 512) != 512) {
		return -1;
	}

	// check boot signature
	if ((buf[0x1fe] != 0x55 || buf[0x1ff] != 0xaa) && buf[0x15] == 0xf8)
		return -1;

	// check boot signature NTFS
	if (memcmp(buf + 3, "NTFS    ", 8) != 0)
		return -1;

	// get path for device
	if (!ioctl(fd, B_GET_PATH_FOR_DEVICE, devpath))
		return -1;

	// try mount
	ntVolume = utils_mount_volume(devpath, MS_RDONLY, true);
	if (!ntVolume)
		return -1;

	// allocate identify_cookie
	cookie = (identify_cookie *)malloc(sizeof(identify_cookie));
	if (!cookie)
		return -1;

	memcpy(&cookie->boot, &boot, 512);

	strcpy(cookie->label, "NTFS Volume");

	if (ntVolume->vol_name && ntVolume->vol_name[0] != '\0')
		strcpy(cookie->label, ntVolume->vol_name);

	ntfs_umount(ntVolume, true);

	*_cookie = cookie;

	return 0.8f;
}


status_t
fs_scan_partition(int fd, partition_data *partition, void *_cookie)
{
	identify_cookie *cookie = (identify_cookie *)_cookie;
	partition->status = B_PARTITION_VALID;
	partition->flags |= B_PARTITION_FILE_SYSTEM;
	partition->content_size = sle64_to_cpu(cookie->boot.number_of_sectors)
		* le16_to_cpu(cookie->boot.bpb.bytes_per_sector);
	partition->block_size = le16_to_cpu(cookie->boot.bpb.bytes_per_sector);
	partition->content_name = strdup(cookie->label);
	return B_OK;
}


void
fs_free_identify_partition_cookie(partition_data *partition, void *_cookie)
{
	identify_cookie *cookie = (identify_cookie *)_cookie;
	free(cookie);
}


status_t
fs_mount(fs_volume *_vol, const char *device, ulong flags, const char *args,
	ino_t *_rootID)
{
	nspace *ns;
	vnode *newNode = NULL;
	char lockname[32];
	void *handle;
	unsigned long mountFlags = 0;
	status_t result = B_NO_ERROR;

	ERRPRINT("fs_mount - ENTER\n");

	ns = ntfs_malloc(sizeof(nspace));
	if (!ns) {
		result = ENOMEM;
		goto exit;
	}

	*ns = (nspace) {
		.state = NF_FreeClustersOutdate | NF_FreeMFTOutdate,
		.show_sys_files = false,
		.ro = false,
		.flags = 0
	};

	strcpy(ns->devicePath,device);

	sprintf(lockname, "ntfs_lock %lx", ns->id);
	recursive_lock_init_etc(&(ns->vlock), lockname, MUTEX_FLAG_CLONE_NAME);

	handle = load_driver_settings("ntfs");
	ns->show_sys_files = ! (strcasecmp(get_driver_parameter(handle,
		"hide_sys_files", "true", "true"), "true") == 0);
	ns->ro = strcasecmp(get_driver_parameter(handle, "read_only", "false",
		"false"), "false") != 0;
	ns->noatime = strcasecmp(get_driver_parameter(handle, "no_atime", "true",
		"true"), "true") == 0;
	unload_driver_settings(handle);

	if (ns->ro || (flags & B_MOUNT_READ_ONLY) != 0) {
		mountFlags |= MS_RDONLY;
		ns->flags |= B_FS_IS_READONLY;
	}
//	if (ns->noatime)
//		mnt_flags |= MS_NOATIME;

	// TODO: this does not take read-only volumes into account!
	ns->ntvol = utils_mount_volume(device, mountFlags, true);
	if (ns->ntvol != NULL)
		result = B_NO_ERROR;
	else
		result = errno;

	if (result == B_NO_ERROR) {
		*_rootID = FILE_root;
		ns->id = _vol->id;
		_vol->private_volume = (void *)ns;
		_vol->ops = &gNTFSVolumeOps;

		newNode = (vnode*)ntfs_calloc(sizeof(vnode));
		if (newNode == NULL)
			result = ENOMEM;
		else {
			newNode->vnid = *_rootID;
			newNode->parent_vnid = -1;

			result = publish_vnode(_vol, *_rootID, (void*)newNode,
				&gNTFSVnodeOps, S_IFDIR, 0);
			if (result != B_NO_ERROR) {
				free(ns);
				result = EINVAL;
				goto exit;
			} else {
				result = B_NO_ERROR;
				ntfs_mark_free_space_outdated(ns);
				ntfs_calc_free_space(ns);
			}
		}
	}

exit:
	ERRPRINT("fs_mount - EXIT, result code is %s\n", strerror(result));

	return result;
}


status_t
fs_unmount(fs_volume *_vol)
{
	nspace *ns = (nspace*)_vol->private_volume;
	status_t result = B_NO_ERROR;

	ERRPRINT("fs_unmount - ENTER\n");

	ntfs_umount(ns->ntvol, true);

	recursive_lock_destroy(&(ns->vlock));

	free(ns);

	ERRPRINT("fs_unmount - EXIT, result is %s\n", strerror(result));

	return result;
}


status_t
fs_rfsstat(fs_volume *_vol, struct fs_info *fss)
{
	nspace *ns = (nspace*)_vol->private_volume;
	int i;

	LOCK_VOL(ns);

	ERRPRINT("fs_rfsstat - ENTER\n");

	ntfs_calc_free_space(ns);

	fss->dev = ns->id;
	fss->root = FILE_root;
	fss->flags = B_FS_IS_PERSISTENT | B_FS_HAS_MIME | B_FS_HAS_ATTR | ns->flags;

	fss->block_size = ns->ntvol->cluster_size;
	fss->io_size = 65536;
	fss->total_blocks = ns->ntvol->nr_clusters;
	fss->free_blocks = ns->free_clusters;
	strncpy(fss->device_name, ns->devicePath, sizeof(fss->device_name));
	strncpy(fss->volume_name, ns->ntvol->vol_name, sizeof(fss->volume_name));

	for (i = strlen(fss->volume_name) - 1; i >=0 ; i--) {
		if (fss->volume_name[i] != ' ')
			break;
	}
	if (i < 0)
		strcpy(fss->volume_name, "NTFS Untitled");
	else
		fss->volume_name[i + 1] = 0;

	strcpy(fss->fsh_name, "NTFS");

	ERRPRINT("fs_rfsstat - EXIT\n");

	UNLOCK_VOL(ns);

	return B_NO_ERROR;
}


status_t
fs_wfsstat(fs_volume *_vol, const struct fs_info *fss, uint32 mask)
{
	nspace* ns = (nspace*)_vol->private_volume;
	status_t result = B_NO_ERROR;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	if (mask & FS_WRITE_FSINFO_NAME) {
		result = ntfs_change_label(ns->ntvol, (char*)fss->volume_name);
		goto exit;
	}

exit:
	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_walk(fs_volume *_vol, fs_vnode *_dir, const char *file, ino_t *vnid)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *baseNode = (vnode*)_dir->private_node;
	vnode *newNode = NULL;
	ntfschar *unicode = NULL;
	ntfs_inode *bi = NULL;
	status_t result = B_NO_ERROR;
	int len;

	LOCK_VOL(ns);

	ERRPRINT("fs_walk - ENTER : find for \"%s\"\n",file);

	if (ns == NULL || _dir == NULL || file == NULL || vnid == NULL) {
		result = EINVAL;
		goto exit;
	}

	if (!strcmp(file, ".")) {
		*vnid = baseNode->vnid;
		if (get_vnode(_vol, *vnid, (void**)&newNode) != 0)
			result = ENOENT;
	} else if (!strcmp(file, "..") && baseNode->vnid != FILE_root) {
		*vnid = baseNode->parent_vnid;
		if (get_vnode(_vol, *vnid, (void**)&newNode) != 0)
			result = ENOENT;
	} else {
		unicode = ntfs_calloc(MAX_PATH);
		len = ntfs_mbstoucs(file, &unicode);
		if (len < 0) {
			result = EILSEQ;
			goto exit;
		}

		bi = ntfs_inode_open(ns->ntvol, baseNode->vnid);
		if (!bi) {
			result = ENOENT;
			goto exit;
		}

		*vnid = MREF(ntfs_inode_lookup_by_name(bi, unicode, len));
		ERRPRINT("fs_walk - VNID = %d\n",*vnid);

		ntfs_inode_close(bi);
		free(unicode);

		if (*vnid == (u64)-1) {
			result = EINVAL;
			goto exit;
		}

		if (get_vnode(_vol, *vnid, (void**)&newNode) != 0)
			result = ENOENT;

		if (newNode!=NULL)
			newNode->parent_vnid = baseNode->vnid;
	}

exit:
	ERRPRINT("fs_walk - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_get_vnode_name(fs_volume *_vol, fs_vnode *_vnode, char *buffer,
	size_t bufferSize)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_vnode->private_node;
	ntfs_inode *ni = NULL;
	status_t result = B_NO_ERROR;

	char path[MAX_PATH];
	char *name;

	LOCK_VOL(ns);

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		result = ENOENT;
		goto exit;
	}

	if (utils_inode_get_name(ni, path, MAX_PATH) == 0) {
		result = EINVAL;
		goto exit;
	}

	name = strrchr(path, '/');
	name++;

	strlcpy(buffer, name, bufferSize);

exit:
	if (ni)
		ntfs_inode_close(ni);

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_read_vnode(fs_volume *_vol, ino_t vnid, fs_vnode *_node, int *_type,
	uint32 *_flags, bool reenter)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *newNode = NULL;
	ntfs_inode *ni = NULL;
	status_t result = B_NO_ERROR;

	if (!reenter)
		LOCK_VOL(ns);

	ERRPRINT("fs_read_vnode - ENTER\n");

	_node->private_node = NULL;
	_node->ops = &gNTFSVnodeOps;
	_flags = 0;

	newNode = (vnode*)ntfs_calloc(sizeof(vnode));
	if (newNode != NULL) {
		char *name = NULL;

		ni = ntfs_inode_open(ns->ntvol, vnid);
		if (ni == NULL) {
			result = ENOENT;
			goto exit;
		}

		// get the node type
		result = get_node_type(ni, _type);
		if (result != B_OK)
			goto exit;

		newNode->vnid = vnid;
		newNode->parent_vnid = ntfs_get_parent_ref(ni);

		if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)
			set_mime(newNode, ".***");
		else {
			name = (char*)malloc(MAX_PATH);
			if (name != NULL) {
				if (utils_inode_get_name(ni, name,MAX_PATH) == 1)
					set_mime(newNode, name);
				free(name);
			}
		}

		_node->private_node = newNode;
	} else
		result = ENOMEM;

exit:
	if (ni != NULL)
		ntfs_inode_close(ni);

	if (result != B_OK && newNode != NULL)
		free(newNode);

	ERRPRINT("fs_read_vnode - EXIT, result is %s\n", strerror(result));

	if (!reenter)
		UNLOCK_VOL(ns);

	return result;
}


status_t
fs_write_vnode(fs_volume *_vol, fs_vnode *_node, bool reenter)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	status_t result = B_NO_ERROR;

	if (!reenter)
		LOCK_VOL(ns);

	ERRPRINT("fs_write_vnode - ENTER (%Ld)\n", node->vnid);

	free(node);

	ERRPRINT("fs_write_vnode - EXIT\n");

	if (!reenter)
		UNLOCK_VOL(ns);

	return result;
}


status_t
fs_remove_vnode(fs_volume *_vol, fs_vnode *_node, bool reenter)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	status_t result = B_NO_ERROR;

	// TODO: this does not look right! The space if the node must be freed *here*
	if (!reenter)
		LOCK_VOL(ns);

	ERRPRINT("fs_remove_vnode - ENTER (%Ld)\n", node->vnid);

	free(node);

	ERRPRINT("fs_remove_vnode - EXIT, result is %s\n", strerror(result));

	if (!reenter)
	 	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_rstat(fs_volume *_vol, fs_vnode *_node, struct stat *stbuf)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	ntfs_inode *ni = NULL;
	ntfs_attr *na;
	status_t result = B_NO_ERROR;

	LOCK_VOL(ns);

	ERRPRINT("fs_rstat - ENTER:\n");

	if (ns == NULL || node == NULL ||stbuf == NULL) {
		result = ENOENT;
		goto exit;
	}

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		result = ENOENT;
		goto exit;
	}
	if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
		// Directory
		stbuf->st_mode = FS_DIR_MODE;
		na = ntfs_attr_open(ni, AT_INDEX_ALLOCATION, NTFS_INDEX_I30, 4);
		if (na) {
			stbuf->st_size = na->data_size;
			stbuf->st_blocks = na->allocated_size >> 9;
			ntfs_attr_close(na);
		}
		stbuf->st_nlink = 1;
	} else {
		// Regular or Interix (INTX) file
		stbuf->st_mode = FS_FILE_MODE;
		stbuf->st_size = ni->data_size;
		stbuf->st_blocks = (ni->allocated_size + 511) >> 9;
		stbuf->st_nlink = le16_to_cpu(ni->mrec->link_count);
		
		if (ni->flags & FILE_ATTR_SYSTEM) {
			na = ntfs_attr_open(ni, AT_DATA, NULL,0);
			if (!na) {
				result = ENOENT;
				goto exit;
			}
			stbuf->st_size = na->data_size;
			stbuf->st_blocks = na->allocated_size >> 9;
			// Check whether it's Interix symbolic link
			if (na->data_size <= sizeof(INTX_FILE_TYPES) + 
			    sizeof(ntfschar) * PATH_MAX && 
			    na->data_size > sizeof(INTX_FILE_TYPES)) {
				INTX_FILE *intx_file;

				intx_file = ntfs_malloc(na->data_size);
				if (!intx_file) {
					result = EINVAL;
					ntfs_attr_close(na);
					goto exit;
				}
				if (ntfs_attr_pread(na, 0, na->data_size,
						intx_file) != na->data_size) {
					result = EINVAL;
					free(intx_file);
					ntfs_attr_close(na);
					goto exit;
				}
				if (intx_file->magic == INTX_SYMBOLIC_LINK)
					stbuf->st_mode = FS_SLNK_MODE;
				free(intx_file);
			}
			ntfs_attr_close(na);
		}
		stbuf->st_mode |= 0666;
	}

	if (ns->flags & B_FS_IS_READONLY) {
		stbuf->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
	}
		
	stbuf->st_uid = 0;
	stbuf->st_gid = 0;
	stbuf->st_ino = MREF(ni->mft_no);
	stbuf->st_atime = ni->last_access_time;
	stbuf->st_ctime = ni->last_mft_change_time;
	stbuf->st_mtime = ni->last_data_change_time;	
	
exit:
	if (ni)
		ntfs_inode_close(ni);

	ERRPRINT("fs_rstat - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_wstat(fs_volume *_vol, fs_vnode *_node, const struct stat *st, uint32 mask)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	ntfs_inode *ni = NULL;
	status_t result = B_NO_ERROR;

	LOCK_VOL(ns);

	ERRPRINT("fs_wstat: ENTER\n");

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		result = ENOENT;
		goto exit;
	}

	if (mask & B_STAT_SIZE) {
		ERRPRINT("fs_wstat: setting file size to %Lx\n", st->st_size);

		if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
			result = EISDIR;
		} else {
			ntfs_attr *na = ntfs_attr_open(ni, AT_DATA, NULL, 0);
			if (!na) {
				result = EINVAL;
				goto exit;
			}

			ntfs_attr_truncate(na, st->st_size);
			ntfs_attr_close(na);

			ntfs_mark_free_space_outdated(ns);

			notify_stat_changed(ns->id, MREF(ni->mft_no), mask);
		}
	}

	if (mask & B_STAT_MODIFICATION_TIME) {
		ERRPRINT("fs_wstat: setting modification time\n");

		ni->last_access_time = st->st_atime;
		ni->last_data_change_time = st->st_mtime;
		ni->last_mft_change_time = st->st_ctime;

		notify_stat_changed(ns->id, MREF(ni->mft_no), mask);
	}

exit:
	if (ni)
		ntfs_inode_close(ni);

	ERRPRINT("fs_wstat: EXIT with (%s)\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_sync(fs_volume *_vol)
{
	nspace *ns = (nspace *)_vol->private_volume;

	LOCK_VOL(ns);

	ERRPRINT("fs_sync - ENTER\n");

	ERRPRINT("fs_sync - EXIT\n");

	UNLOCK_VOL(ns);

	return B_NO_ERROR;
}


status_t
fs_fsync(fs_volume *_vol, fs_vnode *_node)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	ntfs_inode *ni = NULL;
	status_t result = B_NO_ERROR;

	LOCK_VOL(ns);

	ERRPRINT("fs_fsync: ENTER\n");

	if (ns ==NULL || node== NULL) {
		result = ENOENT;
		goto exit;
	}

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		result = ENOENT;
		goto exit;
	}

	ntfs_inode_sync(ni);

exit:
	if (ni)
		ntfs_inode_close(ni);

	ERRPRINT("fs_fsync: EXIT\n");

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_open(fs_volume *_vol, fs_vnode *_node, int omode, void **_cookie)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	filecookie *cookie = NULL;
	ntfs_inode *ni = NULL;
	ntfs_attr *na = NULL;
	status_t result = B_NO_ERROR;

	ERRPRINT("fs_open - ENTER\n");

	LOCK_VOL(ns);

	if (node == NULL) {
		result = EINVAL;
		goto exit;
	}

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		result = errno;
		goto exit;
	}

	if (omode & O_TRUNC) {
		na = ntfs_attr_open(ni, AT_DATA, NULL, 0);
		if (na) {
			if (ntfs_attr_truncate(na, 0))
				result = errno;
		} else
			result = errno;
	}

	cookie = (filecookie*)ntfs_calloc(sizeof(filecookie));

	if (cookie != NULL) {
		cookie->omode = omode;
		*_cookie = (void*)cookie;
	} else
		result = ENOMEM;

exit:
	if (na)
		ntfs_attr_close(na);

	if (ni)
		ntfs_inode_close(ni);

	ERRPRINT("fs_open - EXIT\n");

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_create(fs_volume *_vol, fs_vnode *_dir, const char *name, int omode,
	int perms, void **_cookie, ino_t *_vnid)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *dir = (vnode*)_dir->private_node;
	filecookie *cookie = NULL;
	vnode *newNode = NULL;
	ntfs_attr *na = NULL;
	ntfs_inode *ni = NULL;
	ntfs_inode *bi = NULL;
	ntfschar *uname = NULL;
	status_t result = B_NO_ERROR;
	int unameLength;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	ERRPRINT("fs_create - ENTER: name=%s\n",name);

	if (_vol == NULL || _dir == NULL) {
		result = EINVAL;
		goto exit;
	}

	if (name == NULL || *name == '\0' || strchr(name, '/')
		|| strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		result =  EINVAL;
		goto exit;
	}

	bi = ntfs_inode_open(ns->ntvol, dir->vnid);
	if (bi == NULL) {
		result = ENOENT;
		goto exit;
	}

	if (!(bi->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
		result = EINVAL;
		goto exit;
	}

	unameLength = ntfs_mbstoucs(name, &uname);
	if (unameLength < 0) {
		result = EINVAL;
		goto exit;
	}

	cookie = (filecookie*)ntfs_calloc(sizeof(filecookie));

	if (cookie != NULL) {
		cookie->omode = omode;
	} else {
		result = ENOMEM;
		goto exit;
	}

	ni = ntfs_pathname_to_inode(ns->ntvol, bi, name);
	if (ni) {
		// file exists
		*_vnid	= MREF(ni->mft_no);
		if (omode & O_TRUNC) {
			na = ntfs_attr_open(ni, AT_DATA, NULL, 0);
			if (na) {
				if (ntfs_attr_truncate(na, 0))
					result = errno;
			} else
				result = errno;
		}
	} else {
		ni = ntfs_create(bi, uname, unameLength, S_IFREG);
		if (ni)	{
			*_vnid = MREF(ni->mft_no);

			newNode = (vnode*)ntfs_calloc(sizeof(vnode));
			if (newNode == NULL) {
			 	result = ENOMEM;
			 	goto exit;
			}

			newNode->vnid = *_vnid;
			newNode->parent_vnid = MREF(bi->mft_no);
			set_mime(newNode, name);

			result = B_NO_ERROR;
			result = publish_vnode(_vol, *_vnid, (void*)newNode,&gNTFSVnodeOps,
				S_IFREG, 0);

			ntfs_mark_free_space_outdated(ns);

			notify_entry_created(ns->id, MREF(bi->mft_no), name, *_vnid);
		} else
			result = errno;
	}

	free(uname);

exit:
	if (result >= B_OK) {
		*_cookie = cookie;
	} else
		free(cookie);

	if (na)
		ntfs_attr_close(na);
	if (ni)
		ntfs_inode_close(ni);
	if (bi)
		ntfs_inode_close(bi);

	ERRPRINT("fs_create - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_read(fs_volume *_vol, fs_vnode *_dir, void *_cookie, off_t offset, void *buf,
	size_t *len)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_dir->private_node;
	ntfs_inode *ni = NULL;
	ntfs_attr *na = NULL;
	size_t  size = *len;
	int total = 0;
	status_t result = B_OK;

	LOCK_VOL(ns);

	ERRPRINT("fs_read - ENTER\n");

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		*len = 0;
		result = ENOENT;
		goto exit2;
	}

	if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
		ERRPRINT(("ntfs_read called on directory.\n"));
		*len = 0;
		result = EISDIR;
		goto exit2;
	}

	if (offset < 0) {
		*len = 0;
		result = EINVAL;
		goto exit2;
	}

	na = ntfs_attr_open(ni, AT_DATA, NULL, 0);
	if (!na) {
		*len = 0;
		result = EINVAL;
		goto exit2;
	}
	if (offset + size > na->data_size)
		size = na->data_size - offset;

	while (size) {
		off_t bytesRead = ntfs_attr_pread(na, offset, size, buf);
		if (bytesRead < (s64)size) {
			ntfs_log_error("ntfs_attr_pread returned less bytes than "
				"requested.\n");
		}
		if (bytesRead <= 0) {
			*len = 0;
			result = EINVAL;
			goto exit;
		}
		size -= bytesRead;
		offset += bytesRead;
		total += bytesRead;
	}

	*len = total;

exit:
	if (na)
		ntfs_attr_close(na);

exit2:
	if (ni)
		ntfs_inode_close(ni);

	UNLOCK_VOL(ns);

	ERRPRINT("fs_read - EXIT, result is %s\n", strerror(result));

	return result;
}


status_t
fs_write(fs_volume *_vol, fs_vnode *_dir, void *_cookie, off_t offset,
	const void *buf, size_t *len)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_dir->private_node;
	filecookie *cookie = (filecookie*)_cookie;
	ntfs_inode *ni = NULL;
	ntfs_attr *na = NULL;
	int total = 0;
	size_t size = *len;
	status_t result = B_OK;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	ERRPRINT("fs_write - ENTER, offset=%d, len=%d\n", (int)offset, (int)(*len));

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		*len = 0;
		result = ENOENT;
		goto exit2;
	}

	if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
		ERRPRINT(("fs_write - called on directory.\n"));
		*len = 0;
		result = EISDIR;
		goto exit2;
	}

	if (offset < 0) {
		ERRPRINT(("fs_write - offset < 0.\n"));
		*len = 0;
		result = EINVAL;
		goto exit2;
	}

	na = ntfs_attr_open(ni, AT_DATA, NULL, 0);
	if (!na) {
		ERRPRINT(("fs_write - ntfs_attr_open()==NULL\n"));
		*len = 0;
		result = EINVAL;
		goto exit2;
	}

	if (cookie->omode & O_APPEND)
		offset = na->data_size;

	if (offset + size > na->data_size) {
		ntfs_mark_free_space_outdated(ns);
		if (ntfs_attr_truncate(na, offset + size))
			size = na->data_size - offset;
		else
			notify_stat_changed(ns->id, MREF(ni->mft_no), B_STAT_SIZE);
	}

	while (size) {
		off_t bytesWritten = ntfs_attr_pwrite(na, offset, size, buf);
		if (bytesWritten < (s64)size)
			ERRPRINT("fs_write - ntfs_attr_pwrite returned less bytes than requested.\n");
		if (bytesWritten <= 0) {
			ERRPRINT(("fs_write - ntfs_attr_pwrite()<=0\n"));
			*len = 0;
			result = EINVAL;
			goto exit;
		}
		size -= bytesWritten;
		offset += bytesWritten;
		total += bytesWritten;
	}

	*len = total;
	ERRPRINT(("fs_write - OK\n"));

exit:
	if (na)
		ntfs_attr_close(na);
exit2:
	if (ni)
		ntfs_inode_close(ni);

	ERRPRINT("fs_write - EXIT, result is %s, writed %d bytes\n",
		strerror(result), (int)(*len));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_close(fs_volume *_vol, fs_vnode *_node, void *cookie)
{
	ERRPRINT("fs_close - ENTER\n");

	ERRPRINT("fs_close - EXIT\n");
	return B_NO_ERROR;
}


status_t
fs_free_cookie(fs_volume *_vol, fs_vnode *_node, void *cookie)
{
	ERRPRINT("fs_free_cookie - ENTER\n");

	free(cookie);

	ERRPRINT("fs_free_cookie - EXIT\n");
	return B_NO_ERROR;
}


status_t
fs_access(fs_volume *_vol, fs_vnode *_node, int mode)
{
	ERRPRINT("fs_access - ENTER\n");

	ERRPRINT("fs_access - EXIT\n");
	return B_NO_ERROR;
}


status_t
fs_readlink(fs_volume *_vol, fs_vnode *_node, char *buffer, size_t *bufferSize)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *node = (vnode*)_node->private_node;
	ntfs_attr *na = NULL;
	INTX_FILE *intxFile = NULL;
	ntfs_inode *ni = NULL;
	char *tempBuffer = NULL;
	status_t result = B_NO_ERROR;
	int l = 0;

	LOCK_VOL(ns);

	ERRPRINT("fs_readlink - ENTER\n");

	if (ns == NULL || node == NULL || buffer == NULL || bufferSize == NULL) {
		result = EINVAL;
		goto exit;
	}

	ni = ntfs_inode_open(ns->ntvol, node->vnid);
	if (ni == NULL) {
		result = ENOENT;
		goto exit;
	}

	/* Sanity checks. */
	if (!(ni->flags & FILE_ATTR_SYSTEM)) {
		result = EINVAL;
		goto exit;
	}
	na = ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0);
	if (!na) {
		result = errno;
		goto exit;
	}
	if (na->data_size <= sizeof(INTX_FILE_TYPES)) {
		result = EINVAL;
		goto exit;
	}
	if (na->data_size > sizeof(INTX_FILE_TYPES) + sizeof(ntfschar) * MAX_PATH) {
		result = ENAMETOOLONG;
		goto exit;
	}
	/* Receive file content. */
	intxFile = ntfs_malloc(na->data_size);
	if (!intxFile) {
		result = errno;
		goto exit;
	}
	if (ntfs_attr_pread(na, 0, na->data_size, intxFile) != na->data_size) {
		result = errno;
		goto exit;
	}
	/* Sanity check. */
	if (intxFile->magic != INTX_SYMBOLIC_LINK) {
		result = EINVAL;
		goto exit;
	}
	/* Convert link from unicode to local encoding. */
	l = ntfs_ucstombs(intxFile->target,
		(na->data_size - offsetof(INTX_FILE, target)) / sizeof(ntfschar),
		&tempBuffer, 0);

	if (l < 0) {
		result = errno;
		goto exit;
	}
	if (tempBuffer == NULL) {
		result = B_NO_MEMORY;
		goto exit;
	}

	ERRPRINT("fs_readlink - LINK:[%s]\n", buffer);

	strlcpy(buffer, tempBuffer, *bufferSize);
	free(tempBuffer);

	*bufferSize = l + 1;

	result = B_NO_ERROR;

exit:
	free(intxFile);
	if (na)
		ntfs_attr_close(na);
	if (ni)
		ntfs_inode_close(ni);

	ERRPRINT("fs_readlink - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_create_symlink(fs_volume *_vol, fs_vnode *_dir, const char *name,
	const char *target, int mode)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *dir = (vnode*)_dir->private_node;
	ntfs_inode *sym = NULL;
	ntfs_inode *bi = NULL;
	vnode *symnode = NULL;
	ntfschar *uname = NULL;
	ntfschar *utarget = NULL;
	int unameLength;
	int utargetLength;
	status_t result = B_NO_ERROR;
	int fmode;

	LOCK_VOL(ns);

	ERRPRINT("fs_symlink - ENTER, name=%s, path=%s\n",name, target);

	if (ns == NULL || dir == NULL || name == NULL || target == NULL) {
		result = EINVAL;
		goto exit;
	}

	bi = ntfs_inode_open(ns->ntvol, dir->vnid);
	if (bi == NULL) {
		result = ENOENT;
		goto exit;
	}

	unameLength = ntfs_mbstoucs(name, &uname);
	if (unameLength < 0) {
		result = EINVAL;
		goto exit;
	}

	utargetLength = ntfs_mbstoucs(target, &utarget);
	if (utargetLength < 0) {
		result = EINVAL;
		goto exit;
	}

	sym = ntfs_create_symlink(bi, uname, unameLength, utarget, utargetLength);
	if (sym == NULL) {
		result = EINVAL;
		goto exit;
	}

	symnode = (vnode*)ntfs_calloc(sizeof(vnode));
	if (symnode == NULL) {
	 	result = ENOMEM;
	 	goto exit;
	}

	symnode->vnid = MREF(sym->mft_no);
	symnode->parent_vnid = MREF(bi->mft_no);

	if (sym->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
		set_mime(symnode, ".***");
		fmode = FS_DIR_MODE;
	} else {
		set_mime(symnode, name);
		fmode = FS_FILE_MODE;
	}

	result = publish_vnode(_vol, MREF(sym->mft_no), symnode, &gNTFSVnodeOps,
		S_IFLNK | fmode, 0);
	if (result != 0) {
		ERRPRINT("fs_symlink - new_vnode failed for vnid %Ld: %s\n",
			MREF(sym->mft_no), strerror(result));
	}

	put_vnode(_vol, MREF(sym->mft_no));

	notify_entry_created(ns->id, MREF( bi->mft_no ), name, MREF(sym->mft_no));

exit:
	if (sym)
		ntfs_inode_close(sym);
	if (bi)
		ntfs_inode_close(bi);

	ERRPRINT("fs_symlink - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_mkdir(fs_volume *_vol, fs_vnode *_dir, const char *name,	int perms)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *dir = (vnode*)_dir->private_node;
	vnode *newNode =NULL;
	ntfschar *uname = NULL;
	int unameLength;
	ntfs_inode *ni = NULL;
	ntfs_inode *bi = NULL;
	status_t result = B_NO_ERROR;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	ERRPRINT("fs_mkdir - ENTER: name=%s\n",name);

	if (_vol == NULL || _dir == NULL || name == NULL) {
	 	result = EINVAL;
	 	goto exit;
	}

	bi = ntfs_inode_open(ns->ntvol, dir->vnid);
	if (bi == NULL) {
		result = ENOENT;
		goto exit;
	}

	if (!(bi->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
		result = EINVAL;
		goto exit;
	}

	unameLength = ntfs_mbstoucs(name, &uname);
	if (unameLength < 0) {
		result = EINVAL;
		goto exit;
	}

	ni = ntfs_create(bi, uname, unameLength, S_IFDIR);
	if (ni)	{
		ino_t vnid = MREF(ni->mft_no);

		newNode = (vnode*)ntfs_calloc(sizeof(vnode));
		if (newNode == NULL) {
		 	result = ENOMEM;
		 	ntfs_inode_close(ni);
		 	goto exit;
		}

		newNode->vnid = vnid;
		newNode->parent_vnid = MREF(bi->mft_no);
		set_mime(newNode, ".***");

		result = publish_vnode(_vol, vnid, (void*)newNode, &gNTFSVnodeOps,
			S_IFDIR, 0);

		put_vnode(_vol, MREF(ni->mft_no));

		ntfs_mark_free_space_outdated(ns);

		notify_entry_created(ns->id, MREF(bi->mft_no), name, vnid);
	} else
		result = errno;

exit:
	if (ni)
		ntfs_inode_close(ni);
	if (bi)
		ntfs_inode_close(bi);

	ERRPRINT("fs_mkdir - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_rename(fs_volume *_vol, fs_vnode *_odir, const char *oldname, fs_vnode *_ndir,
	const char *newname)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *odir = (vnode*)_odir->private_node;
	vnode *ndir = (vnode*)_ndir->private_node;

	vnode *onode = NULL;
	vnode *nnode = NULL;

	ino_t ovnid, nvnid;

	ntfs_inode *oi = NULL;
	ntfs_inode *ndi = NULL;
	ntfs_inode *odi = NULL;

	ntfschar *unewname = NULL;
	ntfschar *uoldname = NULL;
	int unewnameLength;
	int uoldnameLength;

	status_t result = B_NO_ERROR;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	ERRPRINT("fs_rename - oldname:%s newname:%s\n", oldname, newname);

	if (_vol == NULL || _odir == NULL || _ndir == NULL
		|| oldname == NULL || *oldname == '\0'
		|| newname == NULL || *newname == '\0'
		|| !strcmp(oldname, ".") || !strcmp(oldname, "..")
		|| !strcmp(newname, ".") || !strcmp(newname, "..")
		|| strchr(newname, '/') != NULL) {
		result = EINVAL;
		goto exit;
	}

	// stupid renaming check
	if (odir == ndir && !strcmp(oldname, newname))
		goto exit;

	// convert names from utf8 to unicode string
	unewnameLength = ntfs_mbstoucs(newname, &unewname);
	if (unewnameLength < 0) {
		result = EINVAL;
		goto exit;
	}

	uoldnameLength = ntfs_mbstoucs(oldname, &uoldname);
	if (uoldnameLength < 0) {
		result = EINVAL;
		goto exit;
	}

	// open source directory inode
	odi = ntfs_inode_open(ns->ntvol, odir->vnid);
	if (odi == NULL) {
		result = ENOENT;
		goto exit;
	}

	ovnid = MREF(ntfs_inode_lookup_by_name(odi, uoldname, uoldnameLength));
	if (ovnid == (u64) -1) {
		result = EINVAL;
		goto exit;
	}

	result = get_vnode(_vol, ovnid, (void**)&onode);
	if (result != B_NO_ERROR)
		goto exit;


	if (odir != ndir) {
		// moving
		ndi = ntfs_inode_open(ns->ntvol, ndir->vnid);
		if (ndi != NULL) {
			nvnid = MREF(ntfs_inode_lookup_by_name(ndi, unewname,
				unewnameLength));
			if (nvnid != (u64) -1)
				get_vnode(_vol, nvnid, (void**)&nnode);
		}

		if (nnode != NULL) {
			result = EINVAL;
			put_vnode(_vol, nnode->vnid);
			goto exit;
		}

		oi = ntfs_inode_open(ns->ntvol, onode->vnid);
		if (oi == NULL) {
			result = EINVAL;
			goto exit;
		}

		if (ntfs_link(oi, ndi, unewname, unewnameLength)) {
 			ntfs_inode_close(oi);
			result = EINVAL;
			goto exit;
		}

		if (oi->mrec->flags & MFT_RECORD_IS_DIRECTORY)
			set_mime(onode, ".***");
		else
			set_mime(onode, newname);

		ntfs_inode_close(oi);

		oi = ntfs_inode_open(ns->ntvol, onode->vnid);
		if (oi == NULL) {
			result = EINVAL;
			goto exit;
		}

		onode->parent_vnid = MREF(ndi->mft_no);

		notify_entry_moved(ns->id, MREF(odi->mft_no), oldname, MREF(ndi->mft_no),
			newname, onode->vnid);

		ntfs_delete(oi, odi, uoldname, uoldnameLength);
		oi = odi = NULL;
			/* ntfs_delete() always closes ni and dir_ni */

		put_vnode(_vol, onode->vnid);
	} else {
		// renaming

		nvnid = MREF(ntfs_inode_lookup_by_name(odi, unewname, unewnameLength));
		if (nvnid != (u64) -1)
			get_vnode(_vol, nvnid, (void**)&nnode);

		if (nnode != NULL) {
			result = EINVAL;
			put_vnode(_vol, nnode->vnid);
			goto exit;
		}

		oi = ntfs_inode_open(ns->ntvol, onode->vnid);
		if (oi == NULL) {
			result = EINVAL;
			goto exit;
		}

		if (ntfs_link(oi, odi, unewname, unewnameLength))  {
 			ntfs_inode_close(oi);
			result = EINVAL;
			goto exit;
		}

		if (oi->mrec->flags & MFT_RECORD_IS_DIRECTORY)
			set_mime(onode, ".***");
		else
			set_mime(onode, newname);

		ntfs_inode_close(oi);

		oi = ntfs_inode_open(ns->ntvol, onode->vnid);
		if (oi == NULL) {
			result = EINVAL;
			goto exit;
		}

		notify_entry_moved(ns->id, MREF(odi->mft_no), oldname, MREF(odi->mft_no),
			newname, onode->vnid);
		put_vnode(_vol, onode->vnid);

		ntfs_delete(oi, odi, uoldname, uoldnameLength);
		oi = odi = NULL;
			/* ntfs_delete() always closes ni and dir_ni */
	}

exit:
	free(unewname);
	free(uoldname);

	if (odi)
		ntfs_inode_close(odi);
	if (ndi)
		ntfs_inode_close(ndi);

	ERRPRINT("fs_rename - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


static status_t
do_unlink(fs_volume *_vol, vnode *dir, const char *name, bool isdir)
{
	nspace *vol = (nspace*)_vol->private_volume;
	ino_t vnid;
	vnode *node = NULL;
	ntfs_inode *ni = NULL;
	ntfs_inode *bi = NULL;
	ntfschar *uname = NULL;
	int unameLength;
	status_t result = B_NO_ERROR;

	unameLength = ntfs_mbstoucs(name, &uname);
	if (unameLength < 0) {
		result = EINVAL;
		goto exit1;
	}

	bi = ntfs_inode_open(vol->ntvol, dir->vnid);
	if (bi == NULL) {
		result = ENOENT;
		goto exit1;
	}

	vnid = MREF(ntfs_inode_lookup_by_name(bi, uname, unameLength));

	if ( vnid == (u64) -1 || vnid == FILE_root)	{
		result = EINVAL;
		goto exit1;
	}

	result = get_vnode(_vol, vnid, (void**)&node);

	if (result != B_NO_ERROR || node==NULL) {
		result = ENOENT;
		goto exit1;
	}

	ni = ntfs_inode_open(vol->ntvol, node->vnid);
	if (ni == NULL) {
		result = ENOENT;
		goto exit2;
	}

	if (isdir) {
		if (!(ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
			result = ENOTDIR;
			goto exit2;
		}
		if (ntfs_check_empty_dir(ni)<0)	{
			result = ENOTEMPTY;
			goto exit2;
		}
	} else if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) {
		result = EISDIR;
		goto exit2;
	}

	// TODO: the file must not be deleted here, only unlinked!
	if (ntfs_delete(ni, bi, uname, unameLength))
	 	result = errno;

	ni = bi = NULL;

	node->parent_vnid = dir->vnid;

	notify_entry_removed(vol->id, dir->vnid, name, vnid);

	result = remove_vnode(_vol, vnid);

exit2:
	put_vnode(_vol, vnid);
exit1:
	free(uname);

	if (ni)
		ntfs_inode_close(ni);
	if (bi)
		ntfs_inode_close(bi);

	return result;
}


status_t
fs_rmdir(fs_volume *_vol, fs_vnode *_dir, const char *name)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *dir = (vnode*)_dir->private_node;
	status_t result = B_NO_ERROR;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	ERRPRINT("fs_rmdir  - ENTER:  name %s\n", name==NULL?"NULL":name);

	if (ns == NULL || dir == NULL || name == NULL) {
		result = EINVAL;
		goto exit1;
	}

	if (strcmp(name, ".") == 0) {
		result = EPERM;
		goto exit1;
	}

	if (strcmp(name, "..") == 0) {
		result = EPERM;
		goto exit1;
	}

	result = do_unlink(_vol, dir, name, true);

	// TODO: space must not be freed here, but in fs_remove_vnode()!!!
	ntfs_mark_free_space_outdated(ns);

exit1:
	ERRPRINT("fs_rmdir - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}


status_t
fs_unlink(fs_volume *_vol, fs_vnode *_dir, const char *name)
{
	nspace *ns = (nspace*)_vol->private_volume;
	vnode *dir = (vnode*)_dir->private_node;
	status_t result = B_NO_ERROR;

	if (ns->flags & B_FS_IS_READONLY) {
		ERRPRINT("ntfs is read-only\n");
		return EROFS;
	}

	LOCK_VOL(ns);

	ERRPRINT("fs_unlink  - ENTER:  name %s\n", name==NULL?"NULL":name);

	if (ns == NULL || dir == NULL || name == NULL) {
		result = EINVAL;
		goto exit;
	}

	if (strcmp(name, ".") == 0) {
		result = EPERM;
		goto exit;
	}

	if (strcmp(name, "..") == 0) {
		result = EPERM;
		goto exit;
	}

	result = do_unlink(_vol, dir, name, false);

	// TODO: space must not be freed here, but in fs_remove_vnode()!!!
	ntfs_mark_free_space_outdated(ns);

exit:
	ERRPRINT("fs_unlink - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);

	return result;
}

