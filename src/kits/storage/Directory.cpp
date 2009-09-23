/*
 * Copyright 2002-2009, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Tyler Dauwalder
 *		Ingo Weinhold, bonefish@users.sf.net
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "storage_support.h"

#include <fcntl.h>
#include <string.h>

#include <compat/sys/stat.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <fs_info.h>
#include <Path.h>
#include <SymLink.h>

#include <syscalls.h>
#include <umask.h>


//! Creates an uninitialized BDirectory object.
BDirectory::BDirectory()
	:
	fDirFd(-1)
{
}


/*!	\brief Creates a copy of the supplied BDirectory.
	\param dir the BDirectory object to be copied
*/
BDirectory::BDirectory(const BDirectory& dir)
	:
	fDirFd(-1)
{
	*this = dir;
}


/*! \brief Creates a BDirectory and initializes it to the directory referred
	to by the supplied entry_ref.
	\param ref the entry_ref referring to the directory
*/
BDirectory::BDirectory(const entry_ref* ref)
	:
	fDirFd(-1)
{
	SetTo(ref);
}


/*! \brief Creates a BDirectory and initializes it to the directory referred
	to by the supplied node_ref.
	\param nref the node_ref referring to the directory
*/
BDirectory::BDirectory(const node_ref* nref)
	:
	fDirFd(-1)
{
	SetTo(nref);
}


/*! \brief Creates a BDirectory and initializes it to the directory referred
	to by the supplied BEntry.
	\param entry the BEntry referring to the directory
*/
BDirectory::BDirectory(const BEntry* entry)
	:
	fDirFd(-1)
{
	SetTo(entry);
}


/*! \brief Creates a BDirectory and initializes it to the directory referred
	to by the supplied path name.
	\param path the directory's path name
*/
BDirectory::BDirectory(const char* path)
	:
	fDirFd(-1)
{
	SetTo(path);
}


/*! \brief Creates a BDirectory and initializes it to the directory referred
	to by the supplied path name relative to the specified BDirectory.
	\param dir the BDirectory, relative to which the directory's path name is
		   given
	\param path the directory's path name relative to \a dir
*/
BDirectory::BDirectory(const BDirectory* dir, const char* path)
	:
	fDirFd(-1)
{
	SetTo(dir, path);
}


/*! If the BDirectory is properly initialized, the directory's file descriptor
	is closed.
*/
BDirectory::~BDirectory()
{
	// Also called by the BNode destructor, but we rather try to avoid
	// problems with calling virtual functions in the base class destructor.
	// Depending on the compiler implementation an object may be degraded to
	// an object of the base class after the destructor of the derived class
	// has been executed.
	close_fd();
}


