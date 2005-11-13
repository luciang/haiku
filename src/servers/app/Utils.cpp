/*
 * Copyright 2001-2005, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 */

/**	Miscellaneous utility functions */

#include "Utils.h"

#include <Entry.h>
#include <GraphicsDefs.h>
#include <String.h>

#include <stdio.h>


/*!
	\brief Send a BMessage to a Looper target
	\param port Receiver port of the targeted Looper
	\param BMessage to send
	
	This SendMessage takes ownership of the message sent, so deleting them after this 
	call is unnecessary. Passing an invalid port will have unpredictable results.
*/
void
SendMessage(port_id port, BMessage *message, int32 target)
{
	if (!message)
		return;

#ifndef USING_MESSAGE4
	if (target == -1)
		_set_message_target_(message, target, true);
	else
		_set_message_target_(message, target, false);
#else
	BMessage::Private(message).SetTarget(target, target == -1);
#endif

	ssize_t size = message->FlattenedSize();
	char *buffer = new char[size];

	if (message->Flatten(buffer, size)==B_OK)
		write_port(port, message->what, buffer, size);

	delete [] buffer;
	delete message;
}


#ifndef USING_MESSAGE4
/*
	Below are friend functions for BMessage which currently are not in the Message.cpp 
	that we need to send messages to BLoopers and such. Placed here to allow compilation.
*/
void
_set_message_target_(BMessage *msg, int32 target, bool preferred)
{
	if (preferred) {
		msg->fTarget = -1;
		msg->fPreferred = true;
	} else {
		msg->fTarget = target;
		msg->fPreferred = false;
	}
}


int32
_get_message_target_(BMessage *msg)
{
	if (msg->fPreferred)
		return -1;

	return msg->fTarget;
}


bool
_use_preferred_target_(BMessage *msg)
{
	return msg->fPreferred;
}
#endif


const char *
MsgCodeToString(int32 code)
{
	// Used to translate BMessage message codes back to a character
	// format
	char string [10];
	sprintf(string, "'%c%c%c%c'", (char)((code & 0xFF000000) >>  24),
		(char)((code & 0x00FF0000) >>  16),
		(char)((code & 0x0000FF00) >>  8),
		(char)((code & 0x000000FF)) );
	return string;
}


BString
MsgCodeToBString(int32 code)
{
	// Used to translate BMessage message codes back to a character
	// format
	char buffer[10];
	sprintf(buffer, "'%c%c%c%c'", (char)((code & 0xFF000000) >>  24),
		(char)((code & 0x00FF0000) >>  16),
		(char)((code & 0x0000FF00) >>  8),
		(char)((code & 0x000000FF)) );

	BString string(buffer);
	return string;
}


