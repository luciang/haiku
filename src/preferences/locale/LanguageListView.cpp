/*
 * Copyright 2006-2010, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Adrien Destugues <pulkomandy@gmail.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Oliver Tappe <zooey@hirschkaefer.de>
 */


#include "LanguageListView.h"

#include <stdio.h>

#include <new>

#include <Bitmap.h>
#include <Country.h>
#include <Catalog.h>
#include <Window.h>


#define MAX_DRAG_HEIGHT		200.0
#define ALPHA				170

#define B_TRANSLATE_CONTEXT "LanguageListView"


LanguageListItem::LanguageListItem(const char* text, const char* id,
	const char* code)
	:
	BStringItem(text),
	fID(id),
	fCode(code)
{
	// TODO: should probably keep the BCountry as a member of the class
	BCountry country(id);

	fIcon = new(std::nothrow) BBitmap(BRect(0, 0, 15, 15), B_RGBA32);
	if (fIcon != NULL && country.GetIcon(fIcon) != B_OK) {
		delete fIcon;
		fIcon = NULL;
	}
}


LanguageListItem::LanguageListItem(const LanguageListItem& other)
	:
	BStringItem(other.Text()),
	fID(other.ID()),
	fCode(other.Code()),
	fIcon(NULL)
{
	if (other.fIcon != NULL)
		fIcon = new BBitmap(*other.fIcon);
}


LanguageListItem::~LanguageListItem()
{
	delete fIcon;
}


void
LanguageListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color kHighlight = {140, 140, 140, 0};
	rgb_color kBlack = {0, 0, 0, 0};

	if (IsSelected() || complete) {
		rgb_color color;
		if (IsSelected())
			color = kHighlight;
		else
			color = owner->ViewColor();
		owner->SetHighColor(color);
		owner->SetLowColor(color);
		owner->FillRect(frame);
		owner->SetHighColor(kBlack);
	} else
		owner->SetLowColor(owner->ViewColor());

	frame.left += 4;
	BRect iconFrame(frame);
	iconFrame.Set(iconFrame.left, iconFrame.top + 1, iconFrame.left + 15,
		iconFrame.top + 16);

	if (fIcon != NULL && fIcon->IsValid()) {
		owner->SetDrawingMode(B_OP_OVER);
		owner->DrawBitmap(fIcon, iconFrame);
		owner->SetDrawingMode(B_OP_COPY);
	}

	frame.left += 16 * (OutlineLevel() + 1);

	BString text = Text();
	if (IsEnabled())
		owner->SetHighColor(kBlack);
	else {
		owner->SetHighColor(tint_color(owner->LowColor(), B_DARKEN_3_TINT));
		text += " (";
		text += B_TRANSLATE("already chosen");
		text += ")";
	}

	BFont font = be_plain_font;
	font_height	finfo;
	font.GetHeight(&finfo);
	owner->SetFont(&font);
	// TODO: the position is unnecessarily complicated, and not correct either
	owner->MovePenTo(frame.left + 8, frame.top
		+ (frame.Height() - (finfo.ascent + finfo.descent + finfo.leading)) / 2
		+ (finfo.ascent + finfo.descent) - 1);
	owner->DrawString(text.String());
}


// #pragma mark -


LanguageListView::LanguageListView(const char* name, list_view_type type)
	:
	BOutlineListView(name, type),
	fDeleteMessage(NULL),
	fDragMessage(NULL)
{
}


LanguageListView::~LanguageListView()
{
}


LanguageListItem*
LanguageListView::ItemForLanguageID(const char* id, int32* _index) const
{
	for (int32 index = 0; index < FullListCountItems(); index++) {
		LanguageListItem* item
			= static_cast<LanguageListItem*>(FullListItemAt(index));

		if (item->ID() == id) {
			if (_index != NULL)
				*_index = index;
			return item;
		}
	}

	return NULL;
}


LanguageListItem*
LanguageListView::ItemForLanguageCode(const char* code, int32* _index) const
{
	for (int32 index = 0; index < FullListCountItems(); index++) {
		LanguageListItem* item
			= static_cast<LanguageListItem*>(FullListItemAt(index));

		if (item->Code() == code) {
			if (_index != NULL)
				*_index = index;
			return item;
		}
	}

	return NULL;
}


void
LanguageListView::SetDeleteMessage(BMessage* message)
{
	delete fDeleteMessage;
	fDeleteMessage = message;
}


void
LanguageListView::SetDragMessage(BMessage* message)
{
	delete fDragMessage;
	fDragMessage = message;
}


void
LanguageListView::AttachedToWindow()
{
	BOutlineListView::AttachedToWindow();
	ScrollToSelection();
}


void
LanguageListView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && _AcceptsDragMessage(message)) {
		// Someone just dropped something on us
		BMessage dragMessage(*message);
		dragMessage.AddInt32("drop_index", fDropIndex);
		dragMessage.AddPointer("drop_target", this);

		Invoke(&dragMessage);
	} else
		BOutlineListView::MessageReceived(message);
}


