/*****************************************************************************/
// OpenBeOS InputServer
//
// Version: [0.0.5] [Development Stage]
//
// [Description]
//
//
// This application and all source files used in its construction, except 
// where noted, are licensed under the MIT License, and have been written 
// and are:
//
// Copyright (c) 2002 OpenBeOS Project
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the 
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included 
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
/*****************************************************************************/


#include <stdio.h>

#include "InputServer.h"
#include "Path.h"
#include "Directory.h"
#include "FindDirectory.h"
#include "Entry.h"
#include "Locker.h"
#include "Debug.h"

#include "InputServerDeviceListEntry.h"
#include "InputServerFilterListEntry.h"
#include "InputServerMethodListEntry.h"
#include "Message.h"

// include app_server headers for communication

#include "PortLink.h"
#include "ServerProtocol.h"

#define SERVER_PORT_NAME "OBappserver"
#define SERVER_INPUT_PORT "OBinputport"
#define X_VALUE "x"
#define Y_VALUE "y"


extern "C" void RegisterDevices(input_device_ref** devices)
{
	printf("RegisterDevices\n");
};


// Static InputServer member variables.
//
BList   InputServer::gInputDeviceList;
BLocker InputServer::gInputDeviceListLocker;

BList InputServer::mInputServerDeviceList;
BList InputServer::mInputServerFilterList;
BList InputServer::mInputServerMethodList;


/*
 *
 */
int main()
{
	InputServer	*myInputServer;
	
	myInputServer = new InputServer;
	
	myInputServer->Run();
	
	delete myInputServer;
}


/*
 *  Method: InputServer::InputServer()
 *   Descr: 
 */
InputServer::InputServer(void) : BApplication("application/x-vnd.OBOS-input_server")
{
	void *pointer;
	
	EventLoop(pointer);

	InitTestDevice();
	
//	InitDevices();
	InitFilters();
	InitMethods();
}

/*
 *  Method: InputServer::InputServer()
 *   Descr: 
 */
InputServer::~InputServer(void)
{
}


/*
 *  Method: InputServer::ArgvReceived()
 *   Descr: 
 */
void InputServer::ArgvReceived(int32 argc, char** argv)
{
	if (2 == argc)
	{
		if (0 == strcmp("-q", argv[1]) )
		{
			// :TODO: Shutdown and restart the InputServer.
			printf("InputServer::ArgvReceived - Restarting . . .\n");
			status_t   quit_status;
			//BMessenger msgr = BMessenger("application/x-vnd.OpenBeOS-input_server", -1, &quit_status);
			BMessenger msgr = BMessenger("application/x-vnd.OBOS-input_server", -1, &quit_status);
			if (B_OK == quit_status)
			{
				BMessage   msg  = BMessage(B_QUIT_REQUESTED);
				msgr.SendMessage(&msg);
			}
			else
			{
				printf("Unable to send Quit message to running InputServer.");
			}
		}
	}
}


/*
 *  Method: InputServer::InitKeyboardMouseStates()
 *   Descr: 
 */
void InputServer::InitKeyboardMouseStates(void)
{
	// This is where we read in the preferences data for the mouse and keyboard as well as
	// determine the screen resolution from the app_server and find the center of the screen
	// sMousePos is then set to the center of the screen.

	sMousePos.x = 200;
	sMousePos.y = 200;

}

void InputServer::InitTestDevice()
{
	printf("InputServer::InitTestDevice - Enter\n");
	const char* path = "/boot/home/Projects/InputServer/ISD/nervous/nervous";
	printf("InputServer::InitTestDevice - Loading add-on . . .\n");
	image_id addon_image = load_add_on(path);
	if (B_ERROR != addon_image)
	{
		status_t            isd_status = B_NO_INIT;
		BInputServerDevice* (*func)()  = NULL;
		printf("InputServer::InitTestDevice - Resolving symbol . . .\n");
		if (B_OK == get_image_symbol(addon_image, "instantiate_input_device", B_SYMBOL_TYPE_TEXT, (void**)&func) )
		{
			printf("Found instantiate_input_device.\n");
			if (NULL != func)
			{
				BInputServerDevice* isd = (*func)();
				if (NULL != isd)
				{
					printf("InputServer::InitTestDevice - Calling InitCheck . . .\n");
					isd_status = isd->InitCheck();
					mInputServerDeviceList.AddItem(
						new InputServerDeviceListEntry(path, isd_status, isd) );
					if (B_OK == isd_status)
					{
						//printf("Starting Nervous . . .\n");
						//isd->Start("Nervous Device", NULL);
					}
					else
					{
						printf("InitCheck failed.\n");
					}
				}
			}
		}
		if (B_OK != isd_status)
		{
			// Free resources associated with ISD's
			// that failed to initialize.
			//
			//unload_add_on(addon_image);
		}
	}
	printf("InputServer::InitTestDevice - Exit\n");
}

