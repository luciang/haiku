/*
 * Copyright 2001-2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold (bonefish@users.sf.net)
 */

/*!	BRoster class lets you launch apps and keeps track of apps
	that are running. 
	Global be_roster represents the default BRoster.
	app_info structure provides info for a running app.
*/

#include <AppFileInfo.h>
#include <Application.h>
#include <AppMisc.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_index.h>
#include <fs_info.h>
#include <image.h>
#include <List.h>
#include <MessengerPrivate.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <OS.h>
#include <Path.h>
#include <PortLink.h>
#include <Query.h>
#include <RegistrarDefs.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <ServerProtocol.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <ctype.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace std;
using namespace BPrivate;

// debugging
//#define DBG(x) x
#define DBG(x)
#ifdef DEBUG_PRINTF
	#define OUT DEBUG_PRINTF
#else
	#define OUT	printf
#endif

enum {
	NOT_IMPLEMENTED	= B_ERROR,
};

// helper function prototypes
static status_t find_message_app_info(BMessage *message, app_info *info);
static status_t query_for_app(const char *signature, entry_ref *appRef);
static status_t can_app_be_used(const entry_ref *ref);
static int32 compare_version_infos(const version_info &info1,
								   const version_info &info2);
static int32 compare_app_versions(const entry_ref *app1,
								  const entry_ref *app2);


const BRoster *be_roster;


//	#pragma mark - app_info


/*!	\brief Creates an uninitialized app_info.
*/
app_info::app_info()
	:
	thread(-1),
	team(-1),
	port(-1),
	flags(B_REG_DEFAULT_APP_FLAGS),
	ref()
{
	signature[0] = '\0';
}


/*!	\brief Does nothing.
*/
app_info::~app_info()
{
}


//	#pragma mark - BRoster::ArgVector


class BRoster::ArgVector {
	public:
		ArgVector();
		~ArgVector();
		status_t Init(int argc, const char *const *args, const entry_ref *appRef,
					  const entry_ref *docRef);
		void Unset();
		inline int Count() const { return fArgc; }
		inline const char *const *Args() const { return fArgs; }

	private:
		int			fArgc;
		const char	**fArgs;
		BPath		fAppPath;
		BPath		fDocPath;
};


/*!	\brief Creates an uninitialized ArgVector.
*/
BRoster::ArgVector::ArgVector()
	:
	fArgc(0),
	fArgs(NULL),
	fAppPath(),
	fDocPath()
{
}


/*!	\brief Frees all resources associated with the ArgVector.
*/
BRoster::ArgVector::~ArgVector()
{
	Unset();
}


/*!	\brief Initilizes the object according to the supplied parameters.

	If the initialization succeeds, the methods Count() and Args() grant
	access to the argument count and vector created by this methods.
	\note The returned vector is valid only as long as the elements of the
	supplied \a args (if any) are valid and this object is not destroyed.
	This object retains ownership of the vector returned by Args().
	In case of error, the value returned by Args() is invalid (or \c NULL).

	The argument vector is created as follows: First element is the path
	of the entry \a appRef refers to, then follow all elements of \a args
	and then, if \a args has at least one element and \a docRef can be
	resolved to a path, the path of the entry \a docRef refers to. That is,
	if no or an empty \a args vector is supplied, the resulting argument
	vector contains only one element, the path associated with \a appRef.

	\param argc Specifies the number of elements \a args contains.
	\param args Argument vector. May be \c NULL.
	\param appRef entry_ref referring to the entry whose path shall be the
		   first element of the resulting argument vector.
	\param docRef entry_ref referring to the entry whose path shall be the
		   last element of the resulting argument vector. May be \c NULL.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a appRef.
	- \c B_ENTRY_NOT_FOUND or other file system error codes: \a appRef could
	  not be resolved to a path.
	- \c B_NO_MEMORY: Not enough memory to allocate for this operation.
*/
status_t
BRoster::ArgVector::Init(int argc, const char *const *args,
	const entry_ref *appRef, const entry_ref *docRef)
{
	// unset old values
	Unset();
	status_t error = (appRef ? B_OK : B_BAD_VALUE);
	// get app path
	if (error == B_OK)
		error = fAppPath.SetTo(appRef);
	// determine number of arguments
	bool hasDocArg = false;
	if (error == B_OK) {
		fArgc = 1;
		if (argc > 0 && args) {
			fArgc += argc;
			if (docRef && fDocPath.SetTo(docRef) == B_OK) {
				fArgc++;
				hasDocArg = true;
			}
		}
		fArgs = new(nothrow) const char*[fArgc + 1];	// + 1 for term. NULL
		if (!fArgs)
			error = B_NO_MEMORY;
	}
	// init vector
	if (error == B_OK) {
		fArgs[0] = fAppPath.Path();
		if (argc > 0 && args) {
			for (int i = 0; i < argc; i++)
				fArgs[i + 1] = args[i];
			if (hasDocArg)
				fArgs[fArgc - 1] = fDocPath.Path();
		}
		// NULL terminate (e.g. required by load_image())
		fArgs[fArgc] = NULL;
	}
	return error;
}

// Unset
/*!	\brief Uninitializes the object.
*/
void
BRoster::ArgVector::Unset()
{
	fArgc = 0;
	delete[] fArgs;
	fArgs = NULL;
	fAppPath.Unset();
	fDocPath.Unset();
}


//	#pragma mark - BRoster


BRoster::BRoster()
	:
	fMessenger(),
	fMimeMessenger()
{
	_InitMessengers();
}


BRoster::~BRoster()
{
}


//	#pragma mark - Querying for apps


/*!	\brief Returns whether or not an application with the supplied signature
		   is currently running.
	\param mimeSig The app signature
	\return \c true, if the supplied signature is not \c NULL and an
			application with this signature is running, \c false otherwise.
*/
bool
BRoster::IsRunning(const char *mimeSig) const
{
	return (TeamFor(mimeSig) >= 0);
}

// IsRunning
/*!	\brief Returns whether or not an application ran from an executable
		   referred to by the supplied entry_ref is currently running.
	\param ref The app's entry_ref
	\return \c true, if the supplied entry_ref is not \c NULL and an
			application executing this file is running, \c false otherwise.
*/
bool
BRoster::IsRunning(entry_ref *ref) const
{
	return (TeamFor(ref) >= 0);
}

// TeamFor
/*!	\brief Returns the team ID of a currently running application with the
		   supplied signature.
	\param mimeSig The app signature
	\return
	- The team ID of a running application with the supplied signature.
	- \c B_BAD_VALUE: \a mimeSig is \c NULL.
	- \c B_ERROR: No application with the supplied signature is currently
	  running.
*/
team_id
BRoster::TeamFor(const char *mimeSig) const
{
	team_id team;
	app_info info;
	status_t error = GetAppInfo(mimeSig, &info);
	if (error == B_OK)
		team = info.team;
	else
		team = error;
	return team;
}

// TeamFor
/*!	\brief Returns the team ID of a currently running application executing
		   the executable referred to by the supplied entry_ref.
	\param ref The app's entry_ref
	\return
	- The team ID of a running application executing the file referred to by
	  \a ref.
	- \c B_BAD_VALUE: \a ref is \c NULL.
	- \c B_ERROR: No application executing the file referred to by \a ref is
	  currently running.
*/
team_id
BRoster::TeamFor(entry_ref *ref) const
{
	team_id team;
	app_info info;
	status_t error = GetAppInfo(ref, &info);
	if (error == B_OK)
		team = info.team;
	else
		team = error;
	return team;
}

// GetAppList
/*!	\brief Returns a list of all currently running applications.

	The supplied list is not emptied before adding the team IDs of the
	running applications. The list elements are team_id's, not pointers.

	\param teamIDList A pointer to a pre-allocated BList to be filled with
		   the team IDs.
*/
void
BRoster::GetAppList(BList *teamIDList) const
{
	status_t error = (teamIDList ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_LIST);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			team_id team;
			for (int32 i = 0; reply.FindInt32("teams", i, &team) == B_OK; i++)
				teamIDList->AddItem((void*)team);
		} else {
			reply.FindInt32("error", &error);
			DBG(OUT("Roster request unsuccessful: %s\n", strerror(error)));
			DBG(reply.PrintToStream());
		}
	} else {
		DBG(OUT("Sending message to roster failed: %s\n", strerror(error)));
	}
}

// GetAppList
/*!	\brief Returns a list of all currently running applications with the
		   specified signature.

	The supplied list is not emptied before adding the team IDs of the
	running applications. The list elements are team_id's, not pointers.
	If \a sig is \c NULL or invalid, no team IDs are added to the list.

	\param sig The app signature
	\param teamIDList A pointer to a pre-allocated BList to be filled with
		   the team IDs.
*/
void
BRoster::GetAppList(const char *sig, BList *teamIDList) const
{
	status_t error = (sig && teamIDList ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_LIST);
	if (error == B_OK)
		error = request.AddString("signature", sig);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			team_id team;
			for (int32 i = 0; reply.FindInt32("teams", i, &team) == B_OK; i++)
				teamIDList->AddItem((void*)team);
		} else
			reply.FindInt32("error", &error);
	}
}

