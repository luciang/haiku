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
//	File Name:		DecView.cpp
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	decorator handler for the app
//  
//	Utilizes app_server graphics module emulation to allows decorators to display
//	a preview. Saves the chosen decorator's path in a settings file in the app_server
//	settings directory.
//------------------------------------------------------------------------------
#include <OS.h>
#include <String.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include "DecView.h"
#include <PortLink.h>
#include <Directory.h>
#include <ServerProtocol.h>
#include <File.h>
#include <stdio.h>
#include "RGBColor.h"
#include "defs.h"
#include "PreviewDriver.h"
#include "Decorator.h"

//#define DEBUG_DECORATOR

DecView::DecView(BRect frame, const char *name, int32 resize, int32 flags)
	:BView(frame,name,resize,flags)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	// Set up list of color attributes
	declist=new BListView(BRect(10,10,110,110),"DecoratorList");
	
	scrollview=new BScrollView("ScrollView",declist, B_FOLLOW_LEFT |
		B_FOLLOW_TOP, 0, false, true);
	AddChild(scrollview);
	scrollview->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	declist->SetSelectionMessage(new BMessage(DECORATOR_CHOSEN));

	BRect cvrect(0,0,50,25);

	cvrect.OffsetTo(Bounds().right-60,
		scrollview->Frame().top);

	apply=new BButton(cvrect,"ApplyButton","Apply",
		new BMessage(APPLY_SETTINGS),B_FOLLOW_LEFT |B_FOLLOW_TOP,
		B_WILL_DRAW | B_NAVIGABLE);
	AddChild(apply);

	// set up app_server emulation
	
	driver=new PreviewDriver();
	if(!driver->Initialize())
		printf("Uh-oh... Couldn't initialize graphics module for server emu!\n");
	else
	{
		preview=driver->View();
		AddChild(preview);
		preview->MoveTo(scrollview->Frame().right+20,scrollview->Frame().top);
	}

	ldata.highcolor.SetColor(51,102,160);
	pat_solid_high=0xFFFFFFFFFFFFFFFFLL;
	driver->FillRect(preview_bounds,&ldata,(int8*)&pat_solid_high);

	decorator=NULL;
	decorator_id=-1;
	GetDecorators();	
	LoadSettings();

	BString path(ConvertIndexToPath(0L));
	if(LoadDecorator(path.String()))
	{
		driver->FillRect(preview_bounds,&ldata,(int8*)&pat_solid_high);
		if(decorator)
		{
			BStringItem *item=(BStringItem*)declist->ItemAt(declist->CurrentSelection());
			path=(item)?item->Text():path="Title";
			decorator->SetDriver(driver);
			decorator->SetTitle(path.String());
			decorator->SetColors(colorset);
			decorator->Draw();
		}
	}
}

DecView::~DecView(void)
{
	driver->Shutdown();
	delete driver;
	if(decorator!=NULL)
		delete decorator;
}

void DecView::AllAttached(void)
{
	declist->SetTarget(this);
	apply->SetTarget(this);
	LoadSettings();
}

void DecView::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case APPLY_SETTINGS:
		{
			SaveSettings();
			NotifyServer();
			break;
		}
		case DECORATOR_CHOSEN:
		{
#ifdef DEBUG_DECORATOR
printf("MSG: Decorator Chosen - #%ld\n",declist->CurrentSelection());
#endif
			bool success=false;

			BString path( ConvertIndexToPath(declist->CurrentSelection()) );
#ifdef DEBUG_DECORATOR
printf("MSG: Decorator path: %s\n",path.String());
#endif

			success=LoadDecorator(path.String());

			if(!success)
			{
#ifdef DEBUG_DECORATOR
printf("MSG: Decorator NOT Chosen - couldn't load decorator\n");
#endif
				break;
			}
			
			driver->FillRect(preview_bounds,&ldata,(int8*)&pat_solid_high);
			if(decorator)
			{
				BStringItem *item=(BStringItem*)declist->ItemAt(declist->CurrentSelection());
				path=(item)?item->Text():path="Title";
				decorator->SetDriver(driver);
				decorator->SetTitle(path.String());
				decorator->SetColors(colorset);
				decorator->Draw();
			}
			break;
		}
		default:
			BView::MessageReceived(msg);
			break;
	}
}

void DecView::SaveSettings(void)
{
	BStringItem *item=(BStringItem*)declist->ItemAt(declist->CurrentSelection());
	if(!item)
		return;
	
	BString path(SETTINGS_DIR);
	path+="DecoratorSettings";
	printf("%s\n",path.String());
	BFile file(path.String(),B_READ_WRITE|B_CREATE_FILE|B_ERASE_FILE);
	
	settings.MakeEmpty();
	settings.AddString("decorator",item->Text());
	settings.Flatten(&file);
}

