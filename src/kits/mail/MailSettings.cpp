/*
 * Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 * Copyright 2004-2007, Haiku Inc. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

//!	The mail daemon's settings


#include <MailSettings.h>

#include <Message.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <File.h>
#include <Entry.h>
#include <Path.h>
#include <String.h>
#include <Messenger.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


class BMailSettings;

namespace MailInternal {
	status_t WriteMessageFile(const BMessage& archive, const BPath& path,
		const char* name);
}


//	#pragma mark - Chain methods

//
// To do
//
BMailChain*
NewMailChain()
{
	// attempted solution: use time(NULL) and hope it's unique. Is there a better idea?
	// note that two chains in two second is quite possible. how to fix this?
	// maybe we could | in some bigtime_t as well. hrrm...

	// This is to fix a problem with generating the correct id for chains.
	// Basically if the chains dir does not exist, the first time you create
	// an account both the inbound and outbound chains will be called 0.
	create_directory("/boot/home/config/settings/Mail/chains",0777);	

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("Mail/chains");
	BDirectory chain_dir(path.Path());
	BDirectory outbound_dir(&chain_dir,"outbound"), inbound_dir(&chain_dir,"inbound");

	chain_dir.Lock(); //---------Try to lock the directory

	int32 id = -1; //-----When inc'ed, we start with 0----
	chain_dir.ReadAttr("last_issued_chain_id",B_INT32_TYPE,0,&id,sizeof(id));

	BString string_id;

	do {
		id++;
		string_id = "";
		string_id << id;
	} while ((outbound_dir.Contains(string_id.String()))
		|| (inbound_dir.Contains(string_id.String())));

	chain_dir.WriteAttr("last_issued_chain_id",B_INT32_TYPE,0,&id,sizeof(id));

	return new BMailChain(id);
}


BMailChain*
GetMailChain(uint32 id)
{
	return new BMailChain(id);
}


status_t
GetInboundMailChains(BList *list)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK) {
		fprintf(stderr, "Couldn't find user settings directory: %s\n",
			strerror(status));
		return status;
	}

	path.Append("Mail/chains/inbound");
	BDirectory chainDirectory(path.Path());
	entry_ref ref;

	while (chainDirectory.GetNextRef(&ref) == B_OK) {
		char *end;
		uint32 id = strtoul(ref.name, &end, 10);

		if (!end || *end == '\0')
			list->AddItem((void*)new BMailChain(id));
	}

	return B_OK;
}


status_t
GetOutboundMailChains(BList *list)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK) {
		fprintf(stderr, "Couldn't find user settings directory: %s\n",
			strerror(status));
		return status;
	}

	path.Append("Mail/chains/outbound");
	BDirectory chainDirectory(path.Path());
	entry_ref ref;

	while (chainDirectory.GetNextRef(&ref) == B_OK) {
		char *end;
		uint32 id = strtoul(ref.name, &end, 10);
		if (!end || *end == '\0')
			list->AddItem((void*)new BMailChain(id));
	}

	return B_OK;
}


//	#pragma mark - BMailSettings


BMailSettings::BMailSettings()
{
	Reload();
}


BMailSettings::~BMailSettings()
{
}


status_t
BMailSettings::InitCheck() const
{
	return B_OK;
}


status_t
BMailSettings::Save(bigtime_t /*timeout*/)
{
	status_t ret;
	//
	// Find chain-saving directory
	//

	BPath path;
	ret = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (ret != B_OK) {
		fprintf(stderr, "Couldn't find user settings directory: %s\n",
			strerror(ret));
		return ret;
	}

	path.Append("Mail");

	status_t result = MailInternal::WriteMessageFile(data,path,"new_mail_daemon");
	if (result < B_OK)
		return result;

	BMessenger("application/x-vnd.Be-POST").SendMessage('mrrs');

	return B_OK;
}


status_t
BMailSettings::Reload()
{
	status_t ret;
	
	BPath path;
	ret = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (ret != B_OK) {
		fprintf(stderr, "Couldn't find user settings directory: %s\n",
			strerror(ret));
		return ret;
	}
	
	path.Append("Mail/new_mail_daemon");
	
	// open
	BFile settings(path.Path(),B_READ_ONLY);
	ret = settings.InitCheck();
	if (ret != B_OK) {
		fprintf(stderr, "Couldn't open settings file '%s': %s\n",
			path.Path(), strerror(ret));
		return ret;
	}
	
	// read settings
	BMessage tmp;
	ret = tmp.Unflatten(&settings);
	if (ret != B_OK) {
		fprintf(stderr, "Couldn't read settings from '%s': %s\n",
			path.Path(), strerror(ret));
		return ret;
	}
	
	// clobber old settings
	data = tmp;
	return B_OK;
}


