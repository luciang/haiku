/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Adrian Oanca <adioanca@cotty.iren.ro>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus, <superstippi@gmx.de>
 */


#include <BeBuild.h>
#include <InterfaceDefs.h>
#include <PropertyInfo.h>
#include <Handler.h>
#include <Looper.h>
#include <Application.h>
#include <Window.h>
#include <View.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <String.h>
#include <Screen.h>
#include <Button.h>
#include <MessageQueue.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <Autolock.h>

#include <ApplicationPrivate.h>
#include <AppMisc.h>
#include <PortLink.h>
#include <ServerProtocol.h>
#include <TokenSpace.h>
#include <MessageUtils.h>
#include <WindowAux.h>

#include <stdio.h>
#include <math.h>


//#define DEBUG_WIN
#ifdef DEBUG_WIN
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

using BPrivate::gDefaultTokens;

static property_info sWindowPropInfo[] = {
	{
		"Feel", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE } 
	},

	{
		"Flags", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"Frame", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_RECT_TYPE }
	},

	{
		"Hidden", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_BOOL_TYPE }
	},

	{
		"Look", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE }
	},

	{
		"Title", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_STRING_TYPE }
	},

	{
		"Workspaces", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_INT32_TYPE}
	},

	{
		"MenuBar", {},
		{ B_DIRECT_SPECIFIER }, NULL, 0, {}
	},

	{
		"View", {}, {}, NULL, 0, {}
	},

	{
		"Minimize", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER }, NULL, 0, { B_BOOL_TYPE }
	},

	{}
};


void 
_set_menu_sem_(BWindow *window, sem_id sem)
{
	if (window != NULL)
		window->fMenuSem = sem;
}


//	#pragma mark -


BWindow::BWindow(BRect frame, const char* title, window_type type,
	uint32 flags, uint32 workspace)
	: BLooper(title)
{
	#ifdef DEBUG_WIN
		printf("BWindow::BWindow()\n");
	#endif
	window_look look;
	window_feel feel;

	decomposeType(type, &look, &feel);

	InitData(frame, title, look, feel, flags, workspace);
}


BWindow::BWindow(BRect frame, const char* title, window_look look, window_feel feel,
	uint32 flags, uint32 workspace)
	: BLooper(title)
{
	InitData(frame, title, look, feel, flags, workspace);
}


BWindow::BWindow(BMessage* data)
	: BLooper(data)
{
	data->FindRect("_frame", &fFrame);

	const char *title;
	data->FindString("_title", &title);

	window_look look;
	data->FindInt32("_wlook", (int32 *)&look);

	window_feel feel;
	data->FindInt32("_wfeel", (int32 *)&feel);

	if (data->FindInt32("_flags", (int32 *)&fFlags) != B_OK)
		fFlags = 0;

	uint32 workspaces;
	data->FindInt32("_wspace", (int32 *)&workspaces);

	uint32 type;
	if (data->FindInt32("_type", (int32*)&type) == B_OK)
		decomposeType((window_type)type, &fLook, &fFeel);

		// connect to app_server and initialize data
	InitData(fFrame, title, look, feel, fFlags, workspaces);

	if (data->FindFloat("_zoom", 0, &fMaxZoomWidth) == B_OK
		&& data->FindFloat("_zoom", 1, &fMaxZoomHeight) == B_OK)
		SetZoomLimits(fMaxZoomWidth, fMaxZoomHeight);

	if (data->FindFloat("_sizel", 0, &fMinWidth) == B_OK
		&& data->FindFloat("_sizel", 1, &fMinHeight) == B_OK
		&& data->FindFloat("_sizel", 2, &fMaxWidth) == B_OK
		&& data->FindFloat("_sizel", 3, &fMaxHeight) == B_OK)
		SetSizeLimits(fMinWidth, fMaxWidth,
			fMinHeight, fMaxHeight);

	if (data->FindInt64("_pulse", &fPulseRate) == B_OK)
		SetPulseRate(fPulseRate);

	BMessage msg;
	int32 i = 0;
	while ( data->FindMessage("_views", i++, &msg) == B_OK){ 
		BArchivable *obj = instantiate_object(&msg);
		BView *child = dynamic_cast<BView *>(obj);
		if (child)
			AddChild(child);
	}
}


BWindow::BWindow(BRect frame, int32 bitmapToken)
	: BLooper("offscreen bitmap")
{
	// TODO: Implement for real
	decomposeType(B_UNTYPED_WINDOW, &fLook, &fFeel);
	InitData(frame, "offscreen", fLook, fFeel, 0, 0, bitmapToken);
}


BWindow::~BWindow()
{
	// the following lines, remove all existing shortcuts and delete accelList
	int32 noOfItems = accelList.CountItems();
	for (int index = noOfItems-1; index >= 0; index--) {
		delete (_BCmdKey*)accelList.ItemAt(index);
	}

	// TODO: release other dynamically-allocated objects
	
	// Deleting this semaphore will tell open menus to quit.
	if (fMenuSem > 0)
		delete_sem(fMenuSem);

	// disable pulsing
	SetPulseRate(0);

	// tell app_server about our demise
	fLink->StartMessage(AS_DELETE_WINDOW);
	fLink->Flush();

	// the sender port belongs to the app_server
	delete_port(fLink->ReceiverPort());
	delete fLink;
}


BArchivable *
BWindow::Instantiate(BMessage *data)
{
	if (!validate_instantiation(data , "BWindow")) 
		return NULL; 

	return new BWindow(data); 
}


status_t
BWindow::Archive(BMessage* data, bool deep) const
{
	status_t retval = BLooper::Archive(data, deep);
	if (retval != B_OK)
		return retval;

	data->AddRect("_frame", fFrame);
	data->AddString("_title", fTitle);
	data->AddInt32("_wlook", fLook);
	data->AddInt32("_wfeel", fFeel);
	if (fFlags)
		data->AddInt32("_flags", fFlags);
	data->AddInt32("_wspace", (uint32)Workspaces());

	if (!composeType(fLook, fFeel))
		data->AddInt32("_type", (uint32)Type());

	if (fMaxZoomWidth != 32768.0 || fMaxZoomHeight != 32768.0) {
		data->AddFloat("_zoom", fMaxZoomWidth);
		data->AddFloat("_zoom", fMaxZoomHeight);
	}

	if (fMinWidth != 0.0 || fMinHeight != 0.0 
		|| fMaxWidth != 32768.0 || fMaxHeight != 32768.0) {
		data->AddFloat("_sizel", fMinWidth);
		data->AddFloat("_sizel", fMinHeight);
		data->AddFloat("_sizel", fMaxWidth);
		data->AddFloat("_sizel", fMaxHeight);
	}

	if (fPulseRate != 500000)
		data->AddInt64("_pulse", fPulseRate);

	if (deep) {
		int32 noOfViews = CountChildren();
		for (int32 i = 0; i < noOfViews; i++){
			BMessage childArchive;
			if (ChildAt(i)->Archive(&childArchive, deep) == B_OK)
				data->AddMessage("_views", &childArchive);
		}
	}

	return B_OK;
}


