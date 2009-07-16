/*
 * Copyright (c) 2001-2009, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Adi Oanca <adioanca@gmail.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "Window.h"

#include "Decorator.h"
#include "DecorManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "HWInterface.h"
#include "MessagePrivate.h"
#include "PortLink.h"
#include "ServerApp.h"
#include "ServerWindow.h"
#include "Workspace.h"
#include "WorkspacesView.h"

#include <ViewPrivate.h>
#include <WindowPrivate.h>

#include <Debug.h>
#include <DirectWindow.h>
#include <PortLink.h>
#include <View.h>

#include <new>
#include <stdio.h>


// Toggle debug output
//#define DEBUG_WINDOW
//#define DEBUG_WINDOW_CLICK

#ifdef DEBUG_WINDOW
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

#ifdef DEBUG_WINDOW_CLICK
#	define STRACE_CLICK(x) printf x
#else
#	define STRACE_CLICK(x) ;
#endif

// IMPORTANT: nested LockSingleWindow()s are not supported (by MultiLocker)

using std::nothrow;

// if the background clearing is delayed until
// the client draws the view, we have less flickering
// when contents have to be redrawn because of resizing
// a window or because the client invalidates parts.
// when redrawing something that has been exposed from underneath
// other windows, the other window will be seen longer at
// its previous position though if the exposed parts are not
// cleared right away. maybe there ought to be a flag in
// the update session, which tells us the cause of the update


//static rgb_color sPendingColor = (rgb_color){ 255, 255, 0, 255 };
//static rgb_color sCurrentColor = (rgb_color){ 255, 0, 255, 255 };


Window::Window(const BRect& frame, const char *name,
		window_look look, window_feel feel, uint32 flags, uint32 workspaces,
		::ServerWindow* window, DrawingEngine* drawingEngine)
	:
	fTitle(name),
	fFrame(frame),

	fVisibleRegion(),
	fVisibleContentRegion(),
	fDirtyRegion(),
	fDirtyCause(0),

	fBorderRegion(),
	fContentRegion(),
	fEffectiveDrawingRegion(),

	fVisibleContentRegionValid(false),
	fBorderRegionValid(false),
	fContentRegionValid(false),
	fEffectiveDrawingRegionValid(false),

	fRegionPool(),

	fIsClosing(false),
	fIsMinimizing(false),
	fIsZooming(false),
	fIsResizing(false),
	fIsSlidingTab(false),
	fIsDragging(false),
	fActivateOnMouseUp(false),

	fDecorator(NULL),
	fTopView(NULL),
	fWindow(window),
	fDrawingEngine(drawingEngine),
	fDesktop(window->Desktop()),

	fLastMousePosition(0.0, 0.0),
	fLastMoveTime(0),

	fCurrentUpdateSession(&fUpdateSessions[0]),
	fPendingUpdateSession(&fUpdateSessions[1]),
	fUpdateRequested(false),
	fInUpdate(false),
	fUpdatesEnabled(true),

	// windows start hidden
	fHidden(true),
	fMinimized(false),
	fIsFocus(false),

	fLook(look),
	fFeel(feel),
	fWorkspaces(workspaces),
	fCurrentWorkspace(-1),

	fMinWidth(1),
	fMaxWidth(32768),
	fMinHeight(1),
	fMaxHeight(32768),

	fWorkspacesViewCount(0)
{
	// make sure our arguments are valid
	if (!IsValidLook(fLook))
		fLook = B_TITLED_WINDOW_LOOK;
	if (!IsValidFeel(fFeel))
		fFeel = B_NORMAL_WINDOW_FEEL;

	SetFlags(flags, NULL);

	if (fLook != B_NO_BORDER_WINDOW_LOOK) {
		fDecorator = gDecorManager.AllocateDecorator(fDesktop, fDrawingEngine,
			frame, name, fLook, fFlags);
		if (fDecorator) {
			fDecorator->GetSizeLimits(&fMinWidth, &fMinHeight,
				&fMaxWidth, &fMaxHeight);
		}
	}

	// do we need to change our size to let the decorator fit?
	// _ResizeBy() will adapt the frame for validity before resizing
	if (feel == kDesktopWindowFeel) {
		// the desktop window spans over the whole screen
		// TODO: this functionality should be moved somewhere else
		//  (so that it is always used when the workspace is changed)
		uint16 width, height;
		uint32 colorSpace;
		float frequency;
		if (fDesktop->ScreenAt(0)) {
			fDesktop->ScreenAt(0)->GetMode(width, height,
				colorSpace, frequency);
// TODO: MOVE THIS AWAY!!! ResizeBy contains calls to virtual methods!
// Also, there is no TopView()!
			fFrame.OffsetTo(B_ORIGIN);
//			ResizeBy(width - frame.Width(), height - frame.Height(), NULL);
		}
	}

	STRACE(("Window %p, %s:\n", this, Name()));
	STRACE(("\tFrame: (%.1f, %.1f, %.1f, %.1f)\n", fFrame.left, fFrame.top,
		fFrame.right, fFrame.bottom));
	STRACE(("\tWindow %s\n", window ? window->Title() : "NULL"));
}


Window::~Window()
{
	if (fTopView) {
		fTopView->DetachedFromWindow();
		delete fTopView;
	}

	delete fDecorator;

	delete fDrawingEngine;
}


status_t
Window::InitCheck() const
{
	if (!fDrawingEngine)
		return B_NO_MEMORY;
	// TODO: anything else?
	return B_OK;
}


void
Window::SetClipping(BRegion* stillAvailableOnScreen)
{
	// this function is only called from the Desktop thread

	// start from full region (as if the window was fully visible)
	GetFullRegion(&fVisibleRegion);
	// clip to region still available on screen
	fVisibleRegion.IntersectWith(stillAvailableOnScreen);

	fVisibleContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;

	// TODO: review this!
	fWindow->HandleDirectConnection(B_DIRECT_MODIFY | B_CLIPPING_MODIFIED);
}


void
Window::GetFullRegion(BRegion* region)
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	// start from the decorator border, extend to use the frame
	GetBorderRegion(region);
	region->Include(fFrame);
}

// GetBorderRegion
void
Window::GetBorderRegion(BRegion* region)
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	if (!fBorderRegionValid) {
		if (fDecorator)
			fDecorator->GetFootprint(&fBorderRegion);
		else
			fBorderRegion.MakeEmpty();

		fBorderRegionValid = true;
	}

	*region = fBorderRegion;
}


void
Window::GetContentRegion(BRegion* region)
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	if (!fContentRegionValid) {
		_UpdateContentRegion();
	}

	*region = fContentRegion;
}


BRegion&
Window::VisibleContentRegion()
{
	// TODO: if someone needs to call this from
	// the outside, the clipping needs to be readlocked!

	// regions expected to be locked
	if (!fVisibleContentRegionValid) {
		GetContentRegion(&fVisibleContentRegion);
		fVisibleContentRegion.IntersectWith(&fVisibleRegion);
	}
	return fVisibleContentRegion;
}


// #pragma mark -


void
Window::_PropagatePosition()
{
	if ((fFlags & B_SAME_POSITION_IN_ALL_WORKSPACES) == 0)
		return;

	for (int32 i = 0; i < kListCount; i++) {
		Anchor(i).position = fFrame.LeftTop();
	}
}


void
Window::MoveBy(int32 x, int32 y)
{
	// this function is only called from the desktop thread

	if (x == 0 && y == 0)
		return;

	fFrame.OffsetBy(x, y);
	_PropagatePosition();

	// take along the dirty region which is not
	// processed yet
	fDirtyRegion.OffsetBy(x, y);

	if (fBorderRegionValid)
		fBorderRegion.OffsetBy(x, y);
	if (fContentRegionValid)
		fContentRegion.OffsetBy(x, y);

	if (fCurrentUpdateSession->IsUsed())
		fCurrentUpdateSession->MoveBy(x, y);
	if (fPendingUpdateSession->IsUsed())
		fPendingUpdateSession->MoveBy(x, y);

	fEffectiveDrawingRegionValid = false;

	if (fDecorator)
		fDecorator->MoveBy(x, y);

	if (fTopView != NULL) {
		fTopView->MoveBy(x, y, NULL);
		fTopView->UpdateOverlay();
	}

	// the desktop will take care of dirty regions

	// dispatch a message to the client informing about the changed size
	BMessage msg(B_WINDOW_MOVED);
	msg.AddInt64("when", system_time());
	msg.AddPoint("where", fFrame.LeftTop());
	fWindow->SendMessageToClient(&msg);
}


void
Window::ResizeBy(int32 x, int32 y, BRegion* dirtyRegion)
{
	// this function is only called from the desktop thread

	int32 wantWidth = fFrame.IntegerWidth() + x;
	int32 wantHeight = fFrame.IntegerHeight() + y;

	// enforce size limits
	if (wantWidth < fMinWidth)
		wantWidth = fMinWidth;
	if (wantWidth > fMaxWidth)
		wantWidth = fMaxWidth;

	if (wantHeight < fMinHeight)
		wantHeight = fMinHeight;
	if (wantHeight > fMaxHeight)
		wantHeight = fMaxHeight;

	x = wantWidth - fFrame.IntegerWidth();
	y = wantHeight - fFrame.IntegerHeight();

	if (x == 0 && y == 0)
		return;

	fWindow->HandleDirectConnection(B_DIRECT_STOP);

	fFrame.right += x;
	fFrame.bottom += y;

	fWindow->HandleDirectConnection(B_DIRECT_START | B_BUFFER_RESIZED);

	fBorderRegionValid = false;
	fContentRegionValid = false;
	fEffectiveDrawingRegionValid = false;

	if (fDecorator) {
		fDecorator->ResizeBy(x, y, dirtyRegion);
//if (dirtyRegion) {
//fDrawingEngine->FillRegion(*dirtyRegion, (rgb_color){ 255, 255, 0, 255 });
//snooze(40000);
//}
	}

	if (fTopView != NULL) {
		fTopView->ResizeBy(x, y, dirtyRegion);
		fTopView->UpdateOverlay();
	}

//if (dirtyRegion)
//fDrawingEngine->FillRegion(*dirtyRegion, (rgb_color){ 0, 255, 255, 255 });

	// send a message to the client informing about the changed size
	BRect frame(Frame());
	BMessage msg(B_WINDOW_RESIZED);
	msg.AddInt64("when", system_time());
	msg.AddInt32("width", frame.IntegerWidth());
	msg.AddInt32("height", frame.IntegerHeight());
	fWindow->SendMessageToClient(&msg);
}


void
Window::ScrollViewBy(View* view, int32 dx, int32 dy)
{
	// this is executed in ServerWindow with the Readlock
	// held

	if (!view || view == fTopView || (dx == 0 && dy == 0))
		return;

	BRegion* dirty = fRegionPool.GetRegion();
	if (!dirty)
		return;

	view->ScrollBy(dx, dy, dirty);

//fDrawingEngine->FillRegion(*dirty, (rgb_color){ 255, 0, 255, 255 });
//snooze(20000);

	if (!IsOffscreenWindow() && IsVisible() && view->IsVisible()) {
		dirty->IntersectWith(&VisibleContentRegion());
		_TriggerContentRedraw(*dirty);
	}

	fRegionPool.Recycle(dirty);
}


//! Takes care of invalidating parts that could not be copied
void
Window::CopyContents(BRegion* region, int32 xOffset, int32 yOffset)
{
	// executed in ServerWindow thread with the read lock held
	if (!IsVisible())
		return;

	BRegion* newDirty = fRegionPool.GetRegion(*region);

	// clip the region to the visible contents at the
	// source and destination location (note that VisibleContentRegion()
	// is used once to make sure it is valid, then fVisibleContentRegion
	// is used directly)
	region->IntersectWith(&VisibleContentRegion());
	if (region->CountRects() > 0) {
		// Constrain to content region at destination
		region->OffsetBy(xOffset, yOffset);
		region->IntersectWith(&fVisibleContentRegion);
		if (region->CountRects() > 0) {
			// if the region still contains any rects
			// offset to source location again
			region->OffsetBy(-xOffset, -yOffset);

			BRegion* allDirtyRegions = fRegionPool.GetRegion(fDirtyRegion);
			if (allDirtyRegions != NULL) {
				if (fPendingUpdateSession->IsUsed()) {
					allDirtyRegions->Include(
						&fPendingUpdateSession->DirtyRegion());
				}
				if (fCurrentUpdateSession->IsUsed()) {
					allDirtyRegions->Include(
						&fCurrentUpdateSession->DirtyRegion());
				}
				// Get just the part of the dirty regions which is semantically
				// copied along
				allDirtyRegions->IntersectWith(region);
			}

			BRegion* copyRegion = fRegionPool.GetRegion(*region);
			if (copyRegion != NULL) {
				// never copy what's already dirty
				if (allDirtyRegions != NULL)
					copyRegion->Exclude(allDirtyRegions);

				fDrawingEngine->CopyRegion(copyRegion, xOffset, yOffset);

				// Prevent those parts from being added to the dirty region...
				newDirty->Exclude(copyRegion);

				// The parts that could be copied are not dirty (at the
				// target location!)
				copyRegion->OffsetBy(xOffset, yOffset);
				// ... and even exclude them from the pending dirty region!
				if (fPendingUpdateSession->IsUsed())
					fPendingUpdateSession->DirtyRegion().Exclude(copyRegion);

				fRegionPool.Recycle(copyRegion);
			} else {
				// Fallback, should never be here.
				fDrawingEngine->CopyRegion(region, xOffset, yOffset);
			}

			if (allDirtyRegions != NULL)
				fRegionPool.Recycle(allDirtyRegions);
		}
	}
	// what is left visible from the original region
	// at the destination after the region which could be
	// copied has been excluded, is considered dirty
	// NOTE: it may look like dirty regions are not moved
	// if no region could be copied, but that's alright,
	// since these parts will now be in newDirty anyways
	// (with the right offset)
	newDirty->OffsetBy(xOffset, yOffset);
	newDirty->IntersectWith(&fVisibleContentRegion);
	if (newDirty->CountRects() > 0)
		ProcessDirtyRegion(*newDirty);

	fRegionPool.Recycle(newDirty);
}


// #pragma mark -


void
Window::SetTopView(View* topView)
{
	fTopView = topView;

	if (fTopView) {
		// the top view is special, it has a coordinate system
		// as if it was attached directly to the desktop, therefor,
		// the coordinate conversion through the view tree works
		// as expected, since the top view has no "parent" but has
		// fFrame as if it had

		// make sure the location of the top view on screen matches ours
		fTopView->MoveBy((int32)(fFrame.left - fTopView->Frame().left),
			(int32)(fFrame.top - fTopView->Frame().top), NULL);

		// make sure the size of the top view matches ours
		fTopView->ResizeBy((int32)(fFrame.Width() - fTopView->Frame().Width()),
			(int32)(fFrame.Height() - fTopView->Frame().Height()), NULL);

		fTopView->AttachedToWindow(this);
	}
}


View*
Window::ViewAt(const BPoint& where)
{
	return fTopView->ViewAt(where);
}


window_anchor&
Window::Anchor(int32 index)
{
	return fAnchor[index];
}


Window*
Window::NextWindow(int32 index) const
{
	return fAnchor[index].next;
}


Window*
Window::PreviousWindow(int32 index) const
{
	return fAnchor[index].previous;
}


// #pragma mark -


void
Window::GetEffectiveDrawingRegion(View* view, BRegion& region)
{
	if (!fEffectiveDrawingRegionValid) {
		fEffectiveDrawingRegion = VisibleContentRegion();
		if (fUpdateRequested && !fInUpdate) {
			// we requested an update, but the client has not started it yet,
			// so it is only allowed to draw outside the pending update sessions region
			fEffectiveDrawingRegion.Exclude(&(fPendingUpdateSession->DirtyRegion()));
		} else if (fInUpdate) {
			// enforce the dirty region of the update session
			fEffectiveDrawingRegion.IntersectWith(&fCurrentUpdateSession->DirtyRegion());
		} else {
			// not in update, the view can draw everywhere
//printf("Window(%s)::GetEffectiveDrawingRegion(for %s) - outside update\n", Title(), view->Name());
		}

		fEffectiveDrawingRegionValid = true;
	}

	// TODO: this is a region that needs to be cached later in the server
	// when the current view in ServerWindow is set, and we are currently
	// in an update (fInUpdate), than we can set this region and remember
	// it for the comming drawing commands until the current view changes
	// again or fEffectiveDrawingRegionValid is suddenly false.
	region = fEffectiveDrawingRegion;
	if (!fContentRegionValid)
		_UpdateContentRegion();

	region.IntersectWith(&view->ScreenAndUserClipping(&fContentRegion));
}


bool
Window::DrawingRegionChanged(View* view) const
{
	return !fEffectiveDrawingRegionValid || !view->IsScreenClippingValid();
}


void
Window::ProcessDirtyRegion(BRegion& region)
{
	// if this is exectuted in the desktop thread,
	// it means that the window thread currently
	// blocks to get the read lock, if it is
	// executed from the window thread, it should
	// have the read lock and the desktop thread
	// is blocking to get the write lock. IAW, this
	// is only executed in one thread.
	if (fDirtyRegion.CountRects() == 0) {
		// the window needs to be informed
		// when the dirty region was empty.
		// NOTE: when the window thread has processed
		// the dirty region in MessageReceived(),
		// it will make the region empty again,
		// when it is empty here, we need to send
		// the message to initiate the next update round.
		// Until the message is processed in the window
		// thread, the desktop thread can add parts to
		// the region as it likes.
		ServerWindow()->RequestRedraw();
	}

	fDirtyRegion.Include(&region);
	fDirtyCause |= UPDATE_EXPOSE;
}


void
Window::RedrawDirtyRegion()
{
	// executed from ServerWindow with the read lock held

	if (IsVisible()) {
		_DrawBorder();

		BRegion* dirtyContentRegion =
			fRegionPool.GetRegion(VisibleContentRegion());
		dirtyContentRegion->IntersectWith(&fDirtyRegion);

		_TriggerContentRedraw(*dirtyContentRegion);

		fRegionPool.Recycle(dirtyContentRegion);
	}

	// reset the dirty region, since
	// we're fully clean. If the desktop
	// thread wanted to mark something
	// dirty in the mean time, it was
	// blocking on the global region lock to
	// get write access, since we're holding
	// the read lock for the whole time.
	fDirtyRegion.MakeEmpty();
	fDirtyCause = 0;
}


void
Window::MarkDirty(BRegion& regionOnScreen)
{
	// for marking any part of the desktop dirty
	// this will get write access to the global
	// region lock, and result in ProcessDirtyRegion()
	// to be called for any windows affected
	if (fDesktop)
		fDesktop->MarkDirty(regionOnScreen);
}


void
Window::MarkContentDirty(BRegion& regionOnScreen)
{
	// for triggering AS_REDRAW
	// since this won't affect other windows, read locking
	// is sufficient. If there was no dirty region before,
	// an update message is triggered
	if (fHidden || IsOffscreenWindow())
		return;

	regionOnScreen.IntersectWith(&VisibleContentRegion());
	fDirtyCause |= UPDATE_REQUEST;
	_TriggerContentRedraw(regionOnScreen);
}


void
Window::MarkContentDirtyAsync(BRegion& regionOnScreen)
{
	// NOTE: see comments in ProcessDirtyRegion()
	if (fHidden || IsOffscreenWindow())
		return;

	regionOnScreen.IntersectWith(&VisibleContentRegion());

	if (fDirtyRegion.CountRects() == 0) {
		ServerWindow()->RequestRedraw();
	}

	fDirtyRegion.Include(&regionOnScreen);
	fDirtyCause |= UPDATE_REQUEST;
}


void
Window::InvalidateView(View* view, BRegion& viewRegion)
{
	if (view && IsVisible() && view->IsVisible()) {
		if (!fContentRegionValid)
			_UpdateContentRegion();

		view->ConvertToScreen(&viewRegion);
		viewRegion.IntersectWith(&VisibleContentRegion());
		if (viewRegion.CountRects() > 0) {
			viewRegion.IntersectWith(
				&view->ScreenAndUserClipping(&fContentRegion));

//fDrawingEngine->FillRegion(viewRegion, rgb_color{ 0, 255, 0, 255 });
//snooze(10000);
			fDirtyCause |= UPDATE_REQUEST;
			_TriggerContentRedraw(viewRegion);
		}
	}
}

// DisableUpdateRequests
void
Window::DisableUpdateRequests()
{
	fUpdatesEnabled = false;
}


// EnableUpdateRequests
void
Window::EnableUpdateRequests()
{
	fUpdatesEnabled = true;
	if (!fUpdateRequested && fPendingUpdateSession->IsUsed())
		_SendUpdateMessage();
}

// #pragma mark -


void
Window::MouseDown(BMessage* message, BPoint where, int32* _viewToken)
{
	// TODO: move into Decorator
	if (!fBorderRegionValid)
		GetBorderRegion(&fBorderRegion);

	// default action is to drag the Window
	if (fBorderRegion.Contains(where)) {
		// clicking Window visible area

		click_type action = DEC_DRAG;

		if (fDecorator)
			action = _ActionFor(message);

		// ignore clicks on decorator buttons if the
		// non-floating window doesn't have focus
		if (!IsFocus() && !IsFloating() && action != DEC_MOVETOBACK
			&& action != DEC_RESIZE && action != DEC_SLIDETAB)
			action = DEC_DRAG;

		// set decorator internals
		switch (action) {
			case DEC_CLOSE:
				fIsClosing = true;
				STRACE_CLICK(("===> DEC_CLOSE\n"));
				break;

			case DEC_ZOOM:
				fIsZooming = true;
				STRACE_CLICK(("===> DEC_ZOOM\n"));
				break;

			case DEC_MINIMIZE:
				fIsMinimizing = true;
				STRACE_CLICK(("===> DEC_MINIMIZE\n"));
				break;

			case DEC_DRAG:
				fIsDragging = true;
				fLastMousePosition = where;
				STRACE_CLICK(("===> DEC_DRAG\n"));
				break;

			case DEC_RESIZE:
				fIsResizing = true;
				fLastMousePosition = where;
				STRACE_CLICK(("===> DEC_RESIZE\n"));
				break;

			case DEC_SLIDETAB:
				fIsSlidingTab = true;
				fLastMousePosition = where;
				STRACE_CLICK(("===> DEC_SLIDETAB\n"));
				break;

			default:
				break;
		}

		// redraw decorator
		BRegion* visibleBorder = fRegionPool.GetRegion();
		GetBorderRegion(visibleBorder);
		visibleBorder->IntersectWith(&VisibleRegion());

		DrawingEngine* engine = fDecorator->GetDrawingEngine();
		engine->LockParallelAccess();
		engine->ConstrainClippingRegion(visibleBorder);

		if (fIsZooming) {
			fDecorator->SetZoom(true);
		} else if (fIsClosing) {
			fDecorator->SetClose(true);
		} else if (fIsMinimizing) {
			fDecorator->SetMinimize(true);
		}

		engine->UnlockParallelAccess();

		fRegionPool.Recycle(visibleBorder);

		// based on what the Decorator returned, properly place this window.
		if (action == DEC_MOVETOBACK) {
			fDesktop->SendWindowBehind(this);
		} else {
			fDesktop->SetMouseEventWindow(this);

			// activate window if not in FFM mode
			DesktopSettings desktopSettings(fDesktop);
			if (!desktopSettings.FocusFollowsMouse()) {
				fDesktop->ActivateWindow(this);
			} else {
				// actually, the window should already be
				// focused since the mouse would have to
				// be over it, but just for completeness...
				fDesktop->SetFocusWindow(this);
				if (action == DEC_DRAG) {
					fActivateOnMouseUp = true;
					fLastMoveTime = system_time();
				}
			}
		}
	} else {
		// click was inside the window contents
		if (View* view = ViewAt(where)) {
			if (HasModal())
				return;

			// clicking a simple View
			if (!IsFocus()) {
				DesktopSettings desktopSettings(fDesktop);

				// Activate window in case it doesn't accept first click, and
				// we're not in FFM mode
				if ((Flags() & B_WILL_ACCEPT_FIRST_CLICK) == 0
					&& !desktopSettings.FocusFollowsMouse())
					fDesktop->ActivateWindow(this);

				// Eat the click if we don't accept first click
				// (B_AVOID_FOCUS never gets the focus, so they always accept
				// the first click)
				// TODO: the latter is unlike BeOS - if we really wanted to
				// imitate this behaviour, we would need to check if we're
				// the front window instead of the focus window
				if ((Flags() & (B_WILL_ACCEPT_FIRST_CLICK
						| B_AVOID_FOCUS)) == 0)
					return;
			}

			// fill out view token for the view under the mouse
			*_viewToken = view->Token();
			view->MouseDown(message, where);
		}
	}
}


void
Window::MouseUp(BMessage* message, BPoint where, int32* _viewToken)
{
	bool invalidate = false;
	if (fDecorator) {
		click_type action = _ActionFor(message);

		// redraw decorator
		BRegion* visibleBorder = fRegionPool.GetRegion();
		GetBorderRegion(visibleBorder);
		visibleBorder->IntersectWith(&VisibleRegion());

		DrawingEngine* engine = fDecorator->GetDrawingEngine();
		engine->LockParallelAccess();
		engine->ConstrainClippingRegion(visibleBorder);

		if (fIsZooming) {
			fIsZooming = false;
			fDecorator->SetZoom(false);
			if (action == DEC_ZOOM) {
				invalidate = true;
				fWindow->NotifyZoom();
			}
		}
		if (fIsClosing) {
			fIsClosing = false;
			fDecorator->SetClose(false);
			if (action == DEC_CLOSE) {
				invalidate = true;
				fWindow->NotifyQuitRequested();
			}
		}
		if (fIsMinimizing) {
			fIsMinimizing = false;
			fDecorator->SetMinimize(false);
			if (action == DEC_MINIMIZE) {
				invalidate = true;
				fWindow->NotifyMinimize(true);
			}
		}

		engine->UnlockParallelAccess();

		fRegionPool.Recycle(visibleBorder);
	}

	// in FFM mode, activate the window and bring it
	// to front in case this was a drag click but the
	// mouse was not moved
	if (fActivateOnMouseUp) {
		fActivateOnMouseUp = false;
		// on R5, there is a time window for this feature
		// ie, click and press too long, nothing will happen
		if (system_time() - fLastMoveTime < 500000)
			fDesktop->ActivateWindow(this);
	}

	fIsDragging = false;
	fIsResizing = false;
	fIsSlidingTab = false;

	if (View* view = ViewAt(where)) {
		if (HasModal())
			return;

		*_viewToken = view->Token();
		view->MouseUp(message, where);
	}
}


void
Window::MouseMoved(BMessage *message, BPoint where, int32* _viewToken,
	bool isLatestMouseMoved)
{
#if 0
	if (fDecorator != NULL && fTopView != NULL) {
		DrawingEngine* engine = fDecorator->GetDrawingEngine();
		engine->LockParallelAccess();
		engine->ConstrainClippingRegion(&VisibleRegion());

		fTopView->MarkAt(engine, where);
		engine->UnlockParallelAccess();
	}
#endif

	View* view = ViewAt(where);
	if (view != NULL)
		*_viewToken = view->Token();

	// ignore pointer history
	if (!isLatestMouseMoved)
		return;

	// limit the rate at which "mouse moved" events
	// are handled that move or resize the window
	if (fIsDragging || fIsResizing) {
		bigtime_t now = system_time();
		if (now - fLastMoveTime < 13333) {
			// TODO: add a "timed event" to query for
			// the then current mouse position
			return;
		}
		fLastMoveTime = now;
	}

	if (fDecorator) {
		BRegion* visibleBorder = fRegionPool.GetRegion();
		GetBorderRegion(visibleBorder);
		visibleBorder->IntersectWith(&VisibleRegion());

		DrawingEngine* engine = fDecorator->GetDrawingEngine();
		engine->LockParallelAccess();
		engine->ConstrainClippingRegion(visibleBorder);

		if (fIsZooming) {
			fDecorator->SetZoom(_ActionFor(message) == DEC_ZOOM);
		} else if (fIsClosing) {
			fDecorator->SetClose(_ActionFor(message) == DEC_CLOSE);
		} else if (fIsMinimizing) {
			fDecorator->SetMinimize(_ActionFor(message) == DEC_MINIMIZE);
		}

		engine->UnlockParallelAccess();
		fRegionPool.Recycle(visibleBorder);
	}

	BPoint delta = where - fLastMousePosition;
	// NOTE: "delta" is later used to change fLastMousePosition.
	// If for some reason no change should take effect, delta
	// is to be set to (0, 0) so that fLastMousePosition is not
	// adjusted. This way the relative mouse position to the
	// item being changed (border during resizing, tab during
	// sliding...) stays fixed when the mouse is moved so that
	// changes are taking effect again.

	// moving
	if (fIsDragging) {
		if (!(Flags() & B_NOT_MOVABLE)) {
			BPoint oldLeftTop = fFrame.LeftTop();

			fDesktop->MoveWindowBy(this, delta.x, delta.y);

			// constrain delta to true change in size
			delta = fFrame.LeftTop() - oldLeftTop;
		} else
			delta = BPoint(0, 0);
	}
	// resizing
	if (fIsResizing) {
		if (!(Flags() & B_NOT_RESIZABLE)) {
			if (Flags() & B_NOT_V_RESIZABLE)
				delta.y = 0;
			if (Flags() & B_NOT_H_RESIZABLE)
				delta.x = 0;

			BPoint oldRightBottom = fFrame.RightBottom();

			fDesktop->ResizeWindowBy(this, delta.x, delta.y);

			// constrain delta to true change in size
			delta = fFrame.RightBottom() - oldRightBottom;
		} else
			delta = BPoint(0, 0);
	}
	// sliding tab
	if (fIsSlidingTab) {
		float loc = TabLocation();
		// TODO: change to [0:1]
		loc += delta.x;
		if (fDesktop->SetWindowTabLocation(this, loc))
			delta.y = 0;
		else
			delta = BPoint(0, 0);
	}

	// NOTE: fLastMousePosition is currently only
	// used for window moving/resizing/sliding the tab
	fLastMousePosition += delta;

	// the window was moved, it doesn't come to
	// the front in FFM mode when the mouse is released
	fActivateOnMouseUp = false;

	// change focus in FFM mode
	DesktopSettings desktopSettings(fDesktop);
	if (desktopSettings.FocusFollowsMouse()
		&& !IsFocus() && !(Flags() & B_AVOID_FOCUS)) {
		fDesktop->SetFocusWindow(this);
	}

	// mouse cursor

	if (view != NULL) {
		view->MouseMoved(message, where);

		// TODO: there is more for real cursor support, ie. if a window is closed,
		//		new app cursor shouldn't override view cursor, ...
		ServerWindow()->App()->SetCurrentCursor(view->Cursor());
	}
}


// #pragma mark -


void
Window::WorkspaceActivated(int32 index, bool active)
{
	if (!active)
		fWindow->HandleDirectConnection(B_DIRECT_STOP);

	BMessage activatedMsg(B_WORKSPACE_ACTIVATED);
	activatedMsg.AddInt64("when", system_time());
	activatedMsg.AddInt32("workspace", index);
	activatedMsg.AddBool("active", active);

	ServerWindow()->SendMessageToClient(&activatedMsg);

	if (active)
		fWindow->HandleDirectConnection(B_DIRECT_START | B_BUFFER_RESET);
}


void
Window::WorkspacesChanged(uint32 oldWorkspaces, uint32 newWorkspaces)
{
	fWorkspaces = newWorkspaces;

	BMessage changedMsg(B_WORKSPACES_CHANGED);
	changedMsg.AddInt64("when", system_time());
	changedMsg.AddInt32("old", oldWorkspaces);
	changedMsg.AddInt32("new", newWorkspaces);

	ServerWindow()->SendMessageToClient(&changedMsg);
}


void
Window::Activated(bool active)
{
	BMessage msg(B_WINDOW_ACTIVATED);
	msg.AddBool("active", active);
	ServerWindow()->SendMessageToClient(&msg);
}


//# pragma mark -


void
Window::SetTitle(const char* name, BRegion& dirty)
{
	// rebuild the clipping for the title area
	// and redraw it.

	fTitle = name;

	if (fDecorator) {
		fDecorator->SetTitle(name, &dirty);

		fBorderRegionValid = false;
			// the border very likely changed
	}
}


void
Window::SetFocus(bool focus)
{
	// executed from Desktop thread
	// it holds the clipping write lock,
	// so the window thread cannot be
	// accessing fIsFocus

	BRegion* dirty = fRegionPool.GetRegion(fBorderRegion);
	if (dirty) {
		dirty->IntersectWith(&fVisibleRegion);
		fDesktop->MarkDirty(*dirty);
		fRegionPool.Recycle(dirty);
	}

	fIsFocus = focus;
	if (fDecorator)
		fDecorator->SetFocus(focus);

	Activated(focus);
}


void
Window::SetHidden(bool hidden)
{
	// the desktop takes care of dirty regions
	if (fHidden != hidden) {
		fHidden = hidden;

		fTopView->SetHidden(hidden);

		// TODO: anything else?
	}
}


void
Window::SetMinimized(bool minimized)
{
	if (minimized == fMinimized)
		return;

	fMinimized = minimized;
}


bool
Window::IsVisible() const
{
	if (IsOffscreenWindow())
		return true;

	if (IsHidden())
		return false;

/*
	if (fVisibleRegion.CountRects() == 0)
		return false;
*/
	return fCurrentWorkspace >= 0 && fCurrentWorkspace < kWorkingList;
}


