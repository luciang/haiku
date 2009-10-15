/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H


#include <OS.h>

#include <Referenceable.h>

#include "Types.h"


class FunctionInstance;
class Image;
class StackFrame;
class Team;
class Thread;
class TypeComponentPath;
class UserBreakpoint;
class UserInterfaceListener;
class Variable;


enum user_notification_type {
	USER_NOTIFICATION_INFO,
	USER_NOTIFICATION_WARNING,
	USER_NOTIFICATION_ERROR
};


class UserInterface : public BReferenceable {
public:
	virtual						~UserInterface();

	virtual	status_t			Init(Team* team,
									UserInterfaceListener* listener) = 0;
	virtual	void				Show() = 0;
	virtual	void				Terminate() = 0;
									// shut down the UI *now* -- no more user
									// feedback

	virtual	void				NotifyUser(const char* title,
									const char* message,
									user_notification_type type) = 0;
	virtual	int32				SynchronouslyAskUser(const char* title,
									const char* message, const char* choice1,
									const char* choice2, const char* choice3)
									= 0;
};


class UserInterfaceListener {
public:
	virtual						~UserInterfaceListener();

	virtual	void				FunctionSourceCodeRequested(
									FunctionInstance* function) = 0;
	virtual	void				ImageDebugInfoRequested(Image* image) = 0;
	virtual	void				StackFrameValueRequested(Thread* thread,
									StackFrame* stackFrame, Variable* variable,
									TypeComponentPath* path) = 0;
									// called with team locked
	virtual	void				ThreadActionRequested(thread_id threadID,
									uint32 action) = 0;

	virtual	void				SetBreakpointRequested(target_addr_t address,
									bool enabled) = 0;
	virtual	void				SetBreakpointEnabledRequested(
									UserBreakpoint* breakpoint,
									bool enabled) = 0;
	virtual	void				ClearBreakpointRequested(
									target_addr_t address) = 0;
	virtual	void				ClearBreakpointRequested(
									UserBreakpoint* breakpoint) = 0;
									// TODO: Consolidate those!

	virtual	bool				UserInterfaceQuitRequested() = 0;
};


#endif	// USER_INTERFACE_H