/*
 *  Method: InputServer::InitDevices()
 *   Descr: 
 */
void InputServer::InitDevices(void)
{
	BDirectory  dir;
	BPath       addon_dir;
	BPath       addon_path;
	BEntry      entry;
	directory_which addon_dirs[] =
	            {
	                B_BEOS_ADDONS_DIRECTORY,
	                B_COMMON_ADDONS_DIRECTORY,
	                B_USER_ADDONS_DIRECTORY
	            };
	const int   addon_dir_count = sizeof(addon_dirs) / sizeof(directory_which);

	printf("InputServer::InitDevices - Enter\n");
	
	// Find all Input Server Devices in each of the predefined
	// addon directories.
	//
	for (int i = 0; i < addon_dir_count; i++)
	{
		if (B_OK == find_directory(addon_dirs[i], &addon_dir) )
		{
			addon_dir.Append("input_server/devices");
			dir.SetTo(addon_dir.Path() );
			while (B_NO_ERROR == dir.GetNextEntry(&entry, false) )
			{
				entry.GetPath(&addon_path);
				printf("Adding %s . . .\n", addon_path.Path() );
				AddInputServerDevice(addon_path.Path() );
			}
		}
	}
	printf("InputServer::InitDevices - Exit\n");
}


/*
 *  Method: InputServer::AddInputServerDevice()
 *   Descr: 
 */
status_t InputServer::AddInputServerDevice(const char* path)
{
	image_id addon_image= load_add_on(path);
	if (B_ERROR != addon_image)
	{
		status_t            isd_status = B_NO_INIT;
		BInputServerDevice* (*func)()  = NULL;
		if (B_OK == get_image_symbol(addon_image, "instantiate_input_device", B_SYMBOL_TYPE_TEXT, (void**)&func) )
		{
			if (NULL != func)
			{
				/*
			    // :DANGER: Only reenable this section if this
			    //          InputServer can start and manage the
			    //          devices, otherwise the system will hang.
			    //			    
				BInputServerDevice* isd = (*func)();
				if (NULL != isd)
				{
					isd_status = isd->InitCheck();
					mInputServerDeviceList.AddItem(
						new InputServerDeviceListEntry(path, isd_status, isd) );
				}
				*/
				mInputServerDeviceList.AddItem(
					new InputServerDeviceListEntry(path, B_NO_INIT, NULL) );
			}
		}
		if (B_OK != isd_status)
		{
			// Free resources associated with ISD's
			// that failed to initialize.
			//
			unload_add_on(addon_image);
		}
	}
	return 0;
}


/*
 *  Method: InputServer::InitFilters()
 *   Descr: 
 */
void InputServer::InitFilters(void)
{
	BDirectory  dir;
	BPath       addon_dir;
	BPath       addon_path;
	BEntry      entry;
	directory_which addon_dirs[] =
	            {
	                B_BEOS_ADDONS_DIRECTORY,
	                B_COMMON_ADDONS_DIRECTORY,
	                B_USER_ADDONS_DIRECTORY
	            };
	const int   addon_dir_count = sizeof(addon_dirs) / sizeof(directory_which);

	printf("InputServer::InitFilters - Enter\n");
	
	// Find all Input Filters in each of the predefined
	// addon directories.
	//
	for (int i = 0; i < addon_dir_count; i++)
	{
		if (B_OK == find_directory(addon_dirs[i], &addon_dir) )
		{
			addon_dir.Append("input_server/filters");
			dir.SetTo(addon_dir.Path() );
			while (B_NO_ERROR == dir.GetNextEntry(&entry, false) )
			{
				entry.GetPath(&addon_path);
				printf("Adding %s . . .\n", addon_path.Path() );
				AddInputServerFilter(addon_path.Path() );
			}
		}
	}
	printf("InputServer::InitFilters - Exit\n");
}


/*
 *  Method: InputServer::AddInputServerFilter()
 *   Descr: 
 */
