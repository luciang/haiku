/*
 * Copyright 2010, Haiku, Inc. All Rights Reserved.
 * Copyright 2009, Pier Luigi Fiorini.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 */

#include <Alert.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <Window.h>
#include <CheckBox.h>
#include <TextControl.h>
#include <Path.h>
#include <Notification.h>
#include <notification/Notifications.h>
#include <notification/NotificationReceived.h>

#include <ColumnListView.h>
#include <ColumnTypes.h>

#include "NotificationsView.h"

#define _T(str) (str)

const float kEdgePadding = 5.0;
const float kCLVTitlePadding = 8.0;

const int32 kApplicationSelected = '_ASL';
const int32 kNotificationSelected = '_NSL';

const int32 kCLVDeleteRow = 'av02';

// Applications column indexes
const int32 kAppIndex = 0;
const int32 kAppEnabledIndex = 1;

// Notifications column indexes
const int32 kTitleIndex = 0;
const int32 kDateIndex = 1;
const int32 kTypeIndex = 2;
const int32 kAllowIndex = 3;

const int32 kSettingChanged = '_STC';


NotificationsView::NotificationsView()
	:
	BView("apps", B_WILL_DRAW)
{
	BRect rect(0, 0, 100, 100);

	// Search application field
	fSearch = new BTextControl(_T("Search:"), NULL,
		new BMessage(kSettingChanged));

	// Applications list
	fApplications = new BColumnListView(rect, _T("Applications"),
		0, B_WILL_DRAW, B_FANCY_BORDER, true);
	fApplications->SetSelectionMode(B_SINGLE_SELECTION_LIST);

	fAppCol = new BStringColumn(_T("Application"), 200,
		be_plain_font->StringWidth(_T("Application")) + (kCLVTitlePadding * 2),
		rect.Width(), B_TRUNCATE_END, B_ALIGN_LEFT);
	fApplications->AddColumn(fAppCol, kAppIndex);

	fAppEnabledCol = new BStringColumn(_T("Enabled"), 10,
		be_plain_font->StringWidth(_T("Enabled")) + (kCLVTitlePadding * 2),
		rect.Width(), B_TRUNCATE_END, B_ALIGN_LEFT);
	fApplications->AddColumn(fAppEnabledCol, kAppEnabledIndex);

	// Notifications list
	fNotifications = new BColumnListView(rect, _T("Notifications"),
		0, B_WILL_DRAW, B_FANCY_BORDER, true);
	fNotifications->SetSelectionMode(B_SINGLE_SELECTION_LIST);

	fTitleCol = new BStringColumn(_T("Title"), 100,
		be_plain_font->StringWidth(_T("Title")) + (kCLVTitlePadding * 2),
		rect.Width(), B_TRUNCATE_END, B_ALIGN_LEFT);
	fNotifications->AddColumn(fTitleCol, kTitleIndex);

	fDateCol = new BDateColumn(_T("Last Received"), 100,
		be_plain_font->StringWidth(_T("Last Received")) + (kCLVTitlePadding * 2),
		rect.Width(), B_ALIGN_LEFT);
	fNotifications->AddColumn(fDateCol, kDateIndex);

	fTypeCol = new BStringColumn(_T("Type"), 100,
		be_plain_font->StringWidth(_T("Type")) + (kCLVTitlePadding * 2),
		rect.Width(), B_TRUNCATE_END, B_ALIGN_LEFT);
	fNotifications->AddColumn(fTypeCol, kTypeIndex);

	fAllowCol = new BStringColumn(_T("Allowed"), 100,
		be_plain_font->StringWidth(_T("Allowed")) + (kCLVTitlePadding * 2),
		rect.Width(), B_TRUNCATE_END, B_ALIGN_LEFT);
	fNotifications->AddColumn(fAllowCol, kAllowIndex);

	// Load the applications list
	_LoadAppUsage();
	_PopulateApplications();

	// Calculate inset
	float inset = ceilf(be_plain_font->Size() * 0.7f);

	// Set layout
	SetLayout(new BGroupLayout(B_VERTICAL));

	// Add views
	AddChild(BGroupLayoutBuilder(B_VERTICAL, inset)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fSearch)
		.End()
		.Add(fApplications)
		.Add(fNotifications)
	);
}


