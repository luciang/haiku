/*
 * Copyright 2006-2009, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef APP_H
#define APP_H

#include <Application.h>
#include <List.h>

class MainWindow;

class App : public BApplication {
public:
								App();
	virtual						~App();

	virtual	bool				QuitRequested();
	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				AboutRequested();
	virtual	void				Pulse();

private:
			void				_StoreSettingsIfNeeded();

			bool				fSettingsChanged;
};

#endif // APP_H
