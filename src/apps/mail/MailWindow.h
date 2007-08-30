/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2001, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

BeMail(TM), Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/
#ifndef _MAIL_WINDOW_H
#define _MAIL_WINDOW_H


#include <Entry.h>
#include <Font.h>
#include <List.h>
#include <Locker.h>
#include <Messenger.h>
#include <Window.h>

#include <mail_encoding.h>


class TContentView;
class TEnclosuresView;
class THeaderView;
class TMailApp;
class TMenu;
class TPrefsWindow;
class TSignatureWindow;

class BEmailMessage;
class BFile;
class BFilePanel;
class BMailMessage;
class BMenuBar;
class BMenuItem;
class BmapButton;
class ButtonBar;
class Words;

class TMailWindow : public BWindow {
	public:
								TMailWindow(BRect frame, const char* title,
									TMailApp* app, const entry_ref* ref,
									const char* to, const BFont *font,
									bool resending,
									BMessenger* trackerMessenger);
		virtual					~TMailWindow();

		virtual	void			FrameResized(float width, float height);
		virtual	void			MenusBeginning();
		virtual	void			MessageReceived(BMessage*);
		virtual	bool			QuitRequested();
		virtual	void			Show();
		virtual	void			Zoom(BPoint, float, float);
		virtual	void			WindowActivated(bool state);

				void			SetTo(const char* mailTo, const char* subject,
									const char* ccTo = NULL,
									const char* bccTo = NULL,
									const BString* body = NULL,
									BMessage* enclosures = NULL);
				void			AddSignature(BMailMessage*);
				void			Forward(entry_ref*, TMailWindow*,
									bool includeAttachments);
				void			Print();
				void			PrintSetup();
				void			Reply(entry_ref*, TMailWindow*, uint32);
				void			CopyMessage(entry_ref* ref, TMailWindow* src);
				status_t		Send(bool);
				status_t		SaveAsDraft();
				status_t		OpenMessage(entry_ref* ref,
									uint32 characterSetForDecoding
										= B_MAIL_NULL_CONVERSION);

				status_t		GetMailNodeRef(node_ref &nodeRef) const;
				BEmailMessage*	Mail() const { return fMail; }

				bool			GetTrackerWindowFile(entry_ref*,
									bool dir) const;
				void			SaveTrackerPosition(entry_ref*);
				void			SetOriginatingWindow(BWindow* window);

				void			SetCurrentMessageRead();
				void			SetTrackerSelectionToCurrent();
				TMailWindow*	FrontmostWindow();
				void			UpdateViews();

	protected:
				void			SetTitleForMessage();
				void			AddEnclosure(BMessage* msg);
				void			BuildButtonBar();
				status_t		TrainMessageAs(const char* commandWord);

	private:
				void			_UpdateSizeLimits();

				TMailApp*		fApp;

				BEmailMessage*	fMail;
				entry_ref*		fRef;
					// Reference to currently displayed file
				int32			fFieldState;
				BFilePanel*		fPanel;
				BMenuBar*		fMenuBar;
				BMenuItem*		fAdd;
				BMenuItem*		fCut;
				BMenuItem*		fCopy;
				BMenuItem*		fHeader;
				BMenuItem*		fPaste;
				BMenuItem*		fPrint;
				BMenuItem*		fPrintSetup;
				BMenuItem*		fQuote;
				BMenuItem*		fRaw;
				BMenuItem*		fRemove;
				BMenuItem*		fRemoveQuote;
				BMenuItem*		fSendNow;
				BMenuItem*		fSendLater;
				BMenuItem*		fUndo;
				BMenuItem*		fRedo;
				BMenuItem*		fNextMsg;
				BMenuItem*		fPrevMsg;
				BMenuItem*		fDeleteNext;
				BMenuItem*		fSpelling;
				BMenu*			fSaveAddrMenu;
		
				ButtonBar*		fButtonBar;
				BmapButton*		fSendButton;
				BmapButton*		fSaveButton;
				BmapButton*		fPrintButton;
				BmapButton*		fSigButton;
		
				BRect			fZoom;
				TContentView*	fContentView;
				THeaderView*	fHeaderView;
				TEnclosuresView* fEnclosuresView;
				TMenu*			fSignature;
				BMessenger		fTrackerMessenger;
					// Talks to tracker window that this was launched from.
				BMessenger		fMessengerToSpamServer;
		
				entry_ref		fPrevRef;
				entry_ref		fNextRef;
				bool			fPrevTrackerPositionSaved : 1;
				bool			fNextTrackerPositionSaved : 1;

				entry_ref		fOpenFolder;

				bool			fSigAdded : 1;
				bool			fIncoming : 1;
				bool			fReplying : 1;
				bool			fResending : 1;
				bool			fSent : 1;
				bool			fDraft : 1;
				bool			fChanged : 1;
			
				static BList	sWindowList;
				static BLocker	sWindowListLock;
		
				char*			fStartingText;	
				entry_ref		fRepliedMail;
				BMessenger*		fOriginatingWindow;
};

#endif // _MAIL_WINDOW_H
