//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		Layer.cpp
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	Class used for rendering to the frame buffer. One layer per 
//					view on screen and also for window decorators
//  
//------------------------------------------------------------------------------
#include <View.h>
#include <string.h>
#include <stdio.h>
#include "Layer.h"
#include "RectUtils.h"
#include "ServerWindow.h"
#include "PortLink.h"
#include "TokenHandler.h"
#include "RectUtils.h"
#include "RootLayer.h"

/*!
	\brief Constructor
	\param frame Size and placement of the Layer
	\param name Name of the layer
	\param resize Resizing flags as defined in View.h
	\param flags BView flags as defined in View.h
	\param win ServerWindow to which the Layer belongs
*/
Layer::Layer(BRect frame, const char *name, int32 token, int32 resize,
				int32 flags, ServerWindow *win)
{
	// frame is in _parent layer's coordinates
	if(frame.IsValid())
		_frame		= frame;
	else
		_frame.Set(0, 0, 5, 5);

	_name			= new BString(name);
	// Layer does not start out as a part of the tree
	_parent			= NULL;
	_uppersibling	= NULL;
	_lowersibling	= NULL;
	_topchild		= NULL;
	_bottomchild	= NULL;

	_visible		= new BRegion( _frame );
	_full			= new BRegion( _frame );
	_invalid		= new BRegion( _frame );

	_flags			= flags;
	_hidden			= false;
	_is_dirty		= false;
	_is_updating	= false;
	_regions_invalid= false;
	_level			= 0;
	_view_token		= token;
	_layerdata		= new LayerData;
	
	_serverwin		= win;
		// what's this needed for?
	_portlink		= NULL;
}

//! Destructor frees all allocated heap space
Layer::~Layer(void)
{
	if(_visible)
	{
		delete _visible;
		_visible=NULL;
	}
	if(_full)
	{
		delete _full;
		_full=NULL;
	}
	if(_invalid)
	{
		delete _invalid;
		_invalid=NULL;
	}
	if(_name)
	{
		delete _name;
		_name=NULL;
	}
	if(_layerdata)
	{
		delete _layerdata;
		_layerdata=NULL;
	}
}

/*!
	\brief Adds a child to the back of the a layer's child stack.
	\param layer The layer to add as a child
	\param before Add the child in front of this layer
	\param rebuild Flag to fully rebuild all visibility regions
*/
void Layer::AddChild(Layer *layer, Layer *before, bool rebuild)
{
	// TODO: Add before support
printf("Layer::AddChild lacks before support\n");

	if(layer->_parent!=NULL)
	{
		printf("ERROR: AddChild(): Layer already has a _parent\n");
		return;
	}
	layer->_parent=this;
	if(layer->_visible && layer->_hidden==false && _visible)
	{
		RootLayer	*rl;
		rl			= dynamic_cast<RootLayer*>(this);
		if ( rl ){
			// RootLayer enters here. It does not need to exclude WinBorder's
			// visible area!
		}
		else{
			// Technically, we could safely take the address of ConvertToParent(BRegion)
			// but we don't just to avoid a compiler nag
			BRegion 	reg(layer->ConvertToParent(layer->_visible));
			_visible->Exclude(&reg);
		}
	}

	// we need to change this to a loop for each _lowersibling of the layer
	if(_bottomchild)
	{
		// we're adding to the front side of the stack
		layer->_uppersibling=_bottomchild;
		
		// added layer will be at the bottom of the stack
		_bottomchild->_lowersibling=layer;

	}
	else
		_topchild=layer;

	_bottomchild=layer;
	layer->_level=_level+1;

	if(rebuild)
		RebuildRegions(true);
}

/*!
	\brief Removes a layer from the child stack
	\param layer The layer to remove
	\param rebuild Flag to rebuild all visibility regions
*/
void Layer::RemoveChild(Layer *layer, bool rebuild)
{
	if(layer->_parent==NULL)
	{
		printf("ERROR: RemoveChild(): Layer doesn't have a _parent\n");
		return;
	}
	if(layer->_parent!=this)
	{
		printf("ERROR: RemoveChild(): Layer is not a child of this layer\n");
		return;
	}

	if( !_hidden && layer->_visible && layer->_parent->_visible)
	{
		BRegion		reg(ConvertToParent(_visible));
		layer->_parent->_visible->Include(&reg);
	}

	// Take care of _parent
	layer->_parent=NULL;
	if(_topchild==layer)
		_topchild=layer->_lowersibling;
	if(_bottomchild==layer)
		_bottomchild=layer->_uppersibling;

	// Take care of siblings
	if(layer->_uppersibling!=NULL)
		layer->_uppersibling->_lowersibling=layer->_lowersibling;
	if(layer->_lowersibling!=NULL)
		layer->_lowersibling->_uppersibling=layer->_uppersibling;
	layer->_uppersibling=NULL;
	layer->_lowersibling=NULL;
	if(rebuild)
		RebuildRegions(true);
}

