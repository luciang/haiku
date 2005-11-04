/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Adrian Oanca <adioanca@cotty.iren.ro>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Jérôme Duval, jerome.duval@free.fr
 */

/*!
	\class ServerApp ServerApp.h
	\brief Counterpart to BApplication within the app_server
*/

#include <stdio.h>
#include <string.h>

#include <AppDefs.h>
#include <Autolock.h>
#include <ColorSet.h>
#include <List.h>
#include <ScrollBar.h>
#include <ServerProtocol.h>
#include <Shape.h>
#include <String.h>

#include "AppServer.h"
#include "BGet++.h"
#include "BitmapManager.h"
#include "CursorManager.h"
#include "CursorSet.h"
#include "Desktop.h"
#include "DecorManager.h"
#include "DrawingEngine.h"
#include "FontManager.h"
#include "HWInterface.h"
//#include "DrawState.h"
#include "OffscreenServerWindow.h"
#include "RAMLinkMsgReader.h"
#include "RootLayer.h"
#include "ServerBitmap.h"
#include "ServerConfig.h"
#include "ServerCursor.h"
#include "ServerPicture.h"
#include "ServerScreen.h"
#include "ServerWindow.h"
#include "SystemPalette.h"
#include "Utils.h"
#include "WinBorder.h"

#include "ServerApp.h"
#include <syslog.h>

//#define DEBUG_SERVERAPP

#ifdef DEBUG_SERVERAPP
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

//#define DEBUG_SERVERAPP_FONT

#ifdef DEBUG_SERVERAPP_FONT
#	define FTRACE(x) printf x
#else
#	define FTRACE(x) ;
#endif


static const uint32 kMsgAppQuit = 'appQ';


/*!
	\brief Constructor
	\param sendport port ID for the BApplication which will receive the ServerApp's messages
	\param rcvport port by which the ServerApp will receive messages from its BApplication.
	\param fSignature NULL-terminated string which contains the BApplication's
	MIME fSignature.
*/
ServerApp::ServerApp(Desktop* desktop, port_id clientReplyPort,
	port_id clientLooperPort, team_id clientTeam, int32 handlerID,
	const char* signature)
	: MessageLooper("application"),
	fMessagePort(-1),
	fClientReplyPort(clientReplyPort),
	fClientLooperPort(clientLooperPort),
	fClientToken(handlerID),
	fDesktop(desktop),
	fSignature(signature),
	fClientTeam(clientTeam),
	fWindowListLock("window list"),
	fAppCursor(NULL),
	fCursorHidden(false),
	fIsActive(false),
	fSharedMem("shared memory")
{
	if (fSignature == "")
		fSignature = "application/no-signature";

	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "a<%s", Signature());

	fMessagePort = create_port(DEFAULT_MONITOR_PORT_SIZE, name);
	if (fMessagePort < B_OK)
		return;

	fLink.SetSenderPort(fClientReplyPort);
	fLink.SetReceiverPort(fMessagePort);

	// we let the application own the port, so that we get aware when it's gone
	if (set_port_owner(fMessagePort, clientTeam) < B_OK) {
		delete_port(fMessagePort);
		fMessagePort = -1;
		return;
	}

	ServerCursor *defaultCursor = 
		fDesktop->GetCursorManager().GetCursor(B_CURSOR_DEFAULT);

	if (defaultCursor) {
		fAppCursor = new ServerCursor(defaultCursor);
		fAppCursor->SetOwningTeam(fClientTeam);
	}

	STRACE(("ServerApp %s:\n", Signature()));
	STRACE(("\tBApp port: %ld\n", fClientReplyPort));
	STRACE(("\tReceiver port: %ld\n", fMessagePort));
}


//! Does all necessary teardown for application
ServerApp::~ServerApp(void)
{
	STRACE(("*ServerApp %s:~ServerApp()\n", Signature()));

	if (!fQuitting)
		CRITICAL("ServerApp: destructor called after Run()!\n");

	fWindowListLock.Lock();

	// quit all server windows

	for (int32 i = fWindowList.CountItems(); i-- > 0;) {
		ServerWindow* window = (ServerWindow*)fWindowList.ItemAt(i);
		window->Quit();
	}
	int32 tries = fWindowList.CountItems() + 1;

	fWindowListLock.Unlock();

	// wait for the windows to quit
	while (tries-- > 0) {
		fWindowListLock.Lock();
		if (fWindowList.CountItems() == 0) {
			// we leave the list locked, doesn't matter anymore
			break;
		}

		fWindowListLock.Unlock();
		snooze(10000);
	}

	if (tries < 0) {
		// This really shouldn't happen, as it shows we're buggy
#if __HAIKU__
		syslog(LOG_ERR, "ServerApp %s needs to kill some server windows...\n", Signature());
#else
		fprintf(stderr, "ServerApp %s needs to kill some server windows...\n", Signature());
#endif

		// there still seem to be some windows left - kill them!
		fWindowListLock.Lock();

		for (int32 i = 0; i < fWindowList.CountItems(); i++) {
			ServerWindow* window = (ServerWindow*)fWindowList.ItemAt(i);
			printf("kill window \"%s\"\n", window->Title());

			kill_thread(window->Thread());
			window->Hide();
			delete window;
		}

		fWindowListLock.Unlock();
	}

	// first, make sure our monitor thread doesn't 
	for (int32 i = 0; i < fBitmapList.CountItems(); i++) {
		gBitmapManager->DeleteBitmap(static_cast<ServerBitmap *>(fBitmapList.ItemAt(i)));
	}

	for (int32 i = 0; i < fPictureList.CountItems(); i++) {
		delete (ServerPicture*)fPictureList.ItemAt(i);
	}

	fDesktop->GetCursorManager().RemoveAppCursors(fClientTeam);

	STRACE(("ServerApp %s::~ServerApp(): Exiting\n", Signature()));
}


/*!
	\brief Checks if the application was initialized correctly
*/
status_t
ServerApp::InitCheck()
{
	if (fMessagePort < B_OK)
		return fMessagePort;

	if (fClientReplyPort < B_OK)
		return fClientReplyPort;

	if (fWindowListLock.Sem() < B_OK)
		return fWindowListLock.Sem();

	return B_OK;
}


/*!
	\brief Starts the ServerApp monitoring for messages
	\return false if the application couldn't start, true if everything went OK.
*/
bool
ServerApp::Run()
{
	if (!MessageLooper::Run())
		return false;

	// Let's tell the client how to talk with us
	fLink.StartMessage(SERVER_TRUE);
	fLink.Attach<int32>(fMessagePort);
	fLink.Flush();

	return true;
}


/*!
	\brief This quits the application and deletes it. You're not supposed
		to call its destructor directly.

	At the point you're calling this method, the application should already
	be removed from the application list.
*/
void
ServerApp::Quit(sem_id shutdownSemaphore)
{
	if (fThread < B_OK) {
		delete this;
		return;
	}

	// execute application deletion in the message looper thread

	fQuitting = true;
	PostMessage(kMsgAppQuit);

	send_data(fThread, 'QUIT', &shutdownSemaphore, sizeof(sem_id));
}


/*!
	\brief Send a message to the ServerApp's BApplication
	\param msg The message to send
*/
void
ServerApp::SendMessageToClient(const BMessage *msg) const
{
	ssize_t size = msg->FlattenedSize();
	char *buffer = new char[size];

	if (msg->Flatten(buffer, size) == B_OK)
		write_port(fClientLooperPort, msg->what, buffer, size);
	else
		printf("PANIC: ServerApp: '%s': can't flatten message in 'SendMessageToClient()'\n", Signature());

	delete [] buffer;
}


/*!
	\brief Sets the ServerApp's active status
	\param value The new status of the ServerApp.
	
	This changes an internal flag and also sets the current cursor to the one specified by
	the application
*/
void
ServerApp::Activate(bool value)
{
	fIsActive = value;
	SetAppCursor();
}


//! Sets the cursor to the application cursor, if any.
void
ServerApp::SetAppCursor(void)
{
	if (fAppCursor)
		fDesktop->GetHWInterface()->SetCursor(fAppCursor);
}


void
ServerApp::_GetLooperName(char* name, size_t length)
{
#ifdef __HAIKU__
	strlcpy(name, Signature(), length);
#else
	strncpy(name, Signature(), length);
	name[length - 1] = '\0';
#endif
}


