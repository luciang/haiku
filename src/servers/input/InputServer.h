/*****************************************************************************/
// Haiku input_server
//
// This is the primary application class for the Haiku input_server.
//
//
// Copyright (c) 2001-2004 Haiku Project
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


#ifndef INPUTSERVERAPP_H
#define INPUTSERVERAPP_H

// BeAPI Headers
#include <Application.h>
#include <Debug.h>
#include <InputServerDevice.h>
#include <InputServerFilter.h>
#include <InputServerMethod.h>
#include "AddOnManager.h"
#include "BottomlineWindow.h"
#include "DeviceManager.h"
#include "MouseSettings.h"
#include "KeyboardSettings.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <Locker.h>

#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <OS.h>
#include <Screen.h>
#include <SupportDefs.h>

#define INPUTSERVER_SIGNATURE "application/x-vnd.Be-input_server"

class BPortLink;

class InputDeviceListItem
{
	public:
	BInputServerDevice* mIsd;    
	input_device_ref   	mDev;
	bool 				mStarted;

	InputDeviceListItem(BInputServerDevice* isd, input_device_ref dev):
		mIsd(isd), mDev(dev), mStarted(false) {};	
};

class _BDeviceAddOn_
{
	public:
		_BDeviceAddOn_(BInputServerDevice *device)
			: fDevice(device) {};
	
		BInputServerDevice *fDevice;
		BList fMonitoredRefs;
};

class _BMethodAddOn_
{
	public:
		_BMethodAddOn_(BInputServerMethod *method, const char* name, const uchar *icon);
		~_BMethodAddOn_();

		status_t SetName(const char *name);
		status_t SetIcon(const uchar *icon);
		status_t SetMenu(const BMenu *menu, const BMessenger &messenger);
		status_t MethodActivated(bool activate);
		status_t AddMethod();
	private:	
		BInputServerMethod *fMethod;
		char *fName;
		uchar fIcon[16*16*1];
		const BMenu *fMenu;
		BMessenger fMessenger;
};


class KeymapMethod : public BInputServerMethod
{
public:
        KeymapMethod();
        ~KeymapMethod();
};


/*****************************************************************************/
// InputServer
//
// Application class for input_server.
/*****************************************************************************/

class InputServer : public BApplication
{
	typedef BApplication Inherited;
public:
	InputServer(void);
	virtual ~InputServer(void);

	virtual void ArgvReceived(long, char**);

	void InitKeyboardMouseStates(void);

	virtual bool QuitRequested(void);
	virtual void ReadyToRun(void);
	virtual void MessageReceived(BMessage*); 

	void HandleSetMethod(BMessage*);
	status_t HandleGetSetMouseType(BMessage*, BMessage*);
	status_t HandleGetSetMouseAcceleration(BMessage*, BMessage*);
	status_t HandleGetSetKeyRepeatDelay(BMessage*, BMessage*);
	status_t HandleGetKeyInfo(BMessage*, BMessage*);
	status_t HandleGetModifiers(BMessage*, BMessage*);
	status_t HandleSetModifierKey(BMessage*, BMessage*);
	status_t HandleSetKeyboardLocks(BMessage*, BMessage*);
	status_t HandleGetSetMouseSpeed(BMessage*, BMessage*);
	status_t HandleSetMousePosition(BMessage*, BMessage*);
	status_t HandleGetSetMouseMap(BMessage*, BMessage*);
	status_t HandleGetKeyboardID(BMessage*, BMessage*);
	status_t HandleGetSetClickSpeed(BMessage*, BMessage*);
	status_t HandleGetSetKeyRepeatRate(BMessage*, BMessage*);
	status_t HandleGetSetKeyMap(BMessage*, BMessage*);
	status_t HandleFocusUnfocusIMAwareView(BMessage*, BMessage*);