void DecView::LoadSettings(void)
{
	settings.MakeEmpty();

	BDirectory dir,newdir;
	if(dir.SetTo(SETTINGS_DIR)==B_ENTRY_NOT_FOUND)
		create_directory(SETTINGS_DIR,0777);

	BString path(SETTINGS_DIR);
	path+="DecoratorSettings";
	BFile file(path.String(),B_READ_ONLY);

	if(file.InitCheck()==B_OK)
	{
		if(settings.Unflatten(&file)==B_OK)
		{
			BString itemtext;
			if(settings.FindString("decorator",&itemtext)==B_OK)
			{
				for(int32 i=0;i<declist->CountItems();i++)
				{
					BStringItem *item=(BStringItem*)declist->ItemAt(i);
					if(item && itemtext.Compare(item->Text())==0)
					{
						declist->Select(i);
						return;
					}
				}
			}
			
			// We got this far, so something must have gone wrong. Select the first item
			declist->Select(0L);
			
			return;
		}
		printf("Error unflattening settings file %s\n",path.String());
	}
}

void DecView::NotifyServer(void)
{
	// Send a message to the app_server to tell it which decorator we have selected.
	
	port_id serverport=find_port(SERVER_PORT_NAME);

	if(serverport==B_NAME_NOT_FOUND)
		return;
	
	BStringItem *item=(BStringItem*)declist->ItemAt(declist->CurrentSelection());
	if(!item)
		return;
	
	PortLink *pl=new PortLink(serverport);
	pl->SetOpCode(SET_DECORATOR);
	pl->Attach(item->Text(),strlen(item->Text())+1);
	pl->Flush();
	delete pl;
}

void DecView::GetDecorators(void)
{
#ifdef DEBUG_DECORATOR
printf("DecView::GetDecorators()\n");
#endif
	BDirectory dir;
	BEntry entry;
	BPath path;
	BString name;
	BStringItem *item;

	status_t dirstat=dir.SetTo(DECORATORS_DIR);
	if(dirstat!=B_OK)
		if(dirstat!=B_OK)
	{
		// We couldn't set the directory, so more than likely it just
		// doesn't exist. Create it and return an empty menu.
		switch(dirstat)
		{
			case B_NAME_TOO_LONG:
			{
				printf("DecView::GetDecorators: Couldn't open the folder for decorators - path string too long.\n");
				break;
			}
			case B_ENTRY_NOT_FOUND:
			{
				printf("Couldn't open the folder for decorators - entry not found\n");
				break;
			}
			case B_BAD_VALUE:
			{
				printf("DecView::GetDecorators: Couldn't open the folder for decorators - bad path\n");
				break;
			}
			case B_NO_MEMORY:
			{
				printf("DecView::GetDecorators: No memory left. We're probably going to crash now\n");
				break;
			}
			case B_BUSY:
			{
				printf("DecView::GetDecorators: Couldn't open the folder for decorators - node busy\n");
				break;
			}
			case B_FILE_ERROR:
			{
				printf("DecView::GetDecorators: Couldn't open the folder for decorators - general file error\n");
				break;
			}
			case B_NO_MORE_FDS:
			{
				printf("DecView::GetDecorators: Couldn't open the folder for decorators - no more file descriptors\n");
				break;
			}
		}
		return;
	}

	int32 count=dir.CountEntries();

	for(int32 i=0;i<count;i++)
	{
		dir.GetNextEntry(&entry);
		entry.GetPath(&path);
		name=path.Path();
		name.Remove(0,name.FindLast('/')+1);

		item=new BStringItem(name.String());
		declist->AddItem(item);
	}
	
}