/*! \brief Re-initializes the BDirectory to the directory referred to by the
	supplied entry_ref.
	\param ref the entry_ref referring to the directory
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref.
	- \c B_ENTRY_NOT_FOUND: Directory not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::SetTo(const entry_ref* ref)
{
	// open node
	status_t error = _SetTo(ref, true);
	if (error != B_OK)
		return error;

	// open dir
	fDirFd = _kern_open_dir_entry_ref(ref->device, ref->directory, ref->name);
	if (fDirFd < 0) {
		status_t error = fDirFd;
		Unset();
		return (fCStatus = error);
	}

	// set close on exec flag on dir FD
	fcntl(fDirFd, F_SETFD, FD_CLOEXEC);

	return B_OK;
}


/*! \brief Re-initializes the BDirectory to the directory referred to by the
	supplied node_ref.
	\param nref the node_ref referring to the directory
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a nref.
	- \c B_ENTRY_NOT_FOUND: Directory not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::SetTo(const node_ref* nref)
{
	Unset();
	status_t error = (nref ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		entry_ref ref(nref->device, nref->node, ".");
		error = SetTo(&ref);
	}
	set_status(error);
	return error;
}


/*! \brief Re-initializes the BDirectory to the directory referred to by the
	supplied BEntry.
	\param entry the BEntry referring to the directory
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a entry.
	- \c B_ENTRY_NOT_FOUND: Directory not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::SetTo(const BEntry* entry)
{
	if (!entry) {
		Unset();
		return (fCStatus = B_BAD_VALUE);
	}

	// open node
	status_t error = _SetTo(entry->fDirFd, entry->fName, true);
	if (error != B_OK)
		return error;

	// open dir
	fDirFd = _kern_open_dir(entry->fDirFd, entry->fName);
	if (fDirFd < 0) {
		status_t error = fDirFd;
		Unset();
		return (fCStatus = error);
	}

	// set close on exec flag on dir FD
	fcntl(fDirFd, F_SETFD, FD_CLOEXEC);

	return B_OK;
}


/*! \brief Re-initializes the BDirectory to the directory referred to by the
	supplied path name.
	\param path the directory's path name
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path.
	- \c B_ENTRY_NOT_FOUND: Directory not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_NAME_TOO_LONG: The supplied path name (\a path) is too long.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	- \c B_NOT_A_DIRECTORY: \a path includes a non-directory.
*/
status_t
BDirectory::SetTo(const char* path)
{
	// open node
	status_t error = _SetTo(-1, path, true);
	if (error != B_OK)
		return error;

	// open dir
	fDirFd = _kern_open_dir(-1, path);
	if (fDirFd < 0) {
		status_t error = fDirFd;
		Unset();
		return (fCStatus = error);
	}

	// set close on exec flag on dir FD
	fcntl(fDirFd, F_SETFD, FD_CLOEXEC);

	return B_OK;
}


/*! \brief Re-initializes the BDirectory to the directory referred to by the
	supplied path name relative to the specified BDirectory.
	\param dir the BDirectory, relative to which the directory's path name is
		   given
	\param path the directory's path name relative to \a dir
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a dir or \a path, or \a path is absolute.
	- \c B_ENTRY_NOT_FOUND: Directory not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_NAME_TOO_LONG: The supplied path name (\a path) is too long.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	- \c B_NOT_A_DIRECTORY: \a path includes a non-directory.
*/
status_t
BDirectory::SetTo(const BDirectory* dir, const char* path)
{
	if (!dir || !path || BPrivate::Storage::is_absolute_path(path)) {
		Unset();
		return (fCStatus = B_BAD_VALUE);
	}

	int dirFD = dir->fDirFd;
	if (dir == this) {
		// prevent that our file descriptor goes away in _SetTo()
		fDirFd = -1;
	}

	// open node
	status_t error = _SetTo(dirFD, path, true);
	if (error != B_OK)
		return error;

	// open dir
	fDirFd = _kern_open_dir(dirFD, path);
	if (fDirFd < 0) {
		status_t error = fDirFd;
		Unset();
		return (fCStatus = error);
	}

	if (dir == this) {
		// cleanup after _SetTo()
		_kern_close(dirFD);
	}

	// set close on exec flag on dir FD
	fcntl(fDirFd, F_SETFD, FD_CLOEXEC);

	return B_OK;
}


/*!	\brief Returns a BEntry referring to the directory represented by this object.
	If the initialization of \a entry fails, it is Unset().
	\param entry a pointer to the entry that shall be set to refer to the
		   directory
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a entry.
	- \c B_ENTRY_NOT_FOUND: Directory not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::GetEntry(BEntry* entry) const
{
	if (!entry)
		return B_BAD_VALUE;
	if (InitCheck() != B_OK)
		return B_NO_INIT;
	return entry->SetTo(this, ".", false);
}


/*!	\brief Returns whether the directory represented by this BDirectory is a
	root directory of a volume.
	\return
	- \c true, if the BDirectory is properly initialized and represents a
	  root directory of some volume,
	- \c false, otherwise.
*/
bool
BDirectory::IsRootDirectory() const
{
	// compare the directory's node ID with the ID of the root node of the FS
	bool result = false;
	node_ref ref;
	fs_info info;
	if (GetNodeRef(&ref) == B_OK && fs_stat_dev(ref.device, &info) == 0)
		result = (ref.node == info.root);
	return result;
}


