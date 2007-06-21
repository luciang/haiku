// Volume.cpp

#include "Volume.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "kernel_emu.h"

// constructor
Volume::Volume(FileSystem* fileSystem, dev_t id)
	: fFileSystem(fileSystem),
	  fID(id)
{
}

// destructor
Volume::~Volume()
{
}

// GetFileSystem
UserlandFS::FileSystem*
Volume::GetFileSystem() const
{
	return fFileSystem;
}

// GetID
dev_t
Volume::GetID() const
{
	return fID;
}


// #pragma mark - FS


// Mount
status_t
Volume::Mount(const char* device, uint32 flags, const char* parameters,
	ino_t* rootID)
{
	return B_BAD_VALUE;
}

// Unmount
status_t
Volume::Unmount()
{
	return B_BAD_VALUE;
}

// Sync
status_t
Volume::Sync()
{
	return B_BAD_VALUE;
}

// ReadFSInfo
status_t
Volume::ReadFSInfo(fs_info* info)
{
	return B_BAD_VALUE;
}

// WriteFSInfo
status_t
Volume::WriteFSInfo(const struct fs_info* info, uint32 mask)
{
	return B_BAD_VALUE;
}


// #pragma mark - vnodes


// Lookup
status_t
Volume::Lookup(fs_vnode dir, const char* entryName, ino_t* vnid, int* type)
{
	return B_BAD_VALUE;
}

// LookupNoType
status_t
Volume::LookupNoType(fs_vnode dir, const char* entryName, ino_t* vnid)
{
	int type;
	return Lookup(dir, entryName, vnid, &type);
}

// GetVNodeName
status_t
Volume::GetVNodeName(fs_vnode node, char* buffer, size_t bufferSize)
{
	// stat the node to get its ID
	struct stat st;
	status_t error = ReadStat(node, &st);
	if (error != B_OK)
		return error;

	// look up the parent directory
	ino_t parentID;
	error = LookupNoType(node, "..", &parentID);
	if (error != B_OK)
		return error;

	// get the parent node handle
	fs_vnode parentNode;
	error = UserlandFS::KernelEmu::get_vnode(GetID(), parentID, &parentNode);
	// Lookup() has already called get_vnode() for us, so we need to put it once
	UserlandFS::KernelEmu::put_vnode(GetID(), parentID);
	if (error != B_OK)
		return error;

	// open the parent dir
	fs_cookie cookie;
	error = OpenDir(parentNode, &cookie);
	if (error == B_OK) {

		while (true) {
			// read an entry
			char _entry[sizeof(struct dirent) + B_FILE_NAME_LENGTH];
			struct dirent* entry = (struct dirent*)_entry;
			uint32 num;

			error = ReadDir(parentNode, cookie, entry, sizeof(_entry), 1, &num);

			if (error != B_OK)
				break;
			if (num == 0) {
				error = B_ENTRY_NOT_FOUND;
				break;
			}

			// found an entry for our node?
			if (st.st_ino == entry->d_ino) {
				// yep, copy the entry name
				size_t nameLen = strnlen(entry->d_name, B_FILE_NAME_LENGTH);
				if (nameLen < bufferSize) {
					memcpy(buffer, entry->d_name, nameLen);
					buffer[nameLen] = '\0';
				} else
					error = B_BUFFER_OVERFLOW;
				break;
			}
		}

		// close the parent dir
		CloseDir(parentNode, cookie);
		FreeDirCookie(parentNode, cookie);
	}

	// put the parent node
	UserlandFS::KernelEmu::put_vnode(GetID(), parentID);

	return error;
}

// ReadVNode
status_t
Volume::ReadVNode(ino_t vnid, bool reenter, fs_vnode* node)
{
	return B_BAD_VALUE;
}

// WriteVNode
status_t
Volume::WriteVNode(fs_vnode node, bool reenter)
{
	return B_BAD_VALUE;
}

// RemoveVNode
status_t
Volume::RemoveVNode(fs_vnode node, bool reenter)
{
	return B_BAD_VALUE;
}


// #pragma mark - nodes


