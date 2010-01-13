#include "AutoRaiseApp.h"
#include "AutoRaiseIcon.h"

AutoRaiseApp::AutoRaiseApp()
	: BApplication( APP_SIG )
{
	removeFromDeskbar(NULL);
	_directToDeskbar = false;
	
	//since the tray item shows an icon, and the class TrayView needs to be able to know the location
	//of the executing binary, we write into the settings file this critical information when the app is fired up
	app_info info;
	be_app->GetAppInfo(&info);
	
	//now, put the path into the settings file
	AutoRaiseSettings settings;
	settings.SetAppPath(info.ref);	
}

AutoRaiseApp::~AutoRaiseApp()
{
	return;
}

void AutoRaiseApp::ArgvReceived(int32 argc, char ** argv)
{
	BString option;
	bool inDeskbar = false, persist = false;

	for (int32 i = 1; i < argc; i++)
	{
		option = argv[i];
		if (option.IFindFirst("deskbar") != B_ERROR)
			inDeskbar = true;
			
		if (option.IFindFirst("persist") != B_ERROR)
			persist = true;
	}
	
	if (inDeskbar && !persist)
	{
		printf(APP_NAME" being put into Tray (one shot)...\n");
		
		PutInTray(false);
		
		_directToDeskbar = true;

	}
	else if (inDeskbar && persist)
	{
		printf(APP_NAME" being put into Tray (persistant)...\n");

		PutInTray(true);
		
		_directToDeskbar = true;
	}
	else
	{
		printf("\nUsage: "APP_NAME" [options]\n\t--deskbar\twill not open window, will just put "APP_NAME" into tray\n\t--persist will put "APP_NAME" into tray such that it remains between bootings\n");
	}
	
	be_app_messenger.SendMessage(B_QUIT_REQUESTED);
}

void AutoRaiseApp::ReadyToRun()
{
	if (!_directToDeskbar)
	{
		printf("\nUsage: " APP_NAME " [options]\n\t--deskbar\twill not open window, will just put " APP_NAME " into tray\n\t--persist will put " APP_NAME " into tray such that it remains between bootings\n");
		BAlert *alert = new BAlert("usage box", APP_NAME ", (c) 2002, mmu_man\nUsage: " APP_NAME " [options]\n\t--deskbar\twill not open window, will just put " APP_NAME " into tray\n\t--persist will put "APP_NAME" into tray such that it remains between bootings\n", "OK", NULL, NULL,
            B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_INFO_ALERT);
        alert->SetShortcut(0, B_ENTER);
   	    alert->Go();
		be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	}
}

void AutoRaiseApp::PutInTray(bool persist)
{
	BDeskbar db;
	
	if (!persist)
		db.AddItem(new TrayView);
	else {
		BRoster roster;
		entry_ref ref;
		roster.FindApp(APP_SIG, &ref);
		int32 id;
		db.AddItem(&ref, &id);
	}
}

int main()
{
	AutoRaiseApp *app = new AutoRaiseApp();
	app->Run();
	
}
