/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/


#include "TimeView.h"

#include <string.h>

#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <Window.h>

#include "CalendarMenuWindow.h"


const char* kShortDateFormat = "%m/%d/%y";
const char* kShortEuroDateFormat = "%d/%m/%y";
const char* kLongDateFormat = "%a, %B %d, %Y";
const char* kLongEuroDateFormat = "%a, %d %B, %Y";

static const char*  const kMinString = "99:99 AM";
static const float kHMargin = 2.0;


enum {
	kShowClock,
	kChangeClock,
	kHide,
	kLongClick,
	kShowCalendar
};


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "TimeView"

TTimeView::TTimeView(float maxWidth, float height, bool showSeconds,
	bool milTime, bool fullDate, bool euroDate, bool)
	:
	BView(BRect(-100,-100,-90,-90), "_deskbar_tv_",
	B_FOLLOW_RIGHT | B_FOLLOW_TOP,
	B_WILL_DRAW | B_PULSE_NEEDED | B_FRAME_EVENTS),
	fParent(NULL),
	fShowInterval(true), // ToDo: defaulting this to true until UI is in place
	fShowSeconds(showSeconds),
	fMilTime(milTime),
	fFullDate(fullDate),
	fCanShowFullDate(false),
	fEuroDate(euroDate),
	fMaxWidth(maxWidth),
	fHeight(height),
	fOrientation(true),
	fLongClickMessageRunner(NULL)
{
	fShowingDate = false;
	fTime = fLastTime = time(NULL);
	fSeconds = fMinute = fHour = 0;
	fLastTimeStr[0] = 0;
	fLastDateStr[0] = 0;
	fNeedToUpdate = true;
}


#ifdef AS_REPLICANT
TTimeView::TTimeView(BMessage* data)
	: BView(data)
{
	fTime = fLastTime = time(NULL);
	data->FindBool("seconds", &fShowSeconds);
	data->FindBool("miltime", &fMilTime);
	data->FindBool("fulldate", &fFullDate);
	data->FindBool("eurodate", &fEuroDate);
	data->FindBool("interval", &fInterval);
	fShowingDate = false;
}
#endif


TTimeView::~TTimeView()
{
	StopLongClickNotifier();
}


#ifdef AS_REPLICANT
BArchivable*
TTimeView::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "TTimeView"))
		return NULL;

	return new TTimeView(data);
}


status_t
TTimeView::Archive(BMessage* data, bool deep) const
{
	BView::Archive(data, deep);
	data->AddBool("seconds", fShowSeconds);
	data->AddBool("miltime", fMilTime);
	data->AddBool("fulldate", fFullDate);
	data->AddBool("eurodate", fEuroDate);
	data->AddBool("interval", fInterval);
	data->AddInt32("deskbar:private_align", B_ALIGN_RIGHT);

	return B_OK;
}
#endif


void
TTimeView::AttachedToWindow()
{
	fTime = time(NULL);

	SetFont(be_plain_font);
	if (Parent()) {
		fParent = Parent();
		SetViewColor(Parent()->ViewColor());
	} else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	ResizeToPreferred();
	CalculateTextPlacement();
}


void
TTimeView::GetPreferredSize(float* width, float* height)
{
	*height = fHeight;

	GetCurrentTime();
	GetCurrentDate();

	// TODO: SetOrientation never gets called, fix that
	// When in vertical mode, we want to limit the width so that it can't
	// overlap the bevels in the parent view.
	if (ShowingDate())
		*width = fOrientation ?
			min_c(fMaxWidth - kHMargin, kHMargin + StringWidth(fDateStr))
			: kHMargin + StringWidth(fDateStr);
	else {
		*width = fOrientation ?
			min_c(fMaxWidth - kHMargin, kHMargin + StringWidth(fTimeStr))
			: kHMargin + StringWidth(fTimeStr);
	}
}


void
TTimeView::ResizeToPreferred()
{
	float width, height;
	float oldWidth = Bounds().Width(), oldHeight = Bounds().Height();

	GetPreferredSize(&width, &height);
	if (height != oldHeight || width != oldWidth) {
		ResizeTo(width, height);
		MoveBy(oldWidth - width, 0);
		fNeedToUpdate = true;
	}
}


void
TTimeView::FrameMoved(BPoint)
{
	Update();
}


