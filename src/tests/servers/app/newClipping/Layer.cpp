#include <OS.h>
#include <Region.h>
#include <Rect.h>
#include <stdio.h>
#include <strings.h>

#include <Window.h>

#include "Layer.h"
#include "MyView.h"

extern BWindow* wind;

Layer::Layer(BRect frame, const char* name, uint32 rm, uint32 flags, rgb_color c)
{
	fFrame = frame;
	fOrigin.Set(0.0f, 0.0f);
	fResizeMode = rm;
	fFlags = flags;
	fColor = c;

	fBottom = NULL;
	fUpper = NULL;
	fLower = NULL;
	fTop = NULL;
	fParent = NULL;
	fView = NULL;
	fCurrent = NULL;
	fHidden = false;

	strcpy(fName, name);
}

Layer::~Layer()
{
	Layer	*c = fBottom;
	Layer	*toast;
	while (c)
	{
		toast = c;
		c = c->fUpper;
		delete toast;
	}
}

void Layer::ConvertToScreen2(BRect* rect) const
{
	MyView *view = GetRootLayer();
	if (view)
		if (fParent)
		{
			rect->OffsetBy(-fOrigin.x, -fOrigin.y);
			rect->OffsetBy(fFrame.left, fFrame.top);

			fParent->ConvertToScreen2(rect);
		}
}

void Layer::ConvertToScreen2(BRegion* reg) const
{
	MyView *view = GetRootLayer();
	if (view)
		if (fParent)
		{
			reg->OffsetBy(-fOrigin.x, -fOrigin.y);
			reg->OffsetBy(fFrame.left, fFrame.top);

			fParent->ConvertToScreen2(reg);
		}
}

MyView* Layer::GetRootLayer() const
{
	if (fView)
		return fView;
	else
		if (fParent)
			return fParent->GetRootLayer();
		else
			return NULL;
}

Layer* Layer::VirtualBottomChild() const
{
	fCurrent = fBottom;
	return fCurrent;
}

Layer* Layer::VirtualTopChild() const
{
	fCurrent = fTop;
	return fCurrent;
}

Layer* Layer::VirtualUpperSibling() const
{
	fCurrent = fCurrent->fUpper;
	return fCurrent;
}

Layer* Layer::VirtualLowerSibling() const
{
	fCurrent = fCurrent->fLower;
	return fCurrent;
}

void Layer::AddLayer(Layer* layer)
{
	if( layer->fParent != NULL ) 
	{
		printf("ERROR: Layer already has a parent\n");
		return;
	}
	
	layer->fParent = this;
	
	if (!fBottom)
	{
		fBottom = layer;
		fTop = layer;
		return;
	}
	fBottom->fLower = layer;
	layer->fUpper = fBottom;
	fBottom = layer;
}

bool Layer::RemLayer(Layer* layer)
{
	if(!layer->fParent || layer->fParent != this)
	{
		printf("ERROR: Rem: Layer doesn't have a fParent or !=this\n");
		return false;
	}
	
	layer->fParent = NULL;
	
	if(fTop == layer)
		fTop = layer->fLower;
	
	if(fBottom == layer )
		fBottom = layer->fUpper;
	
	if(layer->fUpper != NULL)
		layer->fUpper->fLower = layer->fLower;
	
	if(layer->fLower != NULL)
		layer->fLower->fUpper = layer->fUpper;
	
	layer->fUpper = NULL;
	layer->fLower = NULL;

	layer->clear_visible_regions();

	return true;
}

bool
Layer::IsVisuallyHidden() const
{
	if (fHidden)
		return true;

// TODO: remove the following 2 lines when for real.
	if (fView)
		return false;

	if (fParent)
		return fParent->IsVisuallyHidden();
		
	return fHidden;
}