void
BWindow::Quit()
{
	if (!IsLocked()) {
		const char *name = Name();
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

	while (!IsHidden())	{ 
		Hide(); 
	}

	// ... also its children
	//detachTopView();

	if (fFlags & B_QUIT_ON_WINDOW_CLOSE)
		be_app->PostMessage(B_QUIT_REQUESTED);

	BLooper::Quit();
}


void
BWindow::AddChild(BView *child, BView *before)
{
	top_view->AddChild(child, before);
}


bool
BWindow::RemoveChild(BView *child)
{
	return top_view->RemoveChild(child);
}


int32
BWindow::CountChildren() const
{
	return top_view->CountChildren();
}


BView *
BWindow::ChildAt(int32 index) const
{
	return top_view->ChildAt(index);
}


void
BWindow::Minimize(bool minimize)
{
	if (IsModal())
		return;

	if (IsFloating())
		return;		

	if (fMinimized == minimize)
		return;

	fMinimized = minimize;

	Lock();
	fLink->StartMessage(AS_WINDOW_MINIMIZE);
	fLink->Attach<bool>(minimize);
	fLink->Flush();
	Unlock();
}


status_t
BWindow::SendBehind(const BWindow *window)
{
	if (!window)
		return B_ERROR;

	Lock();
	fLink->StartMessage(AS_SEND_BEHIND);
	fLink->Attach<int32>(_get_object_token_(window));

	int32 code = SERVER_FALSE;
	fLink->FlushWithReply(code);

	Unlock();

	return code == SERVER_TRUE ? B_OK : B_ERROR;
}


void
BWindow::Flush() const
{
	const_cast<BWindow *>(this)->Lock();
	fLink->Flush();
	const_cast<BWindow *>(this)->Unlock();
}


void
BWindow::Sync() const
{
	const_cast<BWindow*>(this)->Lock();
	fLink->StartMessage(AS_SYNC);

	// ToDo: why with reply?
	int32 code;
	fLink->FlushWithReply(code);

	const_cast<BWindow*>(this)->Unlock();
}


void
BWindow::DisableUpdates()
{
	Lock();
	fLink->StartMessage(AS_DISABLE_UPDATES);
	fLink->Flush();
	Unlock();
}


void
BWindow::EnableUpdates()
{
	Lock();
	fLink->StartMessage(AS_ENABLE_UPDATES);
	fLink->Flush();
	Unlock();
}


void
BWindow::BeginViewTransaction()
{
	if (!fInTransaction) {
		Lock();
		fLink->StartMessage(AS_BEGIN_TRANSACTION);
		Unlock();

		fInTransaction = true;
	}
}


void
BWindow::EndViewTransaction()
{
	if (fInTransaction) {
		Lock();
		fLink->StartMessage(AS_END_TRANSACTION);
		fLink->Flush();
		Unlock();

		fInTransaction = false;		
	}
}


bool
BWindow::IsFront() const
{
	if (IsActive())
		return true;

	if (IsModal())
		return true;

	return false;
}


void 
BWindow::MessageReceived(BMessage *msg)
{
	if (!msg->HasSpecifiers())
		return BLooper::MessageReceived(msg);

	BMessage replyMsg(B_REPLY);
	bool handled = false;

	switch (msg->what) {
		case B_GET_PROPERTY:
		case B_SET_PROPERTY: {
			BMessage specifier;
			int32 what;
			const char *prop;
			int32 index;

			if (msg->GetCurrentSpecifier(&index, &specifier, &what, &prop) != B_OK)
				break;

			if (!strcmp(prop, "Feel")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddInt32("result", (uint32)Feel());
					handled = true;
				} else {
					uint32 newFeel;
					if (msg->FindInt32("data", (int32 *)&newFeel) == B_OK) {
						SetFeel((window_feel)newFeel);
						handled = true;
					}
				}
			} else if (!strcmp(prop, "Flags")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddInt32("result", Flags());
					handled = true;
				} else {
					uint32 newFlags;
					if (msg->FindInt32("data", (int32 *)&newFlags) == B_OK) {
						SetFlags(newFlags);
						handled = true;
					}
				}
			} else if (!strcmp(prop, "Frame")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddRect("result", Frame());
					handled = true;
				} else {
					BRect newFrame;
					if (msg->FindRect("data", &newFrame) == B_OK) {
						MoveTo(newFrame.LeftTop());
						ResizeTo(newFrame.Width(), newFrame.Height());
						handled = true;
					}
				}
			} else if (!strcmp(prop, "Hidden")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddBool("result", IsHidden());
					handled = true;
				} else {
					bool hide;
					if (msg->FindBool("data", &hide) == B_OK) {
						if (hide) {
							if (!IsHidden())
								Hide();
						} else if (IsHidden())
							Show();

						handled = true;
					}
				}
			} else if (!strcmp(prop, "Look")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddInt32("result", (uint32)Look());
					handled = true;
				} else {
					uint32 newLook;
					if (msg->FindInt32("data", (int32 *)&newLook) == B_OK) {
						SetLook((window_look)newLook);
						handled = true;
					}
				}
			} else if (!strcmp(prop, "Title")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddString("result", Title());
					handled = true;
				} else {
					const char *newTitle = NULL;
					if (msg->FindString("data", &newTitle) == B_OK) {
						SetTitle(newTitle);
						handled = true;
					}
				}
			} else if (!strcmp(prop, "Workspaces")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddInt32( "result", Workspaces());
					handled = true;
				} else {
					uint32 newWorkspaces;
					if (msg->FindInt32("data", (int32 *)&newWorkspaces) == B_OK) {
						SetWorkspaces(newWorkspaces);
						handled = true;
					}
				}
			} else if (!strcmp(prop, "Minimize")) {
				if (msg->what == B_GET_PROPERTY) {
					replyMsg.AddBool("result", IsMinimized());
					handled = true;
				} else {
					bool minimize;
					if (msg->FindBool("data", &minimize) == B_OK) {
						Minimize(minimize);
						handled = true;
					}
				}
			}
			break;
		}
	}

	if (handled) {
		if (msg->what == B_SET_PROPERTY)
			replyMsg.AddInt32("error", B_OK);
	} else {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;
		replyMsg.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
		replyMsg.AddString("message", "Didn't understand the specifier(s)");
	}
	msg->SendReply(&replyMsg);
} 


void 
BWindow::DispatchMessage(BMessage *msg, BHandler *target) 
{
	if (!msg)
		return;

	switch (msg->what) {
		case B_ZOOM:
			Zoom();
			break;

		case B_MINIMIZE:
		{
			bool minimize;
			if (msg->FindBool("minimize", &minimize) == B_OK)
				Minimize(minimize);
			break;
		}

		case B_WINDOW_RESIZED:
		{
			int32 width, height;
			if (msg->FindInt32("width", &width) == B_OK
				&& msg->FindInt32("height", &height) == B_OK) {
				fFrame.right = fFrame.left + width;
				fFrame.bottom = fFrame.top + height;

				FrameResized(width, height);
			}
			break;
		}

		case B_WINDOW_MOVED:
		{
			BPoint origin;
			if (msg->FindPoint("where", &origin) == B_OK) {
				fFrame.OffsetTo(origin);

				FrameMoved(origin);
			}
			break;
		}

		case B_WINDOW_ACTIVATED:
		{
			bool active;
			if (msg->FindBool("active", &active) == B_OK) {
				fActive = active;
				handleActivation(active);
			}
			break;
		}

		case B_SCREEN_CHANGED:
		{
			BRect frame;
			uint32 mode;
			if (msg->FindRect("frame", &frame) == B_OK
				&& msg->FindInt32("mode", (int32 *)&mode) == B_OK)
				ScreenChanged(frame, (color_space)mode);
			break;
		}

		case B_WORKSPACE_ACTIVATED:
		{
			uint32 workspace;
			bool active;
			if (msg->FindInt32("workspace", (int32 *)&workspace) == B_OK
				&& msg->FindBool("active", &active) == B_OK)
				WorkspaceActivated(workspace, active);
			break;
		}

		case B_WORKSPACES_CHANGED:
		{
			uint32 oldWorkspace, newWorkspace;
			if (msg->FindInt32("old", (int32 *)&oldWorkspace) == B_OK
				&& msg->FindInt32("new", (int32 *)&newWorkspace) == B_OK)
				WorkspacesChanged(oldWorkspace, newWorkspace);
			break;
		}

		case B_KEY_DOWN:
		{
			uint32 modifiers;
			int32 rawChar;
			const char *string = NULL;
			msg->FindInt32("modifiers", (int32*)&modifiers);
			msg->FindInt32("raw_char", &rawChar);
			msg->FindString("bytes", &string);

// TODO: USE target !!!!
			if (!_HandleKeyDown(string[0], (uint32)modifiers)) {
				if (fFocus)
					fFocus->KeyDown(string, strlen(string));
				else
					printf("Adi: No Focus\n");
			}
			break;
		}

		case B_KEY_UP:
		{
			const char *string = NULL;
			msg->FindString("bytes", &string);

// TODO: USE target !!!!
			if (fFocus)
				fFocus->KeyUp(string, strlen(string));
			break;
		}

		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
		case B_MODIFIERS_CHANGED:
			if (target != this && target != top_view)
				target->MessageReceived(msg);
			break;

		case B_MOUSE_WHEEL_CHANGED:
			if (target != this && target != top_view)
				target->MessageReceived(msg);
			break;

		case B_MOUSE_DOWN:
		{
			BPoint where;
			uint32 modifiers;
			uint32 buttons;
			int32 clicks;
			msg->FindPoint("where", &where);
			msg->FindInt32("modifiers", (int32 *)&modifiers);
			msg->FindInt32("buttons", (int32 *)&buttons);
			msg->FindInt32("clicks", &clicks);

			if (target && target != this && target != top_view) {
				if (BView *view = dynamic_cast<BView *>(target)) {
					view->ConvertFromScreen(&where);
					view->MouseDown(where);
				} else
					target->MessageReceived(msg);
			}
			break;
		}

		case B_MOUSE_UP:
		{
			BPoint where;
			uint32 modifiers;
			msg->FindPoint("where", &where);
			msg->FindInt32("modifiers", (int32 *)&modifiers);

			if (target && target != this && target != top_view) {
				if (BView *view = dynamic_cast<BView *>(target)) {
					view->ConvertFromScreen(&where);
					view->MouseUp(where);
				} else
					target->MessageReceived(msg);
			}
			break;
		}

		case B_MOUSE_MOVED:
		{
			BPoint where;
			uint32 buttons;
			uint32 transit;
			msg->FindPoint("where", &where);
			msg->FindInt32("buttons", (int32 *)&buttons);
			msg->FindInt32("transit", (int32 *)&transit);
			if (target && target != this && target != top_view) {
				if (BView *view = dynamic_cast<BView *>(target)) {
					fLastMouseMovedView = view;
					view->ConvertFromScreen(&where);
					view->MouseMoved(where, transit, NULL);
				} else
					target->MessageReceived(msg);
			}
			break;
		}

		case B_PULSE:
			if (fPulseEnabled) {
				top_view->_Pulse();
				fLink->Flush();
			}
			break;

		case B_QUIT_REQUESTED:
			if (QuitRequested())
				Quit();
			break;

		case _UPDATE_:
		{
			STRACE(("info:BWindow handling _UPDATE_.\n"));
			BRect updateRect;
			int32 token;
			msg->FindRect("_rect", &updateRect);
			msg->FindInt32("_token", &token);

			fLink->StartMessage(AS_BEGIN_UPDATE);
			DoUpdate(top_view, updateRect);
			fLink->StartMessage(AS_END_UPDATE);
			fLink->Flush();
			break;
		}

		case B_VIEW_RESIZED:
		case B_VIEW_MOVED:
		{
			// NOTE: The problem with this implementation is that BView::Window()->CurrentMessage()
			// will show this message, and not what it used to be on R5. This might break apps and
			// we need to fix this here or change the way this feature is implemented. However, this
			// implementation shows what has to be done when Layers are moved or resized inside the
			// app_server. This message is generated from Layer::move_by() and resize_by() in
			// Layer::AddToViewsWithInvalidCoords().
			int32 token;
			BPoint frameLeftTop;
			float width;
			float height;
			BView *view;
			for (int32 i = 0; CurrentMessage() && msg->FindInt32("_token", i, &token) >= B_OK; i++) {
				if (token >= 0) {
					msg->FindPoint("where", i, &frameLeftTop);
					msg->FindFloat("width", i, &width);
					msg->FindFloat("height", i, &height);
					if ((view = findView(top_view, token))) {
						// update the views offset in parent
						if (view->LeftTop() != frameLeftTop) {
//printf("updating position (%.1f, %.1f): %s\n", frameLeftTop.x, frameLeftTop.y, view->Name());
							view->fParentOffset = frameLeftTop;

							// optionally call FrameMoved
							if (view->fFlags & B_FRAME_EVENTS) {
								STRACE(("Calling BView(%s)::FrameMoved( %.1f, %.1f )\n", view->Name(),
										frameLeftTop.x, frameLeftTop.y));
								view->FrameMoved(frameLeftTop);
							}
						}
						// update the views width and height
						if (view->fBounds.Width() != width || view->fBounds.Height() != height) {
//printf("updating size (%.1f, %.1f): %s\n", width, height, view->Name());
							// TODO: does this work when a views left/top side is resized?
							view->fBounds.right = view->fBounds.left + width;
							view->fBounds.bottom = view->fBounds.top + height;
							// optionally call FrameResized
							if (view->fFlags & B_FRAME_EVENTS) {
								STRACE(("Calling BView(%s)::FrameResized( %f, %f )\n", view->Name(), width, height));
								view->FrameResized(width, height);
							}
						}
					} else {
						fprintf(stderr, "***PANIC: BW: Can't find view with ID: %ld !***\n", token);
					}
				}
			}
			break;
		}

		case _MENUS_DONE_:
			MenusEnded();
			break;

		// These two are obviously some kind of old scripting messages
		// this is NOT an app_server message and we have to be cautious
		case B_WINDOW_MOVE_BY:
		{
			BPoint offset;
			if (msg->FindPoint("data", &offset) == B_OK)
				MoveBy(offset.x, offset.y);
			else
				msg->SendReply(B_MESSAGE_NOT_UNDERSTOOD);
			break;
		}

		// this is NOT an app_server message and we have to be cautious
		case B_WINDOW_MOVE_TO:
		{
			BPoint origin;
			if (msg->FindPoint("data", &origin) == B_OK)
				MoveTo(origin);
			else
				msg->SendReply(B_MESSAGE_NOT_UNDERSTOOD);
			break;
		}

		default:
			BLooper::DispatchMessage(msg, target); 
			break;
	}
}


