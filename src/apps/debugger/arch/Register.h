/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef REGISTER_H
#define REGISTER_H

#include <SupportDefs.h>


enum register_format {
	REGISTER_FORMAT_INTEGER,
	REGISTER_FORMAT_FLOAT
};

enum register_type {
	REGISTER_TYPE_PROGRAM_COUNTER,
	REGISTER_TYPE_STACK_POINTER,
	REGISTER_TYPE_GENERAL_PURPOSE,
	REGISTER_TYPE_SPECIAL_PURPOSE,
	REGISTER_TYPE_EXTENDED
};


class Register {
public:
								Register(int32 index, const char* name,
									register_format format, uint32 bitSize,
									register_type type);
										// name will not be cloned
								Register(const Register& other);

			int32				Index() const	{ return fIndex; }
			const char*			Name() const	{ return fName; }
			register_format		Format() const	{ return fFormat; }
			uint32				BitSize() const	{ return fBitSize; }
			register_type		Type() const	{ return fType; }

private:
			int32				fIndex;
			const char*			fName;
			register_format		fFormat;
			uint32				fBitSize;
			register_type		fType;

};


#endif	// REGISTER_H
