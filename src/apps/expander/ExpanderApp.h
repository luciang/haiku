/*****************************************************************************/
// Expander
// Written by Jérôme Duval
//
// ExpanderApp.h
//
//
// Copyright (c) 2004 OpenBeOS Project
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
/*****************************************************************************/

#ifndef _ExpanderApp_h
#define _ExpanderApp_h

#include <Application.h>
#include <Catalog.h>
#include "ExpanderSettings.h"
#include "ExpanderWindow.h"

class ExpanderApp : public BApplication {
	public:
		ExpanderApp();

	public:
		virtual void AboutRequested();
		virtual void ArgvReceived(int32 argc, char **argv);
		virtual void ReadyToRun();
		virtual void RefsReceived(BMessage *msg);

		ExpanderSettings fSettings;
		void UpdateSettingsFrom(BMessage *message);
	private:
		ExpanderWindow *fWindow;
		BCatalog				fCatalog;
};

#endif /* _ExpanderApp_h */