// GetAppInfo
/*!	\brief Returns the app_info of a currently running application with the
		   supplied signature.
	\param sig The app signature
	\param info A pointer to a pre-allocated app_info structure to be filled
		   in by this method.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \a sig is \c NULL.
	- \c B_ERROR: No application with the supplied signature is currently
	  running.
*/
status_t
BRoster::GetAppInfo(const char *sig, app_info *info) const
{
	status_t error = (sig && info ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	if (error == B_OK)
		error = request.AddString("signature", sig);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else
			reply.FindInt32("error", &error);
	}
	return error;
}

// GetAppInfo
/*!	\brief Returns the app_info of a currently running application executing
		   the executable referred to by the supplied entry_ref.
	\param ref The app's entry_ref
	\param info A pointer to a pre-allocated app_info structure to be filled
		   in by this method.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \a ref is \c NULL.
	- \c B_ERROR: No application executing the file referred to by \a ref is
	  currently running.
*/
status_t
BRoster::GetAppInfo(entry_ref *ref, app_info *info) const
{
	status_t error = (ref && info ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	if (error == B_OK)
		error = request.AddRef("ref", ref);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else
			reply.FindInt32("error", &error);
	}
	return error;
}

// GetRunningAppInfo
/*!	\brief Returns the app_info of a currently running application identified
		   by the supplied team ID.
	\param team The app's team ID
	\param info A pointer to a pre-allocated app_info structure to be filled
		   in by this method.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \a info is \c NULL.
	- \c B_BAD_TEAM_ID: \a team does not identify a running application.
*/
status_t
BRoster::GetRunningAppInfo(team_id team, app_info *info) const
{
	status_t error = (info ? B_OK : B_BAD_VALUE);
	if (error == B_OK && team < 0)
		error = B_BAD_TEAM_ID;
	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	if (error == B_OK)
		error = request.AddInt32("team", team);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else
			reply.FindInt32("error", &error);
	}
	return error;
}

// GetActiveAppInfo
/*!	\brief Returns the app_info of a currently active application.
	\param info A pointer to a pre-allocated app_info structure to be filled
		   in by this method.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \a info is \c NULL.
	- \c B_ERROR: Currently no application is active.
*/
status_t
BRoster::GetActiveAppInfo(app_info *info) const
{
	status_t error = (info ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else
			reply.FindInt32("error", &error);
	}
	return error;
}

// FindApp
/*!	\brief Finds an application associated with a MIME type.

	The method gets the signature of the supplied type's preferred application
	or, if it doesn't have a preferred application, the one of its supertype.
	Then the MIME database is asked which executable is associated with the
	signature. If the database doesn't have a reference to an exectuable, the
	boot volume is queried for a file with the signature. If more than one
	file has been found, the one with the greatest version is picked, or if
	no file has a version info, the one with the most recent modification
	date.

	\param mimeType The MIME type for which an application shall be found.
	\param app A pointer to a pre-allocated entry_ref to be filled with
		   a reference to the found application's executable.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType or \a app.
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- other error codes
*/
status_t
BRoster::FindApp(const char *mimeType, entry_ref *app) const
{
	if (mimeType == NULL || app == NULL)
		return B_BAD_VALUE;

	return _ResolveApp(mimeType, NULL, app, NULL, NULL, NULL);
}

// FindApp
/*!	\brief Finds an application associated with a file.

	The method first checks, if the file has a preferred application
	associated with it (see BNodeInfo::GetPreferredApp()) and if so,
	tries to find the executable the same way FindApp(const char*, entry_ref*)
	does. If not, it gets the MIME type of the file and searches an
	application for it exactly like the first FindApp() method.

	The type of the file is defined in a file attribute (BNodeInfo::GetType()),
	but if it is not set yet, the method tries to guess it via
	BMimeType::GuessMimeType().

	As a special case the file may have execute permission. Then preferred
	application and type are ignored and an entry_ref to the file itself is
	returned.

	\param ref An entry_ref referring to the file for which an application
		   shall be found.
	\param app A pointer to a pre-allocated entry_ref to be filled with
		   a reference to the found application's executable.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType or \a app.
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- other error codes
*/
status_t
BRoster::FindApp(entry_ref *ref, entry_ref *app) const
{
	if (ref == NULL || app == NULL)
		return B_BAD_VALUE;

	entry_ref _ref(*ref);
	return _ResolveApp(NULL, &_ref, app, NULL, NULL, NULL);
}


//	#pragma mark - Launching, activating, and broadcasting to apps

// Broadcast
/*!	\brief Sends a message to all running applications.

	The methods doesn't broadcast the message itself, but it asks the roster
	to do so. It immediatly returns after sending the request. The return
	value only tells about whether the request has successfully been sent.

	The message is sent asynchronously. Replies to it go to the application.
	(\c be_app_messenger).

	\param message The message to be broadcast.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a message.
	- other error codes
*/
status_t
BRoster::Broadcast(BMessage *message) const
{
	return Broadcast(message, be_app_messenger);
}

// Broadcast
/*!	\brief Sends a message to all running applications.

	The methods doesn't broadcast the message itself, but it asks the roster
	to do so. It immediatly returns after sending the request. The return
	value only tells about whether the request has successfully been sent.

	The message is sent asynchronously. Replies to it go to the specified
	target (\a replyTo).

	\param message The message to be broadcast.
	\param replyTo Reply target for the message.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a message.
	- other error codes
*/
status_t
BRoster::Broadcast(BMessage *message, BMessenger replyTo) const
{
	status_t error = (message ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_BROADCAST);
	if (error == B_OK)
		error = request.AddInt32("team", BPrivate::current_team());
	if (error == B_OK)
		error = request.AddMessage("message", message);
	if (error == B_OK)
		error = request.AddMessenger("reply_target", replyTo);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// StartWatching
/*!	\brief Adds a new roster application monitor.

	After StartWatching() event messages will be sent to the supplied target
	according to the specified flags until a respective StopWatching() call.

	\a eventMask must be a bitwise OR of one or more of the following flags:
	- \c B_REQUEST_LAUNCHED: A \c B_SOME_APP_LAUNCHED is sent, whenever an
	  application has been launched.
	- \c B_REQUEST_QUIT: A \c B_SOME_APP_QUIT is sent, whenever an
	  application has quit.
	- \c B_REQUEST_ACTIVATED: A \c B_SOME_APP_ACTIVATED is sent, whenever an
	  application has been activated.

	All event messages contain the following fields supplying more information
	about the concerned application:
	- \c "be:signature", \c B_STRING_TYPE: The signature of the application.
	- \c "be:team", \c B_INT32_TYPE: The team ID of the application
	  (\c team_id).
	- \c "be:thread", \c B_INT32_TYPE: The ID of the application's main thread
	  (\c thread_id).
	- \c "be:flags", \c B_INT32_TYPE: The application flags (\c uint32).
	- \c "be:ref", \c B_REF_TYPE: An entry_ref referring to the application's
	  executable.

	A second call to StartWatching() with the same \a target simply sets
	the new \a eventMask. The messages won't be sent twice to the target.

	\param target The target the event messages shall be sent to.
	\param eventMask Specifies the events the caller is interested in.
	\return
	- \c B_OK: Everything went fine.
	- an error code, if some error occured.
*/
status_t
BRoster::StartWatching(BMessenger target, uint32 eventMask) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_START_WATCHING);
	if (error == B_OK)
		error = request.AddMessenger("target", target);
	if (error == B_OK)
		error = request.AddInt32("events", (int32)eventMask);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// StopWatching
/*!	\brief Removes a roster application monitor added with StartWatching().
	\param target The target that shall not longer receive any event messages.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: No application monitor has been associated with the
	  specified \a target before.
*/
status_t
BRoster::StopWatching(BMessenger target) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_STOP_WATCHING);
	if (error == B_OK)
		error = request.AddMessenger("target", target);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// ActivateApp
/*!	\brief Activates the application identified by the supplied team ID.
	\param team The app's team ID
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_TEAM_ID: \a team does not identify a running application.
*/
status_t
BRoster::ActivateApp(team_id team) const
{
	// get the app server port
	port_id port = find_port(SERVER_PORT_NAME);
	if (port < B_OK)
		return port;

	// create a reply port
	struct ReplyPort {
		ReplyPort()
			: port(create_port(1, "activate app reply"))
		{
		}

		~ReplyPort()
		{
			if (port >= 0)
				delete_port(port);
		}

		port_id port;

	} replyPort;

	if (replyPort.port < 0)
		return replyPort.port;

	BPrivate::PortLink link(port, replyPort.port);

	// We can't use AppServerLink because be_app may be NULL
	link.StartMessage(AS_GET_DESKTOP);
	link.Attach<port_id>(replyPort.port);
	link.Attach<int32>(getuid());

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return B_ERROR;

	// we now talk to the desktop
	link.Read<port_id>(&port);
	link.SetSenderPort(port);

	// prepare the message
	status_t error = link.StartMessage(AS_ACTIVATE_APP);
	if (error != B_OK)
		return error;

	error = link.Attach(replyPort.port);
	if (error != B_OK)
		return error;

	error = link.Attach(team);
	if (error != B_OK)
		return error;

	// send it
	error = link.FlushWithReply(code);
	if (error != B_OK)
		return error;

	return code;
}

