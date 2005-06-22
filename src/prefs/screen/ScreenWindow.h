/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 *		Thomas Kurschel
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SCREEN_WINDOW_H
#define SCREEN_WINDOW_H


#include <Window.h>

#include "ScreenMode.h"

class BBox;
class BPopUpMenu;
class BMenuField;


class RefreshWindow;
class MonitorView;
class ScreenSettings;


class ScreenWindow : public BWindow {
	public:
		ScreenWindow(ScreenSettings *settings);
		virtual ~ScreenWindow();

		virtual	bool QuitRequested();
		virtual void MessageReceived(BMessage *message);
		virtual void WorkspaceActivated(int32 ws, bool state);
		virtual void ScreenChanged(BRect frame, color_space mode);

	private:
		void CheckApplyEnabled();
		void CheckResolutionMenu();
		void CheckColorMenu();
		void CheckRefreshMenu();

		void UpdateActiveMode();
		void UpdateRefreshControl();
		void UpdateMonitorView();
		void UpdateControls();

		void Apply();

		bool CanApply() const;
		bool CanRevert() const;

		ScreenSettings*	fSettings;

		MonitorView*	fMonitorView;
		BMenuItem*		fAllWorkspacesItem;

		BPopUpMenu*		fResolutionMenu;
		BMenuField*		fResolutionField;
		BPopUpMenu*		fColorsMenu;
		BMenuField*		fColorsField;
		BPopUpMenu*		fRefreshMenu;
		BMenuField*		fRefreshField;
		BMenuItem*		fOtherRefresh;

		BPopUpMenu*		fCombineMenu;
		BPopUpMenu*		fSwapDisplaysMenu;
		BPopUpMenu*		fUseLaptopPanelMenu;
		BPopUpMenu*		fTVStandardMenu;

		BButton*		fDefaultsButton;
		BButton*		fApplyButton;
		BButton*		fRevertButton;

		ScreenMode		fScreenMode;
		bool			fChangingAllWorkspaces;
		screen_mode		fActive, fSelected, fOriginal;
};

#endif	/* SCREEN_WINDOW_H */
