/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
*/

#include "TeapotApp.h"

int
main(int argc, char** argv)
{
	TeapotApp* app = new TeapotApp("application/x-vnd.Haiku-GLTeapot");
	app->Run();
	delete app;	
	return 0;
}


TeapotApp::TeapotApp(const char* sign)
	: BApplication(sign)
{
	fTeapotWindow = new TeapotWindow(BRect(5, 25, 300, 315), "GLTeapot", B_TITLED_WINDOW, 0);	
	fTeapotWindow->Show();
}


void
TeapotApp::MessageReceived(BMessage* msg)
{
//	msg->PrintToStream();
	switch (msg->what) {

		default:
			BApplication::MessageReceived(msg);
	}		
}