bool DecView::LoadDecorator(const char *path)
{
	if(!path)
		return false;
	create_decorator *pcreatefunc=NULL;
//	get_decorator_version *pversionfunc=NULL;
	status_t stat;
	image_id addon;
	
	addon=load_add_on(path);

/*	stat=get_image_symbol(addon, "get_decorator_version", B_SYMBOL_TYPE_TEXT, (void**)&pversionfunc);
	if(stat!=B_OK)
	{
		unload_add_on(addon);
#ifdef DEBUG_DECORATOR
printf("LoadDecorator(%s): Couldn't get version symbol\n",path);
#endif
		return false;
	}
*/
	// As of now, we do nothing with decorator versions, but the possibility exists
	// that the API will change even though I cannot forsee any reason to do so. If
	// we *did* do anything with decorator versions, the assignment to a global would
	// go here.
		
	stat=get_image_symbol(addon, "instantiate_decorator", B_SYMBOL_TYPE_TEXT, (void**)&pcreatefunc);
	if(stat!=B_OK)
	{
		unload_add_on(addon);
#ifdef DEBUG_DECORATOR
printf("LoadDecorator(%s): Couldn't get allocation symbol\n",path);
#endif
		return false;
	}
	if(decorator!=NULL)
	{
#ifdef DEBUG_DECORATOR
printf("LoadDecorator(): Deleting old decorator\n");
#endif
		delete decorator;
		decorator=NULL;

		// NEVER, *EVER*, call unload_add_on() if there is _any_ possibility,
		// no matter remote, that memory might still be in use when it is called.
		// It wll lead to unpredictable crashes which have almost nothing to
		// do with the *real* cause.
//		unload_add_on(decorator_id);
	}
	decorator_id=addon;
	decorator=pcreatefunc(BRect(50,50,150,150),B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL,0);
	decorator->SetDriver(driver);
	decorator->SetFocus(true);
//	decorator->Draw();
	return true;
}

BString DecView::ConvertIndexToPath(int32 index)
{
	BStringItem *item=(BStringItem*)declist->ItemAt(index);
	if(!item)
	{
#ifdef DEBUG_DECORATOR
printf("ConvertIndexToPath(): Couldn't get item for index %ld\n",index);
#endif
		return NULL;
	}
	BString path(DECORATORS_DIR);
	path+=item->Text();
#ifdef DEBUG_DECORATOR
printf("ConvertIndexToPath(): returned %s\n",path.String());
#endif
	return BString(path.String());
}

void DecView::SetColors(const BMessage &message)
{
#ifdef DEBUG_DECORATOR
printf("DecView::SetColors\n");
#endif
	if(UnpackSettings(&colorset,message))
	{
		if(decorator)
		{
//			ldata.highcolor.SetColor(colorset.desktop);
			driver->FillRect(preview_bounds,&ldata,(int8*)&pat_solid_high);
			decorator->SetColors(colorset);
			decorator->Draw();
		}
		else
		{
#ifdef DEBUG_DECORATOR
printf("DecView::SetColors: NULL decorator\n");
#endif
		}
	}
	else
	{
#ifdef DEBUG_DECORATOR
printf("DecView::SetColors: UnpackSetting returned false\n");
#endif
	}
}

bool DecView::UnpackSettings(ColorSet *set, const BMessage &msg)
{
	if(!set)
	{
#ifdef DEBUG_DECORATOR
printf("UnpackSettings(): NULL parameter\n");
#endif
		return false;
	}
	rgb_color *col;
	ssize_t size;

	if(msg.FindData("Background",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->panel_background=*col;
	if(msg.FindData("Panel Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->panel_text=*col;
	if(msg.FindData("Document Background",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->document_background=*col;
	if(msg.FindData("Document Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->document_text=*col;
	if(msg.FindData("Control Background",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->control_background=*col;
	if(msg.FindData("Control Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->control_text=*col;
	if(msg.FindData("Control Highlight",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->control_highlight=*col;
	if(msg.FindData("Control Border",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->control_border=*col;
	if(msg.FindData("Tooltip Background",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->tooltip_background=*col;
	if(msg.FindData("Tooltip Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->tooltip_text=*col;
	if(msg.FindData("Menu Background",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->menu_background=*col;
	if(msg.FindData("Selected Menu Item Background",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->menu_selected_background=*col;
	if(msg.FindData("Navigation Base",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->keyboard_navigation_base=*col;
	if(msg.FindData("Navigation Pulse",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->keyboard_navigation_pulse=*col;
	if(msg.FindData("Menu Item Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->menu_text=*col;
	if(msg.FindData("Selected Menu Item Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->menu_selected_text=*col;
	if(msg.FindData("Selected Menu Item Border",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->menu_selected_border=*col;
	if(msg.FindData("Success",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->success=*col;
	if(msg.FindData("Failure",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->failure=*col;
	if(msg.FindData("Shine",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->shine=*col;
	if(msg.FindData("Shadow",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->shadow=*col;
	if(msg.FindData("Window Tab",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->window_tab=*col;
	if(msg.FindData("Window Tab Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->window_tab_text=*col;
	if(msg.FindData("Inactive Window Tab",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->inactive_window_tab=*col;
	if(msg.FindData("Inactive Window Tab Text",(type_code)'RGBC',(const void**)&col,&size)==B_OK)
		set->inactive_window_tab_text=*col;

	return true;
}