void
BWindow::FrameMoved(BPoint new_position)
{
	// does nothing
	// Hook function
}


void
BWindow::FrameResized(float new_width, float new_height)
{
	// does nothing
	// Hook function
}


void
BWindow::WorkspacesChanged(uint32 old_ws, uint32 new_ws)
{
	// does nothing
	// Hook function
}


void
BWindow::WorkspaceActivated(int32 ws, bool state)
{
	// does nothing
	// Hook function
}


void
BWindow::MenusBeginning()
{
	// does nothing
	// Hook function
}


void
BWindow::MenusEnded()
{
	// does nothing
	// Hook function
}


void
BWindow::SetSizeLimits(float minWidth, float maxWidth, 
	float minHeight, float maxHeight)
{
	if (minWidth > maxWidth || minHeight > maxHeight)
		return;

	if (Lock()) {
		fLink->StartMessage(AS_SET_SIZE_LIMITS);
		fLink->Attach<float>(minWidth);
		fLink->Attach<float>(maxWidth);
		fLink->Attach<float>(minHeight);
		fLink->Attach<float>(maxHeight);

		int32 code;
		if (fLink->FlushWithReply(code) == B_OK
			&& code == SERVER_TRUE) {
			// read the values that were really enforced on
			// the server side (the window frame could have
			// been changed, too)
			fLink->Read<BRect>(&fFrame);
			fLink->Read<float>(&fMinWidth);
			fLink->Read<float>(&fMaxWidth);
			fLink->Read<float>(&fMinHeight);
			fLink->Read<float>(&fMaxHeight);
		}
		Unlock();
	}
}


void
BWindow::GetSizeLimits(float *minWidth, float *maxWidth, 
	float *minHeight, float *maxHeight)
{
	// TODO: What about locking?!?
	*minHeight = fMinHeight;
	*minWidth = fMinWidth;
	*maxHeight = fMaxHeight;
	*maxWidth = fMaxWidth;
}


void
BWindow::SetZoomLimits(float maxWidth, float maxHeight)
{
	// TODO: What about locking?!?
	if (maxWidth > fMaxWidth)
		maxWidth = fMaxWidth;
	else
		fMaxZoomWidth = maxWidth;

	if (maxHeight > fMaxHeight)
		maxHeight = fMaxHeight;
	else
		fMaxZoomHeight = maxHeight;
}


void
BWindow::Zoom(BPoint rec_position, float rec_width, float rec_height)
{
	// this is also a Hook function!

	MoveTo(rec_position);
	ResizeTo(rec_width, rec_height);
}


void
BWindow::Zoom()
{
	// TODO: broken.
	// TODO: What about locking?!?
	float minWidth, minHeight;
	BScreen screen;

	/*
		from BeBook:
		However, if the window's rectangle already matches these "zoom" dimensions
		(give or take a few pixels), Zoom() passes the window's previous
		("non-zoomed") size and location. (??????)
	*/

	if (Frame().Width() == fMaxZoomWidth && Frame().Height() == fMaxZoomHeight) {
		BPoint position( Frame().left, Frame().top);
		Zoom(position, fMaxZoomWidth, fMaxZoomHeight);
		return;
	}

	/* From BeBook:
		The dimensions that non-virtual Zoom() passes to hook Zoom() are deduced from
		the smallest of three rectangles:
	*/

	// 1) the rectangle defined by SetZoomLimits(), 
	minHeight = fMaxZoomHeight;
	minWidth = fMaxZoomWidth;

	// 2) the rectangle defined by SetSizeLimits()
	if (fMaxHeight < minHeight)
		minHeight = fMaxHeight;
	if (fMaxWidth < minWidth)
		minWidth = fMaxWidth;

	// 3) the screen rectangle
	if (screen.Frame().Width() < minWidth)
		minWidth = screen.Frame().Width();
	if (screen.Frame().Height() < minHeight)
		minHeight = screen.Frame().Height();

	Zoom(Frame().LeftTop(), minWidth, minHeight);
}


void
BWindow::ScreenChanged(BRect screen_size, color_space depth)
{
	// Hook function
	// does nothing
}


void
BWindow::SetPulseRate(bigtime_t rate)
{
	// TODO: What about locking?!?
	if (rate < 0)
		return;

	// ToDo: isn't fPulseRunner enough? Why fPulseEnabled?
	if (fPulseRate == 0 && !fPulseEnabled) {
		fPulseRunner = new BMessageRunner(BMessenger(this),
			new BMessage(B_PULSE), rate);
		fPulseRate = rate;
		fPulseEnabled = true;
		return;
	}

	if (rate == 0 && fPulseEnabled) {
		delete fPulseRunner;
		fPulseRunner = NULL;

		fPulseRate = rate;
		fPulseEnabled = false;
		return;
	}

	fPulseRunner->SetInterval(rate);
}


bigtime_t
BWindow::PulseRate() const
{
	// TODO: What about locking?!?
	return fPulseRate;
}