void
NotificationsView::AttachedToWindow()
{
	fApplications->SetTarget(this);
	fApplications->SetInvocationMessage(new BMessage(kApplicationSelected));

	fNotifications->SetTarget(this);
	fNotifications->SetInvocationMessage(new BMessage(kNotificationSelected));

#if 0
	fNotifications->AddFilter(new BMessageFilter(B_ANY_DELIVERY,
		B_ANY_SOURCE, B_KEY_DOWN, CatchDelete));
#endif
}


void
NotificationsView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kApplicationSelected: {
				BRow *row = fApplications->CurrentSelection();
				if (row == NULL)
					return;
				BStringField* appname =
					dynamic_cast<BStringField*>(row->GetField(kAppIndex));

				appusage_t::iterator it = fAppFilters.find(appname->String());
				if (it != fAppFilters.end())
					_Populate(it->second);
			} break;
		case kNotificationSelected:
			break;
		default:
			BView::MessageReceived(msg);
			break;
	}
}


status_t
NotificationsView::_LoadAppUsage()
{
	BPath path;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;

	path.Append(kSettingsDirectory);

	if (create_directory(path.Path(), 0755) != B_OK) {
		BAlert* alert = new BAlert("",
			_T("There was a problem saving the preferences.\n"
				"It's possible you don't have write access to the "
				"settings directory."), "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		(void)alert->Go();
		return B_ERROR;
	}

	path.Append(kFiltersSettings);

	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return B_ERROR;

	type_code type;
	int32 count = 0;

	if (settings.GetInfo("app_usage", &type, &count) != B_OK)
		return B_ERROR;

	// Clean filters
	appusage_t::iterator auIt;
	for (auIt = fAppFilters.begin(); auIt != fAppFilters.end(); auIt++)
		delete auIt->second;
	fAppFilters.clear();

	// Add new filters
	for (int32 i = 0; i < count; i++) {
		AppUsage* app = new AppUsage();
		settings.FindFlat("app_usage", i, app);
		fAppFilters[app->Name()] = app;
	}

	return B_OK;
}


void
NotificationsView::_PopulateApplications()
{
	appusage_t::iterator it;

	fApplications->Clear();

	for (it = fAppFilters.begin(); it != fAppFilters.end(); ++it) {
		BRow* row = new BRow();
		row->SetField(new BStringField(it->first.String()), kAppIndex);
		fApplications->AddRow(row);
	}
}


void
NotificationsView::_Populate(AppUsage* usage)
{
	// Sanity check
	if (!usage)
		return;

	int32 size = usage->Notifications();

	if (usage->Allowed() == false)
		fBlockAll->SetValue(B_CONTROL_ON);

	fNotifications->Clear();

	for (int32 i = 0; i < size; i++) {
		NotificationReceived* notification = usage->NotificationAt(i);
		time_t updated = notification->LastReceived();
		const char* allow = notification->Allowed() ? _T("Yes") : _T("No");
		const char* type = "";

		switch (notification->Type()) {
			case B_INFORMATION_NOTIFICATION:
				type = _T("Information");
				break;
			case B_IMPORTANT_NOTIFICATION:
				type = _T("Important");
				break;
			case B_ERROR_NOTIFICATION:
				type = _T("Error");
				break;
			case B_PROGRESS_NOTIFICATION:
				type = _T("Progress");
				break;
			default:
				type = _T("Unknown");
		}

		BRow* row = new BRow();
		row->SetField(new BStringField(notification->Title()), kTitleIndex);
		row->SetField(new BDateField(&updated), kDateIndex);
		row->SetField(new BStringField(type), kTypeIndex);
		row->SetField(new BStringField(allow), kAllowIndex);

		fNotifications->AddRow(row);
	}
}
