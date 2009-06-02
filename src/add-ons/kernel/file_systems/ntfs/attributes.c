/*
	Copyright 1999-2001, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
*/
/* attributes.c
 * handles mime type information for ntfs
 * gets/sets mime information in vnode
 */


#define MIME_STRING_TYPE 'MIMS'

#include <SupportDefs.h>
#include <KernelExport.h>

#include <dirent.h>
#include <fs_attr.h>
#include <string.h>
#include <malloc.h>

#include "ntfs.h"
#include "attributes.h"
#include "mime_table.h"

int32 kBeOSTypeCookie = 0x1234;

status_t set_mime(vnode *node, const char *filename)
{
	struct ext_mime *p;
	int32 namelen, ext_len;

	ERRPRINT("set_mime - for [%s]\n", filename);

	node->mime = NULL;

	namelen = strlen(filename);

	for (p=mimes;p->extension;p++) {
		ext_len = strlen(p->extension);

		if (namelen <= ext_len)
			continue;

		if (filename[namelen-ext_len-1] != '.')
			continue;
		
		if (!strcasecmp(filename + namelen - ext_len, p->extension))
			break;
	}

	node->mime = p->mime;

	return B_NO_ERROR;
}


status_t 
fs_open_attrib_dir(fs_volume *_vol, fs_vnode *_node, void **_cookie)
{
	nspace *ns = (nspace *)_vol->private_volume;
		
	int	result = B_NO_ERROR;

	ERRPRINT("fs_open_attrdir - ENTER\n");

	LOCK_VOL(ns);

	if ((*_cookie = malloc(sizeof(uint32))) == NULL) {
		result = ENOMEM;
		goto	exit;
	}
	
	*(int32 *)(*_cookie) = 0;
	
exit:

	ERRPRINT("fs_open_attrdir - EXIT, result is %s\n", strerror(result));
	
	UNLOCK_VOL(ns);
	
	return result;
}

status_t 
fs_close_attrib_dir(fs_volume *_vol, fs_vnode *_node, void *_cookie)
{
	nspace *ns = (nspace *)_vol->private_volume;
	
	ERRPRINT("fs_close_attrdir - ENTER\n");

	LOCK_VOL(ns);

	*(int32 *)_cookie = 1;
	
	ERRPRINT("fs_close_attrdir - EXIT\n");
	
	UNLOCK_VOL(ns);
	
	return B_NO_ERROR;
}