void
Window::SetSizeLimits(int32 minWidth, int32 maxWidth,
	int32 minHeight, int32 maxHeight)
{
	if (minWidth < 0)
		minWidth = 0;

	if (minHeight < 0)
		minHeight = 0;

	fMinWidth = minWidth;
	fMaxWidth = maxWidth;
	fMinHeight = minHeight;
	fMaxHeight = maxHeight;

	// give the Decorator a say in this too
	if (fDecorator) {
		fDecorator->GetSizeLimits(&fMinWidth, &fMinHeight,
								  &fMaxWidth, &fMaxHeight);
	}

	_ObeySizeLimits();
}


void
Window::GetSizeLimits(int32* minWidth, int32* maxWidth,
	int32* minHeight, int32* maxHeight) const
{
	*minWidth = fMinWidth;
	*maxWidth = fMaxWidth;
	*minHeight = fMinHeight;
	*maxHeight = fMaxHeight;
}


bool
Window::SetTabLocation(float location, BRegion& dirty)
{
	bool ret = false;
	if (fDecorator) {
		ret = fDecorator->SetTabLocation(location, &dirty);
		// the border region changed if ret is true
		fBorderRegionValid = fBorderRegionValid && !ret;
	}
	return ret;
}


float
Window::TabLocation() const
{
	if (fDecorator)
		return fDecorator->TabLocation();
	return 0.0;
}