// Launch
/*!	\brief Launches the application associated with the supplied MIME type.

	The application to be started is searched the same way FindApp() does it.

	\a initialMessage is a message to be sent to the application "on launch",
	i.e. before ReadyToRun() is invoked on the BApplication object. The
	caller retains ownership of the supplied BMessage. In case the method
	fails with \c B_ALREADY_RUNNING the message is delivered to the already
	running instance.

	\param mimeType MIME type for which the application shall be launched.
	\param initialMessage Optional message to be sent to the application
		   "on launch". May be \c NULL.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- \c B_ALREADY_RUNNING: The application's app flags specify
	  \c B_SINGLE_LAUNCH or B_EXCLUSIVE_LAUNCH and the application (the very
	  same (single) or at least one with the same signature (exclusive)) is
	  already running.
	- other error codes
*/
status_t
BRoster::Launch(const char *mimeType, BMessage *initialMessage,
	team_id *appTeam) const
{
	if (mimeType == NULL)
		return B_BAD_VALUE;

	BList messageList;
	if (initialMessage)
		messageList.AddItem(initialMessage);

	return _LaunchApp(mimeType, NULL, &messageList, 0, NULL, appTeam);
}

// Launch
/*!	\brief Launches the application associated with the supplied MIME type.

	The application to be started is searched the same way FindApp() does it.

	\a messageList contains messages to be sent to the application
	"on launch", i.e. before ReadyToRun() is invoked on the BApplication
	object. The caller retains ownership of the supplied BList and the
	contained BMessages. In case the method fails with \c B_ALREADY_RUNNING
	the messages are delivered to the already running instance.

	\param mimeType MIME type for which the application shall be launched.
	\param messageList Optional list of messages to be sent to the application
		   "on launch". May be \c NULL.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- other error codes
*/
status_t
BRoster::Launch(const char *mimeType, BList *messageList,
	team_id *appTeam) const
{
	if (mimeType == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(mimeType, NULL, messageList, 0, NULL, appTeam);
}

// Launch
/*!	\brief Launches the application associated with the supplied MIME type.

	The application to be started is searched the same way FindApp() does it.

	The supplied \a argc and \a args are (if containing at least one argument)
	put into a \c B_ARGV_RECEIVED message and sent to the launched application
	"on launch". The caller retains ownership of the supplied \a args.
	In case the method fails with \c B_ALREADY_RUNNING the message is
	delivered to the already running instance.

	\param mimeType MIME type for which the application shall be launched.
	\param argc Specifies the number of elements in \a args.
	\param args An array of C-strings to be sent as B_ARGV_RECEIVED messaged
		   to the launched application.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- other error codes
*/
status_t
BRoster::Launch(const char *mimeType, int argc, char **args,
	team_id *appTeam) const
{
	if (mimeType == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(mimeType, NULL, NULL, argc, args, appTeam);
}

// Launch
/*!	\brief Launches the application associated with the entry referred to by
		   the supplied entry_ref.

	The application to be started is searched the same way FindApp() does it.

	If \a ref does refer to an application executable, that application is
	launched. Otherwise the respective application is searched and launched,
	and \a ref is sent to it in a \c B_REFS_RECEIVED message.

	\a initialMessage is a message to be sent to the application "on launch",
	i.e. before ReadyToRun() is invoked on the BApplication object. The
	caller retains ownership of the supplied BMessage. In case the method
	fails with \c B_ALREADY_RUNNING the message is delivered to the already
	running instance. The same applies to the \c B_REFS_RECEIVED message.

	\param ref entry_ref referring to the file for which an application shall
		   be launched.
	\param initialMessage Optional message to be sent to the application
		   "on launch". May be \c NULL.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- \c B_ALREADY_RUNNING: The application's app flags specify
	  \c B_SINGLE_LAUNCH or B_EXCLUSIVE_LAUNCH and the application (the very
	  same (single) or at least one with the same signature (exclusive)) is
	  already running.
	- other error codes
*/
status_t
BRoster::Launch(const entry_ref *ref, const BMessage *initialMessage,
	team_id *appTeam) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	BList messageList;
	if (initialMessage)
		messageList.AddItem(const_cast<BMessage*>(initialMessage));

	return _LaunchApp(NULL, ref, &messageList, 0, NULL, appTeam);
}

// Launch
/*!	\brief Launches the application associated with the entry referred to by
		   the supplied entry_ref.

	The application to be started is searched the same way FindApp() does it.

	If \a ref does refer to an application executable, that application is
	launched. Otherwise the respective application is searched and launched,
	and \a ref is sent to it in a \c B_REFS_RECEIVED message.

	\a messageList contains messages to be sent to the application
	"on launch", i.e. before ReadyToRun() is invoked on the BApplication
	object. The caller retains ownership of the supplied BList and the
	contained BMessages. In case the method fails with \c B_ALREADY_RUNNING
	the messages are delivered to the already running instance. The same
	applies to the \c B_REFS_RECEIVED message.

	\param ref entry_ref referring to the file for which an application shall
		   be launched.
	\param messageList Optional list of messages to be sent to the application
		   "on launch". May be \c NULL.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- other error codes
*/
status_t
BRoster::Launch(const entry_ref *ref, const BList *messageList,
	team_id *appTeam) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(NULL, ref, messageList, 0, NULL, appTeam);
}

// Launch
/*!	\brief Launches the application associated with the entry referred to by
		   the supplied entry_ref.

	The application to be started is searched the same way FindApp() does it.

	If \a ref does refer to an application executable, that application is
	launched. Otherwise the respective application is searched and launched,
	and \a ref is sent to it in a \c B_REFS_RECEIVED message, unless other
	arguments are passed via \a argc and \a args -- then the entry_ref is
	converted into a path (C-string) and added to the argument vector.

	The supplied \a argc and \a args are (if containing at least one argument)
	put into a \c B_ARGV_RECEIVED message and sent to the launched application
	"on launch". The caller retains ownership of the supplied \a args.
	In case the method fails with \c B_ALREADY_RUNNING the message is
	delivered to the already running instance. The same applies to the
	\c B_REFS_RECEIVED message, if no arguments are supplied via \a argc and
	\args.

	\param ref entry_ref referring to the file for which an application shall
		   be launched.
	\param argc Specifies the number of elements in \a args.
	\param args An array of C-strings to be sent as B_ARGV_RECEIVED messaged
		   to the launched application.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- other error codes
*/
status_t
BRoster::Launch(const entry_ref *ref, int argc, const char * const *args,
				team_id *appTeam) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(NULL, ref, NULL, argc, args, appTeam);
}

// TODO: just here for providing binary compatibility
// (for example "Guido" needs this)
extern "C"
status_t
Launch__C7BRosterP9entry_refP8BMessagePl(BRoster *roster,
										 entry_ref* ref,
										 BMessage* initialMessage)
{
	return roster->BRoster::Launch(ref, initialMessage, NULL);
}

//	#pragma mark - Recent document and app support

// GetRecentDocuments
void
BRoster::GetRecentDocuments(BMessage *refList, int32 maxCount,
	const char *fileType, const char *appSig) const
{
	if (!refList)
		return;

	status_t err = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage &msg = *refList;
	BMessage &reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (!err) {
		msg.what = B_REG_GET_RECENT_DOCUMENTS;
		err = msg.AddInt32("max count", maxCount);
	}
	if (!err && fileType)
		err = msg.AddString("file type", fileType);
	if (!err && appSig)
		err = msg.AddString("app sig", appSig);
	fMessenger.SendMessage(&msg, &reply);
	if (!err)
		err = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (!err)
		err = reply.FindInt32("result", &result);
	if (!err) 
		err = result;
	// Clear the result if an error occured
	if (err && refList)
		refList->MakeEmpty();
	// No return value, how sad :-(
//	return err;	
}

// GetRecentDocuments
void
BRoster::GetRecentDocuments(BMessage *refList, int32 maxCount,
							const char *fileTypes[], int32 fileTypesCount,
							const char *appSig) const
{
	if (!refList)
		return;

	status_t err = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage &msg = *refList;
	BMessage &reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (!err) {
		msg.what = B_REG_GET_RECENT_DOCUMENTS;
		err = msg.AddInt32("max count", maxCount);
	}
	if (!err && fileTypes) {
		for (int i = 0; i < fileTypesCount && !err; i++)
			err = msg.AddString("file type", fileTypes[i]);
	}
	if (!err && appSig)
		err = msg.AddString("app sig", appSig);
	fMessenger.SendMessage(&msg, &reply);
	if (!err)
		err = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (!err)
		err = reply.FindInt32("result", &result);
	if (!err) 
		err = result;
	// Clear the result if an error occured
	if (err && refList)
		refList->MakeEmpty();
	// No return value, how sad :-(
//	return err;	
}

