/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "GraphicalUserInterface.h"

#include <Alert.h>

#include "TeamWindow.h"
#include "Tracing.h"


GraphicalUserInterface::GraphicalUserInterface()
	:
	fTeamWindow(NULL)
{
}


GraphicalUserInterface::~GraphicalUserInterface()
{
}


status_t
GraphicalUserInterface::Init(Team* team, UserInterfaceListener* listener)
{
	try {
		fTeamWindow = TeamWindow::Create(team, listener);
	} catch (...) {
		// TODO: Notify the user!
		ERROR("Error: Failed to create team window!\n");
		return B_NO_MEMORY;
	}

	return B_OK;
}


void
GraphicalUserInterface::Show()
{
	fTeamWindow->Show();
}


void
GraphicalUserInterface::Terminate()
{
	// quit window
	if (fTeamWindow != NULL) {
		// TODO: This is not clean. If the window has been deleted we shouldn't
		// try to access it!
		if (fTeamWindow->Lock())
			fTeamWindow->Quit();
	}
}


void
GraphicalUserInterface::NotifyUser(const char* title, const char* message,
	user_notification_type type)
{
	// convert notification type to alert type
	alert_type alertType;
	switch (type) {
		case USER_NOTIFICATION_INFO:
			alertType = B_INFO_ALERT;
			break;
		case USER_NOTIFICATION_WARNING:
		case USER_NOTIFICATION_ERROR:
			alertType = B_WARNING_ALERT;
			break;
	}

	BAlert* alert = new(std::nothrow) BAlert(title, message, "OK",
		NULL, NULL, B_WIDTH_AS_USUAL, alertType);
	if (alert != NULL)
		alert->Go(NULL);

	// TODO: We need to let the alert run asynchronously, but we shouldn't just
	// create it and don't care anymore. Maybe an error window, which can
	// display a list of errors would be the better choice.
}


int32
GraphicalUserInterface::SynchronouslyAskUser(const char* title,
	const char* message, const char* choice1, const char* choice2,
	const char* choice3)
{
	BAlert* alert = new(std::nothrow) BAlert(title, message,
		choice1, choice2, choice3);
	if (alert == NULL)
		return 0;
	return alert->Go();
}
