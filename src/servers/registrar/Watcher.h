//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		Watcher.h
//	Author:			Ingo Weinhold (bonefish@users.sf.net)
//	Description:	A Watcher represents a target of a watching service.
//					A WatcherFilter represents a predicate on Watchers.
//------------------------------------------------------------------------------

#ifndef WATCHER_H
#define WATCHER_H

#include <Messenger.h>

// Watcher
class Watcher {
public:
	Watcher(const BMessenger &target);
	virtual ~Watcher();

	const BMessenger &Target() const;

	virtual status_t SendMessage(BMessage *message);

private:
	BMessenger	fTarget;
};

// WatcherFilter
class WatcherFilter {
public:
	WatcherFilter();
	virtual ~WatcherFilter();

	virtual bool Filter(Watcher *watcher, BMessage *message);
};

#endif	// WATCHER_H
