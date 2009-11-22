/*
 * Copyright (c) 2002-2004, Marcus Overhagen <marcus@overhagen.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define BUILDING_MEDIA_ADDON 1

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include "debug.h"
#include "DataExchange.h"
#include "DormantNodeManager.h"
#include "MediaFilePlayer.h"
#include "MediaMisc.h"
#include "MediaRosterEx.h"
#include "MediaSounds.h"
#include "Notifications.h"
#include "ServerInterface.h"
#include "SystemTimeSource.h"
#include "TMap.h"


//#define USER_ADDON_PATH "../add-ons/media"

void DumpFlavorInfo(const flavor_info *info);

struct AddOnInfo {
	media_addon_id id;
	bool wants_autostart;
	int32 flavor_count;
	
	List<media_node> active_flavors;

	BMediaAddOn *addon;
		// if != NULL, need to call _DormantNodeManager->PutAddon(id)
};

class MediaAddonServer : BApplication {
public:
					MediaAddonServer(const char *sig);
	virtual 		~MediaAddonServer();
	virtual	void 	ReadyToRun();
	virtual bool 	QuitRequested();
	virtual void 	MessageReceived(BMessage *msg);
	
private:
			void 	WatchDir(BEntry *dir);
			void 	AddOnAdded(const char *path, ino_t file_node);
			void 	AddOnRemoved(ino_t file_node);
			void 	HandleMessage(int32 code, const void *data, size_t size);

			void 	PutAddonIfPossible(AddOnInfo *info);
			void 	InstantiatePhysicalInputsAndOutputs(AddOnInfo *info);
			void 	InstantiateAutostartFlavors(AddOnInfo *info);
			void 	DestroyInstantiatedFlavors(AddOnInfo *info);
	
			void 	ScanAddOnFlavors(BMediaAddOn *addon);

			port_id ControlPort() const { return fControlPort; }

	static 	int32 	_ControlThread(void *arg);

	Map<ino_t, media_addon_id> *fFileMap;
	Map<media_addon_id, AddOnInfo> *fInfoMap;

	BMediaRoster *fMediaRoster;
	ino_t		fSystemAddOnsNode;
	ino_t		fUserAddOnsNode;
	port_id		fControlPort;
	thread_id	fControlThread;
	bool		fStartup;
	bool		fStartupSound;
	
	typedef BApplication inherited;
};


MediaAddonServer::MediaAddonServer(const char *sig)
	: BApplication(sig),
	fStartup(true),
	fStartupSound(true)
{
	CALLED();
	fMediaRoster = BMediaRoster::Roster();
	fFileMap = new Map<ino_t, media_addon_id>;
	fInfoMap = new Map<media_addon_id, AddOnInfo>;
	fControlPort = create_port(64, MEDIA_ADDON_SERVER_PORT_NAME);
	fControlThread = spawn_thread(_ControlThread, "media_addon_server control",
		B_NORMAL_PRIORITY + 2, this);
	resume_thread(fControlThread);
}


MediaAddonServer::~MediaAddonServer()
{
	CALLED();

	delete_port(fControlPort);
	status_t err;
	wait_for_thread(fControlThread,&err);

	// unregister all media add-ons
	media_addon_id *id;
	for (fFileMap->Rewind(); fFileMap->GetNext(&id); )
		_DormantNodeManager->UnregisterAddon(*id);

	// TODO: unregister system time source

	delete fFileMap;
	delete fInfoMap;
}


void
MediaAddonServer::HandleMessage(int32 code, const void *data, size_t size)
{
	switch (code) {
		case ADDONSERVER_INSTANTIATE_DORMANT_NODE:
		{
			const addonserver_instantiate_dormant_node_request *request = static_cast<const addonserver_instantiate_dormant_node_request *>(data);
			addonserver_instantiate_dormant_node_reply reply;
			status_t rv;
			rv = MediaRosterEx(fMediaRoster)->InstantiateDormantNode(request->addonid, request->flavorid, request->creator_team, &reply.node);
			request->SendReply(rv, &reply, sizeof(reply));
			break;
		}

		case ADDONSERVER_RESCAN_MEDIAADDON_FLAVORS:
		{
			const addonserver_rescan_mediaaddon_flavors_command *command = static_cast<const addonserver_rescan_mediaaddon_flavors_command *>(data);
			BMediaAddOn *addon;
			addon = _DormantNodeManager->GetAddon(command->addonid);
			if (!addon) {
				ERROR("rescan flavors: Can't find a addon object for id %d\n",(int)command->addonid);
				break;
			}
			ScanAddOnFlavors(addon);
			_DormantNodeManager->PutAddon(command->addonid);
			break;
		}
		
		case ADDONSERVER_RESCAN_FINISHED_NOTIFY:
			if (fStartupSound) {
				system_beep(MEDIA_SOUNDS_STARTUP);
				fStartupSound = false;
			}
			break;
		
		default:
			ERROR("media_addon_server: received unknown message code %#08lx\n",code);
	}
}


int32
MediaAddonServer::_ControlThread(void *arg)
{
	char data[B_MEDIA_MESSAGE_SIZE];
	MediaAddonServer *app;
	ssize_t size;
	int32 code;
	
	app = (MediaAddonServer *)arg;
	while ((size = read_port_etc(app->ControlPort(), &code, data, sizeof(data), 0, 0)) > 0)
		app->HandleMessage(code, data, size);

	return 0;
}


void
MediaAddonServer::ReadyToRun()
{
	if (!be_roster->IsRunning("application/x-vnd.Be.media-server")) {
		// the media server is not running, let's quit
		fprintf(stderr, "The media_server is not running!\n");
		Quit();
		return;
	}

	// the control thread is already running at this point,
	// so we can talk to the media server and also receive
	// commands for instantiation

	ASSERT(fStartup == true);
	
	// The very first thing to do is to create the system time source,
	// register it with the server, and make it the default SYSTEM_TIME_SOURCE
	BMediaNode *timeSource = new SystemTimeSource;
	status_t result = fMediaRoster->RegisterNode(timeSource);
	if (result != B_OK) {
		fprintf(stderr, "Can't register system time source : %s\n",
			strerror(result));
		debugger("Can't register system time source");
	}

	if (timeSource->ID() != NODE_SYSTEM_TIMESOURCE_ID)
		debugger("System time source got wrong node ID");
	media_node node = timeSource->Node();
	result = MediaRosterEx(fMediaRoster)->SetNode(SYSTEM_TIME_SOURCE, &node);
	if (result != B_OK)
		debugger("Can't setup system time source as default");

	// During startup, first all add-ons are loaded, then all
	// nodes (flavors) representing physical inputs and outputs
	// are instantiated. Next, all add-ons that need autostart
	// will be autostarted. Finally, add-ons that don't have
	// any active nodes (flavors) will be unloaded.

	// load dormant media nodes
	BPath path;
	find_directory(B_BEOS_ADDONS_DIRECTORY, &path);
	path.Append("media");

	node_ref nref;
	BEntry entry(path.Path());
	entry.GetNodeRef(&nref);
	fSystemAddOnsNode = nref.node;
	WatchDir(&entry);

#ifdef USER_ADDON_PATH
	entry.SetTo(USER_ADDON_PATH);
#else
	find_directory(B_USER_ADDONS_DIRECTORY, &path);
	path.Append("media");
	entry.SetTo(path.Path());
#endif
	entry.GetNodeRef(&nref);
	fUserAddOnsNode = nref.node;
	WatchDir(&entry);

	fStartup = false;

	AddOnInfo *info;

	fInfoMap->Rewind();
	while (fInfoMap->GetNext(&info))
		InstantiatePhysicalInputsAndOutputs(info);

	fInfoMap->Rewind();
	while (fInfoMap->GetNext(&info))
		InstantiateAutostartFlavors(info);

	fInfoMap->Rewind();
	while (fInfoMap->GetNext(&info))
		PutAddonIfPossible(info);

	server_rescan_defaults_command cmd;
	SendToServer(SERVER_RESCAN_DEFAULTS, &cmd, sizeof(cmd));
}


bool
MediaAddonServer::QuitRequested()
{
	CALLED();

	AddOnInfo *info;
	fInfoMap->Rewind();
	while(fInfoMap->GetNext(&info)) {
		DestroyInstantiatedFlavors(info);
		PutAddonIfPossible(info);
	}

	return true;
}


void
MediaAddonServer::ScanAddOnFlavors(BMediaAddOn *addon)
{
	AddOnInfo *info;
	int32 oldflavorcount;
	int32 newflavorcount;
	media_addon_id addon_id;
	port_id port;
	status_t rv;
	bool b;

	ASSERT(addon);
	ASSERT(addon->AddonID() > 0);

	TRACE("MediaAddonServer::ScanAddOnFlavors: id %ld\n", addon->AddonID());

	port = find_port(MEDIA_SERVER_PORT_NAME);
	if (port <= B_OK) {
		ERROR("couldn't find media_server port\n");
		return;
	}

	// cache the media_addon_id in a local variable to avoid
	// calling BMediaAddOn::AddonID() too often
	addon_id = addon->AddonID();

	// update the cached flavor count, get oldflavorcount and newflavorcount
	b = fInfoMap->Get(addon_id, &info);
	ASSERT(b);
	oldflavorcount = info->flavor_count;
	newflavorcount = addon->CountFlavors();
	info->flavor_count = newflavorcount;

	TRACE("%ld old flavors, %ld new flavors\n", oldflavorcount, newflavorcount);

	// during the first update (i == 0), the server removes old dormant_flavor_infos
	for (int i = 0; i < newflavorcount; i++) {
		const flavor_info *info;
		TRACE("flavor %d:\n",i);
		if (B_OK != addon->GetFlavorAt(i, &info)) {
			ERROR("MediaAddonServer::ScanAddOnFlavors GetFlavorAt failed for index %d!\n", i);
			continue;
		}

		#if DEBUG >= 2
		  DumpFlavorInfo(info);
		#endif

		dormant_flavor_info dfi;
		dfi = *info;
		dfi.node_info.addon = addon_id;
		dfi.node_info.flavor_id = info->internal_id;
		strncpy(dfi.node_info.name, info->name, B_MEDIA_NAME_LENGTH - 1);
		dfi.node_info.name[B_MEDIA_NAME_LENGTH - 1] = 0;

		xfer_server_register_dormant_node *msg;
		size_t flattensize;
		size_t msgsize;

		flattensize = dfi.FlattenedSize();
		msgsize = flattensize + sizeof(xfer_server_register_dormant_node);
		msg = (xfer_server_register_dormant_node *) malloc(msgsize);

		// the server should remove previously registered "dormant_flavor_info"s
		// during the first update, but after  the first iteration, we don't
		// want the server to anymore remove old dormant_flavor_infos
		msg->purge_id = (i == 0) ? addon_id : 0;

		msg->dfi_type = dfi.TypeCode();
		msg->dfi_size = flattensize;
		dfi.Flatten(&(msg->dfi),flattensize);

		rv = write_port(port, SERVER_REGISTER_DORMANT_NODE, msg, msgsize);
		if (rv != B_OK) {
			ERROR("MediaAddonServer::ScanAddOnFlavors: couldn't register dormant node\n");
		}

		free(msg);
	}

	// XXX parameter list is (media_addon_id addonid, int32 newcount, int32 gonecount)
	// XXX we currently pretend that all old flavors have been removed, this could
	// XXX probably be done in a smarter way
	BPrivate::media::notifications::FlavorsChanged(addon_id, newflavorcount,
		oldflavorcount);
}


void
MediaAddonServer::AddOnAdded(const char *path, ino_t file_node)
{
	TRACE("\n\nMediaAddonServer::AddOnAdded: path %s\n",path);

	BMediaAddOn *addon;
	media_addon_id id;

	id = _DormantNodeManager->RegisterAddon(path);
	if (id <= 0) {
		ERROR("MediaAddonServer::AddOnAdded: failed to register add-on %s\n", path);
		return;
	}

	TRACE("MediaAddonServer::AddOnAdded: loading addon %ld now...\n", id);

	addon = _DormantNodeManager->GetAddon(id);
	if (addon == NULL) {
		ERROR("MediaAddonServer::AddOnAdded: failed to get add-on %s\n", path);
		_DormantNodeManager->UnregisterAddon(id);
		return;
	}

	TRACE("MediaAddonServer::AddOnAdded: loading finished, id %ld\n", id);

	// put file's inode and addon's id into map
	fFileMap->Insert(file_node, id);

	// also create AddOnInfo struct and get a pointer so
	// we can modify it
	AddOnInfo tempinfo;
	fInfoMap->Insert(id, tempinfo);
	AddOnInfo *info;
	fInfoMap->Get(id, &info);

	// setup
	info->id = id;
	info->wants_autostart = false; // temporary default
	info->flavor_count = 0;
	info->addon = addon; 

	// scan the flavors
	ScanAddOnFlavors(addon);

	// need to call BMediaNode::WantsAutoStart()
	// after the flavors have been scanned
	info->wants_autostart = addon->WantsAutoStart();

	if (info->wants_autostart)
		TRACE("add-on %ld WantsAutoStart!\n", id);

	// During startup, first all add-ons are loaded, then all
	// nodes (flavors) representing physical inputs and outputs
	// are instantiated. Next, all add-ons that need autostart
	// will be autostarted. Finally, add-ons that don't have
	// any active nodes (flavors) will be unloaded.

	// After startup is done, we simply do it for each new
	// loaded add-on, too.
	if (!fStartup) {
		InstantiatePhysicalInputsAndOutputs(info);
		InstantiateAutostartFlavors(info);
		PutAddonIfPossible(info);

		// since something might have changed
		server_rescan_defaults_command cmd;
		SendToServer(SERVER_RESCAN_DEFAULTS, &cmd, sizeof(cmd));	
	}

	// we do not call _DormantNodeManager->PutAddon(id)
	// since it is done by PutAddonIfPossible()
}


void
MediaAddonServer::DestroyInstantiatedFlavors(AddOnInfo *info)
{
	printf("MediaAddonServer::DestroyInstantiatedFlavors\n");
	media_node *node;
	while (info->active_flavors.GetNext(&node)) {
		if ((node->kind & B_TIME_SOURCE) != 0
			&& (fMediaRoster->StopTimeSource(*node, 0, true) != B_OK)) {
			printf("MediaAddonServer::DestroyInstantiatedFlavors couldn't stop "
				"timesource\n");
			continue;
		}

		if (fMediaRoster->StopNode(*node, 0, true) != B_OK) {
			printf("MediaAddonServer::DestroyInstantiatedFlavors couldn't stop "
				"node\n");
			continue;
		}

		if (node->kind & B_BUFFER_CONSUMER) { 
			media_input inputs[16];
			int32 count = 0;
			if (fMediaRoster->GetConnectedInputsFor(*node, inputs, 16, &count)
					!= B_OK) {
				printf("MediaAddonServer::DestroyInstantiatedFlavors couldn't "
					"get connected inputs\n");
				continue;
			}

			for (int32 i = 0; i < count; i++) {
				media_node_id sourceNode;
				if ((sourceNode = fMediaRoster->NodeIDFor(
						inputs[i].source.port)) < 0) {
					printf("MediaAddonServer::DestroyInstantiatedFlavors "
						"couldn't get source node id\n");
					continue;
				}

				if (fMediaRoster->Disconnect(sourceNode, inputs[i].source,
						node->node, inputs[i].destination) != B_OK) {
					printf("MediaAddonServer::DestroyInstantiatedFlavors "
						"couldn't disconnect input\n");
					continue;
				}
			}
		}

		if (node->kind & B_BUFFER_PRODUCER) { 
			media_output outputs[16];
			int32 count = 0;
			if (fMediaRoster->GetConnectedOutputsFor(*node, outputs, 16,
					&count) != B_OK) {
				printf("MediaAddonServer::DestroyInstantiatedFlavors couldn't "
					"get connected outputs\n");
				continue;
			}
			
			for (int32 i = 0; i < count; i++) {
				media_node_id destNode;
				if ((destNode = fMediaRoster->NodeIDFor(
						outputs[i].destination.port)) < 0) {
					printf("MediaAddonServer::DestroyInstantiatedFlavors "
						"couldn't get destination node id\n");
					continue;
				}

				if (fMediaRoster->Disconnect(node->node, outputs[i].source,
						destNode, outputs[i].destination) != B_OK) {
					printf("MediaAddonServer::DestroyInstantiatedFlavors "
						"couldn't disconnect output\n");
					continue;
				}
			}
		}

		info->active_flavors.RemoveCurrent();
	}
}


void
MediaAddonServer::PutAddonIfPossible(AddOnInfo *info)
{
	if (info->addon && info->active_flavors.IsEmpty()) {
		_DormantNodeManager->PutAddon(info->id);
		info->addon = NULL;
	}
}


void
MediaAddonServer::InstantiatePhysicalInputsAndOutputs(AddOnInfo *info)
{
	CALLED();
	int count = info->addon->CountFlavors();
	for (int i = 0; i < count; i++) {
		const flavor_info *flavorinfo;
		if (info->addon->GetFlavorAt(i, &flavorinfo) != B_OK) {
			ERROR("MediaAddonServer::InstantiatePhysialInputsAndOutputs GetFlavorAt failed for index %d!\n", i);
			continue;
		}
		if (flavorinfo->kinds & (B_PHYSICAL_INPUT | B_PHYSICAL_OUTPUT)) {
			media_node node;
			status_t rv;

			dormant_node_info dni;
			dni.addon = info->id;
			dni.flavor_id = flavorinfo->internal_id;
			strcpy(dni.name, flavorinfo->name);
			
			printf("MediaAddonServer::InstantiatePhysialInputsAndOutputs: \"%s\" is a physical input/output\n", flavorinfo->name);
			rv = fMediaRoster->InstantiateDormantNode(dni, &node);
			if (rv != B_OK) {
				ERROR("MediaAddonServer::InstantiatePhysialInputsAndOutputs Couldn't instantiate node flavor, internal_id %ld, name %s\n", flavorinfo->internal_id, flavorinfo->name);
			} else {
				printf("Node created!\n");
				info->active_flavors.Insert(node);
			}
		}
	}
}


void
MediaAddonServer::InstantiateAutostartFlavors(AddOnInfo *info)
{
	if (!info->wants_autostart)
		return;
		
	for (int32 index = 0; ;index++) {
		BMediaNode *outNode;
		int32 outInternalID;
		bool outHasMore;
		status_t rv;
		printf("trying autostart of node %ld, index %ld\n", info->id, index);
		rv = info->addon->AutoStart(index, &outNode, &outInternalID, &outHasMore);
		if (rv == B_OK) {
			printf("started node %ld\n", index);

			// XXX IncrementAddonFlavorInstancesCount

			rv = MediaRosterEx(fMediaRoster)->RegisterNode(outNode, info->id,
				outInternalID);
			if (rv != B_OK) {
				printf("failed to register node %ld\n",index);
				// XXX DecrementAddonFlavorInstancesCount
			}

			info->active_flavors.Insert(outNode->Node());

			if (!outHasMore)
				return;
		} else if (rv == B_MEDIA_ADDON_FAILED && outHasMore) {
			continue;
		} else {
			break;
		}
	}
}


void
MediaAddonServer::AddOnRemoved(ino_t file_node)
{	
	media_addon_id *tempid;
	media_addon_id id;
	AddOnInfo *info;
	int32 oldflavorcount;
	// XXX locking?

	if (!fFileMap->Get(file_node, &tempid)) {
		ERROR("MediaAddonServer::AddOnRemoved: inode %Ld removed, but no media add-on found\n", file_node);
		return;
	}
	id = *tempid; // tempid pointer is invalid after Removing() it from the map
	fFileMap->Remove(file_node);

	if (!fInfoMap->Get(id, &info)) {
		ERROR("MediaAddonServer::AddOnRemoved: couldn't get addon info for add-on %ld\n", id);
		oldflavorcount = 1000;
	} else {
		oldflavorcount = info->flavor_count; //same reason as above

		DestroyInstantiatedFlavors(info);
		PutAddonIfPossible(info);

		if (info->addon) {
			ERROR("MediaAddonServer::AddOnRemoved: couldn't unload addon %ld since flavors are in use\n", id);
		}
	}

	fInfoMap->Remove(id);
	_DormantNodeManager->UnregisterAddon(id);

	BPrivate::media::notifications::FlavorsChanged(id, 0, oldflavorcount);
}


void
MediaAddonServer::WatchDir(BEntry *dir)
{
	// send fake notices to trigger add-on loading
	BDirectory directory(dir);
	node_ref nref;
	entry_ref ref;
	BEntry entry;
	while (directory.GetNextEntry(&entry, false) == B_OK) {
		if (entry.GetRef(&ref) != B_OK || entry.GetNodeRef(&nref) != B_OK)
			continue;

		BMessage msg(B_NODE_MONITOR);
		msg.AddInt32("opcode", B_ENTRY_CREATED);
		msg.AddInt32("device", ref.device);
		msg.AddInt64("directory", ref.directory);
		msg.AddInt64("node", nref.node);
		msg.AddString("name", ref.name);
		msg.AddBool("nowait", true);
		MessageReceived(&msg);
	}

	dir->GetNodeRef(&nref);
	watch_node(&nref, B_WATCH_DIRECTORY, be_app_messenger);
}


void
MediaAddonServer::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MEDIA_ADDON_SERVER_PLAY_MEDIA:
		{
			const char *name, *type;
			if ((msg->FindString(MEDIA_NAME_KEY, &name) != B_OK)
				|| (msg->FindString(MEDIA_TYPE_KEY, &type) != B_OK)) {
				msg->SendReply(B_ERROR);
			}

			PlayMediaFile(type, name);
			msg->SendReply((uint32)B_OK);
				// XXX don't know which reply is expected
			return;
		}

		case B_NODE_MONITOR:
		{
			switch (msg->FindInt32("opcode")) {
				case B_ENTRY_CREATED:
				{
					const char *name;
					entry_ref ref;
					ino_t node;
					BEntry e;
					BPath p;
					msg->FindString("name", &name);
					msg->FindInt64("node", &node);
					msg->FindInt32("device", &ref.device);
					msg->FindInt64("directory", &ref.directory);
					ref.set_name(name);
					e.SetTo(&ref,false);// build a BEntry for the created file/link/dir
					e.GetPath(&p); 		// get the path to the file/link/dir
					e.SetTo(&ref,true); // travese links to see
					if (e.IsFile()) {		// if it's a link to a file, or a file
						if (false == msg->FindBool("nowait")) {
							// XXX wait 5 seconds if this is a regular notification
							// because the file creation may not be finshed when the
							// notification arrives (very ugly, how can we fix this?)
							// this will also fail if copying takes longer than 5 seconds
							snooze(5000000);
						}
						AddOnAdded(p.Path(),node);
					}
					return;
				}
				case B_ENTRY_REMOVED:
				{
					ino_t node;
					msg->FindInt64("node",&node);
					AddOnRemoved(node);
					return;
				}
				case B_ENTRY_MOVED:
				{
					ino_t from;
					ino_t to;
					msg->FindInt64("from directory", &from);
					msg->FindInt64("to directory", &to);
					if (fSystemAddOnsNode == from || fUserAddOnsNode == from) {
						msg->ReplaceInt32("opcode",B_ENTRY_REMOVED);
						msg->AddInt64("directory",from);
						MessageReceived(msg);
					}
					if (fSystemAddOnsNode == to || fUserAddOnsNode == to) {
						msg->ReplaceInt32("opcode",B_ENTRY_CREATED);
						msg->AddInt64("directory",to);
						msg->AddBool("nowait",true);
						MessageReceived(msg);
					}
					return;
				}
			}
			break;
		}

		default:
			inherited::MessageReceived(msg);
			break;
	}
	printf("MediaAddonServer: Unhandled message:\n");
	msg->PrintToStream();
}


//	#pragma mark -


void
DumpFlavorInfo(const flavor_info *info)
{
	printf("  name = %s\n",info->name);
	printf("  info = %s\n",info->info);
	printf("  internal_id = %ld\n",info->internal_id);
	printf("  possible_count = %ld\n",info->possible_count);
	printf("  flavor_flags = 0x%lx",info->flavor_flags);
	if (info->flavor_flags & B_FLAVOR_IS_GLOBAL) printf(" B_FLAVOR_IS_GLOBAL");
	if (info->flavor_flags & B_FLAVOR_IS_LOCAL) printf(" B_FLAVOR_IS_LOCAL");
	printf("\n");
	printf("  kinds = 0x%Lx",info->kinds);
	if (info->kinds & B_BUFFER_PRODUCER) printf(" B_BUFFER_PRODUCER");
	if (info->kinds & B_BUFFER_CONSUMER) printf(" B_BUFFER_CONSUMER");
	if (info->kinds & B_TIME_SOURCE) printf(" B_TIME_SOURCE");
	if (info->kinds & B_CONTROLLABLE) printf(" B_CONTROLLABLE");
	if (info->kinds & B_FILE_INTERFACE) printf(" B_FILE_INTERFACE");
	if (info->kinds & B_ENTITY_INTERFACE) printf(" B_ENTITY_INTERFACE");
	if (info->kinds & B_PHYSICAL_INPUT) printf(" B_PHYSICAL_INPUT");
	if (info->kinds & B_PHYSICAL_OUTPUT) printf(" B_PHYSICAL_OUTPUT");
	if (info->kinds & B_SYSTEM_MIXER) printf(" B_SYSTEM_MIXER");
	printf("\n");
	printf("  in_format_count = %ld\n",info->in_format_count);
	printf("  out_format_count = %ld\n",info->out_format_count);
}


int
main()
{
	new MediaAddonServer(B_MEDIA_ADDON_SERVER_SIGNATURE);
	be_app->Run();
	delete be_app;
	return 0;
}

