/*
 * Copyright 2001-2007, Haiku.
 * Copyright (c) 2003-4 Kian Duffy <myob@users.sourceforge.net>
 * Parts Copyright (C) 1998,99 Kazuho Okui and Takashi Murai. 
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files or portions
 * thereof (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice
 *    in the  binary, as well as this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided with
 *    the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#ifndef TERM_APP_H
#define TERM_APP_H


#include <Application.h>
#include <String.h>


extern int gPfd;
extern char *ptyname;

class TermWindow;
class TermParse;
class BRect;
class AboutDlg;

class TermApp : public BApplication {
	public:
		TermApp();
		virtual ~TermApp();

	protected:
		void ReadyToRun();
		void Quit();
		void AboutRequested();
		void MessageReceived(BMessage* message);
		void RefsReceived(BMessage* message);
		void ArgvReceived(int32 argc, char** argv);

	private:
		status_t _MakeTermWindow(BRect& frame);
		void _SwitchTerm();
		void _ActivateTermWindow(team_id id);
		bool _IsMinimized(team_id id);
		void _UpdateRegistration(bool set);
		void _UnregisterTerminal();
		void _RegisterTerminal();

		void _Usage(char *name);

		port_id		fRegistrationPort;
		int32		fRows, fCols, fXpos, fYpos;
		bool		fStartFullscreen;
		BString		fWindowTitle;
		int32		fWindowNumber;
		rgb_color	fFg, fBg, fCurFg, fCurBg, fSelFg, fSelbg;
		rgb_color	fImfg, fImbg, fImSel;
		TermWindow*	fTermWindow;
		TermParse*	fTermParse;
		BRect		fTermFrame;
		BString		CommandLine;
};

#endif	// TERM_APP_H
