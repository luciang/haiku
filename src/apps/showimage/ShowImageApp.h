/*
 * Copyright 2003-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Fernando Francisco de Oliveira
 *		Michael Wilber
 *		Michael Pfeiffer
 */
#ifndef SHOW_IMAGE_APP_H
#define SHOW_IMAGE_APP_H


#include "ShowImageSettings.h"

#include <Application.h>
#include <Catalog.h>
#include <FilePanel.h>


class ShowImageApp : public BApplication {
public:
								ShowImageApp();
	virtual						~ShowImageApp();

	virtual	void				AboutRequested();
	virtual	void				ArgvReceived(int32 argc, char **argv);
	virtual	void				MessageReceived(BMessage *message);
	virtual	void				ReadyToRun();
	virtual	void				Pulse();
	virtual	void				RefsReceived(BMessage *message);
	virtual	bool				QuitRequested();

			ShowImageSettings* 	Settings() { return &fSettings; }

private:
			void				StartPulse();
			void				Open(const entry_ref *ref);
			void				BroadcastToWindows(BMessage *message);
			void				CheckClipboard();

			BMessenger 			fTrackerMessenger;
			BFilePanel			*fOpenPanel;
			bool				fPulseStarted;
			ShowImageSettings	fSettings;
			BCatalog		fCatalog;
};


extern const char *kApplicationSignature;

#define my_app dynamic_cast<ShowImageApp*>(be_app)


#endif	// SHOW_IMAGE_APP_H