/*! \brief Finds an entry referred to by a path relative to the directory
	represented by this BDirectory.
	\a path may be absolute. If the BDirectory is not properly initialized,
	the entry is search relative to the current directory.
	If the entry couldn't be found, \a entry is Unset().
	\param path the entry's path name. May be relative to this directory or
		   absolute.
	\param entry a pointer to a BEntry to be initialized with the found entry
	\param traverse specifies whether to follow it, if the found entry
		   is a symbolic link.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path or \a entry.
	- \c B_ENTRY_NOT_FOUND: Entry not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_NAME_TOO_LONG: The supplied path name (\a path) is too long.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	- \c B_NOT_A_DIRECTORY: \a path includes a non-directory.
	\note The functionality of this method differs from the one of
		  BEntry::SetTo(BDirectory *, const char *, bool) in that the
		  latter doesn't require the entry to be existent, whereas this
		  function does.
*/
status_t
BDirectory::FindEntry(const char* path, BEntry* entry, bool traverse) const
{
	if (path == NULL || entry == NULL)
		return B_BAD_VALUE;

	entry->Unset();

	// init a potentially abstract entry
	status_t status;
	if (InitCheck() == B_OK)
		status = entry->SetTo(this, path, traverse);
	else
		status = entry->SetTo(path, traverse);

	// fail, if entry is abstract
	if (status == B_OK && !entry->Exists()) {
		status = B_ENTRY_NOT_FOUND;
		entry->Unset();
	}

	return status;
}


/*!	\brief Returns whether this directory or any of its subdirectories
	at any level contain the entry referred to by the supplied path name.
	Only entries that match the node flavor specified by \a nodeFlags are
	considered.
	If the BDirectory is not properly initialized, the method returns \c false.
	A non-absolute path is considered relative to the current directory.

	\note R5's implementation always returns \c true given an absolute path or
	an unitialized directory. This implementation is not compatible with that
	behavior. Instead it converts the path into a BEntry and passes it to the
	other version of Contains().

	\param path the entry's path name. May be relative to this directory or
		   absolute.
	\param nodeFlags Any of the following:
		   - \c B_FILE_NODE: The entry must be a file.
		   - \c B_DIRECTORY_NODE: The entry must be a directory.
		   - \c B_SYMLINK_NODE: The entry must be a symbolic link.
		   - \c B_ANY_NODE: The entry may be of any kind.
	\return
	- \c true, if the entry exists, its kind does match \nodeFlags and the
	  BDirectory is properly initialized and does contain the entry at any
	  level,
	- \c false, otherwise
*/
bool
BDirectory::Contains(const char* path, int32 nodeFlags) const
{
	// check initialization and parameters
	if (InitCheck() != B_OK)
		return false;
	if (!path)
		return true;	// mimic R5 behavior

	// turn the path into a BEntry and let the other version do the work
	BEntry entry;
	if (BPrivate::Storage::is_absolute_path(path))
		entry.SetTo(path);
	else
		entry.SetTo(this, path);

	return Contains(&entry, nodeFlags);
}