	status_t EnqueueDeviceMessage(BMessage*);
	status_t EnqueueMethodMessage(BMessage*);
	status_t UnlockMethodQueue(void);
	status_t LockMethodQueue(void);
	status_t SetNextMethod(bool);
	void SetActiveMethod(BInputServerMethod*);
	const BMessenger* MethodReplicant(void);
	void SetMethodReplicant(const BMessenger *);
	status_t EventLoop();
	bool     EventLoopRunning(void);

	bool DispatchEvents(BList*);
	int DispatchEvent(BMessage*);
	bool CacheEvents(BList*);
	const BList* GetNextEvents(BList*);
	bool FilterEvents(BList*);
	bool SanitizeEvents(BList*);
	bool MethodizeEvents(BList*, bool);

	static status_t StartStopDevices(const char *name, input_device_type type, bool doStart);
	static status_t StartStopDevices(BInputServerDevice *isd, bool);
	static status_t ControlDevices(const char *, input_device_type, unsigned long, BMessage*);

	bool DoMouseAcceleration(int32*, int32*);
	bool SetMousePos(long*, long*, long, long);
	bool SetMousePos(long*, long*, BPoint);
	bool SetMousePos(long*, long*, float, float);

	bool SafeMode(void);

	static BList   gInputDeviceList;
	static BLocker gInputDeviceListLocker;
	
	static BList   gInputFilterList;
	static BLocker gInputFilterListLocker;
	
	static BList   gInputMethodList;
	static BLocker gInputMethodListLocker;
	
	static DeviceManager	gDeviceManager;

	static KeymapMethod gKeymapMethod;

	BRect& ScreenFrame() { return fFrame;};
private:
	status_t 	LoadKeymap();
	status_t 	LoadSystemKeymap();

	bool 			sEventLoopRunning;
	bool 			sSafeMode;
	port_id 		sEventPort;
	
	uint16			sKeyboardID;
	
	KeyboardSettings	fKeyboardSettings;
	MouseSettings		fMouseSettings;

	BPoint			fMousePos;		// current mouse position
	key_info		fKey_info;		// current key info
	key_map			fKeys;			// current key_map
	char			*fChars;		// current keymap chars
	uint32			fCharsSize;		// current keymap char count
	
	port_id      	fEventLooperPort;
	thread_id    	fISPortThread;
	
	static int32 	ISPortWatcher(void *arg);
	void WatchPort();
	
	static bool doStartStopDevice(void*, void*);

	AddOnManager 	*fAddOnManager;
	
	BList			fEventsCache;
	
	BScreen			fScreen;
	BRect			fFrame;

	BInputServerMethod	*fActiveMethod;
	BList			fMethodQueue;
	const BMessenger	*fReplicantMessenger;
	BottomlineWindow 	*fBLWindow;
	bool			fIMAware;

#ifndef COMPILE_FOR_R5	
	// added this to communicate via portlink
	
	BPortLink 		*serverlink;
	
#else

	sem_id 		fCursorSem;
	port_id		fAsPort;
	area_id		fCloneArea;
	uint32		*fAppBuffer;
#endif 
	
#if DEBUG == 2
public:
	static FILE *sLogFile;
#endif
};

#if DEBUG>=1
#if DEBUG == 2
	#undef PRINT
        inline void _iprint(const char *fmt, ...) { char buf[1024]; va_list ap; va_start(ap, fmt); vsprintf(buf, fmt, ap); va_end(ap); \
                fputs(buf, InputServer::sLogFile); fflush(InputServer::sLogFile); }
	#define PRINT(x)	_iprint x
	
#endif
		#define PRINTERR(x)		PRINT(x)
        #define EXIT()          PRINT(("EXIT %s\n", __PRETTY_FUNCTION__))
        #define CALLED()        PRINT(("CALLED %s\n", __PRETTY_FUNCTION__))
#else
        #define EXIT()          ((void)0)
        #define CALLED()        ((void)0)
		#define PRINTERR(x)		SERIAL_PRINT(x)
#endif

#endif
