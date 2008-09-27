/*
 * Copyright 2004-2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

/** A simple class wrapping a path. Has a fixed-sized buffer. */

#include <fs/KPath.h>

#include <stdlib.h>
#include <string.h>

#include <team.h>
#include <vfs.h>


// debugging
#define TRACE(x) ;
//#define TRACE(x) dprintf x


KPath::KPath(size_t bufferSize)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fPathLength(0),
	fLocked(false)
{
	SetTo(NULL, false, bufferSize);
}


KPath::KPath(const char* path, bool normalize, size_t bufferSize)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fPathLength(0),
	fLocked(false)
{
	SetTo(path, normalize, bufferSize);
}


KPath::KPath(const KPath& other)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fPathLength(0),
	fLocked(false)
{
	*this = other;
}


KPath::~KPath()
{
	free(fBuffer);
}


status_t
KPath::SetTo(const char* path, bool normalize, size_t bufferSize,
	bool traverseLeafLink)
{
	if (bufferSize == 0)
		bufferSize = B_PATH_NAME_LENGTH;

	// free the previous buffer, if the buffer size differs
	if (fBuffer && fBufferSize != bufferSize) {
		free(fBuffer);
		fBuffer = NULL;
		fBufferSize = 0;
	}
	fPathLength = 0;
	fLocked = false;

	// allocate buffer
	if (!fBuffer)
		fBuffer = (char*)malloc(bufferSize);
	if (!fBuffer)
		return B_NO_MEMORY;
	if (fBuffer) {
		fBufferSize = bufferSize;
		fBuffer[0] = '\0';
	}
	return SetPath(path, normalize, traverseLeafLink);
}


void
KPath::Adopt(KPath& other)
{
	free(fBuffer);

	fBuffer = other.fBuffer;
	fBufferSize = other.fBufferSize;

	other.fBuffer = NULL;
}


status_t
KPath::InitCheck() const
{
	return fBuffer ? B_OK : B_NO_MEMORY;
}


status_t
KPath::SetPath(const char *path, bool normalize, bool traverseLeafLink)
{
	if (!fBuffer)
		return B_NO_INIT;

	if (path) {
		if (normalize) {
			// normalize path
			status_t error = vfs_normalize_path(path, fBuffer, fBufferSize,
				traverseLeafLink,
				team_get_kernel_team_id() == team_get_current_team_id());
			if (error != B_OK) {
				SetPath(NULL);
				return error;
			}
			fPathLength = strlen(fBuffer);
		} else {
			// don't normalize path
			size_t length = strlen(path);
			if (length >= fBufferSize)
				return B_BUFFER_OVERFLOW;

			memcpy(fBuffer, path, length + 1);
			fPathLength = length;
			_ChopTrailingSlashes();
		}
	} else {
		fBuffer[0] = '\0';
		fPathLength = 0;
	}
	return B_OK;
}


const char*
KPath::Path() const
{
	return fBuffer;
}


char *
KPath::LockBuffer()
{
	if (!fBuffer || fLocked)
		return NULL;

	fLocked = true;
	return fBuffer;
}


void
KPath::UnlockBuffer()
{
	if (!fLocked) {
		TRACE(("KPath::UnlockBuffer(): ERROR: Buffer not locked!\n"));
		return;
	}
	fLocked = false;
	fPathLength = strnlen(fBuffer, fBufferSize);
	if (fPathLength == fBufferSize) {
		TRACE(("KPath::UnlockBuffer(): WARNING: Unterminated buffer!\n"));
		fPathLength--;
		fBuffer[fPathLength] = '\0';
	}
	_ChopTrailingSlashes();
}


char*
KPath::DetachBuffer()
{
	char* buffer = fBuffer;

	if (fBuffer != NULL) {
		fBuffer = NULL;
		fBufferSize = 0;
		fPathLength = 0;
		fLocked = false;
	}

	return buffer;
}


