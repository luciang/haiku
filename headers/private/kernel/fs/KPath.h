// KPath.h
//
// A simple class wrapping a path. Has a fixed-sized buffer.

#ifndef _K_PATH_H
#define _K_PATH_H

#include <KernelExport.h>

namespace BPrivate {
namespace DiskDevice {

class KPath {
public:
	KPath(int32 bufferSize = B_PATH_NAME_LENGTH);
	KPath(const char* path, int32 bufferSize = B_PATH_NAME_LENGTH);
	KPath(const KPath& other);
	~KPath();

	status_t SetTo(const char *path, int32 bufferSize = B_PATH_NAME_LENGTH);

	status_t InitCheck() const;

	status_t SetPath(const char *path);
	const char* Path() const;
	int32 Length() const;

	int32 BufferSize() const;
	char *LockBuffer();
	void UnlockBuffer();

	status_t Append(const char *component);

	KPath& operator=(const KPath& other);
	KPath& operator=(const char* path);

	bool operator==(const KPath& other) const;
	bool operator==(const char* path) const;
	bool operator!=(const KPath& other) const;
	bool operator!=(const char* path) const;

private:
	char*	fBuffer;
	int32	fBufferSize;
	int32	fPathLength;
	bool	fLocked;
};

} // namespace DiskDevice
} // namespace BPrivate

using BPrivate::DiskDevice::KPath;

#endif _K_PATH_H
