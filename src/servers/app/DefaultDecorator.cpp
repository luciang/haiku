/*
 * Copyright 2001-2008, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 */

/*!	Default and fallback decorator for the app_server - the yellow tabs */


#include "DefaultDecorator.h"

#include "BitmapDrawingEngine.h"
#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "FontManager.h"
#include "PatternHandler.h"
#include "ServerBitmap.h"

#include <WindowPrivate.h>

#include <Autolock.h>
#include <Rect.h>
#include <View.h>

#include <new>
#include <stdio.h>

//#define DEBUG_DECORATOR
#ifdef DEBUG_DECORATOR
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


static inline uint8
blend_color_value(uint8 a, uint8 b, float position)
{
	int16 delta = (int16)b - a;
	int32 value = a + (int32)(position * delta);
	if (value > 255)
		return 255;
	if (value < 0)
		return 0;

	return (uint8)value;
}


/*!
	\brief Function mostly for calculating gradient colors
	\param col Start color
	\param col2 End color
	\param position A floating point number such that 0.0 <= position <= 1.0. 0.0 results in the
		start color and 1.0 results in the end color.
	\return The blended color. If an invalid position was given, {0,0,0,0} is returned.
*/
static rgb_color
make_blend_color(rgb_color colorA, rgb_color colorB, float position)
{
	if (position < 0)
		return colorA;
	if (position > 1)
		return colorB;

	rgb_color blendColor;
	blendColor.red = blend_color_value(colorA.red, colorB.red, position);
	blendColor.green = blend_color_value(colorA.green, colorB.green, position);
	blendColor.blue = blend_color_value(colorA.blue, colorB.blue, position);
	blendColor.alpha = blend_color_value(colorA.alpha, colorB.alpha, position);

	return blendColor;
}


//	#pragma mark -


// TODO: get rid of DesktopSettings here, and introduce private accessor
//	methods to the Decorator base class

DefaultDecorator::DefaultDecorator(DesktopSettings& settings, BRect rect,
		window_look look, uint32 flags)
	: Decorator(settings, rect, look, flags),
	fTabOffset(0),
	fTabLocation(0.0),
	fLastClicked(0)
{
	_UpdateFont(settings);

	// common colors to both focus and non focus state
	fFrameColors[0] = (rgb_color){ 152, 152, 152, 255 };
	fFrameColors[1] = (rgb_color){ 255, 255, 255, 255 };
	fFrameColors[4] = (rgb_color){ 152, 152, 152, 255 };
	fFrameColors[5] = (rgb_color){ 96, 96, 96, 255 };

	// state based colors
	fFocusFrameColors[0] = (rgb_color){ 216, 216, 216, 255 };
	fFocusFrameColors[1] = (rgb_color){ 136, 136, 136, 255 };
	fNonFocusFrameColors[0] = (rgb_color){ 232, 232, 232, 255 };
	fNonFocusFrameColors[1] = (rgb_color){ 148, 148, 148, 255 };

	fFocusTabColor = UIColor(B_WINDOW_TAB_COLOR);
	fFocusTextColor = UIColor(B_WINDOW_TEXT_COLOR);
	fNonFocusTabColor = UIColor(B_WINDOW_INACTIVE_TAB_COLOR);
	fNonFocusTextColor = UIColor(B_WINDOW_INACTIVE_TEXT_COLOR);

	fCloseBitmaps[0] = fCloseBitmaps[1] = fCloseBitmaps[2] = fCloseBitmaps[3]
		= fZoomBitmaps[0] = fZoomBitmaps[1] = fZoomBitmaps[2] = fZoomBitmaps[3]
		= NULL;

	// Set appropriate colors based on the current focus value. In this case, each decorator
	// defaults to not having the focus.
	_SetFocus();

	// Do initial decorator setup
	_DoLayout();

	// ToDo: if the decorator was created with a frame too small, it should resize itself!

	STRACE(("DefaultDecorator:\n"));
	STRACE(("\tFrame (%.1f,%.1f,%.1f,%.1f)\n",
		rect.left, rect.top, rect.right, rect.bottom));
}


DefaultDecorator::~DefaultDecorator()
{
	STRACE(("DefaultDecorator: ~DefaultDecorator()\n"));
}


void
DefaultDecorator::SetTitle(const char* string, BRegion* updateRegion)
{
	// TODO: we could be much smarter about the update region

	BRect rect = TabRect();

	Decorator::SetTitle(string);

	if (updateRegion == NULL)
		return;

	BRect updatedRect = TabRect();
	if (rect.left > updatedRect.left)
		rect.left = updatedRect.left;
	if (rect.right < updatedRect.right)
		rect.right = updatedRect.right;

	rect.bottom++;
		// the border will look differently when the title is adjacent

	updateRegion->Include(rect);
}


void
DefaultDecorator::FontsChanged(DesktopSettings& settings, BRegion* updateRegion)
{
	// get previous extent
	if (updateRegion != NULL) {
		BRegion extent;
		GetFootprint(&extent);
		updateRegion->Include(&extent);
	}

	_UpdateFont(settings);
	_DoLayout();

	if (updateRegion != NULL) {
		BRegion extent;
		GetFootprint(&extent);
		updateRegion->Include(&extent);
	}
}


void
DefaultDecorator::SetLook(DesktopSettings& settings, window_look look,
	BRegion* updateRegion)
{
	// TODO: we could be much smarter about the update region

	// get previous extent
	if (updateRegion != NULL) {
		BRegion extent;
		GetFootprint(&extent);
		updateRegion->Include(&extent);
	}

	fLook = look;

	_UpdateFont(settings);
	_DoLayout();

	if (updateRegion != NULL) {
		BRegion extent;
		GetFootprint(&extent);
		updateRegion->Include(&extent);
	}
}