/*!	\brief Returns whether this directory or any of its subdirectories
	at any level contain the entry referred to by the supplied BEntry.
	Only entries that match the node flavor specified by \a nodeFlags are
	considered.
	\param entry a BEntry referring to the entry
	\param nodeFlags Any of the following:
		   - \c B_FILE_NODE: The entry must be a file.
		   - \c B_DIRECTORY_NODE: The entry must be a directory.
		   - \c B_SYMLINK_NODE: The entry must be a symbolic link.
		   - \c B_ANY_NODE: The entry may be of any kind.
	\return
	- \c true, if the BDirectory is properly initialized and the entry of the
	  matching kind could be found,
	- \c false, otherwise
*/
bool
BDirectory::Contains(const BEntry* entry, int32 nodeFlags) const
{
	// check, if the entry exists at all
	if (entry == NULL || !entry->Exists() || InitCheck() != B_OK)
		return false;

	if (nodeFlags != B_ANY_NODE) {
		// test the node kind
		bool result = false;
		if ((nodeFlags & B_FILE_NODE) != 0)
			result = entry->IsFile();
		if (!result && (nodeFlags & B_DIRECTORY_NODE) != 0)
			result = entry->IsDirectory();
		if (!result && (nodeFlags & B_SYMLINK_NODE) != 0)
			result = entry->IsSymLink();
		if (!result)
			return false;
	}

	// If the directory is initialized, get the canonical paths of the dir and
	// the entry and check, if the latter is a prefix of the first one.
	BPath dirPath(this, ".", true);
	BPath entryPath(entry);
	if (dirPath.InitCheck() != B_OK || entryPath.InitCheck() != B_OK)
		return false;

	uint32 dirLen = strlen(dirPath.Path());

	if (!strncmp(dirPath.Path(), entryPath.Path(), dirLen)) {
		// if the paths are identical, return a match to stay consistent with
		// BeOS behavior.
		if (entryPath.Path()[dirLen] == '\0' || entryPath.Path()[dirLen] == '/')
			return true;
	}
	return false;
}


/*!	\fn status_t BDirectory::GetStatFor(const char* path, struct stat* st) const
	\brief Returns the stat structure of the entry referred to by the supplied
	path name.
	\param path the entry's path name. May be relative to this directory or
		   absolute, or \c NULL to get the directories stat info.
	\param st a pointer to the stat structure to be filled in by this function
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a st.
	- \c B_ENTRY_NOT_FOUND: Entry not found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_NAME_TOO_LONG: The supplied path name (\a path) is too long.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	- \c B_NOT_A_DIRECTORY: \a path includes a non-directory.
*/
// Implemented as BDirectory::_GetStatFor().


/*!	\brief Returns the BDirectory's next entry as a BEntry.
	Unlike GetNextDirents() this method ignores the entries "." and "..".
	\param entry a pointer to a BEntry to be initialized to the found entry
	\param traverse specifies whether to follow it, if the found entry
		   is a symbolic link.
	\note The iterator used by this method is the same one used by
		  GetNextRef(), GetNextDirents(), Rewind() and CountEntries().
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a entry.
	- \c B_ENTRY_NOT_FOUND: No more entries found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::GetNextEntry(BEntry* entry, bool traverse)
{
	status_t error = (entry ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		entry_ref ref;
		error = GetNextRef(&ref);
		if (error == B_OK)
			error = entry->SetTo(&ref, traverse);
	}
	return error;
}


/*!	\brief Returns the BDirectory's next entry as an entry_ref.
	Unlike GetNextDirents() this method ignores the entries "." and "..".
	\param ref a pointer to an entry_ref to be filled in with the data of the
		   found entry
	\param traverse specifies whether to follow it, if the found entry
		   is a symbolic link.
	\note The iterator used be this method is the same one used by
		  GetNextEntry(), GetNextDirents(), Rewind() and CountEntries().
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref.
	- \c B_ENTRY_NOT_FOUND: No more entries found.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::GetNextRef(entry_ref* ref)
{
	status_t error = (ref ? B_OK : B_BAD_VALUE);
	if (error == B_OK && InitCheck() != B_OK)
		error = B_FILE_ERROR;
	if (error == B_OK) {
		BPrivate::Storage::LongDirEntry entry;
		bool next = true;
		while (error == B_OK && next) {
			if (GetNextDirents(&entry, sizeof(entry), 1) != 1) {
				error = B_ENTRY_NOT_FOUND;
			} else {
				next = (!strcmp(entry.d_name, ".")
					|| !strcmp(entry.d_name, ".."));
			}
		}
		if (error == B_OK) {
			ref->device = entry.d_pdev;
			ref->directory = entry.d_pino;
			error = ref->set_name(entry.d_name);
		}
	}
	return error;
}


/*!	\brief Returns the BDirectory's next entries as dirent structures.
	Unlike GetNextEntry() and GetNextRef(), this method returns also
	the entries "." and "..".
	\param buf a pointer to a buffer to be filled with dirent structures of
		   the found entries
	\param count the maximal number of entries to be returned.
	\note The iterator used by this method is the same one used by
		  GetNextEntry(), GetNextRef(), Rewind() and CountEntries().
	\return
	- The number of dirent structures stored in the buffer, 0 when there are
	  no more entries to be returned.
	- \c B_BAD_VALUE: \c NULL \a buf.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_NAME_TOO_LONG: The entry's name is too long for the buffer.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
int32
BDirectory::GetNextDirents(dirent* buf, size_t bufSize, int32 count)
{
	if (!buf)
		return B_BAD_VALUE;
	if (InitCheck() != B_OK)
		return B_FILE_ERROR;
	return _kern_read_dir(fDirFd, buf, bufSize, count);
}


/*!	\brief Rewinds the directory iterator.
	\return
	- \c B_OK: Everything went fine.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	\see GetNextEntry(), GetNextRef(), GetNextDirents(), CountEntries()
*/
status_t
BDirectory::Rewind()
{
	if (InitCheck() != B_OK)
		return B_FILE_ERROR;
	return _kern_rewind_dir(fDirFd);
}


