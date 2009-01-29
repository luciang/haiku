/*
 * Copyright 2009, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexandre Deckner, alex@zappotek.com
 */

#include <Application.h>
#include <Roster.h>


int 
main(int argc, char **argv)
{
	BApplication app("application/x-vnd.Haiku-TrackerPreferences");
	
	// launch Tracker if it's not running
	be_roster->Launch("application/x-vnd.Be-TRAK");
	
	BMessage message;	
	message.what = B_EXECUTE_PROPERTY;	
	message.AddSpecifier("Preferences");
	
	BMessenger("application/x-vnd.Be-TRAK").SendMessage(&message);
	
	return 0;
}
