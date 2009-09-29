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

//	Be Menu, used in vertical mode, expanded and mini
//		in mini mode will have team menu next to Be menu
//		Be menu in horizontal mode is embedded in ExpandoMenuBar

#ifndef BARMENUBAR_H
#define BARMENUBAR_H

#include <MenuBar.h>

#include "BarView.h"
#include "BarMenuTitle.h"
#include "TimeView.h"


class TBarMenuBar : public BMenuBar {
	public:
		TBarMenuBar(TBarView* bar, BRect frame, const char* name);
		virtual ~TBarMenuBar();

		virtual void MouseMoved(BPoint where, uint32 code, const BMessage* message);
		virtual void Draw(BRect);

		void DrawBackground(BRect);
		void SmartResize(float width = -1.0f, float height = -1.0f);

		void AddTeamMenu();
		void RemoveTeamMenu();

		void InitTrackingHook(bool (* hookfunction)(BMenu*, void*), void* state,
			bool both = false);
	
	private:
		TBarView* fBarView;
		TBarMenuTitle* fBeMenuItem;
		TBarMenuTitle* fAppListMenuItem;
};

#endif /* BARMENUBAR_H */

