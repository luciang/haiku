#ifndef _BUTTON_H
#include <Button.h>
#endif

#ifndef _CHECK_BOX_H
#include <CheckBox.h>
#endif

#ifndef _STRING_H
#include <String.h>
#endif

#ifndef _TEXT_CONTROL_H
#include <TextControl.h>
#endif

#ifndef _WINDOW_H 
#include <Window.h>
#endif

#ifndef CONSTANTS_H
#include "Constants.h"
#endif

#ifndef REPLACE_WINDOW_H
#include "ReplaceWindow.h"
#endif

ReplaceWindow::ReplaceWindow(BRect frame, BHandler *_handler, BString *searchString, BString *replaceString, bool *caseState, bool *wrapState, bool *backState)
				: BWindow(frame, "", B_MODAL_WINDOW, B_NOT_RESIZABLE,B_CURRENT_WORKSPACE) 
			{
	
	fReplaceView= new BView(Bounds(),"",B_FOLLOW_ALL_SIDES,B_WILL_DRAW);
	fReplaceView->SetViewColor(216,216,216);
	
	fReplaceView->AddChild (fSearchString= new BTextControl(BRect(5,10,290,50), "", "Find:",NULL, NULL,
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fSearchString-> SetDivider(65);
	
	fReplaceView->AddChild(fReplaceString=new BTextControl(BRect(5,35,290,50), "", "Replace with:",NULL,
		 NULL,B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fReplaceString->SetDivider(65);
	
	fReplaceView->AddChild(fCaseSensBox=new BCheckBox(BRect(72,60,162,52),"","Case-sensitive", NULL,
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fReplaceView->AddChild(fWrapBox=new BCheckBox(BRect(72,80,195,70),"","Wrap-around search", NULL,
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fReplaceView->AddChild(fBackSearchBox=new BCheckBox(BRect(72,100,179,95),"","Search backwards", NULL,
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE)); 
	fReplaceView->AddChild(fAllWindowsBox=new BCheckBox(BRect(72,120,195,95),"","Replace in all windows",
		new BMessage(CHANGE_WINDOW) ,B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
		fUichange=false;
	
	//vertical separator at 110 in StyledEdit
	fReplaceView->AddChild(fReplaceAllButton=new BButton(BRect(10,150,98,166),"","Replace All",new BMessage(MSG_REPLACE_ALL),
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fReplaceView->AddChild(fCancelButton=new BButton(BRect(141,150,211,166),"","Cancel",new BMessage(B_QUIT_REQUESTED),
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fReplaceView->AddChild(fReplaceButton=new BButton(BRect(221,150,291,166),"","Replace",new BMessage(MSG_REPLACE),
		B_FOLLOW_LEFT|B_FOLLOW_TOP,B_WILL_DRAW|B_NAVIGABLE));
	fReplaceButton->MakeDefault(true);
	
	AddChild(fReplaceView);
	fHandler= _handler;
	
	const char *searchtext= searchString->String();
	const char *replacetext= replaceString->String(); 
	
	fSearchString->SetText(searchtext);
	fReplaceString->SetText(replacetext);
	fSearchString-> MakeFocus(true); //021021
	
	if(*caseState== true)
		fCaseSensBox->SetValue(B_CONTROL_ON);
	else
		fCaseSensBox->SetValue(B_CONTROL_OFF);
	
	if(*wrapState== true)
		fWrapBox->SetValue(B_CONTROL_ON);
	else
		fWrapBox->SetValue(B_CONTROL_OFF);
	
	if(*backState== true)
		fBackSearchBox->SetValue(B_CONTROL_ON);
	else
		fBackSearchBox->SetValue(B_CONTROL_OFF);
	
	Show();
}

void ReplaceWindow::MessageReceived(BMessage *msg){
	switch(msg->what){
		case B_QUIT_REQUESTED:
			Quit();
		break;
		case MSG_REPLACE:
			ExtractToMsg(new BMessage(MSG_REPLACE));
		break;
		case CHANGE_WINDOW:
			ChangeUi();
		break;
		case MSG_REPLACE_ALL:
			ExtractToMsg(new BMessage(MSG_REPLACE_ALL));
		break;	
	default:
		BWindow::MessageReceived(msg);
		break;	
	}
}

void ReplaceWindow::ChangeUi(){
	
	if(!fUichange){
		fReplaceAllButton->MakeDefault(true);
		fReplaceButton->SetEnabled(false);
		fWrapBox->SetValue(B_CONTROL_ON);
		fWrapBox->SetEnabled(false);
		fBackSearchBox->SetEnabled(false);
		fUichange=true;
		}
	else{
		fReplaceButton->MakeDefault(true);
		fReplaceButton->SetEnabled(true);
		fReplaceAllButton->SetEnabled(true);
		fWrapBox->SetValue(B_CONTROL_OFF);
		fWrapBox->SetEnabled(true);
		fBackSearchBox->SetEnabled(true);
		fUichange=false;
		}
		
}/***ReplaceWindow::ChangeUi()***/	
	
void ReplaceWindow::DispatchMessage(BMessage *message, BHandler *fHandler){
	int8	key;
	if ( message->what == B_KEY_DOWN ) {
		status_t result;
		result= message-> FindInt8("byte", 0, &key);
		
		if (result== B_OK) {
			if(key== B_ESCAPE){
				message-> MakeEmpty();
				message-> what= B_QUIT_REQUESTED;
			}
		}
	}
	BWindow::DispatchMessage(message,fHandler);	
}

void ReplaceWindow::ExtractToMsg(BMessage *message ){
	
	int32	caseBoxState;
	int32 	wrapBoxState;
	int32 	backBoxState;
	int32	allWinBoxState;
	
	caseBoxState= fCaseSensBox-> Value();
	wrapBoxState= fWrapBox-> Value();
	backBoxState= fBackSearchBox-> Value();
	allWinBoxState= fAllWindowsBox-> Value();
	
	bool 	caseSens;
	bool 	wrapIt;
	bool 	backSearch;
	bool	allWindows;
	
	if(caseBoxState== B_CONTROL_ON)	
		caseSens= true;
	else
		caseSens= false;
	
	if(wrapBoxState== B_CONTROL_ON)		 
		wrapIt= true;
	else
		wrapIt= false;
	
	if(backBoxState== B_CONTROL_ON)		 
		backSearch= true;
	else
		backSearch= false;
		
	if(allWinBoxState== B_CONTROL_ON)
		allWindows= true;
	else
		allWindows= false;
			
	message->AddString("FindText",fSearchString->Text());
	message->AddString("ReplaceText",fReplaceString->Text());
	message->AddBool("casesens", caseSens);
	message->AddBool("wrap", wrapIt);
	message->AddBool("backsearch", backSearch);
	message->AddBool("allwindows", allWindows);
	
	fHandler->Looper()->PostMessage(message,fHandler);
	delete(message);
	PostMessage(B_QUIT_REQUESTED);
}


	