void
BWindow::AddShortcut(uint32 key, uint32 modifiers, BMenuItem *item)
{
	if (item->Message())
		AddShortcut(key, modifiers, new BMessage(*item->Message()), this);
}


void
BWindow::AddShortcut(uint32 key, uint32 modifiers, BMessage *msg)
{
	AddShortcut(key, modifiers, msg, this);
}


void
BWindow::AddShortcut(uint32 key, uint32 modifiers, BMessage *msg, BHandler *target)
{
	// NOTE: I'm not sure if it is OK to use 'key'
	// TODO: What about locking?!?

	if (msg == NULL)
		return;

	int64 when = real_time_clock_usecs();
	msg->AddInt64("when", when);

	// TODO: make sure key is a lowercase char !!!

	modifiers = modifiers | B_COMMAND_KEY;

	_BCmdKey *cmdKey = new _BCmdKey(key, modifiers, msg);

	if (target)
		cmdKey->targetToken	= _get_object_token_(target);

	// removes the shortcut from accelList if it exists!
	RemoveShortcut(key, modifiers);

	accelList.AddItem((void*)cmdKey);
}


void
BWindow::RemoveShortcut(uint32 key, uint32 modifiers)
{
	// TODO: What about locking?!?
	int32 index = findShortcut(key, modifiers | B_COMMAND_KEY);
	if (index >=0) {
		_BCmdKey *cmdKey = (_BCmdKey *)accelList.ItemAt(index);

		accelList.RemoveItem(index);
		delete cmdKey;
	}
}


BButton *
BWindow::DefaultButton() const
{
	// TODO: What about locking?!?
	return fDefaultButton;
}


void
BWindow::SetDefaultButton(BButton *button)
{
	// TODO: What about locking?!?
	if (fDefaultButton == button)
		return;

	if (fDefaultButton != NULL) {
		// tell old button it's no longer the default one
		BButton *oldDefault = fDefaultButton;
		oldDefault->MakeDefault(false);
		oldDefault->Invalidate();
	}

	fDefaultButton = button;

	if (button != NULL) {
		// notify new default button
		fDefaultButton->MakeDefault(true);
		fDefaultButton->Invalidate();
	}
}


bool
BWindow::NeedsUpdate() const
{
	// TODO: What about locking?!?

	const_cast<BWindow *>(this)->Lock();	
	fLink->StartMessage(AS_NEEDS_UPDATE);

	int32 code = SERVER_FALSE;
	fLink->FlushWithReply(code);

	const_cast<BWindow *>(this)->Unlock();

	return code == SERVER_TRUE;
}


void
BWindow::UpdateIfNeeded()
{
	// TODO: What about locking?!?
	// works only from this thread
	if (find_thread(NULL) != Thread())
		return;

	// Since we're blocking the event loop, we need to retrieve 
	// all messages that are pending on the port.
	DequeueAll();

	BMessageQueue *queue = MessageQueue();
	queue->Lock();

	// First process and remove any _UPDATE_ message in the queue
	// According to Adi, there can only be one at a time

	BMessage *msg;
	for (int32 i = 0; (msg = queue->FindMessage(i)) != NULL; i++) {
		if (msg->what == _UPDATE_) {
			BWindow::DispatchMessage(msg, this);
				// we need to make sure that no overridden method is called 
				// here; for BWindow::DispatchMessage() we now exactly what
				// will happen
			queue->RemoveMessage(msg);
			delete msg;
			break;
		}
	}

	queue->Unlock();
}


BView *
BWindow::FindView(const char *viewName) const
{
	// TODO: What about locking?!?
	return findView(top_view, viewName);
}


BView *
BWindow::FindView(BPoint point) const
{
	// TODO: What about locking?!?
	return findView(top_view, point);
}


BView *BWindow::CurrentFocus() const
{
	// TODO: What about locking?!?
	return fFocus;
}


void
BWindow::Activate(bool active)
{
	// TODO: What about locking?!?
	if (IsHidden())
		return;

	Lock();
	fLink->StartMessage(AS_ACTIVATE_WINDOW);
	fLink->Attach<bool>(active);
	fLink->Flush();
	Unlock();
}


void
BWindow::WindowActivated(bool state)
{
	// hook function
	// does nothing
}


void
BWindow::ConvertToScreen(BPoint *point) const
{
	point->x += fFrame.left;
	point->y += fFrame.top;
}


BPoint
BWindow::ConvertToScreen(BPoint point) const
{
	return point + fFrame.LeftTop();
}


void
BWindow::ConvertFromScreen(BPoint *point) const
{
	point->x -= fFrame.left;
	point->y -= fFrame.top;
}


BPoint
BWindow::ConvertFromScreen(BPoint point) const
{
	return point - fFrame.LeftTop();
}


void
BWindow::ConvertToScreen(BRect *rect) const
{
	rect->OffsetBy(fFrame.LeftTop());
}


BRect
BWindow::ConvertToScreen(BRect rect) const
{
	return rect.OffsetByCopy(fFrame.LeftTop());
}


void
BWindow::ConvertFromScreen(BRect* rect) const
{
	rect->OffsetBy(-fFrame.left, -fFrame.top);
}


BRect
BWindow::ConvertFromScreen(BRect rect) const
{
	return rect.OffsetByCopy(-fFrame.left, -fFrame.top);
}


bool 
BWindow::IsMinimized() const
{
	// Hiding takes precendence over minimization!!!
	if (IsHidden())
		return false;

	return fMinimized;
}


BRect
BWindow::Bounds() const
{
	return BRect(0, 0, fFrame.Width(), fFrame.Height());
}


BRect
BWindow::Frame() const
{
	return fFrame;
}


const char *
BWindow::Title() const
{
	return fTitle;
}


void
BWindow::SetTitle(const char *title)
{
	if (title == NULL)
		title = "";

	free(fTitle);
	fTitle = strdup(title);

	// we will change BWindow's thread name to "w>window title"	

	char threadName[B_OS_NAME_LENGTH];
	strcpy(threadName, "w>");
#ifdef __HAIKU__
	strlcat(threadName, title, B_OS_NAME_LENGTH);
#else
	int32 length = strlen(title);
	length = min_c(length, B_OS_NAME_LENGTH - 3);
	memcpy(threadName + 2, title, length);
	threadName[length + 2] = '\0';
#endif

	// change the handler's name
	SetName(threadName);

	// if the message loop has been started...
	if (Thread() >= B_OK) {
		rename_thread(Thread(), threadName);

		// we notify the app_server so we can actually see the change
		if (Lock()) {
			fLink->StartMessage(AS_SET_WINDOW_TITLE);
			fLink->AttachString(fTitle);
			fLink->Flush();
			Unlock();
		}
	}
}


bool
BWindow::IsActive() const
{
	return fActive;
}


void
BWindow::SetKeyMenuBar(BMenuBar *bar)
{
	fKeyMenuBar = bar;
}


BMenuBar *
BWindow::KeyMenuBar() const
{
	return fKeyMenuBar;
}


bool
BWindow::IsModal() const
{
	return fFeel == B_MODAL_SUBSET_WINDOW_FEEL
		|| fFeel == B_MODAL_APP_WINDOW_FEEL
		|| fFeel == B_MODAL_ALL_WINDOW_FEEL;
}


bool
BWindow::IsFloating() const
{
	return fFeel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| fFeel == B_FLOATING_APP_WINDOW_FEEL
		|| fFeel == B_FLOATING_ALL_WINDOW_FEEL;
}


status_t
BWindow::AddToSubset(BWindow *window)
{
	if (window == NULL || window->Feel() != B_NORMAL_WINDOW_FEEL
		|| (fFeel != B_MODAL_SUBSET_WINDOW_FEEL
			&& fFeel != B_FLOATING_SUBSET_WINDOW_FEEL))
		return B_BAD_VALUE;

	team_id team = Team();

	Lock();
	fLink->StartMessage(AS_ADD_TO_SUBSET);
	fLink->Attach<int32>(_get_object_token_(window));
	fLink->Attach<team_id>(team);

	int32 code = SERVER_FALSE;
	fLink->FlushWithReply(code);

	Unlock();

	return code == SERVER_TRUE ? B_OK : B_ERROR;
}


status_t
BWindow::RemoveFromSubset(BWindow *window)
{
	if (window == NULL || window->Feel() != B_NORMAL_WINDOW_FEEL
		|| (fFeel != B_MODAL_SUBSET_WINDOW_FEEL
			&& fFeel != B_FLOATING_SUBSET_WINDOW_FEEL))
		return B_BAD_VALUE;

	team_id team = Team();

	Lock();
	fLink->StartMessage(AS_REM_FROM_SUBSET);
	fLink->Attach<int32>(_get_object_token_(window));
	fLink->Attach<team_id>(team);

	int32 code;
	fLink->FlushWithReply(code);
	Unlock();

	return code == SERVER_TRUE ? B_OK : B_ERROR;
}