// GetRecentFolders
void
BRoster::GetRecentFolders(BMessage *refList, int32 maxCount,
						  const char *appSig) const
{
	if (!refList)
		return;

	status_t err = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage &msg = *refList;
	BMessage &reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (!err) {
		msg.what = B_REG_GET_RECENT_FOLDERS;
		err = msg.AddInt32("max count", maxCount);
	}
	if (!err && appSig)
		err = msg.AddString("app sig", appSig);
	fMessenger.SendMessage(&msg, &reply);
	if (!err)
		err = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (!err)
		err = reply.FindInt32("result", &result);
	if (!err) 
		err = result;
	// Clear the result if an error occured
	if (err && refList)
		refList->MakeEmpty();
	// No return value, how sad :-(
//	return err;	
}

// GetRecentApps
void
BRoster::GetRecentApps(BMessage *refList, int32 maxCount) const
{
	if (!refList)
		return;

	status_t err = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage &msg = *refList;
	BMessage &reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (!err) {
		msg.what = B_REG_GET_RECENT_APPS;
		err = msg.AddInt32("max count", maxCount);
	}
	fMessenger.SendMessage(&msg, &reply);
	if (!err)
		err = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (!err)
		err = reply.FindInt32("result", &result);
	if (!err) 
		err = result;
	// Clear the result if an error occured
	if (err && refList)
		refList->MakeEmpty();
	// No return value, how sad :-(
//	return err;	
}

// AddToRecentDocuments
void
BRoster::AddToRecentDocuments(const entry_ref *doc, const char *appSig) const
{
	status_t err = doc ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage msg(B_REG_ADD_TO_RECENT_DOCUMENTS);
	BMessage reply;
	status_t result;
	char *callingAppSig = NULL;
	
	// If no signature is supplied, look up the signature of
	// the calling app
	if (!err && !appSig) {
		app_info info;
		err = GetRunningAppInfo(be_app->Team(), &info);
		if (!err)
			callingAppSig = info.signature;	
	}

	// Build and send the message, read the reply
	if (!err) 
		err = msg.AddRef("ref", doc);
	if (!err)
		err = msg.AddString("app sig", (appSig ? appSig : callingAppSig));
	fMessenger.SendMessage(&msg, &reply);
	if (!err)
		err = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (!err)
		err = reply.FindInt32("result", &result);
	if (!err) 
		err = result;
	if (err)
		DBG(OUT("WARNING: BRoster::AddToRecentDocuments() failed with error 0x%lx\n", err));
}

// AddToRecentFolders
void
BRoster::AddToRecentFolders(const entry_ref *folder, const char *appSig) const
{
	status_t err = folder ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage msg(B_REG_ADD_TO_RECENT_FOLDERS);
	BMessage reply;
	status_t result;
	char *callingAppSig = NULL;
	
	// If no signature is supplied, look up the signature of
	// the calling app
	if (!err && !appSig) {
		app_info info;
		err = GetRunningAppInfo(be_app->Team(), &info);
		if (!err)
			callingAppSig = info.signature;	
	}

	// Build and send the message, read the reply
	if (!err) 
		err = msg.AddRef("ref", folder);
	if (!err)
		err = msg.AddString("app sig", (appSig ? appSig : callingAppSig));
	fMessenger.SendMessage(&msg, &reply);
	if (!err)
		err = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (!err)
		err = reply.FindInt32("result", &result);
	if (!err) 
		err = result;
	if (err)
		DBG(OUT("WARNING: BRoster::AddToRecentDocuments() failed with error 0x%lx\n", err));
}


//	#pragma mark - Private or reserved


/*!	\brief Shuts down the system.

	When \c synchronous is \c true and the method succeeds, it doesn't return.

	\param reboot If \c true, the system will be rebooted instead of being
		   powered off.
	\param confirm If \c true, the user will be asked to confirm to shut down
		   the system.
	\param synchronous If \c false, the method will return as soon as the
		   shutdown process has been initiated successfully (or an error
		   occurred). Otherwise the method doesn't return, if successfully.
	\return
	- \c B_SHUTTING_DOWN, when there's already a shutdown process in
	  progress,
	- \c B_SHUTDOWN_CANCELLED, when the user cancelled the shutdown process,
	- another error code in case something went wrong.
*/
status_t
BRoster::_ShutDown(bool reboot, bool confirm, bool synchronous)
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_SHUT_DOWN);
	if (error == B_OK)
		error = request.AddBool("reboot", reboot);
	if (error == B_OK)
		error = request.AddBool("confirm", confirm);
	if (error == B_OK)
		error = request.AddBool("synchronous", synchronous);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);

	return error;
}


// AddApplication
/*!	\brief (Pre-)Registers an application with the registrar.

	This methods is invoked either to register or to pre-register an
	application. Full registration is requested by supplying \c true via
	\a fullReg.

	A full registration requires \a mimeSig, \a ref, \a flags, \a team,
	\a thread and \a port to contain valid values. No token will be return
	via \a pToken.

	For a pre-registration \a mimeSig, \a ref, \a flags must be valid.
	\a team and \a thread are optional and should be set to -1, if they are
	unknown. If no team ID is supplied, \a pToken should be valid and, if the
	the pre-registration succeeds, will be filled with a unique token assigned
	by the roster.

	In both cases the registration may fail, if single/exclusive launch is
	requested and an instance of the application is already running. Then
	\c B_ALREADY_RUNNING is returned and the team ID of the running instance
	is passed back via \a otherTeam, if supplied.

	\param mimeSig The app's signature
	\param ref An entry_ref referring to the app's executable
	\param flags The app flags
	\param team The app's team ID
	\param thread The app's main thread
	\param port The app looper port
	\param fullReg \c true for full, \c false for pre-registration
	\param pToken A pointer to a pre-allocated uint32 into which the token
		   assigned by the registrar is written (may be \c NULL)
	\param otherTeam A pointer to a pre-allocated team_id into which the
		   team ID of the already running instance of a single/exclusive
		   launch application is written (may be \c NULL)
	\return
	- \c B_OK: Everything went fine.
	- \c B_ENTRY_NOT_FOUND: \a ref doesn't refer to a file.
	- \c B_ALREADY_RUNNING: The application requests single/exclusive launch
	  and an instance is already running.
	- \c B_REG_ALREADY_REGISTERED: An application with the team ID \a team
	  is already registered.
*/
status_t
BRoster::_AddApplication(const char *mimeSig, const entry_ref *ref,
	uint32 flags, team_id team, thread_id thread,
	port_id port, bool fullReg, uint32 *pToken,
	team_id *otherTeam) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_ADD_APP);
	if (error == B_OK && mimeSig)
		error = request.AddString("signature", mimeSig);
	if (error == B_OK && ref)
		error = request.AddRef("ref", ref);
	if (error == B_OK)
		error = request.AddInt32("flags", (int32)flags);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	if (error == B_OK && thread >= 0)
		error = request.AddInt32("thread", thread);
	if (error == B_OK && port >= 0)
		error = request.AddInt32("port", port);
	if (error == B_OK)
		error = request.AddBool("full_registration", fullReg);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (!fullReg && team < 0) {
				uint32 token;
				if (reply.FindInt32("token", (int32*)&token) == B_OK) {
					if (pToken)
						*pToken = token;
				} else
					error = B_ERROR;
			}
		} else {
			reply.FindInt32("error", &error);
			if (otherTeam)
				reply.FindInt32("other_team", otherTeam);
		}
	}
	return error;
}

// SetSignature
/*!	\brief Sets an application's signature.

	The application must be registered or at pre-registered with a valid
	team ID.

	\param team The app's team ID.
	\param mimeSig The app's new signature.
	\return
	- \c B_OK: Everything went fine.
	- \c B_REG_APP_NOT_REGISTERED: The supplied team ID does not identify a
	  registered application.
*/
status_t
BRoster::_SetSignature(team_id team, const char *mimeSig) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_SET_SIGNATURE);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	if (error == B_OK && mimeSig)
		error = request.AddString("signature", mimeSig);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// SetThread
/*!
	\todo Really needed?
*/
void
BRoster::_SetThread(team_id team, thread_id thread) const
{
}

// SetThreadAndTeam
/*!	\brief Sets the team and thread IDs of a pre-registered application.

	After an application has been pre-registered via AddApplication(), without
	supplying a team ID, the team and thread IDs have to be set using this
	method.

	\param entryToken The token identifying the application (returned by
		   AddApplication())
	\param thread The app's thread ID
	\param team The app's team ID
	\return
	- \c B_OK: Everything went fine.
	- \c B_REG_APP_NOT_PRE_REGISTERED: The supplied token does not identify a
	  pre-registered application.
*/
status_t
BRoster::_SetThreadAndTeam(uint32 entryToken, thread_id thread,
	team_id team) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_SET_THREAD_AND_TEAM);
	if (error == B_OK)
		error = request.AddInt32("token", (int32)entryToken);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	if (error == B_OK && thread >= 0)
		error = request.AddInt32("thread", thread);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// CompleteRegistration
