/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "PackageWriter.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <fs_attr.h>

#include <AutoDeleter.h>

#include <haiku_package.h>

#include "FDCloser.h"
#include "Stacker.h"
#include "ZlibCompressor.h"


// minimum length of data we require before trying to zlib compress them
static const size_t kZlibCompressionSizeThreshold = 64;


// #pragma mark - Data interface


struct Data {
	virtual ~Data()
	{
	}

	virtual ssize_t Read(void* buffer, size_t size, off_t offset) = 0;
};


struct FileData : public Data {
	FileData(int fd)
		:
		fFD(fd)
	{
	}

	virtual ssize_t Read(void* buffer, size_t size, off_t offset)
	{
		ssize_t bytesRead = pread(fFD, buffer, size, offset);
		return bytesRead < 0 ? errno : bytesRead;
	}

private:
	int	fFD;
};


struct AttributeData : public Data {
	AttributeData(int fd, const char* attribute, uint32 type)
		:
		fFD(fd),
		fAttribute(attribute)
	{
	}

	virtual ssize_t Read(void* buffer, size_t size, off_t offset)
	{
		ssize_t bytesRead = fs_read_attr(fFD, fAttribute, fType, offset, buffer,
			size);
		return bytesRead < 0 ? errno : bytesRead;
	}

private:
	int			fFD;
	const char*	fAttribute;
	uint32		fType;
};


// #pragma mark - Attributes


struct PackageWriter::AttributeValue {
	union {
		int64			signedInt;
		uint64			unsignedInt;
		CachedString*	string;
		struct {
			uint64		size;
			union {
				uint64	offset;
				uint8	raw[B_HPKG_MAX_INLINE_DATA_SIZE];
			};
		} data;
	};
	uint8		type;
	int8		encoding;

	AttributeValue()
		:
		type(B_HPKG_ATTRIBUTE_TYPE_INVALID),
		encoding(-1)
	{
	}

	~AttributeValue()
	{
	}

	void SetTo(int8 value)
	{
		signedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_INT;
	}

	void SetTo(uint8 value)
	{
		unsignedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_UINT;
	}

	void SetTo(int16 value)
	{
		signedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_INT;
	}

	void SetTo(uint16 value)
	{
		unsignedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_UINT;
	}

	void SetTo(int32 value)
	{
		signedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_INT;
	}

	void SetTo(uint32 value)
	{
		unsignedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_UINT;
	}

	void SetTo(int64 value)
	{
		signedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_INT;
	}

	void SetTo(uint64 value)
	{
		unsignedInt = value;
		type = B_HPKG_ATTRIBUTE_TYPE_UINT;
	}

	void SetTo(CachedString* value)
	{
		string = value;
		type = B_HPKG_ATTRIBUTE_TYPE_STRING;
	}

	void SetToData(uint64 size, uint64 offset)
	{
		data.size = size;
		data.offset = offset;
		type = B_HPKG_ATTRIBUTE_TYPE_RAW;
		encoding = B_HPKG_ATTRIBUTE_ENCODING_RAW_HEAP;
	}

	void SetToData(uint64 size, const void* rawData)
	{
		data.size = size;
		if (size > 0)
			memcpy(data.raw, rawData, size);
		type = B_HPKG_ATTRIBUTE_TYPE_RAW;
		encoding = B_HPKG_ATTRIBUTE_ENCODING_RAW_INLINE;
	}

	uint8 PreferredEncoding() const
	{
		switch (type) {
			case B_HPKG_ATTRIBUTE_TYPE_INT:
				return _PreferredIntEncoding(signedInt >= 0
					? (uint64)signedInt << 1
					: (uint64)(-(signedInt + 1) << 1));
			case B_HPKG_ATTRIBUTE_TYPE_UINT:
				return _PreferredIntEncoding(unsignedInt);
			case B_HPKG_ATTRIBUTE_TYPE_STRING:
				return string->index >= 0
					? B_HPKG_ATTRIBUTE_ENCODING_STRING_TABLE
					: B_HPKG_ATTRIBUTE_ENCODING_STRING_INLINE;
			case B_HPKG_ATTRIBUTE_TYPE_RAW:
				return encoding;
			default:
				return 0;
		}
	}

private:
	static uint8 _PreferredIntEncoding(uint64 value)
	{
		if (value <= 0xff)
			return B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT;
		if (value <= 0xffff)
			return B_HPKG_ATTRIBUTE_ENCODING_INT_16_BIT;
		if (value <= 0xffffffff)
			return B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT;

		return B_HPKG_ATTRIBUTE_ENCODING_INT_64_BIT;
	}
};


struct PackageWriter::AttributeTypeKey {
	const char*	name;
	uint8		type;

	AttributeTypeKey(const char* name, uint8 type)
		:
		name(name),
		type(type)
	{
	}
};