status_t
BWindow::Perform(perform_code d, void *arg)
{
	return BLooper::Perform(d, arg);
}


status_t
BWindow::SetType(window_type type)
{
	window_look look;
	window_feel feel;
	decomposeType(type, &look, &feel);

	status_t status = SetLook(look);
	if (status == B_OK)
		status = SetFeel(feel);

	return status;
}


window_type
BWindow::Type() const
{
	return composeType(fLook, fFeel);
}


status_t
BWindow::SetLook(window_look look)
{
	BAutolock locker(this);

	fLink->StartMessage(AS_SET_LOOK);
	fLink->Attach<int32>((int32)look);

	int32 code;
	status_t status = fLink->FlushWithReply(code);

	// ToDo: the server should probably return something more meaningful, anyway
	if (status == B_OK && code == SERVER_TRUE) {
		fLook = look;
		return B_OK;
	}

	return B_ERROR;
}


window_look
BWindow::Look() const
{
	return fLook;
}


status_t
BWindow::SetFeel(window_feel feel)
{
	// ToDo: that should probably be done by the server, not the window
	if (feel != B_NORMAL_WINDOW_FEEL
		&& feel != B_MODAL_SUBSET_WINDOW_FEEL
		&& feel != B_MODAL_APP_WINDOW_FEEL
		&& feel != B_MODAL_ALL_WINDOW_FEEL
		&& feel != B_FLOATING_SUBSET_WINDOW_FEEL
		&& feel != B_FLOATING_APP_WINDOW_FEEL
		&& feel != B_FLOATING_ALL_WINDOW_FEEL)
		return B_BAD_VALUE;

	Lock();
	fLink->StartMessage(AS_SET_FEEL);
	fLink->Attach<int32>((int32)feel);
	fLink->Flush();
	Unlock();

	// ToDo: return code from server?
	fFeel = feel;

	return B_OK;
}


window_feel
BWindow::Feel() const
{
	return fFeel;
}


status_t
BWindow::SetFlags(uint32 flags)
{

	Lock();	
	fLink->StartMessage(AS_SET_FLAGS);
	fLink->Attach<uint32>(flags);

	int32 code = SERVER_FALSE;
	fLink->FlushWithReply(code);

	Unlock();

	if (code == SERVER_TRUE) {
		fFlags = flags;
		return B_OK;
	}

	return B_ERROR;
}


uint32
BWindow::Flags() const
{
	return fFlags;
}


status_t
BWindow::SetWindowAlignment(window_alignment mode,
	int32 h, int32 hOffset, int32 width, int32 widthOffset,
	int32 v, int32 vOffset, int32 height, int32 heightOffset)
{
	if ((mode & (B_BYTE_ALIGNMENT | B_PIXEL_ALIGNMENT)) == 0
		|| (hOffset >= 0 && hOffset <= h)
		|| (vOffset >= 0 && vOffset <= v)
		|| (widthOffset >= 0 && widthOffset <= width)
		|| (heightOffset >= 0 && heightOffset <= height))
		return B_BAD_VALUE;

	// TODO: test if hOffset = 0 and set it to 1 if true.

	Lock();
	fLink->StartMessage(AS_SET_ALIGNMENT);
	fLink->Attach<int32>((int32)mode);
	fLink->Attach<int32>(h);
	fLink->Attach<int32>(hOffset);
	fLink->Attach<int32>(width);
	fLink->Attach<int32>(widthOffset);
	fLink->Attach<int32>(v);
	fLink->Attach<int32>(vOffset);
	fLink->Attach<int32>(height);
	fLink->Attach<int32>(heightOffset);

	int32 code = SERVER_FALSE;
	fLink->FlushWithReply(code);

	Unlock();

	if (code == SERVER_TRUE)
		return B_OK;

	return B_ERROR;
}


status_t
BWindow::GetWindowAlignment(window_alignment *mode,
	int32 *h, int32 *hOffset, int32 *width, int32 *widthOffset,
	int32 *v, int32 *vOffset, int32 *height, int32 *heightOffset) const
{
	const_cast<BWindow *>(this)->Lock();
	fLink->StartMessage(AS_GET_ALIGNMENT);

	int32 code = SERVER_FALSE;
	if (fLink->FlushWithReply(code) == B_OK
		&& code == SERVER_TRUE) {
		fLink->Read<int32>((int32 *)mode);
		fLink->Read<int32>(h);
		fLink->Read<int32>(hOffset);
		fLink->Read<int32>(width);
		fLink->Read<int32>(widthOffset);
		fLink->Read<int32>(v);
		fLink->Read<int32>(hOffset);
		fLink->Read<int32>(height);
		fLink->Read<int32>(heightOffset);
	}

	const_cast<BWindow *>(this)->Unlock();

	if (code != SERVER_TRUE)
		return B_ERROR;

	return B_OK;
}


uint32
BWindow::Workspaces() const
{
	uint32 workspaces = 0;

	const_cast<BWindow *>(this)->Lock();
	fLink->StartMessage(AS_GET_WORKSPACES);

	int32 code;
	if (fLink->FlushWithReply(code) == B_OK
		&& code == SERVER_TRUE)
		fLink->Read<uint32>(&workspaces);

	const_cast<BWindow *>(this)->Unlock();

	// TODO: shouldn't we cache?
	return workspaces;
}


void
BWindow::SetWorkspaces(uint32 workspaces)
{
	// TODO: don't forget about Tracker's background window.
	if (fFeel != B_NORMAL_WINDOW_FEEL)
		return;

	Lock();
	fLink->StartMessage(AS_SET_WORKSPACES);
	fLink->Attach<uint32>(workspaces);
	fLink->Flush();
	Unlock();
}


BView *
BWindow::LastMouseMovedView() const
{
	return fLastMouseMovedView;
}


void 
BWindow::MoveBy(float dx, float dy)
{
	if (dx == 0.0 && dy == 0.0)
		return;

	Lock();

	fLink->StartMessage(AS_WINDOW_MOVE);
	fLink->Attach<float>(dx);
	fLink->Attach<float>(dy);
	fLink->Flush();

	fFrame.OffsetBy(dx, dy);

	Unlock();
}


void
BWindow::MoveTo(BPoint point)
{
	Lock();

	if (fFrame.left != point.x || fFrame.top != point.y) {
		float xOffset = point.x - fFrame.left;
		float yOffset = point.y - fFrame.top;

		MoveBy(xOffset, yOffset);
	}

	Unlock();
}


void
BWindow::MoveTo(float x, float y)
{
	MoveTo(BPoint(x, y));
}


void
BWindow::ResizeBy(float dx, float dy)
{
	Lock();
	// stay in minimum & maximum frame limits
	if (fFrame.Width() + dx < fMinWidth)
		dx = fMinWidth - fFrame.Width();
	if (fFrame.Width() + dx > fMaxWidth)
		dx = fMaxWidth - fFrame.Width();
	if (fFrame.Height() + dy < fMinHeight)
		dy = fMinHeight - fFrame.Height();
	if (fFrame.Height() + dy > fMaxHeight)
		dy = fMaxHeight - fFrame.Height();

	if (dx != 0.0 || dy != 0.0) {
		fLink->StartMessage(AS_WINDOW_RESIZE);
		fLink->Attach<float>(dx);
		fLink->Attach<float>(dy);
		fLink->Flush();

		fFrame.SetRightBottom(fFrame.RightBottom() + BPoint(dx, dy));
	}
	Unlock();
}


void
BWindow::ResizeTo(float width, float height)
{
	Lock();
	ResizeBy(width - fFrame.Width(), height - fFrame.Height());
	Unlock();
}


void
BWindow::Show()
{
	bool isLocked = this->IsLocked();

	fShowLevel--;

	if (fShowLevel == 0) {
		STRACE(("BWindow(%s): sending AS_SHOW_WINDOW message...\n", Name()));
		if (Lock()) {
			fLink->StartMessage(AS_SHOW_WINDOW);
			fLink->Flush();
			Unlock();
		}
	}

	// if it's the fist time Show() is called... start the Looper thread.
	if (Thread() == B_ERROR) {
		// normally this won't happen, but I want to be sure!
		if (!isLocked) 
			Lock();
		Run();
	}
}


void
BWindow::Hide()
{
	if (fShowLevel == 0 && Lock()) {
		fLink->StartMessage(AS_HIDE_WINDOW);
		fLink->Flush();
		Unlock();
	}
	fShowLevel++;
}


bool
BWindow::IsHidden() const
{
	return fShowLevel > 0; 
}


bool
BWindow::QuitRequested()
{
	return BLooper::QuitRequested();
}


thread_id
BWindow::Run()
{
	return BLooper::Run();
}


status_t
BWindow::GetSupportedSuites(BMessage *data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("Suites", "suite/vnd.Be-window");
	if (status == B_OK) {
		BPropertyInfo propertyInfo(sWindowPropInfo);

		status = data->AddFlat("message", &propertyInfo);
		if (status == B_OK)
			status = BLooper::GetSupportedSuites(data);
	}

	return status;
}