/*!	\brief Completes the registration process for a pre-registered application.

	After an application has been pre-registered via AddApplication() and
	after assigning it a team ID (via SetThreadAndTeam()) the application is
	still pre-registered and must complete the registration.

	\param team The app's team ID
	\param thread The app's thread ID
	\param thread The app looper port
	\return
	- \c B_OK: Everything went fine.
	- \c B_REG_APP_NOT_PRE_REGISTERED: \a team does not identify an existing
	  application or the identified application is already fully registered.
*/
status_t
BRoster::_CompleteRegistration(team_id team, thread_id thread,
	port_id port) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_COMPLETE_REGISTRATION);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	if (error == B_OK && thread >= 0)
		error = request.AddInt32("thread", thread);
	if (error == B_OK && port >= 0)
		error = request.AddInt32("port", port);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// IsAppPreRegistered
/*!	\brief Returns whether an application is pre-registered.

	If the application is indeed pre-registered and \a info is not \c NULL,
	the methods fills in the app_info structure pointed to by \a info.

	\param ref An entry_ref referring to the app's executable
	\param team The app's team ID
	\param info A pointer to a pre-allocated app_info structure to be filled
		   in by this method (may be \c NULL)
	\return \c true, if the application is pre-registered, \c false if not.
*/
bool
BRoster::_IsAppPreRegistered(const entry_ref *ref, team_id team,
	app_info *info) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_IS_APP_PRE_REGISTERED);
	if (error == B_OK && ref)
		error = request.AddRef("ref", ref);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	bool isPreRegistered = false;
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (reply.FindBool("pre-registered", &isPreRegistered) != B_OK)
				error = B_ERROR;
			if (error == B_OK && isPreRegistered && info)
				error = find_message_app_info(&reply, info);
		} else
			reply.FindInt32("error", &error);
	}
	return (error == B_OK && isPreRegistered);
}

// RemovePreRegApp
/*!	\brief Completely unregisters a pre-registered application.

	This method can only be used to unregister applications that don't have
	a team ID assigned yet. All other applications must be unregistered via
	RemoveApp().

	\param entryToken The token identifying the application (returned by
		   AddApplication())
	\return
	- \c B_OK: Everything went fine.
	- \c B_REG_APP_NOT_PRE_REGISTERED: The supplied token does not identify a
	  pre-registered application.
*/
status_t
BRoster::_RemovePreRegApp(uint32 entryToken) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_REMOVE_PRE_REGISTERED_APP);
	if (error == B_OK)
		error = request.AddInt32("token", (int32)entryToken);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// RemoveApp
/*!	\brief Unregisters a (pre-)registered application.

	This method must be used to unregister applications that already have
	a team ID assigned, i.e. also for pre-registered application for which
	SetThreadAndTeam() has already been invoked.

	\param team The app's team ID
	\return
	- \c B_OK: Everything went fine.
	- \c B_REG_APP_NOT_REGISTERED: The supplied team ID does not identify a
	  (pre-)registered application.
*/
status_t
BRoster::_RemoveApp(team_id team) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_REMOVE_APP);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS)
		reply.FindInt32("error", &error);
	return error;
}

// _LaunchApp
/*!	\brief Launches the application associated with the supplied MIME type or
		   the entry referred to by the supplied entry_ref.

	The application to be started is searched the same way FindApp() does it.

	At least one of \a mimeType or \a ref must not be \c NULL. If \a mimeType
	is supplied, \a ref is ignored for finding the application.

	If \a ref does refer to an application executable, that application is
	launched. Otherwise the respective application is searched and launched,
	and \a ref is sent to it in a \c B_REFS_RECEIVED message, unless other
	arguments are passed via \a argc and \a args -- then the entry_ref is
	converted into a path (C-string) and added to the argument vector.

	\a messageList contains messages to be sent to the application
	"on launch", i.e. before ReadyToRun() is invoked on the BApplication
	object. The caller retains ownership of the supplied BList and the
	contained BMessages. In case the method fails with \c B_ALREADY_RUNNING
	the messages are delivered to the already running instance. The same
	applies to the \c B_REFS_RECEIVED message.

	The supplied \a argc and \a args are (if containing at least one argument)
	put into a \c B_ARGV_RECEIVED message and sent to the launched application
	"on launch". The caller retains ownership of the supplied \a args.
	In case the method fails with \c B_ALREADY_RUNNING the message is
	delivered to the already running instance. The same applies to the
	\c B_REFS_RECEIVED message, if no arguments are supplied via \a argc and
	\args.

	\param mimeType MIME type for which the application shall be launched.
		   May be \c NULL.
	\param ref entry_ref referring to the file for which an application shall
		   be launched. May be \c NULL.
	\param messageList Optional list of messages to be sent to the application
		   "on launch". May be \c NULL.
	\param argc Specifies the number of elements in \a args.
	\param args An array of C-strings to be sent as B_ARGV_RECEIVED messaged
		   to the launched application.
	\param appTeam Pointer to a pre-allocated team_id variable to be set to
		   the team ID of the launched application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType and \a ref.
	- \c B_LAUNCH_FAILED_NO_PREFERRED_APP: Neither with the supplied type nor 
	  with its supertype (if the supplied isn't a supertype itself) a
	  preferred application is associated.
	- \c B_LAUNCH_FAILED_APP_NOT_FOUND: The supplied type is not installed or
	  its preferred application could not be found.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The supplied type's preferred
	  application is in trash.
	- \c B_LAUNCH_FAILED_EXECUTABLE: The found application is not executable.
	- other error codes
*/
status_t
BRoster::_LaunchApp(const char *mimeType, const entry_ref *ref,
	const BList *messageList, int argc,
	const char *const *args, team_id *_appTeam) const
{
	DBG(OUT("BRoster::_LaunchApp()"));

	if (_appTeam != NULL) {
		// we're supposed to set _appTeam to -1 on error; we'll
		// reset it later if everything goes well
		*_appTeam = -1;
	}

	if (mimeType == NULL && ref == NULL)
		return B_BAD_VALUE;

	// use a mutable copy of the document entry_ref
	entry_ref _docRef;
	entry_ref *docRef = NULL;
	if (ref != NULL) {
		_docRef = *ref;
		docRef = &_docRef;
	}

	// find the app
	entry_ref appRef;
	char signature[B_MIME_TYPE_LENGTH];
	uint32 appFlags = B_REG_DEFAULT_APP_FLAGS;
	bool wasDocument = true;
	status_t error = _ResolveApp(mimeType, docRef, &appRef, signature, &appFlags,
			&wasDocument);
	DBG(OUT("  find app: %s (%lx)\n", strerror(error), error));

	// build an argument vector
	ArgVector argVector;
	if (error == B_OK) {
		error = argVector.Init(argc, args, &appRef,
			(wasDocument ? docRef : NULL));
	}
	DBG(OUT("  build argv: %s (%lx)\n", strerror(error), error));

	// pre-register the app (but ignore scipts)
	app_info appInfo;
	bool isScript = wasDocument && *docRef == appRef;
	bool alreadyRunning = false;
	uint32 appToken = 0;
	team_id team = -1;
	uint32 otherAppFlags = B_REG_DEFAULT_APP_FLAGS;
	if (error == B_OK && !isScript) {
		error = _AddApplication(signature, &appRef, appFlags, -1, -1, -1, false,
			&appToken, &team);
		if (error == B_ALREADY_RUNNING) {
			DBG(OUT("  already running\n"));
			alreadyRunning = true;
			error = B_OK;
			// get the app flags for the running application
			if (GetRunningAppInfo(team, &appInfo) == B_OK)
				otherAppFlags = appInfo.flags;
		}
		DBG(OUT("  pre-register: %s (%lx)\n", strerror(error), error));
	}

	// launch the app
	if (error == B_OK && !alreadyRunning) {
		DBG(OUT("  token: %lu\n", appToken));
		// load the app image
		thread_id appThread = load_image(argVector.Count(),
			const_cast<const char**>(argVector.Args()),
			const_cast<const char**>(environ));

		// get the app team
		if (appThread >= 0) {
			thread_info threadInfo;
			error = get_thread_info(appThread, &threadInfo);
			if (error == B_OK)
				team = threadInfo.team;
		} else if (wasDocument) {
			error = B_LAUNCH_FAILED_EXECUTABLE;
		} else
			error = appThread;

		DBG(OUT("  load image: %s (%lx)\n", strerror(error), error));
		// finish the registration
		if (error == B_OK && !isScript)
			error = _SetThreadAndTeam(appToken, appThread, team);

		DBG(OUT("  set thread and team: %s (%lx)\n", strerror(error), error));
		// resume the launched team
		if (error == B_OK)
			error = resume_thread(appThread);

		DBG(OUT("  resume thread: %s (%lx)\n", strerror(error), error));
		// on error: kill the launched team and unregister the app
		if (error != B_OK) {
			if (appThread >= 0)
				kill_thread(appThread);
			if (!isScript)
				_RemovePreRegApp(appToken);
		}
	}

	if (alreadyRunning && current_team() == team) {
		// The target team is calling us, so we don't send it the message
		// to prevent an endless loop
		error = B_BAD_VALUE;
	}

	// send "on launch" messages
	if (error == B_OK) {
		// If the target app is B_ARGV_ONLY almost no messages are sent to it.
		// More precisely, the launched app gets at least B_ARGV_RECEIVED and
		// B_READY_TO_RUN, an already running app gets nothing.
		bool argvOnly = (appFlags & B_ARGV_ONLY)
			|| (alreadyRunning && (otherAppFlags & B_ARGV_ONLY));
		const BList *_messageList = (argvOnly ? NULL : messageList);
		// don't send ref, if it refers to the app or is included in the
		// argument vector
		const entry_ref *_ref = argvOnly || !wasDocument
			|| argVector.Count() > 1 ? NULL : docRef;
		if (!(argvOnly && alreadyRunning)) {
			_SendToRunning(team, argVector.Count(), argVector.Args(),
				_messageList, _ref, !alreadyRunning);
		}
	}

	// set return values
	if (error == B_OK) {
		if (alreadyRunning)
			error = B_ALREADY_RUNNING;
		else if (_appTeam)
			*_appTeam = team;
	}

	DBG(OUT("BRoster::_LaunchApp() done: %s (%lx)\n",
		strerror(error), error));
	return error;
}

