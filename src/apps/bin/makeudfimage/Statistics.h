//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Tyler Dauwalder, tyler@dauwalder.net
//----------------------------------------------------------------------

/*! \file Statistics.h

	BDataIO wrapper around a given attribute for a file. (declarations)
*/

#ifndef _STATISTICS_H
#define _STATISTICS_H

#include <OS.h>
#include <SupportDefs.h>

class Statistics {
public:
	Statistics();
	void Reset();
	
	time_t StartTime() const { return fStartTime; }
	time_t ElapsedTime() const { return real_time_clock() - fStartTime; }
	
	void AddDirectory() { fDirectories++; }
	void AddFile() { fFiles++; }
	void AddSymlink() { fSymlinks++; }
	void AddAttribute() { fAttributes++; }
	
	void AddDirectoryBytes(uint32 count) { fDirectoryBytes += count; }
	void AddFileBytes(uint32 count) { fFileBytes += count; }

	uint64 Directories() const { return fDirectories; }
	uint64 Files() const { return fFiles; }
	uint64 Symlinks() const { return fSymlinks; }
	uint64 Attributes() const { return fAttributes; }
	
	uint64 DirectoryBytes() const { return fDirectoryBytes; }
	uint64 FileBytes() const { return fFileBytes; }
private:
	uint64 fDirectories;
	uint64 fFiles;
	uint64 fSymlinks;
	uint64 fAttributes;
	uint64 fDirectoryBytes;
	uint64 fFileBytes;
	time_t fStartTime;
};

#endif	// _STATISTICS_H
