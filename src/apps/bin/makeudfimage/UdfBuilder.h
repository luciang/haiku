//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Tyler Dauwalder, tyler@dauwalder.net
//----------------------------------------------------------------------

/*! \file UdfBuilder.h

	Main UDF image building class interface declarations.
*/

#ifndef _UDF_BUILDER_H
#define _UDF_BUILDER_H

#include <Entry.h>
#include <list>
#include <Node.h>
#include <stdarg.h>
#include <string>
#include <SupportDefs.h>

#include "Allocator.h"
#include "FileStream.h"
#include "PhysicalPartitionAllocator.h"
#include "ProgressListener.h"
#include "Statistics.h"
#include "UdfString.h"

/*! \brief Handy struct into which all the interesting information about
	a processed directory, file, or whatever is placed by the corresponding
	UdfBuilder::_Process*() function.
*/
struct node_data {
	Udf::long_address icbAddress;			//!< Udf icb address
	std::list<Udf::long_address> udfData;	//!< Dataspace for node in Udf partition space
	std::list<Udf::extent_address> isoData;	//!< Dataspace for node in physical space
};

class UdfBuilder {
public:
	UdfBuilder(const char *outputFile, uint32 blockSize, bool doUdf,
	           bool doIso, const char *udfVolumeName, const char *isoVolumeName,
	           const char *rootDirectory, const ProgressListener &listener);
	status_t InitCheck() const;
	status_t Build();
private:
	//! Maximum length of string generated by calls to any _Print*() functions
	static const int kMaxUpdateStringLength = 1024;

	FileStream& _OutputFile() { return fOutputFile; }
	uint32 _BlockSize() const { return fBlockSize; }
	uint32 _BlockShift() const { return fBlockShift; }
	bool _DoUdf() const { return fDoUdf; }
	bool _DoIso() const { return fDoIso; }
	Udf::String& _UdfVolumeName() { return fUdfVolumeName; }
	Udf::String& _IsoVolumeName() { return fIsoVolumeName; }
	BEntry& _RootDirectory() { return fRootDirectory; }
	Allocator& _Allocator() { return fAllocator; }
	PhysicalPartitionAllocator& _PartitionAllocator() { return fPartitionAllocator; }
	Statistics& _Stats() { return fStatistics; }
	time_t _BuildTime() const { return fBuildTime; }
	Udf::timestamp& _BuildTimeStamp() { return fBuildTimeStamp; }
	uint64 _NextUniqueId() { return fNextUniqueId++; }

	void _SetBuildTime(time_t time);

	status_t _FormatString(char *message, const char *formatString, va_list arguments) const;
	void _PrintError(const char *formatString, ...) const;
	void _PrintWarning(const char *formatString, ...) const;
	void _PrintUpdate(VerbosityLevel level, const char *formatString, ...) const;
	
	status_t _ProcessDirectory(BEntry &entry, const char *path, struct stat stats,
	                           node_data &node, bool isRootDirectory = false);
	status_t _ProcessFile(BEntry &file);
	status_t _ProcessSymlink(BEntry &symlink);
	status_t _ProcessAttributes(BNode &node);

	status_t fInitStatus;
	FileStream fOutputFile;
	std::string fOutputFilename;
	uint32 fBlockSize;
	uint32 fBlockShift;
	bool fDoUdf;
	bool fDoIso;
	Udf::String fUdfVolumeName;
	Udf::String fIsoVolumeName;
	BEntry fRootDirectory;
	std::string fRootDirectoryName;
	const ProgressListener &fListener;
	Allocator fAllocator;
	PhysicalPartitionAllocator fPartitionAllocator;
	Statistics fStatistics;
	time_t fBuildTime;
	Udf::timestamp fBuildTimeStamp;
	uint64 fNextUniqueId;
};


#endif	// _UDF_BUILDER_H