/*!	\brief Returns the number of entries in this directory.
	CountEntries() uses the directory iterator also used by GetNextEntry(),
	GetNextRef() and GetNextDirents(). It does a Rewind(), iterates through
	the entries and Rewind()s again. The entries "." and ".." are not counted.
	\return
	- the number of entries in the directory (not counting "." and "..").
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	\see GetNextEntry(), GetNextRef(), GetNextDirents(), Rewind()
*/
int32
BDirectory::CountEntries()
{
	status_t error = Rewind();
	if (error != B_OK)
		return error;
	int32 count = 0;
	BPrivate::Storage::LongDirEntry entry;
	while (error == B_OK) {
		if (GetNextDirents(&entry, sizeof(entry), 1) != 1)
			break;
		if (strcmp(entry.d_name, ".") != 0 && strcmp(entry.d_name, "..") != 0)
			count++;
	}
	Rewind();
	return (error == B_OK ? count : error);
}


/*! \brief Creates a new directory.
	If an entry with the supplied name does already exist, the method fails.
	\param path the new directory's path name. May be relative to this
		   directory or absolute.
	\param dir a pointer to a BDirectory to be initialized to the newly
		   created directory. May be \c NULL.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path.
	- \c B_ENTRY_NOT_FOUND: \a path does not refer to a possible entry.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_FILE_EXISTS: An entry with that name does already exist.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::CreateDirectory(const char* path, BDirectory* dir)
{
	if (!path)
		return B_BAD_VALUE;

	// create the dir
	status_t error = _kern_create_dir(fDirFd, path,
		(S_IRWXU | S_IRWXG | S_IRWXO) & ~__gUmask);
	if (error != B_OK)
		return error;

	if (dir == NULL)
		return B_OK;

	// init the supplied BDirectory
	if (InitCheck() != B_OK || BPrivate::Storage::is_absolute_path(path))
		return dir->SetTo(path);

	return dir->SetTo(this, path);
}


/*! \brief Creates a new file.
	If a file with the supplied name does already exist, the method fails,
	unless it is passed \c false to \a failIfExists -- in that case the file
	is truncated to zero size. The new BFile will operate in \c B_READ_WRITE
	mode.
	\param path the new file's path name. May be relative to this
		   directory or absolute.
	\param file a pointer to a BFile to be initialized to the newly
		   created file. May be \c NULL.
	\param failIfExists \c true, if the method should fail when the file
		   already exists, \c false otherwise
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path.
	- \c B_ENTRY_NOT_FOUND: \a path does not refer to a possible entry.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_FILE_EXISTS: A file with that name does already exist and
	  \c true has been passed for \a failIfExists.
	- \c B_IS_A_DIRECTORY: A directory with the supplied name does already
	  exist.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::CreateFile(const char* path, BFile* file, bool failIfExists)
{
	if (!path)
		return B_BAD_VALUE;

	// Let BFile do the dirty job.
	uint32 openMode = B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE
		| (failIfExists ? B_FAIL_IF_EXISTS : 0);
	BFile tmpFile;
	BFile* realFile = file ? file : &tmpFile;
	status_t error = B_OK;
	if (InitCheck() == B_OK && !BPrivate::Storage::is_absolute_path(path))
		error = realFile->SetTo(this, path, openMode);
	else
		error = realFile->SetTo(path, openMode);
	if (error != B_OK && file) // mimic R5 behavior
		file->Unset();
	return error;
}


/*! \brief Creates a new symbolic link.
	If an entry with the supplied name does already exist, the method fails.
	\param path the new symbolic link's path name. May be relative to this
		   directory or absolute.
	\param linkToPath the path the symbolic link shall point to.
	\param dir a pointer to a BSymLink to be initialized to the newly
		   created symbolic link. May be \c NULL.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path or \a linkToPath.
	- \c B_ENTRY_NOT_FOUND: \a path does not refer to a possible entry.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_FILE_EXISTS: An entry with that name does already exist.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BDirectory::CreateSymLink(const char* path, const char* linkToPath,
	BSymLink* link)
{
	if (!path || !linkToPath)
		return B_BAD_VALUE;

	// create the symlink
	status_t error = _kern_create_symlink(fDirFd, path, linkToPath,
		(S_IRWXU | S_IRWXG | S_IRWXO) & ~__gUmask);
	if (error != B_OK)
		return error;

	if (link == NULL)
		return B_OK;

	// init the supplied BSymLink
	if (InitCheck() != B_OK || BPrivate::Storage::is_absolute_path(path))
		return link->SetTo(path);

	return link->SetTo(this, path);
}


/*!	\brief Assigns another BDirectory to this BDirectory.
	If the other BDirectory is uninitialized, this one wi'll be too. Otherwise
	it will refer to the same directory, unless an error occurs.
	\param dir the original BDirectory
	\return a reference to this BDirectory
*/
BDirectory&
BDirectory::operator=(const BDirectory& dir)
{
	if (&dir != this) {	// no need to assign us to ourselves
		Unset();
		if (dir.InitCheck() == B_OK)
			SetTo(&dir, ".");
	}
	return *this;
}