struct PackageWriter::AttributeType {
	char*			name;
	uint8			type;
	int32			index;
	uint32			usageCount;
	AttributeType*	next;	// hash table link

	AttributeType()
		:
		name(NULL),
		type(B_HPKG_ATTRIBUTE_TYPE_INVALID),
		index(-1),
		usageCount(1)
	{
	}

	~AttributeType()
	{
		free(name);
	}

	bool Init(const char* name, uint8 type)
	{
		this->name = strdup(name);
		if (this->name == NULL)
			return false;

		this->type = type;
		return true;
	}
};


struct PackageWriter::AttributeTypeHashDefinition {
	typedef AttributeTypeKey	KeyType;
	typedef	AttributeType		ValueType;

	size_t HashKey(const AttributeTypeKey& key) const
	{
		return hash_string(key.name) ^ (uint32)key.type;
	}

	size_t Hash(const AttributeType* value) const
	{
		return hash_string(value->name) ^ (uint32)value->type;
	}

	bool Compare(const AttributeTypeKey& key, const AttributeType* value) const
	{
		return strcmp(value->name, key.name) == 0 && value->type == key.type;
	}

	AttributeType*& GetLink(AttributeType* value) const
	{
		return value->next;
	}
};


struct PackageWriter::Attribute : public DoublyLinkedListLinkImpl<Attribute> {
	AttributeType*				type;
	AttributeValue				value;
	DoublyLinkedList<Attribute>	children;

	Attribute(AttributeType* type)
		:
		type(type)
	{
	}

	~Attribute()
	{
		DeleteChildren();
	}

	void AddChild(Attribute* child)
	{
		children.Add(child);
	}

	void DeleteChildren()
	{
		while (Attribute* child = children.RemoveHead())
			delete child;
	}
};


struct PackageWriter::AttributeTypeUsageGreater {
	bool operator()(const AttributeType* a, const AttributeType* b)
	{
		return a->usageCount > b->usageCount;
	}
};


// #pragma mark - PackageWriter


struct PackageWriter::Entry : DoublyLinkedListLinkImpl<Entry> {
	Entry(char* name, size_t nameLength, bool isImplicit)
		:
		fName(name),
		fNameLength(nameLength),
		fIsImplicit(isImplicit)
	{
printf("%p->Entry::Entry(\"%s\", %lu, %d)\n", this, name, nameLength, isImplicit);
	}

	~Entry()
	{
		DeleteChildren();
		free(fName);
	}

	static Entry* Create(const char* name, size_t nameLength, bool isImplicit)
	{
		char* clonedName = (char*)malloc(nameLength + 1);
		if (clonedName == NULL)
			throw std::bad_alloc();
		memcpy(clonedName, name, nameLength);
		clonedName[nameLength] = '\0';

		Entry* entry = new(std::nothrow) Entry(clonedName, nameLength,
			isImplicit);
		if (entry == NULL) {
			free(clonedName);
			throw std::bad_alloc();
		}

		return entry;
	}

	const char* Name() const
	{
		return fName;
	}

	bool IsImplicit() const
	{
		return fIsImplicit;
	}

	void SetImplicit(bool isImplicit)
	{
		fIsImplicit = isImplicit;
	}

	bool HasName(const char* name, size_t nameLength)
	{
		return nameLength == fNameLength
			&& strncmp(name, fName, nameLength) == 0;
	}

	void AddChild(Entry* child)
	{
		fChildren.Add(child);
	}

	void DeleteChildren()
	{
		while (Entry* child = fChildren.RemoveHead())
			delete child;
	}

	Entry* GetChild(const char* name, size_t nameLength) const
	{
		EntryList::ConstIterator it = fChildren.GetIterator();
		while (Entry* child = it.Next()) {
			if (child->HasName(name, nameLength))
				return child;
		}

		return NULL;
	}

	EntryList::ConstIterator ChildIterator() const
	{
		return fChildren.GetIterator();
	}

private:
	char*		fName;
	size_t		fNameLength;
	bool		fIsImplicit;
	EntryList	fChildren;
};


// #pragma mark - PackageWriter


template<typename Type>
inline PackageWriter::Attribute*
PackageWriter::_AddAttribute(const char* attributeName, Type value)
{
	AttributeValue attributeValue;
	attributeValue.SetTo(value);
	return _AddAttribute(attributeName, attributeValue);
}


PackageWriter::PackageWriter()
	:
	fFileName(NULL),
	fFD(-1),
	fFinished(false),
	fDataBuffer(NULL),
	fRootEntry(NULL),
	fRootAttribute(NULL),
	fTopAttribute(NULL),
	fCachedStrings(NULL),
	fAttributeTypes(NULL)
{
}