/*!
	\brief The thread function ServerApps use to monitor messages
*/
void
ServerApp::_MessageLooper()
{
	// Message-dispatching loop for the ServerApp

	BPrivate::LinkReceiver &receiver = fLink.Receiver();

	int32 code;
	status_t err = B_OK;

	while (!fQuitting) {
		STRACE(("info: ServerApp::_MessageLooper() listening on port %ld.\n", fMessagePort));
		err = receiver.GetNextMessage(code, B_INFINITE_TIMEOUT);
		if (err < B_OK || code == B_QUIT_REQUESTED) {
			STRACE(("ServerApp: application seems to be gone...\n"));

			// Tell desktop to quit us
			BPrivate::LinkSender link(fDesktop->MessagePort());
			link.StartMessage(AS_DELETE_APP);
			link.Attach<thread_id>(Thread());
			link.Flush();
			break;
		}

		switch (code) {
			case kMsgAppQuit:
				// we receive this from our destructor on quit
				fQuitting = true;
				break;

			case AS_CREATE_WINDOW:
			case AS_CREATE_OFFSCREEN_WINDOW:
			{
				// Create a ServerWindow/OffscreenServerWindow
				
				// Attached data:
				// 1) int32 bitmap token (only for AS_CREATE_OFFSCREEN_WINDOW)
				// 2) BRect window frame
				// 3) uint32 window look
				// 4) uint32 window feel
				// 5) uint32 window flags
				// 6) uint32 workspace index
				// 7) int32 BHandler token of the window
				// 8) port_id window's reply port
				// 9) port_id window's looper port
				// 10) const char * title

				BRect frame;
				int32 bitmapToken;
				uint32 look;
				uint32 feel;
				uint32 flags;
				uint32 workspaces;
				int32 token = B_NULL_TOKEN;
				port_id	clientReplyPort = -1;
				port_id looperPort = -1;
				char *title = NULL;

				if (code == AS_CREATE_OFFSCREEN_WINDOW)
					receiver.Read<int32>(&bitmapToken);
				
				receiver.Read<BRect>(&frame);
				receiver.Read<uint32>(&look);
				receiver.Read<uint32>(&feel);
				receiver.Read<uint32>(&flags);
				receiver.Read<uint32>(&workspaces);
				receiver.Read<int32>(&token);
				receiver.Read<port_id>(&clientReplyPort);
				receiver.Read<port_id>(&looperPort);
				if (receiver.ReadString(&title) != B_OK)
					break;

				if (!frame.IsValid()) {
					// make sure we pass a valid rectangle to ServerWindow
					frame.right = frame.left + 1;
					frame.bottom = frame.top + 1;
				}
				
				bool success = false;
				ServerWindow *window = NULL;
				
				if (code == AS_CREATE_OFFSCREEN_WINDOW) {
					ServerBitmap* bitmap = FindBitmap(bitmapToken);
				
					if (bitmap != NULL)
						// ServerWindow constructor will reply with port_id of a newly created port
						window = new OffscreenServerWindow(title, this, clientReplyPort, 
																looperPort, token, bitmap);
					else
						free(title);
				} else {
					window = new ServerWindow(title, this, clientReplyPort, looperPort, token);
					STRACE(("\nServerApp %s: New Window %s (%.1f,%.1f,%.1f,%.1f)\n",
							fSignature(), title, frame.left, frame.top, frame.right, frame.bottom));
				}
				
				// NOTE: the reply to the client is handled in window->Run()				
				if (window != NULL) {
					success = window->Init(frame, look, feel, flags, workspaces) >= B_OK && window->Run();

					// add the window to the list
					if (success && fWindowListLock.Lock()) {
						success = fWindowList.AddItem(window);
						fWindowListLock.Unlock();
					}

					if (!success)
						delete window;
				}
				
				if (!success) {
					// window creation failed, we need to notify the client
					BPrivate::LinkSender reply(clientReplyPort);
					reply.StartMessage(SERVER_FALSE);
					reply.Flush();
				}

				// We don't have to free the title, as it's owned by the ServerWindow now
				break;
			}

			case AS_QUIT_APP:
			{
				// This message is received only when the app_server is asked to shut down in
				// test/debug mode. Of course, if we are testing while using AccelerantDriver, we do
				// NOT want to shut down client applications. The server can be quit o in this fashion
				// through the driver's interface, such as closing the ViewDriver's window.

				STRACE(("ServerApp %s:Server shutdown notification received\n", Signature()));

				// If we are using the real, accelerated version of the
				// DrawingEngine, we do NOT want the user to be able shut down
				// the server. The results would NOT be pretty
#if TEST_MODE
				BMessage pleaseQuit(B_QUIT_REQUESTED);
				SendMessageToClient(&pleaseQuit);
#endif
				break;
			}

			default:
				STRACE(("ServerApp %s: Got a Message to dispatch\n", Signature()));
				_DispatchMessage(code, receiver);
				break;
		}
	}

	// Quit() will send us a message; we're handling the exiting procedure
	thread_id sender;
	sem_id shutdownSemaphore;
	receive_data(&sender, &shutdownSemaphore, sizeof(sem_id));

	delete this;

	if (shutdownSemaphore >= B_OK)
		release_sem(shutdownSemaphore);
}


