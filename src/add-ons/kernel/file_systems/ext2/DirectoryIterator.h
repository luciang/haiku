/*
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */
#ifndef DIRECTORY_ITERATOR_H
#define DIRECTORY_ITERATOR_H


#include <SupportDefs.h>


class Inode;

class DirectoryIterator {
public:
						DirectoryIterator(Inode* inode);
						~DirectoryIterator();

			status_t	GetNext(char* name, size_t* _nameLength, ino_t* id);

			status_t	Rewind();

private:
						DirectoryIterator(const DirectoryIterator&);
						DirectoryIterator &operator=(const DirectoryIterator&);
							// no implementation

private:
	Inode*				fInode;
	off_t				fOffset;
};

#endif	// DIRECTORY_ITERATOR_H
