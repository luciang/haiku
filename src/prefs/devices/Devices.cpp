/*

Devices by Sikosis

(C)2003 OBOS

*/

// Includes -------------------------------------------------------------------------------------------------- //
#include <Application.h>
#include <Window.h>
#include <View.h>

#include "Devices.h"
#include "DevicesWindows.h"
#include "DevicesViews.h"
#include "DevicesConstants.h"
// ---------------------------------------------------------------------------------------------------------- //

DevicesWindow   *ptrDevicesWindow;


// Devices -- constructor 
Devices::Devices() : BApplication (APP_SIGNATURE)
{
	BRect DevicesWindowRect(0,0,396,400);
	ptrDevicesWindow = new DevicesWindow(DevicesWindowRect);
}
// ---------------------------------------------------------------------------------------------------------- //


// Devices::MessageReceived -- handles incoming messages
void Devices::MessageReceived (BMessage *message)
{
	switch(message->what)
	{
	    default:
    	    BApplication::MessageReceived(message); // pass it along ... 
        	break;
    }
}
// ---------------------------------------------------------------------------------------------------------- //


// Devices Main
int main(void)
{
   Devices theApp;
   theApp.Run();
   return 0;
}
// end ------------------------------------------------------------------------------------------------------ //

