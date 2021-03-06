/*
 * Copyright 2001-2010, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Pfeiffer
 */


#include "PrinterListView.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <Directory.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <String.h>

#include "pr_server.h"
#include "Messages.h"
#include "Globals.h"
#include "PrintersWindow.h"
#include "SpoolFolder.h"


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "PrinterListView"


// #pragma mark -- PrinterListView


PrinterListView::PrinterListView(BRect frame)
	: Inherited(frame, "printers_list", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fFolder(NULL),
	fActivePrinter(NULL)
{
	fLayoutData.fLeftColumnMaximumWidth = 100;
	fLayoutData.fRightColumnMaximumWidth = 100;
}


PrinterListView::~PrinterListView()
{
	while (!IsEmpty())
		delete RemoveItem(0L);
}


void
PrinterListView::BuildPrinterList()
{
	// clear list
	while (!IsEmpty())
		delete RemoveItem(0L);

	// Find directory containing printer definition nodes
	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while(dir.GetNextEntry(&entry) == B_OK) {
		BDirectory printer(&entry);
		_AddPrinter(printer, false);
	}
	
	_LayoutPrinterItems();
}


void
PrinterListView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	SetSelectionMessage(new BMessage(kMsgPrinterSelected));
	SetInvocationMessage(new BMessage(kMsgMakeDefaultPrinter));
	SetTarget(Window());

	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK) {
		// directory has to exist in order to start watching it
		if (create_directory(path.Path(), 0777) != B_OK)
			return;
		dir.SetTo(path.Path());
	}

	fFolder = new FolderWatcher(Window(), dir, true);
	fFolder->SetListener(this);

	BuildPrinterList();

	// Select active printer
	BString activePrinterName(ActivePrinterName());
	for (int32 i = 0; i < CountItems(); i ++) {
		PrinterItem* item = dynamic_cast<PrinterItem*>(ItemAt(i));
		if (item != NULL && item->Name() == activePrinterName) {
			Select(i);
			fActivePrinter = item;
			break;
		}
	}
}


bool
PrinterListView::QuitRequested()
{
	delete fFolder;
	return true;
}


void
PrinterListView::UpdateItem(PrinterItem* item)
{
	item->UpdatePendingJobs();
	InvalidateItem(IndexOf(item));
}


PrinterItem*
PrinterListView::ActivePrinter() const
{
	return fActivePrinter;
}


void
PrinterListView::SetActivePrinter(PrinterItem* item)
{
	fActivePrinter = item;
}


PrinterItem*
PrinterListView::SelectedItem() const
{
	return dynamic_cast<PrinterItem*>(ItemAt(CurrentSelection()));
}


void
PrinterListView::_AddPrinter(BDirectory& printer, bool calculateLayout)
{
	BString state;
	node_ref node;
		// If the entry is a directory
	if (printer.InitCheck() == B_OK
		&& printer.GetNodeRef(&node) == B_OK
		&& _FindItem(&node) == NULL
		&& printer.ReadAttrString(PSRV_PRINTER_ATTR_STATE, &state) == B_OK
		&& state == "free") {
			// Check it's Mime type for a spool director
		BNodeInfo info(&printer);
		char buffer[256];

		if (info.GetType(buffer) == B_OK
			&& strcmp(buffer, PSRV_PRINTER_FILETYPE) == 0) {
				// Yes, it is a printer definition node
			AddItem(new PrinterItem(dynamic_cast<PrintersWindow*>(Window()), 
				printer, fLayoutData));
			if (calculateLayout)
				_LayoutPrinterItems();
		}
	}
}


void
PrinterListView::_LayoutPrinterItems()
{
	float& leftColumnMaximumWidth = fLayoutData.fLeftColumnMaximumWidth;
	float& rightColumnMaximumWidth = fLayoutData.fRightColumnMaximumWidth;
		
	for (int32 i = 0; i < CountItems(); i ++) {	
		PrinterItem* item = dynamic_cast<PrinterItem*>(ItemAt(i));

		float leftColumnWidth = 0;
		float rightColumnWidth = 0;
		item->GetColumnWidth(this, leftColumnWidth, rightColumnWidth);
		
		leftColumnMaximumWidth = MAX(leftColumnMaximumWidth, 
			leftColumnWidth);
		rightColumnMaximumWidth = MAX(rightColumnMaximumWidth, 
			rightColumnWidth);
	}
	
	Invalidate();
}