/*!
	\brief Handler function for BApplication API messages
	\param code Identifier code for the message. Equivalent to BMessage::what
	\param buffer Any attachments
	
	Note that the buffer's exact format is determined by the particular message. 
	All attachments are placed in the buffer via a PortLink, so it will be a 
	matter of casting and incrementing an index variable to access them.
*/
void
ServerApp::_DispatchMessage(int32 code, BPrivate::LinkReceiver &link)
{
	switch (code) {
		case AS_GET_WINDOW_LIST:
			team_id team;
			link.Read<team_id>(&team);

			fDesktop->WriteWindowList(team, fLink.Sender());
			break;

		case AS_GET_WINDOW_INFO:
			int32 serverToken;
			link.Read<int32>(&serverToken);

			fDesktop->WriteWindowInfo(serverToken, fLink.Sender());
			break;

		case AS_UPDATE_COLORS:
		{
			// NOTE: R2: Eventually we will have windows which will notify their children of changes in 
			// system colors
			
/*			STRACE(("ServerApp %s: Received global UI color update notification\n", Signature()));
			ServerWindow *win;
			BMessage msg(_COLORS_UPDATED);
			
			for(int32 i = 0; i < fWindowList.CountItems(); i++) {
				win=(ServerWindow*)fWindowList.ItemAt(i);
				win->GetWinBorder()->UpdateColors();
				win->SendMessageToClient(AS_UPDATE_COLORS, msg);
			}
*/			break;
		}
		case AS_UPDATE_FONTS:
		{
			// NOTE: R2: Eventually we will have windows which will notify their children of changes in 
			// system fonts
			
/*			STRACE(("ServerApp %s: Received global font update notification\n", Signature()));
			ServerWindow *win;
			BMessage msg(_FONTS_UPDATED);
			
			for(int32 i=0; i<fSWindowList->CountItems(); i++)
			{
				win=(ServerWindow*)fSWindowList->ItemAt(i);
				win->GetWinBorder()->UpdateFont();
				win->SendMessageToClient(AS_UPDATE_FONTS, msg);
			}
*/			break;
		}
		case AS_AREA_MESSAGE:
		{
			// This occurs in only one kind of case: a message is too big to send over a port. This
			// is really an edge case, so this shouldn't happen *too* often
			
			// Attached Data:
			// 1) area_id id of an area already owned by the server containing the message
			// 2) size_t offset of the pointer in the area
			// 3) size_t size of the message
			
			area_id area;
			size_t offset;
			size_t msgsize;
			area_info ai;
			int8 *msgpointer;

			link.Read<area_id>(&area);
			link.Read<size_t>(&offset);
			link.Read<size_t>(&msgsize);

			// Part sanity check, part get base pointer :)
			if (get_area_info(area, &ai) < B_OK)
				break;

			msgpointer = (int8*)ai.address + offset;

			RAMLinkMsgReader mlink(msgpointer);
			_DispatchMessage(mlink.Code(), mlink);

			// This is a very special case in the sense that when ServerMemIO is used for this 
			// purpose, it will be set to NOT automatically free the memory which it had 
			// requested. This is the server's job once the message has been dispatched.
			fSharedMem.ReleaseBuffer(msgpointer);
			break;
		}
		case AS_ACQUIRE_SERVERMEM:
		{
			// This particular call is more than a bit of a pain in the neck. We are given a
			// size of a chunk of memory needed. We need to (1) allocate it, (2) get the area for
			// this particular chunk, (3) find the offset in the area for this chunk, and (4)
			// tell the client about it. Good thing this particular call isn't used much
			
			// Received from a ServerMemIO object requesting operating memory
			// Attached Data:
			// 1) size_t requested size
			// 2) port_id reply_port

			size_t memsize;
			link.Read<size_t>(&memsize);

			// TODO: I wonder if ACQUIRE_SERVERMEM should have a minimum size requirement?

			void *sharedmem = fSharedMem.GetBuffer(memsize);

			if (memsize < 1 || sharedmem == NULL) {
				fLink.StartMessage(SERVER_FALSE);
				fLink.Flush();
				break;
			}
			
			area_id owningArea = area_for(sharedmem);
			area_info info;

			if (owningArea == B_ERROR || get_area_info(owningArea, &info) < B_OK) {
				fLink.StartMessage(SERVER_FALSE);
				fLink.Flush();
				break;
			}

			int32 areaoffset = (addr_t)sharedmem - (addr_t)info.address;
			STRACE(("Successfully allocated shared memory of size %ld\n",memsize));

			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<area_id>(owningArea);
			fLink.Attach<int32>(areaoffset);
			fLink.Flush();
			break;
		}
		case AS_RELEASE_SERVERMEM:
		{
			// Received when a ServerMemIO object on destruction
			// Attached Data:
			// 1) area_id owning area
			// 2) int32 area offset
			
			area_id owningArea;
			int32 areaoffset;
				
			link.Read<area_id>(&owningArea);
			link.Read<int32>(&areaoffset);
			
			area_info areaInfo;
			if (owningArea < 0 || get_area_info(owningArea, &areaInfo) != B_OK)
				break;
			
			STRACE(("Successfully freed shared memory\n"));
			void *sharedmem = ((int32*)areaInfo.address) + areaoffset;
			
			fSharedMem.ReleaseBuffer(sharedmem);
			
			break;
		}
		case AS_UPDATE_DECORATOR:
		{
			STRACE(("ServerApp %s: Received decorator update notification\n", Signature()));

			for (int32 i = 0; i < fWindowList.CountItems(); i++) {
				ServerWindow *window = (ServerWindow*)fWindowList.ItemAt(i);
				window->Lock();
				const_cast<WinBorder *>(window->GetWinBorder())->UpdateDecorator();
				window->Unlock();
			}
			break;
		}
		case AS_SET_DECORATOR:
		{
			// Received from an application when the user wants to set the window
			// decorator to a new one

			// Attached Data:
			// int32 the index of the decorator to use

			int32 index;
			link.Read<int32>(&index);
			if (gDecorManager.SetDecorator(index))
				fDesktop->BroadcastToAllApps(AS_UPDATE_DECORATOR);
			break;
		}
		case AS_COUNT_DECORATORS:
		{
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<int32>(gDecorManager.CountDecorators());
			fLink.Flush();
			break;
		}
		case AS_GET_DECORATOR:
		{
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<int32>(gDecorManager.GetDecorator());
			fLink.Flush();
			break;
		}
		case AS_GET_DECORATOR_NAME:
		{
			int32 index;
			link.Read<int32>(&index);

			BString str(gDecorManager.GetDecoratorName(index));
			if (str.CountChars() > 0)
			{
				fLink.StartMessage(SERVER_TRUE);
				fLink.AttachString(str.String());
			}
			else
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			break;
		}
		case AS_R5_SET_DECORATOR:
		{
			// Sort of supports Tracker's nifty Easter Egg. It was easy to do and 
			// it's kind of neat, so why not?

			// Attached Data:
			// int32 value of the decorator to use
			// 0: BeOS
			// 1: Amiga
			// 2: Windows
			// 3: MacOS

			int32 decindex = 0;
			link.Read<int32>(&decindex);

			if (gDecorManager.SetR5Decorator(decindex))
				fDesktop->BroadcastToAllApps(AS_UPDATE_DECORATOR);
			
			break;
		}
		case AS_CREATE_BITMAP:
		{
			STRACE(("ServerApp %s: Received BBitmap creation request\n", Signature()));
			// Allocate a bitmap for an application
			
			// Attached Data:
			// 1) BRect bounds
			// 2) color_space space
			// 3) int32 bitmap_flags
			// 4) int32 bytes_per_row
			// 5) int32 screen_id::id
			// 6) port_id reply port
			
			// Reply Code: SERVER_TRUE
			// Reply Data:
			//	1) int32 server token
			//	2) area_id id of the area in which the bitmap data resides
			//	3) int32 area pointer offset used to calculate fBasePtr
			
			// First, let's attempt to allocate the bitmap
			ServerBitmap *bitmap = NULL;
			BRect frame;
			color_space colorSpace;
			int32 flags, bytesPerRow;
			screen_id screenID;

			link.Read<BRect>(&frame);
			link.Read<color_space>(&colorSpace);
			link.Read<int32>(&flags);
			link.Read<int32>(&bytesPerRow);
			if (link.Read<screen_id>(&screenID) == B_OK) {
				bitmap = gBitmapManager->CreateBitmap(frame, colorSpace, flags,
													  bytesPerRow, screenID);
			}

			STRACE(("ServerApp %s: Create Bitmap (%.1fx%.1f)\n",
						Signature(), frame.Width(), frame.Height()));

			if (bitmap && fBitmapList.AddItem((void*)bitmap)) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<int32>(bitmap->Token());
				fLink.Attach<area_id>(bitmap->Area());
				fLink.Attach<int32>(bitmap->AreaOffset());
			} else {
				// alternatively, if something went wrong, we reply with SERVER_FALSE
				fLink.StartMessage(SERVER_FALSE);
			}

			fLink.Flush();
			break;
		}
		case AS_DELETE_BITMAP:
		{
			STRACE(("ServerApp %s: received BBitmap delete request\n", Signature()));
			// Delete a bitmap's allocated memory

			// Attached Data:
			// 1) int32 token
			// 2) int32 reply port

			// Reply Code: SERVER_TRUE if successful, 
			//				SERVER_FALSE if the buffer was already deleted or was not found
			int32 id;
			link.Read<int32>(&id);

			ServerBitmap *bitmap = FindBitmap(id);
			if (bitmap && fBitmapList.RemoveItem((void*)bitmap)) {
				STRACE(("ServerApp %s: Deleting Bitmap %ld\n", Signature(), id));

				gBitmapManager->DeleteBitmap(bitmap);
				fLink.StartMessage(SERVER_TRUE);
			} else
				fLink.StartMessage(SERVER_FALSE);

			fLink.Flush();	
			break;
		}
#if 0
		case AS_CREATE_PICTURE:
		{
			// TODO: Implement AS_CREATE_PICTURE
			STRACE(("ServerApp %s: Create Picture unimplemented\n", Signature()));
			break;
		}
		case AS_DELETE_PICTURE:
		{
			// TODO: Implement AS_DELETE_PICTURE
			STRACE(("ServerApp %s: Delete Picture unimplemented\n", Signature()));
			break;
		}
		case AS_CLONE_PICTURE:
		{
			// TODO: Implement AS_CLONE_PICTURE
			STRACE(("ServerApp %s: Clone Picture unimplemented\n", Signature()));
			break;
		}
		case AS_DOWNLOAD_PICTURE:
		{
			// TODO; Implement AS_DOWNLOAD_PICTURE
			STRACE(("ServerApp %s: Download Picture unimplemented\n", Signature()));
			
			// What is this particular function call for, anyway?
			
			// DW: I think originally it might have been to support 
			// the undocumented Flatten function.
			break;
		}
#endif
		case AS_CURRENT_WORKSPACE:
		{
			STRACE(("ServerApp %s: get current workspace\n", Signature()));

			// TODO: Locking this way is not nice
			RootLayer *root = fDesktop->ActiveRootLayer();
			root->Lock();
			
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<int32>(root->ActiveWorkspaceIndex());
			fLink.Flush();
			
			root->Unlock();
			break;
		}
		
		case AS_ACTIVATE_WORKSPACE:
		{
			STRACE(("ServerApp %s: activate workspace\n", Signature()));
			
			// TODO: See above
			int32 index;
			link.Read<int32>(&index);
			RootLayer *root = fDesktop->ActiveRootLayer();
			root->Lock();
			root->SetActiveWorkspace(index);
			root->Unlock();
			// no reply
		
			break;
		}
		
		case AS_SHOW_CURSOR:
		{
			STRACE(("ServerApp %s: Show Cursor\n", Signature()));
			// TODO: support nested showing/hiding
			fDesktop->GetHWInterface()->SetCursorVisible(true);
			fCursorHidden = false;
			break;
		}
		case AS_HIDE_CURSOR:
		{
			STRACE(("ServerApp %s: Hide Cursor\n", Signature()));
			// TODO: support nested showing/hiding
			fDesktop->GetHWInterface()->SetCursorVisible(false);
			fCursorHidden = true;
			break;
		}
		case AS_OBSCURE_CURSOR:
		{
			STRACE(("ServerApp %s: Obscure Cursor\n", Signature()));
			// ToDo: Enable ObscureCursor
			//fDesktop->GetHWInterface()->ObscureCursor();
			break;
		}
		case AS_QUERY_CURSOR_HIDDEN:
		{
			STRACE(("ServerApp %s: Received IsCursorHidden request\n", Signature()));
			fLink.StartMessage(fCursorHidden ? SERVER_TRUE : SERVER_FALSE);
			fLink.Flush();
			break;
		}
		case AS_SET_CURSOR_DATA:
		{
			STRACE(("ServerApp %s: SetCursor via cursor data\n", Signature()));
			// Attached data: 68 bytes of fAppCursor data
			
			int8 cdata[68];
			link.Read(cdata,68);
			
			// Because we don't want an overaccumulation of these particular
			// cursors, we will delete them if there is an existing one. It would
			// otherwise be easy to crash the server by calling SetCursor a
			// sufficient number of times
			if(fAppCursor)
				fDesktop->GetCursorManager().DeleteCursor(fAppCursor->ID());

			fAppCursor = new ServerCursor(cdata);
			fAppCursor->SetOwningTeam(fClientTeam);
			fAppCursor->SetAppSignature(Signature());

			// ToDo: These two should probably both be done in Desktop directly
			fDesktop->GetCursorManager().AddCursor(fAppCursor);
			fDesktop->GetHWInterface()->SetCursor(fAppCursor);
			break;
		}
		case AS_SET_CURSOR_BCURSOR:
		{
			STRACE(("ServerApp %s: SetCursor via BCursor\n", Signature()));
			// Attached data:
			// 1) bool flag to send a reply
			// 2) int32 token ID of the cursor to set
			// 3) port_id port to receive a reply. Only exists if the sync flag is true.
			bool sync;
			int32 ctoken = B_NULL_TOKEN;
			
			link.Read<bool>(&sync);
			link.Read<int32>(&ctoken);

			ServerCursor *cursor = fDesktop->GetCursorManager().FindCursor(ctoken);
			if (cursor)
				fDesktop->GetHWInterface()->SetCursor(cursor);

			if (sync) {
				// the application is expecting a reply, but plans to do literally nothing
				// with the data, so we'll just reuse the cursor token variable
				fLink.StartMessage(SERVER_TRUE);
				fLink.Flush();
			}
			break;
		}
		case AS_CREATE_BCURSOR:
		{
			STRACE(("ServerApp %s: Create BCursor\n", Signature()));
			// Attached data:
			// 1) 68 bytes of fAppCursor data
			// 2) port_id reply port
			
			int8 cursorData[68];
			link.Read(cursorData, sizeof(cursorData));

			// Because we don't want an overaccumulation of these particular
			// cursors, we will delete them if there is an existing one. It would
			// otherwise be easy to crash the server by calling CreateCursor a
			// sufficient number of times
			if (fAppCursor)
				fDesktop->GetCursorManager().DeleteCursor(fAppCursor->ID());

			fAppCursor = new ServerCursor(cursorData);
			fAppCursor->SetOwningTeam(fClientTeam);
			fAppCursor->SetAppSignature(Signature());
			fDesktop->GetCursorManager().AddCursor(fAppCursor);

			// Synchronous message - BApplication is waiting on the cursor's ID
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<int32>(fAppCursor->ID());
			fLink.Flush();
			break;
		}
		case AS_DELETE_BCURSOR:
		{
			STRACE(("ServerApp %s: Delete BCursor\n", Signature()));
			// Attached data:
			// 1) int32 token ID of the cursor to delete
			int32 ctoken = B_NULL_TOKEN;
			link.Read<int32>(&ctoken);

			if (fAppCursor && fAppCursor->ID() == ctoken)
				fAppCursor = NULL;

			fDesktop->GetCursorManager().DeleteCursor(ctoken);
			break;
		}
		case AS_GET_SCROLLBAR_INFO:
		{
			STRACE(("ServerApp %s: Get ScrollBar info\n", Signature()));
			scroll_bar_info info;
			DesktopSettings settings(fDesktop);
			settings.GetScrollBarInfo(info);

			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<scroll_bar_info>(info);
			fLink.Flush();
			break;
		}
		case AS_SET_SCROLLBAR_INFO:
		{
			STRACE(("ServerApp %s: Set ScrollBar info\n", Signature()));
			// Attached Data:
			// 1) scroll_bar_info scroll bar info structure
			scroll_bar_info info;
			if (link.Read<scroll_bar_info>(&info) == B_OK) {
				DesktopSettings settings(fDesktop);
				settings.SetScrollBarInfo(info);
			}

			fLink.StartMessage(SERVER_TRUE);
			fLink.Flush();
			break;
		}

		case AS_GET_MENU_INFO:
		{
			STRACE(("ServerApp %s: Get menu info\n", Signature()));
			menu_info info;
			DesktopSettings settings(fDesktop);
			settings.GetMenuInfo(info);

			fLink.StartMessage(B_OK);
			fLink.Attach<menu_info>(info);
			fLink.Flush();
			break;
		}
		case AS_SET_MENU_INFO:
		{
			STRACE(("ServerApp %s: Set menu info\n", Signature()));
			menu_info info;
			if (link.Read<menu_info>(&info) == B_OK) {
				DesktopSettings settings(fDesktop);
				settings.SetMenuInfo(info);
					// TODO: SetMenuInfo() should do some validity check, so
					//	that the answer we're giving can actually be useful
			}

			fLink.StartMessage(B_OK);
			fLink.Flush();
			break;
		}

		case AS_SET_MOUSE_MODE:
		{
			STRACE(("ServerApp %s: Set Focus Follows Mouse mode\n", Signature()));
			// Attached Data:
			// 1) enum mode_mouse FFM mouse mode
			mode_mouse mouseMode;
			if (link.Read<mode_mouse>(&mouseMode) == B_OK) {
				DesktopSettings settings(fDesktop);
				settings.SetMouseMode(mouseMode);
			}
			break;
		}
		case AS_GET_MOUSE_MODE:
		{
			STRACE(("ServerApp %s: Get Focus Follows Mouse mode\n", Signature()));
			DesktopSettings settings(fDesktop);

			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<mode_mouse>(settings.MouseMode());
			fLink.Flush();
			break;
		}

		case AS_GET_UI_COLORS:
		{
			// Client application is asking for all the system colors at once
			// using a ColorSet object
			gGUIColorSet.Lock();
			
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<ColorSet>(gGUIColorSet);
			fLink.Flush();
			
			gGUIColorSet.Unlock();
			break;
		}
		case AS_SET_UI_COLORS:
		{
			// Client application is asking to set all the system colors at once
			// using a ColorSet object

			// Attached data:
			// 1) ColorSet new colors to use
			gGUIColorSet.Lock();
			link.Read<ColorSet>(&gGUIColorSet);
			gGUIColorSet.Unlock();

			fDesktop->BroadcastToAllApps(AS_UPDATE_COLORS);
			break;
		}
		case AS_GET_UI_COLOR:
		{
			STRACE(("ServerApp %s: Get UI color\n", Signature()));

			RGBColor color;
			int32 whichColor;
			link.Read<int32>(&whichColor);

			gGUIColorSet.Lock();
			color = gGUIColorSet.AttributeToColor(whichColor);
			gGUIColorSet.Unlock();

			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<rgb_color>(color.GetColor32());
			fLink.Flush();
			break;
		}

		/* font messages */

		case AS_UPDATED_CLIENT_FONTLIST:
		{
			STRACE(("ServerApp %s: Acknowledged update of client-side font list\n",
				Signature()));

			// received when the client-side global font list has been
			// refreshed
			gFontManager->Lock();
			gFontManager->FontsUpdated();
			gFontManager->Unlock();
			break;
		}
		case AS_QUERY_FONTS_CHANGED:
		{
			FTRACE(("ServerApp %s: AS_QUERY_FONTS_CHANGED\n",Signature()));
			
			// Attached Data:
			// 1) bool check flag
			bool checkonly;
			link.Read<bool>(&checkonly);
			
			gFontManager->Lock();
			bool needsUpdate = gFontManager->FontsNeedUpdated();
			gFontManager->Unlock();

			if (checkonly) {
				fLink.StartMessage(needsUpdate ? SERVER_TRUE : SERVER_FALSE);
				fLink.Flush();
			} else {
				fLink.StartMessage(SERVER_FALSE);
				fLink.Flush();
			}
			break;
		}
		case AS_GET_FAMILY_NAME:
		{
			FTRACE(("ServerApp %s: AS_GET_FAMILY_NAME\n", Signature()));
			// Attached Data:
			// 1) int32 the index of the font family to get

			// Returns:
			// 1) font_family - name of family
			// 2) uint32 - flags of font family (B_IS_FIXED || B_HAS_TUNED_FONT)
			int32 index;
			link.Read<int32>(&index);

			gFontManager->Lock();

			FontFamily *family = gFontManager->FamilyAt(index);
			if (family) {
				fLink.StartMessage(B_OK);
				fLink.AttachString(family->Name());
				fLink.Attach<uint32>(family->Flags());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_GET_STYLE_NAME:
		{
			FTRACE(("ServerApp %s: AS_GET_STYLE_NAME\n", Signature()));
			// Attached Data:
			// 1) font_family The name of the font family
			// 2) int32 index of the style to get

			// Returns:
			// 1) font_style - name of the style
			// 2) uint16 - appropriate face values
			// 3) uint32 - flags of font style (B_IS_FIXED || B_HAS_TUNED_FONT)

			font_family family;
			int32 styleIndex;
			link.ReadString(family, sizeof(font_family));
			link.Read<int32>(&styleIndex);

			gFontManager->Lock();
			FontStyle *fontStyle = gFontManager->GetStyleByIndex(family, styleIndex);
			if (fontStyle != NULL) {
				fLink.StartMessage(B_OK);
				fLink.AttachString(fontStyle->Name());
				fLink.Attach<uint16>(fontStyle->Face());
				fLink.Attach<uint32>(fontStyle->Flags());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_GET_FAMILY_AND_STYLE:
		{
			FTRACE(("ServerApp %s: AS_GET_FAMILY_AND_STYLE\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID

			// Returns:
			// 1) font_family The name of the font family
			// 2) font_style - name of the style
			uint16 familyID, styleID;
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);

			gFontManager->Lock();

			FontStyle *fontStyle = gFontManager->GetStyle(familyID, styleID);
			if (fontStyle != NULL) {
				fLink.StartMessage(B_OK);
				fLink.AttachString(fontStyle->Family()->Name());
				fLink.AttachString(fontStyle->Name());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			fLink.Flush();
			gFontManager->Unlock();
			break;
		}
		case AS_GET_FONT_DIRECTION:
		{
			FTRACE(("ServerApp %s: AS_GET_FONT_DIRECTION\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID

			// Returns:
			// 1) font_direction direction of font

			int32 familyID, styleID;
			link.Read<int32>(&familyID);
			link.Read<int32>(&styleID);

			gFontManager->Lock();

			FontStyle *fontStyle = gFontManager->GetStyle(familyID, styleID);
			if (fontStyle) {
				font_direction direction = fontStyle->Direction();

				fLink.StartMessage(B_OK);
				fLink.Attach<font_direction>(direction);
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_GET_FONT_FILE_FORMAT:
		{
			FTRACE(("ServerApp %s: AS_GET_FONT_FILE_FORMAT\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID

			// Returns:
			// 1) uint16 font_file_format of font

			int32 familyID, styleID;
			link.Read<int32>(&familyID);
			link.Read<int32>(&styleID);

			gFontManager->Lock();

			FontStyle *fontStyle = gFontManager->GetStyle(familyID, styleID);
			if (fontStyle) {
				fLink.StartMessage(B_OK);
				fLink.Attach<uint16>((uint16)fontStyle->FileFormat());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_GET_STRING_WIDTHS:
		{
			FTRACE(("ServerApp %s: AS_GET_STRING_WIDTHS\n", Signature()));
			// Attached Data:
			// 1) uint16 ID of family
			// 2) uint16 ID of style
			// 3) float point size of font
			// 4) uint8 spacing to use
			// 5) int32 numStrings 
			// 6) int32 string length to measure (numStrings times)
			// 7) string String to measure (numStrings times)

			// Returns:
			// 1) float - width of the string in pixels (numStrings times)

			uint16 family, style;
			float size;
			uint8 spacing;

			link.Read<uint16>(&family);
			link.Read<uint16>(&style);
			link.Read<float>(&size);
			link.Read<uint8>(&spacing);
			int32 numStrings;
			link.Read<int32>(&numStrings);

			float widthArray[numStrings];
			int32 lengthArray[numStrings];
			char *stringArray[numStrings];
			for (int32 i = 0; i < numStrings; i++) {
				// TODO: the length is actually encoded twice here
				//	It would be nicer to only send as much from the string as needed
				link.Read<int32>(&lengthArray[i]);
				link.ReadString(&stringArray[i]);
			}

			ServerFont font;

			if (font.SetFamilyAndStyle(family, style) == B_OK
				&& size > 0) {

				font.SetSize(size);
				font.SetSpacing(spacing);

				for (int32 i = 0; i < numStrings; i++) {
					if (!stringArray[i] || lengthArray[i] <= 0)
						widthArray[i] = 0.0;
					else {
						//widthArray[i] = fDesktop->GetDrawingEngine()->StringWidth(stringArray[i], lengthArray[i], font);
						// NOTE: The line below will return the exact same thing. However,
						// the line above uses the AGG rendering backend, for which glyph caching
						// actually works. It is about 20 times faster!
						// TODO: I've disabled the AGG version for now, as it produces a dead lock
						//	(font use), that I am currently too lazy to investigate...
						widthArray[i] = font.StringWidth(stringArray[i], lengthArray[i]);
					}
				}

				fLink.StartMessage(B_OK);
				fLink.Attach(widthArray, sizeof(widthArray));
			} else
				fLink.StartMessage(B_BAD_VALUE);

			fLink.Flush();

			for (int32 i = 0; i < numStrings; i++) {
				free(stringArray[i]);
			}
			break;
		}
		case AS_GET_FONT_BOUNDING_BOX:
		{
			FTRACE(("ServerApp %s: AS_GET_BOUNDING_BOX unimplemented\n",
				Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID

			// Returns:
			// 1) BRect - box holding entire font

			// ToDo: implement me!
			fLink.StartMessage(B_ERROR);
			fLink.Flush();
			break;
		}
		case AS_GET_TUNED_COUNT:
		{
			FTRACE(("ServerApp %s: AS_GET_TUNED_COUNT\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID

			// Returns:
			// 1) int32 - number of font strikes available
			uint16 familyID, styleID;
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);

			gFontManager->Lock();

			FontStyle *fontStyle = gFontManager->GetStyle(familyID, styleID);
			if (fontStyle != NULL) {
				fLink.StartMessage(B_OK);
				fLink.Attach<int32>(fontStyle->TunedCount());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_GET_TUNED_INFO:
		{
			FTRACE(("ServerApp %s: AS_GET_TUNED_INFO unimplmemented\n",
				Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) uint32 - index of the particular font strike

			// Returns:
			// 1) tuned_font_info - info on the strike specified
			// ToDo: implement me!
			fLink.StartMessage(B_ERROR);
			fLink.Flush();
			break;
		}
		case AS_QUERY_FONT_FIXED:
		{
			FTRACE(("ServerApp %s: AS_QUERY_FONT_FIXED unimplmemented\n",
				Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID

			// Returns:
			// 1) bool - font is/is not fixed
			uint16 familyID, styleID;
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);

			gFontManager->Lock();

			FontStyle *fontStyle = gFontManager->GetStyle(familyID, styleID);
			if (fontStyle != NULL) {
				fLink.StartMessage(B_OK);
				fLink.Attach<bool>(fontStyle->IsFixedWidth());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_SET_FAMILY_AND_STYLE:
		{
			FTRACE(("ServerApp %s: AS_SET_FAMILY_AND_STYLE\n",
				Signature()));
			// Attached Data:
			// 1) font_family - name of font family to use
			// 2) font_style - name of style in family
			// 3) family ID - only used if 1) is empty
			// 4) style ID - only used if 2) is empty
			// 5) face - the font's current face

			// Returns:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) uint16 - face

			font_family family;
			font_style style;
			uint16 familyID, styleID;
			uint16 face;
			if (link.ReadString(family, sizeof(font_family)) == B_OK
				&& link.ReadString(style, sizeof(font_style)) == B_OK
				&& link.Read<uint16>(&familyID) == B_OK
				&& link.Read<uint16>(&styleID) == B_OK
				&& link.Read<uint16>(&face) == B_OK) {
				// get the font and return IDs and face
				gFontManager->Lock();

				FontStyle *fontStyle = gFontManager->GetStyle(family, style,
					familyID, styleID, face);

				if (fontStyle != NULL) {
					fLink.StartMessage(B_OK);
					fLink.Attach<uint16>(fontStyle->Family()->ID());
					fLink.Attach<uint16>(fontStyle->ID());

					// we try to keep the font face close to what we got
					face = fontStyle->PreservedFace(face);

					fLink.Attach<uint16>(face);
				} else
					fLink.StartMessage(B_NAME_NOT_FOUND);

				gFontManager->Unlock();
			} else
				fLink.StartMessage(B_BAD_VALUE);

			fLink.Flush();
			break;
		}
		case AS_COUNT_FONT_FAMILIES:
		{
			FTRACE(("ServerApp %s: AS_COUNT_FONT_FAMILIES\n", Signature()));
			// Returns:
			// 1) int32 - # of font families

			gFontManager->Lock();

			fLink.StartMessage(B_OK);
			fLink.Attach<int32>(gFontManager->CountFamilies());
			fLink.Flush();

			gFontManager->Unlock();
			break;
		}
		case AS_COUNT_FONT_STYLES:
		{
			FTRACE(("ServerApp %s: AS_COUNT_FONT_STYLES\n", Signature()));
			// Attached Data:
			// 1) font_family - name of font family

			// Returns:
			// 1) int32 - # of font styles
			font_family familyName;
			link.ReadString(familyName, sizeof(font_family));

			gFontManager->Lock();

			FontFamily *family = gFontManager->GetFamily(familyName);
			if (family != NULL) {
				fLink.StartMessage(B_OK);
				fLink.Attach<int32>(family->CountStyles());
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_SET_SYSFONT_PLAIN:
		case AS_SET_SYSFONT_BOLD:
		case AS_SET_SYSFONT_FIXED:
		{
			FTRACE(("ServerApp %s: AS_SET_SYSFONT_PLAIN\n", Signature()));
			// Returns:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) float - size in points
			// 4) uint16 - face flags
			// 5) uint32 - font flags

			DesktopSettings settings(fDesktop);

			ServerFont font;
			switch (code) {
				case AS_SET_SYSFONT_PLAIN:
					settings.GetDefaultPlainFont(font);
					break;
				case AS_SET_SYSFONT_BOLD:
					settings.GetDefaultBoldFont(font);
					break;
				case AS_SET_SYSFONT_FIXED:
					settings.GetDefaultFixedFont(font);
					break;
			}

			fLink.StartMessage(B_OK);
			fLink.Attach<uint16>(font.FamilyID());
			fLink.Attach<uint16>(font.StyleID());
			fLink.Attach<float>(font.Size());
			fLink.Attach<uint16>(font.Face());
			fLink.Attach<uint32>(font.Flags());

			fLink.Flush();
			break;
		}
		case AS_GET_FONT_HEIGHT:
		{
			FTRACE(("ServerApp %s: AS_GET_FONT_HEIGHT\n", Signature()));
			// Attached Data:
			// 1) uint16 family ID
			// 2) uint16 style ID
			// 3) float size
			uint16 familyID, styleID;
			float size;
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);
			link.Read<float>(&size);

			gFontManager->Lock();

			FontStyle *fontStyle = gFontManager->GetStyle(familyID, styleID);
			if (fontStyle != NULL) {
				font_height height;
				fontStyle->GetHeight(size, height);

				fLink.StartMessage(B_OK);
				fLink.Attach<font_height>(height);
			} else
				fLink.StartMessage(B_BAD_VALUE);

			gFontManager->Unlock();
			fLink.Flush();
			break;
		}
		case AS_GET_GLYPH_SHAPES:
		{
			FTRACE(("ServerApp %s: AS_GET_GLYPH_SHAPES\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) float - point size
			// 4) float - shear
			// 5) float - rotation
			// 6) uint32 - flags
			// 7) int32 - numChars
			// 8) char - chars (numChars times)

			// Returns:
			// 1) BShape - glyph shape
			// numChars times

			uint16 familyID, styleID;
			uint32 flags;
			float size, shear, rotation;

			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);
			link.Read<float>(&size);
			link.Read<float>(&shear);
			link.Read<float>(&rotation);
			link.Read<uint32>(&flags);

			int32 numChars;
			link.Read<int32>(&numChars);

			char charArray[numChars];			
			link.Read(&charArray, numChars);

			ServerFont font;
			status_t status = font.SetFamilyAndStyle(familyID, styleID);
			if (status == B_OK) {
				font.SetSize(size);
				font.SetShear(shear);
				font.SetRotation(rotation);
				font.SetFlags(flags);

				BShape **shapes = font.GetGlyphShapes(charArray, numChars);
				if (shapes) {
					fLink.StartMessage(B_OK);
					for (int32 i = 0; i < numChars; i++) {
						fLink.AttachShape(*shapes[i]);
						delete shapes[i];
					}

					delete shapes;
				} else
					fLink.StartMessage(B_ERROR);
			} else
				fLink.StartMessage(status);

			fLink.Flush();
			break;
		}
		case AS_GET_HAS_GLYPHS:
		{
			FTRACE(("ServerApp %s: AS_GET_HAS_GLYPHS\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) int32 - numChars
                        // 4) int32 - numBytes
                        // 5) char - the char buffer with size numBytes

			uint16 familyID, styleID;
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);

			int32 numChars;
			link.Read<int32>(&numChars);

			uint32 numBytes;
			link.Read<uint32>(&numBytes);
			char* charArray = new char[numBytes];
			link.Read(charArray, numBytes);
			
			ServerFont font;
			status_t status = font.SetFamilyAndStyle(familyID, styleID);
			if (status == B_OK) {
				bool hasArray[numChars];
				font.GetHasGlyphs(charArray, numChars, hasArray);
				fLink.StartMessage(B_OK);
				fLink.Attach(hasArray, sizeof(hasArray));
			} else
				fLink.StartMessage(status);

			delete[] charArray;

			fLink.Flush();
			break;
		}
		case AS_GET_EDGES:
		{
			FTRACE(("ServerApp %s: AS_GET_EDGES\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) int32 - numChars
			// 4) int32 - numBytes
			// 5) char - the char buffer with size numBytes
			
			uint16 familyID, styleID;
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);

			int32 numChars;
			link.Read<int32>(&numChars);

			uint32 numBytes;
			link.Read<uint32>(&numBytes);
			char* charArray = new char[numBytes];
			link.Read(charArray, numBytes);

			ServerFont font;
			status_t status = font.SetFamilyAndStyle(familyID, styleID);
			if (status == B_OK) {
				edge_info edgeArray[numChars];
				font.GetEdges(charArray, numChars, edgeArray);
				fLink.StartMessage(B_OK);
				fLink.Attach(edgeArray, sizeof(edgeArray));
			} else
				fLink.StartMessage(status);

			delete[] charArray;

			fLink.Flush();
			break;
		}
		case AS_GET_ESCAPEMENTS:
		{
			FTRACE(("ServerApp %s: AS_GET_ESCAPEMENTS\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) float - point size
			// 4) float - rotation
			// 5) uint32 - flags
			// 6) int32 - numChars
			// 7) char - char     -\       both
			// 8) BPoint - offset -/ (numChars times)

			// Returns:
			// 1) BPoint - escapement
			// numChars times

			uint16 familyID, styleID;
			uint32 flags;
			float size, rotation;

			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);
			link.Read<float>(&size);
			link.Read<float>(&rotation);
			link.Read<uint32>(&flags);

			int32 numChars;
			link.Read<int32>(&numChars);

			char charArray[numChars];			
			BPoint offsetArray[numChars];
			for (int32 i = 0; i < numChars; i++) {
				link.Read<char>(&charArray[i]);
				link.Read<BPoint>(&offsetArray[i]);
			}

			ServerFont font;
			status_t status = font.SetFamilyAndStyle(familyID, styleID);
			if (status == B_OK) {
				font.SetSize(size);
				font.SetRotation(rotation);
				font.SetFlags(flags);

				BPoint *escapements = font.GetEscapements(charArray, numChars, offsetArray);
				if (escapements) {
					fLink.StartMessage(B_OK);
					for (int32 i = 0; i < numChars; i++) {
						fLink.Attach<BPoint>(escapements[i]);
					}

					delete escapements;
				} else
					fLink.StartMessage(B_ERROR);
			} else
				fLink.StartMessage(status);

			fLink.Flush();
			break;
		}
		case AS_GET_ESCAPEMENTS_AS_FLOATS:
		{
			FTRACE(("ServerApp %s: AS_GET_ESCAPEMENTS_AS_FLOATS\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) float - point size
			// 4) float - rotation
			// 5) uint32 - flags

			// 6) float - additional "nonspace" delta
			// 7) float - additional "space" delta

			// 8) int32 - numChars
			// 9) int32 - numBytes
			// 10) char - the char buffer with size numBytes

			// Returns:
			// 1) float - escapement buffer with numChar entries
			
			uint16 familyID, styleID;
			uint32 flags;
			float size, rotation;
			
			link.Read<uint16>(&familyID);
			link.Read<uint16>(&styleID);
			link.Read<float>(&size);
			link.Read<float>(&rotation);
			link.Read<uint32>(&flags);

			escapement_delta delta;
			link.Read<float>(&delta.nonspace);
			link.Read<float>(&delta.space);
			
			int32 numChars;
			link.Read<int32>(&numChars);

			uint32 numBytes;
			link.Read<uint32>(&numBytes);
			char* charArray = new char[numBytes];
			link.Read(charArray, numBytes);

			float* escapements = new float[numChars];

			// figure out escapements

			ServerFont font;
			status_t status = font.SetFamilyAndStyle(familyID, styleID);
			if (status == B_OK) {
				font.SetSize(size);
				font.SetRotation(rotation);
				font.SetFlags(flags);

				if (font.GetEscapements(charArray, numChars, numBytes, escapements, delta)) {
					fLink.StartMessage(B_OK);
					fLink.Attach(escapements, numChars * sizeof(float));
				} else
					status = B_ERROR;
			}

			delete[] charArray;
			delete[] escapements;

			if (status != B_OK)
				fLink.StartMessage(status);

			fLink.Flush();
			break;
		}
		case AS_GET_BOUNDINGBOXES_CHARS:
		{
			FTRACE(("ServerApp %s: AS_GET_BOUNDINGBOXES_CHARS\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) float - point size
			// 4) float - rotation
			// 5) float - shear
			// 6) uint8 - spacing
			// 7) uint32 - flags
			
			// 8) font_metric_mode - mode
			// 9) bool - string escapement

			// 10) escapement_delta - additional delta

			// 11) int32 - numChars
			// 12) int32 - numBytes
			// 13) char - the char buffer with size numBytes

			// Returns:
			// 1) BRect - rects with numChar entries
			
			uint16 famid, styid;
			uint32 flags;
			float ptsize, rotation, shear;
			uint8 spacing;
			font_metric_mode mode;
			bool string_escapement;
			
			link.Read<uint16>(&famid);
			link.Read<uint16>(&styid);
			link.Read<float>(&ptsize);
			link.Read<float>(&rotation);
			link.Read<float>(&shear);
			link.Read<uint8>(&spacing);
			link.Read<uint32>(&flags);
			link.Read<font_metric_mode>(&mode);
			link.Read<bool>(&string_escapement);

			escapement_delta delta;
			link.Read<escapement_delta>(&delta);
			
			int32 numChars;
			link.Read<int32>(&numChars);

			uint32 numBytes;
			link.Read<uint32>(&numBytes);

			char charArray[numBytes];
			link.Read(charArray, numBytes);

			BRect rectArray[numChars];
			// figure out escapements

			ServerFont font;
			bool success = false;
			if (font.SetFamilyAndStyle(famid, styid) == B_OK) {
				font.SetSize(ptsize);
				font.SetRotation(rotation);
				font.SetShear(shear);
				font.SetSpacing(spacing);
				font.SetFlags(flags);

				// TODO implement for real
				if (font.GetBoundingBoxesAsString(charArray, numChars, rectArray, string_escapement, mode, delta)) {
					fLink.StartMessage(SERVER_TRUE);
					fLink.Attach(rectArray, sizeof(rectArray));
					success = true;
				}
			}

			if (!success)
				fLink.StartMessage(SERVER_FALSE);

			fLink.Flush();
			break;
		}
		case AS_GET_BOUNDINGBOXES_STRINGS:
		{
			FTRACE(("ServerApp %s: AS_GET_BOUNDINGBOXES_STRINGS\n", Signature()));
			// Attached Data:
			// 1) uint16 - family ID
			// 2) uint16 - style ID
			// 3) float - point size
			// 4) float - rotation
			// 5) float - shear
			// 6) uint8 - spacing
			// 7) uint32 - flags
			
			// 8) font_metric_mode - mode
			// 9) int32 numStrings

			// 10) escapement_delta - additional delta (numStrings times)
			// 11) int32 string length to measure (numStrings times)
			// 12) string - string (numStrings times)

			// Returns:
			// 1) BRect - rects with numStrings entries
			
			uint16 famid, styid;
			uint32 flags;
			float ptsize, rotation, shear;
			uint8 spacing;
			font_metric_mode mode;
			
			link.Read<uint16>(&famid);
			link.Read<uint16>(&styid);
			link.Read<float>(&ptsize);
			link.Read<float>(&rotation);
			link.Read<float>(&shear);
			link.Read<uint8>(&spacing);
			link.Read<uint32>(&flags);
			link.Read<font_metric_mode>(&mode);

			int32 numStrings;
			link.Read<int32>(&numStrings);
			
			escapement_delta deltaArray[numStrings];
			char *stringArray[numStrings];
			int32 lengthArray[numStrings];
			for(int32 i=0; i<numStrings; i++) {
				link.Read<int32>(&lengthArray[i]);
				link.Read<escapement_delta>(&deltaArray[i]);
				link.ReadString(&stringArray[i]);
			}

			BRect rectArray[numStrings];

			ServerFont font;
			bool success = false;
			if (font.SetFamilyAndStyle(famid, styid) == B_OK) {
				font.SetSize(ptsize);
				font.SetRotation(rotation);
				font.SetShear(shear);
				font.SetSpacing(spacing);
				font.SetFlags(flags);

				// TODO implement for real
				if (font.GetBoundingBoxesForStrings(stringArray, lengthArray, numStrings, rectArray, mode, deltaArray)) {
					fLink.StartMessage(SERVER_TRUE);
					fLink.Attach(rectArray, sizeof(rectArray));
					success = true;
				}
			}

			for (int32 i=0; i<numStrings; i++)
				free(stringArray[i]);

			if (!success)
				fLink.StartMessage(SERVER_FALSE);

			fLink.Flush();
			break;
		}

		case AS_SCREEN_GET_MODE:
		{
			STRACE(("ServerApp %s: AS_SCREEN_GET_MODE\n", Signature()));
			// Attached data
			// 1) screen_id screen
			// 2) uint32 workspace index
			screen_id id;
			link.Read<screen_id>(&id);
			uint32 workspace;
			link.Read<uint32>(&workspace);

			// TODO: the display_mode can be different between
			// the various screens.
			// We have the screen_id and the workspace number, with these
			// we need to find the corresponding "driver", and call getmode on it
			display_mode mode;
			fDesktop->ScreenAt(0)->GetMode(&mode);
			// actually this isn't still enough as different workspaces can
			// have different display_modes

			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<display_mode>(mode);
			fLink.Attach<status_t>(B_OK);
			fLink.Flush();
			break;
		}
		case AS_SCREEN_SET_MODE:
		{
			STRACE(("ServerApp %s: AS_SCREEN_SET_MODE\n", Signature()));
			// Attached data
			// 1) screen_id
			// 2) workspace index
			// 3) display_mode to set
			// 4) 'makedefault' boolean
			// TODO: See above: workspaces support, etc.

			screen_id id;
			link.Read<screen_id>(&id);

			uint32 workspace;
			link.Read<uint32>(&workspace);

			display_mode mode;
			link.Read<display_mode>(&mode);

			bool makedefault = false;
			link.Read<bool>(&makedefault);

// TODO: lock RootLayer, set mode and tell it to update it's frame and all clipping
// optionally put this into a message and let the root layer thread handle it.
//			status_t ret = fDesktop->ScreenAt(0)->SetMode(mode);
			status_t ret = B_ERROR;

			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<status_t>(ret);
			fLink.Flush();
			break;
		}

		case AS_PROPOSE_MODE:
		{
			STRACE(("ServerApp %s: AS_PROPOSE_MODE\n", Signature()));
			screen_id id;
			link.Read<screen_id>(&id);

			display_mode target, low, high;
			link.Read<display_mode>(&target);
			link.Read<display_mode>(&low);
			link.Read<display_mode>(&high);
			status_t status = fDesktop->GetHWInterface()->ProposeMode(&target, &low, &high);
			// TODO: We always return SERVER_TRUE and put the real
			// error code in the message. FIX this.
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<display_mode>(target);
			fLink.Attach<status_t>(status);
			fLink.Flush();
						
			break;
		}
		
		case AS_GET_MODE_LIST:
		{
			screen_id id;
			link.Read<screen_id>(&id);
			// TODO: use this screen id

			display_mode* modeList;
			uint32 count;
			if (fDesktop->GetHWInterface()->GetModeList(&modeList, &count) == B_OK) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<uint32>(count);
				fLink.Attach(modeList, sizeof(display_mode) * count);

				delete[] modeList;
			} else
				fLink.StartMessage(SERVER_FALSE);

			fLink.Flush();
			break;
		}

		case AS_SCREEN_GET_COLORMAP:
		{
			STRACE(("ServerApp %s: AS_SCREEN_GET_COLORMAP\n", Signature()));

			screen_id id;
			link.Read<screen_id>(&id);
			
			const color_map *colorMap = SystemColorMap();
			if (colorMap != NULL) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<color_map>(*colorMap);
			} else 
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			break;
		}

		case AS_GET_DESKTOP_COLOR:
		{
			STRACE(("ServerApp %s: get desktop color\n", Signature()));

			uint32 workspaceIndex = 0;
			link.Read<uint32>(&workspaceIndex);

			// ToDo: for some reason, we currently get "1" as no. of workspace
			workspaceIndex = 0;

			// ToDo: locking is probably wrong - why the hell is there no (safe)
			//		way to get to the workspace object directly?
			RootLayer *root = fDesktop->ActiveRootLayer();
			root->Lock();

			Workspace *workspace = root->WorkspaceAt(workspaceIndex);
			if (workspace != NULL) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<rgb_color>(workspace->BGColor().GetColor32());
			} else
				fLink.StartMessage(SERVER_FALSE);

			fLink.Flush();
			root->Unlock();
			break;
		}
		
		case AS_GET_ACCELERANT_INFO:
		{
			STRACE(("ServerApp %s: get accelerant info\n", Signature()));
			
			// We aren't using the screen_id for now...
			screen_id id;
			link.Read<screen_id>(&id);
			
			accelerant_device_info accelerantInfo;
			// TODO: I wonder if there should be a "desktop" lock...
			if (fDesktop->GetHWInterface()->GetDeviceInfo(&accelerantInfo) == B_OK) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<accelerant_device_info>(accelerantInfo);
			} else
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			break;
		}
		
		case AS_GET_RETRACE_SEMAPHORE:
		{
			STRACE(("ServerApp %s: get retrace semaphore\n", Signature()));
			
			// We aren't using the screen_id for now...
			screen_id id;
			link.Read<screen_id>(&id);
			
			sem_id semaphore = fDesktop->GetHWInterface()->RetraceSemaphore();
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<sem_id>(semaphore);
			fLink.Flush();
			break;
		}
		
		case AS_GET_TIMING_CONSTRAINTS:
		{
			STRACE(("ServerApp %s: get timing constraints\n", Signature()));
			// We aren't using the screen_id for now...
			screen_id id;
			link.Read<screen_id>(&id);
			
			display_timing_constraints constraints;
			if (fDesktop->GetHWInterface()->GetTimingConstraints(&constraints) == B_OK) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<display_timing_constraints>(constraints);
			} else
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			break;
		}
		
		case AS_GET_PIXEL_CLOCK_LIMITS:
		{
			STRACE(("ServerApp %s: get pixel clock limits\n", Signature()));
			// We aren't using the screen_id for now...
			screen_id id;
			link.Read<screen_id>(&id);
			display_mode mode;
			link.Read<display_mode>(&mode);
			
			uint32 low, high;
			if (fDesktop->GetHWInterface()->GetPixelClockLimits(&mode, &low, &high) == B_OK) {
				fLink.StartMessage(SERVER_TRUE);
				fLink.Attach<uint32>(low);
				fLink.Attach<uint32>(high);
			} else
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			break;
		}
		
		case AS_SET_DPMS:
		{
			STRACE(("ServerApp %s: AS_SET_DPMS\n", Signature()));
			screen_id id;
			link.Read<screen_id>(&id);
			
			uint32 mode;
			link.Read<uint32>(&mode);
			
			if (fDesktop->GetHWInterface()->SetDPMSMode(mode) == B_OK)
				fLink.StartMessage(SERVER_TRUE);
			else
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			break;
		}
		
		case AS_GET_DPMS_STATE:
		{
			STRACE(("ServerApp %s: AS_GET_DPMS_STATE\n", Signature()));
			
			screen_id id;
			link.Read<screen_id>(&id);
			
			uint32 state = fDesktop->GetHWInterface()->DPMSMode();
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<uint32>(state);
			fLink.Flush();
			break;
		}
				
		case AS_GET_DPMS_CAPABILITIES:
		{
			STRACE(("ServerApp %s: AS_GET_DPMS_CAPABILITIES\n", Signature()));
			screen_id id;
			link.Read<screen_id>(&id);
			
			uint32 capabilities = fDesktop->GetHWInterface()->DPMSCapabilities();
			fLink.StartMessage(SERVER_TRUE);
			fLink.Attach<uint32>(capabilities);
			fLink.Flush();
			break;
		}
		
		case AS_READ_BITMAP:
		{
			STRACE(("ServerApp %s: AS_READ_BITMAP\n", Signature()));
			int32 bitmapToken;
			link.Read<int32>(&bitmapToken);
			
			bool drawCursor = true;
			link.Read<bool>(&drawCursor);
			
			BRect bounds;
			link.Read<BRect>(&bounds);
			
			ServerBitmap *bitmap = FindBitmap(bitmapToken);
			if (bitmap != NULL) {
				fLink.StartMessage(SERVER_TRUE);
				// TODO: Implement for real
			} else
				fLink.StartMessage(SERVER_FALSE);
			
			fLink.Flush();
			
			break;
		}
			
		default:
			printf("ServerApp %s received unhandled message code offset %s\n",
				Signature(), MsgCodeToBString(code).String());

			if (link.NeedsReply()) {
				// the client is now blocking and waiting for a reply!
				fLink.StartMessage(SERVER_FALSE);
				fLink.Flush();
			} else
				puts("message doesn't need a reply!");
			break;
	}
}


void
ServerApp::RemoveWindow(ServerWindow* window)
{
	BAutolock locker(fWindowListLock);

	fWindowList.RemoveItem(window);
}


int32
ServerApp::CountBitmaps() const
{
	return fBitmapList.CountItems();
}


/*!
	\brief Looks up a ServerApp's ServerBitmap in its list
	\param token ID token of the bitmap to find
	\return The bitmap having that ID or NULL if not found
*/
ServerBitmap*
ServerApp::FindBitmap(int32 token) const
{
	int32 count = fBitmapList.CountItems();
	for (int32 i = 0; i < count; i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fBitmapList.ItemAt(i);
		if (bitmap && bitmap->Token() == token)
			return bitmap;
	}

	return NULL;
}


int32
ServerApp::CountPictures() const
{
	return fPictureList.CountItems();
}


ServerPicture *
ServerApp::FindPicture(int32 token) const
{
	for (int32 i = 0; i < fPictureList.CountItems(); i++) {
		ServerPicture *picture = static_cast<ServerPicture *>(fPictureList.ItemAt(i));
		if (picture && picture->GetToken() == token)
			return picture;
	}
	
	return NULL;
}

	
team_id
ServerApp::ClientTeam() const
{
	return fClientTeam;
}

