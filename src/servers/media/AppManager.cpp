/*
 * Copyright (c) 2002, 2003 Marcus Overhagen <Marcus@Overhagen.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files or portions
 * thereof (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice
 *    in the  binary, as well as this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided with
 *    the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <OS.h>
#include <Application.h>
#include <Roster.h>
#include <Messenger.h>
#include <Autolock.h>
#include <stdio.h>
#include "debug.h"
#include "MediaMisc.h"
#include "AppManager.h"
#include "NodeManager.h"
#include "BufferManager.h"
#include "NotificationManager.h"
#include "media_server.h"

AppManager::AppManager()
{
	fAppMap = new Map<team_id, App>;
	fLocker = new BLocker("app manager locker");
	fQuit = create_sem(0, "big brother waits");
	fBigBrother = spawn_thread(bigbrother, "big brother is watching you", B_NORMAL_PRIORITY, this);
	resume_thread(fBigBrother);
}


AppManager::~AppManager()
{
	status_t err;
	delete_sem(fQuit);
	wait_for_thread(fBigBrother, &err);
	delete fLocker;
	delete fAppMap;
}


bool
AppManager::HasTeam(team_id team)
{
	BAutolock lock(fLocker);
	return fAppMap->Has(team);
}


status_t
AppManager::RegisterTeam(team_id team, BMessenger messenger)
{
	BAutolock lock(fLocker);
	TRACE("AppManager::RegisterTeam %ld\n", team);
	if (HasTeam(team)) {
		ERROR("AppManager::RegisterTeam: team %ld already registered\n", team);
		return B_ERROR;
	}
	App app;
	app.team = team;
	app.messenger = messenger;
	return fAppMap->Insert(team, app) ? B_OK : B_ERROR;
}


status_t
AppManager::UnregisterTeam(team_id team)
{
	bool is_removed;
	
	TRACE("AppManager::UnregisterTeam %ld\n", team);
	
	fLocker->Lock();
	is_removed = fAppMap->Remove(team);
	fLocker->Unlock();
	
	CleanupTeam(team);
	
	return is_removed ? B_OK : B_ERROR;
}


status_t
AppManager::SendMessage(team_id team, BMessage *msg)
{
	BAutolock lock(fLocker);
	App *app;
	if (!fAppMap->Get(team, &app))
		return B_ERROR;
	return app->messenger.SendMessage(msg);
}


void
AppManager::TeamDied(team_id team)
{
	CleanupTeam(team);
	fLocker->Lock();
	fAppMap->Remove(team);
	fLocker->Unlock();
}


team_id
AppManager::AddonServerTeam()
{
	team_id id = be_roster->TeamFor(B_MEDIA_ADDON_SERVER_SIGNATURE);
	if (id < 0) {
		ERROR("media_server: Trouble, media_addon_server is dead!\n");
		return -1;
	}
	return id;
}


//=========================================================================
// The BigBrother thread send ping messages to the BMediaRoster of
// all currently running teams. If the reply times out or is wrong,
// the team cleanup function TeamDied() will be called.
//=========================================================================

int32
AppManager::bigbrother(void *self)
{
	static_cast<AppManager *>(self)->BigBrother();
	return 0;
}


void
AppManager::BigBrother()
{
	status_t status;
	BMessage msg('PING');
	BMessage reply;
	team_id team;
	App *app;
	do {
		if (!fLocker->Lock())
			break;
		for (fAppMap->Rewind(); fAppMap->GetNext(&app); ) {
			reply.what = 0;
			status = app->messenger.SendMessage(&msg, &reply, 5000000, 2000000);
			if (status != B_OK || reply.what != 'PONG') {
				team = app->team;
				fLocker->Unlock();
				TeamDied(team);
				continue;
			}
		}
		fLocker->Unlock();
		status = acquire_sem_etc(fQuit, 1, B_RELATIVE_TIMEOUT, 2000000);
	} while (status == B_TIMED_OUT || status == B_INTERRUPTED);
}

//=========================================================================
// The following functions must be called unlocked.
// They clean up after a crash, or start/terminate the media_addon_server.
//=========================================================================

void
AppManager::CleanupTeam(team_id team)
{
	ASSERT(false == fLocker->IsLocked());

	TRACE("AppManager: cleaning up team %ld\n", team);

	gNodeManager->CleanupTeam(team);
	gBufferManager->CleanupTeam(team);
	gNotificationManager->CleanupTeam(team);
}


void
AppManager::Dump()
{
	BAutolock lock(fLocker);
	printf("\n");
	printf("AppManager: list of applications follows:\n");
	App *app;
	app_info info;
	for (fAppMap->Rewind(); fAppMap->GetNext(&app); ) {
		be_roster->GetRunningAppInfo(app->team, &info);
		printf(" team %ld \"%s\", messenger %svalid\n", app->team, info.ref.name, app->messenger.IsValid() ? "" : "NOT ");
	}
	printf("AppManager: list end\n");
}