PackageWriter::~PackageWriter()
{
	delete fRootAttribute;

	if (fAttributeTypes != NULL) {
		AttributeType* attributeType = fAttributeTypes->Clear(true);
		while (attributeType != NULL) {
			AttributeType* next = attributeType->next;
			delete attributeType;
			attributeType = next;
		}
	}

	if (fCachedStrings != NULL) {
		CachedString* cachedString = fCachedStrings->Clear(true);
		while (cachedString != NULL) {
			CachedString* next = cachedString->next;
			delete cachedString;
			cachedString = next;
		}
	}

	delete fRootEntry;

	free(fDataBuffer);

	if (fFD >= 0)
		close(fFD);

	if (!fFinished && fFileName != NULL)
		unlink(fFileName);
}


status_t
PackageWriter::Init(const char* fileName)
{
	try {
		return _Init(fileName);
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc) {
		fprintf(stderr, "Out of memory!\n");
		return B_NO_MEMORY;
	}
}


status_t
PackageWriter::AddEntry(const char* fileName)
{
	try {
		return _RegisterEntry(fileName);
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc) {
		fprintf(stderr, "Out of memory!\n");
		return B_NO_MEMORY;
	}
}


status_t
PackageWriter::Finish()
{
	try {
		return _Finish();
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc) {
		fprintf(stderr, "Out of memory!\n");
		return B_NO_MEMORY;
	}
}


status_t
PackageWriter::_Init(const char* fileName)
{
	// create hash tables
	fCachedStrings = new CachedStringTable;
	if (fCachedStrings->Init() != B_OK)
		throw std::bad_alloc();

	fAttributeTypes = new AttributeTypeTable;
	if (fAttributeTypes->Init() != B_OK)
		throw std::bad_alloc();

	// allocate data buffer
	fDataBufferSize = 2 * B_HPKG_DEFAULT_DATA_CHUNK_SIZE_ZLIB;
	fDataBuffer = malloc(fDataBufferSize);
	if (fDataBuffer == NULL)
		throw std::bad_alloc();

	// create entry list
	fRootEntry = new Entry(NULL, 0, true);

	// open file
	fFD = open(fileName, O_WRONLY | O_CREAT | O_TRUNC,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fFD < 0) {
		fprintf(stderr, "Error: Failed to open package file \"%s\": %s\n",
			fileName, strerror(errno));
		return errno;
	}

	fRootAttribute = new Attribute(NULL);

	fFileName = fileName;
	fHeapOffset = fHeapEnd = sizeof(hpkg_header);
	fTopAttribute = fRootAttribute;

	return B_OK;
}


status_t
PackageWriter::_Finish()
{
	// write entries
	for (EntryList::ConstIterator it = fRootEntry->ChildIterator();
			Entry* entry = it.Next();) {
		_AddEntry(AT_FDCWD, entry, entry->Name());
	}

printf("header size:             %lu\n", sizeof(hpkg_header));
printf("heap size:               %lld\n", fHeapEnd - sizeof(hpkg_header));

	// write the attribute type abbreviations
	off_t attributeTypesOffset = fHeapEnd;
	_WriteAttributeTypes();
printf("attributes types size:   %lld\n", fHeapEnd - attributeTypesOffset);

	// write the cached strings
	off_t cachedStringsOffset = fHeapEnd;
	int32 cachedStringsWritten = _WriteCachedStrings();
printf("cached strings size:     %lld\n", fHeapEnd - cachedStringsOffset);

	// write the TOC
	off_t tocOffset = fHeapEnd;
	_WriteAttributeChildren(fRootAttribute);
printf("toc size:                %lld\n", fHeapEnd - tocOffset);

	// write the package attributes
	off_t packageAttributesOffset = fHeapEnd;
	_Write<uint8>(0);
		// TODO: Write them for real!
printf("package attributes size: %lld\n", fHeapEnd - packageAttributesOffset);

	off_t totalSize = fHeapEnd;
printf("total size:              %lld\n", totalSize);

	// prepare the header

	// general
	hpkg_header header;
	header.magic = B_HOST_TO_BENDIAN_INT32(B_HPKG_MAGIC);
	header.header_size = B_HOST_TO_BENDIAN_INT16(
		(uint16)sizeof(hpkg_header));
	header.version = B_HOST_TO_BENDIAN_INT16(B_HPKG_VERSION);
	header.total_size = B_HOST_TO_BENDIAN_INT64(totalSize);

	// package attributes
	header.attributes_compression = B_HOST_TO_BENDIAN_INT32(
		B_HPKG_COMPRESSION_NONE);
	header.attributes_length_compressed
		= B_HOST_TO_BENDIAN_INT32(fHeapEnd - packageAttributesOffset);
	header.attributes_length_uncompressed
		= header.attributes_length_compressed;
		// TODO: Support compression!

	// TOC
	header.toc_compression = B_HOST_TO_BENDIAN_INT32(B_HPKG_COMPRESSION_NONE);
	header.toc_length_compressed = B_HOST_TO_BENDIAN_INT64(
		packageAttributesOffset - attributeTypesOffset);
	header.toc_length_uncompressed = header.toc_length_compressed;
		// TODO: Support compression!

	// TOC subsections
	header.toc_attribute_types_length = B_HOST_TO_BENDIAN_INT64(
		cachedStringsOffset - attributeTypesOffset);
	header.toc_attribute_types_count = B_HOST_TO_BENDIAN_INT64(
		fAttributeTypes->CountElements());
	header.toc_strings_length = B_HOST_TO_BENDIAN_INT64(
		tocOffset - cachedStringsOffset);
	header.toc_strings_count = B_HOST_TO_BENDIAN_INT64(cachedStringsWritten);

	// write the header
	_WriteBuffer(&header, sizeof(hpkg_header), 0);

	fFinished = true;
	return B_OK;
}


