/* DeskbarView - main_daemon's deskbar menu and view
 *
 * Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */


#include <Bitmap.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <kernel/fs_info.h>
#include <kernel/fs_index.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Query.h>
#include <Rect.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>
#include <SymLink.h>
#include <VolumeRoster.h>
#include <Window.h>

#include <stdio.h>
#include <malloc.h>

#include <E-mail.h>
#include <MailDaemon.h>
#include <MailSettings.h>
#include <MDRLanguage.h>

#include "DeskbarView.h"
#include "DeskbarViewIcons.h"

const char *kTrackerSignature = "application/x-vnd.Be-TRAK";


//-----The following #defines get around a bug in get_image_info on ppc---
#if __INTEL__
#define text_part text
#define text_part_size text_size
#else
#define text_part data
#define text_part_size data_size
#endif

extern "C" _EXPORT BView* instantiate_deskbar_item();


status_t our_image(image_info* image)
{
	int32 cookie = 0;
	status_t ret;
	while ((ret = get_next_image_info(0,&cookie,image)) == B_OK)
	{
		if ((char*)our_image >= (char*)image->text_part &&
			(char*)our_image <= (char*)image->text_part + image->text_part_size)
			break;
	}

	return ret;
}

BView* instantiate_deskbar_item(void)
{
	return new DeskbarView(BRect(0, 0, 15, 15));
}


//-------------------------------------------------------------------------------
//	#pragma mark -


DeskbarView::DeskbarView(BRect frame)
	:
	BView(frame, "mail_daemon", B_FOLLOW_NONE, B_WILL_DRAW | B_PULSE_NEEDED),
	fStatus(kStatusNoMail)
{
	_InitBitmaps();
}


DeskbarView::DeskbarView(BMessage *message)
	:
	BView(message),
	fStatus(kStatusNoMail)
{
	_InitBitmaps();
}


DeskbarView::~DeskbarView()
{
	for (int i = 0; i < kStatusCount; i++)
		delete fBitmaps[i];

	for (int32 i = 0; i < fNewMailQueries.CountItems(); i++)
		delete ((BQuery *)(fNewMailQueries.ItemAt(i)));
}


void DeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	SetLowColor(ViewColor());

	if (be_roster->IsRunning("application/x-vnd.Be-POST")) {
		_RefreshMailQuery();
	} else {
		BDeskbar deskbar;
		deskbar.RemoveItem("mail_daemon");
	}
}


void DeskbarView::_RefreshMailQuery()
{
	for (int32 i = 0; i < fNewMailQueries.CountItems(); i++)
		delete ((BQuery *)(fNewMailQueries.ItemAt(i)));
	fNewMailQueries.MakeEmpty();

	BVolumeRoster volumes;
	BVolume volume;
	fNewMessages = 0;

	while (volumes.GetNextVolume(&volume) == B_OK) {
		BQuery *newMailQuery = new BQuery;

		newMailQuery->SetTarget(this);
		newMailQuery->SetVolume(&volume);
		newMailQuery->PushAttr(B_MAIL_ATTR_STATUS);
		newMailQuery->PushString("New");
		newMailQuery->PushOp(B_EQ);
		newMailQuery->PushAttr("BEOS:TYPE");
		newMailQuery->PushString("text/x-email");
		newMailQuery->PushOp(B_EQ);
		newMailQuery->PushAttr("BEOS:TYPE");
		newMailQuery->PushString("text/x-partial-email");
		newMailQuery->PushOp(B_EQ);
		newMailQuery->PushOp(B_OR);
		newMailQuery->PushOp(B_AND);
		newMailQuery->Fetch();

		BEntry entry;
		while (newMailQuery->GetNextEntry(&entry) == B_OK) {
			if (entry.InitCheck() == B_OK)
				fNewMessages++;
		}

		fNewMailQueries.AddItem(newMailQuery);
	}

	fStatus = (fNewMessages > 0) ? kStatusNewMail : kStatusNoMail;
	Invalidate();
}


DeskbarView* DeskbarView::Instantiate(BMessage *data)
{
	if (!validate_instantiation(data, "DeskbarView"))
		return NULL;

	return new DeskbarView(data);
}


status_t DeskbarView::Archive(BMessage *data,bool deep) const
{
	BView::Archive(data, deep);

	data->AddString("add_on", "application/x-vnd.Be-POST");
	return B_NO_ERROR;
}