bool
Window::SetDecoratorSettings(const BMessage& settings, BRegion& dirty)
{
	bool ret = false;
	if (fDecorator) {
		ret = fDecorator->SetSettings(settings, &dirty);
		// the border region changed if ret is true
		fBorderRegionValid = fBorderRegionValid && !ret;
	}
	return ret;
}


bool
Window::GetDecoratorSettings(BMessage* settings)
{
	if (fDecorator)
		return fDecorator->GetSettings(settings);

	return false;
}


void
Window::FontsChanged(BRegion* updateRegion)
{
	if (fDecorator != NULL) {
		DesktopSettings settings(fDesktop);
		fDecorator->FontsChanged(settings, updateRegion);
		fBorderRegionValid = false;
	}
}


void
Window::SetLook(window_look look, BRegion* updateRegion)
{
	if (fDecorator == NULL && look != B_NO_BORDER_WINDOW_LOOK) {
		// we need a new decorator
		fDecorator = gDecorManager.AllocateDecorator(fDesktop, fDrawingEngine,
			Frame(), Title(), fLook, fFlags);
		if (IsFocus())
			fDecorator->SetFocus(true);
	}

	fLook = look;

	fBorderRegionValid = false;
		// the border very likely changed
	fContentRegionValid = false;
		// mabye a resize handle was added...
	fEffectiveDrawingRegionValid = false;
		// ...and therefor the drawing region is
		// likely not valid anymore either

	if (fDecorator != NULL) {
		DesktopSettings settings(fDesktop);
		fDecorator->SetLook(settings, look, updateRegion);

		// we might need to resize the window!
		fDecorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth, &fMaxHeight);
		_ObeySizeLimits();
	}

	if (look == B_NO_BORDER_WINDOW_LOOK) {
		// we don't need a decorator for this window
		delete fDecorator;
		fDecorator = NULL;
	}
}


