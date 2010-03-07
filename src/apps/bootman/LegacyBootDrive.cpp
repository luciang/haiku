/*
 * Copyright 2008, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 * 
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 */


#include "LegacyBootDrive.h"


#include <Catalog.h>
#include <Drivers.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <Partition.h>
#include <Path.h>
#include <String.h>

#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ByteOrder.h>
#include <DataIO.h>
#include <File.h>
#include <String.h>
#include <UTF8.h>

#include "BootLoader.h"


#define TR_CONTEXT "LegacyBootDrive"
#define USE_SECOND_DISK 0
#define GET_FIRST_BIOS_DRIVE 1


class Buffer : public BMallocIO 
{
public:
	Buffer() : BMallocIO() {}
	bool WriteInt8(int8 value);
	bool WriteInt16(int16 value);
	bool WriteInt32(int32 value);
	bool WriteInt64(int64 value);
	bool WriteString(const char* value);
	bool Align(int16 alignment);
	bool Fill(int16 size, int8 fillByte);
};


bool
Buffer::WriteInt8(int8 value) 
{
	return Write(&value, sizeof(value)) == sizeof(value);
}


bool
Buffer::WriteInt16(int16 value)
{
	return WriteInt8(value & 0xff)
		&& WriteInt8(value >> 8);
}


bool
Buffer::WriteInt32(int32 value)
{
	return WriteInt8(value & 0xff)
		&& WriteInt8(value >> 8)
		&& WriteInt8(value >> 16)
		&& WriteInt8(value >> 24);
}


bool
Buffer::WriteInt64(int64 value)
{
	return WriteInt32(value) && WriteInt32(value >> 32);
}


bool
Buffer::WriteString(const char* value)
{
	int len = strlen(value) + 1;
	return WriteInt8(len)
		&& Write(value, len) == len;
}


bool
Buffer::Align(int16 alignment)
{
	if ((Position() % alignment) == 0)
		return true;
	return Fill(alignment - (Position() % alignment), 0);
}


bool
Buffer::Fill(int16 size, int8 fillByte)
{
	for (int i = 0; i < size; i ++) {
		if (!WriteInt8(fillByte))
			return false;
	}
	return true;
}


class PartitionRecorder : public BDiskDeviceVisitor
{
public:
	PartitionRecorder(BMessage* settings, int8 drive);

	virtual bool Visit(BDiskDevice* device);	
	virtual bool Visit(BPartition* partition, int32 level);

	bool HasPartitions() const;	
	off_t FirstOffset() const;
	
private:
	bool _Record(BPartition* partition);

	BMessage* fSettings;
	int8 fDrive;
	int32 fIndex;
	off_t fFirstOffset;
};


PartitionRecorder::PartitionRecorder(BMessage* settings, int8 drive)
	: fSettings(settings)
	, fDrive(drive)
	, fIndex(0)
	, fFirstOffset(LONGLONG_MAX)
{
}


bool
PartitionRecorder::Visit(BDiskDevice* device)
{
	return false;
}


bool
PartitionRecorder::Visit(BPartition* partition, int32 level)
{
	return _Record(partition);
}


bool
PartitionRecorder::HasPartitions() const
{
	return fFirstOffset != LONGLONG_MAX;
}


off_t
PartitionRecorder::FirstOffset() const
{
	return fFirstOffset;
}


bool
PartitionRecorder::_Record(BPartition* partition)
{
	if (partition->ContainsPartitioningSystem())
		return false;
	
	BPath partitionPath;
	partition->GetPath(&partitionPath);

	BString buffer;
	const char* name = partition->ContentName();
	if (name == NULL) {
		fIndex ++;
		BString number;
		number << fIndex;
		buffer << TR_CMT("Unnamed %d",
			"Default name of a partition whose name could not be read from "
			"disk");
		buffer.ReplaceFirst("%d", number);
		name = buffer.String();
	}
	
	const char* type = partition->Type();
	if (type == NULL)
		type = TR_CMT("Unknown", "Text is shown for an unknown partition type");
		
	BMessage message;
	// Data as required by BootLoader.h
	message.AddBool("show", true);
	message.AddString("name", name);
	message.AddString("type", type);
	message.AddString("path", partitionPath.Path());
	message.AddInt8("drive", fDrive);
	message.AddInt64("size", partition->Size());	
	// Specific data
	off_t offset = partition->Offset();
	message.AddInt64("offset", offset);
	
	fSettings->AddMessage("partition", &message);

	if (offset < fFirstOffset)
		fFirstOffset = offset;
	
	return false;
}


