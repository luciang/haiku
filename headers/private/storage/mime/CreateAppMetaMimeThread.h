//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file CreateAppMetaMimeThread.h
	CreateAppMetaMimeThread interface declaration 
*/

#ifndef _CREATE_APP_META_MIME_THREAD_H
#define _CREATE_APP_META_MIME_THREAD_H

#include <mime/MimeUpdateThread.h>

namespace BPrivate {
namespace Storage {
namespace Mime {

class CreateAppMetaMimeThread : public MimeUpdateThread {
public:
	CreateAppMetaMimeThread(const char *name, int32 priority, BMessenger managerMessenger,
		const entry_ref *root, bool recursive, bool force, BMessage *replyee);
	status_t DoMimeUpdate(const entry_ref *entry, bool *entryIsDir);
};
	
}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

#endif	// _CREATE_APP_META_MIME_THREAD_H