/*!
	\brief Removes the layer from its parent's child stack
	\param rebuild Flag to rebuild visibility regions
*/
void Layer::RemoveSelf(bool rebuild)
{
	// A Layer removes itself from the tree (duh)
	if(_parent==NULL)
	{
		printf("ERROR: RemoveSelf(): Layer doesn't have a _parent\n");
		return;
	}
	Layer *p=_parent;
	_parent->RemoveChild(this);
	
	if(rebuild)
		p->RebuildRegions(true);
}

/*!
	\brief Finds the first child at a given point.
	\param pt Point to look for a child
	\param recursive Flag to look for the bottom-most child
	\return non-NULL if found, NULL if not

	Find out which child gets hit if we click at a certain spot. Returns NULL
	if there are no _visible children or if the click does not hit a child layer
	If recursive==true, then it will continue to call until it reaches a layer
	which has no children, i.e. a layer that is at the top of its 'branch' in
	the layer tree
*/
Layer *Layer::GetChildAt(BPoint pt, bool recursive)
{
	Layer *child;
	if(recursive)
	{
		for(child=_bottomchild; child!=NULL; child=child->_uppersibling)
		{
			if(child->_bottomchild!=NULL)
				child->GetChildAt(pt,true);
			
			if(child->_hidden)
				continue;
			
			if(child->_frame.Contains(pt))
				return child;
		}
	}
	else
	{
		for(child=_bottomchild; child!=NULL; child=child->_uppersibling)
		{
			if(child->_hidden)
				continue;
			if(child->_frame.Contains(pt))
				return child;
		}
	}
	return NULL;
}

/*!
	\brief Returns the size of the layer
	\return the size of the layer
*/
BRect Layer::Bounds(void)
{
	return _frame.OffsetToCopy(0,0);
}

/*!
	\brief Returns the layer's size and position in its parent coordinates
	\return The layer's size and position in its parent coordinates
*/
BRect Layer::Frame(void)
{
	return _frame;
}

/*!
	\brief recursively deletes all children (and grandchildren, etc) of the layer

	This is mostly used for server shutdown or deleting a workspace
*/
void Layer::PruneTree(void)
{
	Layer *lay,*nextlay;

	lay=_topchild;
	_topchild=NULL;
	
	while(lay!=NULL)
	{
		if(lay->_topchild!=NULL)
		{
			lay->PruneTree();
		}
		nextlay=lay->_lowersibling;
		lay->_lowersibling=NULL;
		delete lay;
		lay=nextlay;
	}
	// Man, this thing is short. Elegant, ain't it? :P
}

/*!
	\brief Finds a layer based on its token ID
	\return non-NULL if found, NULL if not
*/
Layer *Layer::FindLayer(int32 token)
{
	// recursive search for a layer based on its view token
	Layer *lay, *trylay;

	// Search child layers first
	for(lay=_topchild; lay!=NULL; lay=lay->_lowersibling)
	{
		if(lay->_view_token==token)
			return lay;
	}
	
	// Hmmm... not in this layer's children. Try lower descendants
	for(lay=_topchild; lay!=NULL; lay=lay->_lowersibling)
	{
		trylay=lay->FindLayer(token);
		if(trylay)
			return trylay;
	}
	
	// Well, we got this far in the function, so apparently there is no match to be found
	return NULL;
}

