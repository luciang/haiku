//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file UpdateMimeInfoThread.h
	UpdateMimeInfoThread interface declaration 
*/

#ifndef _MIME_UPDATE_MIME_INFO_THREAD_H
#define _MIME_UPDATE_MIME_INFO_THREAD_H

#include <mime/MimeUpdateThread.h>

namespace BPrivate {
namespace Storage {
namespace Mime {

class UpdateMimeInfoThread : public MimeUpdateThread {
public:
	UpdateMimeInfoThread(const char *name, int32 priority,
		BMessenger managerMessenger, const entry_ref *root, bool recursive,
		int32 force, BMessage *replyee);
	status_t DoMimeUpdate(const entry_ref *entry, bool *entryIsDir);
};
	
}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

#endif	// _MIME_UPDATE_MIME_INFO_THREAD_H