status_t 
fs_free_attrib_dir_cookie(fs_volume *_vol, fs_vnode *_node, void *_cookie)
{
	nspace *ns = (nspace *)_vol->private_volume;

	int	result = B_NO_ERROR;

	LOCK_VOL(ns);
		
	ERRPRINT("fs_free_attrib_dir_cookie - ENTER\n");

	if (_cookie == NULL) {
		ERRPRINT("fs_free_attrib_dir_cookie - error: called with null cookie\n");
		result =  EINVAL;
		goto	exit;
	}
	
	*(int32 *)_cookie = 0x87654321;
	free(_cookie);

exit:

	ERRPRINT("fs_free_attrib_dir_cookie - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);
	
	return result;
}

status_t 
fs_rewind_attrib_dir(fs_volume *_vol, fs_vnode *_node, void *_cookie)
{
	nspace *ns = (nspace *)_vol->private_volume;

	int	result = B_NO_ERROR;

	LOCK_VOL(ns);
	
	ERRPRINT("fs_rewind_attrcookie  - ENTER\n");

	if (_cookie == NULL) {
		ERRPRINT("fs_rewind_attrcookie - error: fs_rewind_attrcookie called with null cookie\n");
		result =  EINVAL;
		goto	exit;
	}
	
	*(uint32 *)_cookie = 0;

exit:

	ERRPRINT("fs_rewind_attrcookie - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);
	
	return result;
}


status_t 
fs_read_attrib_dir(fs_volume *_vol, fs_vnode *_node, void *_cookie, struct dirent *entry, size_t bufsize, uint32 *num)
{
	nspace *ns = (nspace *)_vol->private_volume;
	vnode *node = (vnode *)_node->private_node;

	int32 *cookie = (int32 *)_cookie;

	LOCK_VOL(ns);

	ERRPRINT("fs_read_attrdir - ENTER\n");

	*num = 0;

	if ((*cookie == 0) && (node->mime)) {
		*num = 1;
		
		entry->d_ino = node->vnid;
		entry->d_dev = ns->id;
		entry->d_reclen = 10;
		strcpy(entry->d_name, "BEOS:TYPE");
	}

	*cookie = 1;

	ERRPRINT("fs_read_attrdir - EXIT\n");

	UNLOCK_VOL(ns);
	
	return B_NO_ERROR;
}

status_t
fs_open_attrib(fs_volume *_vol, fs_vnode *_node, const char *name, int openMode, void **_cookie)
{
	nspace *ns = (nspace *)_vol->private_volume;
	vnode *node = (vnode *)_node->private_node;	
	int	result = B_NO_ERROR;

	LOCK_VOL(ns);
	
	ERRPRINT("fs_open_attrib - ENTER\n");

	if (strcmp(name, "BEOS:TYPE")) {
		result = ENOENT;
		goto	exit;
	}
	
	if (node->mime == NULL)	{
		result = ENOENT;
		goto	exit;
	}

	*_cookie = &kBeOSTypeCookie;
	
exit:

	ERRPRINT("fs_open_attrib - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);
		
	return result;
}


status_t
fs_close_attrib(fs_volume *_vol, fs_vnode *_node, void *cookie)
{
	return B_NO_ERROR;
}


status_t
fs_free_attrib_cookie(fs_volume *_vol, fs_vnode *_node, void *cookie)
{
	return B_NO_ERROR;
}


status_t 
fs_read_attrib_stat(fs_volume *_vol, fs_vnode *_node, void *_cookie,struct stat *stat)
{
	nspace *ns = (nspace *)_vol->private_volume;
	vnode *node = (vnode *)_node->private_node;
	int	result = B_NO_ERROR;

	LOCK_VOL(ns);

	ERRPRINT("fs_read_attr_stat - ENTER\n");

	if (_cookie != &kBeOSTypeCookie) {
		result = ENOENT;
		goto	exit;
	}

	if (node->mime == NULL) {
		result = ENOENT;
		goto	exit;
	}
	
	stat->st_type = MIME_STRING_TYPE;
	stat->st_size = strlen(node->mime) + 1;

exit:

	ERRPRINT("fs_read_attrib_stat - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);
	
	return B_NO_ERROR;
}


status_t 
fs_read_attrib(fs_volume *_vol, fs_vnode *_node, void *_cookie, off_t pos,void *buffer, size_t *_length)
{
	nspace *ns = (nspace *)_vol->private_volume;
	vnode *node = (vnode *)_node->private_node;

	int	result = B_NO_ERROR;

	LOCK_VOL(ns);

	ERRPRINT("fs_read_attr - ENTER\n");

	if (_cookie != &kBeOSTypeCookie) {
		result = ENOENT;
		goto	exit;
	}
			
	if (node->mime == NULL) {
		result = ENOENT;
		goto	exit;
	}
	
	if ((pos < 0) || (pos > strlen(node->mime))) {
		result = EINVAL;
		goto	exit;
	}

	strncpy(buffer, node->mime + pos, *_length - 1);
	((char *)buffer)[*_length - 1] = 0;
	*_length = strlen(buffer) + 1;
	
exit:
	
	ERRPRINT("fs_read_attr - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);
	
	return result;
}


status_t
fs_write_attrib(fs_volume *_vol, fs_vnode *_node, void *_cookie,off_t pos, const void *buffer, size_t *_length)
{
	nspace *ns = (nspace *)_vol->private_volume;

	int	result = B_NO_ERROR;
	
	LOCK_VOL(ns);
	
	ERRPRINT("fs_write_attr - ENTER\n");

	*_length = 0;

	if (_cookie != &kBeOSTypeCookie) {
		result = ENOSYS;
	}
	
	ERRPRINT("fs_write_attrib - EXIT, result is %s\n", strerror(result));

	UNLOCK_VOL(ns);
		
	return result;
}