void
Layer::Hide()
{
	fHidden = true;

	if (fParent && !fParent->IsVisuallyHidden() && GetRootLayer())
	{
		// save fullVisible so we know what to invalidate
		BRegion invalid(fFullVisible);

		clear_visible_regions();

		if (invalid.Frame().IsValid())
			fParent->Invalidate(invalid, this);
	}
}

void
Layer::Show()
{
	fHidden = false;

	if (fParent && !fParent->IsVisuallyHidden() && GetRootLayer())
	{
		BRegion invalid;

		get_user_regions(invalid);

		if (invalid.CountRects() > 0)
			fParent->Invalidate(invalid, this);
	}
}

void
Layer::Invalidate(const BRegion &invalid, const Layer *startFrom)
{
	BRegion		localVisible(fFullVisible);
	localVisible.IntersectWith(&invalid);
	rebuild_visible_regions(invalid, localVisible,
		startFrom? startFrom: VirtualBottomChild());

	// add localVisible to our RootLayer's redraw region.
	GetRootLayer()->fRedrawReg.Include(&localVisible);
	GetRootLayer()->RequestRedraw(); // TODO: what if we pass (fParent, startFromTHIS, &redrawReg)?
}

void
Layer::resize_layer_frame_by(float x, float y)
{
	uint16		rm = fResizeMode & 0x0000FFFF;
	BRect		newFrame = fFrame;

	if ((rm & 0x0F00U) == _VIEW_LEFT_ << 8)
		newFrame.left += 0.0f;
	else if ((rm & 0x0F00U) == _VIEW_RIGHT_ << 8)
		newFrame.left += x;
	else if ((rm & 0x0F00U) == _VIEW_CENTER_ << 8)
		newFrame.left += x/2;

	if ((rm & 0x000FU) == _VIEW_LEFT_)
		newFrame.right += 0.0f;
	else if ((rm & 0x000FU) == _VIEW_RIGHT_)
		newFrame.right += x;
	else if ((rm & 0x000FU) == _VIEW_CENTER_)
		newFrame.right += x/2;

	if ((rm & 0xF000U) == _VIEW_TOP_ << 12)
		newFrame.top += 0.0f;
	else if ((rm & 0xF000U) == _VIEW_BOTTOM_ << 12)
		newFrame.top += y;
	else if ((rm & 0xF000U) == _VIEW_CENTER_ << 12)
		newFrame.top += y/2;

	if ((rm & 0x00F0U) == _VIEW_TOP_ << 4)
		newFrame.bottom += 0.0f;
	else if ((rm & 0x00F0U) == _VIEW_BOTTOM_ << 4)
		newFrame.bottom += y;
	else if ((rm & 0x00F0U) == _VIEW_CENTER_ << 4)
		newFrame.bottom += y/2;

	if (newFrame != fFrame)
	{
		float		dx, dy;

		fFrame	= newFrame;

		dx	= newFrame.Width() - fFrame.Width();
		dy	= newFrame.Height() - fFrame.Height();

		if (dx != 0.0f || dy != 0.0f)
		{
			// call hook function
			ResizedByHook(dx, dy, true); // automatic

			for (Layer *lay = VirtualBottomChild();
						lay ; lay = VirtualUpperSibling())
			{
				lay->resize_layer_frame_by(dx, dy);
			}
		}
		else
			MovedByHook(dx, dy);
	}
}

void
Layer::rezize_layer_redraw_more(BRegion &reg, float dx, float dy)
{
	for (Layer *lay = VirtualBottomChild();
				lay; lay = VirtualUpperSibling())
	{
		uint16		rm = lay->fResizeMode & 0x0000FFFF;

		if (((rm & 0x0F0F) == (uint16)B_FOLLOW_RIGHT && dx != 0) ||
			((rm & 0x0F0F) == (uint16)B_FOLLOW_H_CENTER && dx != 0) ||
			((rm & 0xF0F0) == (uint16)B_FOLLOW_BOTTOM && dy != 0)||
			((rm & 0xF0F0) == (uint16)B_FOLLOW_V_CENTER && dy != 0) ||
		// TODO: these 2 don't need to be redrawn entirely. but ATM we don't have a choice
			(rm & 0x0F0F) == (uint16)B_FOLLOW_LEFT_RIGHT || (rm & 0xF0F0) == (uint16)B_FOLLOW_TOP_BOTTOM)
		{
			reg.Include(&lay->fFullVisible);
		}
	}
}

