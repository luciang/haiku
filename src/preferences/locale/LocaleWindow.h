/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef LOCALE_WINDOW_H
#define LOCALE_WINDOW_H


#include <Window.h>

class BButton;
class BListView;
class TimeFormatSettingsView;


class LocaleWindow : public BWindow {
	public:
		LocaleWindow(BRect rect);

		virtual bool QuitRequested();
		virtual void MessageReceived(BMessage *message);

	private:
		BButton*	fRevertButton;
		BListView*	fPreferredListView;
		TimeFormatSettingsView* fTimeFormatSettings;
};

#endif	/* LOCALE_WINDOW_H */
