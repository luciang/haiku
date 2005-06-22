/*
 * Copyright 2001-2005, Haiku.
 * Copyright 2002, Thomas Kurschel.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Thomas Kurschel
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "MonitorView.h"
#include "Constants.h"

#include <Roster.h>
#include <Screen.h>


MonitorView::MonitorView(BRect rect, char *name, int32 width, int32 height)
	: BView(rect, name, B_FOLLOW_ALL, B_WILL_DRAW),
	fWidth(width),
	fHeight(height)
{
	BScreen screen(B_MAIN_SCREEN_ID);
	fDesktopColor = screen.DesktopColor(current_workspace());
}


MonitorView::~MonitorView()
{
}


void
MonitorView::AttachedToWindow()
{
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
MonitorView::MouseDown(BPoint point)
{
	be_roster->Launch("application/x-vnd.Be-BACK");
}


BRect
MonitorView::MonitorBounds()
{
	float maxWidth, maxHeight;
	float picWidth, picHeight;

	maxWidth = Bounds().Width();
	maxHeight = Bounds().Height();

	picWidth = maxWidth * fWidth / 1600;
	picHeight = maxHeight * fHeight / 1200;

	if (picWidth > maxWidth) {
		picHeight = picHeight * maxWidth / picWidth;
		picWidth = maxWidth;
	}

	if (picHeight > maxHeight) {
		picWidth = picWidth * maxHeight / picHeight;
		picHeight = maxHeight;
	}

	return BRect((maxWidth - picWidth) / 2, (maxHeight - picHeight) / 2, 
		(maxWidth + picWidth) / 2, (maxHeight + picHeight) / 2);
}


void
MonitorView::Draw(BRect updateRect)
{
	rgb_color darkColor = {160, 160, 160, 255};
	rgb_color blackColor = {0, 0, 0, 255};
	rgb_color redColor = {228, 0, 0, 255};
	BRect outerRect = MonitorBounds();

	SetDrawingMode(B_OP_OVER);

	// frame & background

	SetHighColor(darkColor);
	FillRoundRect(outerRect, 3.0, 3.0);

	SetHighColor(blackColor);
	StrokeRoundRect(outerRect, 3.0, 3.0);

	SetHighColor(fDesktopColor);

	BRect innerRect(outerRect.InsetByCopy(4, 4));
	FillRoundRect(innerRect, 2.0, 2.0);

	SetHighColor(blackColor);
	StrokeRoundRect(innerRect, 2.0, 2.0);

	SetDrawingMode(B_OP_COPY);

	// power light

	SetHighColor(redColor);
	BPoint powerPos(outerRect.left + 5, outerRect.bottom - 2);
	StrokeLine(powerPos, BPoint(powerPos.x + 2, powerPos.y));
}


void
MonitorView::SetResolution(int32 width, int32 height)
{
	if (fWidth == width && fHeight == height)
		return;

	fWidth = width;
	fHeight = height;

	Invalidate();
}


void
MonitorView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case UPDATE_DESKTOP_MSG:
		{
			int32 width, height;
			if (message->FindInt32("width", &width) == B_OK
				&& message->FindInt32("height", &height) == B_OK)
				SetResolution(width, height);
			break;
		}

		case UPDATE_DESKTOP_COLOR_MSG:
		{
			BScreen screen(Window());
			rgb_color color = screen.DesktopColor(current_workspace());
			if (color != fDesktopColor) {
				fDesktopColor = color;
				Invalidate();
			}
		}

		default:
			BView::MessageReceived(message);	
			break;
	}
}