void
DefaultDecorator::SetFlags(uint32 flags, BRegion* updateRegion)
{
	// TODO: we could be much smarter about the update region

	// get previous extent
	if (updateRegion != NULL) {
		BRegion extent;
		GetFootprint(&extent);
		updateRegion->Include(&extent);
	}

	Decorator::SetFlags(flags, updateRegion);
	_DoLayout();

	if (updateRegion != NULL) {
		BRegion extent;
		GetFootprint(&extent);
		updateRegion->Include(&extent);
	}
}


void
DefaultDecorator::MoveBy(BPoint offset)
{
	STRACE(("DefaultDecorator: Move By (%.1f, %.1f)\n", offset.x, offset.y));
	// Move all internal rectangles the appropriate amount
	fFrame.OffsetBy(offset);
	fCloseRect.OffsetBy(offset);
	fTabRect.OffsetBy(offset);
	fResizeRect.OffsetBy(offset);
	fZoomRect.OffsetBy(offset);
	fBorderRect.OffsetBy(offset);

	fLeftBorder.OffsetBy(offset);
	fRightBorder.OffsetBy(offset);
	fTopBorder.OffsetBy(offset);
	fBottomBorder.OffsetBy(offset);
}


void
DefaultDecorator::ResizeBy(BPoint offset, BRegion* dirty)
{
	STRACE(("DefaultDecorator: Resize By (%.1f, %.1f)\n", offset.x, offset.y));
	// Move all internal rectangles the appropriate amount
	fFrame.right += offset.x;
	fFrame.bottom += offset.y;

	// handle invalidation of resize rect
	if (dirty && !(fFlags & B_NOT_RESIZABLE)) {
		BRect realResizeRect;
		switch (fLook) {
			case B_DOCUMENT_WINDOW_LOOK:
				realResizeRect = fResizeRect;
				// resize rect at old location
				dirty->Include(realResizeRect);
				realResizeRect.OffsetBy(offset);
				// resize rect at new location
				dirty->Include(realResizeRect);
				break;
			case B_TITLED_WINDOW_LOOK:
			case B_FLOATING_WINDOW_LOOK:
			case B_MODAL_WINDOW_LOOK:
			case kLeftTitledWindowLook:
				realResizeRect.Set(fRightBorder.right - 22, fBottomBorder.top,
					fRightBorder.right - 22, fBottomBorder.bottom - 1);
				// resize rect at old location
				dirty->Include(realResizeRect);
				realResizeRect.OffsetBy(offset);
				// resize rect at new location
				dirty->Include(realResizeRect);

				realResizeRect.Set(fRightBorder.left, fBottomBorder.bottom - 22,
					fRightBorder.right - 1, fBottomBorder.bottom - 22);
				// resize rect at old location
				dirty->Include(realResizeRect);
				realResizeRect.OffsetBy(offset);
				// resize rect at new location
				dirty->Include(realResizeRect);
				break;
			default:
				break;
		}
	}

	fResizeRect.OffsetBy(offset);

	fBorderRect.right += offset.x;
	fBorderRect.bottom += offset.y;

	fLeftBorder.bottom += offset.y;
	fTopBorder.right += offset.x;

	fRightBorder.OffsetBy(offset.x, 0.0);
	fRightBorder.bottom	+= offset.y;

	fBottomBorder.OffsetBy(0.0, offset.y);
	fBottomBorder.right	+= offset.x;

	if (dirty) {
		if (offset.x > 0.0) {
			BRect t(fRightBorder.left - offset.x, fTopBorder.top,
				fRightBorder.right, fTopBorder.bottom);
			dirty->Include(t);
			t.Set(fRightBorder.left - offset.x, fBottomBorder.top,
				fRightBorder.right, fBottomBorder.bottom);
			dirty->Include(t);
			dirty->Include(fRightBorder);
		} else if (offset.x < 0.0) {
			dirty->Include(BRect(fRightBorder.left, fTopBorder.top,
				fRightBorder.right, fBottomBorder.bottom));
		}
		if (offset.y > 0.0) {
			BRect t(fLeftBorder.left, fLeftBorder.bottom - offset.y,
				fLeftBorder.right, fLeftBorder.bottom);
			dirty->Include(t);
			t.Set(fRightBorder.left, fRightBorder.bottom - offset.y,
				fRightBorder.right, fRightBorder.bottom);
			dirty->Include(t);
			dirty->Include(fBottomBorder);
		} else if (offset.y < 0.0) {
			dirty->Include(fBottomBorder);
		}
	}

	// resize tab and layout tab items
	if (fTabRect.IsValid()) {
		BRect oldTabRect(fTabRect);

		float tabSize;
		float maxLocation;
		if (fLook != kLeftTitledWindowLook) {
			tabSize = fRightBorder.right - fLeftBorder.left;
		} else {
			tabSize = fBottomBorder.bottom - fTopBorder.top;
		}
		maxLocation = tabSize - fMaxTabSize;
		if (maxLocation < 0)
			maxLocation = 0;

		float tabOffset = floorf(fTabLocation * maxLocation);
		float delta = tabOffset - fTabOffset;
		fTabOffset = (uint32)tabOffset;
		if (fLook != kLeftTitledWindowLook)
			fTabRect.OffsetBy(delta, 0.0);
		else
			fTabRect.OffsetBy(0.0, delta);

		if (tabSize < fMinTabSize)
			tabSize = fMinTabSize;
		if (tabSize > fMaxTabSize)
			tabSize = fMaxTabSize;

		if (fLook != kLeftTitledWindowLook && tabSize != fTabRect.Width()) {
			fTabRect.right = fTabRect.left + tabSize;
		} else if (fLook == kLeftTitledWindowLook
			&& tabSize != fTabRect.Height()) {
			fTabRect.bottom = fTabRect.top + tabSize;
		}

		if (oldTabRect != fTabRect) {
			_LayoutTabItems(fTabRect);

			if (dirty) {
				// NOTE: the tab rect becoming smaller only would
				// handled be the Desktop anyways, so it is sufficient
				// to include it into the dirty region in it's
				// final state
				BRect redraw(fTabRect);
				if (delta != 0.0) {
					redraw = redraw | oldTabRect;
					if (fLook != kLeftTitledWindowLook)
						redraw.bottom++;
					else
						redraw.right++;
				}
				dirty->Include(redraw);
			}
		}
	}
}


