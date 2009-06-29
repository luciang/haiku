/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <ObjectList.h>
#include <Referenceable.h>

#include "Types.h"


enum user_breakpoint_state {
	USER_BREAKPOINT_NONE,
	USER_BREAKPOINT_ENABLED,
	USER_BREAKPOINT_DISABLED
};


class Image;


class BreakpointClient {
public:
	virtual						~BreakpointClient();
};


class Breakpoint : public Referenceable {
public:
								Breakpoint(Image* image, target_addr_t address);
								~Breakpoint();

			Image*				GetImage() const	{ return fImage; }
			target_addr_t		Address() const		{ return fAddress; }

			user_breakpoint_state UserState() const	{ return fUserState; }
			void				SetUserState(user_breakpoint_state state);

			bool				IsInstalled() const	{ return fInstalled; }
			void				SetInstalled(bool installed);

			bool				ShouldBeInstalled() const;
			bool				IsUnused() const;

			bool				AddClient(BreakpointClient* client);
			void				RemoveClient(BreakpointClient* client);

	static	int					CompareBreakpoints(const Breakpoint* a,
									const Breakpoint* b);
	static	int					CompareAddressBreakpoint(
									const target_addr_t* address,
									const Breakpoint* breakpoint);

private:
			typedef BObjectList<BreakpointClient> ClientList;

private:
			target_addr_t		fAddress;
			Image*				fImage;
			ClientList			fClients;
			user_breakpoint_state fUserState;
			bool				fInstalled;
};


#endif	// BREAKPOINT_H
