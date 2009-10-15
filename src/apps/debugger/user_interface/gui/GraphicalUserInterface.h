/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef GRAPHICAL_USER_INTERFACE_H
#define GRAPHICAL_USER_INTERFACE_H


#include "UserInterface.h"


class TeamWindow;


class GraphicalUserInterface : public UserInterface {
public:
								GraphicalUserInterface();
	virtual						~GraphicalUserInterface();

	virtual	status_t			Init(Team* team,
									UserInterfaceListener* listener);
	virtual	void				Show();
	virtual	void				Terminate();
									// shut down the UI *now* -- no more user
									// feedback

	virtual	void				NotifyUser(const char* title,
									const char* message,
									user_notification_type type);
	virtual	int32				SynchronouslyAskUser(const char* title,
									const char* message, const char* choice1,
									const char* choice2, const char* choice3);

private:
			TeamWindow*			fTeamWindow;
};


#endif	// GRAPHICAL_USER_INTERFACE_H