status_t InputServer::AddInputServerFilter(const char* path)
{
	image_id addon_image= load_add_on(path);
	if (B_ERROR != addon_image)
	{
		status_t            isf_status = B_NO_INIT;
		BInputServerFilter* (*func)()  = NULL;
		if (B_OK == get_image_symbol(addon_image, "instantiate_input_filter", B_SYMBOL_TYPE_TEXT, (void**)&func) )
		{
			if (NULL != func)
			{
				/*
			    // :DANGER: Only reenable this section if this
			    //          InputServer can start and manage the
			    //          filters, otherwise the system will hang.
			    //			    
				BInputFilter isf = (*func)();
				if (NULL != isf)
				{
					isf_status = isf->InitCheck();
					mInputServerFilterList.AddItem(
						new InputServerFilterListEntry(path, isf_status, isf );
				}
				*/
				mInputServerFilterList.AddItem(
					new InputServerFilterListEntry(path, B_NO_INIT, NULL) );
			}
		}
		if (B_OK != isf_status)
		{
			// Free resources associated with InputServerFilters
			// that failed to initialize.
			//
			unload_add_on(addon_image);
		}
	}
	return 0;
}


/*
 *  Method: InputServer::InitMethods()
 *   Descr: 
 */
void InputServer::InitMethods(void)
{
	BDirectory  dir;
	BPath       addon_dir;
	BPath       addon_path;
	BEntry      entry;
	directory_which addon_dirs[] =
	            {
	                B_BEOS_ADDONS_DIRECTORY,
	                B_COMMON_ADDONS_DIRECTORY,
	                B_USER_ADDONS_DIRECTORY
	            };
	const int   addon_dir_count = sizeof(addon_dirs) / sizeof(directory_which);

	printf("InputServer::InitMethods - Enter\n");
	
	// Find all Input Methods in each of the predefined
	// addon directories.
	//
	for (int i = 0; i < addon_dir_count; i++)
	{
		if (B_OK == find_directory(addon_dirs[i], &addon_dir) )
		{
			addon_dir.Append("input_server/methods");
			dir.SetTo(addon_dir.Path() );
			while (B_NO_ERROR == dir.GetNextEntry(&entry, false) )
			{
				entry.GetPath(&addon_path);
				printf("Adding %s . . .\n", addon_path.Path() );
				AddInputServerMethod(addon_path.Path() );
			}
		}
	}
	printf("InputServer::InitMethods - Exit\n");
}


/*
 *  Method: InputServer::AddInputServerMethod()
 *   Descr: 
 */
status_t InputServer::AddInputServerMethod(const char* path)
{
	image_id addon_image= load_add_on(path);
	if (B_ERROR != addon_image)
	{
		status_t            ism_status = B_NO_INIT;
		BInputServerMethod* (*func)()  = NULL;
		if (B_OK == get_image_symbol(addon_image, "instantiate_input_method", B_SYMBOL_TYPE_TEXT, (void**)&func) )
		{
			if (NULL != func)
			{
				/*
			    // :DANGER: Only reenable this section if this
			    //          InputServer can start and manage the
			    //          methods, otherwise the system will hang.
			    //			    
				BInputServerMethod ism = (*func)();
				if (NULL != ism)
				{
					ism_status = ism->InitCheck();
					mInputServerMethodList.AddItem(
						new InputServerMethodListEntry(path, ism_status, ism) );
				}
				*/
				mInputServerMethodList.AddItem(
					new InputServerMethodListEntry(path, B_NO_INIT, NULL) );
			}
		}
		if (B_OK != ism_status)
		{
			// Free resources associated with InputServerMethods
			// that failed to initialize.
			//
			unload_add_on(addon_image);
		}
	}
	return 0;
}


/*
 *  Method: InputServer::QuitRequested()
 *   Descr: 
 */
bool InputServer::QuitRequested(void)
{
	kill_thread(ISPortThread);
	delete_port(EventLooperPort);
	EventLooperPort = -1;
	return true;
}

// ---------------------------------------------------------------
// InputServer::ReadyToRun(void)
//
// Verifies to see if the input_server is able to start.
//
//
// Parameters:
//		None
//
// Returns:
//		B_OK if the
// ---------------------------------------------------------------
void InputServer::ReadyToRun(void)
{
}


/*
 *  Method: InputServer::MessageReceived()
 *   Descr: 
 */
