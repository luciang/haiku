#ifndef	_OPENBEOS_APP_SERVER_H_
#define	_OPENBEOS_APP_SERVER_H_

#include <OS.h>
#include <Locker.h>
#include <List.h>
#include <Application.h>
#include <Window.h>
#include <String.h>
#include "Decorator.h"
#include "ServerConfig.h"

class Layer;
class BMessage;
class ServerApp;
class DisplayDriver;
class BPortLink;
class CursorManager;
class BitmapManager;

/*!
	\class AppServer AppServer.h
	\brief main manager object for the app_server
	
	File for the main app_server thread. This particular thread monitors for
	application start and quit messages. It also starts the housekeeping threads
	and initializes most of the server's globals.
*/
#if DISPLAYDRIVER == HWDRIVER
class AppServer
#else
class AppServer : public BApplication
#endif
{
public:
	AppServer(void);
	~AppServer(void);

	static	int32 PollerThread(void *data);
	static	int32 PicassoThread(void *data);
	thread_id Run(void);
	void MainLoop(void);
	
	bool LoadDecorator(const char *path);
	void InitDecorators(void);
	
	void DispatchMessage(int32 code, BPortLink &link);
	void Broadcast(int32 code);

	ServerApp* FindApp(const char *sig);

private:
	friend	Decorator*	new_decorator(BRect rect, const char *title,
				int32 wlook, int32 wfeel, int32 wflags, DisplayDriver *ddriver);

	// global function pointer
	create_decorator	*make_decorator;
	
	port_id	fMessagePort;
	
	image_id fDecoratorID;
	
	BString fDecoratorName;
	
	bool fQuittingServer;
	
	BList *fAppList;
	thread_id fPicassoThreadID;
	
	sem_id 	fActiveAppLock,
			fAppListLock,
			fDecoratorLock;
	
	DisplayDriver *fDriver;
};

Decorator *new_decorator(BRect rect, const char *title, int32 wlook, int32 wfeel,
	int32 wflags, DisplayDriver *ddriver);

extern CursorManager *cursormanager;
extern BitmapManager *bitmapmanager;
extern ColorSet gui_colorset;
extern AppServer *app_server;

#endif