bool
DefaultDecorator::SetTabLocation(float location, BRegion* updateRegion)
{
	STRACE(("DefaultDecorator: Set Tab Location(%.1f)\n", location));
	if (!fTabRect.IsValid())
		return false;

	if (location < 0)
		location = 0;

	float maxLocation
		= fRightBorder.right - fLeftBorder.left - fTabRect.Width();
	if (location > maxLocation)
		location = maxLocation;

	float delta = location - fTabOffset;
	if (delta == 0.0)
		return false;

	// redraw old rect (1 pix on the border also must be updated)
	BRect trect(fTabRect);
	trect.bottom++;
	updateRegion->Include(trect);

	fTabRect.OffsetBy(delta, 0);
	fTabOffset = (int32)location;
	_LayoutTabItems(fTabRect);

	fTabLocation = maxLocation > 0.0 ? fTabOffset / maxLocation : 0.0;

	// redraw new rect as well
	trect = fTabRect;
	trect.bottom++;
	updateRegion->Include(trect);
	return true;
}


bool
DefaultDecorator::SetSettings(const BMessage& settings, BRegion* updateRegion)
{
	float tabLocation;
	if (settings.FindFloat("tab location", &tabLocation) == B_OK)
		return SetTabLocation(tabLocation, updateRegion);

	return false;
}


bool
DefaultDecorator::GetSettings(BMessage* settings) const
{
	if (!fTabRect.IsValid())
		return false;

	if (settings->AddRect("tab frame", fTabRect) != B_OK)
		return false;

	return settings->AddFloat("tab location", (float)fTabOffset) == B_OK;
}


// #pragma mark -


void
DefaultDecorator::Draw(BRect update)
{
	STRACE(("DefaultDecorator: Draw(%.1f,%.1f,%.1f,%.1f)\n",
		update.left, update.top, update.right, update.bottom));

	// We need to draw a few things: the tab, the resize thumb, the borders,
	// and the buttons
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(update);
	_DrawTab(update);
}


void
DefaultDecorator::Draw()
{
	// Easy way to draw everything - no worries about drawing only certain
	// things
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom()));
	_DrawTab(fTabRect);
}


void
DefaultDecorator::GetSizeLimits(int32* minWidth, int32* minHeight,
	int32* maxWidth, int32* maxHeight) const
{
	if (fTabRect.IsValid()) {
		*minWidth = (int32)roundf(max_c(*minWidth,
			fMinTabSize - 2 * fBorderWidth));
	}
	if (fResizeRect.IsValid()) {
		*minHeight = (int32)roundf(max_c(*minHeight,
			fResizeRect.Height() - fBorderWidth));
	}
}


void
DefaultDecorator::GetFootprint(BRegion* region)
{
	STRACE(("DefaultDecorator: Get Footprint\n"));
	// This function calculates the decorator's footprint in coordinates
	// relative to the view. This is most often used to set a Window
	// object's visible region.
	if (!region)
		return;

	region->MakeEmpty();

	if (fLook == B_NO_BORDER_WINDOW_LOOK)
		return;

	region->Include(fTopBorder);
	region->Include(fLeftBorder);
	region->Include(fRightBorder);
	region->Include(fBottomBorder);

	if (fLook == B_BORDERED_WINDOW_LOOK)
		return;

	region->Include(fTabRect);

	if (fLook == B_DOCUMENT_WINDOW_LOOK) {
		// include the rectangular resize knob on the bottom right
		region->Include(BRect(fFrame.right - 13.0f, fFrame.bottom - 13.0f,
			fFrame.right, fFrame.bottom));
	}
}


click_type
DefaultDecorator::Clicked(BPoint point, int32 buttons, int32 modifiers)
{
#ifdef DEBUG_DECORATOR
	printf("DefaultDecorator: Clicked\n");
	printf("\tPoint: (%.1f,%.1f)\n", point.x, point.y);
	printf("\tButtons: %ld, Modifiers: 0x%lx\n", buttons, modifiers);
#endif // DEBUG_DECORATOR

	// TODO: have a real double-click mechanism, ie. take user settings into
	// account
	bigtime_t now = system_time();
	if (buttons != 0) {
		fWasDoubleClick = now - fLastClicked < 200000;
		fLastClicked = now;
	}

	// In checking for hit test stuff, we start with the smallest rectangles
	// the user might be clicking on and gradually work our way out into larger
	// rectangles.
	if (!(fFlags & B_NOT_CLOSABLE) && fCloseRect.Contains(point))
		return DEC_CLOSE;

	if (!(fFlags & B_NOT_ZOOMABLE) && fZoomRect.Contains(point))
		return DEC_ZOOM;

	if (fLook == B_DOCUMENT_WINDOW_LOOK && fResizeRect.Contains(point))
		return DEC_RESIZE;

	bool clicked = false;

	// Clicking in the tab?
	if (fTabRect.Contains(point)) {
		// tab sliding in any case if either shift key is held down
		// except sliding up-down by moving mouse left-right would look strange
		if ((modifiers & B_SHIFT_KEY) && (fLook != kLeftTitledWindowLook))
			return DEC_SLIDETAB;

		clicked = true;
	} else if (fLeftBorder.Contains(point) || fRightBorder.Contains(point)
		|| fTopBorder.Contains(point) || fBottomBorder.Contains(point)) {
		// Clicked on border

		// check resize area
		if (!(fFlags & B_NOT_RESIZABLE)
			&& (fLook == B_TITLED_WINDOW_LOOK
				|| fLook == B_FLOATING_WINDOW_LOOK
				|| fLook == B_MODAL_WINDOW_LOOK
				|| fLook == kLeftTitledWindowLook)) {
			BRect temp(BPoint(fBottomBorder.right - 18,
				fBottomBorder.bottom - 18), fBottomBorder.RightBottom());
			if (temp.Contains(point))
				return DEC_RESIZE;
		}

		clicked = true;
	}

	if (clicked) {
		// NOTE: On R5, windows are not moved to back if clicked inside the
		// resize area with the second mouse button. So we check this after
		// the check above
		if (buttons == B_SECONDARY_MOUSE_BUTTON)
			return DEC_MOVETOBACK;

		if (fWasDoubleClick && !(fFlags & B_NOT_MINIMIZABLE))
			return DEC_MINIMIZE;

		return DEC_DRAG;
	}

	// Guess user didn't click anything
	return DEC_NONE;
}