/*!
	\brief Sets a region as invalid and, thus, needing to be drawn
	\param The region to invalidate
	
	All children of the layer also receive this call, so only 1 Invalidate call is 
	needed to set a section as invalid on the screen.
*/
void Layer::Invalidate(BRegion& region)
{
	int32 i;
	BRect r;

	// See if the region intersects with our current area
	if(region.Intersects(Bounds()) && !_hidden)
	{
		BRegion clippedreg(region);
		clippedreg.IntersectWith(_visible);
		if(clippedreg.CountRects()>0)
		{
			_is_dirty=true;
			if(_invalid)
				_invalid->Include(&clippedreg);
			else
				_invalid=new BRegion(clippedreg);
		}		
	}
	
	BRegion *reg;
	for(Layer *lay=_topchild;lay!=NULL; lay=lay->_lowersibling)
	{
		if(lay->_hidden==false)
		{	
			reg=new BRegion(lay->ConvertFromParent(&region));
	
			for(i=0;i<reg->CountRects();i++)
			{
				r=reg->RectAt(i);
				if(_frame.Intersects(r))
					lay->Invalidate(r);
			}
	
			delete reg;
		}
	}

}

/*!
	\brief Sets a rectangle as invalid and, thus, needing to be drawn
	\param The rectangle to invalidate
	
	All children of the layer also receive this call, so only 1 Invalidate call is 
	needed to set a section as invalid on the screen.
*/
void Layer::Invalidate(const BRect &rect)
{
	// Make our own section dirty and pass it on to any children, if necessary....
	// YES, WE ARE SHARING DIRT! Mudpies anyone? :D
	if(TestRectIntersection(Bounds(),rect))
	{
		// Clip the rectangle to the _visible region of the layer
		if(TestRegionIntersection(_visible,rect))
		{
			BRegion reg(*_visible);
			BRegion rectreg(rect);
			
//			IntersectRegionWith(&reg,rect);
			reg.IntersectWith(&rectreg);
			if(reg.CountRects()>0)
			{
				_is_dirty=true;
				if(_invalid)
					_invalid->Include(&reg);
				else
					_invalid=new BRegion(reg);
			}
			else
			{
			}
		}
		else
		{
		}
	}	
	for(Layer *lay=_topchild;lay!=NULL; lay=lay->_lowersibling)
		lay->Invalidate(lay->ConvertFromParent(rect));
}

/*!
	\brief Ask the layer's BView to draw itself
	\param r The area that needs to be drawn
*/
void Layer::RequestDraw(const BRect &r)
{
printf("Layer: %s: RequestDraw(%.1f,%.1f,%.1f,%.1f) - unimplemented\n",
	_name->String(),r.left,r.top,r.right,r.bottom);

	// TODO: Implement and fix
/*	if(_visible==NULL || _hidden)
		return;

	if(_serverwin)
	{
		if(_invalid==NULL)
			_invalid=new BRegion(*_visible);
		_serverwin->RequestDraw(_invalid->Frame());
		delete _invalid;
		_invalid=NULL;
	}

	_is_dirty=false;
	for(Layer *lay=_topchild; lay!=NULL; lay=lay->_lowersibling)
	{
		if(lay->IsDirty())
			lay->RequestDraw();
	}
*/
}

void Layer::RequestDraw(void)
{
	RequestDraw(Bounds());
}

/*!
	\brief Determines whether the layer needs to be redrawn
	\return True if the layer needs to be redrawn, false if not
*/
bool Layer::IsDirty(void) const
{
	//return (!_invalid)?true:false;
	return _is_dirty;
}

/*!
	\brief Forces a repaint if there are invalid areas
	\param force_update Force an update. False by default.
*/
void Layer::UpdateIfNeeded(bool force_update)
{
	if(IsHidden())
		return;

	Layer *child;
	
	if(force_update)
	{
		if(_invalid)
			RequestDraw(_invalid->Frame());
		else
			RequestDraw();
	}
	else
	{
		if(_invalid)
			RequestDraw(_invalid->Frame());
	}

	for(child=_bottomchild; child!=NULL; child=child->_uppersibling)
		child->UpdateIfNeeded(force_update);
	_is_dirty=false;
}

/*!
	\brief Marks the layer as needing a region rebuild if intersecting the given rect
	\param rect The rectangle for checking to see if the layer needs to be rebuilt
*/
void Layer::MarkModified(BRect rect)
{
	if(TestRectIntersection(Bounds(),rect))
		_regions_invalid=true;
	
	Layer *child;
	for(child=_bottomchild; child!=NULL; child=child->_uppersibling)
		child->MarkModified(rect.OffsetByCopy(-child->_frame.left,-child->_frame.top));
}

