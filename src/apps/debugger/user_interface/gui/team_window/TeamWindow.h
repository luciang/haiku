/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef TEAM_WINDOW_H
#define TEAM_WINDOW_H


#include <String.h>
#include <Window.h>

#include "BreakpointsView.h"
#include "Function.h"
#include "ImageFunctionsView.h"
#include "ImageListView.h"
#include "SourceView.h"
#include "StackFrame.h"
#include "StackTraceView.h"
#include "Team.h"
#include "ThreadListView.h"
#include "VariablesView.h"


class BButton;
class BMenuBar;
class BTabView;
class Image;
class RegistersView;
class SourceCode;
class StackFrame;
class UserBreakpoint;
class UserInterfaceListener;
class VariablesView;


class TeamWindow : public BWindow, ThreadListView::Listener,
	ImageListView::Listener, StackTraceView::Listener,
	ImageFunctionsView::Listener, BreakpointsView::Listener,
	SourceView::Listener, VariablesView::Listener, Team::Listener,
	Function::Listener, StackFrame::Listener {
public:
								TeamWindow(::Team* team,
									UserInterfaceListener* listener);
								~TeamWindow();

	static	TeamWindow*			Create(::Team* team,
									UserInterfaceListener* listener);
									// throws

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
	enum ActiveSourceObject {
		ACTIVE_SOURCE_NONE,
		ACTIVE_SOURCE_STACK_FRAME,
		ACTIVE_SOURCE_FUNCTION,
		ACTIVE_SOURCE_BREAKPOINT
	};

private:
	// ThreadListView::Listener
	virtual	void				ThreadSelectionChanged(::Thread* thread);

	// ImageListView::Listener
	virtual	void				ImageSelectionChanged(Image* image);

	// StackTraceView::Listener
	virtual	void				StackFrameSelectionChanged(StackFrame* frame);

	// ImageFunctionsView::Listener
	virtual	void				FunctionSelectionChanged(
									FunctionInstance* function);

	// BreakpointsView::Listener
	virtual	void				BreakpointSelectionChanged(
									UserBreakpoint* breakpoint);
	virtual	void				SetBreakpointEnabledRequested(
									UserBreakpoint* breakpoint,
									bool enabled);
	virtual	void				ClearBreakpointRequested(
									UserBreakpoint* breakpoint);

	// SourceView::Listener
	virtual	void				SetBreakpointRequested(target_addr_t address,
									bool enabled);
	virtual	void				ClearBreakpointRequested(target_addr_t address);

	// VariablesView::Listener
	virtual	void				ValueNodeValueRequested(CpuState* cpuState,
									ValueNodeContainer* container,
									ValueNode* valueNode);

	// Team::Listener
	virtual	void				ThreadStateChanged(
									const Team::ThreadEvent& event);
	virtual	void				ThreadCpuStateChanged(
									const Team::ThreadEvent& event);
	virtual	void				ThreadStackTraceChanged(
									const Team::ThreadEvent& event);
	virtual	void				ImageDebugInfoChanged(
									const Team::ImageEvent& event);
	virtual	void				UserBreakpointChanged(
									const Team::UserBreakpointEvent& event);

	// Function::Listener
	virtual	void				FunctionSourceCodeChanged(Function* function);

			void				_Init();

			void				_SetActiveThread(::Thread* thread);
			void				_SetActiveImage(Image* image);
			void				_SetActiveStackTrace(StackTrace* stackTrace);
			void				_SetActiveStackFrame(StackFrame* frame);
			void				_SetActiveBreakpoint(
									UserBreakpoint* breakpoint);
			void				_SetActiveFunction(FunctionInstance* function);
			void				_SetActiveSourceCode(SourceCode* sourceCode);
			void				_UpdateCpuState();
			void				_UpdateRunButtons();
			void				_ScrollToActiveFunction();

			void				_HandleThreadStateChanged(thread_id threadID);
			void				_HandleCpuStateChanged(thread_id threadID);
			void				_HandleStackTraceChanged(thread_id threadID);
			void				_HandleImageDebugInfoChanged(image_id imageID);
			void				_HandleSourceCodeChanged();
			void				_HandleUserBreakpointChanged(
									UserBreakpoint* breakpoint);

private:
			::Team*				fTeam;
			::Thread*			fActiveThread;
			Image*				fActiveImage;
			StackTrace*			fActiveStackTrace;
			StackFrame*			fActiveStackFrame;
			UserBreakpoint*		fActiveBreakpoint;
			FunctionInstance*	fActiveFunction;
			SourceCode*			fActiveSourceCode;
			ActiveSourceObject	fActiveSourceObject;
			UserInterfaceListener* fListener;
			BTabView*			fTabView;
			BTabView*			fLocalsTabView;
			ThreadListView*		fThreadListView;
			ImageListView*		fImageListView;
			ImageFunctionsView*	fImageFunctionsView;
			BreakpointsView*	fBreakpointsView;
			VariablesView*		fVariablesView;
			RegistersView*		fRegistersView;
			StackTraceView*		fStackTraceView;
			SourceView*			fSourceView;
			BButton*			fRunButton;
			BButton*			fStepOverButton;
			BButton*			fStepIntoButton;
			BButton*			fStepOutButton;
			BMenuBar*			fMenuBar;
};


#endif	// TEAM_WINDOW_H
