/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

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

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

#ifndef BAR_WINDOW_H
#define BAR_WINDOW_H

#include <Deskbar.h>
#include <Window.h>

class TBeMenu;
class TBarView;

class TBarWindow : public BWindow {
	public:
		TBarWindow();

		virtual void	MenusBeginning();
		virtual void	MenusEnded();
		virtual	bool	QuitRequested();
		virtual void	WorkspaceActivated(int32 ws, bool activate);
		virtual void	ScreenChanged(BRect size, color_space depth);
		virtual void	DispatchMessage(BMessage *message, BHandler *handler);
		virtual void	MessageReceived(BMessage *m);

		void			SaveSettings();
		TBarView*		BarView() const { return fBarView; };
		static 	void	SetBeMenu(TBeMenu *menu);
		TBeMenu*		BeMenu();

		void			ShowBeMenu();
		void			ShowTeamMenu();

		void 			GetLocation(BMessage*);
		deskbar_location 	DeskbarLocation() const;
		void			SetLocation(BMessage*);
		void			SetDeskbarLocation(deskbar_location location, bool expand);
		
		void			IsExpanded(BMessage*);
		void			Expand(BMessage*);
		
		void			ItemInfo(BMessage*);
		void			ItemExists(BMessage*);
		
		void			CountItems(BMessage*);
		
		void			AddItem(BMessage*);
		void			RemoveItem(BMessage*);
		
		void			GetIconFrame(BMessage*);

private:
		static 	TBeMenu	*sBeMenu;
		TBarView		*fBarView;
};

#endif	/* BAR_WINDOW_H */
