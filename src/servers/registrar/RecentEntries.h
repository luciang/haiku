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
//	File Name:		RecentEntries.h
//	Author:			Tyler Dauwalder (tyler@dauwalder.net)
//	Description:	Recently used entries list
//------------------------------------------------------------------------------
/*! \file RecentEntries.h
	\brief RecentEntries class declarations
*/

#ifndef RECENT_ENTRIES_H
#define RECENT_ENTRIES_H

#include <SupportDefs.h>

#include <list>

struct entry_ref;
struct recent_entry;

class RecentEntries {
public:
	RecentEntries();
	~RecentEntries();
	
	status_t Add(const entry_ref *ref, const char *appSig);
	status_t Get(int32 maxCount, const char *fileTypes[], int32 fileTypesCount,
	             const char *appSig, BMessage *result);
	status_t Clear();
	status_t Print();
private:
	static status_t GetTypeForRef(const entry_ref *ref, char *result);
	
	std::list<recent_entry*> fEntryList;
};

#endif	// RECENT_FOLDERS_H