void
DefaultDecorator::_DoLayout()
{
	STRACE(("DefaultDecorator: Do Layout\n"));
	// Here we determine the size of every rectangle that we use
	// internally when we are given the size of the client rectangle.

	bool hasTab = false;

	switch (Look()) {
		case B_MODAL_WINDOW_LOOK:
			fBorderWidth = 5;
			break;

		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
			hasTab = true;
			fBorderWidth = 5;
			break;
		case B_FLOATING_WINDOW_LOOK:
		case kLeftTitledWindowLook:
			hasTab = true;
			fBorderWidth = 3;
			break;

		case B_BORDERED_WINDOW_LOOK:
			fBorderWidth = 1;
			break;

		default:
			fBorderWidth = 0;
	}

	// calculate our tab rect
	if (hasTab) {
		// distance from one item of the tab bar to another.
		// In this case the text and close/zoom rects
		fTextOffset = (fLook == B_FLOATING_WINDOW_LOOK
			|| fLook == kLeftTitledWindowLook) ? 10 : 18;

		font_height fontHeight;
		fDrawState.Font().GetHeight(fontHeight);

		if (fLook != kLeftTitledWindowLook) {
			fTabRect.Set(fFrame.left - fBorderWidth,
				fFrame.top - fBorderWidth
					- ceilf(fontHeight.ascent + fontHeight.descent + 7.0),
				((fFrame.right - fFrame.left) < 35.0 ?
					fFrame.left + 35.0 : fFrame.right) + fBorderWidth,
				fFrame.top - fBorderWidth);
		} else {
			fTabRect.Set(fFrame.left - fBorderWidth
				- ceilf(fontHeight.ascent + fontHeight.descent + 5.0),
					fFrame.top - fBorderWidth, fFrame.left - fBorderWidth,
				fFrame.bottom + fBorderWidth);
		}

		// format tab rect for a floating window - make the rect smaller
		if (fLook == B_FLOATING_WINDOW_LOOK) {
			fTabRect.InsetBy(0, 2);
			fTabRect.OffsetBy(0, 2);
		}

		float offset;
		float size;
		float inset;
		_GetButtonSizeAndOffset(fTabRect, &offset, &size, &inset);

		// fMinTabSize contains just the room for the buttons
		fMinTabSize = inset * 2 + fTextOffset;
		if ((fFlags & B_NOT_CLOSABLE) == 0)
			fMinTabSize += offset + size;
		if ((fFlags & B_NOT_ZOOMABLE) == 0)
			fMinTabSize += offset + size;

		// fMaxTabSize contains fMinWidth + the width required for the title
		fMaxTabSize = fDrawingEngine
			? ceilf(fDrawingEngine->StringWidth(Title(), strlen(Title()),
				fDrawState.Font())) : 0.0;
		if (fMaxTabSize > 0.0)
			fMaxTabSize += fTextOffset;
		fMaxTabSize += fMinTabSize;

		float tabSize = (fLook != kLeftTitledWindowLook
			? fFrame.Width() : fFrame.Height()) + fBorderWidth * 2;
		if (tabSize < fMinTabSize)
			tabSize = fMinTabSize;
		if (tabSize > fMaxTabSize)
			tabSize = fMaxTabSize;

		// layout buttons and truncate text
		if (fLook != kLeftTitledWindowLook)
			fTabRect.right = fTabRect.left + tabSize;
		else
			fTabRect.bottom = fTabRect.top + tabSize;
	} else {
		// no tab
		fMinTabSize = 0.0;
		fMaxTabSize = 0.0;
		fTabRect.Set(0.0, 0.0, -1.0, -1.0);
		fCloseRect.Set(0.0, 0.0, -1.0, -1.0);
		fZoomRect.Set(0.0, 0.0, -1.0, -1.0);
	}

	// calculate left/top/right/bottom borders
	if (fBorderWidth > 0) {
		// NOTE: no overlapping, the left and right border rects
		// don't include the corners!
		fLeftBorder.Set(fFrame.left - fBorderWidth, fFrame.top,
			fFrame.left - 1, fFrame.bottom);

		fRightBorder.Set(fFrame.right + 1, fFrame.top ,
			fFrame.right + fBorderWidth, fFrame.bottom);

		fTopBorder.Set(fFrame.left - fBorderWidth, fFrame.top - fBorderWidth,
			fFrame.right + fBorderWidth, fFrame.top - 1);

		fBottomBorder.Set(fFrame.left - fBorderWidth, fFrame.bottom + 1,
			fFrame.right + fBorderWidth, fFrame.bottom + fBorderWidth);
	} else {
		// no border
		fLeftBorder.Set(0.0, 0.0, -1.0, -1.0);
		fRightBorder.Set(0.0, 0.0, -1.0, -1.0);
		fTopBorder.Set(0.0, 0.0, -1.0, -1.0);
		fBottomBorder.Set(0.0, 0.0, -1.0, -1.0);
	}

	// calculate resize rect
	fResizeRect.Set(fBottomBorder.right - 18.0, fBottomBorder.bottom - 18.0,
		fBottomBorder.right, fBottomBorder.bottom);

	if (hasTab) {
		// make sure fTabOffset is within limits and apply it to
		// the fTabRect
		if (fTabOffset < 0)
			fTabOffset = 0;
		if (fTabLocation != 0.0
			&& fTabOffset > (fRightBorder.right - fLeftBorder.left
				- fTabRect.Width()))
			fTabOffset = uint32(fRightBorder.right - fLeftBorder.left
				- fTabRect.Width());
		fTabRect.OffsetBy(fTabOffset, 0);

		// finally, layout the buttons and text within the tab rect
		_LayoutTabItems(fTabRect);
	}
}


