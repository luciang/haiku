/*
 * Copyright 2001-2007, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#include "DrawingEngine.h"

#include <Bitmap.h>
#include <stdio.h>
#include <algo.h>
#include <stack.h>

#include "DrawState.h"
#include "Painter.h"
#include "PNGDump.h"
#include "ServerBitmap.h"
#include "ServerCursor.h"
#include "RenderingBuffer.h"

#include "drawing_support.h"

#define CRASH_IF_NOT_LOCKED
//#define CRASH_IF_NOT_LOCKED if (!IsParallelAccessLocked()) debugger("not parallel locked!");

#define CRASH_IF_NOT_EXCLUSIVE_LOCKED
//#define CRASH_IF_NOT_EXCLUSIVE_LOCKED if (!IsExclusiveAccessLocked()) debugger("not exclusive locked!");

// make_rect_valid
static inline void
make_rect_valid(BRect& rect)
{
	if (rect.left > rect.right) {
		float temp = rect.left;
		rect.left = rect.right;
		rect.right = temp;
	}
	if (rect.top > rect.bottom) {
		float temp = rect.top;
		rect.top = rect.bottom;
		rect.bottom = temp;
	}
}

// extend_by_stroke_width
static inline void
extend_by_stroke_width(BRect& rect, const DrawState* context)
{
	// "- 0.5" because if stroke width == 1, we don't need to extend
	float inset = -ceilf(context->PenSize() / 2.0 - 0.5);
	rect.InsetBy(inset, inset);
}


class FontLocker {
	public:
		FontLocker(const DrawState* context)
			:
			fFont(&context->Font())
		{
			fFont->Lock();
		}
		
		FontLocker(const ServerFont* font)
			:
			fFont(font)
		{
			fFont->Lock();
		}

		~FontLocker()
		{
			fFont->Unlock();
		}

	private:
		const ServerFont*	fFont;
};


//	#pragma mark -


DrawingEngine::DrawingEngine(HWInterface* interface)
	: fPainter(new Painter()),
	  fGraphicsCard(NULL),
	  fAvailableHWAccleration(0),
	  fSuspendSyncLevel(0)
{
	SetHWInterface(interface);
}


DrawingEngine::~DrawingEngine()
{
	SetHWInterface(NULL);
	delete fPainter;
}


// #pragma mark - locking


bool
DrawingEngine::LockParallelAccess()
{
	return fGraphicsCard->LockParallelAccess();
}


void
DrawingEngine::UnlockParallelAccess()
{
	fGraphicsCard->UnlockParallelAccess();
}


bool
DrawingEngine::LockExclusiveAccess()
{	
	return fGraphicsCard->LockExclusiveAccess();
}


void
DrawingEngine::UnlockExclusiveAccess()
{
	fGraphicsCard->UnlockExclusiveAccess();
}

// #pragma mark -

void
DrawingEngine::FrameBufferChanged()
{
	if (!fGraphicsCard) {
		fPainter->DetachFromBuffer();
		fAvailableHWAccleration = 0;
		return;
	}

	// NOTE: locking is probably bogus, since we are called
	// in the thread that changed the frame buffer...
	if (LockExclusiveAccess()) {
		fPainter->AttachToBuffer(fGraphicsCard->DrawingBuffer());
		// available HW acceleration might have changed
		fAvailableHWAccleration = fGraphicsCard->AvailableHWAcceleration();
		UnlockExclusiveAccess();
	}
}


void
DrawingEngine::SetHWInterface(HWInterface* interface)
{
	if (fGraphicsCard == interface)
		return;

	if (fGraphicsCard)
		fGraphicsCard->RemoveListener(this);

	fGraphicsCard = interface;

	if (fGraphicsCard)
		fGraphicsCard->AddListener(this);

	FrameBufferChanged();
}

// #pragma mark -

//! the DrawingEngine needs to be locked!
void
DrawingEngine::ConstrainClippingRegion(const BRegion* region)
{
	CRASH_IF_NOT_LOCKED

	fPainter->ConstrainClipping(region);
}


void
DrawingEngine::SetDrawState(const DrawState* state, int32 xOffset, int32 yOffset)
{
	fPainter->SetDrawState(state, false, xOffset, yOffset);	
}


void
DrawingEngine::SetHighColor(const rgb_color& color)
{
	fPainter->SetHighColor(color);	
}


void
DrawingEngine::SetLowColor(const rgb_color& color)
{
	fPainter->SetLowColor(color);	
}


void
DrawingEngine::SetPenSize(float size)
{
	fPainter->SetPenSize(size);	
}


void
DrawingEngine::SetPattern(const struct pattern& pattern)
{
	// TODO: doing it like this prevents an optimization
	// for font rendering (special "solid" drawing mode)
	fPainter->SetPattern(pattern, false);	
}


void
DrawingEngine::SetDrawingMode(drawing_mode mode)
{
	fPainter->SetDrawingMode(mode);	
}


// #pragma mark -


void
DrawingEngine::SuspendAutoSync()
{
	CRASH_IF_NOT_LOCKED

	fSuspendSyncLevel++;
}


void
DrawingEngine::Sync()
{
	CRASH_IF_NOT_LOCKED

	fSuspendSyncLevel--;
	if (fSuspendSyncLevel == 0)
		fGraphicsCard->Sync();
}

// #pragma mark -

// CopyRegion() does a topological sort of the rects in the
// region. The algorithm was suggested by Ingo Weinhold.
// It compares each rect with each rect and builds a tree
// of successors so we know the order in which they can be copied.
// For example, let's suppose these rects are in a BRegion:
//                        ************
//                        *    B     *
//                        ************
//      *************
//      *           *
//      *     A     ****************
//      *           **             *
//      **************             *
//                   *     C       *
//                   *             *
//                   *             *
//                   ***************
// When copying stuff from LEFT TO RIGHT, TOP TO BOTTOM, the
// result of the sort will be C, A, B. For this direction, we search
// for the rects that have no neighbors to their right and to their
// bottom, These can be copied without drawing into the area of
// rects yet to be copied. If you move from RIGHT TO LEFT, BOTTOM TO TOP,
// you go look for the ones that have no neighbors to their top and left.
//
// Here I draw some rays to illustrate LEFT TO RIGHT, TOP TO BOTTOM:
//                        ************
//                        *    B     *
//                        ************
//      *************
//      *           *
//      *     A     ****************-----------------
//      *           **             *
//      **************             *
//                   *     C       *
//                   *             *
//                   *             *
//                   ***************
//                   |
//                   |
//                   |
//                   |
// There are no rects in the area defined by the rays to the right
// and bottom of rect C, so that's the one we want to copy first
// (for positive x and y offsets).
// Since A is to the left of C and B is to the top of C, The "node"
// for C will point to the nodes of A and B as its "successors". Therefor,
// A and B will have an "indegree" of 1 for C pointing to them. C will
// have an "indegree" of 0, because there was no rect to which C
// was to the left or top of. When comparing A and B, neither is left
// or top from the other and in the sense that the algorithm cares about.

// NOTE: comparison of coordinates assumes that rects don't overlap
// and don't share the actual edge either (as is the case in BRegions).

struct node {
			node()
			{
				pointers = NULL;
			}
			node(const BRect& r, int32 maxPointers)
			{
				init(r, maxPointers);
			}
			~node()
			{
				delete [] pointers;
			}

	void	init(const BRect& r, int32 maxPointers)
			{
				rect = r;
				pointers = new node*[maxPointers];
				in_degree = 0;
				next_pointer = 0;
			}

	void	push(node* node)
			{
				pointers[next_pointer] = node;
				next_pointer++;
			}
	node*	top()
			{
				return pointers[next_pointer];
			}
	node*	pop()
			{
				node* ret = top();
				next_pointer--;
				return ret;
			}

	BRect	rect;
	int32	in_degree;
	node**	pointers;
	int32	next_pointer;
};

static bool
is_left_of(const BRect& a, const BRect& b)
{
	return (a.right < b.left);
}
static bool
is_above(const BRect& a, const BRect& b)
{
	return (a.bottom < b.top);
}

// CopyRegion
void
DrawingEngine::CopyRegion(/*const*/ BRegion* region,
						  int32 xOffset, int32 yOffset)
{
	CRASH_IF_NOT_EXCLUSIVE_LOCKED

	BRect frame = region->Frame();
	frame = frame | frame.OffsetByCopy(xOffset, yOffset);
	bool cursorTouched = fGraphicsCard->HideSoftwareCursor(frame);

	int32 count = region->CountRects();

	// TODO: make this step unnecessary
	// (by using different stack impl inside node)
	node nodes[count];
	for (int32 i= 0; i < count; i++) {
		nodes[i].init(region->RectAt(i), count);
	}

	for (int32 i = 0; i < count; i++) {
		BRect a = region->RectAt(i);
		for (int32 k = i + 1; k < count; k++) {
			BRect b = region->RectAt(k);
			int cmp = 0;
			// compare horizontally
			if (xOffset > 0) {
				if (is_left_of(a, b)) {
					cmp -= 1;
				} else if (is_left_of(b, a)) {
					cmp += 1;
				}
			} else if (xOffset < 0) {
				if (is_left_of(a, b)) {
					cmp += 1;
				} else if (is_left_of(b, a)) {
					cmp -= 1;
				}
			}
			// compare vertically
			if (yOffset > 0) {
				if (is_above(a, b)) {
					cmp -= 1;	
				} else if (is_above(b, a)) {
					cmp += 1;
				}
			} else if (yOffset < 0) {
				if (is_above(a, b)) {
					cmp += 1;
				} else if (is_above(b, a)) {
					cmp -= 1;
				}
			}
			// add appropriate node as successor
			if (cmp > 0) {
				nodes[i].push(&nodes[k]);
				nodes[k].in_degree++;
			} else if (cmp < 0) {
				nodes[k].push(&nodes[i]);
				nodes[i].in_degree++;
			}
		}
	}
	// put all nodes onto a stack that have an "indegree" count of zero
	stack<node*> inDegreeZeroNodes;
	for (int32 i = 0; i < count; i++) {
		if (nodes[i].in_degree == 0) {
			inDegreeZeroNodes.push(&nodes[i]);
		}
	}
	// pop the rects from the stack, do the actual copy operation
	// and decrease the "indegree" count of the other rects not
	// currently on the stack and to which the current rect pointed
	// to. If their "indegree" count reaches zero, put them onto the
	// stack as well.

	clipping_rect* sortedRectList = NULL;
	int32 nextSortedIndex = 0;

	if (fAvailableHWAccleration & HW_ACC_COPY_REGION)
		sortedRectList = new clipping_rect[count];

	while (!inDegreeZeroNodes.empty()) {
		node* n = inDegreeZeroNodes.top();
		inDegreeZeroNodes.pop();

		// do the software implementation or add to sorted
		// rect list for using the HW accelerated version
		// later
		if (sortedRectList) {
			sortedRectList[nextSortedIndex].left	= (int32)n->rect.left;
			sortedRectList[nextSortedIndex].top		= (int32)n->rect.top;
			sortedRectList[nextSortedIndex].right	= (int32)n->rect.right;
			sortedRectList[nextSortedIndex].bottom	= (int32)n->rect.bottom;
			nextSortedIndex++;
		} else {
			BRect touched = _CopyRect(n->rect, xOffset, yOffset);
			fGraphicsCard->Invalidate(touched);
		}

		for (int32 k = 0; k < n->next_pointer; k++) {
			n->pointers[k]->in_degree--;
			if (n->pointers[k]->in_degree == 0)
				inDegreeZeroNodes.push(n->pointers[k]);
		}
	}

	// trigger the HW accelerated version if is was available
	if (sortedRectList)
		fGraphicsCard->CopyRegion(sortedRectList, count, xOffset, yOffset);

	delete[] sortedRectList;

	if (cursorTouched)
		fGraphicsCard->ShowSoftwareCursor();
}

