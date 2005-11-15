/*
 * Copyright 2001-2005, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INPUT_SERVER_APP_H
#define INPUT_SERVER_APP_H


#include "AddOnManager.h"
#include "BottomlineWindow.h"
#include "DeviceManager.h"
#include "KeyboardSettings.h"
#include "MouseSettings.h"

#include <Application.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <InputServerDevice.h>
#include <InputServerFilter.h>
#include <InputServerMethod.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Message.h>
#include <OS.h>
#include <Screen.h>
#include <SupportDefs.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define HAIKU_APPSERVER_COMM
#define R5_CURSOR_COMM
	// define this when R5 cursor communication should be used
//#define APPSERVER_R5_COMM
	// define this when R5 app_server communication should be used

#define INPUTSERVER_SIGNATURE "application/x-vnd.Be-input_server"
	// use this when target should replace R5 input_server

#ifdef APPSERVER_R5_COMM
	#define R5_CURSOR_COMM
	#undef HAIKU_APPSERVER_COMM
#endif

class InputDeviceListItem {
	public:
		InputDeviceListItem(BInputServerDevice& serverDevice, input_device_ref& device);

		void Start();
		void Stop();
		void Control(uint32 code, BMessage* message);

		const char* Name() const { return fDevice.name; }
		input_device_type Type() const { return fDevice.type; }
		bool Running() const { return fRunning; }

		bool HasName(const char* name) const;
		bool HasType(input_device_type type) const;
		bool Matches(const char* name, input_device_type type) const;

		BInputServerDevice* ServerDevice() { return fServerDevice; }

	private:
		BInputServerDevice* fServerDevice;
		input_device_ref   	fDevice;
		bool 				fRunning;
};

class _BDeviceAddOn_ {
	public:
		_BDeviceAddOn_(BInputServerDevice *device)
			: fDevice(device) {}

		BInputServerDevice* fDevice;
		BList fMonitoredRefs;
};

class _BMethodAddOn_ {
	public:
		_BMethodAddOn_(BInputServerMethod *method, const char* name, const uchar* icon);
		~_BMethodAddOn_();

		status_t SetName(const char* name);
		status_t SetIcon(const uchar* icon);
		status_t SetMenu(const BMenu* menu, const BMessenger& messenger);
		status_t MethodActivated(bool activate);
		status_t AddMethod();

	private:
		BInputServerMethod* fMethod;
		char* fName;
		uchar fIcon[16*16*1];
		const BMenu* fMenu;
		BMessenger fMessenger;
};


class KeymapMethod : public BInputServerMethod {
	public:
		KeymapMethod();
		~KeymapMethod();
};


class InputServer : public BApplication {
	public:
		InputServer();
		virtual ~InputServer();

		virtual void ArgvReceived(int32 argc, char** argv);

		virtual bool QuitRequested();
		virtual void ReadyToRun();
		virtual void MessageReceived(BMessage* message); 

		void HandleSetMethod(BMessage* message);
		status_t HandleGetSetMouseType(BMessage* message, BMessage* reply);
		status_t HandleGetSetMouseAcceleration(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyRepeatDelay(BMessage* message, BMessage* reply);
		status_t HandleGetKeyInfo(BMessage* message, BMessage* reply);
		status_t HandleGetModifiers(BMessage* message, BMessage* reply);
		status_t HandleSetModifierKey(BMessage* message, BMessage* reply);
		status_t HandleSetKeyboardLocks(BMessage* message, BMessage* reply);
		status_t HandleGetSetMouseSpeed(BMessage* message, BMessage* reply);
		status_t HandleSetMousePosition(BMessage* message, BMessage* reply);
		status_t HandleGetSetMouseMap(BMessage* message, BMessage* reply);
		status_t HandleGetKeyboardID(BMessage* message, BMessage* reply);
		status_t HandleGetSetClickSpeed(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyRepeatRate(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyMap(BMessage* message, BMessage* reply);
		status_t HandleFocusUnfocusIMAwareView(BMessage* message, BMessage* reply);

		status_t EnqueueDeviceMessage(BMessage* message);
		status_t EnqueueMethodMessage(BMessage* message);
		status_t UnlockMethodQueue();
		status_t LockMethodQueue();
		status_t SetNextMethod(bool direction);
		void SetActiveMethod(BInputServerMethod* method);
		const BMessenger* MethodReplicant();
		void SetMethodReplicant(const BMessenger *replicant);
		status_t EventLoop();
		bool EventLoopRunning();

		bool DispatchEvents(BList* eventList);
		status_t DispatchEvent(BMessage* event);
		bool CacheEvents(BList* eventList);
		const BList* GetNextEvents(BList*);
		bool FilterEvents(BList* eventList);
		bool SanitizeEvents(BList* eventList);
		bool MethodizeEvents(BList* eventList, bool);

		status_t GetDeviceInfo(const char* name, input_device_type *_type,
					bool *_isRunning = NULL);
		status_t UnregisterDevices(BInputServerDevice& serverDevice,
					input_device_ref** devices = NULL);
		status_t RegisterDevices(BInputServerDevice& serverDevice,
					input_device_ref** devices);
		status_t StartStopDevices(const char* name, input_device_type type,
					bool doStart);
		status_t StartStopDevices(BInputServerDevice& serverDevice, bool start);
		status_t ControlDevices(const char *name, input_device_type type,
					uint32 code, BMessage* message);

		bool DoMouseAcceleration(int32*, int32*);
		bool SetMousePos(long*, long*, long, long);
		bool SetMousePos(long*, long*, BPoint);
		bool SetMousePos(long*, long*, float, float);

		bool SafeMode();

		static BList gInputFilterList;
		static BLocker gInputFilterListLocker;

		static BList gInputMethodList;
		static BLocker gInputMethodListLocker;

		static DeviceManager gDeviceManager;

		static KeymapMethod gKeymapMethod;

		BRect& ScreenFrame() { return fFrame; }

	private:
		typedef BApplication _inherited;

		status_t LoadKeymap();
		status_t LoadSystemKeymap();
		void _InitKeyboardMouseStates();

		void WatchPort();

		static int32 ISPortWatcher(void *arg);
//		static bool doStartStopDevice(void*, void*);
		InputDeviceListItem* _FindInputDeviceListItem(BInputServerDevice& device);

	private:
		bool 			fEventLoopRunning;
		bool 			fSafeMode;
		port_id 		fEventPort;

		uint16			fKeyboardID;

		BList			fInputDeviceList;
		BLocker 		fInputDeviceListLocker;


		KeyboardSettings	fKeyboardSettings;
		MouseSettings		fMouseSettings;

		BPoint			fMousePos;		// current mouse position
		key_info		fKeyInfo;		// current key info
		key_map			fKeys;			// current key_map
		char*			fChars;			// current keymap chars
		uint32			fCharsSize;		// current keymap char count

		port_id      	fEventLooperPort;
		thread_id    	fISPortThread;

		AddOnManager*	fAddOnManager;

		BList			fEventsCache;

		BScreen			fScreen;
		BRect			fFrame;

		BInputServerMethod*	fActiveMethod;
		BList				fMethodQueue;
		const BMessenger*	fReplicantMessenger;
		BottomlineWindow*	fBLWindow;
		bool				fIMAware;
	
#ifdef R5_CURSOR_COMM
		sem_id 			fCursorSem;
		port_id			fAsPort;
		area_id			fCloneArea;
		uint32*			fAppBuffer;
#endif

#if DEBUG == 2
	public:
		static FILE *sLogFile;
#endif
};

extern InputServer* gInputServer;

#if DEBUG >= 1
#	if DEBUG == 2
#		undef PRINT
        inline void _iprint(const char *fmt, ...) { char buf[1024]; va_list ap; va_start(ap, fmt); vsprintf(buf, fmt, ap); va_end(ap); \
                fputs(buf, InputServer::sLogFile); fflush(InputServer::sLogFile); }
#		define PRINT(x)	_iprint x
#	else
#		undef PRINT
#		define PRINT(x)	SERIAL_PRINT(x)	
#	endif
#	define PRINTERR(x)		PRINT(x)
#	define EXIT()          PRINT(("EXIT %s\n", __PRETTY_FUNCTION__))
#	define CALLED()        PRINT(("CALLED %s\n", __PRETTY_FUNCTION__))
#else
#	define EXIT()          ((void)0)
#	define CALLED()        ((void)0)
#	define PRINTERR(x)		SERIAL_PRINT(x)
#endif

#endif	/* INPUT_SERVER_APP_H */