// SetAppFlags
void
BRoster::_SetAppFlags(team_id team, uint32 flags) const
{
}

// DumpRoster
void
BRoster::_DumpRoster() const
{
}

// _ResolveApp
/*!	\brief Finds an application associated with a MIME type or a file.

	It does also supply the caller with some more information about the
	application, like signature, app flags and whether the supplied
	MIME type/entry_ref already identified an application.

	At least one of \a inType or \a ref must not be \c NULL. If \a inType is
	supplied, \a ref is ignored.

	If \a ref refers to a link, it is updated with the entry_ref for the
	resolved entry.

	\see FindApp() for how the application is searched.

	\a appSig is set to a string with length 0, if the found application
	has no signature.

	\param inType The MIME type for which an application shall be found.
		   May be \c NULL.
	\param ref The file for which an application shall be found.
		   May be \c NULL.
	\param appRef A pointer to a pre-allocated entry_ref to be filled with
		   a reference to the found application's executable. May be \c NULL.
	\param appSig A pointer to a pre-allocated char buffer of at least size
		   \c B_MIME_TYPE_LENGTH to be filled with the signature of the found
		   application. May be \c NULL.
	\param appFlags A pointer to a pre-allocated uint32 variable to be filled
		   with the app flags of the found application. May be \c NULL.
	\param wasDocument A pointer to a pre-allocated bool variable to be set to
		   \c true, if the supplied file was not identifying an application,
		   to \c false otherwise. Has no meaning, if a \a inType is supplied.
		   May be \c NULL.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a inType and \a ref.
	- \see FindApp() for other error codes.
*/
status_t
BRoster::_ResolveApp(const char *inType, entry_ref *ref,
	entry_ref* _appRef, char* _appSig, uint32* _appFlags,
	bool* _wasDocument) const
{
	if ((inType == NULL && ref == NULL)
		|| (inType != NULL && strlen(inType) >= B_MIME_TYPE_LENGTH))
		return B_BAD_VALUE;

	// find the app
	BMimeType appMeta;
	BFile appFile;
	entry_ref appRef;
	status_t error;

	if (inType)
		error = _TranslateType(inType, &appMeta, &appRef, &appFile);
	else {
		error = _TranslateRef(ref, &appMeta, &appRef, &appFile,
			_wasDocument);
	}

	// create meta mime
	if (error == B_OK) {
		BPath path;
		if (path.SetTo(&appRef) == B_OK)
			create_app_meta_mime(path.Path(), false, true, false);
	}

	// set the app hint on the type -- but only if the file has the
	// respective signature, otherwise unset the app hint
	BAppFileInfo appFileInfo;
	if (error == B_OK) {
		char signature[B_MIME_TYPE_LENGTH];
		if (appFileInfo.SetTo(&appFile) == B_OK
			&& appFileInfo.GetSignature(signature) == B_OK) {
			if (!strcasecmp(appMeta.Type(), signature))
				appMeta.SetAppHint(&appRef);
			else {
				appMeta.SetAppHint(NULL);
				appMeta.SetTo(signature);
			}
		} else
			appMeta.SetAppHint(NULL);
	}

	// set the return values
	if (error == B_OK) {
		if (_appRef)
			*_appRef = appRef;

		if (_appSig) {
			// there's no warranty, that appMeta is valid
			if (appMeta.IsValid())
				strcpy(_appSig, appMeta.Type());
			else
				_appSig[0] = '\0';
		}

		if (_appFlags) {
			// if an error occurs here, we don't care and just set a default
			// value
			if (appFileInfo.InitCheck() != B_OK
				|| appFileInfo.GetAppFlags(_appFlags) != B_OK) {
				*_appFlags = B_REG_DEFAULT_APP_FLAGS;
			}
		}
	} else {
		// unset the ref on error
		if (_appRef)
			*_appRef = appRef;
	}

	return error;
}

// _TranslateRef
/*!	\brief Finds an application associated with a file.

	\a appMeta is left unmodified, if the file is executable, but has no
	signature.

	\see FindApp() for how the application is searched.

	If \a ref refers to a link, it is updated with the entry_ref for the
	resolved entry.

	\param ref The file for which an application shall be found.
	\param appMeta A pointer to a pre-allocated BMimeType to be set to the
		   signature of the found application.
	\param appRef A pointer to a pre-allocated entry_ref to be filled with
		   a reference to the found application's executable.
	\param appFile A pointer to a pre-allocated BFile to be set to the
		   executable of the found application.
	\param wasDocument A pointer to a pre-allocated bool variable to be set to
		   \c true, if the supplied file was not identifying an application,
		   to \c false otherwise. May be \c NULL.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref, \a appMeta, \a appRef or \a appFile.
	- \see FindApp() for other error codes.
*/
status_t
BRoster::_TranslateRef(entry_ref *ref, BMimeType *appMeta,
	entry_ref *appRef, BFile *appFile, bool *_wasDocument) const
{
	if (ref == NULL || appMeta == NULL || appRef == NULL || appFile == NULL)
		return B_BAD_VALUE;

	// resolve ref, if necessary
	BEntry entry;
	status_t error = entry.SetTo(ref, false);
	if (error == B_OK && entry.IsSymLink()) {
		// ref refers to a link
		error = entry.SetTo(ref, true);
		if (error == B_OK)
			error = entry.GetRef(ref);
		if (error != B_OK)
			error = B_LAUNCH_FAILED_NO_RESOLVE_LINK;
	}

	// init node
	BNode node;
	if (error == B_OK)
		error = node.SetTo(ref);
	// get permissions
	mode_t permissions;
	if (error == B_OK)
		error = node.GetPermissions(&permissions);
	if (error == B_OK) {
		if ((permissions & S_IXUSR) && node.IsFile()) {
			// node is executable and a file -- we're done
			*appRef = *ref;
			error = appFile->SetTo(appRef, B_READ_ONLY);

			// get the app's signature via a BAppFileInfo
			BAppFileInfo appFileInfo;
			if (error == B_OK)
				error = appFileInfo.SetTo(appFile);

			char type[B_MIME_TYPE_LENGTH];
			bool isDocument = true;

			if (error == B_OK) {
				// don't worry, if the file doesn't have a signature, just
				// unset the supplied object
				if (appFileInfo.GetSignature(type) == B_OK)
					error = appMeta->SetTo(type);
				else
					appMeta->Unset();
				if (appFileInfo.GetType(type) == B_OK
					&& !strcasecmp(type, B_APP_MIME_TYPE))
					isDocument = false;
			}
			if (_wasDocument)
				*_wasDocument = isDocument;
		} else {
			// the node is not exectuable or not a file
			// init a node info
			BNodeInfo nodeInfo;
			if (error == B_OK)
				error = nodeInfo.SetTo(&node);
			char preferredApp[B_MIME_TYPE_LENGTH];
			if (error == B_OK) {
				// if the file has a preferred app, let _TranslateType() find
				// it for us
				if (nodeInfo.GetPreferredApp(preferredApp) == B_OK) {
					error = _TranslateType(preferredApp, appMeta, appRef,
										   appFile);
				} else {
					// no preferred app -- we need to get the file's type
					char fileType[B_MIME_TYPE_LENGTH];
					// get the type from the file, or guess a type
					if (nodeInfo.GetType(fileType) != B_OK)
						error = _SniffFile(ref, &nodeInfo, fileType);
					// now let _TranslateType() do the actual work
					if (error == B_OK) {
						error = _TranslateType(fileType, appMeta, appRef,
											   appFile);
					}
				}
			}
			if (_wasDocument)
				*_wasDocument = true;
		}
	}
	return error;
}

