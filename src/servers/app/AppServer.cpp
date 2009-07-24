/*
 * Copyright (c) 2001-2009, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 * 		Christian Packmann
 */


#include "AppServer.h"

#include "BitmapManager.h"
#include "Desktop.h"
#include "FontManager.h"
#include "InputManager.h"
#include "ScreenManager.h"
#include "ServerProtocol.h"

#include <PortLink.h>

#include <syslog.h>

//#define DEBUG_SERVER
#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// Globals
port_id gAppServerPort;
static AppServer *sAppServer;
BTokenSpace gTokenSpace;
uint32 gAppServerSIMDFlags = 0;


/*!	Detect SIMD flags for use in AppServer. Checks all CPUs in the system
	and chooses the minimum supported set of instructions.
*/
static void
detect_simd()
{
#if __INTEL__
	// Only scan CPUs for which we are certain the SIMD flags are properly
	// defined.
	const char* vendorNames[] = {
		"GenuineIntel",
		"AuthenticAMD",
		"CentaurHauls", // Via CPUs, MMX and SSE support
		"RiseRiseRise", // should be MMX-only
		"CyrixInstead", // MMX-only, but custom MMX extensions
		"GenuineTMx86", // MMX and SSE
		0
	};

	system_info systemInfo;
	if (get_system_info(&systemInfo) != B_OK)
		return;

	// We start out with all flags set and end up with only those flags
	// supported across all CPUs found.
	uint32 appServerSIMD = 0xffffffff;

	for (int32 cpu = 0; cpu < systemInfo.cpu_count; cpu++) {
		cpuid_info cpuInfo;
		get_cpuid(&cpuInfo, 0, cpu);

		// Get the vendor string and terminate it manually
		char vendor[13];
		memcpy(vendor, cpuInfo.eax_0.vendor_id, 12);
		vendor[12] = 0;

		bool vendorFound = false;
		for (uint32 i = 0; vendorNames[i] != 0; i++) {
			if (strcmp(vendor, vendorNames[i]) == 0)
				vendorFound = true;
		}

		uint32 cpuSIMD = 0;
		uint32 maxStdFunc = cpuInfo.regs.eax;
		if (vendorFound && maxStdFunc >= 1) {
			get_cpuid(&cpuInfo, 1, 0);
			uint32 edx = cpuInfo.regs.edx;
			if (edx & (1 << 23))
				cpuSIMD |= APPSERVER_SIMD_MMX;
			if (edx & (1 << 25))
				cpuSIMD |= APPSERVER_SIMD_SSE;
		} else {
			// no flags can be identified
			cpuSIMD = 0;
		}
		appServerSIMD &= cpuSIMD;
	}
	gAppServerSIMDFlags = appServerSIMD;
#endif	// __INTEL__
}


//	#pragma mark -


/*!
	\brief Constructor

	This loads the default fonts, allocates all the major global variables, spawns the main housekeeping
	threads, loads user preferences for the UI and decorator, and allocates various locks.
*/
AppServer::AppServer()
	:
	MessageLooper("app_server"),
	fMessagePort(-1),
	fDesktops(),
	fDesktopLock("AppServerDesktopLock")
{
	openlog("app_server", 0, LOG_DAEMON);

	fMessagePort = create_port(DEFAULT_MONITOR_PORT_SIZE, SERVER_PORT_NAME);
	if (fMessagePort < B_OK)
		debugger("app_server could not create message port");

	fLink.SetReceiverPort(fMessagePort);

	sAppServer = this;

	gInputManager = new InputManager();

	// Create the font server and scan the proper directories.
	gFontManager = new FontManager;
	if (gFontManager->InitCheck() != B_OK)
		debugger("font manager could not be initialized!");

	gFontManager->Run();

	gScreenManager = new ScreenManager();
	gScreenManager->Run();

	// Create the bitmap allocator. Object declared in BitmapManager.cpp
	gBitmapManager = new BitmapManager();

	// Initialize SIMD flags
	detect_simd();
}


/*!	\brief Destructor
	Reached only when the server is asked to shut down in Test mode.
*/
AppServer::~AppServer()
{
	delete gBitmapManager;

	gScreenManager->Lock();
	gScreenManager->Quit();

	gFontManager->Lock();
	gFontManager->Quit();

	closelog();
}


void
AppServer::RunLooper()
{
	rename_thread(find_thread(NULL), "picasso");
	_message_thread((void *)this);
}


/*!	\brief Creates a desktop object for an authorized user
*/
Desktop *
AppServer::_CreateDesktop(uid_t userID)
{
	BAutolock locker(fDesktopLock);
	Desktop* desktop = NULL;
	try {
		desktop = new Desktop(userID);

		status_t status = desktop->Init();
		if (status == B_OK) {
			if (!desktop->Run())
				status = B_ERROR;
		}
		if (status == B_OK && !fDesktops.AddItem(desktop))
			status = B_NO_MEMORY;

		if (status < B_OK) {
			fprintf(stderr, "Cannot initialize Desktop object: %s\n", strerror(status));
			delete desktop;
			return NULL;
		}
	} catch (...) {
		// there is obviously no memory left
		return NULL;
	}

	return desktop;
}


/*!	\brief Finds the desktop object that belongs to a certain user
*/
Desktop *
AppServer::_FindDesktop(uid_t userID)
{
	BAutolock locker(fDesktopLock);

	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);

		if (desktop->UserID() == userID)
			return desktop;
	}

	return NULL;
}


/*!	\brief Message handling function for all messages sent to the app_server
	\param code ID of the message sent
	\param buffer Attachment buffer for the message.

*/
void
AppServer::_DispatchMessage(int32 code, BPrivate::LinkReceiver& msg)
{
	switch (code) {
		case AS_GET_DESKTOP:
		{
			port_id replyPort;
			if (msg.Read<port_id>(&replyPort) < B_OK)
				break;

			int32 userID;
			msg.Read<int32>(&userID);

			Desktop* desktop = _FindDesktop(userID);
			if (desktop == NULL) {
				// we need to create a new desktop object for this user
				// ToDo: test if the user exists on the system
				// ToDo: maybe have a separate AS_START_DESKTOP_SESSION for authorizing the user
				desktop = _CreateDesktop(userID);
			}

			BPrivate::LinkSender reply(replyPort);
			if (desktop != NULL) {
				reply.StartMessage(B_OK);
				reply.Attach<port_id>(desktop->MessagePort());
			} else
				reply.StartMessage(B_ERROR);

			reply.Flush();
			break;
		}

#if TEST_MODE
		case B_QUIT_REQUESTED:
		{
			// We've been asked to quit, so (for now) broadcast to all
			// desktops to quit. This situation will occur only when the server
			// is compiled as a regular Be application.

			fQuitting = true;

			while (fDesktops.CountItems() > 0) {
				Desktop *desktop = fDesktops.RemoveItemAt(0);

				thread_id thread = desktop->Thread();
				desktop->PostMessage(B_QUIT_REQUESTED);

				// we just wait for the desktop to kill itself
				status_t status;
				wait_for_thread(thread, &status);
			}

			delete this;

			// we are now clear to exit
			exit(0);
			break;
		}
#endif

		default:
			STRACE(("Server::MainLoop received unexpected code %ld (offset %ld)\n",
				code, code - SERVER_TRUE));
			break;
	}
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	// There can be only one....
	if (find_port(SERVER_PORT_NAME) >= B_OK)
		return -1;

	srand(real_time_clock_usecs());

	AppServer* server = new AppServer;
	server->RunLooper();

	return 0;
}