// IOCtl
status_t
Volume::IOCtl(fs_vnode node, fs_cookie cookie, uint32 command, void *buffer,
	size_t size)
{
	return B_BAD_VALUE;
}

// SetFlags
status_t
Volume::SetFlags(fs_vnode node, fs_cookie cookie, int flags)
{
	return B_BAD_VALUE;
}

// Select
status_t
Volume::Select(fs_vnode node, fs_cookie cookie, uint8 event, uint32 ref,
	selectsync* sync)
{
	return B_BAD_VALUE;
}

// Deselect
status_t
Volume::Deselect(fs_vnode node, fs_cookie cookie, uint8 event, selectsync* sync)
{
	return B_BAD_VALUE;
}

// FSync
status_t
Volume::FSync(fs_vnode node)
{
	return B_BAD_VALUE;
}

// ReadSymlink
status_t
Volume::ReadSymlink(fs_vnode node, char* buffer, size_t bufferSize,
	size_t* bytesRead)
{
	return B_BAD_VALUE;
}

// CreateSymlink
status_t
Volume::CreateSymlink(fs_vnode dir, const char* name, const char* target,
	int mode)
{
	return B_BAD_VALUE;
}

// Link
status_t
Volume::Link(fs_vnode dir, const char* name, fs_vnode node)
{
	return B_BAD_VALUE;
}

// Unlink
status_t
Volume::Unlink(fs_vnode dir, const char* name)
{
	return B_BAD_VALUE;
}

// Rename
status_t
Volume::Rename(fs_vnode oldDir, const char* oldName, fs_vnode newDir,
	const char* newName)
{
	return B_BAD_VALUE;
}

// Access
status_t
Volume::Access(fs_vnode node, int mode)
{
	return B_BAD_VALUE;
}

// ReadStat
status_t
Volume::ReadStat(fs_vnode node, struct stat* st)
{
	return B_BAD_VALUE;
}

// WriteStat
status_t
Volume::WriteStat(fs_vnode node, const struct stat *st, uint32 mask)
{
	return B_BAD_VALUE;
}


// #pragma mark - files


// Create
status_t
Volume::Create(fs_vnode dir, const char* name, int openMode, int mode,
	fs_cookie* cookie, ino_t* vnid)
{
	return B_BAD_VALUE;
}

// Open
status_t
Volume::Open(fs_vnode node, int openMode, fs_cookie* cookie)
{
	return B_BAD_VALUE;
}