void
DefaultDecorator::_DrawFrame(BRect invalid)
{
	STRACE(("_DrawFrame(%f,%f,%f,%f)\n", invalid.left, invalid.top,
		invalid.right, invalid.bottom));

	// NOTE: the DrawingEngine needs to be locked for the entire
	// time for the clipping to stay valid for this decorator

	if (fLook == B_NO_BORDER_WINDOW_LOOK)
		return;

	if (fBorderWidth <= 0)
		return;

	// Draw the border frame
	BRect r = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());
	switch (fLook) {
		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
		case B_MODAL_WINDOW_LOOK:
		{
			// top
			if (invalid.Intersects(fTopBorder)) {
				for (int8 i = 0; i < 5; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.left + i, r.top + i),
						BPoint(r.right - i, r.top + i), fFrameColors[i]);
				}
				if (fTabRect.IsValid()) {
					// grey along the bottom of the tab
					// (overwrites "white" from frame)
					fDrawingEngine->StrokeLine(
						BPoint(fTabRect.left + 2, fTabRect.bottom + 1),
						BPoint(fTabRect.right - 2, fTabRect.bottom + 1),
						fFrameColors[2]);
				}
			}
			// left
			if (invalid.Intersects(fLeftBorder.InsetByCopy(0, -fBorderWidth))) {
				for (int8 i = 0; i < 5; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.left + i, r.top + i),
						BPoint(r.left + i, r.bottom - i), fFrameColors[i]);
				}
			}
			// bottom
			if (invalid.Intersects(fBottomBorder)) {
				for (int8 i = 0; i < 5; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.left + i, r.bottom - i),
						BPoint(r.right - i, r.bottom - i),
						fFrameColors[(4 - i) == 4 ? 5 : (4 - i)]);
				}
			}
			// right
			if (invalid.Intersects(fRightBorder.InsetByCopy(0, -fBorderWidth))) {
				for (int8 i = 0; i < 5; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.right - i, r.top + i),
						BPoint(r.right - i, r.bottom - i),
						fFrameColors[(4 - i) == 4 ? 5 : (4 - i)]);
				}
			}
			break;
		}

		case B_FLOATING_WINDOW_LOOK:
		case kLeftTitledWindowLook:
		{
			// top
			if (invalid.Intersects(fTopBorder)) {
				for (int8 i = 0; i < 3; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.left + i, r.top + i),
						BPoint(r.right - i, r.top + i), fFrameColors[i * 2]);
				}
				if (fTabRect.IsValid() && fLook != kLeftTitledWindowLook) {
					// grey along the bottom of the tab
					// (overwrites "white" from frame)
					fDrawingEngine->StrokeLine(
						BPoint(fTabRect.left + 2, fTabRect.bottom + 1),
						BPoint(fTabRect.right - 2, fTabRect.bottom + 1),
						fFrameColors[2]);
				}
			}
			// left
			if (invalid.Intersects(fLeftBorder.InsetByCopy(0, -fBorderWidth))) {
				for (int8 i = 0; i < 3; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.left + i, r.top + i),
						BPoint(r.left + i, r.bottom - i), fFrameColors[i * 2]);
				}
				if (fLook == kLeftTitledWindowLook && fTabRect.IsValid()) {
					// grey along the right side of the tab
					// (overwrites "white" from frame)
					fDrawingEngine->StrokeLine(
						BPoint(fTabRect.right + 1, fTabRect.top + 2),
						BPoint(fTabRect.right + 1, fTabRect.bottom - 2),
						fFrameColors[2]);
				}
			}
			// bottom
			if (invalid.Intersects(fBottomBorder)) {
				for (int8 i = 0; i < 3; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.left + i, r.bottom - i),
						BPoint(r.right - i, r.bottom - i),
						fFrameColors[(2 - i) == 2 ? 5 : (2 - i) * 2]);
				}
			}
			// right
			if (invalid.Intersects(fRightBorder.InsetByCopy(0, -fBorderWidth))) {
				for (int8 i = 0; i < 3; i++) {
					fDrawingEngine->StrokeLine(BPoint(r.right - i, r.top + i),
						BPoint(r.right - i, r.bottom - i),
						fFrameColors[(2 - i) == 2 ? 5 : (2 - i) * 2]);
				}
			}
			break;
		}

		case B_BORDERED_WINDOW_LOOK:
			fDrawingEngine->StrokeRect(r, fFrameColors[5]);
			break;

		default:
			// don't draw a border frame
			break;
	}

	// Draw the resize thumb if we're supposed to
	if (!(fFlags & B_NOT_RESIZABLE)) {
		r = fResizeRect;

		switch (fLook) {
			case B_DOCUMENT_WINDOW_LOOK:
			{
				if (!invalid.Intersects(r))
					break;

				float x = r.right - 3;
				float y = r.bottom - 3;

				fDrawingEngine->FillRect(BRect(x - 13, y - 13, x, y),
					fFrameColors[2]);
				fDrawingEngine->StrokeLine(BPoint(x - 15, y - 15),
					BPoint(x - 15, y - 2), fFrameColors[0]);
				fDrawingEngine->StrokeLine(BPoint(x - 14, y - 14),
					BPoint(x - 14, y - 1), fFrameColors[1]);
				fDrawingEngine->StrokeLine(BPoint(x - 15, y - 15),
					BPoint(x - 2, y - 15), fFrameColors[0]);
				fDrawingEngine->StrokeLine(BPoint(x - 14, y - 14),
					BPoint(x - 1, y - 14), fFrameColors[1]);

				if (!IsFocus())
					break;

				for (int8 i = 1; i <= 4; i++) {
					for (int8 j = 1; j <= i; j++) {
						BPoint pt1(x - (3 * j) + 1, y - (3 * (5 - i)) + 1);
						BPoint pt2(x - (3 * j) + 2, y - (3 * (5 - i)) + 2);
						fDrawingEngine->StrokePoint(pt1, fFrameColors[0]);
						fDrawingEngine->StrokePoint(pt2, fFrameColors[1]);
					}
				}
				break;
			}

			case B_TITLED_WINDOW_LOOK:
			case B_FLOATING_WINDOW_LOOK:
			case B_MODAL_WINDOW_LOOK:
			case kLeftTitledWindowLook:
			{
				if (!invalid.Intersects(BRect(fRightBorder.right - 22,
					fBottomBorder.bottom - 22, fRightBorder.right - 1,
					fBottomBorder.bottom - 1)))
					break;

				fDrawingEngine->StrokeLine(
					BPoint(fRightBorder.left, fBottomBorder.bottom - 22),
					BPoint(fRightBorder.right - 1, fBottomBorder.bottom - 22),
					fFrameColors[0]);
				fDrawingEngine->StrokeLine(
					BPoint(fRightBorder.right - 22, fBottomBorder.top),
					BPoint(fRightBorder.right - 22, fBottomBorder.bottom - 1),
					fFrameColors[0]);
				break;
			}

			default:
				// don't draw resize corner
				break;
		}
	}
}