// _TranslateType
/*!	\brief Finds an application associated with a MIME type.

	\see FindApp() for how the application is searched.

	\param mimeType The MIME type for which an application shall be found.
	\param appMeta A pointer to a pre-allocated BMimeType to be set to the
		   signature of the found application.
	\param appRef A pointer to a pre-allocated entry_ref to be filled with
		   a reference to the found application's executable.
	\param appFile A pointer to a pre-allocated BFile to be set to the
		   executable of the found application.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a mimeType, \a appMeta, \a appRef or \a appFile.
	- \see FindApp() for other error codes.
*/
status_t
BRoster::_TranslateType(const char *mimeType, BMimeType *appMeta,
	entry_ref *appRef, BFile *appFile) const
{
	if (mimeType == NULL || appMeta == NULL || appRef == NULL
		|| appFile == NULL || strlen(mimeType) >= B_MIME_TYPE_LENGTH)
		return B_BAD_VALUE;

	// create a BMimeType and check, if the type is installed
	BMimeType type;
	status_t error = type.SetTo(mimeType);

	// get the preferred app
	char primarySignature[B_MIME_TYPE_LENGTH];
	char secondarySignature[B_MIME_TYPE_LENGTH];
	primarySignature[0] = '\0';
	secondarySignature[0] = '\0';

	if (error == B_OK) {
		if (type.IsInstalled()) {
			BMimeType superType;
			if (type.GetSupertype(&superType) == B_OK)
				superType.GetPreferredApp(secondarySignature);

			if (type.GetPreferredApp(primarySignature) != B_OK
				&& !secondarySignature[0]) {
				// The type is installed, but has no preferred app.
				// In fact it might be an app signature and even having a
				// valid app hint. Nevertheless we fail.
				error = B_LAUNCH_FAILED_NO_PREFERRED_APP;
			} else if (!strcmp(primarySignature, secondarySignature)) {
				// Both types have the same preferred app, there is
				// no point in testing it twice.
				secondarySignature[0] = '\0';
			}
		} else {
			// The type is not installed. We assume it is an app signature.
			strcpy(primarySignature, mimeType);
		}
	}

	if (error != B_OK)
		return error;

	// see if we can find the application and if it's valid, try
	// both preferred app signatures, if available (from type and
	// super type)

	status_t primaryError = B_OK;

	for (int32 tries = 0; tries < 2; tries++) {
		const char* signature = tries == 0 ? primarySignature : secondarySignature;
		if (signature[0] == '\0')
			continue;

		error = appMeta->SetTo(signature);

		// check, whether the signature is installed and has an app hint
		bool appFound = false;
		if (error == B_OK && appMeta->GetAppHint(appRef) == B_OK) {
			// resolve symbolic links, if necessary
			BEntry entry;
			if (entry.SetTo(appRef, true) == B_OK && entry.IsFile()
				&& entry.GetRef(appRef) == B_OK) {
				appFound = true;
			} else
				appMeta->SetAppHint(NULL);	// bad app hint -- remove it
		}

		// in case there is no app hint or it is invalid, we need to query for the
		// app
		if (error == B_OK && !appFound)
			error = query_for_app(appMeta->Type(), appRef);
		if (error == B_OK)
			error = appFile->SetTo(appRef, B_READ_ONLY);
		// check, whether the app can be used
		if (error == B_OK)
			error = can_app_be_used(appRef);

		if (error != B_OK) {
			if (tries == 0)
				primaryError = error;
			else if (primarySignature[0])
				error = primaryError;
		} else
			break;
	}

	return error;
}

// _SniffFile
/*!	\brief Sniffs the MIME type for a file.

	Also updates the file's MIME info, if possible.

	\param file An entry_ref referring to the file in question.
	\param nodeInfo A BNodeInfo initialized to the file.
	\param mimeType A pointer to a pre-allocated char buffer of at least size
		   \c B_MIME_TYPE_LENGTH to be filled with the MIME type sniffed for
		   the file.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a file, \a nodeInfo or \a mimeType.
	- other errors
*/
status_t
BRoster::_SniffFile(const entry_ref *file, BNodeInfo *nodeInfo,
	char *mimeType) const
{
	status_t error = (file && nodeInfo && mimeType ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		// Try to update the file's MIME info and just read the updated type.
		// If that fails, sniff manually.
		BPath path;
		if (path.SetTo(file) != B_OK
			|| update_mime_info(path.Path(), false, true, false) != B_OK
			|| nodeInfo->GetType(mimeType) != B_OK) {
			BMimeType type;
			error = BMimeType::GuessMimeType(file, &type);
			if (error == B_OK && type.IsValid())
				strcpy(mimeType, type.Type());
		}
	}
	return error;
}

// _SendToRunning
/*!	\brief Sends messages to a running team.

	In particular those messages are \c B_ARGV_RECEIVED, \c B_REFS_RECEIVED,
	\c B_READY_TO_RUN and other, arbitrary, ones.

	If \a messageList is not \c NULL or empty, those messages are sent first,
	then follow \c B_ARGV_RECEIVED, \c B_REFS_RECEIVED and finally
	\c B_READ_TO_RUN.
	
	\c B_ARGV_RECEIVED is sent only, if \a args is not \c NULL and contains
	more than one element. \c B_REFS_RECEIVED is sent only, if \a ref is not
	\c NULL.

	The ownership of all supplied objects retains to the caller.

	\param team The team ID of the target application.
	\param argc Number of elements in \a args.
	\param args Argument vector to be sent to the target. May be \c NULL.
	\param messageList List of BMessages to be sent to the target. May be
		   \c NULL or empty.
	\param ref entry_ref to be sent to the target. May be \c NULL.
	\param readyToRun \c true, if a \c B_READY_TO_RUN message shall be sent,
		   \c false otherwise.
	\return
	- \c B_OK: Everything went fine.
	- an error code otherwise
*/
status_t
BRoster::_SendToRunning(team_id team, int argc, const char *const *args,
	const BList *messageList, const entry_ref *ref,
	bool readyToRun) const
{
	status_t error = B_OK;
	// Construct a messenger to the app: We can't use the public constructor,
	// since the target application may be B_ARGV_ONLY.
	app_info info;
	error = GetRunningAppInfo(team, &info);
	if (error == B_OK) {
		BMessenger messenger;
		BMessenger::Private(messenger).SetTo(team, info.port, B_PREFERRED_TOKEN);
		// send messages from the list
		if (messageList) {
			for (int32 i = 0;
				 BMessage *message = (BMessage*)messageList->ItemAt(i);
				 i++) {
				messenger.SendMessage(message);
			}
		}
		// send B_ARGV_RECEIVED
		if (args && argc > 1) {
			BMessage message(B_ARGV_RECEIVED);
			message.AddInt32("argc", argc);
			for (int32 i = 0; i < argc; i++)
				message.AddString("argv", args[i]);
			messenger.SendMessage(&message);
		}
		// send B_REFS_RECEIVED
		if (ref) {
			BMessage message(B_REFS_RECEIVED);
			message.AddRef("refs", ref);
			messenger.SendMessage(&message);
		}
		// send B_READY_TO_RUN
		if (readyToRun)
			messenger.SendMessage(B_READY_TO_RUN);
	}
	return error;
}


void
BRoster::_InitMessengers()
{
	DBG(OUT("BRoster::InitMessengers()\n"));

	// find the registrar port
	port_id rosterPort = find_port(BPrivate::get_roster_port_name());
	port_info info;
	if (rosterPort >= 0 && get_port_info(rosterPort, &info) == B_OK) {
		DBG(OUT("  found roster port\n"));
		// ask for the MIME messenger
		BMessenger::Private(fMessenger).SetTo(info.team, rosterPort,
			B_PREFERRED_TOKEN);
		BMessage reply;
		status_t error = fMessenger.SendMessage(B_REG_GET_MIME_MESSENGER, &reply);
		if (error == B_OK && reply.what == B_REG_SUCCESS) {
			DBG(OUT("  got reply from roster\n"));
				reply.FindMessenger("messenger", &fMimeMessenger);
		} else {
			DBG(OUT("  no (useful) reply from roster: error: %lx: %s\n", error,
				strerror(error)));
			if (error == B_OK)
				DBG(reply.PrintToStream());
		}
	}
	DBG(OUT("BRoster::InitMessengers() done\n"));
}


/*! \brief Sends a request to the roster to add the application with the
	given signature to the front of the recent apps list.
*/
void
BRoster::_AddToRecentApps(const char *appSig) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_ADD_TO_RECENT_APPS);
	if (error == B_OK)
		error = request.AddString("app sig", appSig);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	status_t result;
	if (error == B_OK)
		error = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (error == B_OK)
		error = reply.FindInt32("result", &result);
	if (error == B_OK)
		error = result;
	// Nothing to return... how sad :-(
	// return error;
}

/*! \brief Sends a request to the roster to clear the recent
	documents list.
*/
void
BRoster::_ClearRecentDocuments() const
{
	BMessage request(B_REG_CLEAR_RECENT_DOCUMENTS);
	BMessage reply;
	fMessenger.SendMessage(&request, &reply);
}

/*! \brief Sends a request to the roster to clear the recent
	documents list.
*/
void
BRoster::_ClearRecentFolders() const
{
	BMessage request(B_REG_CLEAR_RECENT_FOLDERS);
	BMessage reply;
	fMessenger.SendMessage(&request, &reply);
}

/*! \brief Sends a request to the roster to clear the recent
	documents list.
*/
void
BRoster::_ClearRecentApps() const
{
	BMessage request(B_REG_CLEAR_RECENT_APPS);
	BMessage reply;
	fMessenger.SendMessage(&request, &reply);
}