status_t
PackageWriter::_RegisterEntry(const char* fileName)
{
	if (*fileName == '\0') {
		fprintf(stderr, "Error: Invalid empty file name\n");
		return B_BAD_VALUE;
	}

	// add all components of the path
	Entry* entry = fRootEntry;
	while (*fileName != 0) {
		const char* nextSlash = strchr(fileName, '/');
		// no slash, just add the file name
		if (nextSlash == NULL) {
			entry = _RegisterEntry(entry, fileName, strlen(fileName), false);
			break;
		}

		// find the start of the next component, skipping slashes
		const char* nextComponent = nextSlash + 1;
		while (*nextComponent == '/')
			nextComponent++;

		if (nextSlash == fileName) {
			// the FS root
			entry = _RegisterEntry(entry, fileName, 1,
				*nextComponent != '\0');
		} else {
			entry = _RegisterEntry(entry, fileName, nextSlash - fileName,
				*nextComponent != '\0');
		}

		fileName = nextComponent;
	}

	return B_OK;
}


PackageWriter::Entry*
PackageWriter::_RegisterEntry(Entry* parent, const char* name,
	size_t nameLength, bool isImplicit)
{
	// check the component name -- don't allow "." or ".."
	if (*name == '.'
		&& (nameLength == 1 || (nameLength == 2 && name[2] == '.'))) {
		fprintf(stderr, "Error: Invalid file name: \".\" and \"..\" "
			"are not allowed as path components\n");
		throw status_t(B_BAD_VALUE);
	}

	// the entry might already exist
	Entry* entry = parent->GetChild(name, nameLength);
	if (entry != NULL) {
		// If the entry was implicit and is no longer, we mark it non-implicit
		// and delete all of it's children.
		if (entry->IsImplicit() && !isImplicit) {
			entry->DeleteChildren();
			entry->SetImplicit(false);
		}
	} else {
		// nope -- create it
		entry = Entry::Create(name, nameLength, isImplicit);
		parent->AddChild(entry);
	}

	return entry;
}


void
PackageWriter::_WriteAttributeTypes()
{
	// create an array of the attribute types
	int32 attributeTypeCount = fAttributeTypes->CountElements();
	AttributeType** attributeTypes = new AttributeType*[attributeTypeCount];
	ArrayDeleter<AttributeType*> attributeTypesDeleter(attributeTypes);

	int32 index = 0;
	for (AttributeTypeTable::Iterator it = fAttributeTypes->GetIterator();
			AttributeType* type = it.Next();) {
		attributeTypes[index++] = type;
	}

	// sort it by descending usage count
	std::sort(attributeTypes, attributeTypes + attributeTypeCount,
		AttributeTypeUsageGreater());

	// assign the indices and write entries to disk
	for (int32 i = 0; i < attributeTypeCount; i++) {
		AttributeType* attributeType = attributeTypes[i];
		attributeType->index = i;

		_Write<uint8>(attributeType->type);
		_WriteString(attributeType->name);
	}

	// write a terminating 0 byte
	_Write<uint8>(0);
}


int32
PackageWriter::_WriteCachedStrings()
{
	// create an array of the cached strings
	int32 count = fCachedStrings->CountElements();
	CachedString** cachedStrings = new CachedString*[count];
	ArrayDeleter<CachedString*> cachedStringsDeleter(cachedStrings);

	int32 index = 0;
	for (CachedStringTable::Iterator it = fCachedStrings->GetIterator();
			CachedString* string = it.Next();) {
		cachedStrings[index++] = string;
	}

	// sort it by descending usage count
	std::sort(cachedStrings, cachedStrings + count,
		CachedStringUsageGreater());

	// assign the indices and write entries to disk
	int32 stringsWritten = 0;
	for (int32 i = 0; i < count; i++) {
		CachedString* cachedString = cachedStrings[i];

		// strings that are used only once are better stored inline
		if (cachedString->usageCount < 2)
			break;

		cachedString->index = i;

		_WriteString(cachedString->string);
		stringsWritten++;
	}

	// write a terminating 0 byte
	_Write<uint8>(0);

	return stringsWritten;
}