void
Layer::rezize_layer_redraw_more(BRegion &redraw, BRegion &copy, float dx, float dy)
{
	for (Layer *lay = VirtualBottomChild();
				lay; lay = VirtualUpperSibling())
	{
		uint16		rm = lay->fResizeMode & 0x0000FFFF;

		if ((rm & 0x0F0F) == (uint16)B_FOLLOW_RIGHT && dx != 0)
		{
			copy.Include(&lay->fFullVisible);
			redraw.Include(&lay->fFullVisible);
		}
		else if (((rm & 0x0F0F) == (uint16)B_FOLLOW_H_CENTER && dx != 0) ||
			((rm & 0xF0F0) == (uint16)B_FOLLOW_BOTTOM && dy != 0) ||
			((rm & 0xF0F0) == (uint16)B_FOLLOW_V_CENTER && dy != 0) ||
		// TODO: these 2 don't need to be redrawn entirely. but ATM we don't have a choice
			(rm & 0x0F0F) == (uint16)B_FOLLOW_LEFT_RIGHT || (rm & 0xF0F0) == (uint16)B_FOLLOW_TOP_BOTTOM)
		{
			redraw.Include(&lay->fFullVisible);
		}
	}
}

void
Layer::ResizeBy(float dx, float dy)
{
	fFrame.Set(fFrame.left, fFrame.top, fFrame.right+dx, fFrame.bottom+dy);

	// resize children using their resize_mask.
	for (Layer *lay = VirtualBottomChild();
				lay; lay = VirtualUpperSibling())
			lay->resize_layer_frame_by(dx, dy);

	// call hook function
	if (dx != 0.0f || dy != 0.0f)
		ResizedByHook(dx, dy, false); // manual

	if (!IsVisuallyHidden() && GetRootLayer())
	{
		BRegion oldFullVisible(fFullVisible);
		BRegion oldVisible(fVisible);

// OPT: you can use HW acceleration for either for bottom alligned layer or
//		for right alligned ones. investigate!

		// right, center and bottom alligned layers will change their position
		// so we need to invalidate their current visible regions
//		BRegion redrawRightOrBottom;
//		rezize_layer_redraw_more(redrawRightOrBottom, dx, dy);
BRegion redrawRightOrBottom;
BRegion copyReg;
rezize_layer_redraw_more(redrawRightOrBottom, copyReg, dx, dy);

		// we'll invalidate the old area and the new, maxmial one.
		BRegion invalid;
		get_user_regions(invalid);
		invalid.Include(&fFullVisible);

		clear_visible_regions();

		fParent->RebuildVisibleRegions(invalid, this);

		// done rebuilding regions, now redraw regions that became visible

		// what's invalid, are the differences between to old and the new fullVisible region
		// 1) in case we grow.
		BRegion		redrawReg(fFullVisible);
		redrawReg.Exclude(&oldFullVisible);
		// 2) in case we shrink
		BRegion		redrawReg2(oldFullVisible);
		redrawReg2.Exclude(&fFullVisible);
		// 3) combine.
		redrawReg.Include(&redrawReg2);

		// for center, right and bottom alligned layers, redraw their old positions
		redrawReg.Include(&redrawRightOrBottom);

		// layers that had their frame modified must be entirely redrawn.
		rezize_layer_redraw_more(redrawReg, dx, dy);

		// add redrawReg to our RootLayer's redraw region.
		GetRootLayer()->fRedrawReg.Include(&redrawReg);
		// include layer's visible region in case we want a full update on resize
		if (fFlags & B_FULL_UPDATE_ON_RESIZE && fVisible.Frame().IsValid())
		{
			GetRootLayer()->fRedrawReg.Include(&fVisible);
			GetRootLayer()->fRedrawReg.Include(&oldVisible);
		}
copyReg.OffsetBy(dx, 0);
copyReg.IntersectWith(&fFullVisible);
GetRootLayer()->fRedrawReg.Exclude(&copyReg);
copyReg.OffsetBy(-dx, 0);
GetRootLayer()->CopyRegion(&copyReg, dx, 0);
		// clear canvas and set invalid regions for affected WinBorders
		GetRootLayer()->RequestRedraw(); // TODO: what if we pass (fParent, startFromTHIS, &redrawReg)?
	}

/*
	This works well! Above I'm trying to optimize things.

	if (!IsVisuallyHidden() && GetRootLayer())
	{
		BRegion oldFullVisible(fFullVisible);
		BRegion oldVisible(fVisible);

// OPT: you can we HW acceleration for either for bottom alligned layer or
//		for right alligned ones. investigate!

		// right, center and bottom alligned layers will change their position
		// so we need to invalidate their current visible regions
		BRegion redrawRightOrBottom;
		for (Layer *lay = VirtualBottomChild();
					lay; lay = VirtualUpperSibling())
		{
			uint16		rm = lay->fResizeMode & 0x0000FFFF;

			if ((rm & 0x0F0F) == (uint16)B_FOLLOW_RIGHT || (rm & 0x0F0F) == (uint16)B_FOLLOW_H_CENTER
				|| (rm & 0xF0F0) == (uint16)B_FOLLOW_BOTTOM || (rm & 0xF0F0) == (uint16)B_FOLLOW_V_CENTER
			// TODO: these 2 don't need to be redrawn entirely. but ATM we don't have a choice
				|| (rm & 0x0F0F) == (uint16)B_FOLLOW_LEFT_RIGHT || (rm & 0xF0F0) == (uint16)B_FOLLOW_TOP_BOTTOM)
			{
				redrawRightOrBottom.Include(&lay->fFullVisible);
			}
		}

		// we'll invalidate the old area and the new, maxmial one.
		BRegion invalid;
		get_user_regions(invalid);
		invalid.Include(&fFullVisible);

		clear_visible_regions();

		fParent->RebuildVisibleRegions(invalid, this);

		// done rebuilding regions, now redraw regions that became visible

		// what's invalid, are the differences between to old and the new fullVisible region
		// 1) in case we grow.
		BRegion		redrawReg(fFullVisible);
		redrawReg.Exclude(&oldFullVisible);
		// 2) in case we shrink
		BRegion		redrawReg2(oldFullVisible);
		redrawReg2.Exclude(&fFullVisible);
		// 3) combine.
		redrawReg.Include(&redrawReg2);

		// for center, right and bottom alligned layers, redraw their old positions
		redrawReg.Include(&redrawRightOrBottom);

		// layers that had their frame modified must be entirely redrawn.
		for (Layer *lay = VirtualBottomChild();
					lay; lay = VirtualUpperSibling())
		{
			uint16		rm = lay->fResizeMode & 0x0000FFFF;

			if ((rm & 0x0F0F) == (uint16)B_FOLLOW_RIGHT || (rm & 0x0F0F) == (uint16)B_FOLLOW_H_CENTER
				|| (rm & 0xF0F0) == (uint16)B_FOLLOW_BOTTOM || (rm & 0xF0F0) == (uint16)B_FOLLOW_V_CENTER
			// TODO: these 2 don't need to be redrawn entirely. but ATM we don't have a choice
				|| (rm & 0x0F0F) == (uint16)B_FOLLOW_LEFT_RIGHT || (rm & 0xF0F0) == (uint16)B_FOLLOW_TOP_BOTTOM)
			{
				redrawReg.Include(&lay->fFullVisible);
			}
		}

		// add redrawReg to our RootLayer's redraw region.
		GetRootLayer()->fRedrawReg.Include(&redrawReg);
		// include layer's visible region in case we want a full update on resize
		if (fFlags & B_FULL_UPDATE_ON_RESIZE && fVisible.Frame().IsValid())
		{
			GetRootLayer()->fRedrawReg.Include(&fVisible);
			GetRootLayer()->fRedrawReg.Include(&oldVisible);
		}
		// clear canvas and set invalid regions for affected WinBorders
		GetRootLayer()->RequestRedraw(); // TODO: what if we pass (fParent, startFromTHIS, &redrawReg)?
	}
*/
}