void
DefaultDecorator::_DrawTab(BRect invalid)
{
	STRACE(("_DrawTab(%.1f,%.1f,%.1f,%.1f)\n",
			invalid.left, invalid.top, invalid.right, invalid.bottom));
	// If a window has a tab, this will draw it and any buttons which are
	// in it.
	if (!fTabRect.IsValid() || !invalid.Intersects(fTabRect))
		return;

	// outer frame
	fDrawingEngine->StrokeLine(fTabRect.LeftTop(), fTabRect.LeftBottom(),
		fFrameColors[0]);
	fDrawingEngine->StrokeLine(fTabRect.LeftTop(), fTabRect.RightTop(),
		fFrameColors[0]);
	if (fLook != kLeftTitledWindowLook) {
		fDrawingEngine->StrokeLine(fTabRect.RightTop(), fTabRect.RightBottom(),
			fFrameColors[5]);
	} else {
		fDrawingEngine->StrokeLine(fTabRect.LeftBottom(),
			fTabRect.RightBottom(), fFrameColors[5]);
	}

	// bevel
	fDrawingEngine->StrokeLine(BPoint(fTabRect.left + 1, fTabRect.top + 1),
		BPoint(fTabRect.left + 1,
			fTabRect.bottom - (fLook == kLeftTitledWindowLook ? 1 : 0)),
		fTabColorLight);
	fDrawingEngine->StrokeLine(BPoint(fTabRect.left + 1, fTabRect.top + 1),
		BPoint(fTabRect.right - (fLook == kLeftTitledWindowLook ? 0 : 1),
			fTabRect.top + 1),
		fTabColorLight);

	if (fLook != kLeftTitledWindowLook) {
		fDrawingEngine->StrokeLine(BPoint(fTabRect.right - 1, fTabRect.top + 2),
			BPoint(fTabRect.right - 1, fTabRect.bottom), fTabColorShadow);
	} else {
		fDrawingEngine->StrokeLine(
			BPoint(fTabRect.left + 2, fTabRect.bottom - 1),
			BPoint(fTabRect.right, fTabRect.bottom - 1), fTabColorShadow);
	}

	// fill
	if (fLook != kLeftTitledWindowLook) {
		fDrawingEngine->FillRect(BRect(fTabRect.left + 2, fTabRect.top + 2,
			fTabRect.right - 2, fTabRect.bottom), fTabColor);
	} else {
		fDrawingEngine->FillRect(BRect(fTabRect.left + 2, fTabRect.top + 2,
			fTabRect.right, fTabRect.bottom - 2), fTabColor);
	}

	_DrawTitle(fTabRect);

	// Draw the buttons if we're supposed to
	if (!(fFlags & B_NOT_CLOSABLE) && invalid.Intersects(fCloseRect))
		_DrawClose(fCloseRect);
	if (!(fFlags & B_NOT_ZOOMABLE) && invalid.Intersects(fZoomRect))
		_DrawZoom(fZoomRect);
}


void
DefaultDecorator::_DrawClose(BRect rect)
{
	STRACE(("_DrawClose(%f,%f,%f,%f)\n", rect.left, rect.top, rect.right,
		rect.bottom));

	int32 index = (fButtonFocus ? 0 : 1) + (GetClose() ? 0 : 2);
	ServerBitmap *bitmap = fCloseBitmaps[index];
	if (bitmap == NULL) {
		bitmap = _GetBitmapForButton(DEC_CLOSE, GetClose(), fButtonFocus,
			rect.IntegerWidth(), rect.IntegerHeight(), this);
		fCloseBitmaps[index] = bitmap;
	}

	_DrawButtonBitmap(bitmap, rect);
}


void
DefaultDecorator::_DrawTitle(BRect r)
{
	STRACE(("_DrawTitle(%f,%f,%f,%f)\n", r.left, r.top, r.right, r.bottom));

	fDrawingEngine->SetHighColor(fTextColor);
	fDrawingEngine->SetLowColor(fTabColor);
	fDrawingEngine->SetFont(fDrawState.Font());

	// figure out position of text
	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);

	BPoint titlePos;
	if (fLook != kLeftTitledWindowLook) {
		titlePos.x = fCloseRect.IsValid() ? fCloseRect.right + fTextOffset
			: fTabRect.left + fTextOffset;
		titlePos.y = floorf(((fTabRect.top + 2.0) + fTabRect.bottom
			+ fontHeight.ascent + fontHeight.descent) / 2.0
			- fontHeight.descent + 0.5);
	} else {
		titlePos.x = floorf(((fTabRect.left + 2.0) + fTabRect.right
			+ fontHeight.ascent + fontHeight.descent) / 2.0
			- fontHeight.descent + 0.5);
		titlePos.y = fZoomRect.IsValid() ? fZoomRect.top - fTextOffset
			: fTabRect.bottom - fTextOffset;
	}

	fDrawingEngine->DrawString(fTruncatedTitle.String(), fTruncatedTitleLength,
		titlePos);
}


