/* 
 * Copyright 2005, Ingo Weinhold, bonefish@users.sf.net. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// kernel-side implementation-private definitions for the messaging service

#ifndef MESSAGING_SERVICE_H
#define MESSAGING_SERVICE_H

#include <MessagingServiceDefs.h>

#include "Locker.h"

namespace BPrivate {

// MessagingArea
class MessagingArea : public messaging_area_header {
public:
	~MessagingArea();

	static MessagingArea *Create(sem_id lockSem, sem_id counterSem);

	static bool CheckCommandSize(int32 dataSize);

	void InitHeader();

	bool Lock();
	void Unlock();

	area_id ID() const;
	int32 Size() const;

	void *AllocateCommand(uint32 commandWhat, int32 dataSize);
	void CommitCommand();

	void SetNextArea(MessagingArea *area);
	MessagingArea *NextArea() const;

private:
	MessagingArea();

	messaging_command *_CheckCommand(int32 offset, int32 &size);

	messaging_area_header	*fHeader;
	area_id					fID;
	int32					fSize;
	sem_id					fLockSem;
	sem_id					fCounterSem;
	MessagingArea			*fNextArea;
};

// MessagingService
class MessagingService {
public:
	MessagingService();
	~MessagingService();

	status_t InitCheck() const;

	bool Lock();
	void Unlock();

	status_t RegisterService(sem_id lockingSem, sem_id counterSem,
		area_id &areaID);

	status_t SendMessage(const void *message, int32 messageSize,
		const messaging_target *targets, int32 targetCount);

private:
	status_t _AllocateCommand(int32 commandWhat, int32 size,
		MessagingArea *&area, void *&data);

	BLocker			fLock;
	sem_id			fLockSem;
	sem_id			fCounterSem;
	MessagingArea	*fFirstArea;
	MessagingArea	*fLastArea;
};

}	// namespace BPrivate

using BPrivate::MessagingArea;
using BPrivate::MessagingService;

#endif	// MESSAGING_SERVICE_H
