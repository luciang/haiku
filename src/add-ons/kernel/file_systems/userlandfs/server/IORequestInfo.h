/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef USERLAND_FS_IO_REQUEST_INFO_H
#define USERLAND_FS_IO_REQUEST_INFO_H

#include <SupportDefs.h>


namespace UserlandFS {

struct IORequestInfo {
	off_t	offset;
	size_t	length;
	int32	id;
	bool	isWrite;

	IORequestInfo(int32 id, bool isWrite, off_t offset, size_t length)
		:
		offset(offset),
		length(length),
		id(id),
		isWrite(isWrite)
	{
	}

	IORequestInfo(const IORequestInfo& other)
		:
		offset(other.offset),
		length(other.length),
		id(other.id),
		isWrite(other.isWrite)
	{
	}
};

}	// namespace UserlandFS


using UserlandFS::IORequestInfo;


#endif	// USERLAND_FS_IO_REQUEST_INFO_H
