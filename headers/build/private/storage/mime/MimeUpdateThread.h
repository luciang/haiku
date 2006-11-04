//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file MimeUpdateThread.h
	MimeUpdateThread interface declaration 
*/

#ifndef _MIME_UPDATE_THREAD_H
#define _MIME_UPDATE_THREAD_H

#include <Entry.h>
#include <Messenger.h>
//#include <RegistrarThread.h>
#include <SupportDefs.h>

#include <list>
#include <utility>

struct entry_ref;
class BMessage;

namespace BPrivate {
namespace Storage {
namespace Mime {

class MimeUpdateThread /*: public RegistrarThread*/ {
public:
	MimeUpdateThread(const char *name, int32 priority,
		BMessenger managerMessenger, const entry_ref *root, bool recursive,
		int32 force, BMessage *replyee);
	virtual ~MimeUpdateThread();
	
	virtual status_t InitCheck();	

	status_t DoUpdate()	{ return ThreadFunction(); }

protected:
	virtual status_t ThreadFunction();
	virtual status_t DoMimeUpdate(const entry_ref *entry, bool *entryIsDir) = 0;
	
	const entry_ref fRoot;
	const bool fRecursive;
	const int32 fForce;
	BMessage *fReplyee;
	
	bool DeviceSupportsAttributes(dev_t device);

private:
	std::list< std::pair<dev_t, bool> > fAttributeSupportList;

	status_t UpdateEntry(const entry_ref *ref);
	
	status_t fStatus;
};

}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

#endif	// _MIME_UPDATE_THREAD_H
