#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <app/Application.h>
#include <app/Roster.h>
#include <InterfaceKit.h>
#include <StorageKit.h>
#include <SupportKit.h>

#include "BoneyardAddOn.h"
#include "NetworkSetupAddOn.h"

#include "NetworkSetupWindow.h"

// --------------------------------------------------------------
NetworkSetupWindow::NetworkSetupWindow(const char *title)
	: BWindow(BRect(100, 100, 600, 600), title, B_TITLED_WINDOW,
				B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE)
{
	BMenu 		*show_menu;
	BMenu		*profiles_menu;
	BMenuField 	*menu_field;
	BBox 		*top_box, *bottom_box, *line;	// *group
	BButton		*button;
	BCheckBox 	*check;
	BRect		r;
	float		x, w, h;

	// TODO: cleanup this mess!

	show_menu = new BPopUpMenu("<please select me!>");
	BuildShowMenu(show_menu, SHOW_MSG);
	
#define H_MARGIN	10
#define V_MARGIN	10
#define SMALL_MARGIN	3

	// Resize the window to minimal width
	ResizeTo(fMinAddonViewRect.Width() + 2 * H_MARGIN, Bounds().Height());

	top_box = new BBox(Bounds(), NULL, B_FOLLOW_NONE,
						B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
						B_PLAIN_BORDER);
	AddChild(top_box); 

	r = top_box->Bounds();
	r.InsetBy(H_MARGIN, V_MARGIN);

	// ---- Profiles section

	profiles_menu = new BPopUpMenu("<none>");
	menu_field = new BMenuField(r, "profiles_menu", PROFILE_LABEL, profiles_menu); 

	menu_field->SetFont(be_bold_font);
	menu_field->SetDivider(be_bold_font->StringWidth(PROFILE_LABEL "#"));
	top_box->AddChild(menu_field);
	// menu_field->SetViewColor((rand() % 256), (rand() % 256), (rand() % 256));
	menu_field->ResizeToPreferred();
	menu_field->GetPreferredSize(&w, &h);

	button = new BButton(r, "manage_profiles", MANAGE_PROFILES_LABEL,
					new BMessage(MANAGE_PROFILES_MSG),
					B_FOLLOW_TOP | B_FOLLOW_RIGHT);
	button->GetPreferredSize(&w, &h);
	button->ResizeToPreferred();
	button->MoveTo(r.right - w, r.top);
	top_box->AddChild(button);
	
	r.top += h + V_MARGIN;

	// ---- Separator line between Profiles section and Settings section
	line = new BBox(BRect(r.left, r.top, r.right, r.top + 1), NULL,
						 B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP );
	top_box->AddChild(line);

	BuildProfilesMenu(profiles_menu, SELECT_PROFILE_MSG);

	r.top += 2 + V_MARGIN;

	// ---- Settings section

	// Make the show popup field half the whole width and centered
	menu_field = new BMenuField(r, "show_menu", SHOW_LABEL, show_menu);
	menu_field->SetFont(be_plain_font);
	menu_field->SetDivider(be_plain_font->StringWidth(SHOW_LABEL "#"));
	top_box->AddChild(menu_field);
	// menu_field->SetViewColor((rand() % 256), (rand() % 256), (rand() % 256));

	menu_field->ResizeToPreferred();
	menu_field->GetPreferredSize(&w, &h);
	r.top += h+1 + V_MARGIN;

	r = fMinAddonViewRect.OffsetByCopy(H_MARGIN, r.top);
	fPanel = new BBox(r, "showview_box", B_FOLLOW_NONE,
						B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
						B_PLAIN_BORDER);
	top_box->AddChild(fPanel);
	top_box->ResizeTo(Bounds().Width(), r.bottom + 1 + V_MARGIN);

	// ---- Bottom globals buttons section

	r = Bounds();
	r.top = top_box->Frame().bottom + 1;
	bottom_box = new BBox(r, NULL, B_FOLLOW_NONE,
						B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP,
						B_PLAIN_BORDER);
	AddChild(bottom_box); 

	r.OffsetTo(0, 0);
	r.InsetBy(H_MARGIN, V_MARGIN);

	check = new BCheckBox(r, "dont_touch", DONT_TOUCH_LABEL,
					new BMessage(DONT_TOUCH_MSG),
					B_FOLLOW_TOP | B_FOLLOW_LEFT);
	check->GetPreferredSize(&w, &h);
	check->ResizeToPreferred();
	check->SetValue(B_CONTROL_ON);
	check->MoveTo(H_MARGIN, r.top);
	bottom_box->AddChild(check);

	button = new BButton(r, "apply_now", APPLY_NOW_LABEL,
					new BMessage(APPLY_NOW_MSG),
					B_FOLLOW_TOP | B_FOLLOW_RIGHT);
	button->GetPreferredSize(&w, &h);
	button->ResizeToPreferred();
	x = r.right - w;
	button->MoveTo(x, r.top);
	bottom_box->AddChild(button);

	fApplyNowButton = button;
	
	button = new BButton(r, "revert", REVERT_LABEL,
					new BMessage(REVERT_MSG),
					B_FOLLOW_TOP | B_FOLLOW_RIGHT);
	button->GetPreferredSize(&w, &h);
	button->ResizeToPreferred();
	button->MoveTo(x - w - SMALL_MARGIN, r.top);
	bottom_box->AddChild(button);

	fRevertButton = button;
	fRevertButton->SetEnabled(false);
	
	r.bottom = r.top + h;
	r.InsetBy(-H_MARGIN, -V_MARGIN);
	bottom_box->ResizeTo(Bounds().Width(), r.Height());
	
	// Set default/minimal window size
	ResizeTo(Bounds().Width(), bottom_box->Frame().bottom);
	SetSizeLimits(Bounds().Width(), 20000, Bounds().Height(), 20000);	
	
	// Enable boxes resizing modes
	top_box->SetResizingMode(B_FOLLOW_ALL);
	fPanel->SetResizingMode(B_FOLLOW_ALL);
	bottom_box->SetResizingMode(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT_RIGHT);
	
	fAddonView = NULL;

}


