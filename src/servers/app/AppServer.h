/*
 * Copyright (c) 2001-2005, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Author: DarkWyrm <bpmagic@columbus.rr.com>
 */
#ifndef	APP_SERVER_H
#define	APP_SERVER_H


#include <OS.h>
#include <Locker.h>
#include <List.h>
#include <Application.h>
#include <Window.h>
#include <String.h>
#include <ObjectList.h>
#include <TokenSpace.h>

#include "ServerConfig.h"
#include "MessageLooper.h"

class ServerApp;
class BitmapManager;
class Desktop;

using BPrivate::BTokenSpace;

namespace BPrivate {
	class PortLink;
};


class AppServer : public MessageLooper  {
	public:
		AppServer();
		virtual ~AppServer();

		void			RunLooper();
		virtual port_id	MessagePort() const { return fMessagePort; }

	private:
		virtual void	_DispatchMessage(int32 code, BPrivate::LinkReceiver& link);

		Desktop*		_CreateDesktop(uid_t userID, const char* targetScreen);
		Desktop*		_FindDesktop(uid_t userID, const char* targetScreen);

		void			_LaunchInputServer();

	private:
		port_id			fMessagePort;

		BObjectList<Desktop> fDesktops;
		BLocker			fDesktopLock;
};

extern BitmapManager *gBitmapManager;
extern port_id gAppServerPort;
extern uint32 gAppServerSIMDFlags;

// Defines for SIMD support. Early implementation, subject to change
#define APPSERVER_SIMD_MMX	(1 << 0)
#define APPSERVER_SIMD_SSE	(1 << 1)

#endif	/* APP_SERVER_H */