status_t
BDirectory::_GetStatFor(const char* path, struct stat* st) const
{
	if (!st)
		return B_BAD_VALUE;
	if (InitCheck() != B_OK)
		return B_NO_INIT;

	if (path != NULL) {
		if (path[0] == '\0')
			return B_ENTRY_NOT_FOUND;
		return _kern_read_stat(fDirFd, path, false, st, sizeof(struct stat));
	}
	return GetStat(st);
}


status_t
BDirectory::_GetStatFor(const char* path, struct stat_beos* st) const
{
	struct stat newStat;
	status_t error = _GetStatFor(path, &newStat);
	if (error != B_OK)
		return error;

	convert_to_stat_beos(&newStat, st);
	return B_OK;
}


// FBC
void BDirectory::_ErectorDirectory1() {}
void BDirectory::_ErectorDirectory2() {}
void BDirectory::_ErectorDirectory3() {}
void BDirectory::_ErectorDirectory4() {}
void BDirectory::_ErectorDirectory5() {}
void BDirectory::_ErectorDirectory6() {}


//! Closes the BDirectory's file descriptor.
void
BDirectory::close_fd()
{
	if (fDirFd >= 0) {
		_kern_close(fDirFd);
		fDirFd = -1;
	}
	BNode::close_fd();
}