LegacyBootDrive::LegacyBootDrive()
{
}


LegacyBootDrive::~LegacyBootDrive()
{
}


bool
LegacyBootDrive::IsBootMenuInstalled(BMessage* settings)
{
	// TODO detect bootman
	return false;
}


status_t
LegacyBootDrive::ReadPartitions(BMessage *settings)
{
	BDiskDeviceRoster diskDeviceRoster;
	BDiskDevice device;
	bool diskFound = false;
	while (diskDeviceRoster.GetNextDevice(&device) == B_OK) {
		
		BPath path;
		status_t status = device.GetPath(&path); 
		if (status != B_OK)
			return status;
				
		// skip not from BIOS bootable drives
		int8 drive;
		if (!_GetBiosDrive(path.Path(), &drive))
			continue;
		
		PartitionRecorder recorder(settings, drive);
		device.VisitEachDescendant(&recorder);

		if (!diskFound) {
			settings->AddString("disk", path.Path());
			diskFound = true;
			
			#if !USE_SECOND_DISK
				// Enough space to write boot menu to drive?
				// (ignored in test build)
				off_t size = sizeof(kBootLoader);
				if (!recorder.HasPartitions() || recorder.FirstOffset() < size)
					return kErrorBootSectorTooSmall;
	
				// TODO remove when booting from all drives works
				break;
			#endif
		}
	}	

	#if USE_SECOND_DISK
		// for testing only write boot menu to second hdd
		settings->ReplaceString("disk", "/dev/disk/ata/1/master/raw");
	#endif
	
	if (diskFound)
		return B_OK;
	else
		return B_ERROR;
}


status_t
LegacyBootDrive::WriteBootMenu(BMessage *settings)
{
	BString path;
	if (settings->FindString("disk", &path) != B_OK)
		return B_BAD_VALUE;
	
	int32 defaultPartitionIndex;
	if (settings->FindInt32("defaultPartition", &defaultPartitionIndex) != B_OK)
		return B_BAD_VALUE;
	
	int32 timeout;
	if (settings->FindInt32("timeout", &timeout) != B_OK)
		return B_BAD_VALUE;
	
	int fd = open(path.String(), O_RDWR);
	if (fd < 0)
		return B_IO_ERROR;
	
	MasterBootRecord oldMBR;
	if (read(fd, &oldMBR, sizeof(oldMBR)) != sizeof(oldMBR)) {
		close(fd);
		return B_IO_ERROR;
	}
	
	if (!_IsValid(&oldMBR)) {
		close(fd);
		return B_BAD_VALUE;
	}
	
	Buffer newBootLoader;
	ssize_t size = sizeof(kBootLoader);
	if (newBootLoader.Write(kBootLoader, size) != size) {
		close(fd);
		return B_NO_MEMORY;
	}
	
	MasterBootRecord* newMBR = (MasterBootRecord*)newBootLoader.BMallocIO::Buffer();
	_CopyPartitionTable(newMBR, &oldMBR);
	
	int menuEntries = 0;
	int defaultMenuEntry = 0;
	BMessage partition;
	int32 index;
	for (index = 0; settings->FindMessage("partition", index, &partition) == B_OK; index ++) {
		bool show;
		partition.FindBool("show", &show);
		if (!show)
			continue;
		if (index == defaultPartitionIndex)
			defaultMenuEntry = menuEntries;
		
		menuEntries ++;
	}
	newBootLoader.WriteInt16(menuEntries);
	newBootLoader.WriteInt16(defaultMenuEntry);
	newBootLoader.WriteInt16(timeout);
	
	
	for (index = 0; settings->FindMessage("partition", index, &partition) == B_OK; index ++) {
		bool show;
		BString name;
		BString path;
		int64 offset;
		int8 drive;
		partition.FindBool("show", &show);
		partition.FindString("name", &name);
		partition.FindString("path", &path);
		// LegacyBootDrive specific data
		partition.FindInt64("offset", &offset);
		partition.FindInt8("drive", &drive);
		if (!show)
			continue;
		
		BString biosName;
		_ConvertToBIOSText(name.String(), biosName);

		newBootLoader.WriteString(biosName.String());
		newBootLoader.WriteInt8(drive);
		newBootLoader.WriteInt64(offset / kBlockSize);
	}	

	if (!newBootLoader.Align(kBlockSize)) {
		close(fd);
		return B_ERROR;
	}
	
	lseek(fd, 0, SEEK_SET);
	const uint8* buffer = (uint8*)newBootLoader.BMallocIO::Buffer();
	status_t status = _WriteBlocks(fd, buffer, newBootLoader.Position());
	close(fd);
	return status;
}


