/*
 * Copyright 2005, J�r�me Duval, jerome.duval@free.fr.

 * Distributed under the terms of the MIT License.
 */

#ifndef _WINDOW_PRIVATE_H
#define _WINDOW_PRIVATE_H

/* Private window looks */

const window_look kDesktopWindowLook = window_look(4);
const window_look kLeftTitledWindowLook = window_look(25);

/* Private window feels */

const window_feel kDesktopWindowFeel = window_feel(1024);
const window_feel kMenuWindowFeel = window_feel(1025);

/* Private window types */

const window_type kWindowScreenWindow = window_type(1026);

/* Private window flags */

const uint32 kWorkspacesWindowFlag = 0x8000;
const uint32 kWindowScreenFlag = 0x10000;

#endif //_WINDOW_PRIVATE_H