void
DefaultDecorator::_DrawZoom(BRect rect)
{
	STRACE(("_DrawZoom(%f,%f,%f,%f)\n", rect.left, rect.top, rect.right,
		rect.bottom));

	int32 index = (fButtonFocus ? 0 : 1) + (GetZoom() ? 0 : 2);
	ServerBitmap *bitmap = fZoomBitmaps[index];
	if (bitmap == NULL) {
		bitmap = _GetBitmapForButton(DEC_ZOOM, GetZoom(), fButtonFocus,
			rect.IntegerWidth(), rect.IntegerHeight(), this);
		fZoomBitmaps[index] = bitmap;
	}

	_DrawButtonBitmap(bitmap, rect);
}


void
DefaultDecorator::_SetFocus()
{
	// SetFocus() performs necessary duties for color swapping and
	// other things when a window is deactivated or activated.

	if (IsFocus()
		|| ((fLook == B_FLOATING_WINDOW_LOOK || fLook == kLeftTitledWindowLook)
			&& (fFlags & B_AVOID_FOCUS) != 0)) {
		fTabColor = fFocusTabColor;
		fTextColor = fFocusTextColor;
		fFrameColors[2] = fFocusFrameColors[0];
		fFrameColors[3] = fFocusFrameColors[1];
		fButtonFocus = true;
	} else {
		fTabColor = fNonFocusTabColor;
		fTextColor = fNonFocusTextColor;
		fFrameColors[2] = fNonFocusFrameColors[0];
		fFrameColors[3] = fNonFocusFrameColors[1];
		fButtonFocus = false;
	}

	fTabColorLight = tint_color(fTabColor,
		(B_LIGHTEN_2_TINT + B_LIGHTEN_MAX_TINT) / 2);
	fTabColorShadow = tint_color(fTabColor, B_DARKEN_2_TINT);
}


void
DefaultDecorator::_SetColors()
{
	_SetFocus();
}


void
DefaultDecorator::_UpdateFont(DesktopSettings& settings)
{
	ServerFont font;
	if (fLook == B_FLOATING_WINDOW_LOOK || fLook == kLeftTitledWindowLook) {
		settings.GetDefaultPlainFont(font);
		if (fLook == kLeftTitledWindowLook)
			font.SetRotation(90.0f);
	} else
		settings.GetDefaultBoldFont(font);

	font.SetFlags(B_FORCE_ANTIALIASING);
	font.SetSpacing(B_STRING_SPACING);
	fDrawState.SetFont(font);
}


void
DefaultDecorator::_DrawButtonBitmap(ServerBitmap* bitmap, BRect rect)
{
	if (bitmap == NULL)
		return;

	// TODO: find out why locking sometimes deadlocks here and re-add locking
	// once the problem is fixed (or remove this comment if locking isn't
	// necessary at all...)
	bool copyToFrontEnabled = fDrawingEngine->CopyToFrontEnabled();
	fDrawingEngine->SetCopyToFrontEnabled(true);
	fDrawingEngine->DrawBitmap(bitmap, rect.OffsetToCopy(0, 0), rect);
	fDrawingEngine->SetCopyToFrontEnabled(copyToFrontEnabled);
}


/*!
	\brief Draws a framed rectangle with a gradient.
	\param down The rectangle should be drawn recessed or not
*/
void
DefaultDecorator::_DrawBlendedRect(DrawingEngine* engine, BRect rect,
	bool down, bool focus)
{
	// Actually just draws a blended square
	int32 width = rect.IntegerWidth();
	int32 height = rect.IntegerHeight();
	int32 steps = width < height ? width : height;

	rgb_color startColor, endColor;
	rgb_color tabColor = focus ? fFocusTabColor : fNonFocusTabColor;
	if (down) {
		startColor = tint_color(tabColor, B_DARKEN_1_TINT);
		endColor = tint_color(tabColor, B_LIGHTEN_2_TINT);;
	} else {
		startColor = tint_color(tabColor, B_LIGHTEN_2_TINT);
		endColor = tint_color(tabColor, B_DARKEN_1_TINT);
	}

	rgb_color halfColor = make_blend_color(startColor, endColor, 0.5);

	float rstep = float(startColor.red - halfColor.red) / steps;
	float gstep = float(startColor.green - halfColor.green) / steps;
	float bstep = float(startColor.blue - halfColor.blue) / steps;

	rgb_color tempColor;
	for (int32 i = 0; i <= steps; i++) {
		tempColor.red = uint8(startColor.red - (i * rstep));
		tempColor.green = uint8(startColor.green - (i * gstep));
		tempColor.blue = uint8(startColor.blue - (i * bstep));

		engine->StrokeLine(BPoint(rect.left, rect.top + i),
			BPoint(rect.left + i, rect.top), tempColor);

		tempColor.red = uint8(halfColor.red - (i * rstep));
		tempColor.green = uint8(halfColor.green - (i * gstep));
		tempColor.blue = uint8(halfColor.blue - (i * bstep));

		engine->StrokeLine(BPoint(rect.left + steps, rect.top + i),
			BPoint(rect.left + i, rect.top + steps), tempColor);
	}

	engine->StrokeRect(rect,
		focus ? fFocusFrameColors[1] : fNonFocusFrameColors[1]);
}