void InputServer::MessageReceived(BMessage *message)
{
	BMessenger *app_server;
	switch(message->what)
	{
		case SET_METHOD:
		{
			//HandleSetMethod();
			break;
		}
		case GET_MOUSE_TYPE: 
		{
			//HandleGetSetMouseType();
			break;	
		}
		case SET_MOUSE_TYPE:
		{
			//HandleGetSetMouseType();
			break;
		}
		case GET_MOUSE_ACCELERATION:
		{
			//HandleGetSetMouseAcceleration();
			break;
		}
		case SET_MOUSE_ACCELERATION:
		{
			//HandleGetSetMouseAcceleration();
			break;
		} 
		case GET_KEY_REPEAT_DELAY:
		{
			//HandleGetSetKeyRepeatDelay();
			break;
		} 
		case SET_KEY_REPEAT_DELAY:
		{
			//HandleGetSetKeyRepeatDelay();
			break;
		} 
		case GET_KEY_INFO:
		{
			//HandleGetKeyInfo();
			break;
		} 
		case GET_MODIFIERS:
		{
			//HandleGetModifiers();
			break;
		} 
		case SET_MODIFIER_KEY:
		{
			//HandleSetModifierKey();
			break;
		} 
		case SET_KEYBOARD_LOCKS:
		{
			//HandleSetKeyboardLocks();
			break;
		} 
		case GET_MOUSE_SPEED:
		{
			//HandleGetSetMouseSpeed();
			break;
		} 
		case SET_MOUSE_SPEED:
		{
			//HandleGetSetMouseSpeed();
			break;
		} 
		case SET_MOUSE_POSITION:
		{
			//HandleSetMousePosition();
			break;
		} 
		case GET_MOUSE_MAP:
		{
			//HandleGetSetMouseMap();
			break;
		} 
		case SET_MOUSE_MAP:
		{
			//HandleGetSetMouseMap();
			break;
		} 
		case GET_KEYBOARD_ID:
		{
			//HandleGetKeyboardID();
			break;
		} 
		case GET_CLICK_SPEED:
		{
			//HandleGetSetClickSpeed();
			break;
		} 
		case SET_CLICK_SPEED:
		{
			//HandleGetSetClickSpeed();
			break;
		} 
		case GET_KEY_REPEAT_RATE:
		{
			//HandleGetSetKeyRepeatRate();
			break;
		} 
		case SET_KEY_REPEAT_RATE:
		{
			//HandleGetSetKeyRepeatRate();
			break;
		} 
		case GET_KEY_MAP:
		{
			//HandleGetSetKeyMap();
			break;
		} 
		case SET_KEY_MAP:
		{
			//HandleGetSetKeyMap();
			break;
		} 
		case FOCUS_IM_AWARE_VIEW:
		{
			//HandleFocusUnfocusIMAwareView();
			break;
		} 
		case UNFOCUS_IM_AWARE_VIEW:
		{
			//HandleFocusUnfocusIMAwareView();
			break;
		} 
		case B_QUIT_REQUESTED:
		{
			QuitRequested();
		}
		
		default:
		{
		printf("Default message . . .\n");
		app_server = new BMessenger("application/x-vnd.Be-APPS", -1, NULL);
		if (app_server->IsValid())
		{
			//app_server->SendMessage(message);
			
		}
		delete app_server;
		break;
		}
	}
}


/*
 *  Method: InputServer::HandleSetMethod()
 *   Descr: 
 */
