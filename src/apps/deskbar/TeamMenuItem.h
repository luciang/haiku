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
#ifndef TEAMMENUITEM_H
#define TEAMMENUITEM_H

//	Individual team/application listing
//	item for TeamMenu in mini/vertical mode
// 	item for ExpandoMenuBar in vertical or horizontal expanded mode

#include <MenuItem.h>

#include "WindowMenuItem.h"
#include "BarMenuBar.h"


class BBitmap;


class TTeamMenuItem : public BMenuItem {
	public:
		TTeamMenuItem(BList *team, BBitmap *icon, char *name, char *sig,
			float width = -1.0f, float height = -1.0f,
			bool drawLabel = true, bool vertical = true);
		TTeamMenuItem(float width = -1.0f, float height = -1.0f, bool vertical=true);
		virtual ~TTeamMenuItem();

		status_t Invoke(BMessage *msg = NULL);

		void SetOverrideWidth(float width);
		void SetOverrideHeight(float height);

		bool IsExpanded();
		void ToggleExpandState(bool resizeWindow);
		BRect ExpanderBounds() const;
		TWindowMenuItem* ExpandedWindowItem(int32 id);

		float LabelWidth() const;
		BList *Teams() const;
		const char *Signature() const;
		const char *Name() const;

	protected:
		void GetContentSize(float *width, float *height);
		void Draw();
		void DrawContent();
		void DrawContentLabel();

	private:
		friend class TExpandoMenuBar;
		void InitData(BList *team, BBitmap *icon, char *name, char *sig,
			float width = -1.0f, float height = -1.0f,
			bool drawLabel = true,bool vertical=true);

		BList *fTeam;
		BBitmap *fIcon;
		char *fName;
		char *fSig;
		float fLabelWidth;
		float fLabelAscent;
		float fLabelDescent;
		float fOverrideWidth;
		float fOverrideHeight;

		bool fDrawLabel;
		bool fVertical;

		bool fExpanded;
};

#endif /* TEAMMENUITEM_H */