BHandler *
BWindow::ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
	int32 what,	const char *property)
{
	if (msg->what == B_WINDOW_MOVE_BY
		|| msg->what == B_WINDOW_MOVE_TO)
		return this;

	BPropertyInfo propertyInfo(sWindowPropInfo);
	if (propertyInfo.FindMatch(msg, index, specifier, what, property) >= 0) {
		if (!strcmp(property, "View")) {
			// we will NOT pop the current specifier
			return top_view;
		} else if (!strcmp(property, "MenuBar")) {
			if (fKeyMenuBar) {
				msg->PopSpecifier();
				return fKeyMenuBar;
			} else {
				BMessage replyMsg(B_MESSAGE_NOT_UNDERSTOOD);
				replyMsg.AddInt32("error", B_NAME_NOT_FOUND);
				replyMsg.AddString("message", "This window doesn't have a main MenuBar");
				msg->SendReply(&replyMsg);
				return NULL;
			}
		} else
			return this;
	}

	return BLooper::ResolveSpecifier(msg, index, specifier, what, property);
}


//	#pragma mark -
//--------------------Private Methods-------------------------------------------


void 
BWindow::InitData(BRect frame, const char* title, window_look look,
	window_feel feel, uint32 flags,	uint32 workspace, int32 bitmapToken)
{
	STRACE(("BWindow::InitData()\n"));

	fTitle = NULL;

	if (be_app == NULL) {
		debugger("You need a valid BApplication object before interacting with the app_server");
		return;
	}

	fFrame = frame;

	// ToDo: that looks wrong...
	SetTitle(title ? title : "no_name_window");

	fFeel = feel;
	fLook = look;
	fFlags = flags;

	fInTransaction = false;
	fActive = false;
	fShowLevel = 1;

	top_view = NULL;
	fFocus = NULL;
	fLastMouseMovedView	= NULL;
	fKeyMenuBar = NULL;
	fDefaultButton = NULL;

	AddShortcut('X', B_COMMAND_KEY, new BMessage(B_CUT), NULL);
	AddShortcut('C', B_COMMAND_KEY, new BMessage(B_COPY), NULL);
	AddShortcut('V', B_COMMAND_KEY, new BMessage(B_PASTE), NULL);
	AddShortcut('A', B_COMMAND_KEY, new BMessage(B_SELECT_ALL), NULL);
	AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));

	fPulseEnabled = false;
	fPulseRate = 0;
	fPulseRunner = NULL;

	// TODO: is this correct??? should the thread loop be started???
	//SetPulseRate( 500000 );

	// TODO:  see if you can use 'fViewsNeedPulse'

	fIsFilePanel = false;

	// TODO: see WHEN is this used!
	fMaskActivated = false;

	// TODO: see WHEN is this used!
	fWaitingForMenu = false;
	fMenuSem = -1;

	fMinimized = false;

	fMaxZoomHeight = 32768.0;
	fMaxZoomWidth = 32768.0;
	fMinHeight = 0.0;
	fMinWidth = 0.0;
	fMaxHeight = 32768.0;
	fMaxWidth = 32768.0;

	fLastViewToken = B_NULL_TOKEN;

	// TODO: other initializations!

	// Create the server-side window

	port_id receivePort = create_port(B_LOOPER_PORT_DEFAULT_CAPACITY, "w_rcv_port");
	if (receivePort < B_OK) {
		debugger("Could not create BWindow's receive port, used for interacting with the app_server!");
		delete this;
		return;
	}

	STRACE(("BWindow::InitData(): contacting app_server...\n"));

	// HERE we are in BApplication's thread, so for locking we use be_app variable
	// we'll lock the be_app to be sure we're the only one writing at BApplication's server port
	bool locked = false;
	if (!be_app->IsLocked()) {
		be_app->Lock();
		locked = true; 
	}

	// let app_server know that a window has been created.
	fLink = new BPrivate::PortLink(
		BApplication::Private::ServerLink()->SenderPort(), receivePort);

	if (bitmapToken < 0) {
		fLink->StartMessage(AS_CREATE_WINDOW);
	} else {
		fLink->StartMessage(AS_CREATE_OFFSCREEN_WINDOW);
		fLink->Attach<int32>(bitmapToken);
	}

	fLink->Attach<BRect>(fFrame);
	fLink->Attach<uint32>((uint32)fLook);
	fLink->Attach<uint32>((uint32)fFeel);
	fLink->Attach<uint32>(fFlags);
	fLink->Attach<uint32>(workspace);
	fLink->Attach<int32>(_get_object_token_(this));
	fLink->Attach<port_id>(receivePort);
	fLink->Attach<port_id>(fMsgPort);
	fLink->AttachString(title);

	port_id sendPort;
	int32 code;
	if (fLink->FlushWithReply(code) == B_OK
		&& code == SERVER_TRUE
		&& fLink->Read<port_id>(&sendPort) == B_OK) {
		fLink->SetSenderPort(sendPort);

		// read the frame size and its limits that were really
		// enforced on the server side

		fLink->Read<BRect>(&fFrame);
		fLink->Read<float>(&fMinWidth);
		fLink->Read<float>(&fMaxWidth);
		fLink->Read<float>(&fMinHeight);
		fLink->Read<float>(&fMaxHeight);

		fMaxZoomWidth = fMaxWidth;
		fMaxZoomHeight = fMaxHeight;
	} else
		sendPort = -1;

	if (locked)
		be_app->Unlock();

	STRACE(("Server says that our send port is %ld\n", sendPort));
	STRACE(("Window locked?: %s\n", IsLocked() ? "True" : "False"));

	// build and register top_view with app_server
	BuildTopView();
}


/**	Reads all pending messages from the window port and put them into the queue.
 */

void
BWindow::DequeueAll()
{
	//	Get message count from port
	int32 count = port_count(fMsgPort);

	for (int32 i = 0; i < count; i++) {
		BMessage *message = MessageFromPort(0);
		if (message != NULL)
			fQueue->AddMessage(message);
	}
}


// TODO: This here is a nearly full code duplication to BLooper::task_loop
// but with one little difference: It uses the determine_target function
// to tell what the later target of a message will be, if no explicit target
// is supplied. This is important because we need to call the right targets
// MessageFilter. For B_KEY_DOWN messages for example, not the BWindow but the
// focus view will be the target of the message. This means that also the
// focus views MessageFilters have to be checked before DispatchMessage and
// not the ones of this BWindow.

void 
BWindow::task_looper()
{
	STRACE(("info: BWindow::task_looper() started.\n"));

	//	Check that looper is locked (should be)
	AssertLocked();
	//	Unlock the looper
	Unlock();

	if (IsLocked())
		debugger("window must not be locked!");

	//	loop: As long as we are not terminating.
	while (!fTerminating) {
		// TODO: timeout determination algo
		//	Read from message port (how do we determine what the timeout is?)
		BMessage* msg = MessageFromPort();

		//	Did we get a message?
		if (msg) {
			//	Add to queue
			fQueue->AddMessage(msg);
		} else
			continue;

		//	Get message count from port
		int32 msgCount = port_count(fMsgPort);
		for (int32 i = 0; i < msgCount; ++i) {
			//	Read 'count' messages from port (so we will not block)
			//	We use zero as our timeout since we know there is stuff there
			msg = MessageFromPort(0);
			//	Add messages to queue
			if (msg)
				fQueue->AddMessage(msg);
		}

		//	loop: As long as there are messages in the queue and the port is
		//		  empty... and we are not terminating, of course.
		bool dispatchNextMessage = true;
		while (!fTerminating && dispatchNextMessage) {
			//	Get next message from queue (assign to fLastMessage)
			fLastMessage = fQueue->NextMessage();

			//	Lock the looper
			Lock();
			if (!fLastMessage) {
				// No more messages: Unlock the looper and terminate the
				// dispatch loop.
				dispatchNextMessage = false;
			} else {
				//	Get the target handler
				//	Use BMessage friend functions to determine if we are using the
				//	preferred handler, or if a target has been specified
				BHandler* handler;
				if (_use_preferred_target_(fLastMessage)) {
					handler = fPreferred;
				} else {
					/**
						@note	Here is where all the token stuff starts to
								make sense.  How, exactly, do we determine
								what the target BHandler is?  If we look at
								BMessage, we see an int32 field, fTarget.
								Amazingly, we happen to have a global mapping
								of BHandler pointers to int32s!
					 */
					gDefaultTokens.GetToken(_get_message_target_(fLastMessage),
						B_HANDLER_TOKEN, (void **)&handler);
				}

				if (!handler) {
					handler = determine_target(fLastMessage, handler, false);
					if (!handler)
						handler = this;
				}

				//	Is this a scripting message? (BMessage::HasSpecifiers())
				if (fLastMessage->HasSpecifiers()) {
					int32 index = 0;
					// Make sure the current specifier is kosher
					if (fLastMessage->GetCurrentSpecifier(&index) == B_OK)
						handler = resolve_specifier(handler, fLastMessage);
				}

				if (handler) {
					//	Do filtering
					handler = top_level_filter(fLastMessage, handler);
					if (handler && handler->Looper() == this)
						DispatchMessage(fLastMessage, handler);
				}
			}

			Unlock();

			//	Delete the current message (fLastMessage)
			delete fLastMessage;
			fLastMessage = NULL;

			//	Are any messages on the port?
			if (port_count(fMsgPort) > 0) {
				//	Do outer loop
				dispatchNextMessage = false;
			}
		}
	}
}