void Layer::MoveBy(float dx, float dy)
{
	if (dx == 0.0f && dy == 0.0f)
		return;

//	fFrame.Set(fFrame.left+dx, fFrame.top+dy, fFrame.right+dx, fFrame.bottom+dy);
	fFrame.OffsetBy(dx, dy);

	// call hook function
	MovedByHook(dx, dy);

	if (!IsVisuallyHidden() && GetRootLayer())
	{
		BRegion oldFullVisible(fFullVisible);

		// we'll invalidate the old position and the new, maxmial one.
		BRegion invalid;
		get_user_regions(invalid);
		invalid.Include(&fFullVisible);

		clear_visible_regions();

		fParent->RebuildVisibleRegions(invalid, this);

		// done rebuilding regions, now copy common parts and redraw regions that became visible

		// include the actual and the old fullVisible regions. later, we'll exclude the common parts.
		BRegion		redrawReg(fFullVisible);
		redrawReg.Include(&oldFullVisible);

		// offset to layer's new location so that we can calculate the common region.
		oldFullVisible.OffsetBy(dx, dy);

		// finally we have the region that needs to be redrawn.
		redrawReg.Exclude(&oldFullVisible);

		// by intersecting the old fullVisible offseted to layer's new location, with the current
		// fullVisible, we'll have the common region which can be copied using HW acceleration.
		oldFullVisible.IntersectWith(&fFullVisible);

		// offset back and instruct the HW to do the actual copying.
		oldFullVisible.OffsetBy(-dx, -dy);
		GetRootLayer()->CopyRegion(&oldFullVisible, dx, dy);

		// add redrawReg to our RootLayer's redraw region.
		GetRootLayer()->fRedrawReg.Include(&redrawReg);
		GetRootLayer()->RequestRedraw(); // TODO: what if we pass (fParent, startFromTHIS, &redrawReg)?
	}
}

