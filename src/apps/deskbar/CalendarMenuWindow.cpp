/*
 * Copyright Karsten Heimrich, host.haiku@gmx.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "CalendarMenuWindow.h"


#include <Button.h>
#include <CalendarView.h>
#include <GridLayoutBuilder.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <GroupView.h>
#include <Screen.h>
#include <String.h>
#include <StringView.h>
#include <SpaceLayoutItem.h>


using BPrivate::BCalendarView;
using BPrivate::B_LOCAL_TIME;
using BPrivate::B_WEEK_START_SUNDAY;
using BPrivate::B_WEEK_START_MONDAY;


// #pragma mark -- FlatButton


class FlatButton : public BButton {
public:
					FlatButton(const BString& label, uint32 what)
						: BButton(label.String(), new BMessage(what)) {}
	virtual			~FlatButton() {}

	virtual void	Draw(BRect updateRect);
};


void
FlatButton::Draw(BRect updateRect)
{
	updateRect = Bounds();
	rgb_color highColor = HighColor();

	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(updateRect);

	font_height fh;
	GetFontHeight(&fh);

	const char* label = Label();
	const float stringWidth = StringWidth(label);
	const float x = (updateRect.right - stringWidth) / 2.0f;
	const float labelY = updateRect.top
		+ ((updateRect.Height() - fh.ascent - fh.descent) / 2.0f)
		+ fh.ascent + 1.0f;

	SetHighColor(highColor);
	DrawString(label, BPoint(x, labelY));

	if (IsFocus()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(updateRect);
	}
}


// #pragma mark -- CalendarMenuWindow


enum {
	kInvokationMessage,
	kMonthDownMessage,
	kMonthUpMessage,
	kYearDownMessage,
	kYearUpMessage
};


CalendarMenuWindow::CalendarMenuWindow(BPoint where, bool euroDate)
	: BWindow(BRect(0.0, 0.0, 100.0, 130.0), "", B_BORDERED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE |
		B_NOT_MINIMIZABLE | B_NOT_ZOOMABLE),
	  fYearLabel(NULL),
	  fMonthLabel(NULL),
	  fCalendarView(NULL),
	  fSuppressFirstClose(true)
{
	RemoveShortcut('H', B_COMMAND_KEY | B_CONTROL_KEY);
	AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));

	fYearLabel = new BStringView("year", "");
	fYearLabel->SetFontSize(10.0);

	fMonthLabel = new BStringView("month", "");
	fMonthLabel->SetFontSize(10.0);

	fCalendarView = new BCalendarView(Bounds(), "calendar",
		euroDate ? B_WEEK_START_MONDAY : B_WEEK_START_SUNDAY, B_FOLLOW_ALL);
	fCalendarView->SetInvocationMessage(new BMessage(kInvokationMessage));
	fCalendarView->SetFontSize(10.0);

	BGroupLayout* layout = new BGroupLayout(B_HORIZONTAL);
	SetLayout(layout);

	float width, height;
	fMonthLabel->GetPreferredSize(&width, &height);

	BGridLayout* gridLayout = BGridLayoutBuilder(5.0)
		.Add(_SetupButton("-", kMonthDownMessage, height), 0, 0)
		.Add(fMonthLabel, 1, 0)
		.Add(_SetupButton("+", kMonthUpMessage, height), 2, 0)
		.Add(BSpaceLayoutItem::CreateGlue(), 3, 0)
		.Add(_SetupButton("-", kYearDownMessage, height), 4, 0)
		.Add(fYearLabel, 5, 0)
		.Add(_SetupButton("+", kYearUpMessage, height), 6, 0)
		.SetInsets(5.0, 0.0, 5.0, 0.0);
	gridLayout->SetMinColumnWidth(1, be_plain_font->StringWidth("September"));

	BGroupView* groupView = new BGroupView(B_VERTICAL, 10.0);
	BView* view = BGroupLayoutBuilder(B_VERTICAL, 5.0)
		.Add(gridLayout->View())
		.Add(fCalendarView)
		.SetInsets(5.0, 5.0, 5.0, 5.0);
	groupView->AddChild(view);
	AddChild(groupView);

	MoveTo(where);
	_UpdateUi(BDate::CurrentDate(B_LOCAL_TIME));
}


CalendarMenuWindow::~CalendarMenuWindow()
{
}


void
CalendarMenuWindow::Show()
{
	BRect screen(BScreen().Frame());

	float right = screen.right;
	float bottom = screen.bottom;

	BRect rightTop(right / 2.0, screen.top, right, bottom / 2.0);
	BRect rightBottom(right / 2.0, bottom / 2.0, right + 1.0, bottom + 1.0);
	BRect leftBottom(screen.left, bottom / 2.0, right / 2.0, bottom + 1.0);

	BPoint where = Frame().LeftTop();
	BSize size = GetLayout()->PreferredSize();

	if (rightTop.Contains(where)) {
		where.x -= size.width;
	} else if (leftBottom.Contains(where)) {
		where.y -= (size.height + 4.0);
	} else if (rightBottom.Contains(where)) {
		where.x -= size.width;
		where.y -= (size.height + 4.0);
	}

	MoveTo(where);
	fCalendarView->MakeFocus(true);

	BWindow::Show();
}


void
CalendarMenuWindow::WindowActivated(bool active)
{
	if (active)
		return;

	if (mouse_mode() == 0) {
		if (!active)
			PostMessage(B_QUIT_REQUESTED);
	} else {
		if (fSuppressFirstClose && !active) {
			fSuppressFirstClose = false; return;
		}

		if (!fSuppressFirstClose && !active)
			PostMessage(B_QUIT_REQUESTED);
	}
}


void
CalendarMenuWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kInvokationMessage: {
			int32 day, month, year;
			message->FindInt32("day", &day);
			message->FindInt32("month", &month);
			message->FindInt32("year", &year);

			_UpdateUi(BDate(year, month, day));
		}	break;

		case kMonthDownMessage: {
		case kMonthUpMessage:
			BDate date = fCalendarView->Date();

			int32 day = date.Day();
			int32 year = date.Year();
			int32 month = date.Month();

			month += (kMonthDownMessage == message->what) ? -1 : 1;
			if (month < 1) {
				year--;
				month = 12;
			} else if (month > 12) {
				year++;
				month = 1;
			}
			date.SetDate(year, month, day);

			if (day > date.DaysInMonth())
				day = date.DaysInMonth();

			_UpdateUi(BDate(year, month, day));
		}	break;

		case kYearDownMessage: {
		case kYearUpMessage:
			BDate date = fCalendarView->Date();
			int32 i = kYearDownMessage == message->what ? -1 : 1;
			_UpdateUi(BDate(date.Year() + i, date.Month(), date.Day()));
		}	break;

		default: {
			BWindow::MessageReceived(message);
		}	break;
	}
}


void
CalendarMenuWindow::_UpdateUi(const BDate& date)
{
	if (!date.IsValid())
		return;

	fCalendarView->SetDate(date);

	BString text;
	text << date.Year();
	fYearLabel->SetText(text.String());

	fMonthLabel->SetText(date.LongMonthName(date.Month()).String());
}


BButton*
CalendarMenuWindow::_SetupButton(const char* label, uint32 what, float height)
{
	FlatButton* button = new FlatButton(label, what);
	button->SetExplicitMinSize(BSize(height, height));
	button->SetFontSize(10.0);

	return button;
}