void
Window::SetFeel(window_feel feel)
{
	// if the subset list is no longer needed, clear it
	if ((fFeel == B_MODAL_SUBSET_WINDOW_FEEL || fFeel == B_FLOATING_SUBSET_WINDOW_FEEL)
		&& (feel != B_MODAL_SUBSET_WINDOW_FEEL && feel != B_FLOATING_SUBSET_WINDOW_FEEL))
		fSubsets.MakeEmpty();

	fFeel = feel;

	// having modal windows with B_AVOID_FRONT or B_AVOID_FOCUS doesn't
	// make that much sense, so we filter those flags out on demand
	fFlags = fOriginalFlags;
	fFlags &= ValidWindowFlags(fFeel);

	if (!IsNormal()) {
		fFlags |= B_SAME_POSITION_IN_ALL_WORKSPACES;
		_PropagatePosition();
	}
}


void
Window::SetFlags(uint32 flags, BRegion* updateRegion)
{
	fOriginalFlags = flags;
	fFlags = flags & ValidWindowFlags(fFeel);
	if (!IsNormal())
		fFlags |= B_SAME_POSITION_IN_ALL_WORKSPACES;

	if ((fFlags & B_SAME_POSITION_IN_ALL_WORKSPACES) != 0)
		_PropagatePosition();

	if (fDecorator == NULL)
		return;

	fDecorator->SetFlags(flags, updateRegion);

	fBorderRegionValid = false;
		// the border might have changed (smaller/larger tab)

	// we might need to resize the window!
	if (fDecorator) {
		fDecorator->GetSizeLimits(&fMinWidth, &fMinHeight, &fMaxWidth, &fMaxHeight);
		_ObeySizeLimits();
	}
}