// Close
status_t
Volume::Close(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// FreeCookie
status_t
Volume::FreeCookie(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// Read
status_t
Volume::Read(fs_vnode node, fs_cookie cookie, off_t pos, void* buffer,
	size_t bufferSize, size_t* bytesRead)
{
	return B_BAD_VALUE;
}

// Write
status_t
Volume::Write(fs_vnode node, fs_cookie cookie, off_t pos, const void* buffer,
	size_t bufferSize, size_t* bytesWritten)
{
	return B_BAD_VALUE;
}


// #pragma mark - directories


// CreateDir
status_t
Volume::CreateDir(fs_vnode dir, const char* name, int mode, ino_t *newDir)
{
	return B_BAD_VALUE;
}

// RemoveDir
status_t
Volume::RemoveDir(fs_vnode dir, const char* name)
{
	return B_BAD_VALUE;
}

// OpenDir
status_t
Volume::OpenDir(fs_vnode node, fs_cookie* cookie)
{
	return B_BAD_VALUE;
}

// CloseDir
status_t
Volume::CloseDir(fs_vnode node, fs_vnode cookie)
{
	return B_BAD_VALUE;
}

// FreeDirCookie
status_t
Volume::FreeDirCookie(fs_vnode node, fs_vnode cookie)
{
	return B_BAD_VALUE;
}

// ReadDir
status_t
Volume::ReadDir(fs_vnode node, fs_vnode cookie, void* buffer, size_t bufferSize,
	uint32 count, uint32* countRead)
{
	return B_BAD_VALUE;
}

// RewindDir
status_t
Volume::RewindDir(fs_vnode node, fs_vnode cookie)
{
	return B_BAD_VALUE;
}


// #pragma mark - attribute directories


// OpenAttrDir
status_t
Volume::OpenAttrDir(fs_vnode node, fs_cookie *cookie)
{
	return B_BAD_VALUE;
}

// CloseAttrDir
status_t
Volume::CloseAttrDir(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// FreeAttrDirCookie
status_t
Volume::FreeAttrDirCookie(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// ReadAttrDir
status_t
Volume::ReadAttrDir(fs_vnode node, fs_cookie cookie, void* buffer,
	size_t bufferSize, uint32 count, uint32* countRead)
{
	return B_BAD_VALUE;
}

// RewindAttrDir
status_t
Volume::RewindAttrDir(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}


// #pragma mark - attributes


// CreateAttr
status_t
Volume::CreateAttr(fs_vnode node, const char* name, uint32 type, int openMode,
	fs_cookie* cookie)
{
	return B_BAD_VALUE;
}

// OpenAttr
status_t
Volume::OpenAttr(fs_vnode node, const char* name, int openMode,
	fs_cookie* cookie)
{
	return B_BAD_VALUE;
}

// CloseAttr
status_t
Volume::CloseAttr(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// FreeAttrCookie
status_t
Volume::FreeAttrCookie(fs_vnode node, fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// ReadAttr
status_t
Volume::ReadAttr(fs_vnode node, fs_cookie cookie, off_t pos, void* buffer,
	size_t bufferSize, size_t* bytesRead)
{
	return B_BAD_VALUE;
}

// WriteAttr
status_t
Volume::WriteAttr(fs_vnode node, fs_cookie cookie, off_t pos,
	const void* buffer, size_t bufferSize, size_t* bytesWritten)
{
	return B_BAD_VALUE;
}

// ReadAttrStat
status_t
Volume::ReadAttrStat(fs_vnode node, fs_cookie cookie, struct stat *st)
{
	return B_BAD_VALUE;
}

// WriteAttrStat
status_t
Volume::WriteAttrStat(fs_vnode node, fs_cookie cookie, const struct stat* st,
	int statMask)
{
	return B_BAD_VALUE;
}

// RenameAttr
status_t
Volume::RenameAttr(fs_vnode oldNode, const char* oldName, fs_vnode newNode,
	const char* newName)
{
	return B_BAD_VALUE;
}

// RemoveAttr
status_t
Volume::RemoveAttr(fs_vnode node, const char* name)
{
	return B_BAD_VALUE;
}


// #pragma mark - indices


// OpenIndexDir
status_t
Volume::OpenIndexDir(fs_cookie *cookie)
{
	return B_BAD_VALUE;
}

// CloseIndexDir
status_t
Volume::CloseIndexDir(fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// FreeIndexDirCookie
status_t
Volume::FreeIndexDirCookie(fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// ReadIndexDir
status_t
Volume::ReadIndexDir(fs_cookie cookie, void* buffer, size_t bufferSize,
	uint32 count, uint32* countRead)
{
	return B_BAD_VALUE;
}

// RewindIndexDir
status_t
Volume::RewindIndexDir(fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// CreateIndex
status_t
Volume::CreateIndex(const char* name, uint32 type, uint32 flags)
{
	return B_BAD_VALUE;
}

// RemoveIndex
status_t
Volume::RemoveIndex(const char* name)
{
	return B_BAD_VALUE;
}

// ReadIndexStat
status_t
Volume::ReadIndexStat(const char *name, struct stat *st)
{
	return B_BAD_VALUE;
}


// #pragma mark - queries


// OpenQuery
status_t
Volume::OpenQuery(const char* queryString, uint32 flags, port_id port,
	uint32 token, fs_cookie *cookie)
{
	return B_BAD_VALUE;
}

// CloseQuery
status_t
Volume::CloseQuery(fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// FreeQueryCookie
status_t
Volume::FreeQueryCookie(fs_cookie cookie)
{
	return B_BAD_VALUE;
}

// ReadQuery
status_t
Volume::ReadQuery(fs_cookie cookie, void* buffer, size_t bufferSize,
	uint32 count, uint32* countRead)
{
	return B_BAD_VALUE;
}

// RewindQuery
status_t
Volume::RewindQuery(fs_cookie cookie)
{
	return B_BAD_VALUE;
}