void
PackageWriter::_WriteAttributeChildren(Attribute* attribute)
{
	DoublyLinkedList<Attribute>::Iterator it
		= attribute->children.GetIterator();
	while (Attribute* child = it.Next()) {
		AttributeType* type = child->type;

		// write tag
		uint8 encoding = child->value.PreferredEncoding();
		_WriteUnsignedLEB128(B_HPKG_ATTRIBUTE_TAG_COMPOSE(type->index,
			encoding, !child->children.IsEmpty()));

		// write value
		_WriteAttributeValue(child->value, encoding);

		if (!child->children.IsEmpty())
			_WriteAttributeChildren(child);
	}

	_WriteUnsignedLEB128(0);
}


void
PackageWriter::_WriteAttributeValue(const AttributeValue& value, uint8 encoding)
{
	switch (value.type) {
		case B_HPKG_ATTRIBUTE_TYPE_INT:
		case B_HPKG_ATTRIBUTE_TYPE_UINT:
		{
			uint64 intValue = value.type == B_HPKG_ATTRIBUTE_TYPE_INT
				? (uint64)value.signedInt : value.unsignedInt;

			switch (encoding) {
				case B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT:
					_Write<uint8>((uint8)intValue);
					break;
				case B_HPKG_ATTRIBUTE_ENCODING_INT_16_BIT:
					_Write<uint16>(
						B_HOST_TO_BENDIAN_INT16((uint16)intValue));
					break;
				case B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT:
					_Write<uint32>(
						B_HOST_TO_BENDIAN_INT32((uint32)intValue));
					break;
				case B_HPKG_ATTRIBUTE_ENCODING_INT_64_BIT:
					_Write<uint64>(
						B_HOST_TO_BENDIAN_INT64((uint64)intValue));
					break;
				default:
				{
					fprintf(stderr, "_WriteAttributeValue(): invalid "
						"encoding %d for int value type %d\n", encoding,
						value.type);
					throw status_t(B_BAD_VALUE);
				}
			}

			break;
		}

		case B_HPKG_ATTRIBUTE_TYPE_STRING:
		{
			if (encoding == B_HPKG_ATTRIBUTE_ENCODING_STRING_TABLE)
				_WriteUnsignedLEB128(value.string->index);
			else
				_WriteString(value.string->string);
			break;
		}

		case B_HPKG_ATTRIBUTE_TYPE_RAW:
		{
			_WriteUnsignedLEB128(value.data.size);
			if (encoding == B_HPKG_ATTRIBUTE_ENCODING_RAW_HEAP)
				_WriteUnsignedLEB128(value.data.offset);
			else
				_WriteBuffer(value.data.raw, value.data.size);
			break;
		}

		default:
			fprintf(stderr, "_WriteAttributeValue(): invalid value type: "
				"%d\n", value.type);
			throw status_t(B_BAD_VALUE);
	}
}


void
PackageWriter::_WriteUnsignedLEB128(uint64 value)
{
	uint8 bytes[10];
	int32 count = 0;
	do {
		uint8 byte = value & 0x7f;
		value >>= 7;
		bytes[count++] = byte | (value != 0 ? 0x80 : 0);
	} while (value != 0);

	_WriteBuffer(bytes, count);
}


void
PackageWriter::_WriteBuffer(const void* buffer, size_t size, off_t offset)
{
	ssize_t bytesWritten = pwrite(fFD, buffer, size, offset);
	if (bytesWritten < 0) {
		fprintf(stderr, "_WriteBuffer(%p, %lu) failed to write data: %s\n",
			buffer, size, strerror(errno));
		throw status_t(errno);
	}
	if ((size_t)bytesWritten != size) {
		fprintf(stderr, "_WriteBuffer(%p, %lu) failed to write all data\n",
			buffer, size);
		throw status_t(B_ERROR);
	}
}


