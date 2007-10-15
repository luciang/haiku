#include "AddOnHost.h"
#include "AddOnHostProtocol.h"

#include <Application.h>
#include <Debug.h>
#include <Entry.h>
#include <MediaNode.h>
#include <MediaRoster.h>
#include <Messenger.h>
#include <Path.h>
#include <Roster.h>

#include <OS.h>

#include <cstdlib>
#include <cstring>

__USE_CORTEX_NAMESPACE

// -------------------------------------------------------- //
// constants
// -------------------------------------------------------- //

BMessenger AddOnHost::s_messenger;

// -------------------------------------------------------- //
// *** static interface
// -------------------------------------------------------- //

/*static*/
status_t AddOnHost::FindInstance(
	BMessenger*								outMessenger) {

	status_t err;

	// no current app? launch one
	if(!s_messenger.IsValid()) {
		s_messenger = BMessenger(
			addon_host::g_appSignature,
			-1,
			&err);
			
		if(err < B_OK)
			return err;
		if(!s_messenger.IsValid())
			return B_ERROR;
	}
	
	*outMessenger = s_messenger;	
	return B_OK;
}

/*static*/
status_t AddOnHost::Kill(
	bigtime_t									timeout) {

	if(!s_messenger.IsValid())
		return B_NOT_ALLOWED;

	status_t err = kill_team(s_messenger.Team());
	return err;		
}

/*static*/
status_t AddOnHost::Launch(
	BMessenger*								outMessenger) {

	if(s_messenger.IsValid())
		return B_NOT_ALLOWED;
		
	status_t err;

	// find it
	entry_ref appRef;
	err = be_roster->FindApp(addon_host::g_appSignature, &appRef);
	if(err < B_OK)
		return err;
		
	// start it
	team_id team;
	const char* arg = "--addon-host";
	err = be_roster->Launch(
		&appRef,
		1,
		&arg,
		&team);
	if(err < B_OK)
		return err;

	// fetch messenger to the new app and return it	
	s_messenger = BMessenger(
		addon_host::g_appSignature,
		team,
		&err);
			
	if(err < B_OK)
		return err;
	if(!s_messenger.IsValid())
		return B_ERROR;

	if(outMessenger)
		*outMessenger = s_messenger;

	return B_OK;
}

/*static*/
status_t AddOnHost::InstantiateDormantNode(
	const dormant_node_info&	info,
	media_node*								outNode,
	bigtime_t									timeout) {

	status_t err;

	if(!s_messenger.IsValid()) {
		err = Launch(0);
		
		if(err < B_OK) {
			// give up
			PRINT((
				"!!! AddOnHost::InstantiateDormantNode(): Launch() failed:\n"
				"    %s\n",
				strerror(err)));
			return err;
		}
	}

	// do it	
	ASSERT(s_messenger.IsValid());
	BMessage request(addon_host::M_INSTANTIATE);
	request.AddData("info", B_RAW_TYPE, &info, sizeof(dormant_node_info));
	
	BMessage reply(B_NO_REPLY);
	err = s_messenger.SendMessage(
		&request,
		&reply,
		timeout,
		timeout);
		
//	PRINT((
//		"### SendMessage() returned '%s'\n", strerror(err)));

	if(err < B_OK) {
		PRINT((
			"!!! AddOnHost::InstantiateDormantNode(): SendMessage() failed:\n"
			"    %s\n",
			strerror(err)));
		return err;
	}
	
	if(reply.what == B_NO_REPLY) {
		PRINT((
			"!!! AddOnHost::InstantiateDormantNode(): no reply.\n"));
		return B_ERROR;
	}
	
	if(reply.what == addon_host::M_INSTANTIATE_COMPLETE) {
		media_node_id nodeID;
		
		// fetch node ID
		err = reply.FindInt32("node_id", &nodeID);
		if(err < B_OK) {
			PRINT((
				"!!! AddOnHost::InstantiateDormantNode(): 'node_id' missing from reply.\n"));
			return B_ERROR;
		}

		// fetch node
		err = BMediaRoster::Roster()->GetNodeFor(nodeID, outNode);
		if(err < B_OK) {
			PRINT((
				"!!! AddOnHost::InstantiateDormantNode(): node missing!\n"));
			return B_ERROR;
		}

//		// now solely owned by the add-on host team
//		BMediaRoster::Roster()->ReleaseNode(*outNode);

		return B_OK;
	}
	
	// failed:
	return (reply.FindInt32("error", &err) == B_OK) ? err : B_ERROR;
}

/*static*/
status_t AddOnHost::ReleaseInternalNode(
	const live_node_info&			info,
	bigtime_t									timeout) {
	
	status_t err;

	if(!s_messenger.IsValid()) {
		err = Launch(0);
		
		if(err < B_OK) {
			// give up
			PRINT((
				"!!! AddOnHost::ReleaseInternalNode(): Launch() failed:\n"
				"    %s\n",
				strerror(err)));
			return err;
		}
	}

	// do it	
	ASSERT(s_messenger.IsValid());
	BMessage request(addon_host::M_RELEASE);
	request.AddData("info", B_RAW_TYPE, &info, sizeof(live_node_info));
	
	BMessage reply(B_NO_REPLY);
	err = s_messenger.SendMessage(
		&request,
		&reply,
		timeout,
		timeout);
		

	if(err < B_OK) {
		PRINT((
			"!!! AddOnHost::ReleaseInternalNode(): SendMessage() failed:\n"
			"    %s\n",
			strerror(err)));
		return err;
	}
	
	if(reply.what == B_NO_REPLY) {
		PRINT((
			"!!! AddOnHost::InstantiateDormantNode(): no reply.\n"));
		return B_ERROR;
	}
	
	if(reply.what == addon_host::M_RELEASE_COMPLETE) {
		return B_OK;
	}
	
	// failed:
	return (reply.FindInt32("error", &err) == B_OK) ? err : B_ERROR;
}