window_type
BWindow::composeType(window_look look,
	window_feel feel) const
{
	switch (feel) {
		case B_NORMAL_WINDOW_FEEL:
			switch (look) {
				case B_TITLED_WINDOW_LOOK:
					return B_TITLED_WINDOW;

				case B_DOCUMENT_WINDOW_LOOK:
					return B_DOCUMENT_WINDOW;

				case B_BORDERED_WINDOW_LOOK:
					return B_BORDERED_WINDOW;
				
				default:
					return B_UNTYPED_WINDOW;
			}
			break;

		case B_MODAL_APP_WINDOW_FEEL:
			if (look == B_MODAL_WINDOW_LOOK)
				return B_MODAL_WINDOW;
			break;

		case B_FLOATING_APP_WINDOW_FEEL:
			if (look == B_FLOATING_WINDOW_LOOK)
				return B_FLOATING_WINDOW;
			break;

		default:
			return B_UNTYPED_WINDOW;
	}

	return B_UNTYPED_WINDOW;
}


void
BWindow::decomposeType(window_type type, window_look *look,
	window_feel *feel) const
{
	switch (type) {
		case B_TITLED_WINDOW:
		{
			*look = B_TITLED_WINDOW_LOOK;
			*feel = B_NORMAL_WINDOW_FEEL;
			break;
		}
		case B_DOCUMENT_WINDOW:
		{
			*look = B_DOCUMENT_WINDOW_LOOK;
			*feel = B_NORMAL_WINDOW_FEEL;
			break;
		}
		case B_MODAL_WINDOW:
		{
			*look = B_MODAL_WINDOW_LOOK;
			*feel = B_MODAL_APP_WINDOW_FEEL;
			break;
		}
		case B_FLOATING_WINDOW:
		{
			*look = B_FLOATING_WINDOW_LOOK;
			*feel = B_FLOATING_APP_WINDOW_FEEL;
			break;
		}
		case B_BORDERED_WINDOW:
		{
			*look = B_BORDERED_WINDOW_LOOK;
			*feel = B_NORMAL_WINDOW_FEEL;
			break;
		}
		case B_UNTYPED_WINDOW:
		{
			*look = B_TITLED_WINDOW_LOOK;
			*feel = B_NORMAL_WINDOW_FEEL;
			break;
		}
		default:
		{
			*look = B_TITLED_WINDOW_LOOK;
			*feel = B_NORMAL_WINDOW_FEEL;
			break;
		}
	}
}


void
BWindow::BuildTopView()
{
	STRACE(("BuildTopView(): enter\n"));

	BRect frame = fFrame.OffsetToCopy(B_ORIGIN);
	top_view = new BView(frame, "top_view",
		B_FOLLOW_ALL, B_WILL_DRAW);
	top_view->top_level_view = true;

	//inhibit check_lock()
	fLastViewToken = _get_object_token_(top_view);

	// set top_view's owner, add it to window's eligible handler list
	// and also set its next handler to be this window.

	STRACE(("Calling setowner top_view = %p this = %p.\n", 
		top_view, this));

	top_view->_SetOwner(this);

	//we can't use AddChild() because this is the top_view
  	top_view->attachView(top_view);

	STRACE(("BuildTopView ended\n"));
}


void
BWindow::prepareView(BView *view)
{
	// TODO: implement
}


void
BWindow::attachView(BView *view)
{
	// TODO: implement
}


void
BWindow::detachView(BView *view)
{
	// TODO: implement
}


void
BWindow::setFocus(BView *focusView, bool notifyInputServer)
{
	if (fFocus == focusView)
		return;

	if (focusView)
		focusView->MakeFocus(true);

	// TODO: Notify the input server if we are passing focus
	// from a view which has the B_INPUT_METHOD_AWARE to a one
	// which does not, or vice-versa
	if (notifyInputServer) {
		// TODO: Send a message to input server using
		// control_input_server()
	}
}


void
BWindow::handleActivation(bool active)
{
	WindowActivated(active);

	// recursively call hook function 'WindowActivated(bool)'
	// for all views attached to this window.
	top_view->_Activate(active);
}


BHandler *
BWindow::determine_target(BMessage *msg, BHandler *target, bool pref)
{
	// TODO: this is mostly guessed; check for correctness.
	// I think this function is used to determine if a BView will be
	// the target of a message. This is used in the BLooper::task_loop
	// to determine what BHandler will dispatch the message and what filters
	// should be checked before doing so.
	
	switch (msg->what) {
		case B_KEY_DOWN:
		case B_KEY_UP:
		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
		case B_MODIFIERS_CHANGED:
		case B_MOUSE_WHEEL_CHANGED:
			// these messages will be dispatched by the focus view later
			return fFocus;
		
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
		case B_MOUSE_MOVED:
			// TODO: find out how to determine the target for these
			break;
		
		case B_PULSE:
		case B_QUIT_REQUESTED:
			// TODO: test wether R5 will let BView dispatch these messages
			break;
		
		case B_VIEW_RESIZED:
		case B_VIEW_MOVED: {
			int32 token = B_NULL_TOKEN;
			msg->FindInt32("_token", &token);
			BView *view = findView(top_view, token);
			if (view)
				return view;
			break;
		}
		default: 
			break;
	}

	return target;
}