void DeskbarView::Draw(BRect /*updateRect*/)
{
	if (fBitmaps[fStatus] == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);
	DrawBitmap(fBitmaps[fStatus]);
	SetDrawingMode(B_OP_COPY);
}


status_t OpenFolder(const char* end)
{
	BPath path;
	find_directory(B_USER_DIRECTORY, &path);
	path.Append(end);

	entry_ref ref;
	if (get_ref_for_path(path.Path(),&ref) != B_OK)
		return B_NAME_NOT_FOUND;

	if (!BEntry(&ref).Exists())
		return B_NAME_NOT_FOUND;

	BMessage open_mbox(B_REFS_RECEIVED);
	open_mbox.AddRef("refs",&ref);

	BMessenger tracker("application/x-vnd.Be-TRAK");
	tracker.SendMessage(&open_mbox);
	return B_OK;
}


void
DeskbarView::MessageReceived(BMessage* message)
{
	switch(message->what)
	{
		case MD_CHECK_SEND_NOW:
			// also happens in DeskbarView::MouseUp() with
			// B_TERTIARY_MOUSE_BUTTON pressed
			BMailDaemon::CheckMail(true);
			break;
		case MD_CHECK_FOR_MAILS:
			BMailDaemon::CheckMail(false,message->FindString("account"));
			break;
		case MD_SEND_MAILS:
			BMailDaemon::SendQueuedMail();
			break;

		case MD_OPEN_NEW:
		{
			char* argv[] = {"New Message", "mailto:"};
			be_roster->Launch("text/x-email",2,argv);
			break;
		}
		case MD_OPEN_PREFS:
			be_roster->Launch("application/x-vnd.Haiku-Mail");
			break;

		case MD_REFRESH_QUERY:
			_RefreshMailQuery();
			break;

		case B_QUERY_UPDATE:
		{
			int32 what;
			message->FindInt32("opcode",&what);
			switch (what) {
				case B_ENTRY_CREATED:
					fNewMessages++;
					break;
				case B_ENTRY_REMOVED:
					fNewMessages--;
					break;
			}
			fStatus = (fNewMessages > 0) ? kStatusNewMail : kStatusNoMail;
			Invalidate();
			break;
		}
		case B_QUIT_REQUESTED:
			BMailDaemon::Quit();
			break;

		// open received files in the standard mail application
		case B_REFS_RECEIVED:
		{
			BMessage argv(B_ARGV_RECEIVED);
			argv.AddString("argv", "E-mail");

			entry_ref ref;
			BPath path;
			int i = 0;

			while (message->FindRef("refs", i++, &ref) == B_OK
				&& path.SetTo(&ref) == B_OK) {
				//fprintf(stderr,"got %s\n", path.Path());
				argv.AddString("argv", path.Path());
			}

			if (i > 1) {
				argv.AddInt32("argc", i);
				be_roster->Launch("text/x-email", &argv);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}


void
DeskbarView::_InitBitmaps()
{
	for (int i = 0; i < kStatusCount; i++)
		fBitmaps[i] = NULL;

	image_info info;
	if (our_image(&info) != B_OK)
		return;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BResources resources(&file);
	if (resources.InitCheck() != B_OK)
		return;

	for (int i = 0; i < kStatusCount; i++) {
		const void* data = NULL;
		size_t size;
		data = resources.LoadResource(B_VECTOR_ICON_TYPE,
			kIconNoMail + i, &size);
		if (data != NULL) {
			BBitmap* icon = new BBitmap(Bounds(), B_RGBA32);
			if (icon->InitCheck() == B_OK
				&& BIconUtils::GetVectorIcon((const uint8 *)data,
					size, icon) == B_OK) {
				fBitmaps[i] = icon;
			} else
				delete icon;
		}
	}
}


void
DeskbarView::Pulse()
{
	// TODO: Check if mail_daemon is still running
}


void
DeskbarView::MouseUp(BPoint pos)
{
	if (fLastButtons & B_PRIMARY_MOUSE_BUTTON) {
		if (OpenFolder("mail/mailbox") != B_OK
			&& OpenFolder("mail/in") != B_OK
			&& OpenFolder("mail/INBOX") != B_OK)
			OpenFolder("mail");
	}

	if (fLastButtons & B_TERTIARY_MOUSE_BUTTON)
		BMailDaemon::CheckMail(true);
}


void
DeskbarView::MouseDown(BPoint pos)
{
	Looper()->CurrentMessage()->FindInt32("buttons",&fLastButtons);
	Looper()->CurrentMessage()->PrintToStream();

	if (fLastButtons & B_SECONDARY_MOUSE_BUTTON) {
		ConvertToScreen(&pos);

		BPopUpMenu* menu = _BuildMenu();
		if (menu) {
			menu->Go(pos, true, true, BRect(pos.x - 2, pos.y - 2,
				pos.x + 2, pos.y + 2), true);
		}
	}
}


bool
DeskbarView::_CreateMenuLinks(BDirectory& directory, BPath& path)
{
	status_t status = directory.SetTo(path.Path());
	if (status == B_OK)
		return true;

	// Check if the directory has to be created (and do it in this case,
	// filling it with some standard links).  Normally the installer will
	// create the directory and fill it with links, so normally this doesn't
	// get used.

	BEntry entry(path.Path());
	if (status != B_ENTRY_NOT_FOUND
		|| entry.GetParent(&directory) < B_OK
		|| directory.CreateDirectory(path.Leaf(), NULL) < B_OK
		|| directory.SetTo(path.Path()) < B_OK)
		return false;

	BPath targetPath;
	find_directory(B_USER_DIRECTORY, &targetPath);
	targetPath.Append("mail/in");

	directory.CreateSymLink("Open Inbox Folder", targetPath.Path(), NULL);
	targetPath.GetParent(&targetPath);
	directory.CreateSymLink("Open Mail Folder", targetPath.Path(), NULL);

	// create the draft query

	BFile file;
	if (directory.CreateFile("Open Draft", &file) < B_OK)
		return true;

	BString string("MAIL:draft==1");
	file.WriteAttrString("_trk/qrystr", &string);
	string = "E-mail";
	file.WriteAttrString("_trk/qryinitmime", &string);
	BNodeInfo(&file).SetType("application/x-vnd.Be-query");

	return true;
}


void
DeskbarView::_CreateNewMailQuery(BEntry& query)
{
	BFile file(&query, B_READ_WRITE | B_CREATE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BString string("((" B_MAIL_ATTR_STATUS "==\"[nN][eE][wW]\")&&((BEOS:TYPE=="
		"\"text/x-email\")||(BEOS:TYPE==\"text/x-partial-email\")))");
	file.WriteAttrString("_trk/qrystr", &string);
	string = "E-mail";
	file.WriteAttrString("_trk/qryinitmime", &string);
	BNodeInfo(&file).SetType("application/x-vnd.Be-query");
}


BPopUpMenu*
DeskbarView::_BuildMenu()
{
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
	menu->SetFont(be_plain_font);

	menu->AddItem(new BMenuItem(MDR_DIALECT_CHOICE (
		"Create New Message", "N) 新規メッセージ作成")B_UTF8_ELLIPSIS,
		new BMessage(MD_OPEN_NEW)));
	menu->AddSeparatorItem();

	BMessenger tracker(kTrackerSignature);
	BNavMenu* navMenu;
	BMenuItem* item;
	BMessage* msg;
	entry_ref ref;

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("Mail/Menu Links");

	BDirectory directory;
	if (_CreateMenuLinks(directory, path)) {
		int32 count = 0;

		while (directory.GetNextRef(&ref) == B_OK) {
			count++;

			path.SetTo(&ref);
			// the true here dereferences the symlinks all the way :)
			BEntry entry(&ref, true);

			// do we want to use the NavMenu, or just an ordinary BMenuItem?
			// we are using the NavMenu only for directories and queries
			bool useNavMenu = false;

			if (entry.InitCheck() == B_OK) {
				if (entry.IsDirectory())
					useNavMenu = true;
				else if (entry.IsFile()) {
					// Files should use the BMenuItem unless they are queries
					char mimeString[B_MIME_TYPE_LENGTH];
					BNode node(&entry);
					BNodeInfo info(&node);
					if (info.GetType(mimeString) == B_OK
						&& strcmp(mimeString, "application/x-vnd.Be-query")
							== 0)
						useNavMenu = true;
				}
				// clobber the existing ref only if the symlink derefernces
				// completely, otherwise we'll stick with what we have
				entry.GetRef(&ref);
			}

			msg = new BMessage(B_REFS_RECEIVED);
			msg->AddRef("refs", &ref);

			if (useNavMenu) {
				item = new BMenuItem(navMenu = new BNavMenu(path.Leaf(),
					B_REFS_RECEIVED, tracker), msg);
				navMenu->SetNavDir(&ref);
			} else
				item = new BMenuItem(path.Leaf(), msg);

			menu->AddItem(item);
			if(entry.InitCheck() != B_OK)
				item->SetEnabled(false);
		}
		if (count > 0)
			menu->AddSeparatorItem();
	}

	// Hack for R5's buggy Query Notification
	#ifdef HAIKU_TARGET_PLATFORM_BEOS
		menu->AddItem(new BMenuItem(
			MDR_DIALECT_CHOICE("Refresh New Mail Count",
				"未読メールカウントを更新"),
			new BMessage(MD_REFRESH_QUERY)));
	#endif

	// The New E-mail query

	if (fNewMessages > 0) {
		BString string;
		MDR_DIALECT_CHOICE(
			string << fNewMessages << " new message"
				<< (fNewMessages != 1 ? "s" : B_EMPTY_STRING),
			string << fNewMessages << " 通の未読メッセージ");

		find_directory(B_USER_SETTINGS_DIRECTORY, &path);
		path.Append("Mail/New E-mail");

		BEntry query(path.Path());
		if (!query.Exists())
			_CreateNewMailQuery(query);
		query.GetRef(&ref);

		item = new BMenuItem(navMenu = new BNavMenu(string.String(),
			B_REFS_RECEIVED, BMessenger(kTrackerSignature)),
			msg = new BMessage(B_REFS_RECEIVED));
		msg->AddRef("refs", &ref);
		navMenu->SetNavDir(&ref);

		menu->AddItem(item);
	} else {
		menu->AddItem(item = new BMenuItem(
			MDR_DIALECT_CHOICE ("No new messages","未読メッセージなし"), NULL));
		item->SetEnabled(false);
	}

	BList list;
	GetInboundMailChains(&list);

	if (modifiers() & B_SHIFT_KEY) {
		BMenu *chainMenu = new BMenu(
			MDR_DIALECT_CHOICE ("Check For Mails Only","R) メール受信のみ"));
		BFont font;
		menu->GetFont(&font);
		chainMenu->SetFont(&font);

		for (int32 i = 0; i < list.CountItems(); i++) {
			BMailChain* chain = (BMailChain*)list.ItemAt(i);

			BMessage* message = new BMessage(MD_CHECK_FOR_MAILS);
			message->AddString("account", chain->Name());

			chainMenu->AddItem(new BMenuItem(chain->Name(), message));
			delete chain;
		}
		if (list.IsEmpty()) {
			item = new BMenuItem("<no accounts>", NULL);
			item->SetEnabled(false);
			chainMenu->AddItem(item);
		}
		chainMenu->SetTargetForItems(this);
		menu->AddItem(new BMenuItem(chainMenu, new BMessage(MD_CHECK_FOR_MAILS)));

		// Not used:
		// menu->AddItem(new BMenuItem(MDR_DIALECT_CHOICE (
		// "Check For Mails Only","メール受信のみ"), new BMessage(MD_CHECK_FOR_MAILS)));
		menu->AddItem(new BMenuItem(
			MDR_DIALECT_CHOICE ("Send Pending Mails", "M) 保留メールを送信"),
		new BMessage(MD_SEND_MAILS)));
	} else {
		menu->AddItem(item = new BMenuItem(
			MDR_DIALECT_CHOICE ("Check For Mail Now", "C) メールチェック"),
			new BMessage(MD_CHECK_SEND_NOW)));
		if (list.IsEmpty())
			item->SetEnabled(false);
	}

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(
		MDR_DIALECT_CHOICE ("Edit Preferences", "P) メール環境設定") B_UTF8_ELLIPSIS,
		new BMessage(MD_OPEN_PREFS)));

	if (modifiers() & B_SHIFT_KEY) {
		menu->AddItem(new BMenuItem(
			MDR_DIALECT_CHOICE ("Shutdown Mail Services", "Q) 終了"),
			new BMessage(B_QUIT_REQUESTED)));
	}

	// Reset Item Targets (only those which aren't already set)

	for (int32 i = menu->CountItems(); i-- > 0;) {
		item = menu->ItemAt(i);
		if (item && (msg = item->Message()) != NULL) {
			if (msg->what == B_REFS_RECEIVED)
				item->SetTarget(tracker);
			else
				item->SetTarget(this);
		}
	}
	return menu;
}