status_t
LegacyBootDrive::SaveMasterBootRecord(BMessage* settings, BFile* file)
{	
	BString path;

	if (settings->FindString("disk", &path) != B_OK)
		return B_BAD_VALUE;
	
	int fd = open(path.String(), O_RDONLY);
	if (fd < 0)
		return B_IO_ERROR;
	
	ssize_t size = kBlockSize * kNumberOfBootLoaderBlocks;
	uint8* buffer = new(std::nothrow) uint8[size];
	if (buffer == NULL) {
		close(fd);		
		return B_NO_MEMORY;
	}

	status_t status = _ReadBlocks(fd, buffer, size);
	if (status != B_OK) {
		close(fd);
		delete[] buffer;
		return B_IO_ERROR;
	}		

	MasterBootRecord* mbr = (MasterBootRecord*)buffer;
	if (!_IsValid(mbr)) {
		close(fd);
		delete[] buffer;
		return B_BAD_VALUE;
	}

	if (file->Write(buffer, size) != size)
		status = B_IO_ERROR;
	delete[] buffer;		
	close(fd);
	return status;
}


status_t
LegacyBootDrive::RestoreMasterBootRecord(BMessage* settings, BFile* file)
{
	BString path;
	if (settings->FindString("disk", &path) != B_OK)
		return B_BAD_VALUE;
	
	int fd = open(path.String(), O_RDWR);
	if (fd < 0)
		return B_IO_ERROR;
	
	MasterBootRecord oldMBR;
	if (read(fd, &oldMBR, sizeof(oldMBR)) != sizeof(oldMBR)) {
		close(fd);
		return B_IO_ERROR;	
	}
	if (!_IsValid(&oldMBR)) {
		close(fd);
		return B_BAD_VALUE;
	}
	
	lseek(fd, 0, SEEK_SET);	
	
	size_t size = kBlockSize * kNumberOfBootLoaderBlocks;
	uint8* buffer = new(std::nothrow) uint8[size];
	if (buffer == NULL) {
		close(fd);		
		return B_NO_MEMORY;
	}

	if (file->Read(buffer, size) != (ssize_t)size) {
		close(fd);
		delete[] buffer;
		return B_IO_ERROR;
	}		

	MasterBootRecord* newMBR = (MasterBootRecord*)buffer;
	if (!_IsValid(newMBR)) {
		close(fd);
		delete[] buffer;
		return B_BAD_VALUE;
	}
	
	_CopyPartitionTable(newMBR, &oldMBR);

	status_t status = _WriteBlocks(fd, buffer, size);
	delete[] buffer;		
	close(fd);
	return status;
}