/*!	Returns wether or not a window is in the workspace list with the
	specified \a index.
*/
bool
Window::InWorkspace(int32 index) const
{
	return (fWorkspaces & (1UL << index)) != 0;
}


bool
Window::SupportsFront()
{
	if (fFeel == kDesktopWindowFeel
		|| fFeel == kMenuWindowFeel
		|| (fFlags & B_AVOID_FRONT) != 0)
		return false;

	return true;
}


bool
Window::IsModal() const
{
	return IsModalFeel(fFeel);
}


bool
Window::IsFloating() const
{
	return IsFloatingFeel(fFeel);
}


bool
Window::IsNormal() const
{
	return !IsFloatingFeel(fFeel) && !IsModalFeel(fFeel);
}


bool
Window::HasModal() const
{
	for (Window* window = NextWindow(fCurrentWorkspace); window != NULL;
			window = window->NextWindow(fCurrentWorkspace)) {
		if (window->IsHidden() || !window->IsModal())
			continue;

		if (window->HasInSubset(this))
			return true;
	}

	return false;
}


/*!	\brief Returns the windows that's in behind of the backmost position
		this window can get.
	Returns NULL is this window can be the backmost window.

	\param workspace the workspace on which this check should be made. If
		the value is -1, the window's current workspace will be used.
*/
Window*
Window::Backmost(Window* window, int32 workspace)
{
	if (workspace == -1)
		workspace = fCurrentWorkspace;

	ASSERT(workspace != -1);
	if (workspace == -1)
		return NULL;

	// Desktop windows are always backmost
	if (fFeel == kDesktopWindowFeel)
		return NULL;

	if (window == NULL)
		window = PreviousWindow(workspace);

	for (; window != NULL; window = window->PreviousWindow(workspace)) {
		if (window->IsHidden() || window == this)
			continue;

		if (HasInSubset(window))
			return window;
	}

	return NULL;
}


