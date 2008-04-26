/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginWindow.h"
#include "LoginView.h"

#define WINDOW_FEEL B_NORMAL_WINDOW_FEEL
//#define WINDOW_FEEL B_FLOATING_ALL_WINDOW_FEEL

LoginWindow::LoginWindow(BRect frame)
	: BWindow(frame, "Welcome to Haiku", B_TITLED_WINDOW_LOOK, 
		WINDOW_FEEL, 
		B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | 
		B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | 
		B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES)
{
	LoginView *v = new LoginView(Bounds());
	AddChild(v);
	SetPulseRate(1000000LL);
}


LoginWindow::~LoginWindow()
{
}