status_t
LegacyBootDrive::GetDisplayText(const char* text, BString& displayText)
{
	BString biosText;
	if (!_ConvertToBIOSText(text, biosText)) {
		displayText = "???";
		return B_ERROR;
	}

	// convert back to UTF-8
	int32 biosTextLength = biosText.Length();
	int32 bufferLength = strlen(text);
	char* buffer = displayText.LockBuffer(bufferLength + 1);
	int32 state = 0;
	if (convert_to_utf8(B_MS_DOS_CONVERSION,
		biosText.String(), &biosTextLength,
		buffer, &bufferLength, &state) != B_OK) {
		displayText.UnlockBuffer(0);
		displayText = "???";
		return B_ERROR;
	}

	buffer[bufferLength] = '\0';
	bufferLength ++;
	displayText.UnlockBuffer(bufferLength);
	return B_OK;
}


bool
LegacyBootDrive::_ConvertToBIOSText(const char* text, BString& biosText)
{
	// convert text in UTF-8 to 'code page 437'
	int32 textLength = strlen(text);

	int32 biosTextLength = textLength;
	char* buffer = biosText.LockBuffer(biosTextLength + 1);
	if (buffer == NULL) {
		biosText.UnlockBuffer(0);
		return false;
	}

	int32 state = 0;
	if (convert_from_utf8(B_MS_DOS_CONVERSION, text, &textLength,
		buffer, &biosTextLength, &state) != B_OK) {
		biosText.UnlockBuffer(0);
		return false;
	}

	buffer[biosTextLength] = '\0';
	biosTextLength ++;
	biosText.UnlockBuffer(biosTextLength);
	return biosTextLength <= kMaxBootMenuItemLength;
}


bool
LegacyBootDrive::_GetBiosDrive(const char* device, int8* drive)
{
	#if !GET_FIRST_BIOS_DRIVE
		int fd = open(device, O_RDONLY);
		if (fd < 0)
			return false;
		bool isBootableDrive = ioctl(fd, B_GET_BIOS_DRIVE_ID, drive, 1) == B_OK;
		close(fd);
		return isBootableDrive;
	#else
		*drive = 0x80;
		return true;
	#endif
}


status_t
LegacyBootDrive::_ReadBlocks(int fd, uint8* buffer, size_t size)
{
	if (size % kBlockSize != 0) {
		fprintf(stderr, "_ReadBlocks buffer size must be a multiple of %d\n",
			(int)kBlockSize);
		return B_BAD_VALUE;
	}
	const size_t blocks = size / kBlockSize;
	uint8* block = buffer;
	for (size_t i = 0; i < blocks; i ++, block += kBlockSize) {
		if (read(fd, block, kBlockSize) != (ssize_t)kBlockSize)
			return B_IO_ERROR;
	}
	return B_OK;
}


status_t
LegacyBootDrive::_WriteBlocks(int fd, const uint8* buffer, size_t size)
{
	if (size % kBlockSize != 0) {
		fprintf(stderr, "_WriteBlocks buffer size must be a multiple of %d\n",
			(int)kBlockSize);
		return B_BAD_VALUE;
	}
	const size_t blocks = size / kBlockSize;
	const uint8* block = buffer;
	for (size_t i = 0; i < blocks; i ++, block += kBlockSize) {
		if (write(fd, block, kBlockSize) != (ssize_t)kBlockSize)
			return B_IO_ERROR;
	}
	return B_OK;
}


void
LegacyBootDrive::_CopyPartitionTable(MasterBootRecord* destination,
		const MasterBootRecord* source)
{
	memcpy(destination->diskSignature, source->diskSignature, 
		sizeof(source->diskSignature) + sizeof(source->reserved) +
		sizeof(source->partition));
}


bool
LegacyBootDrive::_IsValid(const MasterBootRecord* mbr)
{
	return  mbr->signature[0] == (kMBRSignature & 0xff) &&
		mbr->signature[1] == (kMBRSignature >> 8);
}
