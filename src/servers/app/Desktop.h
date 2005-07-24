/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Adrian Oanca <adioanca@cotty.iren.ro>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef _DESKTOP_H_
#define _DESKTOP_H_


#include "ScreenManager.h"
#include "ServerScreen.h"
#include "VirtualScreen.h"
#include "DesktopSettings.h"
#include "MessageLooper.h"

#include <InterfaceDefs.h>
#include <List.h>
#include <Locker.h>
#include <Menu.h>
#include <Autolock.h>


class BMessage;

class DisplayDriver;
class HWInterface;
class Layer;
class RootLayer;
class WinBorder;

namespace BPrivate {
	class LinkSender;
};


class Desktop : public MessageLooper, public ScreenOwner {
 public:
	// startup methods
								Desktop();
	virtual						~Desktop();

			void				Init();

	// Methods for multiple monitors.
	inline	Screen*				ScreenAt(int32 index) const
									{ return fActiveScreen; }
	inline	Screen*				ActiveScreen() const
									{ return fActiveScreen; }
	inline	RootLayer*			ActiveRootLayer() const { return fRootLayer; }

	virtual void				ScreenRemoved(Screen* screen) {}
	virtual void				ScreenAdded(Screen* screen) {}
	virtual bool				ReleaseScreen(Screen* screen) { return false; }

	const	::VirtualScreen&	VirtualScreen() const { return fVirtualScreen; }
	inline	DisplayDriver*		GetDisplayDriver() const
									{ return fVirtualScreen.DisplayDriver(); }
	inline	HWInterface*		GetHWInterface() const
									{ return fVirtualScreen.HWInterface(); }

	// Methods for layer(WinBorder) manipulation.
			void				AddWinBorder(WinBorder *winBorder);
			void				RemoveWinBorder(WinBorder *winBorder);
			void				SetWinBorderFeel(WinBorder *winBorder,
												 uint32 feel);
			void				AddWinBorderToSubset(WinBorder *winBorder,
													 WinBorder *toWinBorder);
			void				RemoveWinBorderFromSubset(WinBorder *winBorder,
														  WinBorder *fromWinBorder);

			WinBorder*			FindWinBorderByClientToken(int32 token, team_id teamID);
			//WinBorder*		FindWinBorderByServerToken(int32 token);

	// get list of registed windows
			const BList&		WindowList() const
								{
									if (!IsLocked())
										debugger("You must lock before getting registered windows list\n");
									return fWinBorderList;
								}

			void				WriteWindowList(team_id team, BPrivate::LinkSender& sender);
			void				WriteWindowInfo(int32 serverToken, BPrivate::LinkSender& sender);

 private:
	virtual void				_GetLooperName(char* name, size_t size);
	virtual port_id				_MessagePort() const { return fMessagePort; }

 private:
			friend class DesktopSettings;

			::VirtualScreen		fVirtualScreen;
			DesktopSettings::Private* fSettings;
			port_id				fMessagePort;
			BList				fWinBorderList;

			RootLayer*			fRootLayer;
			Screen*				fActiveScreen;
};

extern Desktop *gDesktop;

#endif	// _DESKTOP_H_
