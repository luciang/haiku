/*

NewConnectionWindow - DialUp Networking

Author: Sikosis (beos@gravity24hr.com)

(C) 2002 OpenBeOS under MIT license

*/

// Includes -------------------------------------------------------------------------------------------------- //
#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Directory.h>
#include <Path.h>
#include <Screen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <TextControl.h>
#include <Window.h>
#include <View.h>

#include "NewConnectionWindow.h"
#include "DUNView.h"

// Constants ------------------------------------------------------------------------------------------------- //
const uint32 BTN_ADD = 'BAdd';
const uint32 BTN_CANCEL = 'BCnl';
const uint32 TXT_NEW_CONNECTION = 'TxCx';
// ---------------------------------------------------------------------------------------------------------- //


// CenterWindowOnScreen -- Centers the BWindow to the Current Screen
static void CenterWindowOnScreen(BWindow* w)
{
	BRect	screenFrame = (BScreen(B_MAIN_SCREEN_ID).Frame());
	BPoint 	pt;
	pt.x = screenFrame.Width()/2 - w->Bounds().Width()/2;
	pt.y = screenFrame.Height()/2 - w->Bounds().Height()/2;

	if (screenFrame.Contains(pt))
		w->MoveTo(pt);
}
// ---------------------------------------------------------------------------------------------------------- //


// NewConnectionWindow - Constructor
NewConnectionWindow::NewConnectionWindow(BRect frame) : BWindow (frame, "NewConnectionWindow", B_MODAL_WINDOW , B_NOT_RESIZABLE , 0)
{
	InitWindow();
	CenterWindowOnScreen(this);
	Show();
}


// NewConnectionWindow - Destructor
NewConnectionWindow::~NewConnectionWindow()
{
	//exit(0);
}


// NewConnectionWindow::InitWindow
void NewConnectionWindow::InitWindow(void)
{	
	BRect r;
	r = Bounds(); // the whole view
	
	int LeftMargin = 14;
	int AddButtonSize = 75;
	int CancelButtonSize = 75;
	
	//float CancelLeftMargin = (r.right / 2) - ((AddButtonSize + 20 + CancelButtonSize) / 2);
	//float AddLeftMargin = CancelLeftMargin + CancelButtonSize + 20;;
	
	float AddLeftMargin = r.right - (AddButtonSize + 10);
	float CancelLeftMargin = AddLeftMargin - (CancelButtonSize + 11);
	
	int NewConnectionTop = 10;
	
	txtNewConnection = new BTextControl(BRect(LeftMargin,NewConnectionTop,r.right-10,NewConnectionTop+10), "txtNewConnection","Connection name:","New Connection",new BMessage(TXT_NEW_CONNECTION), B_FOLLOW_LEFT | B_FOLLOW_TOP , B_WILL_DRAW | B_NAVIGABLE);
	txtNewConnection->SetDivider(88);
	   	
	btnCancel = new BButton(BRect (CancelLeftMargin,r.bottom-34,CancelLeftMargin+CancelButtonSize,r.bottom-14),"Cancel","Cancel", new BMessage(BTN_CANCEL), B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW | B_NAVIGABLE);		
  	btnAdd = new BButton(BRect (AddLeftMargin,r.bottom-34,AddLeftMargin+AddButtonSize,r.bottom-14),"Add","Add", new BMessage(BTN_ADD), B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW | B_NAVIGABLE);
  	btnAdd->MakeDefault(true);
		
	AddChild(ptrNewConnectionWindowView = new NewConnectionWindowView(r));
	ptrNewConnectionWindowView->AddChild(txtNewConnection);
	ptrNewConnectionWindowView->AddChild(btnCancel);
    ptrNewConnectionWindowView->AddChild(btnAdd);
    txtNewConnection->MakeFocus(true);
}
// ---------------------------------------------------------------------------------------------------------- //

// NewConnectionWindow::MessageReceived -- receives messages
void NewConnectionWindow::MessageReceived (BMessage *message)
{
	switch(message->what)
	{
		case BTN_ADD:
			char tmp[256];
			sprintf(tmp,"%s",txtNewConnection->Text());
			(new BAlert("",tmp,"tmp"))->Go();
			
			// need to create a new file called txtNewConnection->Text() and
			// place it in the /boot/home/config/settings/DUN/DUNCONNECT directory
			
			// before closing we need to change the main window to state 1
						
			Quit();
			break;
		case BTN_CANCEL:
		    Quit();
			//Hide(); // change later 
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
// ---------------------------------------------------------------------------------------------------------- //


