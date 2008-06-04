/* 
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "IOScheduler.h"

#include <KernelExport.h>

#include <khash.h>
#include <lock.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>


IORequest::IORequest(void *_cookie, off_t _offset, void *_buffer, size_t _size, bool _writeMode)
	:
	cookie(_cookie),
	virtual_address(addr_t(_buffer)),
	offset(_offset),
	size(_size),
	write(_writeMode)
{
}


IORequest::IORequest(void *_cookie, off_t _offset, const void *_buffer, size_t _size, bool _writeMode)
	:
	cookie(_cookie),
	virtual_address(addr_t(const_cast<void *>(_buffer))),
	offset(_offset),
	size(_size),
	write(_writeMode)
{
}


//	#pragma mark -


IOScheduler::IOScheduler(const char* name, device_module_info* module)
	:
	fModule(module)
{
	mutex_init(&fLock, "I/O scheduler queue");

	// start thread for device
	fThread = spawn_kernel_thread(&IOScheduler::scheduler, name, B_NORMAL_PRIORITY, (void *)this);
#if 0
	if (fThread >= B_OK)
		resume_thread(fThread);
#endif
}


IOScheduler::~IOScheduler()
{
	kill_thread(fThread);
	mutex_destroy(&fLock);
}


status_t
IOScheduler::InitCheck() const
{
	if (fThread < B_OK)
		return fThread;

	return B_OK;
}


status_t
IOScheduler::Process(IORequest &request)
{
	// ToDo: put the request into the queue, and wait until it got processed by the scheduler
	// ToDo: translate addresses into physical locations
	// ToDo: connect to the DPC mechanism in the SCSI/IDE bus manager?
	// ToDo: assume locked memory?

	if (request.write)
		return fModule->write(request.cookie, request.offset, (const void *)request.virtual_address, &request.size);

	return fModule->read(request.cookie, request.offset, (void *)request.virtual_address, &request.size);
}


#if 0
IOScheduler *
IOScheduler::GetScheduler()
{
	return NULL;
}
#endif


int32
IOScheduler::Scheduler()
{
	// main loop

	return 0;
}


int32
IOScheduler::scheduler(void *_self)
{
	IOScheduler *self = (IOScheduler *)_self;
	return self->Scheduler();
}