/*!	\brief Returns the windows that's in front of the frontmost position
		this window can get.
	Returns NULL if this window can be the frontmost window.

	\param workspace the workspace on which this check should be made. If
		the value is -1, the window's current workspace will be used.
*/
Window*
Window::Frontmost(Window* first, int32 workspace)
{
	if (workspace == -1)
		workspace = fCurrentWorkspace;

	ASSERT(workspace != -1);
	if (workspace == -1)
		return NULL;

	if (fFeel == kDesktopWindowFeel)
		return first ? first : NextWindow(workspace);

	if (first == NULL)
		first = NextWindow(workspace);

	for (Window* window = first; window != NULL;
			window = window->NextWindow(workspace)) {
		if (window->IsHidden() || window == this)
			continue;

		if (window->HasInSubset(this))
			return window;
	}

	return NULL;
}


bool
Window::AddToSubset(Window* window)
{
	return fSubsets.AddItem(window);
}


void
Window::RemoveFromSubset(Window* window)
{
	fSubsets.RemoveItem(window);
}


/*!	Returns whether or not a window is in the subset of this window.
	If a window is in the subset of this window, it means it should always
	appear behind this window.
*/
bool
Window::HasInSubset(const Window* window) const
{
	if (window == NULL || fFeel == window->Feel()
		|| fFeel == B_NORMAL_WINDOW_FEEL)
		return false;

	// Menus are a special case: they will always be on-top of every window
	// of their application
	if (fFeel == kMenuWindowFeel)
		return window->ServerWindow()->App() == ServerWindow()->App();
	if (window->Feel() == kMenuWindowFeel)
		return false;

	// we have a few special feels that have a fixed order

	const int32 kFeels[] = {kPasswordWindowFeel, kWindowScreenFeel,
		B_MODAL_ALL_WINDOW_FEEL, B_FLOATING_ALL_WINDOW_FEEL};

	for (uint32 order = 0;
			order < sizeof(kFeels) / sizeof(kFeels[0]); order++) {
		if (fFeel == kFeels[order])
			return true;
		if (window->Feel() == kFeels[order])
			return false;
	}

	if ((fFeel == B_FLOATING_APP_WINDOW_FEEL
			&& window->Feel() != B_MODAL_APP_WINDOW_FEEL)
		|| fFeel == B_MODAL_APP_WINDOW_FEEL)
		return window->ServerWindow()->App() == ServerWindow()->App();

	return fSubsets.HasItem(window);
}


/*!	\brief Collects all workspaces views in this window and puts it into \a list
*/
void
Window::FindWorkspacesViews(BObjectList<WorkspacesView>& list) const
{
	int32 count = fWorkspacesViewCount;
	fTopView->FindViews(kWorkspacesViewFlag, (BObjectList<View>&)list, count);
}


/*!	\brief Returns on which workspaces the window should be visible.

	A modal or floating window may be visible on a workspace if one
	of its subset windows is visible there. Floating windows also need
	to have a subset as front window to be visible.
*/
uint32
Window::SubsetWorkspaces() const
{
	if (fFeel == B_MODAL_ALL_WINDOW_FEEL
		|| fFeel == B_FLOATING_ALL_WINDOW_FEEL)
		return B_ALL_WORKSPACES;

	if (fFeel == B_FLOATING_APP_WINDOW_FEEL) {
		Window* front = fDesktop->FrontWindow();
		if (front != NULL && front->IsNormal()
			&& front->ServerWindow()->App() == ServerWindow()->App())
			return ServerWindow()->App()->Workspaces();

		return 0;
	}

	if (fFeel == B_MODAL_APP_WINDOW_FEEL) {
		uint32 workspaces = ServerWindow()->App()->Workspaces();
		if (workspaces == 0) {
			// The application doesn't seem to have any other windows
			// open or visible - but we'd like to see modal windows
			// anyway, at least when they are first opened.
			return 1UL << fDesktop->CurrentWorkspace();
		}
		return workspaces;
	}

	if (fFeel == B_MODAL_SUBSET_WINDOW_FEEL
		|| fFeel == B_FLOATING_SUBSET_WINDOW_FEEL) {
		uint32 workspaces = 0;
		bool hasNormalFront = false;
		for (int32 i = 0; i < fSubsets.CountItems(); i++) {
			Window* window = fSubsets.ItemAt(i);

			if (!window->IsHidden())
				workspaces |= window->Workspaces();
			if (window == fDesktop->FrontWindow() && window->IsNormal())
				hasNormalFront = true;
		}

		if (fFeel == B_FLOATING_SUBSET_WINDOW_FEEL && !hasNormalFront)
			return 0;

		return workspaces;
	}

	return 0;
}


/*!	Returns wether or not a window is in the subset workspace list with the
	specified \a index.
	See SubsetWorkspaces().
*/
bool
Window::InSubsetWorkspace(int32 index) const
{
	return (SubsetWorkspaces() & (1UL << index)) != 0;
}


