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
//	File Name:		Window.cpp
//	Author:			Adrian Oanca (oancaadrian@yahoo.com)
//	Description:	A BWindow object represents a window that can be displayed
//					on the screen, and that can be the target of user events
//------------------------------------------------------------------------------

// Standard Includes -----------------------------------------------------------

// System Includes -------------------------------------------------------------
#include <BeBuild.h>
#include <stdio.h>
#include <math.h>

// Project Includes ------------------------------------------------------------
#include <AppMisc.h>
#include <InterfaceDefs.h>
#include <Application.h>
#include <Looper.h>
#include <Handler.h>
#include <View.h>
#include <MenuBar.h>
#include <String.h>
#include <PropertyInfo.h>
#include <Window.h>
#include <Screen.h>
#include <Button.h>
#include <PortLink.h>
#include <ServerProtocol.h>
#include <AppServerLink.h>
#include <MessageQueue.h>
#include <MessageRunner.h>
#include <Roster.h>

// Local Includes --------------------------------------------------------------
#include "WindowAux.h"
#include <TokenSpace.h>
#include "MessageUtils.h"

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------
static property_info windowPropInfo[] =
{
	{ "Feel", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the current feel of the window.",0 },

	{ "Feel", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the feel of the window.",0 },

	{ "Flags", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the current flags of the window.",0 },

	{ "Flags", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the window flags.",0 },

	{ "Frame", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the window's frame rectangle.",0},

	{ "Frame", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the window's frame rectangle.",0 },

	{ "Hidden", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if the window is hidden; false otherwise.",0},

	{ "Hidden", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Hides or shows the window.",0 },

	{ "Look", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the current look of the window.",0},

	{ "Look", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the look of the window.",0 },

	{ "Title", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns a string containing the window title.",0},

	{ "Title", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the window title.",0 },

	{ "Workspaces", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns int32 bitfield of the workspaces in which the window appears.",0},

	{ "Workspaces", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the workspaces in which the window appears.",0 },

	{ "MenuBar", { 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Directs the scripting message to the key menu bar.",0 },

	{ "View", { 0 },
		{ 0 }, "Directs the scripting message to the top view without popping the current specifier.",0 },

	{ "Minimize", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Minimizes the window if \"data\" is true; restores otherwise.",0 },

	{ 0, { 0 }, { 0 }, 0, 0 }
}; 
//------------------------------------------------------------------------------

// Constructors
//------------------------------------------------------------------------------
BWindow::BWindow(BRect frame,
				const char* title, 
				window_type type,
				uint32 flags,
				uint32 workspace)
			: BLooper( title )
{
	window_look look;
	window_feel feel;
	
	decomposeType(type, &look, &feel);

	InitData( frame, title, look, feel, flags, workspace);
}

//------------------------------------------------------------------------------

BWindow::BWindow(BRect frame,
				const char* title, 
				window_look look,
				window_feel feel,
				uint32 flags,
				uint32 workspace)
			: BLooper( title )
{
	InitData( frame, title, look, feel, flags, workspace );
}

//------------------------------------------------------------------------------

BWindow::BWindow(BMessage* data)
	: BLooper(data)
{
	BMessage		msg;
	BArchivable		*obj;
	const char		*title;
	window_look		look;
	window_feel		feel;
	uint32			type;
	uint32			workspaces;

	status_t		retval;

	data->FindRect("_frame", &fFrame);
	data->FindString("_title", &title);
	data->FindInt32("_wlook", (int32*)&look);
	data->FindInt32("_wfeel", (int32*)&feel);
	if ( data->FindInt32("_flags", (int32*)&fFlags) == B_OK )
		{ }
	else
		fFlags		= 0;
	data->FindInt32("_wspace", (int32*)&workspaces);

	if ( data->FindInt32("_type", (int32*)&type) == B_OK ){
		decomposeType( (window_type)type, &fLook, &fFeel );
	}

		// connect to app_server and initialize data
	InitData( fFrame, title, look, feel, fFlags, workspaces );

	if ( data->FindFloat("_zoom", 0, &fMaxZoomWidth) == B_OK )
		if ( data->FindFloat("_zoom", 1, &fMaxZoomHeight) == B_OK)
			SetZoomLimits( fMaxZoomWidth, fMaxZoomHeight );

	if (data->FindFloat("_sizel", 0, &fMinWindWidth) == B_OK )
		if (data->FindFloat("_sizel", 1, &fMinWindHeight) == B_OK )
			if (data->FindFloat("_sizel", 2, &fMaxWindWidth) == B_OK )
				if (data->FindFloat("_sizel", 3, &fMaxWindHeight) == B_OK )
					SetSizeLimits(	fMinWindWidth, fMaxWindWidth,
									fMinWindHeight, fMaxWindHeight );

	if (data->FindInt64("_pulse", &fPulseRate) == B_OK )
		SetPulseRate( fPulseRate );

	int				i = 0;
	while ( data->FindMessage("_views", i++, &msg) == B_OK){ 
		obj			= instantiate_object(&msg);

		BView		*child;
		child		= dynamic_cast<BView *>(obj);
		if (child)
			AddChild( child ); 
	}
}

//------------------------------------------------------------------------------

BWindow::~BWindow(){

		// the following lines, remove all existing shortcuts and delete accelList
	int32			noOfItems;
	
	noOfItems		= accelList.CountItems();
	for ( int index = noOfItems;  index >= 0; index-- ) {
		_BCmdKey		*cmdKey;

		cmdKey			= (_BCmdKey*)accelList.ItemAt( index );
		
		accelList.RemoveItem(index);
		
		delete cmdKey->message;
		delete cmdKey;
	}
	
// TODO: release other dinamicaly alocated objects

		// disable pulsing
	SetPulseRate( 0 );

	delete		srvGfxLink;
	delete		serverLink;
	delete_port( receive_port );
}

//------------------------------------------------------------------------------

BArchivable* BWindow::Instantiate(BMessage* data){

   if ( !validate_instantiation( data , "BWindow" ) ) 
      return NULL; 
   return new BWindow(data); 
}

//------------------------------------------------------------------------------

status_t BWindow::Archive(BMessage* data, bool deep) const{

	status_t		retval;

	retval		= BLooper::Archive( data, deep );
	if (retval != B_OK)
		return retval;

	data->AddRect("_frame", fFrame);
	data->AddString("_title", fTitle);
	data->AddInt32("_wlook", fLook);
	data->AddInt32("_wfeel", fFeel);
	if (fFlags)
		data->AddInt32("_flags", fFlags);
	data->AddInt32("_wspace", (uint32)Workspaces());

	if ( !composeType(fLook, fFeel) )
		data->AddInt32("_type", (uint32)Type());

	if (fMaxZoomWidth != 32768.0 || fMaxZoomHeight != 32768.0)
	{
		data->AddFloat("_zoom", fMaxZoomWidth);
		data->AddFloat("_zoom", fMaxZoomHeight);
	}

	if (fMinWindWidth != 0.0	 || fMinWindHeight != 0.0 ||
		fMaxWindWidth != 32768.0 || fMaxWindHeight != 32768.0)
	{
		data->AddFloat("_sizel", fMinWindWidth);
		data->AddFloat("_sizel", fMinWindHeight);
		data->AddFloat("_sizel", fMaxWindWidth);
		data->AddFloat("_sizel", fMaxWindHeight);
	}

	if (fPulseRate != 500000)
		data->AddInt64("_pulse", fPulseRate);

	if (deep)
	{
		int32		noOfViews = CountChildren();
		for (int i=0; i<noOfViews; i++){
			BMessage		childArchive;

			retval			= ChildAt(i)->Archive( &childArchive, deep );
			if (retval == B_OK)
				data->AddMessage( "_views", &childArchive );
		}
	}

	return B_OK;
}

//------------------------------------------------------------------------------

void BWindow::Quit(){

	if (!IsLocked())
	{
		const char* name = Name();
		if (!name)
			name = "no-name";

		printf("ERROR - you must Lock a looper before calling Quit(), "
			   "team=%ld, looper=%s\n", Team(), name);
	}

		// Try to lock
	if (!Lock()){
			// We're toast already
		return;
	}

	while (!IsHidden())	{ Hide(); }

		// ... also its children
	detachTopView();

		// tell app_server, this window will finish execution
	stopConnection();

	if (fFlags & B_QUIT_ON_WINDOW_CLOSE)
		be_app->PostMessage( B_QUIT_REQUESTED );

	BLooper::Quit();
}

//------------------------------------------------------------------------------

void BWindow::AddChild(BView *child, BView *before){
	top_view->AddChild( child, before );
}

//------------------------------------------------------------------------------

bool BWindow::RemoveChild(BView *child){
	return top_view->RemoveChild( child );
}

//------------------------------------------------------------------------------

int32 BWindow::CountChildren() const{
	return top_view->CountChildren();
}

//------------------------------------------------------------------------------

BView* BWindow::ChildAt(int32 index) const{
	return top_view->ChildAt( index );
}

//------------------------------------------------------------------------------

void BWindow::Minimize(bool minimize){
	if (IsModal())
		return;

	if (IsFloating())
		return;		

	serverLink->SetOpCode( AS_WINDOW_MINIMIZE );
	serverLink->Attach( minimize );

	Lock();
	serverLink->Flush( );
	Unlock();
}
//------------------------------------------------------------------------------

status_t BWindow::SendBehind(const BWindow* window){

	if (!window)
		return B_ERROR;

	PortLink::ReplyData		replyData;
	
	serverLink->SetOpCode( AS_SEND_BEHIND );
	serverLink->Attach( _get_object_token_(window) );	

	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();
	
	delete replyData.buffer;

	return replyData.code == SERVER_TRUE? B_OK : B_ERROR;
}

//------------------------------------------------------------------------------

void BWindow::Flush() const{
	const_cast<BWindow*>(this)->Lock();
	srvGfxLink->Flush();
	const_cast<BWindow*>(this)->Unlock();
}

//------------------------------------------------------------------------------

void BWindow::Sync() const{
	PortLink::ReplyData		replyData;

	const_cast<BWindow*>(this)->Lock();	
	srvGfxLink->FlushWithReply( &replyData );
	const_cast<BWindow*>(this)->Unlock();
	
	delete replyData.buffer;
}

//------------------------------------------------------------------------------

void BWindow::DisableUpdates(){

	serverLink->SetOpCode( AS_DISABLE_UPDATES );
	
	Lock();	
	serverLink->Flush( );
	Unlock();
}

//------------------------------------------------------------------------------

void BWindow::EnableUpdates(){
	
	serverLink->SetOpCode( AS_ENABLE_UPDATES );

	Lock();
	serverLink->Flush( );
	Unlock();
}

//------------------------------------------------------------------------------

void BWindow::BeginViewTransaction(){
	if ( !fInTransaction ){
		srvGfxLink->SetOpCode( AS_BEGIN_TRANSACTION );
		fInTransaction		= true;
	}
}

//------------------------------------------------------------------------------

void BWindow::EndViewTransaction(){
	if ( fInTransaction ){
// !!!!!!!!! if not '(int32)AS_END_TRANSACTION' we receive an error ?????
		srvGfxLink->Attach( (int32)AS_END_TRANSACTION );
		fInTransaction		= false;
		Flush();
	}
}

//------------------------------------------------------------------------------

bool BWindow::IsFront() const{
	if (IsActive())
		return true;

	if (IsModal())
		return true;

	return false;
}

//------------------------------------------------------------------------------

void BWindow::MessageReceived( BMessage *msg )
{ 
	BMessage			specifier;
	int32				what;
	const char*			prop;
	int32				index;
	status_t			err;

	if (msg->HasSpecifiers()){

	err = msg->GetCurrentSpecifier(&index, &specifier, &what, &prop);
	if (err == B_OK)
	{
		BMessage			replyMsg;

		switch (msg->what)
		{
		case B_GET_PROPERTY:{
				replyMsg.what		= B_NO_ERROR;
				replyMsg.AddInt32( "error", B_OK );
				
				if (strcmp(prop, "Feel") ==0 )
				{
					replyMsg.AddInt32( "result", (uint32)Feel());
				}
				else if (strcmp(prop, "Flags") ==0 )
				{
					replyMsg.AddInt32( "result", Flags());
				}
				else if (strcmp(prop, "Frame") ==0 )
				{
					replyMsg.AddRect( "result", Frame());				
				}
				else if (strcmp(prop, "Hidden") ==0 )
				{
					replyMsg.AddBool( "result", IsHidden());				
				}
				else if (strcmp(prop, "Look") ==0 )
				{
					replyMsg.AddInt32( "result", (uint32)Look());				
				}
				else if (strcmp(prop, "Title") ==0 )
				{
					replyMsg.AddString( "result", Title());				
				}
				else if (strcmp(prop, "Workspaces") ==0 )
				{
					replyMsg.AddInt32( "result", Workspaces());				
				}
			}break;

		case B_SET_PROPERTY:{
				if (strcmp(prop, "Feel") ==0 )
				{
					uint32			newFeel;
					if (msg->FindInt32( "data", (int32*)&newFeel ) == B_OK){
						SetFeel( (window_feel)newFeel );
						
						replyMsg.what		= B_NO_ERROR;
						replyMsg.AddInt32( "error", B_OK );
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
				}
				
				else if (strcmp(prop, "Flags") ==0 )
				{
					uint32			newFlags;
					if (msg->FindInt32( "data", (int32*)&newFlags ) == B_OK){
						SetFlags( newFlags );
						
						replyMsg.what		= B_NO_ERROR;
						replyMsg.AddInt32( "error", B_OK );
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
				}
				
				else if (strcmp(prop, "Frame") ==0 )
				{
					BRect			newFrame;
					if (msg->FindRect( "data", &newFrame ) == B_OK){
						MoveTo( newFrame.LeftTop() );
						ResizeTo( newFrame.right, newFrame.bottom);
						
						replyMsg.what		= B_NO_ERROR;
						replyMsg.AddInt32( "error", B_OK );
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
				}
				
				else if (strcmp(prop, "Hidden") ==0 )
				{
					bool			newHiddenState;
					if (msg->FindBool( "data", &newHiddenState ) == B_OK){
						if ( !IsHidden() && newHiddenState == true ){
							Hide();
							
							replyMsg.what		= B_NO_ERROR;
							replyMsg.AddInt32( "error", B_OK );
							
						}
						else if ( IsHidden() && newHiddenState == false ){
							Show();
							
							replyMsg.what		= B_NO_ERROR;
							replyMsg.AddInt32( "error", B_OK );
						}
						else{
							replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
							replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
							replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
						}
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
				}
				
				else if (strcmp(prop, "Look") ==0 )
				{
					uint32			newLook;
					if (msg->FindInt32( "data", (int32*)&newLook ) == B_OK){
						SetLook( (window_look)newLook );
						
						replyMsg.what		= B_NO_ERROR;
						replyMsg.AddInt32( "error", B_OK );
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
				}
				
				else if (strcmp(prop, "Title") ==0 )
				{
					const char		**newTitle;
					if (msg->FindString( "data", newTitle ) == B_OK){
						SetTitle( *newTitle );
						
						replyMsg.what		= B_NO_ERROR;
						replyMsg.AddInt32( "error", B_OK );
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
					delete newTitle;
				}
				
				else if (strcmp(prop, "Workspaces") ==0 )
				{
					uint32			newWorkspaces;
					if (msg->FindInt32( "data", (int32*)&newWorkspaces ) == B_OK){
						SetWorkspaces( newWorkspaces );
						
						replyMsg.what		= B_NO_ERROR;
						replyMsg.AddInt32( "error", B_OK );
					}
					else{
						replyMsg.what		= B_MESSAGE_NOT_UNDERSTOOD;
						replyMsg.AddInt32( "error", B_BAD_SCRIPT_SYNTAX );
						replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
					}
				}
				
			}break;
		}
		msg->SendReply( &replyMsg );
	}
	else{
		BMessage		replyMsg(B_MESSAGE_NOT_UNDERSTOOD);
		replyMsg.AddInt32( "error" , B_BAD_SCRIPT_SYNTAX );
		replyMsg.AddString( "message", "Didn't understand the specifier(s)" );
		
		msg->SendReply( &replyMsg );
	}

	} // END: if (msg->HasSpecifiers())
	else
		BLooper::MessageReceived( msg );
} 

//------------------------------------------------------------------------------

void BWindow::DispatchMessage(BMessage *msg, BHandler *target) 
{
	if (!msg){
		BLooper::DispatchMessage( msg, target );
		return;
	}

	switch ( msg->what ) { 
	case B_ZOOM:{
		Zoom();
		break;}

	case B_MINIMIZE:{
		bool			minimize;

		msg->FindBool("minimize", &minimize);

		fMinimized		= minimize;
		Minimize( minimize );
		break;}

	case B_WINDOW_RESIZED:{
		BPoint			offset;
		int32			width, height;

		msg->FindInt32("width", &width);
		msg->FindInt32("height", &height);
		offset.x		= width;
		offset.y		= height;

		fFrame.SetRightBottom( fFrame.LeftTop() + offset );
		FrameResized( offset.x, offset.y );
		break;}

	case B_WINDOW_MOVED:{
		BPoint			origin;

		msg->FindPoint("where", &origin);

		fFrame.OffsetTo( origin );
		FrameMoved( origin );
		break;}

		// this is NOT an app_server message and we have to be cautious
	case B_WINDOW_MOVE_BY:{
		BPoint			offset;

		if (msg->FindPoint("data", &offset) == B_OK)
			MoveBy( offset.x, offset.y );
		else
			msg->SendReply( B_MESSAGE_NOT_UNDERSTOOD );
		break;}

		// this is NOT an app_server message and we have to be cautious
	case B_WINDOW_MOVE_TO:{
		BPoint			origin;

		if (msg->FindPoint("data", &origin) == B_OK)
			MoveTo( origin );
		else
			msg->SendReply( B_MESSAGE_NOT_UNDERSTOOD );
		break;}

	case B_WINDOW_ACTIVATED:{
		bool			active;

		msg->FindBool("active", &active);

		fActive			= active; 
		handleActivation( active );
		break;}

	case B_SCREEN_CHANGED:{
		BRect			frame;
		uint32			mode;

		msg->FindRect("frame", &frame);
		msg->FindInt32("mode", (int32*)&mode);
		ScreenChanged( frame, (color_space)mode );
		break;}

	case B_WORKSPACE_ACTIVATED:{
		uint32			workspace;
		bool			active;

		msg->FindInt32( "workspace", (int32*)&workspace );
		msg->FindBool( "active", &active );
		WorkspaceActivated( workspace, active );
		break;}

	case B_WORKSPACES_CHANGED:{
		uint32			oldWorkspace;
		uint32			newWorkspace;

		msg->FindInt32( "old", (int32*)&oldWorkspace );
		msg->FindInt32( "new", (int32*)&newWorkspace );
		WorkspacesChanged( oldWorkspace, newWorkspace );
		break;}

	case B_KEY_DOWN:{
		uint32			modifiers;
		int32			raw_char;
		const char		*string;

		msg->FindInt32( "modifiers", (int32*)&modifiers );
		msg->FindInt32( "raw_char", &raw_char );
		msg->FindString( "bytes", &string );

		if ( !handleKeyDown( raw_char, (uint32)modifiers) )
			fFocus->KeyDown( string, strlen(string)-1 );
		break;}

	case B_KEY_UP:{
		int32			keyRepeat;
		const char		*string;

		msg->FindString( "bytes", &string );
		fFocus->KeyUp( string, strlen(string)-1 );
		break;}

	case B_UNMAPPED_KEY_DOWN:{
		if (fFocus)
			fFocus->MessageReceived( msg );
		break;}

	case B_UNMAPPED_KEY_UP:{
		if (fFocus)
			fFocus->MessageReceived( msg );
		break;}

	case B_MODIFIERS_CHANGED:{
		if (fFocus)
			fFocus->MessageReceived( msg );
		break;}

	case B_MOUSE_WHEEL_CHANGED:{
		if (fFocus)
			fFocus->MessageReceived( msg );
		break;}

	case B_MOUSE_DOWN:{
		BPoint			where;
		uint32			modifiers;
		uint32			buttons;
		int32			clicks;

		msg->FindPoint( "where", &where );
		msg->FindInt32( "modifiers", (int32*)&modifiers );
		msg->FindInt32( "buttons", (int32*)&buttons );
		msg->FindInt32( "clicks", &clicks );

		sendMessageUsingEventMask( B_MOUSE_DOWN, where );
		break;}

	case B_MOUSE_UP:{
		BPoint			where;
		uint32			modifiers;

		msg->FindPoint( "where", &where );
		msg->FindInt32( "modifiers", (int32*)&modifiers );
		
		sendMessageUsingEventMask( B_MOUSE_UP, where );
		break;}

	case B_MOUSE_MOVED:{
		BPoint			where;
		uint32			buttons;

		msg->FindPoint( "where", &where );
		msg->FindInt32( "buttons", (int32*)&buttons );
		
		sendMessageUsingEventMask( B_MOUSE_MOVED, where );
		break;}

	case B_PULSE:{
		if (fPulseEnabled)
			sendPulse( top_view );
		break;}

	case B_QUIT_REQUESTED:{
		if (QuitRequested())
			Quit();
		break;}

	case _UPDATE_:{
		BView			*view;
		int32			token;
		BRect			frame;

		msg->FindInt32("token", &token);
		msg->FindRect("frame", &frame);
		view			= findView( top_view, token );

		drawView( view, frame );
		break;}

	default:{
		BLooper::DispatchMessage(msg, target); 
		break;}
   }
   Flush();
}

//------------------------------------------------------------------------------

void BWindow::FrameMoved(BPoint new_position){
	// does nothing
	// Hook function
}

//------------------------------------------------------------------------------

void BWindow::FrameResized(float new_width, float new_height){
	// does nothing
	// Hook function
}

//------------------------------------------------------------------------------

void BWindow::WorkspacesChanged(uint32 old_ws, uint32 new_ws){
	// does nothing
	// Hook function
}

//------------------------------------------------------------------------------

void BWindow::WorkspaceActivated(int32 ws, bool state){
	// does nothing
	// Hook function
}

//------------------------------------------------------------------------------

void BWindow::MenusBeginning(){
	// does nothing
	// Hook function
}

//------------------------------------------------------------------------------

void BWindow::MenusEnded(){
	// does nothing
	// Hook function
}

//------------------------------------------------------------------------------

void BWindow::SetSizeLimits(float minWidth, float maxWidth, 
							float minHeight, float maxHeight){

	if (minWidth > maxWidth)
		return;
	if (minHeight > maxHeight)
		return;

	PortLink::ReplyData		replyData;

	serverLink->SetOpCode( AS_SET_SIZE_LIMITS );
	serverLink->Attach( fMinWindWidth );
	serverLink->Attach( fMaxWindWidth );
	serverLink->Attach( fMinWindHeight );
	serverLink->Attach( fMaxWindHeight );
		
	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();

	delete replyData.buffer;

	if (replyData.code == SERVER_TRUE){
		fMinWindHeight		= minHeight;
		fMinWindWidth		= minWidth;
		fMaxWindHeight		= maxHeight;
		fMaxWindWidth		= maxWidth;
	}

}

//------------------------------------------------------------------------------

void BWindow::GetSizeLimits(float *minWidth, float *maxWidth, 
							float *minHeight, float *maxHeight){
	*minHeight			= fMinWindHeight;
	*minWidth			= fMinWindWidth;
	*maxHeight			= fMaxWindHeight;
	*maxWidth			= fMaxWindWidth;
}

//------------------------------------------------------------------------------

void BWindow::SetZoomLimits(float maxWidth, float maxHeight){
	if (maxWidth > fMaxWindWidth)
		maxWidth	= fMaxWindWidth;
	else
		fMaxZoomWidth		= maxWidth;

	if (maxHeight > fMaxWindHeight)
		maxHeight	= fMaxWindHeight;
	else
		fMaxZoomHeight		= maxHeight;
}

//------------------------------------------------------------------------------

void BWindow::Zoom(	BPoint rec_position, float rec_width, float rec_height){

		// this is also a Hook function!
		
	MoveTo( rec_position );
	ResizeTo( rec_width, rec_height );
}

//------------------------------------------------------------------------------

void BWindow::Zoom(){
	float			minWidth, minHeight;
	BScreen			screen;
/*	from BeBook:
	However, if the window's rectangle already matches these "zoom" dimensions
	(give or take a few pixels), Zoom() passes the window's previous
	("non-zoomed") size and location. (??????)
*/
	if (Frame().Width() == fMaxZoomWidth && Frame().Height() == fMaxZoomHeight) {
		BPoint position( Frame().left, Frame().top);
		Zoom( position, fMaxZoomWidth, fMaxZoomHeight );
		return;
	}
	
/*	from BeBook:
	The dimensions that non-virtual Zoom() passes to hook Zoom() are deduced from
	the smallest of three rectangles: 3) the screen rectangle, 1) the rectangle
	defined by SetZoomLimits(), 2) the rectangle defined by SetSizeLimits()
*/
		// 1
	minHeight		= fMaxZoomHeight;
	minWidth		= fMaxZoomWidth;

		// 2
	if ( fMaxWindHeight < minHeight ) { minHeight		= fMaxWindHeight; }
	if ( fMaxWindWidth  < minWidth  ) { minWidth		= fMaxWindWidth; }

		// 3
	if ( screen.Frame().Width()  < minWidth )   { minWidth		= screen.Frame().Width(); }
	if ( screen.Frame().Height() < minHeight  ) { minHeight		= screen.Frame().Height(); }

	Zoom( Frame().LeftTop(), minWidth, minHeight );
}

//------------------------------------------------------------------------------

void BWindow::ScreenChanged(BRect screen_size, color_space depth){
	// Hook function
	// does nothing
}

//------------------------------------------------------------------------------

void BWindow::SetPulseRate(bigtime_t rate){
	if ( rate < 0 )
		return;

	if (fPulseRate == 0 && !fPulseEnabled){
		fPulseRunner	= new BMessageRunner(	BMessenger(this),
												new BMessage( B_PULSE ),
												rate);
		fPulseRate		= rate;
		fPulseEnabled	= true;

		return;
	}

	if (rate == 0 && fPulseEnabled){
		delete			fPulseRunner;
		fPulseRunner	= NULL;

		fPulseRate		= rate;
		fPulseEnabled	= false;

		return;
	}

	fPulseRunner->SetInterval( rate );
}

//------------------------------------------------------------------------------

bigtime_t BWindow::PulseRate() const{
	return fPulseRate;
}

//------------------------------------------------------------------------------

void BWindow::AddShortcut(	uint32 key,	uint32 modifiers, BMessage* msg){
	AddShortcut( key, modifiers, msg, this);
}

//------------------------------------------------------------------------------

void BWindow::AddShortcut(	uint32 key,	uint32 modifiers, BMessage* msg, BHandler* target){
/*
	NOTE: I'm not sure if it is OK to use 'key'
*/
	if ( !msg )
		return;

	int64				when;
	_BCmdKey			*cmdKey;

	when				= real_time_clock_usecs();
	msg->AddInt64("when", when);

// TODO:	make sure key is a lowercase char !!!

	modifiers			= modifiers | B_COMMAND_KEY;

	cmdKey				= new _BCmdKey;
	cmdKey->key			= key;
	cmdKey->modifiers	= modifiers;
	cmdKey->message		= msg;
	if (target == NULL)
		cmdKey->targetToken	= B_ANY_TOKEN;
	else
		cmdKey->targetToken	= _get_object_token_(target);

		// removes the shortcut from accelList if it exists!
	RemoveShortcut( key, modifiers );

	accelList.AddItem( cmdKey );

}

//------------------------------------------------------------------------------

void BWindow::RemoveShortcut(uint32 key, uint32 modifiers){
	int32				index;
	
	modifiers			= modifiers | B_COMMAND_KEY;

	index				= findShortcut( key, modifiers );
	if ( index >=0 ) {
		_BCmdKey		*cmdKey;

		cmdKey			= (_BCmdKey*)accelList.ItemAt( index );

		accelList.RemoveItem(index);
		
		delete cmdKey->message;
		delete cmdKey;
	}
}

//------------------------------------------------------------------------------

BButton* BWindow::DefaultButton() const{
	return fDefaultButton;
}

//------------------------------------------------------------------------------

void BWindow::SetDefaultButton(BButton* button){
/*
Note: for developers!
	He he, if you really want to understand what is happens here, take a piece of
		paper and start taking possible values and then walk with them through
		the code.
*/
	BButton				*aux;

	if ( fDefaultButton == button )
		return;

	if ( fDefaultButton ){
		aux				= fDefaultButton;
		fDefaultButton	= NULL;
		aux->MakeDefault( false );
		aux->Invalidate();
	}
	
	if ( button == NULL ){
		fDefaultButton		= NULL;
		return;
	}
	
	fDefaultButton			= button;
	fDefaultButton->MakeDefault( true );
	fDefaultButton->Invalidate();
}

//------------------------------------------------------------------------------

bool BWindow::NeedsUpdate() const{
	PortLink::ReplyData		replyData;

	serverLink->SetOpCode( AS_NEEDS_UPDATE );

	const_cast<BWindow*>(this)->Lock();	
	serverLink->FlushWithReply( &replyData );
	const_cast<BWindow*>(this)->Unlock();
	
	delete replyData.buffer;
	
	return replyData.code == SERVER_TRUE;
}

//------------------------------------------------------------------------------

void BWindow::UpdateIfNeeded(){
		// works only from this thread
	if (find_thread(NULL) == Thread()){
		Flush();
		drawAllViews( top_view );
	}
}

//------------------------------------------------------------------------------

BView* BWindow::FindView(const char* viewName) const{

	return findView( top_view, viewName );
}

//------------------------------------------------------------------------------

BView* BWindow::FindView(BPoint point) const{

	return findView( top_view, point );
}

//------------------------------------------------------------------------------

BView* BWindow::CurrentFocus() const{
	return fFocus;
}

//------------------------------------------------------------------------------

void BWindow::Activate(bool active){
	if (IsHidden())
		return;

	serverLink->SetOpCode( AS_ACTIVATE_WINDOW );
	serverLink->Attach( active );
	
	Lock();
	serverLink->Flush( );
	Unlock();
}

//------------------------------------------------------------------------------

void BWindow::WindowActivated(bool state){
	// hook function
	// does nothing
}

//------------------------------------------------------------------------------

void BWindow::ConvertToScreen(BPoint* pt) const{
	pt->x			+= fFrame.left;
	pt->y			+= fFrame.top;
}

//------------------------------------------------------------------------------

BPoint BWindow::ConvertToScreen(BPoint pt) const{
	pt.x			+= fFrame.left;
	pt.y			+= fFrame.top;

	return pt;
}

//------------------------------------------------------------------------------

void BWindow::ConvertFromScreen(BPoint* pt) const{
	pt->x			-= fFrame.left;
	pt->y			-= fFrame.top;
}

//------------------------------------------------------------------------------

BPoint BWindow::ConvertFromScreen(BPoint pt) const{
	pt.x			-= fFrame.left;
	pt.y			-= fFrame.top;

	return pt;
}

//------------------------------------------------------------------------------

void BWindow::ConvertToScreen(BRect* rect) const{
	rect->top			+= fFrame.top;
	rect->left			+= fFrame.left;
	rect->bottom		+= fFrame.top;
	rect->right			+= fFrame.left;
}

//------------------------------------------------------------------------------

BRect BWindow::ConvertToScreen(BRect rect) const{
	rect.top			+= fFrame.top;
	rect.left			+= fFrame.left;
	rect.bottom			+= fFrame.top;
	rect.right			+= fFrame.left;

	return rect;
}

//------------------------------------------------------------------------------

void BWindow::ConvertFromScreen(BRect* rect) const{
	rect->top			-= fFrame.top;
	rect->left			-= fFrame.left;
	rect->bottom		-= fFrame.top;
	rect->right			-= fFrame.left;
}

//------------------------------------------------------------------------------

BRect BWindow::ConvertFromScreen(BRect rect) const{
	rect.top			-= fFrame.top;
	rect.left			-= fFrame.left;
	rect.bottom			-= fFrame.top;
	rect.right			-= fFrame.left;

	return rect;
}

//------------------------------------------------------------------------------

bool BWindow::IsMinimized() const{
		// Hiding takes precendence over minimization!!!
	if ( IsHidden() )
		return false;

	return fMinimized;
}

//------------------------------------------------------------------------------

BRect BWindow::Bounds() const{
	BRect			bounds( 0.0, 0.0, fFrame.Width(), fFrame.Height() );
	return bounds;
}

//------------------------------------------------------------------------------

BRect BWindow::Frame() const{
	return fFrame;
}

//------------------------------------------------------------------------------

const char* BWindow::Title() const{
	return fTitle;
}

//------------------------------------------------------------------------------

void BWindow::SetTitle(const char* title){
	if (!title)
		return;

	if (fTitle){
		delete fTitle;
		fTitle = NULL;
	}
	
	fTitle		= strdup( title );

		// we will change BWindow's thread name to "w>window_title"	
	int32		length;
	length		= strlen( fTitle );
	
	char		*threadName;
	threadName	= new char[32];
	strcpy(threadName, "w>");
	strncat(threadName, fTitle, (length>=29) ? 29: length);

		// if the message loop has been started...
	if (Thread() != B_ERROR ){
		SetName( threadName );
		rename_thread( Thread(), threadName );

			// we notify the app_server so we can actually see the change
		serverLink->SetOpCode( AS_WINDOW_TITLE);
		serverLink->Attach( fTitle, strlen(fTitle)+1 );
	
		Lock();
		serverLink->Flush( );
		Unlock();
	}
	else
		SetName( threadName );

}

//------------------------------------------------------------------------------

bool BWindow::IsActive() const{
	return fActive;
}

//------------------------------------------------------------------------------

void BWindow::SetKeyMenuBar(BMenuBar* bar){
	fKeyMenuBar			= bar;
}

//------------------------------------------------------------------------------

BMenuBar* BWindow::KeyMenuBar() const{
	return fKeyMenuBar;
}

//------------------------------------------------------------------------------

bool BWindow::IsModal() const{
	if ( fFeel == B_MODAL_SUBSET_WINDOW_FEEL)
		return true;
	if ( fFeel == B_MODAL_APP_WINDOW_FEEL)
		return true;
	if ( fFeel == B_MODAL_ALL_WINDOW_FEEL)
		return true;

	return false;

}

//------------------------------------------------------------------------------

bool BWindow::IsFloating() const{
	if ( fFeel == B_FLOATING_SUBSET_WINDOW_FEEL)
		return true;
	if ( fFeel == B_FLOATING_APP_WINDOW_FEEL)
		return true;
	if ( fFeel == B_FLOATING_ALL_WINDOW_FEEL)
		return true;

	return false;
}

//------------------------------------------------------------------------------

status_t BWindow::AddToSubset(BWindow* window){
	if ( !window )
			return B_ERROR;

	if (window->Feel() == B_MODAL_SUBSET_WINDOW_FEEL ||
		window->Feel() == B_FLOATING_SUBSET_WINDOW_FEEL){
		
		PortLink::ReplyData		replyData;

		serverLink->SetOpCode( AS_ADD_TO_SUBSET );
		serverLink->Attach( _get_object_token_(window) );
		
		Lock();
		serverLink->FlushWithReply( &replyData );
		Unlock();
		
		delete replyData.buffer;

		return replyData.code == SERVER_TRUE? B_OK : B_ERROR;
	}

	return B_ERROR;
}

//------------------------------------------------------------------------------

status_t BWindow::RemoveFromSubset(BWindow* window){
	if ( !window )
			return B_ERROR;

	PortLink::ReplyData		replyData;

	serverLink->SetOpCode( AS_REM_FROM_SUBSET );
	serverLink->Attach( _get_object_token_(window) );
		
	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();

	delete replyData.buffer;

	return replyData.code == SERVER_TRUE? B_OK : B_ERROR;
}

//------------------------------------------------------------------------------

status_t BWindow::Perform(perform_code d, void* arg){
	return BLooper::Perform( d, arg );
}

//------------------------------------------------------------------------------

status_t BWindow::SetType(window_type type){
	decomposeType(type, &fLook, &fFeel);
	SetLook( fLook );
	SetFeel( fFeel );
}

//------------------------------------------------------------------------------

window_type	BWindow::Type() const{
	return composeType( fLook, fFeel );
}

//------------------------------------------------------------------------------

status_t BWindow::SetLook(window_look look){

	uint32					uintLook;
	PortLink::ReplyData		replyData;
	
	uintLook		= WindowLookToInteger( look );

	serverLink->SetOpCode( AS_SET_LOOK );
	serverLink->Attach( &uintLook, sizeof( uint32 ) );
		
	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();
	
	delete replyData.buffer;

	if (replyData.code == SERVER_TRUE){
		fLook		= look;
		return B_OK;
	}
	else
		return B_ERROR;
}

//------------------------------------------------------------------------------

window_look	BWindow::Look() const{
	return fLook;
}

//------------------------------------------------------------------------------

status_t BWindow::SetFeel(window_feel feel){

/* TODO:	See what happens when a window that is part of a subset, changes its
			feel!? should it be removed from the subset???
*/
	uint32					uintFeel;
	PortLink::ReplyData		replyData;
	
	uintFeel		= WindowFeelToInteger( feel );

	serverLink->SetOpCode( AS_SET_FEEL );
	serverLink->Attach( &uintFeel, sizeof( uint32 ) );
		
	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();
	
	delete replyData.buffer;

	if (replyData.code == SERVER_TRUE){
		fFeel		= feel;
		return B_OK;
	}
	else
		return B_ERROR;
}

//------------------------------------------------------------------------------

window_feel	BWindow::Feel() const{
	return fFeel;
}

//------------------------------------------------------------------------------

status_t BWindow::SetFlags(uint32 flags){
	PortLink::ReplyData		replyData;
	
	serverLink->SetOpCode( AS_SET_FLAGS );
	serverLink->Attach( &flags, sizeof( uint32 ) );
		
	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();
	
	delete replyData.buffer;

	if (replyData.code == SERVER_TRUE){
		fFlags		= flags;
		return B_OK;
	}
	else
		return B_ERROR;
}

//------------------------------------------------------------------------------

uint32	BWindow::Flags() const{
	return fFlags;
}

//------------------------------------------------------------------------------

status_t BWindow::SetWindowAlignment(window_alignment mode,
											int32 h, int32 hOffset = 0,
											int32 width = 0, int32 widthOffset = 0,
											int32 v = 0, int32 vOffset = 0,
											int32 height = 0, int32 heightOffset = 0)
{
	if ( !(	(mode && B_BYTE_ALIGNMENT) ||
			(mode && B_PIXEL_ALIGNMENT) ) )
	{
		return B_ERROR;
	}

	if ( 0 <= hOffset && hOffset <=h )
		return B_ERROR;

	if ( 0 <= vOffset && vOffset <=v )
		return B_ERROR;

	if ( 0 <= widthOffset && widthOffset <=width )
		return B_ERROR;

	if ( 0 <= heightOffset && heightOffset <=height )
		return B_ERROR;

// TODO: test if hOffset = 0 and set it to 1 if true.

	PortLink::ReplyData		replyData;

	serverLink->SetOpCode( AS_SET_ALIGNMENT );
	serverLink->Attach( (int32)mode );
	serverLink->Attach( h );
	serverLink->Attach( hOffset );
	serverLink->Attach( width );
	serverLink->Attach( widthOffset );
	serverLink->Attach( v );
	serverLink->Attach( vOffset );
	serverLink->Attach( height );
	serverLink->Attach( heightOffset );

	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();
	
	delete replyData.buffer;
	
	if ( replyData.code == SERVER_TRUE){
		return B_NO_ERROR;
	}

	return B_ERROR;
}

//------------------------------------------------------------------------------

status_t BWindow::GetWindowAlignment(window_alignment* mode = NULL,
											int32* h = NULL, int32* hOffset = NULL,
											int32* width = NULL, int32* widthOffset = NULL,
											int32* v = NULL, int32* vOffset = NULL,
											int32* height = NULL, int32* heightOffset = NULL) const
{
	PortLink::ReplyData		replyData;
	int8					*rb;		// short for: replybuffer

	serverLink->SetOpCode( AS_GET_ALIGNMENT );
	
	const_cast<BWindow*>(this)->Lock();
	serverLink->FlushWithReply( &replyData );
	const_cast<BWindow*>(this)->Unlock();
	
	if (replyData.code == SERVER_TRUE){
		rb				= replyData.buffer;
		if (mode)
			*mode			= *((window_alignment*)*((int32*)rb));	rb += sizeof(int32);
		if (h)
			*h				= *((int32*)rb);		rb += sizeof(int32);
		if (hOffset)
			*hOffset		= *((int32*)rb);		rb += sizeof(int32);
		if (width)
			*width			= *((int32*)rb);		rb += sizeof(int32);
		if (widthOffset)
			*widthOffset	= *((int32*)rb);		rb += sizeof(int32);
		if (v)
			*v				= *((int32*)rb);		rb += sizeof(int32);
		if (vOffset)
			*vOffset		= *((int32*)rb);		rb += sizeof(int32);
		if (height)
			*height			= *((int32*)rb);		rb += sizeof(int32);
		if (heightOffset)
			*heightOffset	= *((int32*)rb);

		delete replyData.buffer;

		return B_NO_ERROR;
	}
	
	return B_ERROR;
}

//------------------------------------------------------------------------------

uint32 BWindow::Workspaces() const{
	PortLink::ReplyData	replyData;
	uint32					workspaces;

	serverLink->SetOpCode( AS_GET_WORKSPACES );
	
	const_cast<BWindow*>(this)->Lock();
	serverLink->FlushWithReply( &replyData );
	const_cast<BWindow*>(this)->Unlock();
	
	if (replyData.code == SERVER_TRUE){
		workspaces		= *((uint32*)replyData.buffer);
		
		delete replyData.buffer;

		return workspaces;
	}
	
	return B_CURRENT_WORKSPACE;
}

//------------------------------------------------------------------------------

void BWindow::SetWorkspaces(uint32 workspaces){
//	PortLink::ReplyData			replyData;

	serverLink->SetOpCode( AS_SET_WORKSPACES );
	serverLink->Attach( (int32)workspaces );
	
	Lock();
	serverLink->Flush( );
//	serverLink->FlushWithReply( &replyData );
	Unlock();

//	delete replyData.buffer;
/*
Note:	In future versions of OBOS we should make SetWorkspaces(uint32) return
			a status value! The following code should be added
			
	if (replyData.code == SERVER_TRUE)
		return B_OK;
		
	return B_ERROR;
*/
}

//------------------------------------------------------------------------------

BView* BWindow::LastMouseMovedView() const{
	return fLastMouseMovedView;
}

//------------------------------------------------------------------------------

void BWindow::MoveBy(float dx, float dy){

	BPoint			offset( dx, dy );

	MoveTo( fFrame.LeftTop() + offset );
}

//------------------------------------------------------------------------------

void BWindow::MoveTo( BPoint point ){
	MoveTo( point.x, point.y );
}

//------------------------------------------------------------------------------

void BWindow::MoveTo(float x, float y){

	serverLink->SetOpCode( AS_WINDOW_MOVE );
	serverLink->Attach( x );
	serverLink->Attach( y );
	
	Lock();
	serverLink->Flush();
	Unlock();
}

//------------------------------------------------------------------------------

void BWindow::ResizeBy(float dx, float dy){

		// stay in minimum & maximum frame limits
	(fFrame.Width() + dx) < fMinWindWidth ? fMinWindWidth : fFrame.Width() + dx;
	(fFrame.Width() + dx) > fMaxWindWidth ? fMaxWindWidth : fFrame.Width() + dx;

	(fFrame.Height() + dy) < fMinWindHeight ? fMinWindHeight : fFrame.Height() + dy;
	(fFrame.Height() + dy) > fMaxWindHeight ? fMaxWindHeight : fFrame.Height() + dy;

	ResizeTo( fFrame.Width() + dx, fFrame.Height() + dy );
}

//------------------------------------------------------------------------------

void BWindow::ResizeTo(float width, float height){

		// stay in minimum & maximum frame limits
	width < fMinWindWidth ? fMinWindWidth : width;
	width > fMaxWindWidth ? fMaxWindWidth : width;

	height < fMinWindHeight ? fMinWindHeight : height;
	height > fMaxWindHeight ? fMaxWindHeight : height;

	serverLink->SetOpCode( AS_WINDOW_RESIZE );
	serverLink->Attach( width );
	serverLink->Attach( height );
	
	Lock();
	serverLink->Flush( );
	Unlock();
}

//------------------------------------------------------------------------------

void BWindow::Show(){
	if ( Thread() == B_ERROR )
		Run();

	fShowLevel--;

	if (fShowLevel == 0){
		serverLink->SetOpCode( AS_SHOW_WINDOW );
		
		Lock();
		serverLink->Flush( );
		Unlock();
	}
}

//------------------------------------------------------------------------------

void BWindow::Hide(){
	if (fShowLevel == 0){
		serverLink->SetOpCode( AS_HIDE_WINDOW );

		Lock();
		serverLink->Flush();
		Unlock();
	}
	fShowLevel++;
}

//------------------------------------------------------------------------------

bool BWindow::IsHidden() const{
	return fShowLevel > 0; 
}

//------------------------------------------------------------------------------

bool BWindow::QuitRequested(){
	return BLooper::QuitRequested();
}

//------------------------------------------------------------------------------

thread_id BWindow::Run(){
	return BLooper::Run();
}

//------------------------------------------------------------------------------

status_t BWindow::GetSupportedSuites(BMessage* data){
	status_t err = B_OK;
	if (!data)
		err = B_BAD_VALUE;

	if (!err){
		err = data->AddString("Suites", "suite/vnd.Be-window");
		if (!err){
			BPropertyInfo propertyInfo(windowPropInfo);
			err = data->AddFlat("message", &propertyInfo);
			if (!err){
				err = BLooper::GetSupportedSuites(data);
			}
		}
	}
	return err;
}

//------------------------------------------------------------------------------

BHandler* BWindow::ResolveSpecifier(BMessage* msg, int32 index,	BMessage* specifier,
										int32 what,	const char* property)
{
	if (msg->what == B_WINDOW_MOVE_BY)
		return this;
	if (msg->what == B_WINDOW_MOVE_TO)
		return this;

	BPropertyInfo propertyInfo(windowPropInfo);
	switch (propertyInfo.FindMatch(msg, index, specifier, what, property))
	{
		case B_ERROR:
			break;
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
			return this;
			
		case 14:
			if (fKeyMenuBar){
				msg->PopSpecifier();
				return fKeyMenuBar;
			}
			else{
				BMessage		replyMsg(B_MESSAGE_NOT_UNDERSTOOD);
				replyMsg.AddInt32( "error", B_NAME_NOT_FOUND );
				replyMsg.AddString( "message", "This window doesn't have a main MenuBar");
				msg->SendReply( &replyMsg );
				return NULL;
			}
		case 15:
				// we will NOT pop the current specifier
			return top_view;
			
		case 16:
			return this;
	}

	return BLooper::ResolveSpecifier(msg, index, specifier, what, property);
}

// PRIVATE
//--------------------Private Methods-------------------------------------------
// PRIVATE

void BWindow::InitData(	BRect frame,
						const char* title,
						window_look look,
						window_feel feel,
						uint32 flags,
						uint32 workspace){

	if ( be_app == NULL ){
		//debugger("You need a valid BApplication object before interacting with the app_server");
		return;
	}

// TODO: what should I do if frame rect is invalid?
	if ( frame.IsValid() )
		fFrame			= frame;
	else
		frame.Set( frame.left, frame.top, frame.left+320, frame.top+240 );
	
	if (title)
		SetTitle( title );
	else
		SetTitle( "no_name_window" );

	fFeel			= feel;
	fLook			= look;
	fFlags			= flags;
	

	fInTransaction	= false;
	fActive			= false;
	fShowLevel		= 1;

	top_view		= NULL;
	fFocus			= NULL;
	fLastMouseMovedView	= NULL;
	fKeyMenuBar		= NULL;
	fDefaultButton	= NULL;

//	accelList		= new BList( 10 );
	AddShortcut('X', B_COMMAND_KEY, new BMessage(B_CUT), NULL);
	AddShortcut('C', B_COMMAND_KEY, new BMessage(B_COPY), NULL);
	AddShortcut('V', B_COMMAND_KEY, new BMessage(B_PASTE), NULL);
	AddShortcut('A', B_COMMAND_KEY, new BMessage(B_SELECT_ALL), NULL);
	AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));

	fPulseEnabled	= false;
	fPulseRate		= 0;
	fPulseRunner	= NULL;

// TODO: is this correct??? should the thread loop be started???
	SetPulseRate( 500000 );

// TODO:  see if you can use 'fViewsNeedPulse'

	fIsFilePanel	= false;

// TODO: see WHEN is this used!
	fMaskActivated	= false;

// TODO: see WHEN is this used!
	fWaitingForMenu	= false;

	fMinimized		= false;

// TODO:  see WHERE you can use 'fMenuSem'

	fMaxZoomHeight	= 32768.0;
	fMaxZoomWidth	= 32768.0;
	fMinWindHeight	= 0.0;
	fMinWindWidth	= 0.0;
	fMaxWindHeight	= 32768.0;
	fMaxWindWidth	= 32768.0;

// TODO: other initializations!

/*
	Here, we will contact app_server and let him that a window has been created
*/
	receive_port	= create_port( B_LOOPER_PORT_DEFAULT_CAPACITY ,
						"w_rcv_port");
	if (receive_port==B_BAD_VALUE || receive_port==B_NO_MORE_PORTS){
		//debugger("Could not create BWindow's receive port, used for interacting with the app_server!");
		return;
	}
	

		// let app_server to know that a window has been created.
	serverLink		= new PortLink(be_app->fServerTo);
	status_t		replyStat;
	PortLink::ReplyData		replyData;

	serverLink->SetOpCode(AS_CREATE_WINDOW);
	serverLink->Attach(fFrame);
	serverLink->Attach((int32)WindowLookToInteger(fLook));
	serverLink->Attach((int32)WindowFeelToInteger(fFeel));
	serverLink->Attach((int32)fFlags);
	serverLink->Attach((int32)workspace);
	serverLink->Attach((int32)_get_object_token_(this));
	serverLink->Attach(&receive_port,sizeof(port_id));
		// We add one so that the string will end up NULL-terminated.
	serverLink->Attach( (char*)title, strlen(title)+1 );

		// HERE we are in BApplication's thread, so for locking we use be_app variable
		// we'll lock the be_app to be sure we're the only one writing at BApplication's server port
	be_app->Lock();

		// Send and wait for ServerWindow port. Necessary here so we can respond to
		// messages as soon as Show() is called.
	replyStat		= serverLink->FlushWithReply( &replyData );
	if ( replyStat != B_OK ){
		//debugger("First reply from app_server was not received.");
		return;
	}
		// unlock, so other threads can do their job.
	be_app->Unlock();
	
	send_port		= *((port_id*)replyData.buffer);

		// Set the port on witch app_server will listen for us	
	serverLink->SetPort(send_port);
		// Initialize a PortLink object for use with graphic calls.
		// They need to be sent separately because of the speed reasons.
	srvGfxLink		= new PortLink( send_port );	
	
	delete replyData.buffer;

		// Create and attach the top view
	top_view			= buildTopView();
		// Here we will register the top view with app_server
// TODO: implement the following function
	attachTopView( );
}

//------------------------------------------------------------------------------

void BWindow::task_looper(){

	//	Check that looper is locked (should be)
	AssertLocked();

	Unlock();
	
	using namespace BPrivate;
	bool		dispatchNextMessage = false;
	BMessage	*msg;

	//	loop: As long as we are not terminating.
	while (!fTerminating)
	{

		// get BMessages from app_server
		while ( msg = ReadMessageFromPort(0) ){
			fQueue->Lock();
			fQueue->AddMessage(msg);
			fQueue->Unlock();
			dispatchNextMessage = true;
		}

		// get BMessages from BLooper port
		while ( msg = MessageFromPort(0) ){
			fQueue->Lock();
			fQueue->AddMessage(msg);
			fQueue->Unlock();
			dispatchNextMessage = true;
		}

		//	loop: As long as there are messages in the queue and
		//		  and we are not terminating.
		while (!fTerminating && dispatchNextMessage)
		{
			fQueue->Lock();
			fLastMessage		= fQueue->NextMessage();
			fQueue->Unlock();

			Lock();
			if (!fLastMessage)
			{
				// No more messages: Unlock the looper and terminate the
				// dispatch loop.
				dispatchNextMessage = false;
			}
			else
			{
				//	Get the target handler
				BHandler* handler;
				if (_use_preferred_target_(fLastMessage))
					handler = fPreferred;
				else
					gDefaultTokens.GetToken(_get_message_target_(fLastMessage),
					 						 B_HANDLER_TOKEN,
					 						 (void**)&handler);

				if (!handler)
					handler = this;

				//	Is this a scripting message?
				if (fLastMessage->HasSpecifiers())
				{
					int32 index = 0;
					// Make sure the current specifier is kosher
					if (fLastMessage->GetCurrentSpecifier(&index) == B_OK)
						handler = resolve_specifier(handler, fLastMessage);
				}
				
				if (handler)
				{
					//	Do filtering
					handler = top_level_filter(fLastMessage, handler);

					if (handler && handler->Looper() == this)
						DispatchMessage(fLastMessage, handler);
				}
			}

			Unlock();

			//	Delete the current message (fLastMessage)
			if (fLastMessage)
			{
				delete fLastMessage;
				fLastMessage = NULL;
			}
		}
	}

}

//------------------------------------------------------------------------------

BMessage* BWindow::ReadMessageFromPort(bigtime_t tout){
	int32			msgcode;
	BMessage*		msg;
	uint8*			msgbuffer;

	msgbuffer		= ReadRawFromPort(&msgcode, tout);

	if (msgcode != B_ERROR)
		msg			= ConvertToMessage(msgbuffer, msgcode);

	if (msgbuffer)
		delete msgbuffer;

	return msg;
}

//------------------------------------------------------------------------------

uint8* BWindow::ReadRawFromPort(int32* code, bigtime_t tout){

	uint8*			msgbuffer = NULL;
	ssize_t			buffersize;
	ssize_t			bytesread;

		// we NEVER have to use B_INFINITE_TIMEOUT
	if (tout == B_INFINITE_TIMEOUT)
		tout		= 0;

	buffersize = port_buffer_size_etc(receive_port, B_TIMEOUT, tout);
	if (buffersize == B_TIMED_OUT || buffersize == B_BAD_PORT_ID ||
		buffersize == B_WOULD_BLOCK)
	{
		*code = B_ERROR;
		return NULL;
	}


	if (buffersize > 0){
		msgbuffer = new uint8[buffersize];

		read_port_etc(	receive_port, code, msgbuffer,
						buffersize, B_TIMEOUT, tout);
	}

	return msgbuffer;
}

//------------------------------------------------------------------------------

window_type BWindow::composeType(window_look look,	
								 window_feel feel) const
{
	window_type returnValue;

	switch(feel)
	{
	case B_NORMAL_WINDOW_FEEL:
		switch (look)
		{
		case B_TITLED_WINDOW_LOOK:
			returnValue = B_TITLED_WINDOW;
			break;

		case B_DOCUMENT_WINDOW_LOOK:
			returnValue = B_DOCUMENT_WINDOW;
			break;

		case B_BORDERED_WINDOW_LOOK:
			returnValue = B_BORDERED_WINDOW;
			break;
		
		default:
			returnValue = B_UNTYPED_WINDOW;
		}
		break;

	case B_MODAL_APP_WINDOW_FEEL:
		if (look == B_MODAL_WINDOW_LOOK)
			returnValue = B_MODAL_WINDOW;
		break;

	case B_FLOATING_APP_WINDOW_FEEL:
		if (look == B_FLOATING_WINDOW_LOOK)
			returnValue = B_FLOATING_WINDOW;
		break;

	default:
		returnValue = B_UNTYPED_WINDOW;
	}
	
	return returnValue;
}

//------------------------------------------------------------------------------

void BWindow::decomposeType(window_type type, 
							   window_look* look,
							   window_feel* feel) const
{
	switch (type)
	{
	case B_TITLED_WINDOW:
		*look = B_TITLED_WINDOW_LOOK;
		*feel = B_NORMAL_WINDOW_FEEL;
		break;
	case B_DOCUMENT_WINDOW:
		*look = B_DOCUMENT_WINDOW_LOOK;
		*feel = B_NORMAL_WINDOW_FEEL;
		break;
	case B_MODAL_WINDOW:
		*look = B_MODAL_WINDOW_LOOK;
		*feel = B_MODAL_APP_WINDOW_FEEL;
		break;
	case B_FLOATING_WINDOW:
		*look = B_FLOATING_WINDOW_LOOK;
		*feel = B_FLOATING_APP_WINDOW_FEEL;
		break;
	case B_BORDERED_WINDOW:
		*look = B_BORDERED_WINDOW_LOOK;
		*feel = B_NORMAL_WINDOW_FEEL;

	case B_UNTYPED_WINDOW:
		*look = B_TITLED_WINDOW_LOOK;
		*feel = B_NORMAL_WINDOW_FEEL;

	default:
		*look = B_TITLED_WINDOW_LOOK;
		*feel = B_NORMAL_WINDOW_FEEL;
	}
}

//------------------------------------------------------------------------------

uint32 BWindow::WindowLookToInteger(window_look wl)
{
	switch(wl)
	{
		case B_BORDERED_WINDOW_LOOK:
			return 1;
		case B_TITLED_WINDOW_LOOK:
			return 2;
		case B_DOCUMENT_WINDOW_LOOK:
			return 3;
		case B_MODAL_WINDOW_LOOK:
			return 4;
		case B_FLOATING_WINDOW_LOOK:
			return 5;
		case B_NO_BORDER_WINDOW_LOOK:
		default:
			return 0;
	}
}

//------------------------------------------------------------------------------

uint32 BWindow::WindowFeelToInteger(window_feel wf)
{
	switch(wf)
	{
		case B_MODAL_SUBSET_WINDOW_FEEL:
			return 1;
		case B_MODAL_APP_WINDOW_FEEL:
			return 2;
		case B_MODAL_ALL_WINDOW_FEEL:
			return 3;
		case B_FLOATING_SUBSET_WINDOW_FEEL:
			return 4;
		case B_FLOATING_APP_WINDOW_FEEL:
			return 5;
		case B_FLOATING_ALL_WINDOW_FEEL:
			return 6;

		case B_NORMAL_WINDOW_FEEL:
		default:
			return 0;
	}
}

//------------------------------------------------------------------------------

BView* BWindow::buildTopView(){
	BView			*topView;

	topView					= new BView( fFrame.OffsetToCopy(0,0), "top_view",
										 B_FOLLOW_ALL, B_WILL_DRAW);
	topView->owner			= this;
	topView->attached		= true;
	topView->top_level_view	= true;

/* Note:
		I don't think adding top_view to BLooper's list
		of eligible handlers is a good idea!
*/

	return topView;
}

//------------------------------------------------------------------------------

void BWindow::attachTopView(){

// TODO: implement after you have a messaging protocol with app_server

	serverLink->SetOpCode( AS_LAYER_CREATE_ROOT );
	serverLink->Attach( _get_object_token_( top_view ) ); // no need for that!
/*	serverLink->Attach( top_view->Name() );
	serverLink->Attach( fCachedBounds.OffsetToCopy( origin_h, origin_v ) );
	serverLink->Attach( ...ResizeMode... );
	serverLink->Attach( top_view->fFlags );
	serverLink->Attach( top_view->fShowLevel );
*/
	Lock();
	serverLink->Flush();
	Unlock();

}

//------------------------------------------------------------------------------

void BWindow::detachTopView(){

// TODO: detach all views

	serverLink->SetOpCode( AS_LAYER_DELETE_ROOT );
	serverLink->Attach( _get_object_token_( top_view ) ); // no need for that!

	Lock();
	serverLink->Flush();
	Unlock();

	delete top_view;
}

//------------------------------------------------------------------------------

void BWindow::stopConnection(){
	PortLink::ReplyData		replyData;

	serverLink->SetOpCode( AS_QUIT_WINDOW );
	
	Lock();
	serverLink->FlushWithReply( &replyData );
	Unlock();
	
	delete replyData.buffer;
}

//------------------------------------------------------------------------------

void BWindow::prepareView(BView *aView){

// TODO: implement

}

//------------------------------------------------------------------------------

void BWindow::attachView(BView *aView){

// TODO: implement

}

//------------------------------------------------------------------------------

void BWindow::detachView(BView *aView){

// TODO: implement

}
//------------------------------------------------------------------------------

void BWindow::setFocus(BView *focusView, bool notifyInputServer){

	fFocus			= NULL;
	fFocus->Draw( fFocus->Bounds() );

	fFocus			= focusView;
	fFocus->Draw( fFocus->Bounds() );

// TODO: find out why do we have to notify input server.
	if (notifyInputServer){
		// what am I suppose to do here??
	}
}

//------------------------------------------------------------------------------

void BWindow::handleActivation( bool active ){

	if (active){
// TODO: talk to Ingo to make BWindow a friend for BRoster	
//		be_roster->UpdateActiveApp( be_app->Team() );
	}

	WindowActivated( active );

		// recursively call hook function 'WindowActivated(bool)'
		//    for all views attached to this window.
	activateView( top_view, active );
}

//------------------------------------------------------------------------------

void BWindow::activateView( BView *aView, bool active ){

	aView->WindowActivated( active );

	BView		*child;
	if ( child = aView->first_child ){
		while ( child ) {
			activateView( child, active );
			child 		= child->next_sibling; 
		}
	}
}

//------------------------------------------------------------------------------

bool BWindow::handleKeyDown( int32 raw_char, uint32 modifiers){

// TODO: ask people if using 'raw_char' is OK ?

		// handle BMenuBar key
	if ( (raw_char == B_ESCAPE) && (modifiers & B_COMMAND_KEY)
		&& fKeyMenuBar)
	{

// TODO: ask Marc about 'fWaitingForMenu' member!

		// fWaitingForMenu		= true;
		fKeyMenuBar->StartMenuBar(0, true, false, NULL);
		return true;
	}

		// Command+q has been pressed, so, we will quit
	if ( (raw_char == 'Q' || raw_char == 'q') && modifiers & B_COMMAND_KEY){
		be_app->PostMessage(B_QUIT_REQUESTED); 
		return true;
	}

		// Keyboard navigation through views!!!!
	if ( raw_char == B_TAB){

			// even if we have no focus view, we'll say that we will handle TAB key
		if (!fFocus)
			return true;

		BView			*nextFocus;

		if (modifiers & B_CONTROL_KEY & B_SHIFT_KEY){
			nextFocus		= findPrevView( fFocus, B_NAVIGABLE_JUMP );
		}
		else
			if (modifiers & B_CONTROL_KEY){
				nextFocus		= findNextView( fFocus, B_NAVIGABLE_JUMP );
			}
			else
				if (modifiers & B_SHIFT_KEY){
					nextFocus		= findPrevView( fFocus, B_NAVIGABLE );
				}
				else
					nextFocus		= findNextView( fFocus, B_NAVIGABLE );

		if ( nextFocus )
			setFocus( nextFocus, false );

		return true;
	}

		// Handle shortcuts
	int			index;
	if ( (index = findShortcut(raw_char, modifiers)) >=0){
		_BCmdKey		*cmdKey;

		cmdKey			= (_BCmdKey*)accelList.ItemAt( index );

			// we'll give the message to the focus view
		if (cmdKey->targetToken == B_ANY_TOKEN){
			fFocus->MessageReceived( cmdKey->message );
			return true;
		}
		else{
			BHandler		*handler;
			BHandler		*aHandler;
			int				noOfHandlers;

				// search for a match through BLooper's list of eligible handlers
			handler			= NULL;
			noOfHandlers	= CountHandlers();
			for( int i=0; i < noOfHandlers; i++ )
					// do we have a match?
				if ( _get_object_token_( aHandler = HandlerAt(i) )
					 == cmdKey->targetToken)
				{
						// yes, we do.
					handler		= aHandler;
					break;
				}

			if ( handler )
				handler->MessageReceived( cmdKey->message );
			else
					// if no handler was found, BWindow will handle the message
				MessageReceived( cmdKey->message );
		}
		return true;
	}

		// if <ENTER> is pressed and we have a default button
	if (DefaultButton() && (raw_char == B_ENTER)){
		const char		*chars;		// just to be sure
		CurrentMessage()->FindString("bytes", &chars);

		DefaultButton()->KeyDown( chars, strlen(chars)-1 );
		return true;
	}


	return false;
}

//------------------------------------------------------------------------------

BView* BWindow::sendMessageUsingEventMask2( BView* aView, int32 message, BPoint where ){

	BView		*destView;
	destView	= NULL;

	if ( aView->fCachedBounds.Contains( aView->ConvertFromScreen(where) ) &&
		 !aView->first_child ){
		return aView;
	}

		// Code for Event Masks
	BView *child; 
	if ( child = aView->first_child ){
		while ( child ) { 
				// see if a BView registered for mouse events and it's not the current focus view
			if ( child->fEventMask & B_POINTER_EVENTS &&
				 aView != fFocus ){
				switch (message){
					case B_MOUSE_DOWN:{
						child->MouseDown( child->ConvertFromScreen( where ) );
					}
					break;
					
					case B_MOUSE_UP:{
						child->MouseUp( child->ConvertFromScreen( where ) );
					}
					break;
					
					case B_MOUSE_MOVED:{
						BMessage	*dragMessage;

// TODO: get the dragMessage if any
							// for now...
						dragMessage	= NULL;

/* TODO: after you have an example working, see if a view that registered for such events,
			does reveive B_MOUSE_MOVED with other options than B_OUTDIDE_VIEW !!!
					like: B_INSIDE_VIEW, B_ENTERED_VIEW, B_EXITED_VIEW
*/
						child->MouseMoved( ConvertFromScreen(where), B_OUTSIDE_VIEW , dragMessage);
					}
					break;
				}
			}
			if (destView == NULL)
				destView = sendMessageUsingEventMask2( child, message, where );
			else
				sendMessageUsingEventMask2( child, message, where );
			child = child->next_sibling; 
		}
	}
	
	return destView;
}

//------------------------------------------------------------------------------

void BWindow::sendMessageUsingEventMask( int32 message, BPoint where ){
	BView*			destView;

	destView		= sendMessageUsingEventMask2(top_view, message, where);
	
		// I'm SURE this is NEVER going to happen, but, durring development of BWindow, it may slip a NULL value
	if (!destView){
		// debugger("There is no BView under the mouse;");
		return;
	}
	
	switch( message ){
		case B_MOUSE_DOWN:{
			setFocus( destView );
			destView->MouseDown( destView->ConvertFromScreen( where ) );
			break;}

		case B_MOUSE_UP:{
			destView->MouseUp( destView->ConvertFromScreen( where ) );
			break;}

		case B_MOUSE_MOVED:{
			BMessage	*dragMessage;

// TODO: add code for drag and drop
				// for now...
			dragMessage	= NULL;

			if (destView != fLastMouseMovedView){
				fLastMouseMovedView->MouseMoved( destView->ConvertFromScreen( where ), B_EXITED_VIEW , dragMessage);
				destView->MouseMoved( ConvertFromScreen( where ), B_ENTERED_VIEW, dragMessage);
				fLastMouseMovedView		= destView;
			}
			else{
				destView->MouseMoved( ConvertFromScreen( where ), B_INSIDE_VIEW , dragMessage);
			}

				// I'm guessing that B_OUTSIDE_VIEW is given to the view that has focus, I'll have to check
// TODO: Do a research on mouse capturing, maybe it has something to do with this
			if (fFocus != destView)
				fFocus->MouseMoved( ConvertFromScreen( where ), B_OUTSIDE_VIEW , dragMessage);
			break;}
	}
}

//------------------------------------------------------------------------------

BMessage* BWindow::ConvertToMessage(void* raw1, int32 code){

	BMessage	*msg;

		// This is in case we receive a BMessage from another thread or application
	msg			= BLooper::ConvertToMessage( raw1, code );
	if (msg)
		return msg;

		// (ALL)This is in case we receive a message from app_server
	uint8		*raw;
	raw			= (uint8*)raw1;

		// time since 01/01/70
	int64		when;

	msg			= new BMessage();

	switch(code){
		case B_WINDOW_ACTIVATED:{
			bool		active;

			when		= *((int64*)raw);	raw += sizeof(int64);
			active		= *((bool*)raw);

			msg->what	= B_WINDOW_ACTIVATED;
			msg->AddInt64("when", when);
			msg->AddBool("active", active);

			break;}

// TODO: this might be sent by app_server as a BMessage! Anyway, this won't be a problem.
		case B_QUIT_REQUESTED:{

			msg->what	= B_QUIT_REQUESTED;
			msg->AddBool("shortcut", false);

			break;}

		case B_KEY_DOWN:{
			int32			physicalKeyCode,
							repeat,
							modifiers,
							ASCIIcode;
			char			*bytes;
			uint8			states;
			int8			UTF8_1, UTF8_2, UTF8_3;

			when			= *((int64*)raw);	raw += sizeof(int64);
			physicalKeyCode	= *((int32*)raw);	raw += sizeof(int32);
			repeat			= *((int32*)raw);	raw += sizeof(int32);
			modifiers		= *((int32*)raw);	raw += sizeof(int32);
			states			= *((uint8*)raw);	raw += sizeof(uint8);
			UTF8_1			= *((int8*)raw);	raw += sizeof(int8);
			UTF8_2			= *((int8*)raw);	raw += sizeof(int8);
			UTF8_3			= *((int8*)raw);	raw += sizeof(int8);
			ASCIIcode		= *((int32*)raw);	raw += sizeof(int32);

			bytes			= strdup( (char*)raw );

			msg->what		= B_KEY_DOWN;
			msg->AddInt64("when", when);
			msg->AddInt32("key", physicalKeyCode);
			msg->AddInt32("be:key_repeat", repeat);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt8("states", states);
			msg->AddInt8("byte", UTF8_1);
			msg->AddInt8("byte", UTF8_2);
			msg->AddInt8("byte", UTF8_3);
			msg->AddInt32("raw_char", ASCIIcode);
			msg->AddString("bytes", bytes);

			break;}

		case B_KEY_UP:{
			int32			physicalKeyCode,
							modifiers,
							ASCIIcode;
			char			*bytes;
			uint8			states;
			int8			UTF8_1, UTF8_2, UTF8_3;

			when			= *((int64*)raw);	raw += sizeof(int64);
			physicalKeyCode	= *((int32*)raw);	raw += sizeof(int32);
			modifiers		= *((int32*)raw);	raw += sizeof(int32);
			states			= *((uint8*)raw);	raw += sizeof(uint8);
			UTF8_1			= *((int8*)raw);	raw += sizeof(int8);
			UTF8_2			= *((int8*)raw);	raw += sizeof(int8);
			UTF8_3			= *((int8*)raw);	raw += sizeof(int8);
			ASCIIcode		= *((int32*)raw);	raw += sizeof(int32);

			bytes			= strdup( (char*)raw );

			msg->what		= B_KEY_UP;
			msg->AddInt64("when", when);
			msg->AddInt32("key", physicalKeyCode);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt8("states", states);
			msg->AddInt8("byte", UTF8_1);
			msg->AddInt8("byte", UTF8_2);
			msg->AddInt8("byte", UTF8_3);
			msg->AddInt32("raw_char", ASCIIcode);
			msg->AddString("bytes", bytes);

			break;}

		case B_UNMAPPED_KEY_DOWN:{
			int32			physicalKeyCode,
							modifiers;
			uint8			states;

			when			= *((int64*)raw);	raw += sizeof(int64);
			physicalKeyCode	= *((int32*)raw);	raw += sizeof(int32);
			modifiers		= *((int32*)raw);	raw += sizeof(int32);
			states			= *((uint8*)raw);

			msg->what		= B_UNMAPPED_KEY_DOWN;
			msg->AddInt64("when", when);
			msg->AddInt32("key", physicalKeyCode);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt8("states", states);

			break;}

		case B_UNMAPPED_KEY_UP:{
			int32			physicalKeyCode,
							modifiers;
			uint8			states;

			when			= *((int64*)raw);	raw += sizeof(int64);
			physicalKeyCode	= *((int32*)raw);	raw += sizeof(int32);
			modifiers		= *((int32*)raw);	raw += sizeof(int32);
			states			= *((uint8*)raw);

			msg->what		= B_UNMAPPED_KEY_UP;
			msg->AddInt64("when", when);
			msg->AddInt32("key", physicalKeyCode);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt8("states", states);

			break;}

		case B_MODIFIERS_CHANGED:{
			int32			modifiers,
							modifiersOld;
			uint8			states;

			when			= *((int64*)raw);	raw += sizeof(int64);
			modifiers		= *((int32*)raw);	raw += sizeof(int32);
			modifiersOld	= *((int32*)raw);	raw += sizeof(int32);
			states			= *((uint8*)raw);

			msg->what		= B_MODIFIERS_CHANGED;
			msg->AddInt64("when", when);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt32("be:old_modifiers", modifiersOld);
			msg->AddInt8("states", states);

			break;}

		case B_MINIMIZE:{
			bool			minimize;

			when			= *((int64*)raw);	raw += sizeof(int64);
			minimize		= *((bool*)raw);

			msg->what		= B_MINIMIZE;
			msg->AddInt64("when", when);
			msg->AddBool("minimize", minimize);

			break;}

		case B_MOUSE_DOWN:{
			float			mouseLocationX,
							mouseLocationY;
			int32			modifiers,
							buttons,
							noOfClicks;
			BPoint			where;

			when			= *((int64*)raw);	raw += sizeof(int64);
			mouseLocationX	= *((float*)raw);	raw += sizeof(float);	// view's coordinate system
			mouseLocationY	= *((float*)raw);	raw += sizeof(float);	// view's coordinate system
			modifiers		= *((int32*)raw);	raw += sizeof(int32);
			buttons			= *((int32*)raw);	raw += sizeof(int32);
			noOfClicks		= *((int32*)raw);

			where.Set( mouseLocationX, mouseLocationY );

			msg->what		= B_MOUSE_DOWN;
			msg->AddInt64("when", when);
			msg->AddPoint("where", where);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt32("buttons", buttons);
			msg->AddInt32("clicks", noOfClicks);

			break;}

		case B_MOUSE_MOVED:{
			float			mouseLocationX,
							mouseLocationY;
			int32			buttons;
			int32			modifiers;		// added by OBOS Team

			BPoint			where;

			when			= *((int64*)raw);	raw += sizeof(int64);
			mouseLocationX	= *((float*)raw);	raw += sizeof(float);	// windows's coordinate system
			mouseLocationY	= *((float*)raw);	raw += sizeof(float);	// windows's coordinate system
			modifiers		= *((int32*)raw);	raw += sizeof(int32);	// added by OBOS Team
			buttons			= *((int32*)raw);

			where.Set( mouseLocationX, mouseLocationY );

			msg->what		= B_MOUSE_MOVED;
			msg->AddInt64("when", when);
			msg->AddPoint("where", where);
			msg->AddInt32("buttons", buttons);
// TODO Add "modifiers" field !

			break;}

		case B_MOUSE_UP:{
			float			mouseLocationX,
							mouseLocationY;
			int32			modifiers,
							buttons;
			BPoint			where;

			when			= *((int64*)raw);	raw += sizeof(int64);
			mouseLocationX	= *((float*)raw);	raw += sizeof(float);	// view's coordinate system
			mouseLocationY	= *((float*)raw);	raw += sizeof(float);	// view's coordinate system
			buttons			= *((int32*)raw);	raw += sizeof(int32);
			modifiers		= *((int32*)raw);

			where.Set( mouseLocationX, mouseLocationY );

			msg->what		= B_MOUSE_UP;
			msg->AddInt64("when", when);
			msg->AddPoint("where", where);
			msg->AddInt32("modifiers", modifiers);
			msg->AddInt32("buttons", buttons);

			break;}

		case B_MOUSE_WHEEL_CHANGED:{
			float			whellChangeX,
							whellChangeY;

			when			= *((int64*)raw);	raw += sizeof(int64);
			whellChangeY	= *((float*)raw);	raw += sizeof(float);
			whellChangeY	= *((float*)raw);

			msg->what		= B_MOUSE_WHEEL_CHANGED;
			msg->AddInt64("when", when);
			msg->AddFloat("be:wheel_delta_x", whellChangeX);
			msg->AddFloat("be:wheel_delta_y", whellChangeY);

			break;}

		case B_SCREEN_CHANGED:{
			float			top,
							left,
							right,
							bottom;
			int32			colorSpace;
			BRect			frame;

			when			= *((int64*)raw);	raw += sizeof(int64);
			left			= *((float*)raw);	raw += sizeof(float);
			top				= *((float*)raw);	raw += sizeof(float);
			right			= *((float*)raw);	raw += sizeof(float);
			bottom			= *((float*)raw);	raw += sizeof(float);
			colorSpace		= *((int32*)raw);

			frame.Set( left, top, right, bottom );

			msg->what		= B_SCREEN_CHANGED;
			msg->AddInt64("when", when);
			msg->AddRect("frame", frame);
			msg->AddInt32("mode", colorSpace);

			break;}

		case B_VIEW_MOVED:{
			float			xAxisNewOrigin,
							yAxisNewOrigin;
			BPoint			where;
			int32			token;

			when			= *((int64*)raw);	raw += sizeof(int64);
			token			= *((int32*)raw);	raw += sizeof(int32);
			xAxisNewOrigin	= *((float*)raw);	raw += sizeof(float);
			yAxisNewOrigin	= *((float*)raw);

			where.Set( xAxisNewOrigin, yAxisNewOrigin );

			msg->what		= B_VIEW_MOVED;
			msg->AddInt64("when", when);
			msg->AddPoint("where", where);

			_set_message_target_( msg, token, false);
			
			break;}

		case B_VIEW_RESIZED:{
			int32			newWidth,
							newHeight;
			float			xAxisNewOrigin,
							yAxisNewOrigin;
			BPoint			where;
			int32			token;

			when			= *((int64*)raw);	raw += sizeof(int64);
			token			= *((int32*)raw);	raw += sizeof(int32);
			newWidth		= *((int32*)raw);	raw += sizeof(int32);
			newHeight		= *((int32*)raw);	raw += sizeof(int32);
			xAxisNewOrigin	= *((float*)raw);	raw += sizeof(float);
			yAxisNewOrigin	= *((float*)raw);

			where.Set( xAxisNewOrigin, yAxisNewOrigin );

			msg->what		= B_VIEW_RESIZED;
			msg->AddInt64("when", when);
			msg->AddInt32("width", newWidth);
			msg->AddInt32("height", newHeight);
			msg->AddPoint("where", where);

			_set_message_target_( msg, token, false);

			break;}

		case _UPDATE_:{
			int32			token;
			float			left, top,
							right, bottom;
			BRect			frame;

			token			= *((int32*)raw);	raw += sizeof(int32);
			left			= *((float*)raw);	raw += sizeof(float);
			top				= *((float*)raw);	raw += sizeof(float);
			right			= *((float*)raw);	raw += sizeof(float);
			bottom			= *((float*)raw);

			frame.Set( left, top, right, bottom );

			msg->what		= _UPDATE_;
			msg->AddInt32("token", token);
			msg->AddRect("frame", frame);

			break;}

		case B_WINDOW_MOVED:{
			float			xAxisNewOrigin,
							yAxisNewOrigin;
			BPoint			where;

			when			= *((int64*)raw);	raw += sizeof(int64);
			xAxisNewOrigin	= *((float*)raw);	raw += sizeof(float);
			yAxisNewOrigin	= *((float*)raw);

			where.Set( xAxisNewOrigin, yAxisNewOrigin );

			msg->what		= B_WINDOW_MOVED;
			msg->AddInt64("when", when);
			msg->AddPoint("where", where);

			break;}

		case B_WINDOW_RESIZED:{
			int32			newWidth,
							newHeight;

			when			= *((int64*)raw);	raw += sizeof(int64);
			newWidth		= *((int32*)raw);	raw += sizeof(int32);
			newHeight		= *((int32*)raw);

			msg->what		= B_WINDOW_RESIZED;
			msg->AddInt64("when", when);
			msg->AddInt32("width", newWidth);
			msg->AddInt32("height", newHeight);

			break;}

		case B_WORKSPACES_CHANGED:{
			int32			newWorkSpace,
							oldWorkSpace;

			when			= *((int64*)raw);	raw += sizeof(int64);
			oldWorkSpace	= *((int32*)raw);	raw += sizeof(int32);
			newWorkSpace	= *((int32*)raw);

			msg->what		= B_WORKSPACES_CHANGED;
			msg->AddInt64("when", when);
			msg->AddInt32("old", oldWorkSpace);
			msg->AddInt32("new", newWorkSpace);

			break;}

		case B_WORKSPACE_ACTIVATED:{
			int32			workSpace;
			bool			active;

			when			= *((int64*)raw);	raw += sizeof(int64);
			workSpace		= *((int32*)raw);	raw += sizeof(int32);
			active			= *((bool*)raw);

			msg->what		= B_WORKSPACE_ACTIVATED;
			msg->AddInt64("when", when);
			msg->AddInt32("workspace", workSpace);
			msg->AddBool("active", active);

			break;}

		case B_ZOOM:{

			when			= *((int64*)raw);

			msg->what		= B_ZOOM;
			msg->AddInt64("when", when);

			break;}
		
		default:{
			// there is no need for default, but, just in case...
			printf("There is a message from app_server that I don't undersand: '%c%c%c%c'\n",
				(code & 0xFF000000) >> 24, (code & 0x00FF0000) >> 16,
				(code & 0x0000FF00) >> 8, code & 0x000000FF );
			}
	}
	
	return msg;
}

//------------------------------------------------------------------------------

void BWindow::sendPulse( BView* aView ){
	BView *child; 
	if ( child = aView->first_child ){
		while ( child ) { 
			if ( child->Flags() & B_PULSE_NEEDED ) child->Pulse();
			sendPulse( child );
			child = child->next_sibling; 
		}
	}
}

//------------------------------------------------------------------------------

int32 BWindow::findShortcut( uint32 key, int32 modifiers ){
	int32			index,
					noOfItems;

	index			= -1;
	noOfItems		= accelList.CountItems();

	for ( int i = 0;  i < noOfItems; i++ ) {
		_BCmdKey*		tempCmdKey;

		tempCmdKey		= (_BCmdKey*)accelList.ItemAt(i);
		if (tempCmdKey->key == key && tempCmdKey->modifiers == modifiers){
			index		= i;
			break;
		}
	}
	
	return index;
}

//------------------------------------------------------------------------------

BView* BWindow::findView(BView* aView, int32 token){

	if ( _get_object_token_(aView) == token )
		return aView;

	BView			*child;
	if ( child = aView->first_child ){
		while ( child ) {
			BView*		view;
			if ( view = findView( child, token ) )
				return view;
			child 		= child->next_sibling; 
		}
	}

	return NULL;
}

//------------------------------------------------------------------------------

BView* BWindow::findView(BView* aView, const char* viewName) const{

	if ( strcmp( viewName, aView->Name() ) == 0)
		return aView;

	BView			*child;
	if ( child = aView->first_child ){
		while ( child ) {
			BView*		view;
			if ( view = findView( child, viewName ) )
				return view;
			child 		= child->next_sibling; 
		}
	}

	return NULL;
}

//------------------------------------------------------------------------------

BView* BWindow::findView(BView* aView, BPoint point) const{

	if ( aView->Bounds().Contains(point) &&
		 !aView->first_child )
		return aView;

	BView			*child;
	if ( child = aView->first_child ){
		while ( child ) {
			BView*		view;
			if ( view = findView( child, point ) )
				return view;
			child 		= child->next_sibling; 
		}
	}

	return NULL;
}

//------------------------------------------------------------------------------

BView* BWindow::findNextView( BView *focus, uint32 flags){
	bool		found;
	found		= false;

	BView		*nextFocus;
	nextFocus	= focus;

		/*	Ufff... this toked me some time... this is the best form I've reached.
				This algorithm searches the tree for BViews that accept focus.
		*/
	while (!found){
		if (nextFocus->first_child)
			nextFocus		= nextFocus->first_child;
		else	
			if (nextFocus->next_sibling)
				nextFocus		= nextFocus->next_sibling;
			else{
				while( !nextFocus->next_sibling && nextFocus->parent ){
					nextFocus		= nextFocus->parent;
				}

				if (nextFocus == top_view)
					nextFocus		= nextFocus->first_child;
				else
					nextFocus		= nextFocus->next_sibling;
			}

		if (nextFocus->Flags() & flags)
			found = true;

			/*	It means that the hole tree has been searched and there is no
				view with B_NAVIGABLE_JUMP flag set!
			*/
		if (nextFocus == focus)
			return NULL;
	}

	return nextFocus;
}

//------------------------------------------------------------------------------

BView* BWindow::findPrevView( BView *focus, uint32 flags){
	bool		found;
	found		= false;

	BView		*prevFocus;
	prevFocus	= focus;

	BView		*aView;

	while (!found){
		if ( (aView = findLastChild(prevFocus)) )
			prevFocus		= aView;
		else	
			if (prevFocus->prev_sibling)
				prevFocus		= prevFocus->prev_sibling;
			else{
				while( !prevFocus->prev_sibling && prevFocus->parent ){
					prevFocus		= prevFocus->parent;
				}

				if (prevFocus == top_view)
					prevFocus		= findLastChild( prevFocus );
				else
					prevFocus		= prevFocus->prev_sibling;
			}

		if (prevFocus->Flags() & flags)
			found = true;


			/*	It means that the hole tree has been searched and there is no
				view with B_NAVIGABLE_JUMP flag set!
			*/
		if (prevFocus == focus)
			return NULL;
	}

	return prevFocus;
}

//------------------------------------------------------------------------------

BView* BWindow::findLastChild(BView *parent){
	BView		*aView;
	if ( (aView = parent->first_child) ){
		while (aView->next_sibling)
			aView		= aView->next_sibling;

		return aView;
	}
	else
		return NULL;
}

//------------------------------------------------------------------------------

void BWindow::drawAllViews(BView* aView){

	BMessageQueue	*queue;
	BMessage		*msg;

	queue			= MessageQueue();

		// process all update BMessages from message queue
	queue->Lock();
	while ( (msg = queue->FindMessage( (uint32)_UPDATE_ )) )
	{
		Lock();
		DispatchMessage( msg, this );
		Unlock();

		queue->RemoveMessage( msg );
	}
	queue->Unlock();

		// we'll send a message to app_server, tell him that we want all our views updated
	serverLink->SetOpCode( AS_UPDATE_IF_NEEDED );

	Lock();
	serverLink->Flush();
	Unlock();
	
		// process all update messages from receive port queue
	bool			over = false;
	while (!over)
	{
		msg			= ReadMessageFromPort(0);
		if (msg){
			switch (msg->what){
			
			case _ALL_UPDATED_:{
				over		= true;
				}break;

			case _UPDATE_:{
				Lock();
				DispatchMessage( msg, this );
				Unlock();
				}break;

			default:{
				queue->Lock();
				queue->AddMessage(msg);
				queue->Unlock();
				}break;
			}
		}
	}
}

//------------------------------------------------------------------------------

void BWindow::drawView(BView* aView, BRect area){

/* TODO: Drawing during an Update
			These settings are temporary. When the update is over, all graphics
			  parameters are reset to their initial values
*/
	aView->Draw( area );

	BView			*child;
	if ( child = aView->first_child ){
		while ( child ) {
			if ( area.Intersects( child->Frame() ) ){
				BRect			newArea;

				newArea			= area & child->Frame();
				child->ConvertFromParent( &newArea );
				child->Invalidate( newArea );
			}
			child 		= child->next_sibling; 
		}
	}
}

//------------------------------------------------------------------------------

void BWindow::SetIsFilePanel(bool yes){

// TODO: is this not enough?
	fIsFilePanel	= yes;
}

//------------------------------------------------------------------------------

bool BWindow::IsFilePanel() const{
	return fIsFilePanel;
}


//------------------------------------------------------------------------------
// Virtual reserved Functions

void BWindow::_ReservedWindow1() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow2() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow3() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow4() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow5() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow6() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow7() { }
//------------------------------------------------------------------------------
void BWindow::_ReservedWindow8() { }


/*
TODO list:

	*) take care of temporarely events mask!!!
	*) what's with this flag B_ASYNCHRONOUS_CONTROLS ?
	*) what should I do if frame rect is invalid?
	*) AddInt32("ccc", (uint32));
	*) test arguments for SetWindowAligment
	*) call hook functions: MenusBeginning, MenusEnded. Add menu activation code.

*/