void
PackageWriter::_AddEntry(int dirFD, Entry* entry, const char* fileName)
{
printf("PackageWriter::_AddEntry(%d, %p, \"%s\")\n", dirFD, entry, fileName);
	bool isImplicitEntry = entry != NULL && entry->IsImplicit();

	// open the node
	int fd = openat(dirFD, fileName,
		O_RDONLY | (isImplicitEntry ? 0 : O_NOTRAVERSE));
	if (fd < 0) {
		fprintf(stderr, "Error: Failed to open entry \"%s\": %s\n",
			fileName, strerror(errno));
		throw status_t(errno);
	}
	FDCloser fdCloser(fd);

	// stat the node
	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "Error: Failed to fstat() file \"%s\": %s\n",
			fileName, strerror(errno));
		throw status_t(errno);
	}

	// implicit entries must be directories
	if (isImplicitEntry && !S_ISDIR(st.st_mode)) {
		fprintf(stderr, "Error: Non-leaf path component \"%s\" is not a "
			"directory\n", fileName);
		throw status_t(B_BAD_VALUE);
	}

	// check/translate the node type
	uint8 fileType;
	uint32 defaultPermissions;
	if (S_ISREG(st.st_mode)) {
		fileType = B_HPKG_FILE_TYPE_FILE;
		defaultPermissions = B_HPKG_DEFAULT_FILE_PERMISSIONS;
	} else if (S_ISLNK(st.st_mode)) {
		fileType = B_HPKG_FILE_TYPE_SYMLINK;
		defaultPermissions = B_HPKG_DEFAULT_SYMLINK_PERMISSIONS;
	} else if (S_ISDIR(st.st_mode)) {
		fileType = B_HPKG_FILE_TYPE_DIRECTORY;
		defaultPermissions = B_HPKG_DEFAULT_DIRECTORY_PERMISSIONS;
	} else {
		// unsupported node type
		fprintf(stderr, "Error: Unsupported node type, entry: \"%s\"\n",
			fileName);
		throw status_t(B_UNSUPPORTED);
	}

	// add attribute entry
	Attribute* entryAttribute = _AddStringAttribute(
		B_HPKG_ATTRIBUTE_NAME_DIRECTORY_ENTRY, fileName);
	Stacker<Attribute> entryAttributeStacker(fTopAttribute, entryAttribute);

	// add stat data
	if (fileType != B_HPKG_DEFAULT_FILE_TYPE)
		_AddAttribute(B_HPKG_ATTRIBUTE_NAME_FILE_TYPE, fileType);
	if (defaultPermissions != uint32(st.st_mode & ALLPERMS)) {
		_AddAttribute(B_HPKG_ATTRIBUTE_NAME_FILE_PERMISSIONS,
			uint32(st.st_mode & ALLPERMS));
	}
	_AddAttribute(B_HPKG_ATTRIBUTE_NAME_FILE_ATIME, uint32(st.st_atime));
	_AddAttribute(B_HPKG_ATTRIBUTE_NAME_FILE_MTIME, uint32(st.st_mtime));
	_AddAttribute(B_HPKG_ATTRIBUTE_NAME_FILE_CRTIME, uint32(st.st_crtime));
	// TODO: File user/group!

	// add file data/symlink path
	if (S_ISREG(st.st_mode)) {
		// regular file -- add data
		if (st.st_size > 0) {
			FileData data(fd);
			status_t error = _AddData(data, st.st_size);
			if (error != B_OK)
				throw status_t(error);
		}
	} else if (S_ISLNK(st.st_mode)) {
		// symlink -- add link address
		char path[B_PATH_NAME_LENGTH + 1];
		ssize_t bytesRead = readlinkat(dirFD, fileName, path,
			B_PATH_NAME_LENGTH);
		if (bytesRead < 0) {
			fprintf(stderr, "Error: Failed to read symlink \"%s\": %s\n",
				fileName, strerror(errno));
			throw status_t(errno);
		}

		path[bytesRead] = '\0';
		_AddStringAttribute(B_HPKG_ATTRIBUTE_NAME_SYMLINK_PATH, path);
	}

	// add attributes
	if (DIR* attrDir = fs_fopen_attr_dir(fd)) {
		CObjectDeleter<DIR, int> attrDirCloser(attrDir, fs_close_attr_dir);

		while (dirent* entry = readdir(attrDir)) {
			attr_info attrInfo;
			if (fs_stat_attr(fd, entry->d_name, &attrInfo) < 0) {
				fprintf(stderr, "Error: Failed to stat attribute \"%s\" of "
					"file \"%s\": %s\n", entry->d_name, fileName,
					strerror(errno));
				throw status_t(errno);
			}

			// create attribute entry
			Attribute* attributeAttribute = _AddStringAttribute(
				B_HPKG_ATTRIBUTE_NAME_FILE_ATTRIBUTE, entry->d_name);
			Stacker<Attribute> attributeAttributeStacker(fTopAttribute,
				attributeAttribute);

			// add type
			_AddAttribute(B_HPKG_ATTRIBUTE_NAME_FILE_ATTRIBUTE_TYPE,
				(uint32)attrInfo.type);

			// add data
			AttributeData data(fd, entry->d_name, attrInfo.type);
			status_t error = _AddData(data, attrInfo.size);
			if (error != B_OK)
				throw status_t(error);
		}
	}

	if (S_ISDIR(st.st_mode)) {
		// directory -- recursively add children
		if (isImplicitEntry) {
			// this is an implicit entry -- just add it's children
			for (EntryList::ConstIterator it = entry->ChildIterator();
					Entry* child = it.Next();) {
				_AddEntry(fd, child, child->Name());
			}
		} else {
			// we need to clone the directory FD for fdopendir()
			int clonedFD = dup(fd);
			if (clonedFD < 0) {
				fprintf(stderr, "Error: Failed to dup() directory FD: %s\n",
					strerror(errno));
				throw status_t(errno);
			}

			DIR* dir = fdopendir(clonedFD);
			if (dir == NULL) {
				fprintf(stderr, "Error: Failed to open directory \"%s\": %s\n",
					fileName, strerror(errno));
				close(clonedFD);
				throw status_t(errno);
			}
			CObjectDeleter<DIR, int> dirCloser(dir, closedir);

			while (dirent* entry = readdir(dir)) {
				// skip "." and ".."
				if (strcmp(entry->d_name, ".") == 0
					|| strcmp(entry->d_name, "..") == 0) {
					continue;
				}

				_AddEntry(fd, NULL, entry->d_name);
			}
		}
	}
}