// #pragma mark - static


/*static*/
bool
Window::IsValidLook(window_look look)
{
	return look == B_TITLED_WINDOW_LOOK
		|| look == B_DOCUMENT_WINDOW_LOOK
		|| look == B_MODAL_WINDOW_LOOK
		|| look == B_FLOATING_WINDOW_LOOK
		|| look == B_BORDERED_WINDOW_LOOK
		|| look == B_NO_BORDER_WINDOW_LOOK
		|| look == kDesktopWindowLook
		|| look == kLeftTitledWindowLook;
}


/*static*/
bool
Window::IsValidFeel(window_feel feel)
{
	return feel == B_NORMAL_WINDOW_FEEL
		|| feel == B_MODAL_SUBSET_WINDOW_FEEL
		|| feel == B_MODAL_APP_WINDOW_FEEL
		|| feel == B_MODAL_ALL_WINDOW_FEEL
		|| feel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| feel == B_FLOATING_APP_WINDOW_FEEL
		|| feel == B_FLOATING_ALL_WINDOW_FEEL
		|| feel == kDesktopWindowFeel
		|| feel == kMenuWindowFeel
		|| feel == kWindowScreenFeel
		|| feel == kPasswordWindowFeel;
}


/*static*/
bool
Window::IsModalFeel(window_feel feel)
{
	return feel == B_MODAL_SUBSET_WINDOW_FEEL
		|| feel == B_MODAL_APP_WINDOW_FEEL
		|| feel == B_MODAL_ALL_WINDOW_FEEL;
}


/*static*/
bool
Window::IsFloatingFeel(window_feel feel)
{
	return feel == B_FLOATING_SUBSET_WINDOW_FEEL
		|| feel == B_FLOATING_APP_WINDOW_FEEL
		|| feel == B_FLOATING_ALL_WINDOW_FEEL;
}


/*static*/
uint32
Window::ValidWindowFlags()
{
	return B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
		| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
		| B_NOT_H_RESIZABLE | B_NOT_V_RESIZABLE
		| B_AVOID_FRONT | B_AVOID_FOCUS
		| B_WILL_ACCEPT_FIRST_CLICK | B_OUTLINE_RESIZE
		| B_NO_WORKSPACE_ACTIVATION
		| B_NOT_ANCHORED_ON_ACTIVATE
		| B_ASYNCHRONOUS_CONTROLS
		| B_QUIT_ON_WINDOW_CLOSE
		| B_SAME_POSITION_IN_ALL_WORKSPACES
		| kWindowScreenFlag;
}


/*static*/
uint32
Window::ValidWindowFlags(window_feel feel)
{
	uint32 flags = ValidWindowFlags();
	if (IsModalFeel(feel))
		return flags & ~(B_AVOID_FOCUS | B_AVOID_FRONT);

	return flags;
}


// #pragma mark - private


// _ShiftPartOfRegion
void
Window::_ShiftPartOfRegion(BRegion* region, BRegion* regionToShift,
	int32 xOffset, int32 yOffset)
{
	BRegion* common = fRegionPool.GetRegion(*regionToShift);
	if (!common)
		return;
	// see if there is a common part at all
	common->IntersectWith(region);
	if (common->CountRects() > 0) {
		// cut the common part from the region,
		// offset that to destination and include again
		region->Exclude(common);
		common->OffsetBy(xOffset, yOffset);
		region->Include(common);
	}
	fRegionPool.Recycle(common);
}


void
Window::_TriggerContentRedraw(BRegion& dirtyContentRegion)
{
	if (IsVisible() && dirtyContentRegion.CountRects() > 0) {
		// put this into the pending dirty region
		// to eventually trigger a client redraw
		bool wasExpose = fPendingUpdateSession->IsExpose();
		BRegion* backgroundClearingRegion = &dirtyContentRegion;

		_TransferToUpdateSession(&dirtyContentRegion);

		if (fPendingUpdateSession->IsExpose()) {
			if (!fContentRegionValid)
				_UpdateContentRegion();

			if (!wasExpose) {
				// there was suddenly added a dirty region
				// caused by exposing content, we need to clear
				// the entire background
				backgroundClearingRegion
					= &(fPendingUpdateSession->DirtyRegion());
			}

			if (fDrawingEngine->LockParallelAccess()) {
				bool copyToFrontEnabled = fDrawingEngine->CopyToFrontEnabled();
				fDrawingEngine->SetCopyToFrontEnabled(true);
				fDrawingEngine->SuspendAutoSync();

//sCurrentColor.red = rand() % 255;
//sCurrentColor.green = rand() % 255;
//sCurrentColor.blue = rand() % 255;
//sPendingColor.red = rand() % 255;
//sPendingColor.green = rand() % 255;
//sPendingColor.blue = rand() % 255;
//fDrawingEngine->FillRegion(*backgroundClearingRegion, sCurrentColor);
//snooze(10000);

				fTopView->Draw(fDrawingEngine, backgroundClearingRegion,
					&fContentRegion, true);

				fDrawingEngine->Sync();
				fDrawingEngine->SetCopyToFrontEnabled(copyToFrontEnabled);
				fDrawingEngine->UnlockParallelAccess();
			}
		}
	}
}


void
Window::_DrawBorder()
{
	// this is executed in the window thread, but only
	// in respond to a REDRAW message having been received, the
	// clipping lock is held for reading

	if (!fDecorator)
		return;

	// construct the region of the border that needs redrawing
	BRegion* dirtyBorderRegion = fRegionPool.GetRegion();
	if (!dirtyBorderRegion)
		return;
	GetBorderRegion(dirtyBorderRegion);
	// intersect with our visible region
	dirtyBorderRegion->IntersectWith(&fVisibleRegion);
	// intersect with the dirty region
	dirtyBorderRegion->IntersectWith(&fDirtyRegion);

	DrawingEngine* engine = fDecorator->GetDrawingEngine();
	if (dirtyBorderRegion->CountRects() > 0 && engine->LockParallelAccess()) {
		engine->ConstrainClippingRegion(dirtyBorderRegion);
		bool copyToFrontEnabled = engine->CopyToFrontEnabled();
		engine->SetCopyToFrontEnabled(true);

		fDecorator->Draw(dirtyBorderRegion->Frame());

		engine->SetCopyToFrontEnabled(copyToFrontEnabled);

// TODO: remove this once the DrawState stuff is handled
// more cleanly. The reason why this is needed is that
// when the decorator draws strings, a draw state is set
// on the Painter object, and this is were it might get
// out of sync with what the ServerWindow things is the
// current DrawState set on the Painter
fWindow->ResyncDrawState();

		engine->UnlockParallelAccess();
	}
	fRegionPool.Recycle(dirtyBorderRegion);
}


/*!
	pre: the clipping is readlocked (this function is
	only called from _TriggerContentRedraw()), which
	in turn is only called from MessageReceived() with
	the clipping lock held
*/
void
Window::_TransferToUpdateSession(BRegion* contentDirtyRegion)
{
	if (contentDirtyRegion->CountRects() <= 0)
		return;

//fDrawingEngine->FillRegion(*contentDirtyRegion, sPendingColor);
//snooze(20000);

	// add to pending
	fPendingUpdateSession->SetUsed(true);
//	if (!fPendingUpdateSession->IsExpose())
	fPendingUpdateSession->AddCause(fDirtyCause);
	fPendingUpdateSession->Include(contentDirtyRegion);

	if (!fUpdateRequested) {
		// send this to client
		_SendUpdateMessage();
		// the pending region is now the current,
		// though the update does not start until
		// we received BEGIN_UPDATE from the client
	}
}

