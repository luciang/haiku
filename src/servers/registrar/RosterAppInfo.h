//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		RosterAppInfo.h
//	Author:			Ingo Weinhold (bonefish@users.sf.net)
//	Description:	An extended app_info.
//------------------------------------------------------------------------------

#ifndef ROSTER_APP_INFO_H
#define ROSTER_APP_INFO_H

#include <Roster.h>

enum application_state {
	APP_STATE_UNREGISTERED,
	APP_STATE_PRE_REGISTERED,
	APP_STATE_REGISTERED,
};


struct RosterAppInfo : app_info {
	application_state	state;
	uint32				token;
		// token is meaningful only if state is APP_STATE_PRE_REGISTERED and
		// team is -1.
	bigtime_t			registration_time;	// time of first addition

	RosterAppInfo();
	void Init(thread_id thread, team_id team, port_id port, uint32 flags,
			  const entry_ref *ref, const char *signature);
};

#endif	// ROSTER_APP_INFO_H