// --------------------------------------------------------------
NetworkSetupWindow::~NetworkSetupWindow()
{
}



// --------------------------------------------------------------
bool NetworkSetupWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


// --------------------------------------------------------------
void NetworkSetupWindow::MessageReceived
	(
	BMessage *	msg
	)
{
	switch (msg->what) {
/*
	case NEW_PROFILE_MSG:
		break;
		
	case COPY_PROFILE_MSG:
		break;
		
	case DELETE_PROFILE_MSG: {
		BString text;
		
		text << "Are you sure you want to remove '";
		text << fProfilesMenu->MenuItem()->Label();
		text << "' profile?";  
		BAlert *alert = new BAlert("Hum...", text.String() ,"Cancel", "Delete",
										NULL, B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->Go();
		break;
	}
*/	
	case SELECT_PROFILE_MSG: {
		BPath name;
		const char *path;
		bool is_default;
		bool is_current;
		
		if (msg->FindString("path", &path) != B_OK)
			break;
			
		name.SetTo(path);

		is_default = (strcmp(name.Leaf(), "default") == 0);
		is_current = (strcmp(name.Leaf(), "current") == 0);

		fApplyNowButton->SetEnabled(!is_current);
		break;
	}
	
	case SHOW_MSG: {
		if (fAddonView)
			fAddonView->RemoveSelf();
		
		fAddonView = NULL;
		if (msg->FindPointer("addon_view", (void **) &fAddonView) != B_OK)
				break;

		fPanel->AddChild(fAddonView);
		fAddonView->ResizeTo(fPanel->Bounds().Width(), fPanel->Bounds().Height());
		// fAddonView->SetViewColor((rand() % 256), (rand() % 256), (rand() % 256));
		break;
	}

	default:
		inherited::MessageReceived(msg);
	};
}


// --------------------------------------------------------------
void NetworkSetupWindow::BuildProfilesMenu
	(
	BMenu *menu,
	int32 msg_what
	)
{
	BMenuItem *	item;
	char current_profile[256] = { 0 };
	
	menu->SetRadioMode(true);

	BDirectory dir("/etc/network/profiles");

	if (dir.InitCheck() == B_OK) {
		BEntry entry;
		BMessage *msg;

		dir.Rewind();
		while (dir.GetNextEntry(&entry) >= 0) {
			BPath name;
			entry.GetPath(&name);

			if (entry.IsSymLink() &&
				strcmp("current", name.Leaf()) == 0) {
				BSymLink symlink(&entry);
			
				if (symlink.IsAbsolute())
					// oh oh, sorry, wrong symlink...
					continue;
				
				symlink.ReadLink(current_profile, sizeof(current_profile));
				continue;	
			};

			if (!entry.IsDirectory())
				continue;

			msg = new BMessage(msg_what);
			msg->AddString("path", name.Path());
			
			item = new BMenuItem(name.Leaf(), msg);
			menu->AddItem(item);
		};
	};
/*	
	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem(NEW_PROFILE_LABEL, new BMessage(NEW_PROFILE_MSG)));
	menu->AddItem(new BMenuItem(RENAME_PROFILE_LABEL, new BMessage(RENAME_PROFILE_MSG)));
	menu->AddItem(new BMenuItem(DELETE_PROFILE_LABEL, new BMessage(DELETE_PROFILE_MSG)));
*/
	if (strlen(current_profile)) {
		item = menu->FindItem(current_profile);
		if (item) {
			BString label;
			// bool is_default = (strcmp(current_profile, "default") == 0);

			label << item->Label();
			label << " (current)";
			item->SetLabel(label.String());
			item->SetMarked(true);

			// fDeleteProfileButton->SetEnabled(!is_default);
		};
	};

}

