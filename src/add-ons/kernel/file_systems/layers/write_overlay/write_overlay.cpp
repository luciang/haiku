/*
 * Copyright 2009, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 */

#include <new>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>

#include <util/kernel_cpp.h>

#include <fs_cache.h>
#include <fs_info.h>
#include <fs_interface.h>
#include <io_requests.h>

#include <debug.h>
#include <KernelExport.h>
#include <NodeMonitor.h>

#include "IORequest.h"


//#define TRACE_OVERLAY
#ifdef TRACE_OVERLAY
#define TRACE(x...)			dprintf("write_overlay: " x)
#define TRACE_VOLUME(x...)	dprintf("write_overlay: " x)
#define TRACE_ALWAYS(x...)	dprintf("write_overlay: " x)
#else
#define TRACE(x...)			/* nothing */
#define TRACE_VOLUME(x...)	/* nothing */
#define TRACE_ALWAYS(x...)	dprintf("write_overlay: " x)
#endif


namespace write_overlay {

status_t publish_overlay_vnode(fs_volume *volume, ino_t inodeNumber,
	void *privateNode, int type);

class OverlayInode;

struct open_cookie {
	int				open_mode;
	void *			super_cookie;
};


struct open_dir_cookie {
	uint32			index;
};


struct overlay_dirent {
	ino_t			inode_number;
	char *			name;

	void			remove_and_dispose(fs_volume *volume, ino_t directoryInode)
					{
						notify_entry_removed(volume->id, directoryInode,
							name, inode_number);
						remove_vnode(volume, inode_number);
						free(name);
						free(this);
					}
};


struct write_buffer {
	write_buffer *	next;
	off_t			position;
	size_t			length;
	uint8			buffer[1];
};


class OverlayVolume {
public:
							OverlayVolume(fs_volume *volume);
							~OverlayVolume();

		fs_volume *			Volume() { return fVolume; }
		fs_volume *			SuperVolume() { return fVolume->super_volume; }

		ino_t				BuildInodeNumber() { return fCurrentInodeNumber++; }

private:
		fs_volume *			fVolume;
		ino_t				fCurrentInodeNumber;
};


class OverlayInode {
public:
							OverlayInode(OverlayVolume *volume,
								fs_vnode *superVnode, ino_t inodeNumber,
								OverlayInode *parentDir = NULL,
								const char *name = NULL, mode_t mode = 0);
							~OverlayInode();

		status_t			InitCheck();

		bool				IsVirtual() { return fIsVirtual; }
		bool				IsModified() { return fIsModified; }

		fs_volume *			Volume() { return fVolume->Volume(); }
		fs_volume *			SuperVolume() { return fVolume->SuperVolume(); }
		fs_vnode *			SuperVnode() { return &fSuperVnode; }
		ino_t				InodeNumber() { return fInodeNumber; }

		void				SetModified();
		void				CreateCache();

		void				SetParentDir(OverlayInode *parentDir);
		OverlayInode *		ParentDir() { return fParentDir; }

		bool				IsNonEmptyDirectory();

		status_t			Lookup(const char *name, ino_t *inodeNumber);
		void				SetName(const char *name);
		status_t			GetName(char *buffer, size_t bufferSize);

		status_t			ReadStat(struct stat *stat);
		status_t			WriteStat(const struct stat *stat, uint32 statMask);

		status_t			Create(const char *name, int openMode, int perms,
								void **cookie, ino_t *newInodeNumber);
		status_t			Open(int openMode, void **cookie);
		status_t			Close(void *cookie);
		status_t			FreeCookie(void *cookie);
		status_t			Read(void *cookie, off_t position, void *buffer,
								size_t *length, bool readPages,
								IORequest *ioRequest);
		status_t			Write(void *cookie, off_t position,
								const void *buffer, size_t length,
								IORequest *request);

		status_t			SynchronousIO(void *cookie, IORequest *request);

		status_t			SetFlags(void *cookie, int flags);

		status_t			CreateDir(const char *name, int perms);
		status_t			RemoveDir(const char *name);
		status_t			OpenDir(void **cookie);
		status_t			CloseDir(void *cookie);
		status_t			FreeDirCookie(void *cookie);
		status_t			ReadDir(void *cookie, struct dirent *buffer,
								size_t bufferSize, uint32 *num);
		status_t			RewindDir(void *cookie);

		status_t			CreateSymlink(const char *name, const char *path,
								int mode);
		status_t			ReadSymlink(char *buffer, size_t *bufferSize);

		status_t			AddEntry(overlay_dirent *entry);
		status_t			RemoveEntry(const char *name,
								overlay_dirent **entry);

private:
		void				_TrimBuffers();

		status_t			_PopulateDirents();
		status_t			_CreateCommon(const char *name, int type, int perms,
								ino_t *newInodeNumber, OverlayInode **node);