void InputServer::HandleSetMethod(BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetMouseType()
 *   Descr: 
 */
void InputServer::HandleGetSetMouseType(BMessage *,
                                   BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetMouseAcceleration()
 *   Descr: 
 */
void InputServer::HandleGetSetMouseAcceleration(BMessage *,
                                           BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetKeyRepeatDelay()
 *   Descr: 
 */
void InputServer::HandleGetSetKeyRepeatDelay(BMessage *,
                                        BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetKeyInfo()
 *   Descr: 
 */
void InputServer::HandleGetKeyInfo(BMessage *,
                              BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetModifiers()
 *   Descr: 
 */
void InputServer::HandleGetModifiers(BMessage *,
                                BMessage *)
{
}


/*
 *  Method: InputServer::HandleSetModifierKey()
 *   Descr: 
 */
void InputServer::HandleSetModifierKey(BMessage *,
                                  BMessage *)
{
}


/*
 *  Method: InputServer::HandleSetKeyboardLocks()
 *   Descr: 
 */
void InputServer::HandleSetKeyboardLocks(BMessage *,
                                    BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetMouseSpeed()
 *   Descr: 
 */
void InputServer::HandleGetSetMouseSpeed(BMessage *,
                                    BMessage *)
{
}


/*
 *  Method: InputServer::HandleSetMousePosition()
 *   Descr: 
 */
void InputServer::HandleSetMousePosition(BMessage *message, BMessage *outbound)
{
	
	// this assumes that both supplied pointers are identical
	
	ASSERT(outbound == message);
		
	sMousePos.x = 200;
	sMousePos.y = 200;

	int32 xValue, 
		  yValue;
    
    message->FindInt32("x",xValue);
    printf("[HandleSetMousePosition] x = %lu:\n",xValue);
	
   	switch(message->what){
   		case B_MOUSE_MOVED:{
    		// get point and button from msg
    		if((outbound->FindInt32(X_VALUE,&xValue) == B_OK) && (outbound->FindInt32(Y_VALUE,&yValue) == B_OK)){
				sMousePos.x += xValue;
				sMousePos.y += yValue;
				outbound->ReplaceInt32(X_VALUE,sMousePos.x); 
				outbound->ReplaceInt32(Y_VALUE,sMousePos.y);
	   			}
    		break;
    		}
   				// Should be some Mouse Down and Up code here ..
   				// Along with some Key Down and up codes ..
   		default:
      		break;
   			
		}
}


/*
 *  Method: InputServer::HandleGetSetMouseMap()
 *   Descr: 
 */
void InputServer::HandleGetSetMouseMap(BMessage *,
                                  BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetKeyboardID()
 *   Descr: 
 */
void InputServer::HandleGetKeyboardID(BMessage *,
                                 BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetClickSpeed()
 *   Descr: 
 */
void InputServer::HandleGetSetClickSpeed(BMessage *,
                                    BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetKeyRepeatRate()
 *   Descr: 
 */
void InputServer::HandleGetSetKeyRepeatRate(BMessage *,
                                       BMessage *)
{
}


/*
 *  Method: InputServer::HandleGetSetKeyMap()
 *   Descr: 
 */
void InputServer::HandleGetSetKeyMap(BMessage *,
                                BMessage *)
{
}


/*
 *  Method: InputServer::HandleFocusUnfocusIMAwareView()
 *   Descr: 
 */
void InputServer::HandleFocusUnfocusIMAwareView(BMessage *,
                                           BMessage *)
{
}


/*
 *  Method: InputServer::EnqueueDeviceMessage()
 *   Descr: 
 */
status_t InputServer::EnqueueDeviceMessage(BMessage *message)
{
	//return (write_port(fEventPort, (int32)message, NULL, 0));
	return (write_port(EventLooperPort, (int32)message, NULL, 0));
}


/*
 *  Method: InputServer::EnqueueMethodMessage()
 *   Descr: 
 */
status_t InputServer::EnqueueMethodMessage(BMessage *)
{
	return 0;
}


/*
 *  Method: InputServer::UnlockMethodQueue()
 *   Descr: 
 */
status_t InputServer::UnlockMethodQueue(void)
{
	return 0;
}


/*
 *  Method: InputServer::LockMethodQueue()
 *   Descr: 
 */
status_t InputServer::LockMethodQueue(void)
{
	return 0;
}


/*
 *  Method: InputServer::SetNextMethod()
 *   Descr: 
 */
status_t InputServer::SetNextMethod(bool)
{
	return 0;
}


/*
 *  Method: InputServer::SetActiveMethod()
 *   Descr: 
 */
/* 
InputServer::SetActiveMethod(_BMethodAddOn_ *)
{
	return 0;
}
*/


/*
 *  Method: InputServer::MethodReplicant()
 *   Descr: 
 */
const BMessenger* InputServer::MethodReplicant(void)
{
	return NULL;
}


/*
 *  Method: InputServer::EventLoop()
 *   Descr: 
 */
status_t InputServer::EventLoop(void *)
{
	printf("Starting event loop . . .\n");
	EventLooperPort = create_port(100, "obos_is_event_port");
	if(EventLooperPort < 0) {
		_sPrintf("OBOS InputServer: create_port error: (0x%x) %s\n",EventLooperPort,strerror(EventLooperPort));
	} 
	ISPortThread = spawn_thread(ISPortWatcher, "_input_server_event_loop_", B_REAL_TIME_DISPLAY_PRIORITY+3, this);
	resume_thread(ISPortThread);

	return 0;
}


/*
 *  Method: InputServer::EventLoopRunning()
 *   Descr: 
 */
bool InputServer::EventLoopRunning(void)
{
	return true;
}


/*
 *  Method: InputServer::DispatchEvents()
 *   Descr: 
 */
bool InputServer::DispatchEvents(BList *eventList)
{
BMessage *event;

for ( int32 i = 0; NULL != (event = (BMessage *)eventList->ItemAt(i)); i++ )
	{
	// now we must send each event to the app_server
	DispatchEvent(event);
	}
	return true;
}// end DispatchEvents()

int InputServer::DispatchEvent(BMessage *message)
{
	// variables
	int32 xValue, 
		  yValue;
    uint32 buttons = 0;
    
    message->FindInt32("x",xValue);
    printf("[DispatchEvent] x = %lu:\n",xValue);
	
	port_id pid = find_port(SERVER_INPUT_PORT);
   	PortLink *appsvrlink = new PortLink(pid);
   	switch(message->what){
   		case B_MOUSE_MOVED:{
    		// get point and button from msg
    		if((message->FindInt32(X_VALUE,&xValue) == B_OK) && (message->FindInt32(Y_VALUE,&yValue) == B_OK)){
    			int64 time=(int64)real_time_clock();
    			appsvrlink->SetOpCode(B_MOUSE_MOVED);
    			appsvrlink->Attach(&time,sizeof(int64));
    			appsvrlink->Attach((float)xValue);
    			appsvrlink->Attach((float)yValue);
    			message->FindInt32("buttons",buttons);
    			appsvrlink->Attach(&buttons,sizeof(int32));
    			appsvrlink->Flush();
    			printf("B_MOUSE_MOVED: x = %lu: y = %lu: time = %llu: buttons = %lu\n",xValue,yValue,time,buttons);
    			}
    		break;
    		}
   				// Should be some Mouse Down and Up code here ..
   				// Along with some Key Down and up codes ..
   		default:
      		break;
   			
		}
	delete appsvrlink;
    return true;
}

/*
 *  Method: InputServer::CacheEvents()
 *   Descr: 
 */
bool InputServer::CacheEvents(BList *)
{
	return true;
}


/*
 *  Method: InputServer::GetNextEvents()
 *   Descr: 
 */
const BList* InputServer::GetNextEvents(BList *)
{
	return NULL;
}


/*
 *  Method: InputServer::FilterEvents()
 *  Descr:  This method applies all defined filters to each event in the
 *          supplied list.  The supplied list is modified to reflect the
 *          output of the filters.
 *          The method returns true if the filters were applied to all
 *          events without error and false otherwise.
 */
bool InputServer::FilterEvents(BList *eventsToFilter)
{
	if (NULL != eventsToFilter)
	{
		BInputServerFilter* current_filter;
		BMessage*           current_event;
		int32               filter_index  = 0;
		int32               event_index   = 0;

		while (NULL != (current_filter = (BInputServerFilter*)mInputServerFilterList.ItemAt(filter_index) ) )
		{
			// Apply the current filter to all available event messages.
			//		
			while (NULL != (current_event = (BMessage*)eventsToFilter->ItemAt(event_index) ) )
			{
				// Storage for new event messages generated by the filter.
				//
				BList out_list;
				
				// Apply the current filter to the current event message.
				//
				filter_result result = current_filter->Filter(current_event, &out_list);
				if (B_DISPATCH_MESSAGE == result)
				{
					// Use the result in current_message; ignore out_list.
					//
					event_index++;

					// Free resources associated with items in out_list.
					//
					void* out_item; 
					for (int32 i = 0; NULL != (out_item = out_list.ItemAt(i) ); i++)
					{
						delete out_item;
					}
				}
				else if (B_SKIP_MESSAGE == result)
				{
					// Use the result in out_list (if any); ignore current message.
					//
					eventsToFilter->RemoveItem(event_index);
					eventsToFilter->AddList(&out_list, event_index);
					event_index += out_list.CountItems();
					
					// NOTE: eventsToFilter now owns out_list's items.
				}
				else
				{
					// Error - Free resources associated with items in out_list and return.
					//
					void* out_item;
					for (int32 i = 0; NULL != (out_item = out_list.ItemAt(i) ); i++)
					{
						delete out_item;
					}
					return false;
				}
				
				// NOTE: The BList destructor frees out_lists's resources here.
				//       It does NOT free the resources associated with out_list's
				//       member items - those should either already be deleted or
				//       should be owned by eventsToFilter.
			}

		} // while()
		
		filter_index++;
		
	} // while()
	
	return true;	
}


/*
 *  Method: InputServer::SanitizeEvents()
 *   Descr: 
 */
bool InputServer::SanitizeEvents(BList *)
{
	return true;
}


/*
 *  Method: InputServer::MethodizeEvents()
 *   Descr: 
 */
bool InputServer::MethodizeEvents(BList *,
                             bool)
{
	return true;
}


/*
 *  Method: InputServer::StartStopDevices()
 *   Descr: 
 */
status_t InputServer::StartStopDevices(const char*       deviceName,
                                       input_device_type deviceType,
                                       bool              doStart)
{
	printf("StartStopDevice: Enter\n");
	for (int i = gInputDeviceList.CountItems() - 1; i >= 0; i--)
	{
		printf("Device #%d\n", i);
		InputDeviceListItem* item = (InputDeviceListItem*)gInputDeviceList.ItemAt(i);
		if (NULL != item)
		{
			BInputServerDevice* isd = item->mIsd;
			input_device_ref*   dev = item->mDev;
			printf("Hey\n");
			if ( (NULL != isd) && (NULL != dev) )
			{
				printf("  Starting/stopping: %s\n", dev->name);
				if (deviceType == dev->type)
				{
					if (doStart) isd->Start(dev->name, dev->cookie);
					else          isd->Stop(dev->name, dev->cookie);
				}
			}
		}
	}
	printf("StartStopDevice: Exit\n");
	
	return B_OK;
}


/*
 *  Method: InputServer::ControlDevices()
 *   Descr: 
 */
status_t InputServer::ControlDevices(const char *,
                            input_device_type,
                            unsigned long,
                            BMessage *)
{
	return 0;
}


/*
 *  Method: InputServer::DoMouseAcceleration()
 *   Descr: 
 */
bool InputServer::DoMouseAcceleration(long *,
                                 long *)
{
	return true;
}


/*
 *  Method: InputServer::SetMousePos()
 *   Descr: 
 */
bool InputServer::SetMousePos(long *,
                         long *,
                         long,
                         long)
{
	return true;
}


/*
 *  Method: InputServer::SetMousePos()
 *   Descr: 
 */
bool InputServer::SetMousePos(long *,
                         long *,
                         BPoint)
{
	return true;
}


/*
 *  Method: InputServer::SetMousePos()
 *   Descr: 
 */
bool InputServer::SetMousePos(long *,
                         long *,
                         float,
                         float)
{
	return true;
}


/*
 *  Method: InputServer::SafeMode()
 *   Descr: 
 */
bool InputServer::SafeMode(void)
{
	return true;
}

int32 InputServer::ISPortWatcher(void *arg)
{
	InputServer *self = (InputServer*)arg;
	self->WatchPort();
	return (B_NO_ERROR);
}

void InputServer::WatchPort()
{
	int32     	code;
	ssize_t    	length;
	char		*buffer;
	status_t  	err;

	while (true) { 
		// Block until we find the size of the next message
		length = port_buffer_size(EventLooperPort);
		buffer = (char*)malloc(length);
		printf("[Event Looper] BMessage Size = %lu\n", length);
		//event = NULL;
		BMessage *event = new BMessage();
		err = read_port(EventLooperPort, &code, buffer, length);
		if(err != length) {
			if(err >= 0) {
				printf("InputServer: failed to read full packet (read %lu of %lu)\n",err,length);
			} else {
				printf("InputServer: read_port error: (0x%lx) %s\n",err,strerror(err));
			}
		}else{
		
			if ((err = event->Unflatten(buffer)) < 0) {
				printf("[InputServer] Unflatten() error: (0x%lx) %s\n",err,strerror(err));
			} else {
			// This is where the message should be processed.	
			event->PrintToStream();

			HandleSetMousePosition(event, event);
						
			DispatchEvent(event);
			
			//printf("Event writen to port\n");
			delete(event);
			}
			
		}
		free(buffer);
		if(event!=NULL) {
			//delete(event);
			event = NULL;  
		} 
	}

}