PrinterItem*
PrinterListView::_FindItem(node_ref* node) const
{
	for (int32 i = CountItems() - 1; i >= 0; i--) {
		PrinterItem* item = dynamic_cast<PrinterItem*>(ItemAt(i));
		node_ref ref;
		if (item && item->Node()->GetNodeRef(&ref) == B_OK && ref == *node)
			return item;
	}
	return NULL;
}


void
PrinterListView::_EntryCreated(node_ref* node, entry_ref* entry)
{
	BDirectory printer(node);
	_AddPrinter(printer, true);
}


void
PrinterListView::_EntryRemoved(node_ref* node)
{
	PrinterItem* item = _FindItem(node);
	if (item) {
		if (item == fActivePrinter)
			fActivePrinter = NULL;

		RemoveItem(item);
		delete item;
	}
}


void
PrinterListView::_AttributeChanged(node_ref* node)
{
	BDirectory printer(node);
	_AddPrinter(printer, true);
}




// #pragma mark -- PrinterItem


BBitmap* PrinterItem::sIcon = NULL;
BBitmap* PrinterItem::sSelectedIcon = NULL;


PrinterItem::PrinterItem(PrintersWindow* window, const BDirectory& node,
		PrinterListLayoutData& layoutData)
	: BListItem(0, false),
	fFolder(NULL),
	fNode(node),
	fLayoutData(layoutData)
{
	BRect rect(0, 0, B_LARGE_ICON - 1, B_LARGE_ICON - 1);
	if (sIcon == NULL) {
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
		sIcon = new BBitmap(rect, B_RGBA32);
#else
		sIcon = new BBitmap(rect, B_CMAP8);
#endif
		BMimeType type(PSRV_PRINTER_FILETYPE);
		type.GetIcon(sIcon, B_LARGE_ICON);
	}

	if (sIcon && sIcon->IsValid() && sSelectedIcon == NULL) {
		BBitmap *checkMark = LoadBitmap("check_mark_icon", 'BBMP');
		if (checkMark && checkMark->IsValid()) {
			sSelectedIcon = new BBitmap(rect, B_RGBA32, true);
			if (sSelectedIcon && sSelectedIcon->IsValid()) {
				// draw check mark at bottom left over printer icon
				BView *view = new BView(rect, "offscreen", B_FOLLOW_ALL,
					B_WILL_DRAW);
				float y = rect.Height() - checkMark->Bounds().Height();
				sSelectedIcon->Lock();
				sSelectedIcon->AddChild(view);
				view->DrawBitmap(sIcon);
				view->SetDrawingMode(B_OP_ALPHA);
				view->DrawBitmap(checkMark, BPoint(0, y));
				view->Sync();
				view->RemoveSelf();
				sSelectedIcon->Unlock();
				delete view;
			}
		}
		delete checkMark;
	}

	// Get Name of printer
	_GetStringProperty(PSRV_PRINTER_ATTR_PRT_NAME, fName);
	_GetStringProperty(PSRV_PRINTER_ATTR_COMMENTS, fComments);
	_GetStringProperty(PSRV_PRINTER_ATTR_TRANSPORT, fTransport);
	_GetStringProperty(PSRV_PRINTER_ATTR_DRV_NAME, fDriverName);

	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path) != B_OK)
		return;

	// Setup spool folder
	path.Append(fName.String());
	BDirectory dir(path.Path());
	if (dir.InitCheck() == B_OK) {
		fFolder = new SpoolFolder(window, this, dir);
		UpdatePendingJobs();
	}
}


PrinterItem::~PrinterItem()
{
	delete fFolder;
}


void
PrinterItem::GetColumnWidth(BView* view, float& leftColumn, float& rightColumn)
{
	BFont font;
	view->GetFont(&font);
	
	leftColumn = font.StringWidth(fName.String());
	leftColumn = MAX(leftColumn, font.StringWidth(fDriverName.String()));
	
	rightColumn = font.StringWidth(fPendingJobs.String());
	rightColumn = MAX(rightColumn, font.StringWidth(fTransport.String()));
	rightColumn = MAX(rightColumn, font.StringWidth(fComments.String()));
}


void
PrinterItem::Update(BView *owner, const BFont *font)
{
	BListItem::Update(owner,font);

	font_height height;
	font->GetHeight(&height);

	SetHeight((height.ascent + height.descent + height.leading) * 3.0 + 8.0);
}


bool PrinterItem::Remove(BListView* view)
{
	BMessenger msgr;
	if (GetPrinterServerMessenger(msgr) == B_OK) {
		BMessage script(B_DELETE_PROPERTY);
		script.AddSpecifier("Printer", view->IndexOf(this));

		BMessage reply;
		if (msgr.SendMessage(&script,&reply) == B_OK)
			return true;
	}
	return false;
}