void
DefaultDecorator::_GetButtonSizeAndOffset(const BRect& tabRect, float* _offset,
	float* _size, float* _inset) const
{
	float tabSize = fLook == kLeftTitledWindowLook ?
		tabRect.Width() : tabRect.Height();

	bool smallTab = fLook == B_FLOATING_WINDOW_LOOK
		|| fLook == kLeftTitledWindowLook;

	*_offset = smallTab ? floorf(fDrawState.Font().Size() / 2.6)
		: floorf(fDrawState.Font().Size() / 2.3);
	*_inset = smallTab ? floorf(fDrawState.Font().Size() / 5.0)
		: floorf(fDrawState.Font().Size() / 6.0);

	// "+ 2" so that the rects are centered within the solid area
	// (without the 2 pixels for the top border)
	*_size = tabSize - 2 * *_offset + *_inset;
}


void
DefaultDecorator::_LayoutTabItems(const BRect& tabRect)
{
	float offset;
	float size;
	float inset;
	_GetButtonSizeAndOffset(tabRect, &offset, &size, &inset);

	// calulate close rect based on the tab rectangle
	if (fLook != kLeftTitledWindowLook) {
		fCloseRect.Set(tabRect.left + offset, tabRect.top + offset,
			tabRect.left + offset + size, tabRect.top + offset + size);

		fZoomRect.Set(tabRect.right - offset - size, tabRect.top + offset,
			tabRect.right - offset, tabRect.top + offset + size);

		// hidden buttons have no width
		if ((Flags() & B_NOT_CLOSABLE) != 0)
			fCloseRect.right = fCloseRect.left - offset;
		if ((Flags() & B_NOT_ZOOMABLE) != 0)
			fZoomRect.left = fZoomRect.right + offset;
	} else {
		fCloseRect.Set(tabRect.left + offset, tabRect.top + offset,
			tabRect.left + offset + size, tabRect.top + offset + size);

		fZoomRect.Set(tabRect.left + offset, tabRect.bottom - offset - size,
			tabRect.left + size + offset, tabRect.bottom - offset);

		// hidden buttons have no height
		if ((Flags() & B_NOT_CLOSABLE) != 0)
			fCloseRect.bottom = fCloseRect.top - offset;
		if ((Flags() & B_NOT_ZOOMABLE) != 0)
			fZoomRect.top = fZoomRect.bottom + offset;
	}

	// calculate room for title
	// TODO: the +2 is there because the title often appeared
	//	truncated for no apparent reason - OTOH the title does
	//	also not appear perfectly in the middle
	if (fLook != kLeftTitledWindowLook)
		size = (fZoomRect.left - fCloseRect.right) - fTextOffset * 2 + inset;
	else
		size = (fZoomRect.top - fCloseRect.bottom) - fTextOffset * 2 + inset;

	fTruncatedTitle = Title();
	fDrawState.Font().TruncateString(&fTruncatedTitle, B_TRUNCATE_MIDDLE, size);
	fTruncatedTitleLength = fTruncatedTitle.Length();
}


ServerBitmap*
DefaultDecorator::_GetBitmapForButton(int32 item, bool down, bool focus,
	int32 width, int32 height, DefaultDecorator* object)
{
	// TODO: the list of shared bitmaps is never freed
	struct decorator_bitmap {
		int32				item;
		bool				down;
		bool				focus;
		int32				width;
		int32				height;
		UtilityBitmap*		bitmap;
		decorator_bitmap*	next;
	};

	static BLocker sBitmapListLock("decorator lock", true);
	static decorator_bitmap* sBitmapList = NULL;
	BAutolock locker(sBitmapListLock);

	// search our list for a matching bitmap
	decorator_bitmap* current = sBitmapList;
	while (current) {
		if (current->item == item && current->down == down
			&& current->focus == focus && current->width == width
			&& current->height == height) {
			return current->bitmap;
		}

		current = current->next;
	}

	static BitmapDrawingEngine* sBitmapDrawingEngine = NULL;

	// didn't find any bitmap, create a new one
	if (sBitmapDrawingEngine == NULL)
		sBitmapDrawingEngine = new(std::nothrow) BitmapDrawingEngine();
	if (sBitmapDrawingEngine == NULL
		|| sBitmapDrawingEngine->SetSize(width, height) != B_OK)
		return NULL;

	BRect rect(0, 0, width - 1, height - 1);
	sBitmapDrawingEngine->FillRect(rect,
		focus ? object->fFocusTabColor : object->fNonFocusTabColor);

	STRACE(("DefaultDecorator creating bitmap for %s %sfocus %s at size %ldx%ld\n",
		item == DEC_CLOSE ? "close" : "zoom", focus ? "" : "non-",
		down ? "down" : "up", width, height));
	switch (item) {
		case DEC_CLOSE:
			object->_DrawBlendedRect(sBitmapDrawingEngine, rect, down, focus);
			break;

		case DEC_ZOOM:
		{
			float inset = floorf(width / 4.0);
			BRect zoomRect(rect);
			zoomRect.left += inset;
			zoomRect.top += inset;
			object->_DrawBlendedRect(sBitmapDrawingEngine, zoomRect, down,
				focus);

			inset = floorf(width / 2.1);
			zoomRect = rect;
			zoomRect.right -= inset;
			zoomRect.bottom -= inset;
			object->_DrawBlendedRect(sBitmapDrawingEngine, zoomRect, down,
				focus);
			break;
		}
	}

	UtilityBitmap* bitmap = sBitmapDrawingEngine->ExportToBitmap(width, height,
		B_RGB32);
	if (bitmap == NULL)
		return NULL;

	// bitmap ready, put it into the list
	decorator_bitmap* entry = new(std::nothrow) decorator_bitmap;
	if (entry == NULL) {
		delete bitmap;
		return NULL;
	}

	entry->item = item;
	entry->down = down;
	entry->focus = focus;
	entry->width = width;
	entry->height = height;
	entry->bitmap = bitmap;
	entry->next = sBitmapList;
	sBitmapList = entry;
	return bitmap;
}