/*!
	\brief Rebuilds the layer's regions and updates the screen as needed
	\param force Force an update
*/
void Layer::UpdateRegions(bool force)
{
	if(force)
	{
		RebuildRegions(true);
//		MoveChildren();
//		InvalidateNewAreas();
	}
	
	if( (_regions_invalid && (_parent==NULL) && _invalid) || force)
	    UpdateIfNeeded(force);
}

//! Show the layer. Operates just like the BView call with the same name
void Layer::Show(void)
{
	if( !_hidden )
		return;
	
	_hidden		= false;

	if( _parent ){
		BRegion 	reg(ConvertToParent(_visible));
		_parent->_visible->Exclude( &reg );
		_parent->_is_dirty=true;
	}
	_is_dirty=true;

	if( _parent ){
		Layer *sibling;
		for (sibling=_parent->_bottomchild; sibling!=NULL; sibling=sibling->_uppersibling)
		{
			if(TestRectIntersection(sibling->_frame,_frame)) 
				sibling->MarkModified(_frame.OffsetByCopy(-sibling->_frame.left,-sibling->_frame.top));
		}
	}

	Layer *child;
	for(child=_topchild; child!=NULL; child=child->_lowersibling)
		child->Show();

	if(_parent){
		_parent->RebuildRegions(true);
		_parent->UpdateIfNeeded();
	}
	
	UpdateIfNeeded();
}

//! Hide the layer. Operates just like the BView call with the same name
void Layer::Hide(void)
{
	if ( _hidden )
		return;

	_hidden		= true;

	BRegion 	reg(ConvertToParent(_visible));
	_parent->_visible->Include( &reg );

	_parent->_is_dirty=true;
	_is_dirty=true;
	
	Layer *child;
	for(child=_topchild; child!=NULL; child=child->_lowersibling)
		child->Hide();
}

/*!
	\brief Determines whether the layer is hidden or not
	\return true if hidden, false if not.
*/
bool Layer::IsHidden(void)
{
	return _hidden;
}

/*!
	\brief Counts the number of children the layer has
	\return the number of children the layer has, not including grandchildren
*/
uint32 Layer::CountChildren(void)
{
	uint32 i=0;
	Layer *lay=_topchild;
	while(lay!=NULL)
	{
		lay=lay->_lowersibling;
		i++;
	}
	return i;
}

/*!
	\brief Moves a layer in its parent coordinate space
	\param x X offset
	\param y Y offset
*/
void Layer::MoveBy(float x, float y)
{
	_frame.OffsetBy(x,y);

	BRegion		oldVisible( *_visible );
	_visible->OffsetBy( x, y );
	_full->OffsetBy( x, y );

	if(_parent)
	{
		BRegion		exclude(oldVisible);
		exclude.Exclude(_visible);

		if(_parent->_invalid == NULL)
			_parent->_invalid = new BRegion( exclude );
		else
			_parent->_invalid->Include( &exclude );

		_parent->_is_dirty	= true;

		// if _uppersibling is non-NULL, we have other layers which we may have been
		// covering up. If we did cover up some siblings, they need to be redrawn
		for(Layer *sib=_uppersibling;sib!=NULL;sib=sib->_uppersibling)
		{
			BRegion		exclude2(oldVisible);
			exclude2.Exclude(sib->_visible);
			
			if( exclude2.CountRects() != 0 )
			{
				// The boundaries intersect on screen, so invalidate the area that
				// was hidden
				// a new region, becase we do not want to modify 'oldVisible'
				BRegion		exclude3(oldVisible);
				exclude3.IntersectWith(sib->_visible);
				
				sib->Invalidate( exclude3 );
				sib->_is_dirty=true;
			}
		}
	}
	_is_dirty=true;
}

/*!
	\brief Resizes the layer.
	\param x X offset
	\param y Y offset
	
	This resizes the layer itself and resizes any children based on their resize
	flags.
*/
void Layer::ResizeBy(float x, float y)
{
	// TODO: Implement and test child resizing based on flags
	
	BRect oldframe=_frame;
	_frame.right+=x;
	_frame.bottom+=y;

//	for(Layer *lay=_topchild; lay!=NULL; lay=lay->_lowersibling)
//		lay->ResizeBy(x,y);

	if(_parent)
		_parent->RebuildRegions(true);
	else
		RebuildRegions(true);
	if(x<0 || y<0)
		_parent->Invalidate(oldframe);
}

