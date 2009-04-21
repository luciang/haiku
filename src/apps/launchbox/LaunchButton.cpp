/*
 * Copyright 2006 - 2009, Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "LaunchButton.h"

#include <malloc.h> // string.h is not enough on Haiku?!?
#include <stdio.h>
#include <string.h>

#include <AppDefs.h>
#include <AppFileInfo.h>
#include <Bitmap.h>
#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Region.h>
#include <Roster.h>
#include <Window.h>

//#include "BubbleHelper.h"
#include "PadView.h"


static const float kDragStartDist = 10.0;
static const float kDragBitmapAlphaScale = 0.6;
//static const char* kEmptyHelpString = "You can drag an icon here.";


bigtime_t
LaunchButton::sClickSpeed = 0;

bool
LaunchButton::sIgnoreDoubleClick = true;


LaunchButton::LaunchButton(const char* name, uint32 id, const char* label,
		BMessage* message, BHandler* target)
	: IconButton(name, id, label, message, target),
	  fRef(NULL),
	  fAppSig(NULL),
	  fDescription(""),
	  fAnticipatingDrop(false),
	  fLastClickTime(0),
	  fIconSize(DEFAULT_ICON_SIZE)
{
	if (sClickSpeed == 0 || get_click_speed(&sClickSpeed) != B_OK)
		sClickSpeed = 500000;
}


LaunchButton::~LaunchButton()
{
	delete fRef;
	free(fAppSig);
}


void
LaunchButton::AttachedToWindow()
{
	IconButton::AttachedToWindow();
	_UpdateToolTip();
}


void
LaunchButton::DetachedFromWindow()
{
//	BubbleHelper::Default()->SetHelp(this, NULL);
}


void
LaunchButton::Draw(BRect updateRect)
{
	if (fAnticipatingDrop) {
		rgb_color color = fRef ? ui_color(B_KEYBOARD_NAVIGATION_COLOR)
							   : (rgb_color){ 0, 130, 60, 255 };
		SetHighColor(color);
		// limit clipping region to exclude the blue rect we just drew
		BRect r(Bounds());
		StrokeRect(r);
		r.InsetBy(1.0, 1.0);
		BRegion region(r);
		ConstrainClippingRegion(&region);
	}
	if (IsValid()) {
		IconButton::Draw(updateRect);
	} else {
		rgb_color background = LowColor();
		rgb_color lightShadow = tint_color(background,
			(B_NO_TINT + B_DARKEN_1_TINT) / 2.0);
		rgb_color shadow = tint_color(background, B_DARKEN_1_TINT);
		rgb_color light = tint_color(background, B_LIGHTEN_1_TINT);
		BRect r(Bounds());
		_DrawFrame(r, shadow, light, lightShadow, lightShadow);
		r.InsetBy(2.0, 2.0);
		SetHighColor(lightShadow);
		FillRect(r);
	}
}


void
LaunchButton::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED: {
			entry_ref ref; 
			if (message->FindRef("refs", &ref) == B_OK) {
				if (fRef) {
					if (ref != *fRef) {
						BEntry entry(fRef, true);
						if (entry.IsDirectory()) {
							message->PrintToStream();
							// copy stuff into the directory
						} else {
							message->what = B_REFS_RECEIVED;
							team_id team;
							if (fAppSig)
								team = be_roster->TeamFor(fAppSig);
							else
								team = be_roster->TeamFor(fRef);
							if (team < 0) {
								if (fAppSig)
									be_roster->Launch(fAppSig, message, &team);
								else
									be_roster->Launch(fRef, message, &team);
							} else {
								app_info appInfo;
								if (team >= 0
									&& be_roster->GetRunningAppInfo(team,
										&appInfo) == B_OK) {
									BMessenger messenger(appInfo.signature,
										team);
									if (messenger.IsValid())
										messenger.SendMessage(message);
								}
							}
						}
					}
				} else {
					SetTo(&ref);
				}
			}
			break;
		}
		case B_PASTE:
		case B_MODIFIERS_CHANGED:
		default:
			IconButton::MessageReceived(message);
			break;
	}
}


void
LaunchButton::MouseDown(BPoint where)
{
	bigtime_t now = system_time();
	bool callInherited = true;
	if (sIgnoreDoubleClick && now - fLastClickTime < sClickSpeed)
		callInherited = false;
	fLastClickTime = now;
	if (BMessage* message = Window()->CurrentMessage()) {
		uint32 buttons;
		message->FindInt32("buttons", (int32*)&buttons);
		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			if (PadView* parent = dynamic_cast<PadView*>(Parent())) {
				parent->DisplayMenu(ConvertToParent(where), this);
				_ClearFlags(STATE_INSIDE);
				callInherited = false;
			}
		} else {
			fDragStart = where;
		}
	}
	if (callInherited)
		IconButton::MouseDown(where);
}


void
LaunchButton::MouseUp(BPoint where)
{
	if (fAnticipatingDrop) {
		fAnticipatingDrop = false;
		Invalidate();
	}
	IconButton::MouseUp(where);
}


void
LaunchButton::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if ((dragMessage && (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW))
		&& ((dragMessage->what == B_SIMPLE_DATA
			|| dragMessage->what == B_REFS_RECEIVED) || fRef)) {
		if (!fAnticipatingDrop) {
			fAnticipatingDrop = true;
			Invalidate();
		}
	}
	if (!dragMessage || (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW)) {
		if (fAnticipatingDrop) {
			fAnticipatingDrop = false;
			Invalidate();
		}
	}
	// see if we should create a drag message
	if (_HasFlags(STATE_TRACKING) && fRef) {
		BPoint diff = where - fDragStart;
		float dist = sqrtf(diff.x * diff.x + diff.y * diff.y);
		if (dist >= kDragStartDist) {
			// stop tracking
			_ClearFlags(STATE_PRESSED | STATE_TRACKING | STATE_INSIDE);
			// create drag bitmap and message
			if (BBitmap* bitmap = Bitmap()) {
				if (bitmap->ColorSpace() == B_RGB32) {
					// make semitransparent
					uint8* bits = (uint8*)bitmap->Bits();
					uint32 width = bitmap->Bounds().IntegerWidth() + 1;
					uint32 height = bitmap->Bounds().IntegerHeight() + 1;
					uint32 bpr = bitmap->BytesPerRow();
					for (uint32 y = 0; y < height; y++) {
						uint8* bitsHandle = bits;
						for (uint32 x = 0; x < width; x++) {
							bitsHandle[3] = uint8(bitsHandle[3]
								* kDragBitmapAlphaScale);
							bitsHandle += 4;
						}
						bits += bpr;
					}
				}
				BMessage message(B_SIMPLE_DATA);
				message.AddPointer("button", this);
				message.AddRef("refs", fRef);
				DragMessage(&message, bitmap, B_OP_ALPHA, fDragStart);
			}
		}
	}
	IconButton::MouseMoved(where, transit, dragMessage);
}


BSize
LaunchButton::MinSize()
{
	return PreferredSize();
}


BSize
LaunchButton::PreferredSize()
{
	float minWidth = fIconSize;
	float minHeight = fIconSize;

	float hPadding = max_c(6.0, ceilf(minHeight / 3.0));
	float vPadding = max_c(6.0, ceilf(minWidth / 3.0));

	if (fLabel.CountChars() > 0) {
		font_height fh;
		GetFontHeight(&fh);
		minHeight += ceilf(fh.ascent + fh.descent) + vPadding;
		minWidth += StringWidth(fLabel.String()) + vPadding;
	}

	return BSize(minWidth + hPadding, minHeight + vPadding);
}


BSize
LaunchButton::MaxSize()
{
	return PreferredSize();
}


// #pragma mark -


void
LaunchButton::SetTo(const entry_ref* ref)
{
	free(fAppSig);
	fAppSig = NULL;

	delete fRef;
	if (ref) {
		fRef = new entry_ref(*ref);
		// follow links
		BEntry entry(fRef, true);
		entry.GetRef(fRef);

		_UpdateIcon(fRef);
		// see if this is an application
		BFile file(ref, B_READ_ONLY);
		BAppFileInfo info;
		if (info.SetTo(&file) == B_OK) {
			char mimeSig[B_MIME_TYPE_LENGTH];
			if (info.GetSignature(mimeSig) == B_OK) {
				SetTo(mimeSig, false);
			} else {
				fprintf(stderr, "no MIME signature for '%s'\n", fRef->name);
			}
		} else {
			fprintf(stderr, "no BAppFileInfo for '%s'\n", fRef->name);
		}
	} else {
		fRef = NULL;
		ClearIcon();
	}
	_UpdateToolTip();
}


entry_ref*
LaunchButton::Ref() const
{
	return fRef;
}


void
LaunchButton::SetTo(const char* appSig, bool updateIcon)
{
	if (appSig) {
		free(fAppSig);
		fAppSig = strdup(appSig);
		if (updateIcon) {
			entry_ref ref;
			if (be_roster->FindApp(fAppSig, &ref) == B_OK)
				SetTo(&ref);
		}
	}
	_UpdateToolTip();
}


void
LaunchButton::SetDescription(const char* text)
{
	fDescription.SetTo(text);
	_UpdateToolTip();
}


void
LaunchButton::SetIconSize(uint32 size)
{
	if (fIconSize == size)
		return;

	fIconSize = size;
	_UpdateIcon(fRef);

	InvalidateLayout();
	Invalidate();
}


void
LaunchButton::SetIgnoreDoubleClick(bool refuse)
{
	sIgnoreDoubleClick = refuse;
}


// #pragma mark -


void
LaunchButton::_UpdateToolTip()
{
	if (fRef) {
		BString helper(fRef->name);
		if (fDescription.CountChars() > 0) {
			helper << "\n\n" << fDescription.String();
		} else {
			BFile file(fRef, B_READ_ONLY);
			BAppFileInfo appFileInfo;
			version_info info;
			if (appFileInfo.SetTo(&file) == B_OK
				&& appFileInfo.GetVersionInfo(&info,
					B_APP_VERSION_KIND) == B_OK
				&& strlen(info.short_info) > 0) {
				helper << "\n\n" << info.short_info;
			}
		}
//		BubbleHelper::Default()->SetHelp(this, helper.String());
	} else {
//		BubbleHelper::Default()->SetHelp(this, kEmptyHelpString);
	}
}


void
LaunchButton::_UpdateIcon(const entry_ref* ref)
{
	BBitmap* icon = new BBitmap(BRect(0.0, 0.0, fIconSize - 1,
		fIconSize - 1), B_RGBA32);
	// NOTE: passing an invalid/unknown icon_size argument will cause
	// the BNodeInfo to ignore it and just use the bitmap bounds.
	if (BNodeInfo::GetTrackerIcon(ref, icon, (icon_size)fIconSize) == B_OK)
		SetIcon(icon);

	delete icon;
}
