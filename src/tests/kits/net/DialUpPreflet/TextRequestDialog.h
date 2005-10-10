/*
 * Copyright 2004, Waldemar Kornewald <wkornew@gmx.net>
 * Distributed under the terms of the MIT License.
 */

#ifndef _TEXT_REQUEST_CONNECTOG__H
#define _TEXT_REQUEST_CONNECTOG__H

#include <Window.h>


class TextRequestDialog : public BWindow {
	public:
		TextRequestDialog(const char *title, const char *information,
			const char *request, const char *text = NULL);
		virtual ~TextRequestDialog();
		
		virtual void MessageReceived(BMessage *message);
		virtual bool QuitRequested();
		
		status_t Go(BInvoker *invoker);

	private:
		void UpdateControls();

	private:
		BTextView *fTextView;
			// displays information text
		BButton *fOKButton;
		BTextControl *fTextControl;
		BInvoker *fInvoker;
};


#endif
