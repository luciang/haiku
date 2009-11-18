/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "PackageNodeAttribute.h"

#include <stdlib.h>
#include <string.h>


PackageNodeAttribute::PackageNodeAttribute(PackageNode* parent, uint32 type,
	const PackageData& data)
	:
	fData(data),
	fParent(parent),
	fName(NULL),
	fType(type)
{
}


PackageNodeAttribute::~PackageNodeAttribute()
{
	free(fName);
}


status_t
PackageNodeAttribute::Init(const char* name)
{
	fName = strdup(name);
	return fName != NULL ? B_OK : B_NO_MEMORY;
}
