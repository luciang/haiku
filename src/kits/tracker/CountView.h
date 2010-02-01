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

#ifndef __COUNT_VIEW__
#define __COUNT_VIEW__

#include <String.h>
#include <View.h>

namespace BPrivate {

class BPoseView;

class BCountView : public BView {
	// displays the item count and a barber pole while the view is updating

public:
	BCountView(BRect, BPoseView *);
	~BCountView();

	virtual	void Draw(BRect);
	virtual	void MouseDown(BPoint);
	virtual	void AttachedToWindow();
	virtual void Pulse();
	virtual void WindowActivated(bool active);

	void CheckCount();
	void StartBarberPole();
	void EndBarberPole();

	void SetTypeAhead(const char *);
	const char *TypeAhead() const;
	bool IsTypingAhead() const;

	void AddFilterCharacter(const char *character);
	void RemoveFilterCharacter();
	void CancelFilter();
	const char *Filter() const;
	bool IsFiltering() const;

	void SetBorderHighlighted(bool highlighted);

private:
	BRect BarberPoleInnerRect() const;
	BRect BarberPoleOuterRect() const;
	BRect TextInvalRect() const;
	BRect TextAndBarberPoleRect() const;
	void TrySpinningBarberPole();

	int32 fLastCount;
	BPoseView *fPoseView;
	bool fShowingBarberPole : 1;
	bool fBorderHighlighted : 1;
	BBitmap *fBarberPoleMap;
	float fLastBarberPoleOffset;
	bigtime_t fStartSpinningAfter;
	BString fTypeAheadString;
	BString fFilterString;
};

} // namespace BPrivate

using namespace BPrivate;

#endif