bool
LanguageListView::InitiateDrag(BPoint point, int32 dragIndex,
	bool /*wasSelected*/)
{
	if (fDragMessage == NULL)
		return false;

	BListItem* item = FullListItemAt(CurrentSelection(0));
	if (item == NULL) {
		// workaround for a timing problem
		// TODO: this should support extending the selection
		item = ItemAt(dragIndex);
		Select(dragIndex);
	}
	if (item == NULL)
		return false;

	// create drag message
	BMessage message = *fDragMessage;
	message.AddPointer("listview", this);

	for (int32 i = 0;; i++) {
		int32 index = FullListCurrentSelection(i);
		if (index < 0)
			break;

		message.AddInt32("index", index);
	}

	// figure out drag rect

	BRect dragRect(0.0, 0.0, Bounds().Width(), -1.0);
	int32 numItems = 0;
	bool fade = false;

	// figure out, how many items fit into our bitmap
	for (int32 i = 0, index; message.FindInt32("index", i, &index) == B_OK;
			i++) {
		BListItem* item = FullListItemAt(index);
		if (item == NULL)
			break;

		dragRect.bottom += ceilf(item->Height()) + 1.0;
		numItems++;

		if (dragRect.Height() > MAX_DRAG_HEIGHT) {
			dragRect.bottom = MAX_DRAG_HEIGHT;
			fade = true;
			break;
		}
	}

	BBitmap* dragBitmap = new BBitmap(dragRect, B_RGB32, true);
	if (dragBitmap->IsValid()) {
		BView* view = new BView(dragBitmap->Bounds(), "helper", B_FOLLOW_NONE,
			B_WILL_DRAW);
		dragBitmap->AddChild(view);
		dragBitmap->Lock();
		BRect itemBounds(dragRect) ;
		itemBounds.bottom = 0.0;
		// let all selected items, that fit into our drag_bitmap, draw
		for (int32 i = 0; i < numItems; i++) {
			int32 index = message.FindInt32("index", i);
			LanguageListItem* item
				= static_cast<LanguageListItem*>(FullListItemAt(index));
			itemBounds.bottom = itemBounds.top + ceilf(item->Height());
			if (itemBounds.bottom > dragRect.bottom)
				itemBounds.bottom = dragRect.bottom;
			item->DrawItem(view, itemBounds);
			itemBounds.top = itemBounds.bottom + 1.0;
		}
		// make a black frame arround the edge
		view->SetHighColor(0, 0, 0, 255);
		view->StrokeRect(view->Bounds());
		view->Sync();

		uint8* bits = (uint8*)dragBitmap->Bits();
		int32 height = (int32)dragBitmap->Bounds().Height() + 1;
		int32 width = (int32)dragBitmap->Bounds().Width() + 1;
		int32 bpr = dragBitmap->BytesPerRow();

		if (fade) {
			for (int32 y = 0; y < height - ALPHA / 2; y++, bits += bpr) {
				uint8* line = bits + 3;
				for (uint8* end = line + 4 * width; line < end; line += 4)
					*line = ALPHA;
			}
			for (int32 y = height - ALPHA / 2; y < height;
				y++, bits += bpr) {
				uint8* line = bits + 3;
				for (uint8* end = line + 4 * width; line < end; line += 4)
					*line = (height - y) << 1;
			}
		} else {
			for (int32 y = 0; y < height; y++, bits += bpr) {
				uint8* line = bits + 3;
				for (uint8* end = line + 4 * width; line < end; line += 4)
					*line = ALPHA;
			}
		}
		dragBitmap->Unlock();
	} else {
		delete dragBitmap;
		dragBitmap = NULL;
	}

	if (dragBitmap != NULL)
		DragMessage(&message, dragBitmap, B_OP_ALPHA, BPoint(0.0, 0.0));
	else
		DragMessage(&message, dragRect.OffsetToCopy(point), this);

	return true;
}


void
LanguageListView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (dragMessage != NULL && _AcceptsDragMessage(dragMessage)) {
		switch (transit) {
			case B_ENTERED_VIEW:
			case B_INSIDE_VIEW:
			{
				// set drop target through virtual function
				// offset where by half of item height
				BRect r = ItemFrame(0);
				where.y += r.Height() / 2.0;

				int32 index = FullListIndexOf(where);
				if (index < 0)
					index = FullListCountItems();
				if (fDropIndex != index) {
					fDropIndex = index;
					if (fDropIndex >= 0) {
// TODO: find out what this was intended for (as it doesn't have any effect)
//						int32 count = FullListCountItems();
//						if (fDropIndex == count) {
//							BRect r;
//							if (FullListItemAt(count - 1)) {
//								r = ItemFrame(count - 1);
//								r.top = r.bottom;
//								r.bottom = r.top + 1.0;
//							} else {
//								r = Bounds();
//								r.bottom--;
//									// compensate for scrollbars moved slightly
//									// out of window
//							}
//						} else {
//							BRect r = ItemFrame(fDropIndex);
//							r.top--;
//							r.bottom = r.top + 1.0;
//						}
					}
				}
				break;
			}
		}
	} else
		BOutlineListView::MouseMoved(where, transit, dragMessage);
}


void
LanguageListView::KeyDown(const char* bytes, int32 numBytes)
{
	if (bytes[0] == B_DELETE && fDeleteMessage != NULL) {
		Invoke(fDeleteMessage);
		return;
	}

	BOutlineListView::KeyDown(bytes, numBytes);
}


bool
LanguageListView::_AcceptsDragMessage(const BMessage* message) const
{
	LanguageListView* sourceView = NULL;
	return message != NULL
		&& message->FindPointer("listview", (void**)&sourceView) == B_OK;
}