// InvertRect
void
DrawingEngine::InvertRect(BRect r)
{
	CRASH_IF_NOT_LOCKED

	make_rect_valid(r);
	r = fPainter->ClipRect(r);
	if (r.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(r);

		// try hardware optimized version first
		if (fAvailableHWAccleration & HW_ACC_INVERT_REGION) {
			BRegion region(r);
			region.IntersectWith(fPainter->ClippingRegion());
			fGraphicsCard->InvertRegion(region);
		} else {		
			fPainter->InvertRect(r);

			fGraphicsCard->Invalidate(r);
		}

		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// DrawBitmap
void
DrawingEngine::DrawBitmap(ServerBitmap *bitmap,
						  const BRect &source, const BRect &dest,
						  const DrawState *d)
{
	CRASH_IF_NOT_LOCKED

	BRect clipped = fPainter->ClipRect(dest);
	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

//		fPainter->SetDrawState(d);
		fPainter->DrawBitmap(bitmap, source, dest);

		fGraphicsCard->Invalidate(clipped);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// DrawArc
void
DrawingEngine::DrawArc(BRect r, const float &angle,
					   const float &span, const DrawState *d,
					   bool filled)
{
	CRASH_IF_NOT_LOCKED

	make_rect_valid(r);
	BRect clipped(r);
	if (!filled)
		extend_by_stroke_width(clipped, d);
	clipped = fPainter->ClipRect(r);
	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

//		fPainter->SetDrawState(d);

		float xRadius = r.Width() / 2.0;
		float yRadius = r.Height() / 2.0;
		BPoint center(r.left + xRadius,
					  r.top + yRadius);

		if (filled)	
			fPainter->FillArc(center, xRadius, yRadius, angle, span);
		else
			fPainter->StrokeArc(center, xRadius, yRadius, angle, span);

		fGraphicsCard->Invalidate(clipped);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// DrawBezier
void
DrawingEngine::DrawBezier(BPoint *pts, const DrawState *d, bool filled)
{
	CRASH_IF_NOT_LOCKED

	// TODO: figure out bounds and hide cursor depending on that
	fGraphicsCard->HideSoftwareCursor();

//	fPainter->SetDrawState(d);
	BRect touched = fPainter->DrawBezier(pts, filled);

	fGraphicsCard->Invalidate(touched);
	fGraphicsCard->ShowSoftwareCursor();
}

// DrawEllipse
void
DrawingEngine::DrawEllipse(BRect r, const DrawState *d, bool filled)
{
	CRASH_IF_NOT_LOCKED

	make_rect_valid(r);
	BRect clipped = r;
	fPainter->AlignEllipseRect(&clipped, filled);

	if (!filled)
		extend_by_stroke_width(clipped, d);

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	clipped = fPainter->ClipRect(clipped);

	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

//		fPainter->SetDrawState(d);
		fPainter->DrawEllipse(r, filled);

		fGraphicsCard->Invalidate(clipped);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// DrawPolygon
void
DrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts,
						   BRect bounds, const DrawState* d,
						   bool filled, bool closed)
{
	CRASH_IF_NOT_LOCKED

	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, d);
	bounds = fPainter->ClipRect(bounds);
	if (bounds.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(bounds);

//		fPainter->SetDrawState(d);
		fPainter->DrawPolygon(ptlist, numpts, filled, closed);

		fGraphicsCard->Invalidate(bounds);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// #pragma mark - RGBColor

void
DrawingEngine::StrokePoint(const BPoint& pt, const RGBColor &color)
{
	StrokeLine(pt, pt, color);
}

// StrokeLine
//
// * this function is only used by Decorators
// * it assumes a one pixel wide line
void
DrawingEngine::StrokeLine(const BPoint &start, const BPoint &end,
						  const RGBColor &color)
{
	CRASH_IF_NOT_LOCKED

	BRect touched(start, end);
	make_rect_valid(touched);
	touched = fPainter->ClipRect(touched);
	bool cursorTouched = fGraphicsCard->HideSoftwareCursor(touched);

	if (!fPainter->StraightLine(start, end, color.GetColor32())) {
		// TODO: all this is quite expensive, but it is currently
		// used only for the "gradient" decorator tab buttons
		rgb_color previousColor = fPainter->HighColor();
		drawing_mode previousMode = fPainter->DrawingMode();
		fPainter->SetHighColor(color);
		fPainter->SetDrawingMode(B_OP_OVER);
		fPainter->StrokeLine(start, end);
		fPainter->SetHighColor(previousColor);
		fPainter->SetDrawingMode(previousMode);
	} else {
		fGraphicsCard->Invalidate(touched);
	}
	if (cursorTouched)
		fGraphicsCard->ShowSoftwareCursor();
}

// this function is used to draw a one pixel wide rect
void
DrawingEngine::StrokeRect(BRect r, const RGBColor &color)
{
	CRASH_IF_NOT_LOCKED

	make_rect_valid(r);
	BRect clipped = fPainter->ClipRect(r);
	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

		fPainter->StrokeRect(r, color.GetColor32());

		fGraphicsCard->Invalidate(clipped);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}


void
DrawingEngine::FillRect(BRect r, const RGBColor& color)
{
	CRASH_IF_NOT_LOCKED

	// NOTE: Write locking because we might use HW acceleration.
	// This needs to be investigated, I'm doing this because of
	// gut feeling.
	make_rect_valid(r);
	r = fPainter->ClipRect(r);
	if (r.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(r);

		// try hardware optimized version first
		if (fAvailableHWAccleration & HW_ACC_FILL_REGION) {
			BRegion region(r);
			region.IntersectWith(fPainter->ClippingRegion());
			fGraphicsCard->FillRegion(region, color,
									  fSuspendSyncLevel == 0
									  || cursorTouched);
		} else {
			fPainter->FillRect(r, color.GetColor32());
	
			fGraphicsCard->Invalidate(r);
		}

		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}


void
DrawingEngine::FillRegion(BRegion& r, const RGBColor& color)
{
	CRASH_IF_NOT_LOCKED

	// NOTE: region expected to be already clipped correctly!!
	BRect frame = r.Frame();
	bool cursorTouched = fGraphicsCard->HideSoftwareCursor(frame);

	bool doInSoftware = true;
	// try hardware optimized version first
	if ((fAvailableHWAccleration & HW_ACC_FILL_REGION) != 0
		&& frame.Width() * frame.Height() > 100) {
		fGraphicsCard->FillRegion(r, color, fSuspendSyncLevel == 0
											|| cursorTouched);
		doInSoftware = false;
	}

	if (doInSoftware) {

		int32 count = r.CountRects();
		for (int32 i = 0; i < count; i++) {
			fPainter->FillRectNoClipping(r.RectAt(i), color.GetColor32());
		}

		fGraphicsCard->Invalidate(frame);
	}

	if (cursorTouched)
		fGraphicsCard->ShowSoftwareCursor();
}

// #pragma mark - DrawState

void
DrawingEngine::StrokeRect(BRect r, const DrawState *d)
{
	CRASH_IF_NOT_LOCKED

	// support invalid rects
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, d);
	clipped = fPainter->ClipRect(clipped);
	if (clipped.IsValid()) {

		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

//		fPainter->SetDrawState(d);
		fPainter->StrokeRect(r);

		fGraphicsCard->Invalidate(clipped);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}


void
DrawingEngine::FillRect(BRect r, const DrawState *d)
{
	CRASH_IF_NOT_LOCKED

	make_rect_valid(r);
	r = fPainter->ClipRect(r);
	if (r.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(r);

		bool doInSoftware = true;
		if ((r.Width() + 1) * (r.Height() + 1) > 100.0) {
			// try hardware optimized version first
			// if the rect is large enough
			if ((fAvailableHWAccleration & HW_ACC_FILL_REGION) != 0) {
				if (d->GetPattern() == B_SOLID_HIGH
					&& (d->GetDrawingMode() == B_OP_COPY
						|| d->GetDrawingMode() == B_OP_OVER)) {
					BRegion region(r);
					region.IntersectWith(fPainter->ClippingRegion());
					fGraphicsCard->FillRegion(region, d->HighColor(),
											  fSuspendSyncLevel == 0
											  || cursorTouched);
					doInSoftware = false;
				} else if (d->GetPattern() == B_SOLID_LOW
						   && d->GetDrawingMode() == B_OP_COPY) {
					BRegion region(r);
					region.IntersectWith(fPainter->ClippingRegion());
					fGraphicsCard->FillRegion(region, d->LowColor(),
											  fSuspendSyncLevel == 0
											  || cursorTouched);
					doInSoftware = false;
				}
			}
		}
		if (doInSoftware) {
//			fPainter->SetDrawState(d);
			fPainter->FillRect(r);

			fGraphicsCard->Invalidate(r);
		}

		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}


void
DrawingEngine::FillRegion(BRegion& r, const DrawState *d)
{
	CRASH_IF_NOT_LOCKED

	BRect clipped = fPainter->ClipRect(r.Frame());
	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

		bool doInSoftware = true;
		// try hardware optimized version first
		if ((fAvailableHWAccleration & HW_ACC_FILL_REGION) != 0) {
			if (d->GetPattern() == B_SOLID_HIGH
				&& (d->GetDrawingMode() == B_OP_COPY
					|| d->GetDrawingMode() == B_OP_OVER)) {
				r.IntersectWith(fPainter->ClippingRegion());
				fGraphicsCard->FillRegion(r, d->HighColor(),
										  fSuspendSyncLevel == 0
										  || cursorTouched);
				doInSoftware = false;
			} else if (d->GetPattern() == B_SOLID_LOW
					   && d->GetDrawingMode() == B_OP_COPY) {
				r.IntersectWith(fPainter->ClippingRegion());
				fGraphicsCard->FillRegion(r, d->LowColor(),
										  fSuspendSyncLevel == 0
										  || cursorTouched);
				doInSoftware = false;
			}
		}

		if (doInSoftware) {
//			fPainter->SetDrawState(d);

			BRect touched = fPainter->FillRect(r.RectAt(0));

			int32 count = r.CountRects();
			for (int32 i = 1; i < count; i++) {
				touched = touched | fPainter->FillRect(r.RectAt(i));
			}

			fGraphicsCard->Invalidate(touched);
		}

		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}


void
DrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad,
	const DrawState* d, bool filled)
{
	CRASH_IF_NOT_LOCKED

	// NOTE: the stroke does not extend past "r" in R5,
	// though I consider this unexpected behaviour.
	make_rect_valid(r);
	BRect clipped = fPainter->ClipRect(r);

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

//		fPainter->SetDrawState(d);
		BRect touched = filled ? fPainter->FillRoundRect(r, xrad, yrad)
							   : fPainter->StrokeRoundRect(r, xrad, yrad);

		fGraphicsCard->Invalidate(touched);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}


void
DrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 ptCount, const BPoint* ptList,
	const DrawState* d, bool filled)
{
	CRASH_IF_NOT_LOCKED

	// NOTE: hides cursor regardless of if and where
	// shape is drawn on screen, TODO: optimize
	fGraphicsCard->HideSoftwareCursor();

//	fPainter->SetDrawState(d);
	BRect touched = fPainter->DrawShape(opCount, opList,
										ptCount, ptList,
										filled);

	fGraphicsCard->Invalidate(touched);
	fGraphicsCard->ShowSoftwareCursor();
}


void
DrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds,
	const DrawState* d, bool filled)
{
	CRASH_IF_NOT_LOCKED

	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, d);
	clipped = fPainter->ClipRect(clipped);
	if (clipped.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(clipped);

//		fPainter->SetDrawState(d);
		if (filled)
			fPainter->FillTriangle(pts[0], pts[1], pts[2]);
		else
			fPainter->StrokeTriangle(pts[0], pts[1], pts[2]);

		fGraphicsCard->Invalidate(clipped);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// StrokeLine
void
DrawingEngine::StrokeLine(const BPoint &start, const BPoint &end, DrawState* context)
{
	CRASH_IF_NOT_LOCKED

	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, context);
	touched = fPainter->ClipRect(touched);
	if (touched.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(touched);

//		fPainter->SetDrawState(context);
		fPainter->StrokeLine(start, end);

		fGraphicsCard->Invalidate(touched);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// StrokeLineArray
void
DrawingEngine::StrokeLineArray(int32 numLines,
	const LineArrayData *linedata, const DrawState *d)
{
	CRASH_IF_NOT_LOCKED

	if (!d || !linedata || numLines <= 0)
		return;

	// figure out bounding box for line array
	const LineArrayData *data = (const LineArrayData *)&(linedata[0]);
	BRect touched(min_c(data->pt1.x, data->pt2.x),
				  min_c(data->pt1.y, data->pt2.y),
				  max_c(data->pt1.x, data->pt2.x),
				  max_c(data->pt1.y, data->pt2.y));

	for (int32 i = 1; i < numLines; i++) {
		data = (const LineArrayData *)&(linedata[i]);
		BRect box(min_c(data->pt1.x, data->pt2.x),
				  min_c(data->pt1.y, data->pt2.y),
				  max_c(data->pt1.x, data->pt2.x),
				  max_c(data->pt1.y, data->pt2.y));
		touched = touched | box;
	}
	extend_by_stroke_width(touched, d);
	touched = fPainter->ClipRect(touched);
	if (touched.IsValid()) {
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(touched);

		data = (const LineArrayData *)&(linedata[0]);

		fPainter->SetHighColor(data->color);
		fPainter->StrokeLine(data->pt1, data->pt2);

		for (int32 i = 1; i < numLines; i++) {
			data = (const LineArrayData *)&(linedata[i]);
			fPainter->SetHighColor(data->color);
			fPainter->StrokeLine(data->pt1, data->pt2);
		}

		// restore correct drawing state highcolor
		fPainter->SetHighColor(d->HighColor());

		fGraphicsCard->Invalidate(touched);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}
}

// #pragma mark -

BPoint
DrawingEngine::DrawString(const char* string, int32 length,
						  const BPoint& pt, DrawState* d,
						  escapement_delta* delta)
{
	CRASH_IF_NOT_LOCKED

// TODO: use delta
	FontLocker locker(d);

	BPoint penLocation = pt;

	fPainter->SetDrawState(d, true);
		// TODO: this will reset the scrolling offsets used for
		// pattern drawing again, it is really necessary to change
		// the whole graphics state synchronization between DrawState
		// and Painter, and remove any individual SetDrawState() calls
		// like here
//bigtime_t now = system_time();
// TODO: BoundingBox is quite slow!! Optimizing it will be beneficial.
// Cursiously, the DrawString after it is actually faster!?!
// TODO: make the availability of the hardware cursor part of the 
// HW acceleration flags and skip all calculations for HideSoftwareCursor
// in case we don't have one.
// TODO: Watch out about penLocation and use Painter::PenLocation() when
// not using BoundindBox anymore.
	BRect b = fPainter->BoundingBox(string, length, pt, &penLocation, delta);
	// stop here if we're supposed to render outside of the clipping
	b = fPainter->ClipRect(b);
	if (b.IsValid()) {
//printf("bounding box '%s': %lld µs\n", string, system_time() - now);
		bool cursorTouched = fGraphicsCard->HideSoftwareCursor(b);

//now = system_time();
		BRect touched = fPainter->DrawString(string, length, pt, delta);
//printf("drawing string: %lld µs\n", system_time() - now);

		fGraphicsCard->Invalidate(touched);
		if (cursorTouched)
			fGraphicsCard->ShowSoftwareCursor();
	}

	return penLocation;
}

// StringWidth
float
DrawingEngine::StringWidth(const char* string, int32 length,
						   const DrawState* d, escapement_delta* delta)
{
// TODO: use delta
	FontLocker locker(d);

	float width = 0.0;
// NOTE: For now it is enough to block on the
// font style lock, this already prevents multiple
// threads from executing this code and avoids a
// deadlock in case another thread holds the font
// lock already and then tries to lock the drawing
// engine after it is already locked here (race condition)
	width = fPainter->StringWidth(string, length, d);
	return width;
}

// StringWidth
float
DrawingEngine::StringWidth(const char* string, int32 length,
						   const ServerFont& font, escapement_delta* delta)
{
	DrawState d;
	d.SetFont(font);
	return StringWidth(string, length, &d, delta);
}

// StringHeight
float
DrawingEngine::StringHeight(const char *string, int32 length,
							const DrawState *d)
{
	FontLocker locker(d);

	float height = 0.0;
// NOTE: For now it is enough to block on the
// font style lock, this already prevents multiple
// threads from executing this code and avoids a
// deadlock in case another thread holds the font
// lock already and then tries to lock the drawing
// engine after it is already locked here (race condition)
	fPainter->SetDrawState(d, true);
	BPoint dummy1(0.0, 0.0);
	BPoint dummy2(0.0, 0.0);
	height = fPainter->BoundingBox(string, length, dummy1, &dummy2).Height();
	return height;
}

// #pragma mark -

// DumpToFile
bool
DrawingEngine::DumpToFile(const char *path)
{
	CRASH_IF_NOT_EXCLUSIVE_LOCKED

	RenderingBuffer* buffer = fGraphicsCard->DrawingBuffer();
	if (buffer) {
		BRect bounds(0.0, 0.0, buffer->Width() - 1, buffer->Height() - 1);
		SaveToPNG(path, bounds, buffer->ColorSpace(),
				  buffer->Bits(),
				  buffer->BitsLength(),
				  buffer->BytesPerRow());
		return true;
	}
	return false;
}

// DumpToBitmap
ServerBitmap*
DrawingEngine::DumpToBitmap()
{
	return NULL;
}

status_t
DrawingEngine::ReadBitmap(ServerBitmap *bitmap, bool drawCursor, BRect bounds)
{
	CRASH_IF_NOT_EXCLUSIVE_LOCKED

	RenderingBuffer *buffer = fGraphicsCard->DrawingBuffer();
	if (!buffer)
		return B_ERROR;

	BRect clip(0, 0, buffer->Width() - 1, buffer->Height() - 1);
	bounds = bounds & clip;
	fGraphicsCard->HideSoftwareCursor(bounds);

	status_t result = bitmap->ImportBits(buffer->Bits(), buffer->BitsLength(),
		buffer->BytesPerRow(), buffer->ColorSpace(),
		bounds.LeftTop(), BPoint(0, 0), 
		bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1);

	if (drawCursor) {
		ServerCursor *cursor = fGraphicsCard->Cursor();
		int32 cursorWidth = cursor->Width();
		int32 cursorHeight = cursor->Height();

		BPoint cursorPosition = fGraphicsCard->CursorPosition();
		cursorPosition -= bounds.LeftTop() + cursor->GetHotSpot();

		BBitmap cursorArea(BRect(0, 0, cursorWidth - 1, cursorHeight - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);
			
		cursorArea.ImportBits(bitmap->Bits(), bitmap->BitsLength(),
			bitmap->BytesPerRow(), bitmap->ColorSpace(),
			cursorPosition,	BPoint(0, 0),
			cursorWidth, cursorHeight);

		uint8 *bits = (uint8 *)cursorArea.Bits();
		uint8 *cursorBits = (uint8 *)cursor->Bits();
		for (int32 i = 0; i < cursorHeight; i++) {
			for (int32 j = 0; j < cursorWidth; j++) {
				uint8 alpha = 255 - cursorBits[3];
				bits[0] = ((bits[0] * alpha) >> 8) + cursorBits[0];
				bits[1] = ((bits[1] * alpha) >> 8) + cursorBits[1];
				bits[2] = ((bits[2] * alpha) >> 8) + cursorBits[2];
				cursorBits += 4;
				bits += 4;
			}
		}

		bitmap->ImportBits(cursorArea.Bits(), cursorArea.BitsLength(),
			cursorArea.BytesPerRow(), cursorArea.ColorSpace(),
			BPoint(0, 0), cursorPosition, 
			cursorWidth, cursorHeight);
	}

	fGraphicsCard->ShowSoftwareCursor();

	return result;
}

// #pragma mark -

BRect
DrawingEngine::_CopyRect(BRect src, int32 xOffset, int32 yOffset) const
{
	// TODO: assumes drawing buffer is 32 bits (which it currently always is)
	BRect dst;
	RenderingBuffer* buffer = fGraphicsCard->DrawingBuffer();
	if (buffer) {
		BRect clip(0, 0, buffer->Width() - 1, buffer->Height() - 1);

		dst = src;
		dst.OffsetBy(xOffset, yOffset);

		if (clip.Intersects(src) && clip.Intersects(dst)) {
			uint32 bytesPerRow = buffer->BytesPerRow();
			uint8* bits = (uint8*)buffer->Bits();

			// clip source rect
			src = src & clip;
			// clip dest rect
			dst = dst & clip;
			// move dest back over source and clip source to dest
			dst.OffsetBy(-xOffset, -yOffset);
			src = src & dst;

			// calc offset in buffer
			bits += (int32)src.left * 4 + (int32)src.top * bytesPerRow;

			uint32 width = src.IntegerWidth() + 1;
			uint32 height = src.IntegerHeight() + 1;

			_CopyRect(bits, width, height, bytesPerRow, xOffset, yOffset);

			// offset dest again, because it is return value
			dst.OffsetBy(xOffset, yOffset);
		}
	}
	return dst;
}


void
DrawingEngine::_CopyRect(uint8* src, uint32 width, uint32 height,
	uint32 bytesPerRow, int32 xOffset, int32 yOffset) const
{
	// TODO: assumes drawing buffer is 32 bits (which it currently always is)
	int32 xIncrement;
	int32 yIncrement;

	if (yOffset == 0 && xOffset > 0) {
		// copy from right to left
		xIncrement = -1;
		src += (width - 1) * 4;
	} else {
		// copy from left to right
		xIncrement = 1;
	}

	if (yOffset > 0) {
		// copy from bottom to top
		yIncrement = -bytesPerRow;
		src += (height - 1) * bytesPerRow;
	} else {
		// copy from top to bottom
		yIncrement = bytesPerRow;
	}

	uint8* dst = src + yOffset * bytesPerRow + xOffset * 4;

	if (xIncrement == 1) {
		uint8 tmpBuffer[width * 4];
		for (uint32 y = 0; y < height; y++) {
			// NOTE: read into temporary scanline buffer,
			// avoid memcpy because it might be graphics card memory
			gfxcpy32(tmpBuffer, src, width * 4);
			// write back temporary scanline buffer
			// NOTE: **don't read and write over the PCI bus
			// at the same time**
			memcpy(dst, tmpBuffer, width * 4);
// NOTE: this (instead of the two pass copy above) might
// speed up QEMU -> ?!? (would depend on how it emulates
// the PCI bus...)
// TODO: would be nice if we actually knew
// if we're operating in graphics memory or main memory...
//memcpy(dst, src, width * 4);
			src += yIncrement;
			dst += yIncrement;
		}
	} else {
		for (uint32 y = 0; y < height; y++) {
			uint32* srcHandle = (uint32*)src;
			uint32* dstHandle = (uint32*)dst;
			for (uint32 x = 0; x < width; x++) {
				*dstHandle = *srcHandle;
				srcHandle += xIncrement;
				dstHandle += xIncrement;
			}
			src += yIncrement;
			dst += yIncrement;
		}
	}
}