		OverlayVolume *		fVolume;
		OverlayInode *		fParentDir;
		const char *		fName;
		fs_vnode			fSuperVnode;
		ino_t				fInodeNumber;
		write_buffer *		fWriteBuffers;
		off_t				fOriginalNodeLength;
		overlay_dirent **	fDirents;
		uint32				fDirentCount;
		struct stat			fStat;
		bool				fHasStat;
		bool				fHasDirents;
		bool				fIsVirtual;
		bool				fIsModified;
		void *				fFileCache;
};


//	#pragma mark OverlayVolume


OverlayVolume::OverlayVolume(fs_volume *volume)
	:	fVolume(volume),
		fCurrentInodeNumber((ino_t)1 << 60)
{
}


OverlayVolume::~OverlayVolume()
{
}


//	#pragma mark OverlayInode


OverlayInode::OverlayInode(OverlayVolume *volume, fs_vnode *superVnode,
	ino_t inodeNumber, OverlayInode *parentDir, const char *name, mode_t mode)
	:	fVolume(volume),
		fParentDir(parentDir),
		fName(name),
		fInodeNumber(inodeNumber),
		fWriteBuffers(NULL),
		fOriginalNodeLength(-1),
		fDirents(NULL),
		fDirentCount(0),
		fHasStat(false),
		fHasDirents(false),
		fIsVirtual(superVnode == NULL),
		fIsModified(false),
		fFileCache(NULL)
{
	TRACE("inode created %lld\n", fInodeNumber);

	if (superVnode != NULL)
		fSuperVnode = *superVnode;
	else {
		fStat.st_dev = SuperVolume()->id;
		fStat.st_ino = fInodeNumber;
		fStat.st_mode = mode;
		fStat.st_nlink = 1;
		fStat.st_uid = 0;
		fStat.st_gid = 0;
		fStat.st_size = 0;
		fStat.st_rdev = 0;
		fStat.st_blksize = 1024;
		fStat.st_atime = fStat.st_mtime = fStat.st_ctime = fStat.st_crtime
			= time(NULL);
		fStat.st_type = 0;
		fHasStat = true;
	}
}


OverlayInode::~OverlayInode()
{
	TRACE("inode destroyed %lld\n", fInodeNumber);
	if (fFileCache != NULL)
		file_cache_delete(fFileCache);

	write_buffer *element = fWriteBuffers;
	while (element) {
		write_buffer *next = element->next;
		free(element);
		element = next;
	}

	for (uint32 i = 0; i < fDirentCount; i++)
		free(fDirents[i]);
	free(fDirents);
}


status_t
OverlayInode::InitCheck()
{
	return B_OK;
}


void
OverlayInode::SetModified()
{
	// we must ensure that a modified node never get's put, as we cannot get it
	// from the underlying filesystem, so we get an additional reference here
	// and deliberately leak it
	// TODO: what about non-force unmounting then?
	void *unused = NULL;
	get_vnode(Volume(), fInodeNumber, &unused);
	fIsModified = true;
}


void
OverlayInode::CreateCache()
{
	if (!S_ISDIR(fStat.st_mode) && !S_ISLNK(fStat.st_mode)) {
		fFileCache = file_cache_create(fStat.st_dev, fStat.st_ino, 0);
		if (fFileCache != NULL)
			file_cache_disable(fFileCache);
	}
}


void
OverlayInode::SetParentDir(OverlayInode *parentDir)
{
	fParentDir = parentDir;
	if (fHasDirents && fDirentCount >= 2)
		fDirents[1]->inode_number = parentDir->InodeNumber();
}


bool
OverlayInode::IsNonEmptyDirectory()
{
	if (!fHasStat)
		ReadStat(NULL);

	if (!S_ISDIR(fStat.st_mode))
		return false;

	if (!fHasDirents)
		_PopulateDirents();

	return fDirentCount > 2; // accounting for "." and ".." entries
}


status_t
OverlayInode::Lookup(const char *name, ino_t *inodeNumber)
{
	if (!fHasDirents)
		_PopulateDirents();

	for (uint32 i = 0; i < fDirentCount; i++) {
		if (strcmp(fDirents[i]->name, name) == 0) {
			*inodeNumber = fDirents[i]->inode_number;

			OverlayInode *node = NULL;
			status_t result = get_vnode(Volume(), *inodeNumber,
				(void **)&node);
			if (result == B_OK && node != NULL && i >= 2)
				node->SetParentDir(this);
			return result;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


void
OverlayInode::SetName(const char *name)
{
	fName = name;
	if (!fIsModified)
		SetModified();
}


status_t
OverlayInode::GetName(char *buffer, size_t bufferSize)
{
	if (fName != NULL) {
		strlcpy(buffer, fName, bufferSize);
		return B_OK;
	}

	if (fIsVirtual)
		return B_UNSUPPORTED;

	if (fSuperVnode.ops->get_vnode_name == NULL)
		return B_UNSUPPORTED;

	return fSuperVnode.ops->get_vnode_name(SuperVolume(), &fSuperVnode, buffer,
		bufferSize);
}


status_t
OverlayInode::ReadStat(struct stat *stat)
{
	if (!fHasStat) {
		if (fSuperVnode.ops->read_stat == NULL)
			return B_UNSUPPORTED;

		status_t result = fSuperVnode.ops->read_stat(SuperVolume(),
			&fSuperVnode, &fStat);
		if (result != B_OK)
			return result;

		fHasStat = true;
	}

	if (stat == NULL)
		return B_OK;

	memcpy(stat, &fStat, sizeof(struct stat));
	stat->st_blocks = (stat->st_size + stat->st_blksize - 1) / stat->st_blksize;
	return B_OK;
}


status_t
OverlayInode::WriteStat(const struct stat *stat, uint32 statMask)
{
	if (!fHasStat)
		ReadStat(NULL);

	if (statMask & B_STAT_SIZE) {
		fStat.st_size = stat->st_size;
		_TrimBuffers();
	}

	if (statMask & B_STAT_MODE)
		fStat.st_mode = (fStat.st_mode & ~S_IUMSK) | (stat->st_mode & S_IUMSK);
	if (statMask & B_STAT_UID)
		fStat.st_uid = stat->st_uid;
	if (statMask & B_STAT_GID)
		fStat.st_gid = stat->st_gid;

	if (statMask & B_STAT_MODIFICATION_TIME)
		fStat.st_mtime = stat->st_mtime;
	if (statMask & B_STAT_CREATION_TIME)
		fStat.st_crtime = stat->st_crtime;

	if ((statMask & (B_STAT_MODE | B_STAT_UID | B_STAT_GID)) != 0
		&& (statMask & B_STAT_MODIFICATION_TIME) == 0) {
		fStat.st_mtime = time(NULL);
		statMask |= B_STAT_MODIFICATION_TIME;
	}

	if (!fIsModified)
		SetModified();

	notify_stat_changed(SuperVolume()->id, fInodeNumber, statMask);
	return B_OK;
}


status_t
OverlayInode::Create(const char *name, int openMode, int perms, void **cookie,
	ino_t *newInodeNumber)
{
	OverlayInode *newNode = NULL;
	status_t result = _CreateCommon(name, S_IFREG, perms, newInodeNumber,
		&newNode);
	if (result != B_OK)
		return result;

	return newNode->Open(openMode, cookie);
}


status_t
OverlayInode::Open(int openMode, void **_cookie)
{
	if (!fHasStat)
		ReadStat(NULL);

	open_cookie *cookie = (open_cookie *)malloc(sizeof(open_cookie));
	if (cookie == NULL)
		return B_NO_MEMORY;

	cookie->open_mode = openMode;
	*_cookie = cookie;

	if (fIsVirtual) {
		if (openMode & O_TRUNC) {
			fStat.st_size = 0;
			_TrimBuffers();
		}

		return B_OK;
	}

	if (fSuperVnode.ops->open == NULL)
		return B_UNSUPPORTED;

	if (fOriginalNodeLength < 0) {
		struct stat stat;
		status_t result = fSuperVnode.ops->read_stat(SuperVolume(),
			&fSuperVnode, &stat);
		if (result != B_OK)
			return result;

		fOriginalNodeLength = stat.st_size;
	}

	if (openMode & O_TRUNC) {
		fStat.st_size = 0;
		_TrimBuffers();
		if (!fIsModified)
			SetModified();
	}

	openMode &= ~(O_RDWR | O_WRONLY | O_TRUNC | O_CREAT);
	status_t result = fSuperVnode.ops->open(SuperVolume(), &fSuperVnode,
		openMode, &cookie->super_cookie);
	if (result != B_OK) {
		free(cookie);
		return result;
	}

	return B_OK;
}


status_t
OverlayInode::Close(void *_cookie)
{
	if (fIsVirtual)
		return B_OK;

	open_cookie *cookie = (open_cookie *)_cookie;
	return fSuperVnode.ops->close(SuperVolume(), &fSuperVnode,
		cookie->super_cookie);
}


status_t
OverlayInode::FreeCookie(void *_cookie)
{
	status_t result = B_OK;
	open_cookie *cookie = (open_cookie *)_cookie;
	if (!fIsVirtual) {
		result = fSuperVnode.ops->free_cookie(SuperVolume(),
			&fSuperVnode, cookie->super_cookie);
	}

	free(cookie);
	return result;
}


status_t
OverlayInode::Read(void *_cookie, off_t position, void *buffer, size_t *length,
	bool readPages, IORequest *ioRequest)
{
	if (position >= fStat.st_size) {
		*length = 0;
		return B_OK;
	}

	uint8 *pointer = (uint8 *)buffer;
	write_buffer *element = fWriteBuffers;
	size_t bytesLeft = MIN(fStat.st_size - position, *length);
	*length = bytesLeft;

	void *superCookie = _cookie;
	if (!fIsVirtual && !readPages && _cookie != NULL)
		superCookie = ((open_cookie *)_cookie)->super_cookie;

	while (bytesLeft > 0) {
		size_t gapSize = bytesLeft;
		if (element != NULL) {
			gapSize = MIN(bytesLeft, element->position > position ?
				element->position - position : 0);
		}

		if (gapSize > 0 && !fIsVirtual && position < fOriginalNodeLength) {
			// there's a part missing between the read position and our
			// next position, fill the gap with original file content
			size_t readLength = MIN(fOriginalNodeLength - position, gapSize);
			status_t result = B_ERROR;
			if (readPages) {
				iovec vector;
				vector.iov_base = pointer;
				vector.iov_len = readLength;

				result = fSuperVnode.ops->read_pages(SuperVolume(),
					&fSuperVnode, superCookie, position, &vector, 1,
					&readLength);
			} else if (ioRequest != NULL) {
				IORequest *subRequest;
				result = ioRequest->CreateSubRequest(position, position,
					readLength, subRequest);
				if (result != B_OK)
					return result;

				result = fSuperVnode.ops->io(SuperVolume(), &fSuperVnode,
					superCookie, subRequest);
				if (result != B_OK)
					return result;

				result = subRequest->Wait(0, 0);
				readLength = subRequest->TransferredBytes();
			} else {
				result = fSuperVnode.ops->read(SuperVolume(), &fSuperVnode,
					superCookie, position, pointer, &readLength);
			}

			if (result != B_OK)
				return result;

			pointer += readLength;
			position += readLength;
			bytesLeft -= readLength;
			gapSize -= readLength;
		}

		if (gapSize > 0) {
			// there's a gap before our next position which we cannot
			// fill with original file content, zero it out
			if (ioRequest != NULL)
				;// TODO: handle this case
			else
				memset(pointer, 0, gapSize);

			bytesLeft -= gapSize;
			position += gapSize;
			pointer += gapSize;
		}

		// we've reached the end
		if (bytesLeft == 0 || element == NULL)
			break;

		off_t elementEnd = element->position + element->length;
		if (elementEnd > position) {
			size_t copyLength = MIN(elementEnd - position, bytesLeft);

			const void *source = element->buffer + (position
				- element->position);
			if (ioRequest != NULL) {
				ioRequest->CopyData(source, (addr_t)pointer - (addr_t)buffer,
					copyLength);
			} else
				memcpy(pointer, source, copyLength);

			bytesLeft -= copyLength;
			position += copyLength;
			pointer += copyLength;
		}

		element = element->next;
	}

	return B_OK;
}


status_t
OverlayInode::Write(void *_cookie, off_t position, const void *buffer,
	size_t length, IORequest *ioRequest)
{
	if (_cookie != NULL) {
		open_cookie *cookie = (open_cookie *)_cookie;
		if (cookie->open_mode & O_APPEND)
			position = fStat.st_size;
	}

	if (!fIsModified)
		SetModified();

	// find insertion point
	write_buffer **link = &fWriteBuffers;
	write_buffer *other = fWriteBuffers;
	write_buffer *swallow = NULL;
	off_t newPosition = position;
	size_t newLength = length;
	uint32 swallowCount = 0;

	while (other) {
		off_t newEnd = newPosition + newLength;
		off_t otherEnd = other->position + other->length;
		if (otherEnd < newPosition) {
			// other is completely before us
			link = &other->next;
			other = other->next;
			continue;
		}

		if (other->position > newEnd) {
			// other is completely past us
			break;
		}

		swallowCount++;
		if (swallow == NULL)
			swallow = other;

		if (other->position <= newPosition) {
			if (swallowCount == 1 && otherEnd >= newEnd) {
				// other chunk completely covers us, just copy
				void *target = other->buffer + (newPosition - other->position);
				if (ioRequest != NULL)
					ioRequest->CopyData(0, target, length);
				else
					memcpy(target, buffer, length);

				fStat.st_mtime = time(NULL);
				notify_stat_changed(SuperVolume()->id, fInodeNumber,
					B_STAT_MODIFICATION_TIME);
				return B_OK;
			}

			newLength += newPosition - other->position;
			newPosition = other->position;
		}

		if (otherEnd > newEnd)
			newLength += otherEnd - newEnd;

		other = other->next;
	}

	write_buffer *element = (write_buffer *)malloc(sizeof(write_buffer) - 1
		+ newLength);
	if (element == NULL)
		return B_NO_MEMORY;

	element->next = *link;
	element->position = newPosition;
	element->length = newLength;
	*link = element;

	bool sizeChanged = false;
	off_t newEnd = newPosition + newLength;
	if (newEnd > fStat.st_size) {
		fStat.st_size = newEnd;
		sizeChanged = true;

		if (fFileCache)
			file_cache_set_size(fFileCache, newEnd);
	}

	// populate the buffer with the existing chunks
	if (swallowCount > 0) {
		while (swallowCount-- > 0) {
			memcpy(element->buffer + (swallow->position - newPosition),
				swallow->buffer, swallow->length);

			element->next = swallow->next;
			free(swallow);
			swallow = element->next;
		}
	}

	void *target = element->buffer + (position - newPosition);
	if (ioRequest != NULL)
		ioRequest->CopyData(0, target, length);
	else
		memcpy(target, buffer, length);

	fStat.st_mtime = time(NULL);
	notify_stat_changed(SuperVolume()->id, fInodeNumber,
		B_STAT_MODIFICATION_TIME | (sizeChanged ? B_STAT_SIZE : 0));
	return B_OK;
}


status_t
OverlayInode::SynchronousIO(void *cookie, IORequest *request)
{
	status_t result;
	size_t length = request->Length();
	if (request->IsWrite())
		result = Write(cookie, request->Offset(), NULL, length, request);
	else
		result = Read(cookie, request->Offset(), NULL, &length, false, request);

	if (result == B_OK)
		request->SetTransferredBytes(false, length);

	request->SetStatusAndNotify(result);
	return result;
}


status_t
OverlayInode::SetFlags(void *_cookie, int flags)
{
	// we can only handle O_APPEND, O_NONBLOCK is ignored.
	open_cookie *cookie = (open_cookie *)_cookie;
	cookie->open_mode = (cookie->open_mode & ~O_APPEND) | (flags & ~O_APPEND);
	return B_OK;
}


status_t
OverlayInode::CreateDir(const char *name, int perms)
{
	return _CreateCommon(name, S_IFDIR, perms, NULL, NULL);
}


status_t
OverlayInode::RemoveDir(const char *name)
{
	return RemoveEntry(name, NULL);
}


status_t
OverlayInode::OpenDir(void **cookie)
{
	if (!fHasDirents)
		_PopulateDirents();

	open_dir_cookie *dirCookie = (open_dir_cookie *)malloc(
		sizeof(open_dir_cookie));
	if (dirCookie == NULL)
		return B_NO_MEMORY;

	dirCookie->index = 0;
	*cookie = dirCookie;
	return B_OK;
}


status_t
OverlayInode::CloseDir(void *cookie)
{
	return B_OK;
}


status_t
OverlayInode::FreeDirCookie(void *cookie)
{
	free(cookie);
	return B_OK;
}


status_t
OverlayInode::ReadDir(void *cookie, struct dirent *buffer, size_t bufferSize,
	uint32 *num)
{
	open_dir_cookie *dirCookie = (open_dir_cookie *)cookie;
	if (dirCookie->index >= fDirentCount) {
		*num = 0;
		return B_OK;
	}

	overlay_dirent *dirent = fDirents[dirCookie->index++];
	size_t nameLength = MIN(strlen(dirent->name),
		bufferSize - sizeof(struct dirent)) + 1;

	buffer->d_dev = SuperVolume()->id;
	buffer->d_pdev = 0;
	buffer->d_ino = dirent->inode_number;
	buffer->d_pino = 0;
	buffer->d_reclen = sizeof(struct dirent) + nameLength;
	strlcpy(buffer->d_name, dirent->name, nameLength);

	*num = 1;
	return B_OK;
}


status_t
OverlayInode::RewindDir(void *cookie)
{
	open_dir_cookie *dirCookie = (open_dir_cookie *)cookie;
	dirCookie->index = 0;
	return B_OK;
}


status_t
OverlayInode::CreateSymlink(const char *name, const char *path, int mode)
{
	OverlayInode *newNode = NULL;
	// TODO: find out why mode is ignored
	status_t result = _CreateCommon(name, S_IFLNK, 0777, NULL, &newNode);
	if (result != B_OK)
		return result;

	return newNode->Write(NULL, 0, path, strlen(path), NULL);
}


status_t
OverlayInode::ReadSymlink(char *buffer, size_t *bufferSize)
{
	if (fIsVirtual) {
		if (!S_ISLNK(fStat.st_mode))
			return B_BAD_VALUE;

		return Read(NULL, 0, buffer, bufferSize, false, NULL);
	}

	if (fSuperVnode.ops->read_symlink == NULL)
		return B_UNSUPPORTED;

	return fSuperVnode.ops->read_symlink(SuperVolume(), &fSuperVnode, buffer,
		bufferSize);
}


status_t
OverlayInode::AddEntry(overlay_dirent *entry)
{
	if (!fHasDirents)
		_PopulateDirents();

	status_t result = RemoveEntry(entry->name, NULL);
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return B_FILE_EXISTS;

	overlay_dirent **newDirents = (overlay_dirent **)realloc(fDirents,
		sizeof(overlay_dirent *) * (fDirentCount + 1));
	if (newDirents == NULL)
		return B_NO_MEMORY;

	fDirents = newDirents;
	fDirents[fDirentCount++] = entry;

	if (!fIsModified)
		SetModified();

	return B_OK;
}


status_t
OverlayInode::RemoveEntry(const char *name, overlay_dirent **_entry)
{
	if (!fHasDirents)
		_PopulateDirents();

	for (uint32 i = 0; i < fDirentCount; i++) {
		overlay_dirent *entry = fDirents[i];
		if (strcmp(entry->name, name) == 0) {
			if (_entry == NULL) {
				// check for non-empty directories when trying
				// to dispose the entry
				OverlayInode *node = NULL;
				status_t result = get_vnode(Volume(), entry->inode_number,
					(void **)&node);
				if (result != B_OK)
					return result;

				if (node->IsNonEmptyDirectory())
					result = B_DIRECTORY_NOT_EMPTY;

				put_vnode(Volume(), entry->inode_number);
				if (result != B_OK)
					return result;
			}

			for (uint32 j = i + 1; j < fDirentCount; j++)
				fDirents[j - 1] = fDirents[j];
			fDirentCount--;

			if (_entry != NULL)
				*_entry = entry;
			else
				entry->remove_and_dispose(Volume(), fInodeNumber);

			if (!fIsModified)
				SetModified();

			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


void
OverlayInode::_TrimBuffers()
{
	// the file size has been changed and we want to trim
	// off everything that goes beyond the new size
	write_buffer **link = &fWriteBuffers;
	write_buffer *buffer = fWriteBuffers;

	while (buffer != NULL) {
		off_t bufferEnd = buffer->position + buffer->length;
		if (bufferEnd > fStat.st_size)
			break;

		link = &buffer->next;
		buffer = buffer->next;
	}

	if (buffer == NULL) {
		// didn't find anything crossing or past the end
		return;
	}

	if (buffer->position < fStat.st_size) {
		// got a crossing buffer to resize
		size_t newLength = fStat.st_size - buffer->position;
		write_buffer *newBuffer = (write_buffer *)realloc(buffer,
			sizeof(write_buffer) - 1 + newLength);

		if (newBuffer != NULL) {
			buffer = newBuffer;
			*link = newBuffer;
		} else {
			// we don't really care if it worked, if it didn't we simply
			// keep the old buffer and reset it's size
		}

		buffer->length = newLength;
		link = &buffer->next;
		buffer = buffer->next;
	}

	// everything else we can throw away
	*link = NULL;
	while (buffer != NULL) {
		write_buffer *next = buffer->next;
		free(buffer);
		buffer = next;
	}
}


status_t
OverlayInode::_PopulateDirents()
{
	if (fHasDirents)
		return B_OK;

	fDirents = (overlay_dirent **)malloc(sizeof(overlay_dirent *) * 2);
	if (fDirents == NULL)
		return B_NO_MEMORY;

	const char *names[] = { ".", ".." };
	ino_t inodes[] = { fInodeNumber,
		fParentDir != NULL ? fParentDir->InodeNumber() : 0 };
	for (uint32 i = 0; i < 2; i++) {
		fDirents[i] = (overlay_dirent *)malloc(sizeof(overlay_dirent));
		if (fDirents[i] == NULL)
			return B_NO_MEMORY;

		fDirents[i]->inode_number = inodes[i];
		fDirents[i]->name = strdup(names[i]);
		if (fDirents[i]->name == NULL) {
			free(fDirents[i]);
			return B_NO_MEMORY;
		}

		fDirentCount++;
	}

	fHasDirents = true;
	if (fIsVirtual || fSuperVnode.ops->open_dir == NULL
		|| fSuperVnode.ops->read_dir == NULL)
		return B_OK;

	// we don't really care about errors from here on
	void *superCookie = NULL;
	status_t result = fSuperVnode.ops->open_dir(SuperVolume(),
		&fSuperVnode, &superCookie);
	if (result != B_OK)
		return B_OK;

	size_t bufferSize = sizeof(struct dirent) + B_FILE_NAME_LENGTH;
	struct dirent *buffer = (struct dirent *)malloc(bufferSize);
	if (buffer == NULL)
		goto close_dir;

	while (true) {
		uint32 num = 1;
		result = fSuperVnode.ops->read_dir(SuperVolume(),
			&fSuperVnode, superCookie, buffer, bufferSize, &num);
		if (result != B_OK || num == 0)
			break;

		overlay_dirent **newDirents = (overlay_dirent **)realloc(fDirents,
			sizeof(overlay_dirent *) * (fDirentCount + num));
		if (newDirents == NULL) {
			TRACE_ALWAYS("failed to allocate storage for dirents\n");
			break;
		}

		fDirents = newDirents;
		struct dirent *dirent = buffer;
		for (uint32 i = 0; i < num; i++) {
			if (strcmp(dirent->d_name, ".") != 0
				&& strcmp(dirent->d_name, "..") != 0) {
				overlay_dirent *entry = (overlay_dirent *)malloc(
					sizeof(overlay_dirent));
				if (entry == NULL) {
					TRACE_ALWAYS("failed to allocate storage for dirent\n");
					break;
				}

				entry->inode_number = dirent->d_ino;
				entry->name = strdup(dirent->d_name);
				if (entry->name == NULL) {
					TRACE_ALWAYS("failed to duplicate dirent entry name\n");
					free(entry);
					break;
				}

				fDirents[fDirentCount++] = entry;
			}

			dirent = (struct dirent *)((uint8 *)dirent + dirent->d_reclen);
		}
	}

	free(buffer);

close_dir:
	if (fSuperVnode.ops->close_dir != NULL)
		fSuperVnode.ops->close_dir(SuperVolume(), &fSuperVnode, superCookie);

	if (fSuperVnode.ops->free_dir_cookie != NULL) {
		fSuperVnode.ops->free_dir_cookie(SuperVolume(), &fSuperVnode,
			superCookie);
	}

	return B_OK;
}


status_t
OverlayInode::_CreateCommon(const char *name, int type, int perms,
	ino_t *newInodeNumber, OverlayInode **_node)
{
	if (!fHasStat)
		ReadStat(NULL);

	if (!S_ISDIR(fStat.st_mode))
		return B_NOT_A_DIRECTORY;

	overlay_dirent *entry = (overlay_dirent *)malloc(sizeof(overlay_dirent));
	if (entry == NULL)
		return B_NO_MEMORY;

	entry->name = strdup(name);
	if (entry->name == NULL) {
		free(entry);
		return B_NO_MEMORY;
	}

	entry->inode_number = fVolume->BuildInodeNumber();

	OverlayInode *node = new(std::nothrow) OverlayInode(fVolume, NULL,
		entry->inode_number, this, entry->name, (perms & S_IUMSK) | type);
	if (node == NULL) {
		free(entry->name);
		free(entry);
		return B_NO_MEMORY;
	}

	status_t result = AddEntry(entry);
	if (result != B_OK) {
		free(entry->name);
		free(entry);
		delete node;
		return result;
	}

	result = publish_overlay_vnode(fVolume->Volume(), entry->inode_number,
		node, type);
	if (result != B_OK) {
		RemoveEntry(entry->name, NULL);
		delete node;
		return result;
	}

	node->SetModified();
	node->CreateCache();

	if (newInodeNumber != NULL)
		*newInodeNumber = entry->inode_number;
	if (_node != NULL)
		*_node = node;

	notify_entry_created(SuperVolume()->id, fInodeNumber, entry->name,
		entry->inode_number);

	return B_OK;
}


//	#pragma mark - vnode ops


#define OVERLAY_CALL(op, params...) \
	TRACE("relaying op: " #op "\n"); \
	OverlayInode *node = (OverlayInode *)vnode->private_node; \
	if (node->IsVirtual()) \
		return B_UNSUPPORTED; \
	fs_vnode *superVnode = node->SuperVnode(); \
	if (superVnode->ops->op != NULL) \
		return superVnode->ops->op(volume->super_volume, superVnode, params); \
	return B_UNSUPPORTED;


static status_t
overlay_put_vnode(fs_volume *volume, fs_vnode *vnode, bool reenter)
{
	TRACE("put_vnode\n");
	OverlayInode *node = (OverlayInode *)vnode->private_node;
	if (node->IsVirtual() || node->IsModified()) {
		panic("loosing virtual/modified node\n");
		delete node;
		return B_OK;
	}

	status_t result = B_OK;
	fs_vnode *superVnode = node->SuperVnode();
	if (superVnode->ops->put_vnode != NULL) {
		result = superVnode->ops->put_vnode(volume->super_volume, superVnode,
			reenter);
	}

	delete node;
	return result;
}


static status_t
overlay_remove_vnode(fs_volume *volume, fs_vnode *vnode, bool reenter)
{
	TRACE("remove_vnode\n");
	OverlayInode *node = (OverlayInode *)vnode->private_node;
	if (node->IsVirtual()) {
		delete node;
		return B_OK;
	}

	status_t result = B_OK;
	fs_vnode *superVnode = node->SuperVnode();
	if (superVnode->ops->put_vnode != NULL) {
		result = superVnode->ops->put_vnode(volume->super_volume, superVnode,
			reenter);
	}

	delete node;
	return result;
}


static status_t
overlay_get_super_vnode(fs_volume *volume, fs_vnode *vnode,
	fs_volume *superVolume, fs_vnode *_superVnode)
{
	if (volume == superVolume) {
		*_superVnode = *vnode;
		return B_OK;
	}

	OverlayInode *node = (OverlayInode *)vnode->private_node;
	if (node->IsVirtual()) {
		*_superVnode = *vnode;
		return B_OK;
	}

	fs_vnode *superVnode = node->SuperVnode();
	if (superVnode->ops->get_super_vnode != NULL) {
		return superVnode->ops->get_super_vnode(volume->super_volume,
			superVnode, superVolume, _superVnode);
	}

	*_superVnode = *superVnode;
	return B_OK;
}


static status_t
overlay_lookup(fs_volume *volume, fs_vnode *vnode, const char *name, ino_t *id)
{
	TRACE("lookup: \"%s\"\n", name);
	return ((OverlayInode *)vnode->private_node)->Lookup(name, id);
}


static status_t
overlay_get_vnode_name(fs_volume *volume, fs_vnode *vnode, char *buffer,
	size_t bufferSize)
{
	return ((OverlayInode *)vnode->private_node)->GetName(buffer, bufferSize);
}


static bool
overlay_can_page(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	TRACE("relaying op: can_page\n");
	OverlayInode *node = (OverlayInode *)vnode->private_node;
	if (node->IsVirtual())
		return false;

	fs_vnode *superVnode = node->SuperVnode();
	if (superVnode->ops->can_page != NULL) {
		return superVnode->ops->can_page(volume->super_volume, superVnode,
			cookie);
	}

	return false;
}


static status_t
overlay_read_pages(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	const iovec *vecs, size_t count, size_t *numBytes)
{
	OverlayInode *node = (OverlayInode *)vnode->private_node;
	size_t bytesLeft = *numBytes;

	for (size_t i = 0; i < count; i++) {
		size_t transferBytes = MIN(vecs[i].iov_len, bytesLeft);
		status_t result = node->Read(cookie, pos, vecs[i].iov_base,
			&transferBytes, true, NULL);
		if (result != B_OK) {
			*numBytes -= bytesLeft;
			return result;
		}

		bytesLeft -= transferBytes;
		if (bytesLeft == 0)
			return B_OK;

		if (transferBytes < vecs[i].iov_len) {
			*numBytes -= bytesLeft;
			return B_OK;
		}

		pos += transferBytes;
	}

	*numBytes = 0;
	return B_OK;
}


static status_t
overlay_write_pages(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	const iovec *vecs, size_t count, size_t *numBytes)
{
	OverlayInode *node = (OverlayInode *)vnode->private_node;
	size_t bytesLeft = *numBytes;

	for (size_t i = 0; i < count; i++) {
		size_t transferBytes = MIN(vecs[i].iov_len, bytesLeft);
		status_t result = node->Write(cookie, pos, vecs[i].iov_base,
			transferBytes, NULL);
		if (result != B_OK) {
			*numBytes -= bytesLeft;
			return result;
		}

		bytesLeft -= transferBytes;
		if (bytesLeft == 0)
			return B_OK;

		if (transferBytes < vecs[i].iov_len) {
			*numBytes -= bytesLeft;
			return B_OK;
		}

		pos += transferBytes;
	}

	*numBytes = 0;
	return B_OK;
}


static status_t
overlay_io(fs_volume *volume, fs_vnode *vnode, void *cookie,
	io_request *request)
{
	OverlayInode *node = (OverlayInode *)vnode->private_node;
	if (io_request_is_write(request) || node->IsModified())
		return node->SynchronousIO(cookie, (IORequest *)request);

	TRACE("relaying op: io\n");
	fs_vnode *superVnode = node->SuperVnode();
	if (superVnode->ops->io != NULL) {
		return superVnode->ops->io(volume->super_volume, superVnode,
			cookie != NULL ? ((open_cookie *)cookie)->super_cookie : NULL,
			request);
	}

	return B_UNSUPPORTED;
}


static status_t
overlay_cancel_io(fs_volume *volume, fs_vnode *vnode, void *cookie,
	io_request *request)
{
	OVERLAY_CALL(cancel_io, cookie, request)
}


static status_t
overlay_get_file_map(fs_volume *volume, fs_vnode *vnode, off_t offset,
	size_t size, struct file_io_vec *vecs, size_t *count)
{
	OVERLAY_CALL(get_file_map, offset, size, vecs, count)
}


static status_t
overlay_ioctl(fs_volume *volume, fs_vnode *vnode, void *cookie, ulong op,
	void *buffer, size_t length)
{
	OVERLAY_CALL(ioctl, cookie, op, buffer, length)
}


static status_t
overlay_set_flags(fs_volume *volume, fs_vnode *vnode, void *cookie,
	int flags)
{
	return ((OverlayInode *)vnode->private_node)->SetFlags(cookie, flags);
}


static status_t
overlay_select(fs_volume *volume, fs_vnode *vnode, void *cookie, uint8 event,
	selectsync *sync)
{
	OVERLAY_CALL(select, cookie, event, sync)
}


static status_t
overlay_deselect(fs_volume *volume, fs_vnode *vnode, void *cookie, uint8 event,
	selectsync *sync)
{
	OVERLAY_CALL(deselect, cookie, event, sync)
}


static status_t
overlay_fsync(fs_volume *volume, fs_vnode *vnode)
{
	return B_OK;
}


static status_t
overlay_read_symlink(fs_volume *volume, fs_vnode *vnode, char *buffer,
	size_t *bufferSize)
{
	TRACE("read_symlink\n");
	return ((OverlayInode *)vnode->private_node)->ReadSymlink(buffer,
		bufferSize);
}


static status_t
overlay_create_symlink(fs_volume *volume, fs_vnode *vnode, const char *name,
	const char *path, int mode)
{
	TRACE("create_symlink: \"%s\" -> \"%s\"\n", name, path);
	return ((OverlayInode *)vnode->private_node)->CreateSymlink(name, path,
		mode);
}


static status_t
overlay_link(fs_volume *volume, fs_vnode *vnode, const char *name,
	fs_vnode *target)
{
	return B_UNSUPPORTED;
}


static status_t
overlay_unlink(fs_volume *volume, fs_vnode *vnode, const char *name)
{
	TRACE("unlink: \"%s\"\n", name);
	return ((OverlayInode *)vnode->private_node)->RemoveEntry(name, NULL);
}


static status_t
overlay_rename(fs_volume *volume, fs_vnode *vnode,
	const char *fromName, fs_vnode *toVnode, const char *toName)
{
	TRACE("rename: \"%s\" -> \"%s\"\n", fromName, toName);
	OverlayInode *fromNode = (OverlayInode *)vnode->private_node;
	OverlayInode *toNode = (OverlayInode *)toVnode->private_node;
	overlay_dirent *entry = NULL;

	status_t result = fromNode->RemoveEntry(fromName, &entry);
	if (result != B_OK)
		return result;

	char *oldName = entry->name;
	entry->name = strdup(toName);
	if (entry->name == NULL) {
		entry->name = oldName;
		if (fromNode->AddEntry(entry) != B_OK)
			entry->remove_and_dispose(volume, fromNode->InodeNumber());

		return B_NO_MEMORY;
	}

	result = toNode->AddEntry(entry);
	if (result != B_OK) {
		free(entry->name);
		entry->name = oldName;
		if (fromNode->AddEntry(entry) != B_OK)
			entry->remove_and_dispose(volume, fromNode->InodeNumber());

		return result;
	}

	OverlayInode *node = NULL;
	result = get_vnode(volume, entry->inode_number, (void **)&node);
	if (result == B_OK && node != NULL) {
		node->SetName(entry->name);
		node->SetParentDir(toNode);
		put_vnode(volume, entry->inode_number);
	}

	free(oldName);
	notify_entry_moved(volume->id, fromNode->InodeNumber(), fromName,
		toNode->InodeNumber(), toName, entry->inode_number);
	return B_OK;
}


static status_t
overlay_access(fs_volume *volume, fs_vnode *vnode, int mode)
{
	// TODO: implement
	return B_OK;
}


static status_t
overlay_read_stat(fs_volume *volume, fs_vnode *vnode, struct stat *stat)
{
	TRACE("read_stat\n");
	return ((OverlayInode *)vnode->private_node)->ReadStat(stat);
}


static status_t
overlay_write_stat(fs_volume *volume, fs_vnode *vnode, const struct stat *stat,
	uint32 statMask)
{
	TRACE("write_stat\n");
	return ((OverlayInode *)vnode->private_node)->WriteStat(stat, statMask);
}


static status_t
overlay_create(fs_volume *volume, fs_vnode *vnode, const char *name,
	int openMode, int perms, void **cookie, ino_t *newVnodeID)
{
	TRACE("create: \"%s\"\n", name);
	return ((OverlayInode *)vnode->private_node)->Create(name, openMode,
		perms, cookie, newVnodeID);
}


static status_t
overlay_open(fs_volume *volume, fs_vnode *vnode, int openMode, void **cookie)
{
	TRACE("open\n");
	return ((OverlayInode *)vnode->private_node)->Open(openMode, cookie);
}


static status_t
overlay_close(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	TRACE("close\n");
	return ((OverlayInode *)vnode->private_node)->Close(cookie);
}


static status_t
overlay_free_cookie(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	TRACE("free_cookie\n");
	return ((OverlayInode *)vnode->private_node)->FreeCookie(cookie);
}


static status_t
overlay_read(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	void *buffer, size_t *length)
{
	TRACE("read\n");
	return ((OverlayInode *)vnode->private_node)->Read(cookie, pos, buffer,
		length, false, NULL);
}


static status_t
overlay_write(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	const void *buffer, size_t *length)
{
	TRACE("write\n");
	return ((OverlayInode *)vnode->private_node)->Write(cookie, pos, buffer,
		*length, NULL);
}


static status_t
overlay_create_dir(fs_volume *volume, fs_vnode *vnode, const char *name,
	int perms)
{
	TRACE("create_dir: \"%s\"\n", name);
	return ((OverlayInode *)vnode->private_node)->CreateDir(name, perms);
}


static status_t
overlay_remove_dir(fs_volume *volume, fs_vnode *vnode, const char *name)
{
	TRACE("remove_dir: \"%s\"\n", name);
	return ((OverlayInode *)vnode->private_node)->RemoveDir(name);
}


static status_t
overlay_open_dir(fs_volume *volume, fs_vnode *vnode, void **cookie)
{
	TRACE("open_dir\n");
	return ((OverlayInode *)vnode->private_node)->OpenDir(cookie);
}


static status_t
overlay_close_dir(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	TRACE("close_dir\n");
	return ((OverlayInode *)vnode->private_node)->CloseDir(cookie);
}


static status_t
overlay_free_dir_cookie(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	TRACE("free_dir_cookie\n");
	return ((OverlayInode *)vnode->private_node)->FreeDirCookie(cookie);
}


static status_t
overlay_read_dir(fs_volume *volume, fs_vnode *vnode, void *cookie,
	struct dirent *buffer, size_t bufferSize, uint32 *num)
{
	TRACE("read_dir\n");
	return ((OverlayInode *)vnode->private_node)->ReadDir(cookie, buffer,
		bufferSize, num);
}


static status_t
overlay_rewind_dir(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	TRACE("rewind_dir\n");
	return ((OverlayInode *)vnode->private_node)->RewindDir(cookie);
}


static status_t
overlay_open_attr_dir(fs_volume *volume, fs_vnode *vnode, void **cookie)
{
	OVERLAY_CALL(open_attr_dir, cookie)
}


static status_t
overlay_close_attr_dir(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	OVERLAY_CALL(close_attr_dir, cookie)
}


static status_t
overlay_free_attr_dir_cookie(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	OVERLAY_CALL(free_attr_dir_cookie, cookie)
}


static status_t
overlay_read_attr_dir(fs_volume *volume, fs_vnode *vnode, void *cookie,
	struct dirent *buffer, size_t bufferSize, uint32 *num)
{
	OVERLAY_CALL(read_attr_dir, cookie, buffer, bufferSize, num)
}


static status_t
overlay_rewind_attr_dir(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	OVERLAY_CALL(rewind_attr_dir, cookie)
}


static status_t
overlay_create_attr(fs_volume *volume, fs_vnode *vnode, const char *name,
	uint32 type, int openMode, void **cookie)
{
	OVERLAY_CALL(create_attr, name, type, openMode, cookie)
}


static status_t
overlay_open_attr(fs_volume *volume, fs_vnode *vnode, const char *name,
	int openMode, void **cookie)
{
	OVERLAY_CALL(open_attr, name, openMode, cookie)
}


static status_t
overlay_close_attr(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	OVERLAY_CALL(close_attr, cookie)
}


static status_t
overlay_free_attr_cookie(fs_volume *volume, fs_vnode *vnode, void *cookie)
{
	OVERLAY_CALL(free_attr_cookie, cookie)
}


static status_t
overlay_read_attr(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	void *buffer, size_t *length)
{
	OVERLAY_CALL(read_attr, cookie, pos, buffer, length)
}


static status_t
overlay_write_attr(fs_volume *volume, fs_vnode *vnode, void *cookie, off_t pos,
	const void *buffer, size_t *length)
{
	OVERLAY_CALL(write_attr, cookie, pos, buffer, length)
}


static status_t
overlay_read_attr_stat(fs_volume *volume, fs_vnode *vnode, void *cookie,
	struct stat *stat)
{
	OVERLAY_CALL(read_attr_stat, cookie, stat)
}


static status_t
overlay_write_attr_stat(fs_volume *volume, fs_vnode *vnode, void *cookie,
	const struct stat *stat, int statMask)
{
	OVERLAY_CALL(write_attr_stat, cookie, stat, statMask)
}


static status_t
overlay_rename_attr(fs_volume *volume, fs_vnode *vnode,
	const char *fromName, fs_vnode *toVnode, const char *toName)
{
	OVERLAY_CALL(rename_attr, fromName, toVnode, toName)
}


static status_t
overlay_remove_attr(fs_volume *volume, fs_vnode *vnode, const char *name)
{
	OVERLAY_CALL(remove_attr, name)
}


static status_t
overlay_create_special_node(fs_volume *volume, fs_vnode *vnode,
	const char *name, fs_vnode *subVnode, mode_t mode, uint32 flags,
	fs_vnode *_superVnode, ino_t *nodeID)
{
	OVERLAY_CALL(create_special_node, name, subVnode, mode, flags, _superVnode, nodeID)
}


static fs_vnode_ops sOverlayVnodeOps = {
	&overlay_lookup,
	&overlay_get_vnode_name,

	&overlay_put_vnode,
	&overlay_remove_vnode,

	&overlay_can_page,
	&overlay_read_pages,
	&overlay_write_pages,

	&overlay_io,
	&overlay_cancel_io,

	&overlay_get_file_map,

	/* common */
	&overlay_ioctl,
	&overlay_set_flags,
	&overlay_select,
	&overlay_deselect,
	&overlay_fsync,

	&overlay_read_symlink,
	&overlay_create_symlink,
	&overlay_link,
	&overlay_unlink,
	&overlay_rename,

	&overlay_access,
	&overlay_read_stat,
	&overlay_write_stat,

	/* file */
	&overlay_create,
	&overlay_open,
	&overlay_close,
	&overlay_free_cookie,
	&overlay_read,
	&overlay_write,

	/* directory */
	&overlay_create_dir,
	&overlay_remove_dir,
	&overlay_open_dir,
	&overlay_close_dir,
	&overlay_free_dir_cookie,
	&overlay_read_dir,
	&overlay_rewind_dir,

	/* attribute directory operations */
	&overlay_open_attr_dir,
	&overlay_close_attr_dir,
	&overlay_free_attr_dir_cookie,
	&overlay_read_attr_dir,
	&overlay_rewind_attr_dir,

	/* attribute operations */
	&overlay_create_attr,
	&overlay_open_attr,
	&overlay_close_attr,
	&overlay_free_attr_cookie,
	&overlay_read_attr,
	&overlay_write_attr,

	&overlay_read_attr_stat,
	&overlay_write_attr_stat,
	&overlay_rename_attr,
	&overlay_remove_attr,

	/* support for node and FS layers */
	&overlay_create_special_node,
	&overlay_get_super_vnode
};


//	#pragma mark - volume ops


#define OVERLAY_VOLUME_CALL(op, params...) \
	TRACE_VOLUME("relaying volume op: " #op "\n"); \
	if (volume->super_volume->ops->op != NULL) \
		return volume->super_volume->ops->op(volume->super_volume, params);


static status_t
overlay_unmount(fs_volume *volume)
{
	TRACE_VOLUME("relaying volume op: unmount\n");
	if (volume->super_volume != NULL
		&& volume->super_volume->ops != NULL
		&& volume->super_volume->ops->unmount != NULL)
		volume->super_volume->ops->unmount(volume->super_volume);

	delete (OverlayVolume *)volume->private_volume;
	return B_OK;
}


static status_t
overlay_read_fs_info(fs_volume *volume, struct fs_info *info)
{
	TRACE_VOLUME("relaying volume op: read_fs_info\n");
	status_t result = B_UNSUPPORTED;
	if (volume->super_volume->ops->read_fs_info != NULL) {
		result = volume->super_volume->ops->read_fs_info(volume->super_volume,
			info);
		if (result != B_OK)
			return result;

		info->flags &= ~B_FS_IS_READONLY;

		// TODO: maybe calculate based on available ram
		off_t available = 1024 * 1024 * 100 / info->block_size;
		info->total_blocks += available;
		info->free_blocks += available;
		return B_OK;
	}

	return B_UNSUPPORTED;
}


static status_t
overlay_write_fs_info(fs_volume *volume, const struct fs_info *info,
	uint32 mask)
{
	OVERLAY_VOLUME_CALL(write_fs_info, info, mask)
	return B_UNSUPPORTED;
}


static status_t
overlay_sync(fs_volume *volume)
{
	TRACE_VOLUME("relaying volume op: sync\n");
	if (volume->super_volume->ops->sync != NULL)
		return volume->super_volume->ops->sync(volume->super_volume);
	return B_UNSUPPORTED;
}


static status_t
overlay_get_vnode(fs_volume *volume, ino_t id, fs_vnode *vnode, int *_type,
	uint32 *_flags, bool reenter)
{
	TRACE_VOLUME("relaying volume op: get_vnode\n");
	if (volume->super_volume->ops->get_vnode != NULL) {
		status_t status = volume->super_volume->ops->get_vnode(
			volume->super_volume, id, vnode, _type, _flags, reenter);
		if (status != B_OK)
			return status;

		OverlayInode *node = new(std::nothrow) OverlayInode(
			(OverlayVolume *)volume->private_volume, vnode, id);
		if (node == NULL) {
			vnode->ops->put_vnode(volume->super_volume, vnode, reenter);
			return B_NO_MEMORY;
		}

		status = node->InitCheck();
		if (status != B_OK) {
			vnode->ops->put_vnode(volume->super_volume, vnode, reenter);
			delete node;
			return status;
		}

		vnode->private_node = node;
		vnode->ops = &sOverlayVnodeOps;
		return B_OK;
	}

	return B_UNSUPPORTED;
}


static status_t
overlay_open_index_dir(fs_volume *volume, void **cookie)
{
	OVERLAY_VOLUME_CALL(open_index_dir, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_close_index_dir(fs_volume *volume, void *cookie)
{
	OVERLAY_VOLUME_CALL(close_index_dir, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_free_index_dir_cookie(fs_volume *volume, void *cookie)
{
	OVERLAY_VOLUME_CALL(free_index_dir_cookie, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_read_index_dir(fs_volume *volume, void *cookie, struct dirent *buffer,
	size_t bufferSize, uint32 *_num)
{
	OVERLAY_VOLUME_CALL(read_index_dir, cookie, buffer, bufferSize, _num)
	return B_UNSUPPORTED;
}


static status_t
overlay_rewind_index_dir(fs_volume *volume, void *cookie)
{
	OVERLAY_VOLUME_CALL(rewind_index_dir, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_create_index(fs_volume *volume, const char *name, uint32 type,
	uint32 flags)
{
	OVERLAY_VOLUME_CALL(create_index, name, type, flags)
	return B_UNSUPPORTED;
}


static status_t
overlay_remove_index(fs_volume *volume, const char *name)
{
	OVERLAY_VOLUME_CALL(remove_index, name)
	return B_UNSUPPORTED;
}


static status_t
overlay_read_index_stat(fs_volume *volume, const char *name, struct stat *stat)
{
	OVERLAY_VOLUME_CALL(read_index_stat, name, stat)
	return B_UNSUPPORTED;
}


static status_t
overlay_open_query(fs_volume *volume, const char *query, uint32 flags,
	port_id port, uint32 token, void **_cookie)
{
	OVERLAY_VOLUME_CALL(open_query, query, flags, port, token, _cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_close_query(fs_volume *volume, void *cookie)
{
	OVERLAY_VOLUME_CALL(close_query, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_free_query_cookie(fs_volume *volume, void *cookie)
{
	OVERLAY_VOLUME_CALL(free_query_cookie, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_read_query(fs_volume *volume, void *cookie, struct dirent *buffer,
	size_t bufferSize, uint32 *_num)
{
	OVERLAY_VOLUME_CALL(read_query, cookie, buffer, bufferSize, _num)
	return B_UNSUPPORTED;
}


static status_t
overlay_rewind_query(fs_volume *volume, void *cookie)
{
	OVERLAY_VOLUME_CALL(rewind_query, cookie)
	return B_UNSUPPORTED;
}


static status_t
overlay_all_layers_mounted(fs_volume *volume)
{
	return B_OK;
}


static status_t
overlay_create_sub_vnode(fs_volume *volume, ino_t id, fs_vnode *vnode)
{
	OverlayInode *node = new(std::nothrow) OverlayInode(
		(OverlayVolume *)volume->private_volume, vnode, id);
	if (node == NULL)
		return B_NO_MEMORY;

	status_t status = node->InitCheck();
	if (status != B_OK) {
		delete node;
		return status;
	}

	vnode->private_node = node;
	vnode->ops = &sOverlayVnodeOps;
	return B_OK;
}


static status_t
overlay_delete_sub_vnode(fs_volume *volume, fs_vnode *vnode)
{
	delete (OverlayInode *)vnode;
	return B_OK;
}


static fs_volume_ops sOverlayVolumeOps = {
	&overlay_unmount,

	&overlay_read_fs_info,
	&overlay_write_fs_info,
	&overlay_sync,

	&overlay_get_vnode,
	&overlay_open_index_dir,
	&overlay_close_index_dir,
	&overlay_free_index_dir_cookie,
	&overlay_read_index_dir,
	&overlay_rewind_index_dir,

	&overlay_create_index,
	&overlay_remove_index,
	&overlay_read_index_stat,

	&overlay_open_query,
	&overlay_close_query,
	&overlay_free_query_cookie,
	&overlay_read_query,
	&overlay_rewind_query,

	&overlay_all_layers_mounted,
	&overlay_create_sub_vnode,
	&overlay_delete_sub_vnode
};


//	#pragma mark - filesystem module


static status_t
overlay_mount(fs_volume *volume, const char *device, uint32 flags,
	const char *args, ino_t *rootID)
{
	TRACE_VOLUME("mounting write overlay\n");
	volume->private_volume = new(std::nothrow) OverlayVolume(volume);
	if (volume->private_volume == NULL)
		return B_NO_MEMORY;

	volume->ops = &sOverlayVolumeOps;
	return B_OK;
}


static status_t
overlay_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
		default:
			return B_ERROR;
	}
}


static file_system_module_info sOverlayFileSystem = {
	{
		"file_systems/write_overlay"B_CURRENT_FS_API_VERSION,
		0,
		overlay_std_ops,
	},

	"write_overlay",				// short_name
	"Write Overlay File System",	// pretty_name
	0,								// DDM flags

	// scanning
	NULL, // identify_partition
	NULL, // scan_partition
	NULL, // free_identify_partition_cookie
	NULL, // free_partition_content_cookie

	// general operations
	&overlay_mount,

	// capability querying
	NULL, // get_supported_operations

	NULL, // validate_resize
	NULL, // validate_move
	NULL, // validate_set_content_name
	NULL, // validate_set_content_parameters
	NULL, // validate_initialize

	// shadow partition modification
	NULL, // shadow_changed

	// writing
	NULL, // defragment
	NULL, // repair
	NULL, // resize
	NULL, // move
	NULL, // set_content_name
	NULL, // set_content_parameters
	NULL // initialize
};


status_t
publish_overlay_vnode(fs_volume *volume, ino_t inodeNumber, void *privateNode,
	int type)
{
	return publish_vnode(volume, inodeNumber, privateNode, &sOverlayVnodeOps,
		type, 0);
}

}	// namespace write_overlay

using namespace write_overlay;

module_info *modules[] = {
	(module_info *)&sOverlayFileSystem,
	NULL,
};
