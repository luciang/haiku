/*
 * Copyright 2002-2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm (darkwyrm@earthlink.net)
 */
#ifndef APR_WINDOW_H
#define APR_WINDOW_H

#include <Application.h>
#include <Button.h>
#include <Window.h>
#include <Message.h>
#include <TabView.h>

#include "APRView.h"
#include "AntialiasingSettingsView.h"

class APRWindow : public BWindow 
{
public:
			APRWindow(BRect frame); 
	bool	QuitRequested(void);
	void	MessageReceived(BMessage *message);
	
private:
		APRView*				fColorsView;
		BButton*				fDefaultsButton;
		BButton*				fRevertButton;

		AntialiasingSettingsView*	fAntialiasingSettings;

};

static const int32 kMsgUpdate = 'updt';

#endif