const char *
KPath::Leaf() const
{
	if (!fBuffer)
		return NULL;

	// only "/" has trailing slashes -- then we have to return the complete
	// buffer, as we have to do in case there are no slashes at all
	if (fPathLength != 1 || fBuffer[0] != '/') {
		for (int32 i = fPathLength - 1; i >= 0; i--) {
			if (fBuffer[i] == '/')
				return fBuffer + i + 1;
		}
	}
	return fBuffer;
}


status_t
KPath::ReplaceLeaf(const char *newLeaf)
{
	const char *leaf = Leaf();
	if (!leaf)
		return B_NO_INIT;

	int32 leafIndex = leaf - fBuffer;
	// chop off the current leaf (don't replace "/", though)
	if (leafIndex != 0 || fBuffer[leafIndex - 1]) {
		fBuffer[leafIndex] = '\0';
		fPathLength = leafIndex;
		_ChopTrailingSlashes();
	}

	// if a leaf was given, append it
	if (newLeaf)
		return Append(newLeaf);
	return B_OK;
}


bool
KPath::RemoveLeaf()
{
	// get the leaf -- bail out, if not initialized or only the "/" is left
	const char *leaf = Leaf();
	if (!leaf || leaf == fBuffer)
		return false;

	// chop off the leaf
	int32 leafIndex = leaf - fBuffer;
	fBuffer[leafIndex] = '\0';
	fPathLength = leafIndex;
	_ChopTrailingSlashes();

	return true;
}


status_t
KPath::Append(const char *component, bool isComponent)
{
	// check initialization and parameter
	if (!fBuffer)
		return B_NO_INIT;
	if (!component)
		return B_BAD_VALUE;
	if (fPathLength == 0)
		return SetPath(component);

	// get component length
	size_t componentLength = strlen(component);
	if (componentLength < 1)
		return B_OK;

	// if our current path is empty, we just copy the supplied one
	// compute the result path len
	bool insertSlash = isComponent && fBuffer[fPathLength - 1] != '/'
		&& component[0] != '/';
	size_t resultPathLength = fPathLength + componentLength
		+ (insertSlash ? 1 : 0);
	if (resultPathLength >= fBufferSize)
		return B_BUFFER_OVERFLOW;

	// compose the result path
	if (insertSlash)
		fBuffer[fPathLength++] = '/';
	memcpy(fBuffer + fPathLength, component, componentLength + 1);
	fPathLength = resultPathLength;
	return B_OK;
}


status_t
KPath::Normalize(bool traverseLeafLink)
{
	if (fBuffer == NULL)
		return B_NO_INIT;
	if (fPathLength == 0)
		return B_BAD_VALUE;

	status_t error = vfs_normalize_path(fBuffer, fBuffer, fBufferSize,
		traverseLeafLink,
		team_get_kernel_team_id() == team_get_current_team_id());
	if (error != B_OK) {
		// vfs_normalize_path() might have screwed up the previous path -- unset
		// it completely to avoid weird problems.
		fBuffer[0] = '\0';
		fPathLength = 0;
	}

	fPathLength = strlen(fBuffer);
	return B_OK;
}


KPath&
KPath::operator=(const KPath& other)
{
	SetTo(other.fBuffer, false, other.fBufferSize);
	return *this;
}


KPath&
KPath::operator=(const char* path)
{
	SetTo(path);
	return *this;
}


bool
KPath::operator==(const KPath& other) const
{
	if (!fBuffer)
		return !other.fBuffer;

	return (other.fBuffer
		&& fPathLength == other.fPathLength
		&& strcmp(fBuffer, other.fBuffer) == 0);
}


bool
KPath::operator==(const char* path) const
{
	if (!fBuffer)
		return (!path);

	return path && !strcmp(fBuffer, path);
}


bool
KPath::operator!=(const KPath& other) const
{
	return !(*this == other);
}


bool
KPath::operator!=(const char* path) const
{
	return !(*this == path);
}


void
KPath::_ChopTrailingSlashes()
{
	if (fBuffer) {
		while (fPathLength > 1 && fBuffer[fPathLength - 1] == '/')
			fBuffer[--fPathLength] = '\0';
	}
}

