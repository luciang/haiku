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
 */
#ifndef _SERVERAPP_H_
#define _SERVERAPP_H_


#include "MessageLooper.h"
#include "SubWindowList.h"
#include "BGet++.h"

#include <String.h>


class AreaPool;
class BMessage;
class BList;
class DisplayDriver;
class ServerPicture;
class ServerCursor;
class ServerBitmap;
class Desktop;

namespace BPrivate {
	class PortLink;
};

class ServerApp : public MessageLooper {
	public:
		ServerApp(Desktop* desktop, port_id clientAppPort,
			port_id clientLooperPort, team_id clientTeamID,
			int32 handlerID, const char* signature);
		virtual ~ServerApp();

		status_t InitCheck();
		void Quit(sem_id shutdownSemaphore = -1);

		virtual bool Run();
		virtual port_id MessagePort() const { return fMessagePort; }

		/*!
			\brief Determines whether the application is the active one
			\return true if active, false if not.
		*/
		bool IsActive(void) const { return fIsActive; }
		void Activate(bool value);

		void SendMessageToClient(const BMessage* msg) const;

		void SetAppCursor(void);

		team_id	ClientTeam() const;
		const char *Signature() const { return fSignature.String(); }

		void RemoveWindow(ServerWindow* window);

		int32 CountBitmaps() const;
		ServerBitmap *FindBitmap(int32 token) const;

		int32 CountPictures() const;
		ServerPicture *FindPicture(int32 token) const;

		AreaPool *AppAreaPool() { return &fSharedMem; }

		Desktop* GetDesktop() const { return fDesktop; }

		// ToDo: public?
		SubWindowList fAppSubWindowList;

	private:
		virtual void _DispatchMessage(int32 code, BPrivate::LinkReceiver &link);
		virtual void _MessageLooper();
		virtual void _GetLooperName(char* name, size_t size);

		port_id	fMessagePort;
		port_id	fClientReplyPort;	
			// our BApplication's event port

		port_id	fClientLooperPort;
		int32 fClientToken;
			// To send a BMessage to the client (port + token)

		Desktop* fDesktop;
		BString fSignature;
		team_id fClientTeam;

		BLocker fWindowListLock;
		BList fWindowList;

		// TODO:
		// - Are really Bitmaps and Pictures stored per application and not globally ?
		// - As we reference these stuff by token, what about putting them in hash tables ?
		BList fBitmapList;
		BList fPictureList;

		ServerCursor *fAppCursor;
		bool fCursorHidden;

		bool fIsActive;

		AreaPool fSharedMem;
};

#endif	// _SERVERAPP_H_