bool
BWindow::_HandleKeyDown(char key, uint32 modifiers)
{
	// TODO: ask people if using 'raw_char' is OK ?

	// handle BMenuBar key
	if (key == B_ESCAPE && (modifiers & B_COMMAND_KEY) != 0
		&& fKeyMenuBar) {
		// TODO: ask Marc about 'fWaitingForMenu' member!

		// fWaitingForMenu = true;
		fKeyMenuBar->StartMenuBar(0, true, false, NULL);
		return true;
	}

	// Command+q has been pressed, so, we will quit
	if ((key == 'Q' || key == 'q') && (modifiers & B_COMMAND_KEY) != 0) {
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	// Keyboard navigation through views
	// (B_OPTION_KEY makes BTextViews and friends navigable, even in editing mode)
	if (key == B_TAB && (modifiers & (B_COMMAND_KEY | B_OPTION_KEY)) != 0) {
		_KeyboardNavigation();
		return true;
	}

	// Handle shortcuts
	int index;
	if ((index = findShortcut(key, modifiers)) >= 0) {
		_BCmdKey *cmdKey = (_BCmdKey*)accelList.ItemAt(index);

		// we'll give the message to the focus view
		if (cmdKey->targetToken == B_ANY_TOKEN) {
			fFocus->MessageReceived(cmdKey->message);
			return true;
		} else {
			BHandler *target = NULL;
			int32 count = CountHandlers();

			// ToDo: this looks wrong: why not just send a message to the
			//	target? Only if the target is a handler of this looper we
			//	can do what is done below.

			// search for a match through BLooper's list of eligible handlers
			for (int32 i = 0; i < count; i++) {
				BHandler *handler = HandlerAt(i);

				// do we have a match?
				if (_get_object_token_(handler) == cmdKey->targetToken) {
					// yes, we do.
					target = handler;
					break;
				}
			}

			if (target)
				target->MessageReceived(cmdKey->message);
			else {
				// if no handler was found, BWindow will handle the message
				MessageReceived(cmdKey->message);
			}
		}
		return true;
	}

	// if <ENTER> is pressed and we have a default button
	if (DefaultButton() && key == B_ENTER) {
		const char *chars;
		CurrentMessage()->FindString("bytes", &chars);

		DefaultButton()->KeyDown(chars, strlen(chars));
		return true;
	}

	return false;
}


void
BWindow::_KeyboardNavigation()
{
	BMessage *message = CurrentMessage();
	if (message == NULL)
		return;

	const char *bytes;
	uint32 modifiers;
	if (message->FindString("bytes", &bytes) != B_OK
		|| bytes[0] != B_TAB)
		return;

	message->FindInt32("modifiers", (int32*)&modifiers);

	BView *nextFocus;
	int32 jumpGroups = modifiers & B_CONTROL_KEY ? B_NAVIGABLE_JUMP : B_NAVIGABLE;
	if (modifiers & B_SHIFT_KEY)
		nextFocus = _FindPreviousNavigable(fFocus, jumpGroups);
	else
		nextFocus = _FindNextNavigable(fFocus, jumpGroups);

	if (nextFocus && nextFocus != fFocus)
		setFocus(nextFocus, false);
}


BMessage *
BWindow::ConvertToMessage(void *raw, int32 code)
{
	return BLooper::ConvertToMessage(raw, code);
}


int32
BWindow::findShortcut(uint32 key, uint32 modifiers)
{
	int32 count = accelList.CountItems();

	for (int32 index = 0; index < count; index++) {
		_BCmdKey *cmdKey = (_BCmdKey *)accelList.ItemAt(index);

		if (cmdKey->key == key && cmdKey->modifiers == modifiers)
			return index;
	}

	return -1;
}


BView *
BWindow::findView(BView *view, int32 token)
{
	if (_get_object_token_(view) == token)
		return view;

	BView *child = view->fFirstChild;

	while (child != NULL) {
		if ((view = findView(child, token)) != NULL)
			return view;

		child = child->fNextSibling;
	}

	return NULL;
}


BView *
BWindow::findView(BView *view, const char *name) const
{
	if (!strcmp(name, view->Name()))
		return view;

	BView *child = view->fFirstChild;

	while (child != NULL) {
		if ((view = findView(child, name)) != NULL)
			return view;

		child = child->fNextSibling;
	}

	return NULL;
}


BView *
BWindow::findView(BView *view, BPoint point) const
{
	if (view->Bounds().Contains(point) && !view->fFirstChild)
		return view;

	BView *child = view->fFirstChild;

	while (child != NULL) {
		if ((view = findView(child, point)) != NULL)
			return view;

		child = child->fNextSibling;
	}

	return NULL;
}


BView *
BWindow::_FindNextNavigable(BView *focus, uint32 flags)
{
	if (focus == NULL)
		focus = top_view;

	BView *nextFocus = focus;

	// Search the tree for views that accept focus
	while (true) {
		if (nextFocus->fFirstChild)
			nextFocus = nextFocus->fFirstChild;
		else if (nextFocus->fNextSibling)
			nextFocus = nextFocus->fNextSibling;
		else {
			while (!nextFocus->fNextSibling && nextFocus->fParent)
				nextFocus = nextFocus->fParent;

			if (nextFocus == top_view)
				nextFocus = nextFocus->fFirstChild;
			else
				nextFocus = nextFocus->fNextSibling;
		}

		// It means that the hole tree has been searched and there is no
		// view with B_NAVIGABLE_JUMP flag set!
		if (nextFocus == focus)
			return NULL;

		if (nextFocus->Flags() & flags)
			return nextFocus;
	}
}


BView *
BWindow::_FindPreviousNavigable(BView *focus, uint32 flags)
{
	BView *prevFocus = focus;

	// Search the tree for views that accept focus
	while (true) {
		BView *view;
		if ((view = findLastChild(prevFocus)) != NULL)
			prevFocus = view;
		else if (prevFocus->fPreviousSibling)
			prevFocus = prevFocus->fPreviousSibling;
		else {
			while (!prevFocus->fPreviousSibling && prevFocus->fParent)
				prevFocus = prevFocus->fParent;

			if (prevFocus == top_view)
				prevFocus = findLastChild(prevFocus);
			else
				prevFocus = prevFocus->fPreviousSibling;
		}

		// It means that the hole tree has been searched and there is no
		// view with B_NAVIGABLE_JUMP flag set!
		if (prevFocus == focus)
			return NULL;

		if (prevFocus->Flags() & flags)
			return prevFocus;
	}
}


BView *
BWindow::findLastChild(BView *parent)
{
	BView *last = parent->fFirstChild;
	if (last == NULL)
		return NULL;

	while (last->fNextSibling)
		last = last->fNextSibling;

	return last;
}


void
BWindow::drawAllViews(BView* aView)
{
	if (Lock()) {
		top_view->Invalidate();
		Unlock();
	}
	Sync();
}


void
BWindow::DoUpdate(BView *view, BRect &area)
{
	STRACE(("info: BWindow::DoUpdate() BRect(%f,%f,%f,%f) called.\n",
		area.left, area.top, area.right, area.bottom));

	// don't draw hidden views or their children
	if (view->IsHidden(view))
		return;

	view->check_lock();

	if (view->Flags() & B_WILL_DRAW) {
		// ToDo: make states robust
		view->PushState();
		view->Draw(area);
		view->PopState();
	} else {
		// The code below is certainly not correct, because
		// it redoes what the app_server already did
		// Find out what happens on R5 if a view has ViewColor() = 
		// B_TRANSPARENT_COLOR but not B_WILL_DRAW
/*		rgb_color c = aView->HighColor();
		aView->SetHighColor(aView->ViewColor());
		aView->FillRect(aView->Bounds(), B_SOLID_HIGH);
		aView->SetHighColor(c);*/
	}

	BView *child = view->fFirstChild;
	while (child) {
		if (area.Intersects(child->Frame())) {
			BRect newArea = area & child->Frame();
			child->ConvertFromParent(&newArea);

			DoUpdate(child, newArea);
		}
		child = child->fNextSibling; 
	}

	if (view->Flags() & B_WILL_DRAW) {
		view->PushState();
		view->DrawAfterChildren(area);
		view->PopState();
	}
}


void
BWindow::SetIsFilePanel(bool yes)
{
	// TODO: is this not enough?
	fIsFilePanel = yes;
}


bool
BWindow::IsFilePanel() const
{
	return fIsFilePanel;
}


//------------------------------------------------------------------------------
// Virtual reserved Functions

void BWindow::_ReservedWindow1() {}
void BWindow::_ReservedWindow2() {}
void BWindow::_ReservedWindow3() {}
void BWindow::_ReservedWindow4() {}
void BWindow::_ReservedWindow5() {}
void BWindow::_ReservedWindow6() {}
void BWindow::_ReservedWindow7() {}
void BWindow::_ReservedWindow8() {}

void
BWindow::PrintToStream() const
{
	printf("BWindow '%s' data:\
		Title			= %s\
		Token			= %ld\
		InTransaction 	= %s\
		Active 			= %s\
		fShowLevel		= %d\
		Flags			= %lx\
		send_port		= %ld\
		receive_port	= %ld\
		top_view name	= %s\
		focus view name	= %s\
		lastMouseMoved	= %s\
		fLink			= %p\
		KeyMenuBar name	= %s\
		DefaultButton	= %s\
		# of shortcuts	= %ld",
		Name(), fTitle,
		_get_object_token_(this),		
		fInTransaction == true ? "yes" : "no",
		fActive == true ? "yes" : "no",
		fShowLevel,
		fFlags,
		fLink->SenderPort(),
		fLink->ReceiverPort(),
		top_view != NULL ? top_view->Name() : "NULL",
		fFocus != NULL ? fFocus->Name() : "NULL",
		fLastMouseMovedView != NULL ? fLastMouseMovedView->Name() : "NULL",
		fLink,
		fKeyMenuBar != NULL ? fKeyMenuBar->Name() : "NULL",
		fDefaultButton != NULL ? fDefaultButton->Name() : "NULL",
		accelList.CountItems());
/*
	for( int32 i=0; i<accelList.CountItems(); i++){
		_BCmdKey	*key = (_BCmdKey*)accelList.ItemAt(i);
		printf("\tShortCut %ld: char %s\n\t\t message: \n", i, (key->key > 127)?"ASCII":"UNICODE");
		key->message->PrintToStream();
	}
*/	
	printf("\
		topViewToken	= %ld\
		pluseEnabled	= %s\
		isFilePanel		= %s\
		MaskActivated	= %s\
		pulseRate		= %lld\
		waitingForMenu	= %s\
		minimized		= %s\
		Menu semaphore	= %ld\
		maxZoomHeight	= %f\
		maxZoomWidth	= %f\
		minWindHeight	= %f\
		minWindWidth	= %f\
		maxWindHeight	= %f\
		maxWindWidth	= %f\
		frame			= ( %f, %f, %f, %f )\
		look			= %d\
		feel			= %d\
		lastViewToken	= %ld\
		pulseRUNNER		= %s\n",
		fTopViewToken,
		fPulseEnabled==true?"Yes":"No",
		fIsFilePanel==true?"Yes":"No",
		fMaskActivated==true?"Yes":"No",
		fPulseRate,
		fWaitingForMenu==true?"Yes":"No",
		fMinimized==true?"Yes":"No",
		fMenuSem,
		fMaxZoomHeight,
		fMaxZoomWidth,
		fMinHeight,
		fMinWidth,
		fMaxHeight,
		fMaxWidth,
		fFrame.left, fFrame.top, fFrame.right, fFrame.bottom, 
		(int16)fLook,
		(int16)fFeel,
		fLastViewToken,
		fPulseRunner!=NULL?"In place":"NULL");
}

/*
TODO list:

	*) take care of temporarely events mask!!!
	*) what's with this flag B_ASYNCHRONOUS_CONTROLS ?
	*) test arguments for SetWindowAligment
	*) call hook functions: MenusBeginning, MenusEnded. Add menu activation code.
*/