void Layer::ScrollBy(float dx, float dy)
{
	fOrigin.Set(fOrigin.x + dx, fOrigin.y + dy);

	if (!IsVisuallyHidden() && GetRootLayer())
	{
		// set the region to be invalidated.
		BRegion		invalid(fFullVisible);

		clear_visible_regions();

		rebuild_visible_regions(invalid, invalid, VirtualBottomChild());

		// for the moment we say that the whole surface needs to be redraw.
		BRegion		redrawReg(fFullVisible);

		// offset old region so that we can start comparing.
		invalid.OffsetBy(dx, dy);

		// compute the common region. we'll use HW acc to copy this to the new location.
		invalid.IntersectWith(&fFullVisible);
		GetRootLayer()->CopyRegion(&invalid, -dx, -dy);

		// common region goes back to its original location. then, by excluding
		// it from curent fullVisible we'll obtain the region that needs to be redrawn.
		invalid.OffsetBy(-dx, -dy);
		redrawReg.Exclude(&invalid);

		GetRootLayer()->fRedrawReg.Include(&redrawReg);
		GetRootLayer()->RequestRedraw(); // TODO: what if we pass (fParent, startFromTHIS, &redrawReg)?
	}

	if (dx != 0.0f || dy != 0.0f)
		ScrolledByHook(dx, dy);
}

void Layer::GetWantedRegion(BRegion &reg)
{
	get_user_regions(reg);
}

