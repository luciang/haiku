/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include "Directory.h"
#include "File.h"

#include <StorageDefs.h>
#include <util/kernel_cpp.h>

#include <string.h>


namespace BFS {

Directory::Directory(Volume &volume, block_run run)
	:
	fStream(volume, run),
	fTree(&fStream)
{
}


Directory::Directory(Volume &volume, off_t id)
	:
	fStream(volume, id),
	fTree(&fStream)
{
}


Directory::Directory(const Stream &stream)
	:
	fStream(stream),
	fTree(&fStream)
{
}


Directory::~Directory()
{
}


status_t 
Directory::InitCheck()
{
	return fStream.InitCheck();
}


status_t 
Directory::Open(void **_cookie, int mode)
{
	*_cookie = (void *)new TreeIterator(&fTree);
	if (*_cookie == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


status_t 
Directory::Close(void *cookie)
{
	delete (TreeIterator *)cookie;
	return B_OK;
}


Node *
Directory::Lookup(const char *name, bool traverseLinks)
{
	off_t id;
	if (fTree.Find((uint8 *)name, strlen(name), &id) < B_OK)
		return NULL;

	Node *node = Stream::NodeFactory(fStream.GetVolume(), id);
	if (node->Type() == S_IFLNK) {
		// ToDo: support links!
		delete node;
		return NULL;
	}

	return node;
}


status_t 
Directory::GetNextEntry(void *cookie, char *name, size_t size)
{
	TreeIterator *iterator = (TreeIterator *)cookie;
	uint16 length;
	off_t id;

	return iterator->GetNextEntry(name, &length, size, &id);
}


status_t 
Directory::GetNextNode(void *cookie, Node **_node)
{
	TreeIterator *iterator = (TreeIterator *)cookie;
	char name[B_FILE_NAME_LENGTH];
	uint16 length;
	off_t id;

	status_t status = iterator->GetNextEntry(name, &length, sizeof(name), &id);
	if (status != B_OK)
		return status;

	*_node = Stream::NodeFactory(fStream.GetVolume(), id);
	if (*_node == NULL)
		return B_ERROR;

	return B_OK;
}


status_t 
Directory::Rewind(void *cookie)
{
	TreeIterator *iterator = (TreeIterator *)cookie;

	return iterator->Rewind();
}


status_t 
Directory::GetName(char *name, size_t size) const
{
	if (fStream.inode_num == fStream.GetVolume().Root()) {
		strlcpy(name, fStream.GetVolume().SuperBlock().name, size);
		return B_OK;
	}

	return fStream.GetName(name, size);
}

}	// namespace BFS
