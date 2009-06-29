/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef ATTRIBUTE_VALUE_H
#define ATTRIBUTE_VALUE_H

#include "AttributeClasses.h"
#include "DwarfTypes.h"
#include "TargetAddressRangeList.h"


class DebugInfoEntry;


struct AttributeValue {
	union {
		dwarf_addr_t		address;
		struct {
			const void*		data;
			dwarf_size_t	length;
		}					block;
		uint64				constant;
		bool				flag;
		TargetAddressRangeList*	rangeList;
		dwarf_off_t			pointer;
		DebugInfoEntry*		reference;
		const char*			string;
	};

	uint16				attributeForm;
	uint8				attributeClass;
	bool				isSigned;

	AttributeValue()
		:
		attributeClass(ATTRIBUTE_CLASS_UNKNOWN)
	{
	}

	~AttributeValue()
	{
		Unset();
	}

	void SetToAddress(dwarf_addr_t address)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_ADDRESS;
		this->address = address;
	}

	void SetToBlock(const void* data, dwarf_size_t length)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
		block.data = data;
		block.length = length;
	}

	void SetToConstant(uint64 value, bool isSigned)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
		this->constant = value;
		this->isSigned = isSigned;
	}

	void SetToFlag(bool value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_FLAG;
		this->flag = value;
	}

	void SetToLinePointer(dwarf_off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_LINEPTR;
		this->pointer = value;
	}

	void SetToLocationListPointer(dwarf_off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_LOCLISTPTR;
		this->pointer = value;
	}

	void SetToMacroPointer(dwarf_off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_MACPTR;
		this->pointer = value;
	}

	void SetToRangeList(TargetAddressRangeList* rangeList)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_RANGELISTPTR;
		this->rangeList = rangeList;
		if (rangeList != NULL)
			rangeList->AddReference();
	}

	void SetToReference(DebugInfoEntry* entry)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_REFERENCE;
		this->reference = entry;
	}

	void SetToString(const char* string)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_STRING;
		this->string = string;
	}

	void Unset()
	{
		if (attributeClass == ATTRIBUTE_CLASS_RANGELISTPTR
			&& rangeList != NULL) {
			rangeList->RemoveReference();
		}
		attributeClass = ATTRIBUTE_CLASS_UNKNOWN;
	}

	const char* ToString(char* buffer, size_t size);
};


struct DynamicAttributeValue {
	union {
		uint64				constant;
		DebugInfoEntry*		reference;
		struct {
			const void*		data;
			dwarf_size_t	length;
		}					block;
	};
	uint8				attributeClass;

	DynamicAttributeValue()
		:
		attributeClass(ATTRIBUTE_CLASS_CONSTANT)
	{
		this->constant = 0;
	}

	void SetTo(uint64 constant)
	{
		this->constant = constant;
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
	}

	void SetTo(DebugInfoEntry* reference)
	{
		this->reference = reference;
		attributeClass = ATTRIBUTE_CLASS_REFERENCE;
	}

	void SetTo(const void* data, dwarf_size_t length)
	{
		block.data = data;
		block.length = length;
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
	}
};


struct ConstantAttributeValue {
	union {
		uint64				constant;
		const char*			string;
		struct {
			const void*		data;
			dwarf_size_t	length;
		}					block;
	};
	uint8				attributeClass;

	ConstantAttributeValue()
		:
		attributeClass(ATTRIBUTE_CLASS_CONSTANT)
	{
		this->constant = 0;
	}

	void SetTo(uint64 constant)
	{
		this->constant = constant;
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
	}

	void SetTo(const char* string)
	{
		this->string = string;
		attributeClass = ATTRIBUTE_CLASS_STRING;
	}

	void SetTo(const void* data, dwarf_size_t length)
	{
		block.data = data;
		block.length = length;
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
	}
};


struct DeclarationLocation {
	uint32	file;
	uint32	line;
	uint32	column;

	DeclarationLocation()
		:
		file(0),
		line(0),
		column(0)
	{
	}

	void SetFile(uint32 file)
	{
		this->file = file;
	}

	void SetLine(uint32 line)
	{
		this->line = line;
	}

	void SetColumn(uint32 column)
	{
		this->column = column;
	}
};

#endif	// ATTRIBUTE_VALUE_H