/*!	\brief Returns the BDirectory's file descriptor.
	To be used instead of accessing the BDirectory's private \c fDirFd member
	directly.
	\return the file descriptor, or -1, if not properly initialized.
*/
int
BDirectory::get_fd() const
{
	return fDirFd;
}


//	#pragma mark - C functions


/*!	\brief Creates all missing directories along a given path.
	\param path the directory path name.
	\param mode a permission specification, which shall be used for the
		   newly created directories.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path.
	- \c B_ENTRY_NOT_FOUND: \a path does not refer to a possible entry.
	- \c B_PERMISSION_DENIED: Directory permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NOT_A_DIRECTORY: An entry other than a directory with that name does
	  already exist.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	\todo Check for efficency.
*/
status_t
create_directory(const char* path, mode_t mode)
{
	if (!path)
		return B_BAD_VALUE;

	// That's the strategy: We start with the first component of the supplied
	// path, create a BPath object from it and successively add the following
	// components. Each time we get a new path, we check, if the entry it
	// refers to exists and is a directory. If it doesn't exist, we try
	// to create it. This goes on, until we're done with the input path or
	// an error occurs.
	BPath dirPath;
	char* component;
	int32 nextComponent;
	do {
		// get the next path component
		status_t error = BPrivate::Storage::parse_first_path_component(path,
			component, nextComponent);
		if (error != B_OK)
			return error;

		// append it to the BPath
		if (dirPath.InitCheck() == B_NO_INIT)	// first component
			error = dirPath.SetTo(component);
		else
			error = dirPath.Append(component);
		delete[] component;
		if (error != B_OK)
			return error;
		path += nextComponent;

		// create a BEntry from the BPath
		BEntry entry;
		error = entry.SetTo(dirPath.Path(), true);
		if (error != B_OK)
			return error;

		// check, if it exists
		if (entry.Exists()) {
			// yep, it exists
			if (!entry.IsDirectory())	// but is no directory
				return B_NOT_A_DIRECTORY;
		} else {
			// it doesn't exist -- create it
			error = _kern_create_dir(-1, dirPath.Path(), mode & ~__gUmask);
			if (error != B_OK)
				return error;
		}
	} while (nextComponent != 0);
	return B_OK;
}


// #pragma mark - symbol versions


#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if __GNUC__ == 2	// gcc 2

	B_DEFINE_SYMBOL_VERSION("_GetStatFor__C10BDirectoryPCcP4stat",
		"GetStatFor__C10BDirectoryPCcP4stat@@LIBBE_TEST");

#	else	// gcc 4

	B_DEFINE_SYMBOL_VERSION("_ZNK10BDirectory11_GetStatForEPKcP4stat",
		"_ZNK10BDirectory10GetStatForEPKcP4stat@@LIBBE_TEST");

#	endif	// gcc 4
#else	// !HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if __GNUC__ == 2	// gcc 2

	// BeOS compatible GetStatFor()
	B_DEFINE_SYMBOL_VERSION("_GetStatFor__C10BDirectoryPCcP9stat_beos",
		"GetStatFor__C10BDirectoryPCcP4stat@LIBBE_BASE");

	// Haiku GetStatFor()
	B_DEFINE_SYMBOL_VERSION("_GetStatFor__C10BDirectoryPCcP4stat",
		"GetStatFor__C10BDirectoryPCcP4stat@@LIBBE_1_ALPHA1");

#	else	// gcc 4

	// BeOS compatible GetStatFor()
	B_DEFINE_SYMBOL_VERSION("_ZNK10BDirectory11_GetStatForEPKcP9stat_beos",
		"_ZNK10BDirectory10GetStatForEPKcP4stat@LIBBE_BASE");

	// Haiku GetStatFor()
	B_DEFINE_SYMBOL_VERSION("_ZNK10BDirectory11_GetStatForEPKcP4stat",
		"_ZNK10BDirectory10GetStatForEPKcP4stat@@LIBBE_1_ALPHA1");

#	endif	// gcc 4
#endif	// !HAIKU_TARGET_PLATFORM_LIBBE_TEST