// LoadRecentLists
/*! \brief Loads the system's recently used document, folder, and
	application lists from the specified file.
	
	\note The current lists are cleared before loading the new lists
	
	\param filename The name of the file to load from
*/
void
BRoster::_LoadRecentLists(const char *filename) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_LOAD_RECENT_LISTS);
	if (error == B_OK)
		error = request.AddString("filename", filename);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	status_t result;
	if (error == B_OK)
		error = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (error == B_OK)
		error = reply.FindInt32("result", &result);
	if (error == B_OK)
		error = result;
	// Nothing to return... how sad :-(
	// return error;
}

// SaveRecentLists
/*! \brief Saves the system's recently used document, folder, and
	application lists to the specified file.
	
	\param filename The name of the file to save to
*/
void
BRoster::_SaveRecentLists(const char *filename) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_SAVE_RECENT_LISTS);
	if (error == B_OK)
		error = request.AddString("filename", filename);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	status_t result;
	if (error == B_OK)
		error = reply.what == B_REG_RESULT ? (status_t)B_OK : (status_t)B_BAD_REPLY;
	if (error == B_OK)
		error = reply.FindInt32("result", &result);
	if (error == B_OK)
		error = result;
	// Nothing to return... how sad :-(
	// return error;
}


//	#pragma mark - Helper functions


/*!	\brief Extracts an app_info from a BMessage.

	The function searchs for a field "app_info" typed B_REG_APP_INFO_TYPE
	and initializes \a info with the found data.

	\param message The message
	\param info A pointer to a pre-allocated app_info to be filled in with the
		   info found in the message.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a message or \a info.
	- other error codes
*/
static
status_t
find_message_app_info(BMessage *message, app_info *info)
{
	status_t error = (message && info ? B_OK : B_BAD_VALUE);
	const flat_app_info *flatInfo = NULL;
	ssize_t size = 0;
	// find the flat app info in the message
	if (error == B_OK) {
		error = message->FindData("app_info", B_REG_APP_INFO_TYPE,
								  (const void**)&flatInfo, &size);
	}
	// unflatten the flat info
	if (error == B_OK) {
		if (size == sizeof(flat_app_info)) {
			memcpy(info, &flatInfo->info, sizeof(app_info));
			info->ref.name = NULL;
			if (strlen(flatInfo->ref_name) > 0)
				info->ref.set_name(flatInfo->ref_name);
		} else
			error = B_ERROR;
	}
	return error;
 }

// query_for_app
/*!	\brief Finds an app by signature on any mounted volume.
	\param signature The app's signature.
	\param appRef A pointer to a pre-allocated entry_ref to be filled with
		   a reference to the found application's executable.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a signature or \a appRef.
	- B_LAUNCH_FAILED_APP_NOT_FOUND: An application with this signature
	  could not be found.
	- other error codes
*/
static
status_t
query_for_app(const char *signature, entry_ref *appRef)
{
	if (signature == NULL || appRef == NULL)
		return B_BAD_VALUE;

	status_t error = B_LAUNCH_FAILED_APP_NOT_FOUND;
	bool caseInsensitive = false;

	while (true) {
		// search on all volumes
		BVolumeRoster volumeRoster;
		BVolume volume;
		while (volumeRoster.GetNextVolume(&volume) == B_OK) {
			if (!volume.KnowsQuery())
				continue;

			index_info info;
			if (fs_stat_index(volume.Device(), "BEOS:APP_SIG", &info) != 0) {
				// This volume doesn't seem to have the index we're looking for;
				// querying it might need a long time, and we don't care *that*
				// much...
				continue;
			}

			BQuery query;
			query.SetVolume(&volume);
			query.PushAttr("BEOS:APP_SIG");
			if (!caseInsensitive)
				query.PushString(signature);
			else {
				// second pass, create a case insensitive query string
				char string[B_MIME_TYPE_LENGTH * 4];
				strcpy(string, "application/");

				int32 length = strlen(string);
				const char *from = signature + length;
				char *to = string + length;

				for (; from[0]; from++) {
					if (isalpha(from[0])) {
						*to++ = '[';
						*to++ = tolower(from[0]);
						*to++ = toupper(from[0]);
						*to++ = ']';
					} else
						*to++ = from[0];
				}

				to[0] = '\0';
				query.PushString(string);
			}
			query.PushOp(B_EQ);

			query.Fetch();

			// walk through the query
			bool appFound = false;
			status_t foundAppError = B_OK;
			entry_ref ref;
			while (query.GetNextRef(&ref) == B_OK) {
				if ((!appFound || compare_app_versions(appRef, &ref) < 0)
					&& (foundAppError = can_app_be_used(&ref)) == B_OK) {
					*appRef = ref;
					appFound = true;
				}
			}
			if (!appFound) {
				// If the query didn't return any hits, the error is
				// B_LAUNCH_FAILED_APP_NOT_FOUND, otherwise we return the
				// result of the last can_app_be_used().
				error = foundAppError != B_OK
					? foundAppError : B_LAUNCH_FAILED_APP_NOT_FOUND;
			} else
				return B_OK;
		}

		if (!caseInsensitive)
			caseInsensitive = true;
		else
			break;
	}

	return error;
}


// can_app_be_used
/*!	\brief Checks whether or not an application can be used.

	Currently it is only checked whether the application is in the trash.

	\param ref An entry_ref referring to the application executable.
	\return
	- \c B_OK: The application can be used.
	- \c B_ENTRY_NOT_FOUND: \a ref doesn't refer to and existing entry.
	- \c B_IS_A_DIRECTORY: \a ref refers to a directory.
	- \c B_LAUNCH_FAILED_APP_IN_TRASH: The application executable is in the
	  trash.
	- other error codes specifying why the application cannot be used.
*/
static
status_t
can_app_be_used(const entry_ref *ref)
{
	status_t error = (ref ? B_OK : B_BAD_VALUE);
	// check whether the file exists and is a file.
	BEntry entry;
	if (error == B_OK)
		error = entry.SetTo(ref, true);
	if (error == B_OK && !entry.Exists())
		error = B_ENTRY_NOT_FOUND;
	if (error == B_OK && !entry.IsFile())
		error = B_IS_A_DIRECTORY;
	// check whether the file is in trash
	BPath trashPath;
	BDirectory directory;
	if (error == B_OK
		&& find_directory(B_TRASH_DIRECTORY, &trashPath) == B_OK
		&& directory.SetTo(trashPath.Path()) == B_OK
		&& directory.Contains(&entry)) {
		error = B_LAUNCH_FAILED_APP_IN_TRASH;
	}
	return error;
}

// compare_version_infos
/*!	\brief Compares the supplied version infos.
	\param info1 The first info.
	\param info2 The second info.
	\return \c -1, if the first info is less than the second one, \c 1, if
			the first one is greater than the second one, and \c 0, if both
			are equal.
*/
static
int32
compare_version_infos(const version_info &info1, const version_info &info2)
{
	int32 result = 0;
	if (info1.major < info2.major)
		result = -1;
	else if (info1.major > info2.major)
		result = 1;
	else if (info1.middle < info2.middle)
		result = -1;
	else if (info1.middle > info2.middle)
		result = 1;
	else if (info1.minor < info2.minor)
		result = -1;
	else if (info1.minor > info2.minor)
		result = 1;
	else if (info1.variety < info2.variety)
		result = -1;
	else if (info1.variety > info2.variety)
		result = 1;
	else if (info1.internal < info2.internal)
		result = -1;
	else if (info1.internal > info2.internal)
		result = 1;
	return result;
}

// compare_app_versions
/*!	\brief Compares the version of two applications.

	If both files have a version info, then those are compared.
	If one file has a version info, it is said to be greater. If both
	files have no version info, their modification times are compared.

	\param app1 An entry_ref referring to the first application.
	\param app2 An entry_ref referring to the second application.
	\return \c -1, if the first application version is less than the second
			one, \c 1, if the first one is greater than the second one, and
			\c 0, if both are equal.
*/
static
int32
compare_app_versions(const entry_ref *app1, const entry_ref *app2)
{
	BFile file1, file2;
	BAppFileInfo appFileInfo1, appFileInfo2;
	file1.SetTo(app1, B_READ_ONLY);
	file2.SetTo(app2, B_READ_ONLY);
	appFileInfo1.SetTo(&file1);
	appFileInfo2.SetTo(&file2);
	time_t modificationTime1 = 0;
	time_t modificationTime2 = 0;
	file1.GetModificationTime(&modificationTime1);
	file2.GetModificationTime(&modificationTime2);
	int32 result = 0;
	version_info versionInfo1, versionInfo2;
	bool hasVersionInfo1 = (appFileInfo1.GetVersionInfo(
		&versionInfo1, B_APP_VERSION_KIND) == B_OK);
	bool hasVersionInfo2 = (appFileInfo2.GetVersionInfo(
		&versionInfo2, B_APP_VERSION_KIND) == B_OK);
	if (hasVersionInfo1) {
		if (hasVersionInfo2)
			result = compare_version_infos(versionInfo1, versionInfo2);
		else
			result = 1;
	} else {
		if (hasVersionInfo2)
			result = -1;
		else if (modificationTime1 < modificationTime2)
			result = -1;
		else if (modificationTime1 > modificationTime2)
			result = 1;
	}
	return result;	
}