// _SendUpdateMessage
void
Window::_SendUpdateMessage()
{
	if (!fUpdatesEnabled)
		return;

	BMessage message(_UPDATE_);
	if (ServerWindow()->SendMessageToClient(&message) != B_OK) {
		// If sending the message failed, we'll just keep adding to the dirty
		// region until sending was successful.
		// TODO: we might want to automatically resend this message in this case
		return;
	}

	fUpdateRequested = true;
	fEffectiveDrawingRegionValid = false;
}


void
Window::BeginUpdate(BPrivate::PortLink& link)
{
	// NOTE: since we might "shift" parts of the
	// internal dirty regions from the desktop thread
	// in response to Window::ResizeBy(), which
	// might move arround views, the user of this function
	// needs to hold the global clipping lock so that the internal
	// dirty regions are not messed with from the Desktop thread
	// and ServerWindow thread at the same time.

	if (!fUpdateRequested) {
		link.StartMessage(B_ERROR);
		link.Flush();
		fprintf(stderr, "Window::BeginUpdate() - no update requested!\n");
		return;
	}

	// make the pending update session the current update session
	// (toggle the pointers)
	UpdateSession* temp = fCurrentUpdateSession;
	fCurrentUpdateSession = fPendingUpdateSession;
	fPendingUpdateSession = temp;
	fPendingUpdateSession->SetUsed(false);
	// all drawing command from the client
	// will have the dirty region from the update
	// session enforced
	fInUpdate = true;
	fEffectiveDrawingRegionValid = false;

	// TODO: each view could be drawn individually
	// right before carrying out the first drawing
	// command from the client during an update
	// (View::IsBackgroundDirty() can be used
	// for this)
	if (!fContentRegionValid)
		_UpdateContentRegion();

	BRegion* dirty = fRegionPool.GetRegion(
		fCurrentUpdateSession->DirtyRegion());
	if (!dirty) {
		link.StartMessage(B_ERROR);
		link.Flush();
		return;
	}

	dirty->IntersectWith(&VisibleContentRegion());

//if (!fCurrentUpdateSession->IsExpose()) {
////sCurrentColor.red = rand() % 255;
////sCurrentColor.green = rand() % 255;
////sCurrentColor.blue = rand() % 255;
////sPendingColor.red = rand() % 255;
////sPendingColor.green = rand() % 255;
////sPendingColor.blue = rand() % 255;
//fDrawingEngine->FillRegion(*dirty, sCurrentColor);
//snooze(10000);
//}

	link.StartMessage(B_OK);
	// append the current window geometry to the
	// message, the client will need it
	link.Attach<BPoint>(fFrame.LeftTop());
	link.Attach<float>(fFrame.Width());
	link.Attach<float>(fFrame.Height());
	// find and attach all views that intersect with
	// the dirty region
	fTopView->AddTokensForViewsInRegion(link, *dirty, &fContentRegion);
	// mark the end of the token "list"
	link.Attach<int32>(B_NULL_TOKEN);
	link.Flush();

	// supress back to front buffer copies in the drawing engine
	fDrawingEngine->SetCopyToFrontEnabled(false);

	if (!fCurrentUpdateSession->IsExpose() && fDrawingEngine->LockParallelAccess()) {
		fDrawingEngine->SuspendAutoSync();

		fTopView->Draw(fDrawingEngine, dirty, &fContentRegion, true);

		fDrawingEngine->Sync();
		fDrawingEngine->UnlockParallelAccess();
	} // else the background was cleared already

	fRegionPool.Recycle(dirty);
}


void
Window::EndUpdate()
{
	// NOTE: see comment in _BeginUpdate()

	if (fInUpdate) {
		// reenable copy to front
		fDrawingEngine->SetCopyToFrontEnabled(true);

		BRegion* dirty = fRegionPool.GetRegion(
			fCurrentUpdateSession->DirtyRegion());

		if (dirty) {
			dirty->IntersectWith(&VisibleContentRegion());

			fDrawingEngine->CopyToFront(*dirty);
			fRegionPool.Recycle(dirty);
		}

		fCurrentUpdateSession->SetUsed(false);

		fInUpdate = false;
		fEffectiveDrawingRegionValid = false;
	}
	if (fPendingUpdateSession->IsUsed()) {
		// send this to client
		_SendUpdateMessage();
	} else {
		fUpdateRequested = false;
	}
}


void
Window::_UpdateContentRegion()
{
	fContentRegion.Set(fFrame);

	// resize handle
	if (fDecorator) {
		if (!fBorderRegionValid)
			GetBorderRegion(&fBorderRegion);

		fContentRegion.Exclude(&fBorderRegion);
	}

	fContentRegionValid = true;
}


click_type
Window::_ActionFor(const BMessage* msg) const
{
	if (fDecorator == NULL)
		return DEC_NONE;

	BPoint where;
	if (msg->FindPoint("where", &where) != B_OK)
		return DEC_NONE;

	int32 buttons;
	if (msg->FindInt32("buttons", &buttons) != B_OK)
		buttons = 0;

	int32 modifiers;
	if (msg->FindInt32("modifiers", &modifiers) != B_OK)
		modifiers = 0;

	return fDecorator->Clicked(where, buttons, modifiers);
}


void
Window::_ObeySizeLimits()
{
	// make sure we even have valid size limits
	if (fMaxWidth < fMinWidth)
		fMaxWidth = fMinWidth;

	if (fMaxHeight < fMinHeight)
		fMaxHeight = fMinHeight;

	// Automatically resize the window to fit these new limits
	// if it does not already.

	// On R5, Windows don't automatically resize, but since
	// BWindow::ResizeTo() even honors the limits, I would guess
	// this is a bug that we don't have to adopt.
	// Note that most current apps will do unnecessary resizing
	// after having set the limits, but the overhead is neglible.

	float minWidthDiff = fMinWidth - fFrame.Width();
	float minHeightDiff = fMinHeight - fFrame.Height();
	float maxWidthDiff = fMaxWidth - fFrame.Width();
	float maxHeightDiff = fMaxHeight - fFrame.Height();

	float xDiff = 0.0;
	if (minWidthDiff > 0.0)	// we're currently smaller than minWidth
		xDiff = minWidthDiff;
	else if (maxWidthDiff < 0.0) // we're currently larger than maxWidth
		xDiff = maxWidthDiff;

	float yDiff = 0.0;
	if (minHeightDiff > 0.0) // we're currently smaller than minHeight
		yDiff = minHeightDiff;
	else if (maxHeightDiff < 0.0) // we're currently larger than maxHeight
		yDiff = maxHeightDiff;

	if (fDesktop)
		fDesktop->ResizeWindowBy(this, xDiff, yDiff);
	else
		ResizeBy((int32)xDiff, (int32)yDiff, NULL);
}


// #pragma mark - UpdateSession


Window::UpdateSession::UpdateSession()
	: fDirtyRegion(),
	  fInUse(false),
	  fCause(0)
{
}


Window::UpdateSession::~UpdateSession()
{
}


void
Window::UpdateSession::Include(BRegion* additionalDirty)
{
	fDirtyRegion.Include(additionalDirty);
}


void
Window::UpdateSession::Exclude(BRegion* dirtyInNextSession)
{
	fDirtyRegion.Exclude(dirtyInNextSession);
}


void
Window::UpdateSession::MoveBy(int32 x, int32 y)
{
	fDirtyRegion.OffsetBy(x, y);
}


void
Window::UpdateSession::SetUsed(bool used)
{
	fInUse = used;
	if (!fInUse) {
		fDirtyRegion.MakeEmpty();
		fCause = 0;
	}
}


void
Window::UpdateSession::AddCause(uint8 cause)
{
	fCause |= cause;
}