PackageWriter::Attribute*
PackageWriter::_AddAttribute(const char* attributeName,
	const AttributeValue& value)
{
	Attribute* attribute = new Attribute(
		_GetAttributeType(attributeName, value.type));

	attribute->value = value;
	fTopAttribute->AddChild(attribute);

	return attribute;
}


PackageWriter::Attribute*
PackageWriter::_AddStringAttribute(const char* attributeName, const char* value)
{
	AttributeValue attributeValue;
	attributeValue.SetTo(_GetCachedString(value));
	return _AddAttribute(attributeName, attributeValue);
}


PackageWriter::Attribute*
PackageWriter::_AddDataAttribute(const char* attributeName, uint64 dataSize,
	uint64 dataOffset)
{
	AttributeValue attributeValue;
	attributeValue.SetToData(dataSize, dataOffset);
	return _AddAttribute(attributeName, attributeValue);
}


CachedString*
PackageWriter::_GetCachedString(const char* value)
{
	CachedString* string = fCachedStrings->Lookup(value);
	if (string != NULL) {
		string->usageCount++;
		return string;
	}

	string = new CachedString;
	if (!string->Init(value)) {
		delete string;
		throw std::bad_alloc();
	}

	fCachedStrings->Insert(string);
	return string;
}


PackageWriter::AttributeType*
PackageWriter::_GetAttributeType(const char* attributeName, uint8 type)
{
	AttributeType* attributeType = fAttributeTypes->Lookup(
		AttributeTypeKey(attributeName, type));
	if (attributeType != NULL) {
		attributeType->usageCount++;
		return attributeType;
	}

	attributeType = new AttributeType;
	if (!attributeType->Init(attributeName, type)) {
		delete attributeType;
		throw std::bad_alloc();
	}

	fAttributeTypes->Insert(attributeType);
	return attributeType;
}


status_t
PackageWriter::_AddData(Data& data, off_t size)
{
	uint64 dataOffset = fHeapEnd;

	uint64 compression = B_HPKG_COMPRESSION_NONE;
	uint64 compressedSize;

	status_t error = _WriteZlibCompressedData(data, size, dataOffset,
		compressedSize);
	if (error == B_OK) {
		compression = B_HPKG_COMPRESSION_ZLIB;
	} else {
		error = _WriteUncompressedData(data, size, dataOffset);
		compressedSize = size;
	}
	if (error != B_OK)
		return error;

	fHeapEnd = dataOffset + compressedSize;

	// add data attribute
	Attribute* dataAttribute = _AddDataAttribute(B_HPKG_ATTRIBUTE_NAME_DATA,
		compressedSize, dataOffset - fHeapOffset);
	Stacker<Attribute> attributeAttributeStacker(fTopAttribute, dataAttribute);

	// if compressed, add compression attributes
	if (compression != B_HPKG_COMPRESSION_NONE) {
		_AddAttribute(B_HPKG_ATTRIBUTE_NAME_DATA_COMPRESSION, compression);
		_AddAttribute(B_HPKG_ATTRIBUTE_NAME_DATA_SIZE, (uint64)size);
		// uncompressed size
	}

	// TODO: Support inline data!

	return B_OK;
}


