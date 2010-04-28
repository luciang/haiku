#include "NetworkSetupAddOn.h"
#include "NetworkSetupWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <InterfaceKit.h>
#include <Locale.h>
#include <Roster.h>
#include <StorageKit.h>
#include <SupportKit.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#undef TR_CONTEXT
#define TR_CONTEXT	"NetworkSetupWindow"


// --------------------------------------------------------------
NetworkSetupWindow::NetworkSetupWindow(const char *title)
	:
	BWindow(BRect(100, 100, 300, 300), title, B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	BMenu *showPopup = new BPopUpMenu("<please select me!>");
	_BuildShowMenu(showPopup, SHOW_MSG);

	BBox *topDivider = new BBox(B_EMPTY_STRING);
	topDivider->SetBorder(B_PLAIN_BORDER);

	// ---- Profiles section
	BMenu *profilesPopup = new BPopUpMenu("<none>");
	_BuildProfilesMenu(profilesPopup, SELECT_PROFILE_MSG);

	BMenuField *profilesMenuField = new BMenuField("profiles_menu",
			TR("Profile:"), profilesPopup);
	profilesMenuField->SetFont(be_bold_font);

	BButton *button = new BButton("manage_profiles",
			TR("Manage profiles" B_UTF8_ELLIPSIS),
			new BMessage(MANAGE_PROFILES_MSG));

	// ---- Settings section

	// Make the show popup field half the whole width and centered
	BMenuField *showMenuField = new BMenuField("show_menu", 
			TR("Show:"), showPopup);
	showMenuField->SetFont(be_bold_font);

	fPanel = new BBox("showview_box");
	fPanel->SetBorder(B_NO_BORDER);

	// ---- Bottom globals buttons section
	BBox *bottomDivider = new BBox(B_EMPTY_STRING);
	bottomDivider->SetBorder(B_PLAIN_BORDER);

	BCheckBox *dontTouchCheckBox = new BCheckBox("dont_touch", 
			TR("Prevent unwanted changes"), new BMessage(DONT_TOUCH_MSG));
	dontTouchCheckBox->SetValue(B_CONTROL_ON);
	
	fApplyNowButton = new BButton("apply_now", TR("Apply Now"),
			new BMessage(APPLY_NOW_MSG));

	fRevertButton = new BButton("revert", TR("Revert"), 
			new BMessage(REVERT_MSG));
	fRevertButton->SetEnabled(false);

	// Enable boxes resizing modes
	fPanel->SetResizingMode(B_FOLLOW_ALL);

	// Build the layout
	SetLayout(new BGroupLayout(B_VERTICAL));

	AddChild(BGroupLayoutBuilder(B_VERTICAL, 10)
		.AddGroup(B_HORIZONTAL, 5)
			.Add(profilesMenuField)
			.AddGlue()
			.Add(button)
		.End()
		.Add(topDivider)
		.Add(showMenuField)
		.Add(fPanel)
		.Add(bottomDivider)
		.AddGroup(B_HORIZONTAL, 5)
			.Add(dontTouchCheckBox)
			.Add(fRevertButton)
			.Add(fApplyNowButton)
		.End()
		.SetInsets(10, 10, 10, 10)
	);

	topDivider->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 1));
	bottomDivider->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 1));
	fPanel->SetExplicitMinSize(BSize(fMinAddonViewRect.Width(), 
			fMinAddonViewRect.Height()));

	fAddonView = NULL;
	
	
}


NetworkSetupWindow::~NetworkSetupWindow()
{
}


bool
NetworkSetupWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
NetworkSetupWindow::MessageReceived(BMessage*	msg)
{
	switch (msg->what) {

	case NEW_PROFILE_MSG:
		break;
		
	case DELETE_PROFILE_MSG: {
		break;
	}
	
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
		fAddonView->ResizeTo(fPanel->Bounds().Width(), 
			fPanel->Bounds().Height());
		break;
	}

	default:
		inherited::MessageReceived(msg);
	}
}


void
NetworkSetupWindow::_BuildProfilesMenu(BMenu* menu, int32 msg_what)
{
	BMenuItem*	item;
	char current_profile[256] = { 0 };
	
	menu->SetRadioMode(true);

	BDirectory dir("/boot/common/settings/network/profiles");

	if (dir.InitCheck() == B_OK) {
		BEntry entry;
		BMessage* msg;

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
		}
	}

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(TR("New" B_UTF8_ELLIPSIS), new BMessage(NEW_PROFILE_MSG)));
	menu->AddItem(new BMenuItem(TR("Delete"), new BMessage(DELETE_PROFILE_MSG)));

	if (strlen(current_profile)) {
		item = menu->FindItem(current_profile);
		if (item) {
			BString label;
			label << item->Label();
			label << " (current)";
			item->SetLabel(label.String());
			item->SetMarked(true);
		}
	}
}


void
NetworkSetupWindow::_BuildShowMenu(BMenu* menu, int32 msg_what)
{
	menu->SetRadioMode(true);		
	BPath path;
	BPath addon_path;
	BDirectory dir;
	BEntry entry;

	char* search_paths = getenv("ADDON_PATH");
	if (!search_paths)
		return;

	fMinAddonViewRect.Set(0, 0, 200, 200);	// Minimum size
		
	search_paths = strdup(search_paths);	
	char* next_path_token;
	char* search_path = strtok_r(search_paths, ":", &next_path_token);
	
	while (search_path) {
		if (strncmp(search_path, "%A/", 3) == 0) {
			app_info ai;			
			be_app->GetAppInfo(&ai);
			entry.SetTo(&ai.ref);
			entry.GetPath(&path);
			path.GetParent(&path);
			path.Append(search_path + 3);
		} else {
			path.SetTo(search_path);
			path.Append("network_setup");
		}

		search_path = strtok_r(NULL, ":", &next_path_token);
		
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
				printf("Failed to load %s addon: %s.\n", addon_path.Path(), 
					strerror(addon_id));
				continue;
			}

			network_setup_addon_instantiate get_nth_addon;
			status_t status = get_image_symbol(addon_id, "get_nth_addon", 
				B_SYMBOL_TYPE_TEXT, (void **) &get_nth_addon);
				
			if (status == B_OK) {
				NetworkSetupAddOn *addon;
				int n = 0;
				while ((addon = get_nth_addon(addon_id, n)) != NULL) {
					BMessage* msg = new BMessage(msg_what);
					
					BRect r(0, 0, 0, 0);
					BView* addon_view = addon->CreateView(&r);
					fMinAddonViewRect = fMinAddonViewRect | r;
					
					msg->AddInt32("image_id", addon_id);
					msg->AddString("addon_path", addon_path.Path());
					msg->AddPointer("addon", addon);
					msg->AddPointer("addon_view", addon_view);
					menu->AddItem(new BMenuItem(addon->Name(), msg));
					n++;
				}
				continue;
			}

			//  No "addon instantiate function" symbol found in this addon
			printf("No symbol \"get_nth_addon\" found in %s addon: not a "
				"network setup addon!\n", addon_path.Path());
			unload_add_on(addon_id);
		}
	}

	free(search_paths);
}	