status_t
ConvertModeToDisplayMode(uint32 mode, display_mode *dmode)
{
	if (!mode)
		return B_BAD_VALUE;

	switch (mode) {
		case B_8_BIT_640x400:
		{
			dmode->virtual_width=640;
			dmode->virtual_height=400;
			dmode->space=B_CMAP8;
			break;
		}
		case B_8_BIT_640x480:
		{
			dmode->virtual_width=640;
			dmode->virtual_height=480;
			dmode->space=B_CMAP8;
			break;
		}
		case B_15_BIT_640x480:
		{
			dmode->virtual_width=640;
			dmode->virtual_height=480;
			dmode->space=B_RGB15;
			break;
		}
		case B_16_BIT_640x480:
		{
			dmode->virtual_width=640;
			dmode->virtual_height=480;
			dmode->space=B_RGB16;
			break;
		}
		case B_32_BIT_640x480:
		{
			dmode->virtual_width=640;
			dmode->virtual_height=480;
			dmode->space=B_RGB32;
			break;
		}
		case B_8_BIT_800x600:
		{
			dmode->virtual_width=800;
			dmode->virtual_height=600;
			dmode->space=B_CMAP8;
			break;
		}
		case B_15_BIT_800x600:
		{
			dmode->virtual_width=800;
			dmode->virtual_height=600;
			dmode->space=B_RGB15;
			break;
		}
		case B_16_BIT_800x600:
		{
			dmode->virtual_width=800;
			dmode->virtual_height=600;
			dmode->space=B_RGB16;
			break;
		}
		case B_32_BIT_800x600:
		{
			dmode->virtual_width=800;
			dmode->virtual_height=600;
			dmode->space=B_RGB32;
			break;
		}
		case B_8_BIT_1024x768:
		{
			dmode->virtual_width=1024;
			dmode->virtual_height=768;
			dmode->space=B_CMAP8;
			break;
		}
		case B_15_BIT_1024x768:
		{
			dmode->virtual_width=1024;
			dmode->virtual_height=768;
			dmode->space=B_RGB15;
			break;
		}
		case B_16_BIT_1024x768:
		{
			dmode->virtual_width=1024;
			dmode->virtual_height=768;
			dmode->space=B_RGB16;
			break;
		}
		case B_32_BIT_1024x768:
		{
			dmode->virtual_width=1024;
			dmode->virtual_height=768;
			dmode->space=B_RGB32;
			break;
		}
		case B_8_BIT_1152x900:
		{
			dmode->virtual_width=1152;
			dmode->virtual_height=900;
			dmode->space=B_CMAP8;
			break;
		}
		case B_15_BIT_1152x900:
		{
			dmode->virtual_width=1152;
			dmode->virtual_height=900;
			dmode->space=B_RGB15;
			break;
		}
		case B_16_BIT_1152x900:
		{
			dmode->virtual_width=1152;
			dmode->virtual_height=900;
			dmode->space=B_RGB16;
			break;
		}
		case B_32_BIT_1152x900:
		{
			dmode->virtual_width=1152;
			dmode->virtual_height=900;
			dmode->space=B_RGB32;
			break;
		}
		case B_8_BIT_1280x1024:
		{
			dmode->virtual_width=1280;
			dmode->virtual_height=1024;
			dmode->space=B_CMAP8;
			break;
		}
		case B_15_BIT_1280x1024:
		{
			dmode->virtual_width=1280;
			dmode->virtual_height=1024;
			dmode->space=B_RGB15;
			break;
		}
		case B_16_BIT_1280x1024:
		{
			dmode->virtual_width=1280;
			dmode->virtual_height=1024;
			dmode->space=B_RGB16;
			break;
		}
		case B_32_BIT_1280x1024:
		{
			dmode->virtual_width=1280;
			dmode->virtual_height=1024;
			dmode->space=B_RGB32;
			break;
		}
		case B_8_BIT_1600x1200:
		{
			dmode->virtual_width=1600;
			dmode->virtual_height=1200;
			dmode->space=B_CMAP8;
			break;
		}
		case B_15_BIT_1600x1200:
		{
			dmode->virtual_width=1600;
			dmode->virtual_height=1200;
			dmode->space=B_RGB15;
			break;
		}
		case B_16_BIT_1600x1200:
		{
			dmode->virtual_width=1600;
			dmode->virtual_height=1200;
			dmode->space=B_RGB16;
			break;
		}
		case B_32_BIT_1600x1200:
		{
			dmode->virtual_width=1600;
			dmode->virtual_height=1200;
			dmode->space=B_RGB32;
			break;
		}
		default:
			return B_ERROR;
	}

	return B_OK;
}


BRect
CalculatePolygonBounds(BPoint *pts, int32 pointcount)
{
	if (!pts)
		return BRect(0,0,0,0);

	BRect r(0,0,0,0);

	// shamelessly stolen from Marc's BPolygon code and tweaked to fit. :P
	r = BRect(pts[0], pts[0]);

	for (int32 i = 1; i < 4; i++) {
		if (pts[i].x < r.left)
			r.left = pts[i].x;
		if (pts[i].y < r.top)
			r.top = pts[i].y;
		if (pts[i].x > r.right)
			r.right = pts[i].x;
		if (pts[i].y > r.bottom)
			r.bottom = pts[i].y;
	}

	return r;
}

