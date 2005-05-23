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

#ifndef	STATUS_WINDOW_H
#define STATUS_WINDOW_H

#include <Window.h>
#include <View.h>
#include <Bitmap.h>
#include <StatusBar.h>
#include <String.h>

#include "ObjectList.h"

namespace BPrivate {

enum StatusWindowState {
	kCopyState,
	kMoveState,
	kDeleteState,
	kTrashState,
	kVolumeState,
	kCreateLinkState,
	kRestoreFromTrashState
};

class BStatusView;

class BStatusWindow : public BWindow {
public:
	BStatusWindow();
	~BStatusWindow();
	void CreateStatusItem(thread_id, StatusWindowState);
	void InitStatusItem(thread_id, int32 totalItems, off_t totalSize,
		const entry_ref *destDir = NULL, bool showCount = true);
	void UpdateStatus(thread_id, const char *curItem, off_t itemSize, bool optional = false);
		// if true is passed in <optional> status will only
		// be updated if 0.2 seconds elapsed since the last update
	void RemoveStatusItem(thread_id);
	bool HasStatus(thread_id);
	bool CheckCanceledOrPaused(thread_id);
	void UpdateButtonState();
	
	bool AttemptToQuit();
		// called by the tracker app during quit time, before
		// inherited QuitRequested; kills all the copy/move/empty trash
		// threads in a clean way by issuing a cancel
protected:
	virtual	void WindowActivated(bool state);
	
private:
	BObjectList<BStatusView> fViewList;
	BMessageFilter *fMouseDownFilter;
	
	bool fRetainDesktopFocus;

	typedef BWindow _inherited;
};

class BStatusView : public BView {
public:
	BStatusView(BRect, thread_id, StatusWindowState);
	virtual ~BStatusView();
	
	void Init();
	
	void InitStatus(int32 totalItems, off_t totalSize, const entry_ref *destDir,
		bool showCount);
	
	// BView overrides
	virtual	void Draw(BRect);
	virtual	void AttachedToWindow();
	virtual	void MessageReceived(BMessage *);
	void UpdateStatus(const char *curItem, off_t itemSize, bool optional = false);
		// if true is passed in <optional> status will only
		// be updated if 0.2 seconds elapsed since the last update
	
	bool WasCanceled() const;
	bool IsPaused() const;
	thread_id Thread() const;
	
	void ForceQuit();
	void SetWasCanceled();
	// called by AboutToQuit
	
private:
	BStatusBar *fStatusBar;
	off_t fTotalSize;
	off_t fItemSize;
	int32 fCurItem;
	int32 fType;
	BBitmap *fBitmap;
	BButton *fStopButton;
	BButton *fPauseButton;
	thread_id fThread;
	float fLastUpdateTime;
	bool fShowCount;
	bool fWasCanceled;
	bool fIsPaused;
	BString fDestDir;
	char fPendingStatusString[128];

	typedef BView _inherited;
};

inline bool
BStatusView::IsPaused() const
{
	return fIsPaused;
}

inline bool
BStatusView::WasCanceled() const
{
	return fWasCanceled;
}

inline thread_id
BStatusView::Thread() const
{
	return fThread;
}

extern BStatusWindow *gStatusWindow;

} // namespace BPrivate

using namespace BPrivate;

#endif