void
TTimeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kFullDate:
			ShowFullDate(!ShowingFullDate());
			break;

		case kShowSeconds:
			ShowSeconds(!ShowingSeconds());
			break;

		case kMilTime:
			ShowMilTime(!ShowingMilTime());
			break;

		case kEuroDate:
			ShowEuroDate(!ShowingEuroDate());
			break;

		case kChangeClock:
			// launch the time prefs app
			be_roster->Launch("application/x-vnd.Haiku-Time");
			break;

		case 'time':
			Window()->PostMessage(message, Parent());
			break;

		case kLongClick:
		{
			StopLongClickNotifier();
			BPoint where;
			message->FindPoint("where", &where);
			ShowCalendar(where);
			break;
		}

		case kShowCalendar:
		{
			BRect bounds(Bounds());
			BPoint center(bounds.LeftTop());
			center += BPoint(bounds.Width() / 2, bounds.Height() / 2);
			ShowCalendar(center);
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


void
TTimeView::ShowCalendar(BPoint where)
{
	if (fCalendarWindow.IsValid()) {
		// If the calendar is already shown, just activate it
		BMessage activate(B_SET_PROPERTY);
		activate.AddSpecifier("Active");
		activate.AddBool("data", true);

		if (fCalendarWindow.SendMessage(&activate) == B_OK)
			return;
	}

	where.y = Bounds().bottom + 4.0;
	ConvertToScreen(&where);

	if (where.y >= BScreen().Frame().bottom)
		where.y -= (Bounds().Height() + 4.0);

	CalendarMenuWindow* window = new CalendarMenuWindow(where, fEuroDate);
	fCalendarWindow = BMessenger(window);

	window->Show();
}


void
TTimeView::StartLongClickNotifier(BPoint where)
{
	StopLongClickNotifier();

	BMessage longClickMessage(kLongClick);
	longClickMessage.AddPoint("where", where);

	bigtime_t longClickThreshold;
	get_click_speed(&longClickThreshold);
		// use the doubleClickSpeed as a threshold

	fLongClickMessageRunner = new BMessageRunner(BMessenger(this),
		&longClickMessage, longClickThreshold, 1);
}


void
TTimeView::StopLongClickNotifier()
{
	delete fLongClickMessageRunner;
	fLongClickMessageRunner = NULL;
}


void
TTimeView::GetCurrentTime()
{
	char tmp[64];
	tm time = *localtime(&fTime);

	if (fMilTime) {
		strftime(tmp, 64, fShowSeconds ? "%H:%M:%S" : "%H:%M", &time);
	} else {
		if (fShowInterval)
			strftime(tmp, 64, fShowSeconds ? "%I:%M:%S %p" : "%I:%M %p", &time);
		else
			strftime(tmp, 64, fShowSeconds ? "%I:%M:%S" : "%I:%M", &time);
	}

	//	remove leading 0 from time when hour is less than 10
	const char* str = tmp;
	if (str[0] == '0')
		str++;

	strcpy(fTimeStr, str);

	fSeconds = time.tm_sec;
	fMinute = time.tm_min;
	fHour = time.tm_hour;
}


void
TTimeView::GetCurrentDate()
{
	char tmp[64];
	tm time = *localtime(&fTime);

	if (fFullDate && CanShowFullDate())
		strftime(tmp, 64, fEuroDate ? kLongEuroDateFormat : kLongDateFormat,
			&time);
	else
		strftime(tmp, 64, fEuroDate ? kShortEuroDateFormat : kShortDateFormat,
			&time);

	//	remove leading 0 from date when month is less than 10 (MM/DD/YY)
	//  or remove leading 0 from date when day is less than 10 (DD/MM/YY)
	const char* str = tmp;
	if (str[0] == '0')
		str++;

	strcpy(fDateStr, str);
}


void
TTimeView::Draw(BRect /*updateRect*/)
{
	PushState();

	SetHighColor(ViewColor());
	SetLowColor(ViewColor());
	FillRect(Bounds());
	SetHighColor(0, 0, 0, 255);

	if (fShowingDate)
		DrawString(fDateStr, fDateLocation);
	else
		DrawString(fTimeStr, fTimeLocation);

	PopState();
}


void
TTimeView::MouseDown(BPoint point)
{
	uint32 buttons;

	Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	if (buttons == B_SECONDARY_MOUSE_BUTTON) {
		ShowClockOptions(ConvertToScreen(point));
		return;
	} else if (buttons == B_PRIMARY_MOUSE_BUTTON) {
		StartLongClickNotifier(point);
	}

	//	flip to/from showing date or time
	fShowingDate = !fShowingDate;
	if (fShowingDate)
		fLastTime = time(NULL);

	// invalidate last time/date strings and call the pulse
	// method directly to change the display instantly
	fLastDateStr[0] = '\0';
	fLastTimeStr[0] = '\0';
	Pulse();
}


void
TTimeView::MouseUp(BPoint point)
{
	StopLongClickNotifier();
}


void
TTimeView::Pulse()
{
	time_t curTime = time(NULL);
	tm	ct = *localtime(&curTime);
	fTime = curTime;

	GetCurrentTime();
	GetCurrentDate();
	if ((!fShowingDate && strcmp(fTimeStr, fLastTimeStr) != 0)
		|| 	(fShowingDate && strcmp(fDateStr, fLastDateStr) != 0)) {
		// Update bounds when the size of the strings has changed
		// For dates, Update() could be called two times in a row,
		// but that should only happen very rarely
		if ((!fShowingDate && fLastTimeStr[1] != fTimeStr[1]
				&& (fLastTimeStr[1] == ':' || fTimeStr[1] == ':'))
			|| (fShowingDate && strlen(fDateStr) != strlen(fLastDateStr))
			|| !fLastTimeStr[0])
			Update();

		strcpy(fLastTimeStr, fTimeStr);
		strcpy(fLastDateStr, fDateStr);
		fNeedToUpdate = true;
	}

	if (fShowingDate && (fLastTime + 5 <= time(NULL))) {
		fShowingDate = false;
		Update();	// Needs to happen since size can change here
	}

	if (fNeedToUpdate) {
		fSeconds = ct.tm_sec;
		fMinute = ct.tm_min;
		fHour = ct.tm_hour;
		fInterval = ct.tm_hour >= 12;

		Draw(Bounds());
		fNeedToUpdate = false;
	}
}


void
TTimeView::ShowSeconds(bool on)
{
	fShowSeconds = on;
	Update();
}


void
TTimeView::ShowMilTime(bool on)
{
	fMilTime = on;
	Update();
}


void
TTimeView::ShowDate(bool on)
{
	fShowingDate = on;
	Update();
}


void
TTimeView::ShowFullDate(bool on)
{
	fFullDate = on;
	Update();
}


void
TTimeView::ShowEuroDate(bool on)
{
	fEuroDate = on;
	Update();
}


void
TTimeView::AllowFullDate(bool allow)
{
	fCanShowFullDate = allow;

	if (allow != ShowingFullDate())
		Update();
}


void
TTimeView::Update()
{
	GetCurrentTime();
	GetCurrentDate();

	ResizeToPreferred();
	CalculateTextPlacement();

	if (fParent) {
		BMessage reformat('Trfm');
		fParent->MessageReceived(&reformat);
			// time string format realign
		fParent->Invalidate();
	}
}


void
TTimeView::SetOrientation(bool o)
{
	fOrientation = o;
	CalculateTextPlacement();
	Invalidate();
}


void
TTimeView::CalculateTextPlacement()
{
	BRect bounds(Bounds());

	fDateLocation.x = 0.0;
	fTimeLocation.x = 0.0;

	BFont font;
	GetFont(&font);
	const char* stringArray[1];
	stringArray[0] = fTimeStr;
	BRect rectArray[1];
	escapement_delta delta = { 0.0, 0.0 };
	font.GetBoundingBoxesForStrings(stringArray, 1, B_SCREEN_METRIC, &delta,
		rectArray);

	fTimeLocation.y = fDateLocation.y = ceilf((bounds.Height() -
		rectArray[0].Height() + 1.0) / 2.0 - rectArray[0].top);
}


void
TTimeView::ShowClockOptions(BPoint point)
{
	BPopUpMenu* menu = new BPopUpMenu("", false, false);
	menu->SetFont(be_plain_font);
	BMenuItem* item;

	item = new BMenuItem(B_TRANSLATE("Change time" B_UTF8_ELLIPSIS),
		new BMessage(kChangeClock));
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Hide time"), new BMessage('time'));
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Show calendar" B_UTF8_ELLIPSIS),
		new BMessage(kShowCalendar));
	menu->AddItem(item);

	menu->SetTargetForItems(this);
	// Changed to accept screen coord system point;
	// not constrained to this view now
	menu->Go(point, true, true, BRect(point.x - 4, point.y - 4,
		point.x + 4, point.y +4), true);
}

