//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file MimeUpdateThread.cpp
	MimeUpdateThread implementation
*/

#include "mime/MimeUpdateThread.h"

#include <Directory.h>
#include <Message.h>
#include <Path.h>
#include <RegistrarDefs.h>

#include <stdio.h>

//#define DBG(x) x
#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {

/*!	\class MimeUpdateThread
	\brief RegistrarThread class implementing the common functionality of
	update_mime_info() and create_app_meta_mime()
*/

// constructor
/*! \brief Creates a new MimeUpdateThread object.

	If \a replyee is non-NULL and construction succeeds, the MimeThreadObject
	assumes resposibility for its deletion.
	
	Also, if \c non-NULL, \a replyee is expected to be a \c B_REG_MIME_UPDATE_MIME_INFO
	or a \c B_REG_MIME_CREATE_APP_META_MIME	message with a \c true \c "synchronous"
	field detached from the registrar's	mime manager looper (though this is not verified).
	The message will be	replied to at the end of the thread's execution.
*/
MimeUpdateThread::MimeUpdateThread(const char *name, int32 priority, BMessenger managerMessenger,
	const entry_ref *root, bool recursive, bool force, BMessage *replyee)
	: RegistrarThread(name, priority, managerMessenger)
	, fRoot(root ? *root : entry_ref())
	, fRecursive(recursive)
	, fForce(force)
	, fReplyee(replyee)
	, fStatus(root ? B_OK : B_BAD_VALUE)
{	
}

// destructor
/*!	\brief Destroys the MimeUpdateThread object.

	If the object was properly initialized (i.e. InitCheck() returns \c B_OK) and
	the replyee message passed to the constructor was \c non-NULL, the replyee
	message is deleted.
*/
MimeUpdateThread::~MimeUpdateThread()
{
	if (InitCheck() == B_OK)
		delete fReplyee;
}

// InitCheck()
/*! \brief Returns the initialization status of the object
*/
status_t
MimeUpdateThread::InitCheck()
{
	status_t err = RegistrarThread::InitCheck();
	if (!err)
		err = fStatus;
	return err;
}

// ThreadFunction
/*! \brief Implements the common functionality of update_mime_info() and
	create_app_meta_mime(), namely iterating through the filesystem and
	updating entries.
*/
status_t
MimeUpdateThread::ThreadFunction()
{
	status_t err = InitCheck();
	// Do the updates
	if (!err)
		err = UpdateEntry(&fRoot);
	// Send a reply if we have a message to reply to
	if (fReplyee) {
		BMessage reply(B_REG_RESULT);
		status_t error = reply.AddInt32("result", err);
		err = error;
		if (!err)
			err = fReplyee->SendReply(&reply);
	}
	// Flag ourselves as finished
	fIsFinished = true;
	// Notify the thread manager to make a cleanup run
	if (!err) {
		BMessage msg(B_REG_MIME_UPDATE_THREAD_FINISHED);
		status_t error = fManagerMessenger.SendMessage(&msg, (BHandler*)NULL, 500000);
		if (error)
			OUT("WARNING: ThreadManager::ThreadEntryFunction(): Termination notification "
				"failed with error 0x%lx\n", error);
	}
	DBG(OUT("(id: %ld) exiting mime update thread with result 0x%lx\n",
		find_thread(NULL), err));
}

// UpdateEntry
/*! \brief Updates the given entry and then recursively updates all the entry's child
	entries	if the entry is a directory and \c fRecursive is true.
*/
status_t
MimeUpdateThread::UpdateEntry(const entry_ref *ref)
{
	status_t err = ref ? B_OK : B_BAD_VALUE;
	bool entryIsDir = false;
	
	// Update this entry
	if (!err) {
		DoMimeUpdate(ref, &entryIsDir);
//		err = DoMimeUpdate(ref, &entryIsDir);
/*		BPath path(ref);
		printf("Updating '%s'... ", path.Path());
		fflush(stdout);
		printf("0x%lx\n", DoMimeUpdate(ref, &entryIsDir));
*/
	}
	// Look to see if we're being terminated
	if (!err && fShouldExit)
		err = B_CANCELED;
	// If we're recursing and this is a directory, update
	// each of the directory's children as well
	if (!err && fRecursive && entryIsDir) {		
		BDirectory dir;		
		err = dir.SetTo(ref);
		if (!err) {
			entry_ref childRef;
			while (!err) {
				err = dir.GetNextRef(&childRef);
				if (err) {
					// If we've come to the end of the directory listing,
					// it's not an error.
					if (err == B_ENTRY_NOT_FOUND)
					 	err = B_OK;
					break;
				} else {
					err = UpdateEntry(&childRef);				
				}			
			}		
		}			
	}
	return err;			  
}

}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate
