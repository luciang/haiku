//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------

#include <RosterPrivate.h>
#include <Roster.h>

/*!	\class BRoster::Private
	\brief Class used to access private BRoster members.

	This way, the only friend BRoster needs is this class.
*/

// SetTo
/*!	\brief Initializes the roster.

	\param mainMessenger A BMessenger targeting the registrar application.
	\param mimeMessenger A BMessenger targeting the MIME manager.
*/
void
BRoster::Private::SetTo(BMessenger mainMessenger, BMessenger mimeMessenger)
{
	if (fRoster) {
		fRoster->fMess = mainMessenger;
		fRoster->fMimeMess = mimeMessenger;
	}
}

// SendTo
/*!	\brief Sends a message to the registrar.

	\a mime specifies whether to send the message to the roster or to the
	MIME data base service.
	If \a reply is not \c NULL, the function waits for a reply.

	\param message The message to be sent.
	\param reply A pointer to a pre-allocated BMessage into which the reply
		   message will be copied.
	\param mime \c true, if the message should be sent to the MIME data base
		   service, \c false for the roster.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a message.
	- \c B_NO_INIT: the roster is \c NULL.
	- another error code
*/
status_t
BRoster::Private::SendTo(BMessage *message, BMessage *reply, bool mime)
{
	status_t error = (message ? B_OK : B_BAD_VALUE);
	if (error == B_OK && !fRoster)
		error = B_NO_INIT;
	if (error == B_OK) {
		if (mime)
			error = fRoster->fMimeMess.SendMessage(message, reply);
		else
			error = fRoster->fMess.SendMessage(message, reply);
	}
	return error;
}

// IsMessengerValid
/*!	\brief Returns whether the roster's messengers are valid.

	\a mime specifies whether to check the roster messenger or the one of
	the MIME data base service.

	\param mime \c true, if the MIME data base service messenger should be
		   checked, \c false for the roster messenger.
	\return \true, if the selected messenger is valid, \c false otherwise.
*/
bool
BRoster::Private::IsMessengerValid(bool mime) const
{
	return (fRoster && (mime ? fRoster->fMimeMess.IsValid()
							 : fRoster->fMess.IsValid()));
}


// _init_roster_
/*!	\brief Initializes the global be_roster variable.

	Called before the global constructors are invoked.

	\return Unknown!

	\todo Investigate what the return value means.
*/
int
_init_roster_()
{
	be_roster = new BRoster;
	return 0;
}

// _delete_roster_
/*!	\brief Deletes the global be_roster.

	Called after the global destructors are invoked.

	\return Unknown!

	\todo Investigate what the return value means.
*/
int
_delete_roster_()
{
	delete be_roster;
	return 0;
}