// --------------------------------------------------------------
void NetworkSetupWindow::BuildShowMenu
	(
	BMenu *menu,
	int32 msg_what
	)
{
	menu->SetRadioMode(true);
	
	menu->AddItem(new BMenuItem("Status", new BMessage(msg_what)));
	menu->AddSeparatorItem();
	
/*
	menu->AddItem(item = new BMenuItem("vt86c100/0 interface", 0));
	item->SetMarked(true);
	menu->AddItem(item = new BMenuItem("rtl8136/0 interface", 0));
	menu->AddItem(item = new BMenuItem("Dialup", 0));
	menu->AddItem(item = new BMenuItem("Identity", 0));
	menu->AddItem(item = new BMenuItem("Services", 0));
*/
	
	BPath path;
	BPath addon_path;
	BDirectory dir;
	BEntry entry;
	char * search_paths;
	char * search_path;
	char * next_path_token;

	search_paths = getenv("ADDON_PATH");
	if (!search_paths)
		// Nowhere to search addons!!!
		return;

	fMinAddonViewRect.Set(0, 0, 200, 200);	// Minimum size
		
	search_paths = strdup(search_paths);
	search_path = strtok_r(search_paths, ":", &next_path_token);
	
	while (search_path) {
		if (strncmp(search_path, "%A/", 3) == 0) {
			// compute "%A/..." path
			app_info ai;
			
			be_app->GetAppInfo(&ai);
			entry.SetTo(&ai.ref);
			entry.GetPath(&path);
			path.GetParent(&path);
			path.Append(search_path + 3);
		} else {
			path.SetTo(search_path);
			path.Append("boneyard");
		};

		search_path = strtok_r(NULL, ":", &next_path_token);

		printf("Looking into %s\n", path.Path());
		
		dir.SetTo(path.Path());
		if (dir.InitCheck() != B_OK)
			continue;
		
		dir.Rewind();
		while (dir.GetNextEntry(&entry) >= 0) {
			if (entry.IsDirectory())
				continue;

			entry.GetPath(&addon_path);
			image_id addon_id = load_add_on(addon_path.Path());
			if (addon_id < 0) {
				printf("Failed to load %s addon: %s.\n", addon_path.Path(), strerror(addon_id));
				continue;
			};
	
			printf("Addon %s loaded.\n", addon_path.Path());
		
			by_instantiate_func by_instantiate;
			network_setup_addon_instantiate get_nth_addon;
			status_t status;
			
			status = get_image_symbol(addon_id, "get_nth_addon", B_SYMBOL_TYPE_TEXT, (void **) &get_nth_addon);
			if (status == B_OK) {
				NetworkSetupAddOn *addon;
				int n;
				
				n = 0;
				while ((addon = get_nth_addon(addon_id, n)) != NULL) {
					BMessage *msg = new BMessage(msg_what);
					
					BRect r(0, 0, 0, 0);
					BView * addon_view = addon->CreateView(&r);
					fMinAddonViewRect = fMinAddonViewRect | r;
					
					msg->AddInt32("image_id", addon_id);
					msg->AddString("addon_path", addon_path.Path());
					msg->AddPointer("addon", addon);
					msg->AddPointer("addon_view", addon_view);
					menu->AddItem(new BMenuItem(addon->Name(), msg));
					n++;
				}

				continue;	// skip the Boneyard addon test...
			};

			status = get_image_symbol(addon_id, "instantiate", B_SYMBOL_TYPE_TEXT, (void **) &by_instantiate);
			if (status == B_OK) {
				BYAddon *addon;

				addon = by_instantiate();

				BRect r(0, 0, 0, 0);
				BView * addon_view = addon->CreateView(&r);
				fMinAddonViewRect = fMinAddonViewRect | r;
					
				BMessage *msg = new BMessage(msg_what);
				msg->AddInt32("image_id", addon_id);
				msg->AddString("addon_path", addon_path.Path());
				msg->AddPointer("byaddon", addon);
				msg->AddPointer("addon_view", addon_view);
				menu->AddItem(new BMenuItem(addon->Name(), msg));
				continue;
			};
	
			//  No "addon instantiate function" symbol found in this addon
			printf("No symbol \"get_nth_addon\" or \"instantiate\" not found in %s addon: not a network setup addon!\n", addon_path.Path());
			unload_add_on(addon_id);
		};
		
	};

	free(search_paths);

	menu->AddItem(new BMenuItem("Very long label of network stuff to set/display", new BMessage(msg_what)));
}	