//	# pragma mark - Global settings


int32
BMailSettings::WindowFollowsCorner()
{
	return data.FindInt32("WindowFollowsCorner");
}


void
BMailSettings::SetWindowFollowsCorner(int32 which_corner)
{
	if (data.ReplaceInt32("WindowFollowsCorner",which_corner))
		data.AddInt32("WindowFollowsCorner",which_corner);
}


uint32
BMailSettings::ShowStatusWindow()
{
	return data.FindInt32("ShowStatusWindow");
}


void
BMailSettings::SetShowStatusWindow(uint32 mode)
{
	if (data.ReplaceInt32("ShowStatusWindow",mode))
		data.AddInt32("ShowStatusWindow",mode);
}


bool
BMailSettings::DaemonAutoStarts()
{
	return data.FindBool("DaemonAutoStarts");
}


void
BMailSettings::SetDaemonAutoStarts(bool does_it)
{
	if (data.ReplaceBool("DaemonAutoStarts",does_it))
		data.AddBool("DaemonAutoStarts",does_it);
}


BRect
BMailSettings::ConfigWindowFrame()
{
	return data.FindRect("ConfigWindowFrame");
}


void
BMailSettings::SetConfigWindowFrame(BRect frame)
{
	if (data.ReplaceRect("ConfigWindowFrame",frame))
		data.AddRect("ConfigWindowFrame",frame);
}


BRect
BMailSettings::StatusWindowFrame()
{
	BRect frame;
	if (data.FindRect("StatusWindowFrame", &frame) != B_OK)
		return BRect(100, 100, 200, 120);
	
	return frame;
}


void
BMailSettings::SetStatusWindowFrame(BRect frame)
{
	if (data.ReplaceRect("StatusWindowFrame",frame))
		data.AddRect("StatusWindowFrame",frame);
}


int32
BMailSettings::StatusWindowWorkspaces()
{
	return data.FindInt32("StatusWindowWorkSpace");
}


void
BMailSettings::SetStatusWindowWorkspaces(int32 workspace)
{
	if (data.ReplaceInt32("StatusWindowWorkSpace",workspace))
		data.AddInt32("StatusWindowWorkSpace",workspace);

	BMessage msg('wsch');
	msg.AddInt32("StatusWindowWorkSpace",workspace);
	BMessenger("application/x-vnd.Be-POST").SendMessage(&msg);
}


int32
BMailSettings::StatusWindowLook()
{
	return data.FindInt32("StatusWindowLook");
}


void
BMailSettings::SetStatusWindowLook(int32 look)
{
	if (data.ReplaceInt32("StatusWindowLook",look))
		data.AddInt32("StatusWindowLook",look);
		
	BMessage msg('lkch');
	msg.AddInt32("StatusWindowLook",look);
	BMessenger("application/x-vnd.Be-POST").SendMessage(&msg);
}


bigtime_t
BMailSettings::AutoCheckInterval()
{
	bigtime_t value = B_INFINITE_TIMEOUT;
	data.FindInt64("AutoCheckInterval",&value);
	return value;
}


void
BMailSettings::SetAutoCheckInterval(bigtime_t interval)
{
	if (data.ReplaceInt64("AutoCheckInterval",interval))
		data.AddInt64("AutoCheckInterval",interval);
}


bool
BMailSettings::CheckOnlyIfPPPUp()
{
	return data.FindBool("CheckOnlyIfPPPUp");
}


void
BMailSettings::SetCheckOnlyIfPPPUp(bool yes)
{
	if (data.ReplaceBool("CheckOnlyIfPPPUp",yes))
		data.AddBool("CheckOnlyIfPPPUp",yes);
}


bool
BMailSettings::SendOnlyIfPPPUp()
{
	return data.FindBool("SendOnlyIfPPPUp");
}


void
BMailSettings::SetSendOnlyIfPPPUp(bool yes)
{
	if (data.ReplaceBool("SendOnlyIfPPPUp",yes))
		data.AddBool("SendOnlyIfPPPUp",yes);
}


uint32
BMailSettings::DefaultOutboundChainID()
{
	return data.FindInt32("DefaultOutboundChainID");
}


void
BMailSettings::SetDefaultOutboundChainID(uint32 to)
{
	if (data.ReplaceInt32("DefaultOutboundChainID",to))
		data.AddInt32("DefaultOutboundChainID",to);
}