void Layer::get_user_regions(BRegion &reg)
{
// OPT: maybe we should have all these cached in a 'fFull' member

	// 1) set to frame in screen coords
	BRect			screenFrame(Bounds());
	ConvertToScreen2(&screenFrame);
	reg.Set(screenFrame);

	// 2) intersect with screen region
// TODO: remove locking when for real
wind->Lock();
	BRegion			screenReg(GetRootLayer()->Bounds());
wind->Unlock();
	reg.IntersectWith(&screenReg);

// TODO: you MUST at some point uncomment this block!
/*
	// 3) impose user constrained regions
	LayerData		*stackData = fLayerData;
	while (stackData)
	{
		// transform in screen coords
		BRegion		screenReg(stackData->ClippingRegion());
		ConvertToScreen2(&screenReg);
		reg.IntersectWith(&screenReg);
		stackData	= stackData->prevState;
	}
*/
}

void Layer::RebuildVisibleRegions(const BRegion &invalid, const Layer *startFrom)
{
	BRegion		localVisible(fFullVisible);
	localVisible.IntersectWith(&invalid);
	rebuild_visible_regions(invalid, localVisible, startFrom);
}

void Layer::rebuild_visible_regions(const BRegion &invalid,
									const BRegion &parentLocalVisible,
									const Layer *startFrom)
{
	// no point in continuing if this layer is hidden. starting from here, all
	// descendants have (and will have) invalid visible regions.
	if (fHidden)
		return;

	// no need to go deeper if the parent doesn't have a visible region anymore
	// and our fullVisible region is also empty.
	if (!parentLocalVisible.Frame().IsValid() && !(fFullVisible.CountRects() > 0))
		return;

	bool fullRebuild = false;

	// intersect maximum wanted region with the invalid region
	BRegion common;
	get_user_regions(common);
	common.IntersectWith(&invalid);

	// if the resulted region is not valid, this layer is not in the catchment area
	// of the region being invalidated
	if (!common.CountRects() > 0)
		return;

	// now intersect with parent's visible part of the region that was/is invalidated
	common.IntersectWith(&parentLocalVisible);

	// exclude the invalid region
	fFullVisible.Exclude(&invalid);
	fVisible.Exclude(&invalid);

	// put in what's really visible
	fFullVisible.Include(&common);

	// this is to allow a layer to hide some parts of itself so children
	// won't take them.
	BRegion unalteredVisible(common);
	bool altered = alter_visible_for_children(common);

	for (Layer *lay = VirtualBottomChild(); lay ; lay = VirtualUpperSibling())
	{
		if (lay == startFrom)
			fullRebuild = true;

		if (fullRebuild)
			lay->rebuild_visible_regions(invalid, common, lay->VirtualBottomChild());

		// to let children know much they can take from parent's visible region
		common.Exclude(&lay->fFullVisible);
		// we've hidden some parts of our visible region from our children,
		// and we must be in sysnc with this region too...
		if (altered)
			unalteredVisible.Exclude(&lay->fFullVisible);
	}

	// the visible region of this layer is what left after all its children took
	// what they could.
	if (altered)
		fVisible.Include(&unalteredVisible);
	else
		fVisible.Include(&common);
}

bool Layer::alter_visible_for_children(BRegion &reg)
{
	// Empty Hook function
	return false;
}

void Layer::clear_visible_regions()
{
	// OPT: maybe we should uncomment these lines for performance
//	if (fFullVisible.CountRects() <= 0)
//		return;

	fVisible.MakeEmpty();
	fFullVisible.MakeEmpty();
	for (Layer *child = VirtualBottomChild(); child;
				child = VirtualUpperSibling())
		child->clear_visible_regions();
}

void Layer::PrintToStream() const
{
	printf("-> %s\n", fName);
	fVisible.PrintToStream();
	fFullVisible.PrintToStream();
	for (Layer *child = VirtualBottomChild(); child;
				child = VirtualUpperSibling())
		child->PrintToStream();
}