void
PrinterItem::DrawItem(BView *owner, BRect /*bounds*/, bool complete)
{
	BListView* list = dynamic_cast<BListView*>(owner);
	if (list == NULL)
		return;

	BFont font;
	owner->GetFont(&font);

	font_height height;
	font.GetHeight(&height);

	float fntheight = height.ascent + height.descent + height.leading;

	BRect bounds = list->ItemFrame(list->IndexOf(this));

	rgb_color color = owner->ViewColor();
	rgb_color oldViewColor = color;
	rgb_color oldLowColor = owner->LowColor();
	rgb_color oldHighColor = owner->HighColor();

	if (IsSelected())
		color = tint_color(color, B_HIGHLIGHT_BACKGROUND_TINT);

	owner->SetViewColor(color);
	owner->SetLowColor(color);
	owner->SetHighColor(color);

	owner->FillRect(bounds);

	owner->SetLowColor(oldLowColor);
	owner->SetHighColor(oldHighColor);

	float iconColumnWidth = B_LARGE_ICON + 8.0;
	float x = iconColumnWidth;
	BPoint iconPt(bounds.LeftTop() + BPoint(2.0, 2.0));
	BPoint namePt(iconPt + BPoint(x, fntheight));
	BPoint driverPt(iconPt + BPoint(x, fntheight * 2.0));
	BPoint defaultPt(iconPt + BPoint(x, fntheight * 3.0));

	float totalWidth = bounds.Width() - iconColumnWidth;
	float maximumWidth = fLayoutData.fLeftColumnMaximumWidth +
		fLayoutData.fRightColumnMaximumWidth;
	float width;
	if (totalWidth < maximumWidth) {
		width = fLayoutData.fRightColumnMaximumWidth * totalWidth /
			maximumWidth;
	} else {
		width = fLayoutData.fRightColumnMaximumWidth;
	}
		
	BPoint pendingPt(bounds.right - width - 8.0, namePt.y);
	BPoint transportPt(bounds.right - width - 8.0, driverPt.y);
	BPoint commentPt(bounds.right - width - 8.0, defaultPt.y);


	drawing_mode mode = owner->DrawingMode();
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	owner->SetDrawingMode(B_OP_ALPHA);
#else
	owner->SetDrawingMode(B_OP_OVER);
#endif
	if (IsActivePrinter()) {
		if (sSelectedIcon && sSelectedIcon->IsValid())
			owner->DrawBitmap(sSelectedIcon, iconPt);
		else
			owner->DrawString(B_TRANSLATE("Default Printer"), defaultPt);
	} else {
		if (sIcon && sIcon->IsValid())
			owner->DrawBitmap(sIcon, iconPt);
	}

	owner->SetDrawingMode(B_OP_OVER);

	// left of item
	BString s = fName;
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, pendingPt.x - namePt.x);
	owner->DrawString(s.String(), s.Length(), namePt);
	s = fDriverName.String();
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, pendingPt.x - driverPt.x);
	owner->DrawString(s.String(), s.Length(), driverPt);

	// right of item
	s = fPendingJobs;
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, bounds.Width() - pendingPt.x);
	owner->DrawString(s.String(), s.Length(), pendingPt);
	s = fTransport;
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, bounds.Width() - transportPt.x);
	owner->DrawString(s.String(), s.Length(), transportPt);
	s = fComments;
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, bounds.Width() - commentPt.x);
	owner->DrawString(s.String(), s.Length(), commentPt);

	owner->SetDrawingMode(mode);
	owner->SetViewColor(oldViewColor);
}


bool
PrinterItem::IsActivePrinter() const
{
	return fName == ActivePrinterName();
}


bool
PrinterItem::HasPendingJobs() const
{
	return fFolder && fFolder->CountJobs() > 0;
}


SpoolFolder*
PrinterItem::Folder() const
{
	return fFolder;
}


BDirectory*
PrinterItem::Node()
{
	return &fNode;
}


void
PrinterItem::UpdatePendingJobs()
{
	if (fFolder) {
		uint32 pendingJobs = fFolder->CountJobs();
		if (pendingJobs == 1) {
			fPendingJobs = B_TRANSLATE("1 pending job.");
			return;
		} else if (pendingJobs > 1) {
			fPendingJobs << pendingJobs << B_TRANSLATE(" pending jobs.");
			return;
		}
	}
	fPendingJobs = B_TRANSLATE("No pending jobs.");
}


void
PrinterItem::_GetStringProperty(const char* propName, BString& outString)
{
	fNode.ReadAttrString(propName, &outString);
}