/*!
	\brief Rebuilds visibility regions for child layers
	\param include_children Flag to rebuild all children and subchildren
*/
void Layer::RebuildRegions(bool include_children)
{
	// Algorithm:
	// 1) Reset child visibility regions to completely visible
	// 2) Clip each child to visible region of this layer
	// 3) Clip each child to its siblings, going front to back
	// 4) Remove the visible regions of the children from the current one
	
	// Reset children to fully visible and clip to this layer's visible region
	for(Layer *childlay=_topchild; childlay!=NULL; childlay=childlay->_lowersibling)
	{
		childlay->_visible->MakeEmpty();
		childlay->_visible->Include(childlay->_full);

		if(childlay->_visible && childlay->_hidden==false)
			childlay->_visible->IntersectWith(_visible);

	}

	// This region is the common clipping region used when clipping children to their
	// siblings. We will use this because of efficiency - it is gradually built by
	// first clipping a child to the region if the region is not empty and then
	// adding the child's resulting footprint to the clipping region. Once all the
	// children have been clipped, then the resulting region will allow for one call
	// to remove the footprints of all the children from the layer's visible region.
	BRegion clipregion;
	clipregion.MakeEmpty();

	// Clip children to siblings which are closer to the front
	for(Layer *siblay=_bottomchild; siblay!=NULL; siblay=siblay->_uppersibling)
	{
		if( clipregion.CountRects()>0 &&
			TestRectIntersection(siblay->Frame(),clipregion.Frame()) && 
			siblay->_visible && 
			siblay->_hidden==false )
		{
			siblay->_visible->Exclude(&clipregion);
		}
		clipregion.Include(siblay->_visible);
	}
	
	// Rebuild the regions for subchildren if we're supposed to
	if(include_children)
	{
		for(Layer *lay=_topchild; lay!=NULL; lay=lay->_lowersibling)
		{
			if(lay->_topchild)
				lay->RebuildRegions(true);
		}
	}
	_regions_invalid=false;
}

//! Prints all relevant layer data to stdout
void Layer::PrintToStream(void)
{
	printf("-----------\nLayer %s\n",_name->String());
	if(_parent)
		printf("Parent: %s (%p)\n",_parent->_name->String(), _parent);
	else
		printf("Parent: NULL\n");
	if(_uppersibling)
		printf("Upper sibling: %s (%p)\n",_uppersibling->_name->String(), _uppersibling);
	else
		printf("Upper sibling: NULL\n");
	if(_lowersibling)
		printf("Lower sibling: %s (%p)\n",_lowersibling->_name->String(), _lowersibling);
	else
		printf("Lower sibling: NULL\n");
	if(_topchild)
		printf("Top child: %s (%p)\n",_topchild->_name->String(), _topchild);
	else
		printf("Top child: NULL\n");
	if(_bottomchild)
		printf("Bottom child: %s (%p)\n",_bottomchild->_name->String(), _bottomchild);
	else
		printf("Bottom child: NULL\n");
	printf("Frame: "); _frame.PrintToStream();
	printf("Token: %ld\nLevel: %ld\n",_view_token, _level);
	printf("Hide count: %s\n",_hidden?"true":"false");
	if(_invalid)
	{
		printf("Invalid Areas: "); _invalid->PrintToStream();
	}
	else
		printf("Invalid Areas: NULL\n");
	if(_visible)
	{
		printf("Visible Areas: "); _visible->PrintToStream();
	}
	else
		printf("Visible Areas: NULL\n");
	printf("Is updating = %s\n",(_is_updating)?"yes":"no");
}

//! Prints hierarchy data to stdout
void Layer::PrintNode(void)
{
	printf("-----------\nLayer %s\n",_name->String());
	if(_parent)
		printf("Parent: %s (%p)\n",_parent->_name->String(), _parent);
	else
		printf("Parent: NULL\n");
	if(_uppersibling)
		printf("Upper sibling: %s (%p)\n",_uppersibling->_name->String(), _uppersibling);
	else
		printf("Upper sibling: NULL\n");
	if(_lowersibling)
		printf("Lower sibling: %s (%p)\n",_lowersibling->_name->String(), _lowersibling);
	else
		printf("Lower sibling: NULL\n");
	if(_topchild)
		printf("Top child: %s (%p)\n",_topchild->_name->String(), _topchild);
	else
		printf("Top child: NULL\n");
	if(_bottomchild)
		printf("Bottom child: %s (%p)\n",_bottomchild->_name->String(), _bottomchild);
	else
		printf("Bottom child: NULL\n");
	if(_visible)
	{
		printf("Visible Areas: "); _visible->PrintToStream();
	}
	else
		printf("Visible Areas: NULL\n");
}

