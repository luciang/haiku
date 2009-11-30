/*
 * Copyright 2001-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold (bonefish@users.sf.net)
 */

//!	Global library initialization/termination routines.


#include <stdio.h>
#include <stdlib.h>

#include <MessagePrivate.h>
#include <RosterPrivate.h>


// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf


static void
initialize_forked_child()
{
	DBG(OUT("initialize_forked_child()\n"));

	BMessage::Private::StaticReInitForkedChild();

	DBG(OUT("initialize_forked_child() done\n"));
}


extern "C" void
initialize_before()
{
	DBG(OUT("initialize_before()\n"));

	BMessage::Private::StaticInit();
	BRoster::Private::InitBeRoster();

	atfork(initialize_forked_child);

	DBG(OUT("initialize_before() done\n"));
}


extern "C" void
initialize_after()
{
	DBG(OUT("initialize_after()\n"));

	DBG(OUT("initialize_after() done\n"));
}


extern "C" void
terminate_after()
{
	DBG(OUT("terminate_after()\n"));

	BRoster::Private::DeleteBeRoster();
	BMessage::Private::StaticCleanup();
	BMessage::Private::StaticCacheCleanup();

	DBG(OUT("terminate_after() done\n"));
}