status_t
PackageWriter::_WriteUncompressedData(Data& data, off_t size,
	uint64 writeOffset)
{
	// copy the data to the heap
	off_t readOffset = 0;
	off_t remainingSize = size;
	while (remainingSize > 0) {
		// read data
		size_t toCopy = std::min(remainingSize, (off_t)fDataBufferSize);
		ssize_t bytesRead = data.Read(fDataBuffer, toCopy, readOffset);
		if (bytesRead < 0) {
			fprintf(stderr, "Error: Failed to read data: %s\n",
				strerror(errno));
			return errno;
		}
		if ((size_t)bytesRead != toCopy) {
			fprintf(stderr, "Error: Failed to read all data\n");
			return B_ERROR;
		}

		// write to heap
		ssize_t bytesWritten = pwrite(fFD, fDataBuffer, bytesRead, writeOffset);
		if (bytesWritten < 0) {
			fprintf(stderr, "Error: Failed to write data: %s\n",
				strerror(errno));
			return errno;
		}
		if (bytesWritten != bytesRead) {
			fprintf(stderr, "Error: Failed to write all data\n");
			return B_ERROR;
		}

		remainingSize -= bytesRead;
		readOffset += bytesRead;
		writeOffset += bytesRead;
	}

	return B_OK;
}


status_t
PackageWriter::_WriteZlibCompressedData(Data& data, off_t size,
	uint64 writeOffset, uint64& _compressedSize)
{
	// Use zlib compression only for data large enough.
	if (size < kZlibCompressionSizeThreshold)
		return B_BAD_VALUE;

	// fDataBuffer is 2 * B_HPKG_DEFAULT_DATA_CHUNK_SIZE_ZLIB, so split it into
	// two halves we can use for reading and compressing
	const size_t chunkSize = B_HPKG_DEFAULT_DATA_CHUNK_SIZE_ZLIB;
	uint8* inputBuffer = (uint8*)fDataBuffer;
	uint8* outputBuffer = (uint8*)fDataBuffer + chunkSize;

	// account for the offset table
	uint64 chunkCount = (size + (chunkSize - 1)) / chunkSize;
	off_t offsetTableOffset = writeOffset;
	uint64* offsetTable = NULL;
	if (chunkCount > 1) {
		offsetTable = new uint64[chunkCount - 1];
		writeOffset = offsetTableOffset + (chunkCount - 1) * sizeof(uint64);
	}
	ArrayDeleter<uint64> offsetTableDeleter(offsetTable);

	const uint64 dataOffset = writeOffset;
	const uint64 dataEndLimit = offsetTableOffset + size;

	// read the data, compress them and write them to the heap
	off_t readOffset = 0;
	off_t remainingSize = size;
	uint64 chunkIndex = 0;
	while (remainingSize > 0) {
		// read data
		size_t toCopy = std::min(remainingSize, (off_t)chunkSize);
		ssize_t bytesRead = data.Read(inputBuffer, toCopy, readOffset);
		if (bytesRead < 0) {
			fprintf(stderr, "Error: Failed to read data: %s\n",
				strerror(errno));
			return errno;
		}
		if ((size_t)bytesRead != toCopy) {
			fprintf(stderr, "Error: Failed to read all data\n");
			return B_ERROR;
		}

		// compress
		ZlibCompressor compressor;
		size_t compressedSize;
		status_t error = compressor.Compress(inputBuffer, bytesRead,
			outputBuffer, bytesRead, compressedSize);

		const void* writeBuffer;
		size_t bytesToWrite;
		if (error == B_OK) {
			writeBuffer = outputBuffer;
			bytesToWrite = compressedSize;
		} else {
			if (error != B_BUFFER_OVERFLOW)
				return error;
			writeBuffer = inputBuffer;
			bytesToWrite = bytesRead;
		}

		// check the total compressed data size
		if (writeOffset + bytesToWrite >= dataEndLimit)
			return B_BUFFER_OVERFLOW;

		if (chunkIndex > 0)
			offsetTable[chunkIndex - 1] = writeOffset - dataOffset;

		// write to heap
		ssize_t bytesWritten = pwrite(fFD, writeBuffer, bytesToWrite,
			writeOffset);
		if (bytesWritten < 0) {
			fprintf(stderr, "Error: Failed to write data: %s\n",
				strerror(errno));
			return errno;
		}
		if ((size_t)bytesWritten != bytesToWrite) {
			fprintf(stderr, "Error: Failed to write all data\n");
			return B_ERROR;
		}

		remainingSize -= bytesRead;
		readOffset += bytesRead;
		writeOffset += bytesToWrite;
		chunkIndex++;
	}

	// write the offset table
	if (chunkCount > 1) {
		size_t bytesToWrite = (chunkCount - 1) * sizeof(uint64);
		ssize_t bytesWritten = pwrite(fFD, offsetTable, bytesToWrite,
			offsetTableOffset);
		if (bytesWritten < 0) {
			fprintf(stderr, "Error: Failed to write data: %s\n",
				strerror(errno));
			return errno;
		}
		if ((size_t)bytesWritten != bytesToWrite) {
			fprintf(stderr, "Error: Failed to write all data\n");
			return B_ERROR;
		}
	}

	_compressedSize = writeOffset - dataOffset;
	return B_OK;
}