/*!
	\brief Converts the rectangle to the layer's parent coordinates
	\param the rectangle to convert
	\return the converted rectangle
*/
BRect Layer::ConvertToParent(BRect rect)
{
	return (rect.OffsetByCopy(_frame.LeftTop()));
}

/*!
	\brief Converts the region to the layer's parent coordinates
	\param the region to convert
	\return the converted region
*/
BRegion Layer::ConvertToParent(BRegion *reg)
{
	BRegion newreg;
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include(ConvertToParent(reg->RectAt(i)));
	return BRegion(newreg);
}

/*!
	\brief Converts the rectangle from the layer's parent coordinates
	\param the rectangle to convert
	\return the converted rectangle
*/
BRect Layer::ConvertFromParent(BRect rect)
{
	return (rect.OffsetByCopy(_frame.left*-1,_frame.top*-1));
}

/*!
	\brief Converts the region from the layer's parent coordinates
	\param the region to convert
	\return the converted region
*/
BRegion Layer::ConvertFromParent(BRegion *reg)
{
	BRegion newreg;
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include(ConvertFromParent(reg->RectAt(i)));
	return BRegion(newreg);
}

/*!
	\brief Converts the region to screen coordinates
	\param the region to convert
	\return the converted region
*/
BRegion Layer::ConvertToTop(BRegion *reg)
{
	BRegion newreg;
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include(ConvertToTop(reg->RectAt(i)));
	return BRegion(newreg);
}

/*!
	\brief Converts the rectangle to screen coordinates
	\param the rectangle to convert
	\return the converted rectangle
*/
BRect Layer::ConvertToTop(BRect rect)
{
	if (_parent!=NULL)
		return(_parent->ConvertToTop(rect.OffsetByCopy(_frame.LeftTop())) );
	else
		return(rect);
}

/*!
	\brief Converts the region from screen coordinates
	\param the region to convert
	\return the converted region
*/
BRegion Layer::ConvertFromTop(BRegion *reg)
{
	BRegion newreg;
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include(ConvertFromTop(reg->RectAt(i)));
	return BRegion(newreg);
}

/*!
	\brief Converts the rectangle from screen coordinates
	\param the rectangle to convert
	\return the converted rectangle
*/
BRect Layer::ConvertFromTop(BRect rect)
{
	if (_parent!=NULL)
		return(_parent->ConvertFromTop(rect.OffsetByCopy(_frame.LeftTop().x*-1,
			_frame.LeftTop().y*-1)) );
	else
		return(rect);
}

/*!
	\brief Makes the layer the backmost layer belonging to its parent
	
	This function will do nothing if the Layer has no parent or no siblings. Region 
	rebuilding is not performed.
*/
void Layer::MakeTopChild(void)
{
	// Handle redundant and pointless cases
	if(!_parent || (!_uppersibling && !_lowersibling) || _parent->_topchild==this)
		return;
	
	// Pull ourselves out of the layer tree
	if(_uppersibling)
		_uppersibling->_lowersibling=_lowersibling;
		
	if(_lowersibling)
		_lowersibling->_uppersibling=_uppersibling;
	
	// Set this layer's upper/lower sib pointers to appropriate values
	_uppersibling=NULL;
	_lowersibling=_parent->_topchild;
	
	// move former top child down a layer
	_lowersibling->_uppersibling=this;
	_parent->_topchild=this;
}

/*!
	\brief Makes the layer the frontmost layer belonging to its parent
	
	This function will do nothing if the Layer has no parent or no siblings. Region 
	rebuilding is not performed.
*/
void Layer::MakeBottomChild(void)
{
	// Handle redundant and pointless cases
	if(!_parent || (!_uppersibling && !_lowersibling) || _parent->_bottomchild==this)
		return;
	
	// Pull ourselves out of the layer tree
	if(_uppersibling)
		_uppersibling->_lowersibling=_lowersibling;
		
	if(_lowersibling)
		_lowersibling->_uppersibling=_uppersibling;
	
	// Set this layer's upper/lower sib pointers to appropriate values
	_uppersibling=_parent->_bottomchild;
	_lowersibling=NULL;
	
	// move former bottom child up a layer
	_uppersibling->_lowersibling=this;
	_parent->_bottomchild=this;
	
}
