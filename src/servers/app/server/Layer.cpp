//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, Haiku, Inc.
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
//					Adi Oanca <adioanca@myrealbox.com>
//	Description:	Class used for rendering to the frame buffer. One layer per 
//					view on screen and also for window decorators
//  
//------------------------------------------------------------------------------
#include <View.h>
#include <Message.h>
#include <AppDefs.h>
#include <Region.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "Layer.h"
#include "ServerWindow.h"
#include "WinBorder.h"
#include "RGBColor.h"
#include "RootLayer.h"
#include "DisplayDriver.h"
#include "LayerData.h"
#include "PortLink.h"
#include "ServerProtocol.h"

//#define DEBUG_LAYER
#ifdef DEBUG_LAYER
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

//#define DEBUG_LAYER_REBUILD
#ifdef DEBUG_LAYER_REBUILD
#	define RBTRACE(x) printf x
#else
#	define RBTRACE(x) ;
#endif

enum {
	B_LAYER_ACTION_NONE = 0,
	B_LAYER_ACTION_MOVE,
	B_LAYER_ACTION_RESIZE
};

Layer::Layer(BRect frame, const char *name, int32 token, uint32 resize,
				uint32 flags, DisplayDriver *driver)
{
	// frame is in fParent coordinates
	if(frame.IsValid())
		fFrame		= frame;
	else
		// TODO: Decorator class should have a method witch returns the minimum frame width.
		fFrame.Set(0.0f, 0.0f, 5.0f, 5.0f);

	fBoundsLeftTop.Set( 0.0f, 0.0f );

	fName			= new BString(name ? name : B_EMPTY_STRING);
	fLayerData		= new LayerData();

	// driver init
	if (!driver)
		debugger("You MUST have a valid driver to init a Layer object\n");
	fDriver			= driver;

	// Layer does not start out as a part of the tree
	fParent			= NULL;
	fUpperSibling	= NULL;
	fLowerSibling	= NULL;
	fTopChild		= NULL;
	fBottomChild	= NULL;
	
	fCurrent		= NULL;
	fRootLayer		= NULL;

	fFlags			= flags;
	fAdFlags		= 0;
	fClassID		= AS_LAYER_CLASS;
	fFrameAction	= B_LAYER_ACTION_NONE;
	fResizeMode		= resize;
	fHidden			= false;
	
	fInUpdate		= false;
	fIsTopLayer		= false;
	fLevel			= 0;
	
	fViewToken		= token;
	fServerWin		= NULL;
	clipToPicture	= NULL;
	
	// NOW all regions (fVisible, fFullVisible, fFull) are empty
	fClipReg		= &fVisible;
	STRACE(("Layer(%s) successfuly created\n", GetName()));
}

//! Destructor frees all allocated heap space
Layer::~Layer(void)
{
	if(fLayerData)
	{
		delete fLayerData;
		fLayerData = NULL;
	}
	
	if(fName)
	{
		delete fName;
		fName = NULL;
	}
	
	// TODO: uncomment!
	//PruneTree();
	
//	fServerWin->RemoveChild(fDriver);
//	delete fDriver;
	
	if (clipToPicture)
	{
		delete clipToPicture;
		clipToPicture = NULL;
		
		// TODO: allocate and release a ServerPicture Object.
	}
}

/*!
	\brief Adds a child layer to the current one
	\param layer a new child layer
	\param serverWin the serverwindow to which the layer will belong
	
	Unlike the BView version, if the layer already belongs to another, then
	it spits an error to stdout and returns.
*/
void Layer::AddChild(Layer *layer, ServerWindow *serverWin)
{
	STRACE(("Layer(%s)::AddChild(%s) START\n", GetName(), layer->GetName()));
	
	if( layer->fParent != NULL ) 
	{
		printf("ERROR: AddChild(): Layer already has a parent\n");
		return;
	}
	
	// 1) attach layer to the tree structure
	layer->fParent = this;
	
	// if we have children already, bump the current front child back one and
	// make the new child the frontmost layer
	if( fBottomChild )
	{
		layer->fUpperSibling = fBottomChild;
		fBottomChild->fLowerSibling	= layer;
	}
	else
	{
		fTopChild = layer;
	}
	fBottomChild = layer;

	// if we have no RootLayer yet, then there is no need to set any parameters --
	// they will be set when the RootLayer for this tree will be added
	// to the main tree structure.
	if (!fRootLayer)
	{
		STRACE(("Layer(%s)::AddChild(%s) END\n", GetName(), layer->GetName()));
		return;
	}

	// 2) Iterate over the newly-added layer and all its children, setting the 
	//	root layer and server window and also rebuilding the full-size region
	//	for every descendant of the newly-added layer
	
	//c = short for: current
	Layer *c = layer;
	Layer *stop = layer;
	while( true )
	{
		// action block

		// 2.1) set the RootLayer for this object.
		c->SetRootLayer(c->fParent->fRootLayer);
		
		// 2.2) this Layer must know if it has a ServerWindow object attached.
		fServerWin=serverWin;
		
		// 2.3) we are attached to the main tree so build our full region.
		c->RebuildFullRegion();
		
		// tree parsing algorithm
		if(	c->fTopChild )
		{
			// go deep
			c = c->fTopChild;
		}
		else
		{
			// go right or up
			
			if (c == stop) // out trip is over
				break;
				
			if( c->fLowerSibling )
			{
				// go right
				c = c->fLowerSibling;
			}
			else
			{
				// go up
				while( !c->fParent->fLowerSibling && c->fParent != stop )
					c = c->fParent;
				
				if( c->fParent == stop ) // that's enough!
					break;
				
				c = c->fParent->fLowerSibling;
			}
		}
	}

	STRACE(("Layer(%s)::AddChild(%s) END\n", GetName(), layer->GetName()));
}

/*!
	\brief Removes a child layer from the current one
	\param layer the layer to remove
	
	If the layer does not belong to the the current layer, then this function 
	spits out an error to stdout and returns
*/
void Layer::RemoveChild(Layer *layer)
{
	STRACE(("Layer(%s)::RemoveChild(%s) START\n", GetName(), layer->GetName()));
	
	if(!layer->fParent)
	{
		printf("ERROR: RemoveChild(): Layer doesn't have a fParent\n");
		return;
	}
	
	if(layer->fParent != this)
	{
		printf("ERROR: RemoveChild(): Layer is not a child of this layer\n");
		return;
	}

	// 1) remove this layer from the main tree.
	
	// Take care of fParent
	layer->fParent = NULL;
	
	if( fTopChild == layer )
		fTopChild = layer->fLowerSibling;
	
	if( fBottomChild == layer )
		fBottomChild = layer->fUpperSibling;
	
	// Take care of siblings
	if( layer->fUpperSibling != NULL )
		layer->fUpperSibling->fLowerSibling	= layer->fLowerSibling;
	
	if( layer->fLowerSibling != NULL )
		layer->fLowerSibling->fUpperSibling = layer->fUpperSibling;
	
	layer->fUpperSibling = NULL;
	layer->fLowerSibling = NULL;

	// 2) Iterate over all of the removed-layer's descendants and unset the
	//	root layer, server window, and all redraw-related regions
	
	Layer *c = layer; //c = short for: current
	Layer *stop = layer;
	
	while( true )
	{
		// action block
		{
			// 2.1) set the RootLayer for this object.
			c->SetRootLayer(NULL);
			// 2.2) this Layer must know if it has a ServerWindow object attached.
			fServerWin=NULL;
			// 2.3) we were removed from the main tree so clear our full region.
			c->fFull.MakeEmpty();
			// 2.4) clear fullVisible region.
			c->fFullVisible.MakeEmpty();
			// 2.5) we don't have a visible region anymore.
			c->fVisible.MakeEmpty();
		}

		// tree parsing algorithm
		if(	c->fTopChild )
		{	
			// go deep
			c = c->fTopChild;
		}
		else
		{	
			// go right or up
			if (c == stop) // out trip is over
				break;

			if( c->fLowerSibling )
			{
				// go right
				c = c->fLowerSibling;
			}
			else
			{
				// go up
				while( !c->fParent->fLowerSibling && c->fParent != stop )
					c = c->fParent;
				
				if( c->fParent == stop ) // that enough!
					break;
				
				c = c->fParent->fLowerSibling;
			}
		}
	}
	STRACE(("Layer(%s)::RemoveChild(%s) END\n", GetName(), layer->GetName()));
}

//! Removes the calling layer from the tree
void Layer::RemoveSelf()
{
	// A Layer removes itself from the tree (duh)
	if( fParent == NULL )
	{
		printf("ERROR: RemoveSelf(): Layer doesn't have a fParent\n");
		return;
	}
	fParent->RemoveChild(this);
}

/*!
	\brief Determins if the calling layer has the passed layer as a child
	\return true if the child is owned by the caller, false if not
*/
bool Layer::HasChild(Layer* layer)
{
	for(Layer *lay = VirtualTopChild(); lay; lay = VirtualLowerSibling())
	{
		if(lay == layer)
			return true;
	}
	return false;
}

/*!
	\brief Returns the layer at the given point
	\param pt The point to look the layer at
	\return The layer containing the point or NULL if no layer found
*/
Layer* Layer::LayerAt(const BPoint &pt)
{
	if (fVisible.Contains(pt))
		return this;
	
	if (fFullVisible.Contains(pt))
	{
		Layer *lay = NULL;
		for ( Layer* child = VirtualBottomChild(); child; child = VirtualUpperSibling() )
		{
			lay = child->LayerAt( pt );
			if (lay)
				return lay;
		}
	}
	
	return NULL;
}

//! Matches the BView call of the same name
BRect Layer::Bounds(void) const
{
	BRect r(fFrame);
	r.OffsetTo( fBoundsLeftTop );
	return r;
}

//! Matches the BView call of the same name
BRect Layer::Frame(void) const
{
	return fFrame;
}

//! Recursively deletes all children of the calling layer
void Layer::PruneTree(void)
{
	Layer *lay;
	Layer *nextlay;
	
	lay = fTopChild;
	fTopChild = NULL;
	
	while(lay != NULL)
	{
		if(lay->fTopChild != NULL)
			lay->PruneTree();
		
		nextlay = lay->fLowerSibling;
		lay->fLowerSibling = NULL;
		
		delete lay;
		lay = nextlay;
	}
	// Man, this thing is short. Elegant, ain't it? :P
}

/*!
	\brief Finds a child of the caller based on its token ID
	\param token ID of the layer to find
	\return Pointer to the layer or NULL if not found
*/
Layer *Layer::FindLayer(const int32 token)
{
	// recursive search for a layer based on its view token
	Layer *lay;
	Layer *trylay;
	
	// Search child layers first
	for(lay = VirtualTopChild(); lay; lay = VirtualLowerSibling())
	{
		if(lay->fViewToken == token)
			return lay;
	}
	
	// Hmmm... not in this layer's children. Try lower descendants
	for(lay = VirtualTopChild(); lay != NULL; lay = VirtualLowerSibling())
	{
		trylay = lay->FindLayer(token);
		if(trylay)
			return trylay;
	}
	
	// Well, we got this far in the function, so apparently there is no match to be found
	return NULL;
}

/*!
	\brief Returns the layer's ServerWindow
	
	If the layer's ServerWindow has not been assigned, it attempts to find 
	the owning ServerWindow in the tree.
*/
ServerWindow* Layer::SearchForServerWindow()
{
	if(!fServerWin)
		fServerWin=fParent->SearchForServerWindow();
	
	return fServerWin;
}

void Layer::RebuildAndForceRedraw(const BRegion& reg, Layer *target)
{
	STRACE(("Layer(%s)::RebuildAndForceRedraw():\n", GetName()));
	BPoint pt(0,0);

	StartRebuildRegions(reg, NULL, B_LAYER_NONE, pt);

	if (target)
		fRootLayer->fRedrawReg.Include(&(target->fFullVisible));

	Redraw(fRootLayer->fRedrawReg);

	EmptyGlobals();
}

void Layer::FullInvalidate(const BRect &rect)
{
	FullInvalidate( BRegion(rect) );
}

void Layer::FullInvalidate(const BRegion& region)
{
	STRACE(("Layer(%s)::FullInvalidate():\n", GetName()));
	
	#ifdef DEBUG_LAYER
	region.PrintToStream();
	printf("\n");
	#endif

	BPoint pt(0,0);
	StartRebuildRegions(region, NULL,/* B_LAYER_INVALIDATE, pt); */B_LAYER_NONE, pt);
	
	Redraw(fRootLayer->fRedrawReg);
	
	EmptyGlobals();
}

void Layer::Invalidate(const BRegion& region)
{
	STRACE(("Layer(%s)::Invalidate():\n", GetName()));
	#ifdef DEBUG_LAYER
	region.PrintToStream();
	printf("\n");
	#endif
	
	fRootLayer->fRedrawReg	= region;
	
	Redraw(fRootLayer->fRedrawReg);
	
	EmptyGlobals();
}

void Layer::Redraw(const BRegion& reg, Layer *startFrom)
{
	STRACE(("Layer(%s)::Redraw();\n", GetName()));
	if (IsHidden())
		// this layer has nothing visible on screen, so bail out.
		return;

	BRegion *pReg = const_cast<BRegion*>(&reg);

	if (pReg->CountRects() > 0)
		RequestDraw(reg, startFrom);
	
	STRACE(("Layer(%s)::Redraw() ENDED\n", GetName()));
}

void Layer::RequestDraw(const BRegion &reg, Layer *startFrom)
{
	STRACE(("Layer(%s)::RequestDraw()\n", GetName()));

	// do not redraw any child until you must
	int redraw = false;
	if (!startFrom)
		redraw = true;

	if (fVisible.CountRects() > 0)
	{
		// client side drawing. Send only one UPDATE message!
		if (HasClient())
		{
			if (IsTopLayer())
			{
				// calculate the minimum region/rectangle to be updated with
				// a single message to the client.
				fUpdateReg = fFullVisible;
				if (fFlags & B_FULL_UPDATE_ON_RESIZE
					&& fFrameAction	== B_LAYER_ACTION_RESIZE)
				{
					// do nothing
				}
				else
				{
					fUpdateReg.IntersectWith(&reg);
				}

				if (fUpdateReg.CountRects() > 0)
				{
					SendUpdateMsg();
				}
				
				// we're not that different than other. We too have an
				// update region to which our drawing is restrincted.
			}

			// calculate the update region, then...
			fUpdateReg = fVisible;
			if (fFlags & B_FULL_UPDATE_ON_RESIZE
				&& fFrameAction	== B_LAYER_ACTION_RESIZE)
			{
				// do nothing
			}
			else
			{
				fUpdateReg.IntersectWith(&reg);
			}

			if (fUpdateReg.CountRects() > 0)
			{
				// clear background with viewColor.
				fDriver->ConstrainClippingRegion(&fUpdateReg);
				fDriver->FillRect(fUpdateReg.Frame(), fLayerData->viewcolor);
				fDriver->ConstrainClippingRegion(NULL);
			}
		}
		else
		{
			// server drawings are immediate.
			// No IPC is needed so this is done in place.
			
			fUpdateReg = fVisible;
			if (fFlags & B_FULL_UPDATE_ON_RESIZE
				&& fFrameAction	== B_LAYER_ACTION_RESIZE)
			{
				// do nothing
			}
			else
			{
				fUpdateReg.IntersectWith(&reg);
			}

			if (fUpdateReg.CountRects() > 0)
			{
				fDriver->ConstrainClippingRegion(&fUpdateReg);
				Draw(fUpdateReg.Frame());
				fDriver->ConstrainClippingRegion(NULL);
				fUpdateReg.MakeEmpty();
			}
		}
	}

	for (Layer *lay = VirtualBottomChild(); lay != NULL; lay = VirtualUpperSibling())
	{
		if (lay == startFrom)
			redraw = true;

		if (redraw && !(lay->IsHidden()))
		{
			// no need to go deeper if not even the FullVisible region intersects
			// Update one.
			
			BRegion common(lay->fFullVisible);
			common.IntersectWith(&reg);
			
			if (common.CountRects() > 0)
			{
				// lock/unlock if we are a winborder
				if (lay->fClassID == AS_WINBORDER_CLASS)
					lay->Window()->Lock();
				
				lay->RequestDraw(reg, NULL);
				
				if (lay->fClassID == AS_WINBORDER_CLASS)
					lay->Window()->Unlock();
			}
		}
	}
}

void Layer::Draw(const BRect &r)
{
	// TODO/NOTE: this should be an empty method! the next lines are for testing only

	#ifdef DEBUG_LAYER
	printf("Layer::Draw: ");
	r.PrintToStream();
	#endif	
	
	fDriver->FillRect(r, fLayerData->viewcolor);
	
	// empty HOOK function.
}

void Layer::UpdateStart()
{
	// During updates we only want to draw what's in the update region
	fInUpdate = true;
	fClipReg = &fUpdateReg;
}

void Layer::UpdateEnd()
{
	// The usual case. Drawing is permitted in the whole visible area.
	fInUpdate = false;
	fClipReg = &fVisible;
	fUpdateReg.MakeEmpty();
}

/*!
	\brief Shows the layer
	\param invalidate Invalidate the region when showing the layer. defaults to true
*/
void Layer::Show(bool invalidate)
{
	STRACE(("Layer(%s)::Show()\n", GetName()));
	if( !IsHidden() )
		return;
	
	fHidden	= false;
	
	if(invalidate)
		GetRootLayer()->GoInvalidate(this, fFull);
}

/*!
	\brief Shows the layer
	\param invalidate Invalidate the region when hiding the layer. defaults to true
*/
void Layer::Hide(bool invalidate)
{
	STRACE(("Layer(%s)::Hide()\n", GetName()));
	if ( IsHidden() )
		return;
	
	fHidden	= true;
	
	if(invalidate)
		GetRootLayer()->GoInvalidate(this, fFullVisible);
}

//! Returns true if the layer is hidden
bool Layer::IsHidden(void) const
{
	if (fHidden)
		return true;
	else
	{
		if (fParent)
			return fParent->IsHidden();
	}
	
	return false;
}

//! Returns the number of children
uint32 Layer::CountChildren(void) const
{
	uint32 i = 0;
	Layer *lay = VirtualTopChild();
	while(lay != NULL)
	{
		lay	= VirtualLowerSibling();
		i++;
	}
	return i;
}

//! Rebuilds the layer's "completely visible" region
void Layer::RebuildFullRegion(void)
{
	STRACE(("Layer(%s)::RebuildFullRegion()\n", GetName()));
	
	if (fParent)
		fFull.Set( fParent->ConvertToTop( fFrame ) );
	else
		fFull.Set( fFrame );
	
	// TODO: restrict to screen coordinates
	
	// TODO: Convert to screen coordinates
	
	LayerData *ld;
	ld = fLayerData;
	do
	{
		// clip to user region
		if(ld->clipReg)
			fFull.IntersectWith( ld->clipReg );
		
	} while( (ld = ld->prevState) );
	
	// clip to user picture region
	if(clipToPicture)
	{
		if(clipToPictureInverse)
			fFull.Exclude( clipToPicture );
		else
			fFull.IntersectWith( clipToPicture );
	}
}

void Layer::RebuildRegions( const BRegion& reg, uint32 action, BPoint pt, BPoint ptOffset)
{
	STRACE(("Layer(%s)::RebuildRegions() START\n", GetName()));
	
	// TODO:/NOTE: this method must be executed as quickly as possible.
	
	// Currently SendView[Moved/Resized]Msg() simply constructs a message and calls
	// ServerWindow::SendMessageToClient(). This involves the alternative use of 
	// kernel and this code in the CPU, so there are a lot of context switches. 
	// This is NOT good at all!
	
	// One alternative would be the use of a BMessageQueue per ServerWindows OR only
	// one for app_server which will be emptied as soon as this critical operation ended.
	// Talk to DW, Gabe.
	
	BRegion	oldRegion;
	uint32 newAction = action;
	BPoint newPt = pt;
	BPoint newOffset = ptOffset; // used for resizing only
	
	BPoint dummyNewLocation;

	RRLabel1:
	switch(action)
	{
		case B_LAYER_NONE:
		{
			RBTRACE(("1) Action B_LAYER_NONE\n"));
			oldRegion = fVisible;
			break;
		}
		case B_LAYER_MOVE:
		{
			RBTRACE(("1) Action B_LAYER_MOVE\n"));
			oldRegion = fFullVisible;
			fFrame.OffsetBy(pt.x, pt.y);
			fFull.OffsetBy(pt.x, pt.y);
			
			// TODO: uncomment later when you'll implement a queue in ServerWindow::SendMessgeToClient()
			//SendViewMovedMsg();

			newAction	= B_LAYER_SIMPLE_MOVE;
			break;
		}
		case B_LAYER_SIMPLE_MOVE:
		{
			RBTRACE(("1) Action B_LAYER_SIMPLE_MOVE\n"));
			fFull.OffsetBy(pt.x, pt.y);
			
			break;
		}
		case B_LAYER_RESIZE:
		{
			RBTRACE(("1) Action B_LAYER_RESIZE\n"));
			oldRegion	= fVisible;
			
			fFrame.right	+= pt.x;
			fFrame.bottom	+= pt.y;
			RebuildFullRegion();
			
			// TODO: uncomment later when you'll implement a queue in ServerWindow::SendMessgeToClient()
			//SendViewResizedMsg();
			
			newAction = B_LAYER_MASK_RESIZE;
			break;
		}
		case B_LAYER_MASK_RESIZE:
		{
			RBTRACE(("1) Action B_LAYER_MASK_RESIZE\n"));
			oldRegion = fVisible;
			
			BPoint offset, rSize;
			BPoint coords[2];
			
			ResizeOthers(pt.x, pt.y, coords, NULL);
			offset = coords[0];
			rSize = coords[1];
			newOffset = offset + ptOffset;
			
			if(!(rSize.x == 0.0f && rSize.y == 0.0f))
			{
				fFrame.OffsetBy(offset);
				fFrame.right += rSize.x;
				fFrame.bottom += rSize.y;
				RebuildFullRegion();
				
				// TODO: uncomment later when you'll implement a queue in ServerWindow::SendMessgeToClient()
				//SendViewResizedMsg();
				
				newAction = B_LAYER_MASK_RESIZE;
				newPt = rSize;
				dummyNewLocation = newOffset;
			}
			else
			{
				if (!(offset.x == 0.0f && offset.y == 0.0f))
				{
					pt = newOffset;
					action = B_LAYER_MOVE;
					newPt = pt;
					goto RRLabel1;
				}
				else
				{
					pt = ptOffset;
					action = B_LAYER_MOVE;
					newPt = pt;
					goto RRLabel1;
				}
			}
			break;
		}
	}

	if (!IsHidden())
	{
		fFullVisible.MakeEmpty();
		fVisible = fFull;
		
		#ifdef DEBUG_LAYER_REBUILD
			printf("\n ======= Layer(%s):: RR ****** ======\n", GetName());
			fFull.PrintToStream();
			fVisible.PrintToStream();
			printf("\n ======= Layer(%s):: RR ****** END ======\n", GetName());
		
			if (!fParent)
				printf("\t NO parent\n");
			else
				printf("\t VALID Parent: %s.\n", fParent->GetName());
			if (!(fVisible.CountRects() > 0))
				printf("\t NO visible area!\n");
			else
				printf("\t VALID visble area\n");
		#endif
		
		if (fParent && fVisible.CountRects() > 0)
		{
			// not the usual case, but support fot this is needed.
			if (fParent->fAdFlags & B_LAYER_CHILDREN_DEPENDANT)
			{
				#ifdef DEBUG_LAYER_REBUILD
					printf("\n ======= Layer(%s)::B_LAYER_CHILDREN_DEPENDANT Parent ======\n", GetName());
					fFull.PrintToStream();
					fVisible.PrintToStream();
				#endif
				
				// because we're skipping one level, we need to do out
				// parent business as well.
				
				// our visible area is relative to our parent's parent.
				if (fParent->fParent)
					fVisible.IntersectWith(&(fParent->fParent->fVisible));
					
				#ifdef DEBUG_LAYER_REBUILD
					fVisible.PrintToStream();
				#endif
				// exclude parent's visible area which could be composed by
				// prior siblings' visible areas.
				if (fVisible.CountRects() > 0)
					fVisible.Exclude(&(fParent->fVisible));
				
				#ifdef DEBUG_LAYER_REBUILD
					fVisible.PrintToStream();
				#endif
				
				// we have a final visible area. Include it to our parent's one,
				// exclude from parent's parent.
				if (fVisible.CountRects() > 0)
				{
					fParent->fFullVisible.Include(&fVisible);
						
					#ifdef DEBUG_LAYER_REBUILD
						fParent->fFullVisible.PrintToStream();
					#endif
					
					if (fParent->fParent)
						fParent->fParent->fVisible.Exclude(&fVisible);
					
					#ifdef DEBUG_LAYER_REBUILD
						fParent->fParent->fVisible.PrintToStream();
					#endif
				}
				#ifdef DEBUG_LAYER_REBUILD
					printf("\n ======= Layer(%s)::B_LAYER_CHILDREN_DEPENDANT Parent END ======\n", GetName());
				#endif
			}
			else
			{
				// for 95+% of cases
				
				#ifdef DEBUG_LAYER_REBUILD
					printf("\n ======= Layer(%s):: (!)B_LAYER_CHILDREN_DEPENDANT Parent ======\n", GetName());
				#endif
				
				// the visible area is the one common with parent's one.
				fVisible.IntersectWith(&(fParent->fVisible));
				
				// exclude from parent's visible area. we're the owners now.
				if (fVisible.CountRects() > 0)
					fParent->fVisible.Exclude(&fVisible);
			}
		}
		fFullVisible = fVisible;
	}
	
	// Rebuild regions for children...
	for(Layer *lay = VirtualBottomChild(); lay != NULL; lay = VirtualUpperSibling())
		lay->RebuildRegions(reg, newAction, newPt, newOffset);
	
	if(!IsHidden())
	{
		switch(action)
		{
			case B_LAYER_NONE:
			{
				BRegion r(fVisible);
				if (oldRegion.CountRects() > 0)
					r.Exclude(&oldRegion);
				
				if(r.CountRects() > 0)
					fRootLayer->fRedrawReg.Include(&r);
				break;
			}
			case B_LAYER_MOVE:
			{
				BRegion redrawReg;
				BRegion	*copyReg = new BRegion();
				BRegion	screenReg(fRootLayer->Bounds());
				
				oldRegion.OffsetBy(pt.x, pt.y);
				oldRegion.IntersectWith(&fFullVisible);
				
				*copyReg = oldRegion;
				copyReg->IntersectWith(&screenReg);
				if(copyReg->CountRects() > 0 && !(pt.x == 0.0f && pt.y == 0.0f) )
				{
					copyReg->OffsetBy(-pt.x, -pt.y);
					BPoint		*point = new BPoint(pt);
					fRootLayer->fCopyRegList.AddItem(copyReg);
					fRootLayer->fCopyList.AddItem(point);
				}
				else
				{
					delete copyReg;
				}
				
				redrawReg	= fFullVisible;
				redrawReg.Exclude(&oldRegion);
				if(redrawReg.CountRects() > 0 && !(pt.x == 0.0f && pt.y == 0.0f) )
				{
					fRootLayer->fRedrawReg.Include(&redrawReg);
				}
	
				break;
			}
			case B_LAYER_RESIZE:
			{
				BRegion redrawReg;
				
				redrawReg = fVisible;
				redrawReg.Exclude(&oldRegion);
				if(redrawReg.CountRects() > 0)
					fRootLayer->fRedrawReg.Include(&redrawReg);
	
				break;
			}
			case B_LAYER_MASK_RESIZE:
			{
				BRegion redrawReg;
				BRegion	*copyReg = new BRegion();
				
				oldRegion.OffsetBy(dummyNewLocation.x, dummyNewLocation.y);
				
				redrawReg	= fVisible;
				redrawReg.Exclude(&oldRegion);
				if(redrawReg.CountRects() > 0)
				{
					fRootLayer->fRedrawReg.Include(&redrawReg);
				}
				
				*copyReg = fVisible;
				copyReg->IntersectWith(&oldRegion);
				copyReg->OffsetBy(-dummyNewLocation.x, -dummyNewLocation.y);
				if(copyReg->CountRects() > 0
					&& !(dummyNewLocation.x == 0.0f && dummyNewLocation.y == 0.0f))
				{
					fRootLayer->fCopyRegList.AddItem(copyReg);
					fRootLayer->fCopyList.AddItem(new BPoint(dummyNewLocation));
				}
				
				break;
			}
			default:
			{
				break;
			}
		}
	}
/*	if (IsHidden())
	{
		fFullVisible.MakeEmpty();
		fVisible.MakeEmpty();
	}
*/

	#ifdef DEBUG_LAYER_REBUILD
	printf("\n ======= Layer(%s)::RR finals ======\n", GetName());
	oldRegion.PrintToStream();
	fFull.PrintToStream();
	fFullVisible.PrintToStream();
	fVisible.PrintToStream();
	printf("==========RedrawReg===========\n");
	fRootLayer->fRedrawReg.PrintToStream();
	printf("=====================\n");
	#endif
	
	STRACE(("Layer(%s)::RebuildRegions() END\n", GetName()));
}

void Layer::StartRebuildRegions( const BRegion& reg, Layer *target, uint32 action, BPoint& pt)
{
	STRACE(("Layer(%s)::StartRebuildRegions() START\n", GetName()));
	if(!fParent)
		fFullVisible = fFull;
	
	BRegion oldVisible = fVisible;
	
	fVisible = fFullVisible;
	
	// Rebuild regions for children...
	for(Layer *lay = VirtualBottomChild(); lay != NULL; lay = VirtualUpperSibling())
	{
		if (lay == target)
			lay->RebuildRegions(reg, action, pt, BPoint(0.0f, 0.0f));
		else
			lay->RebuildRegions(reg, B_LAYER_NONE, pt, BPoint(0.0f, 0.0f));
	}
	
	#ifdef DEBUG_LAYER_REBUILD
	printf("\n ===!=== Layer(%s)::SRR finals ===!===\n", GetName());
	fFull.PrintToStream();
	fFullVisible.PrintToStream();
	fVisible.PrintToStream();
	oldVisible.PrintToStream();
	printf("=====!=====RedrawReg=====!=====\n");
	fRootLayer->fRedrawReg.PrintToStream();
	printf("=====================\n");
	#endif
	
	BRegion redrawReg(fVisible);
	
	// if this is the first time
	if (oldVisible.CountRects() > 0)
		redrawReg.Exclude(&oldVisible);

	if (redrawReg.CountRects() > 0)
		fRootLayer->fRedrawReg.Include(&redrawReg);
	
	#ifdef DEBUG_LAYER_REBUILD
	printf("Layer(%s)::StartRebuildREgions() ended! Redraw Region:\n", GetName());
	fRootLayer->fRedrawReg.PrintToStream();
	printf("\n");
	printf("Layer(%s)::StartRebuildREgions() ended! Copy Region:\n", GetName());
	for(int32 k=0; k<fRootLayer->fCopyRegList.CountItems(); k++)
	{
		((BRegion*)(fRootLayer->fCopyRegList.ItemAt(k)))->PrintToStream();
		((BPoint*)(fRootLayer->fCopyList.ItemAt(k)))->PrintToStream();
	}
	printf("\n");
	#endif

	STRACE(("Layer(%s)::StartRebuildRegions() END\n", GetName()));
}

//! Moves the layer by specified values, complete with redraw
void Layer::MoveBy(float x, float y)
{
	STRACE(("Layer(%s)::MoveBy() START\n", GetName()));
	if(!fParent)
	{
		debugger("ERROR: in Layer::MoveBy()! - No parent!\n");
		return;
	}

	BPortLink	msg(-1, -1);
	msg.StartMessage(AS_ROOTLAYER_LAYER_MOVE);
	msg.Attach<Layer*>(this);
	msg.Attach<float>(x);
	msg.Attach<float>(y);
	GetRootLayer()->EnqueueMessage(msg);

	STRACE(("Layer(%s)::MoveBy() END\n", GetName()));
}

void Layer::move_layer(float x, float y)
{
	fFrameAction	= B_LAYER_ACTION_MOVE;

	BPoint pt(x,y);	
	BRect rect(fFull.Frame().OffsetByCopy(pt));
	
	fParent->StartRebuildRegions(BRegion(rect), this, B_LAYER_MOVE, pt);
	fDriver->CopyRegionList(&fRootLayer->fCopyRegList, &fRootLayer->fCopyList, fRootLayer->fCopyRegList.CountItems(), &fFullVisible);
	fParent->Redraw(fRootLayer->fRedrawReg, this);
	
	EmptyGlobals();

	fFrameAction	= B_LAYER_ACTION_NONE;	
}

void Layer::EmptyGlobals()
{
	void *item;
	
	fRootLayer->fRedrawReg.MakeEmpty();
	
	while((item = fRootLayer->fCopyRegList.RemoveItem((int32)0)))
		delete (BRegion*)item;
	
	while((item = fRootLayer->fCopyList.RemoveItem((int32)0)))
		delete (BPoint*)item;
}

uint32 Layer::ResizeOthers(float x, float y, BPoint coords[], BPoint *ptOffset)
{
	STRACE(("Layer(%s)::ResizeOthers() START\n", GetName()));
	uint32 rmask = fResizeMode;
	
	// offset
	coords[0].x	= 0.0f;
	coords[0].y	= 0.0f;
	
	// resize by width/height
	coords[1].x	= 0.0f;
	coords[1].y	= 0.0f;

	if ((rmask & 0x00000f00UL)>>8 == _VIEW_LEFT_
			&& (rmask & 0x0000000fUL)>>0 == _VIEW_RIGHT_)
	{
		coords[1].x		= x;
	}
	else
	if ((rmask & 0x00000f00UL)>>8 == _VIEW_LEFT_)
	{
	}
	else
	if ((rmask & 0x0000000fUL)>>0 == _VIEW_RIGHT_)
	{
		coords[0].x		= x;
	}
	else
	if ((rmask & 0x00000f00UL)>>8 == _VIEW_CENTER_)
	{
		coords[0].x		= x/2;
	}
	else
	{
		// illegal flag. Do nothing.
	}


	if ((rmask & 0x0000f000UL)>>12 == _VIEW_TOP_
			&& (rmask & 0x000000f0UL)>>4 == _VIEW_BOTTOM_)
	{
		coords[1].y		= y;
	}
	else
	if ((rmask & 0x0000f000UL)>>12 == _VIEW_TOP_)
	{
	}
	else
	if ((rmask & 0x000000f0UL)>>4 == _VIEW_BOTTOM_)
	{
		coords[0].y		= y;
	}
	else
	if ((rmask & 0x0000f000UL)>>12 == _VIEW_CENTER_)
	{
		coords[0].y		= y/2;
	}
	else
	{
		// illegal flag. Do nothing.
	}

	STRACE(("Layer(%s)::ResizeOthers() END\n", GetName()));
	return 0UL;
}

//! Resize the layer by the specified amount, complete with redraw
void Layer::ResizeBy(float x, float y)
{
	STRACE(("Layer(%s)::ResizeBy() START\n", GetName()));
	
	if(!fParent)
	{
		printf("ERROR: in Layer::MoveBy()! - No parent!\n");
		return;
	}

	BPortLink	msg(-1, -1);
	msg.StartMessage(AS_ROOTLAYER_LAYER_RESIZE);
	msg.Attach<Layer*>(this);
	msg.Attach<float>(x);
	msg.Attach<float>(y);
	GetRootLayer()->EnqueueMessage(msg);

	STRACE(("Layer(%s)::ResizeBy() END\n", GetName()));
}

void Layer::resize_layer(float x, float y)
{
	fFrameAction	= B_LAYER_ACTION_RESIZE;

	BPoint pt(x,y);	
	BRect rect(fFull.Frame());
	rect.right += x;
	rect.bottom += y;
	
	fParent->StartRebuildRegions(BRegion(rect), this, B_LAYER_RESIZE, pt);
	
	fDriver->CopyRegionList(&fRootLayer->fCopyRegList, &fRootLayer->fCopyList, fRootLayer->fCopyRegList.CountItems(), &fFullVisible);
	fParent->Redraw(fRootLayer->fRedrawReg, this);
	
	EmptyGlobals();

	fFrameAction	= B_LAYER_ACTION_NONE;
}

//! Prints information about the layer's current state
void Layer::PrintToStream(void)
{
	printf("\n----------- Layer %s -----------\n",fName->String());
	printf("\t Parent: %s\n", fParent? fParent->GetName():"NULL");
	printf("\t us: %s\t ls: %s\n",
				fUpperSibling? fUpperSibling->GetName():"NULL",
				fLowerSibling? fLowerSibling->GetName():"NULL");
	printf("\t topChild: %s\t bottomChild: %s\n",
				fTopChild? fTopChild->GetName():"NULL",
				fBottomChild? fBottomChild->GetName():"NULL");
	
	printf("Frame: (%f, %f, %f, %f)", fFrame.left, fFrame.top, fFrame.right, fFrame.bottom);
	printf("Token: %ld\n",fViewToken);
	printf("Hidden - direct: %s\n", fHidden?"true":"false");
	printf("Hidden - indirect: %s\n", IsHidden()?"true":"false");
	printf("ResizingMode: %lx\n", fResizeMode);
	printf("Flags: %lx\n", fFlags);
	
	if (fLayerData)
		fLayerData->PrintToStream();
	else
		printf(" NO LayerData valid pointer\n");
}

//! Prints pointer info kept by the current layer
void Layer::PrintNode(void)
{
	printf("-----------\nLayer %s\n",fName->String());
	if(fParent)
		printf("Parent: %s (%p)\n",fParent->GetName(), fParent);
	else
		printf("Parent: NULL\n");
	if(fUpperSibling)
		printf("Upper sibling: %s (%p)\n",fUpperSibling->GetName(), fUpperSibling);
	else
		printf("Upper sibling: NULL\n");
	if(fLowerSibling)
		printf("Lower sibling: %s (%p)\n",fLowerSibling->GetName(), fLowerSibling);
	else
		printf("Lower sibling: NULL\n");
	if(fTopChild)
		printf("Top child: %s (%p)\n",fTopChild->GetName(), fTopChild);
	else
		printf("Top child: NULL\n");
	if(fBottomChild)
		printf("Bottom child: %s (%p)\n",fBottomChild->GetName(), fBottomChild);
	else
		printf("Bottom child: NULL\n");
	printf("Visible Areas: "); fVisible.PrintToStream();
}

//! Prints the tree hierarchy from the current layer down
void Layer::PrintTree()
{
	printf("\n Tree structure:\n");
	printf("\t%s\t%s\n", GetName(), IsHidden()? "Hidden": "NOT hidden");
	for(Layer *lay = VirtualBottomChild(); lay != NULL; lay = VirtualUpperSibling())
		printf("\t%s\t%s\n", lay->GetName(), lay->IsHidden()? "Hidden": "NOT hidden");
}

//! Converts the passed rectangle to parent coordinates
BRect Layer::ConvertToParent(BRect rect)
{
	return (rect.OffsetByCopy(fFrame.LeftTop()));
}

//! Converts the passed region to parent coordinates
BRegion Layer::ConvertToParent(BRegion *reg)
{
	BRegion newreg;
	
	for(int32 i=0; i<reg->CountRects(); i++)
		newreg.Include( (reg->RectAt(i)).OffsetByCopy(fFrame.LeftTop()) );
	
	return newreg;
}

//! Converts the passed rectangle from parent coordinates
BRect Layer::ConvertFromParent(BRect rect)
{
	return (rect.OffsetByCopy(fFrame.left*-1,fFrame.top*-1));
}

//! Converts the passed region from parent coordinates
BRegion Layer::ConvertFromParent(BRegion *reg)
{
	BRegion newreg;
	
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include((reg->RectAt(i)).OffsetByCopy(fFrame.left*-1,fFrame.top*-1));
	
	return newreg;
}

BPoint Layer::ConvertToTop(BPoint pt)
{
	if (fParent!=NULL)
	{
		return(fParent->ConvertToTop(pt+fFrame.LeftTop()));
	}
	else
		return(pt);
}

//! Converts the passed region to screen coordinates
BRegion Layer::ConvertToTop(BRegion *reg)
{
	BRegion newreg;
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include(ConvertToTop(reg->RectAt(i)));
	return newreg;
}

//! Converts the passed rectangle to screen coordinates
BRect Layer::ConvertToTop(BRect rect)
{
	if (fParent!=NULL)
		return(fParent->ConvertToTop(rect.OffsetByCopy(fFrame.LeftTop())) );
	else
		return(rect);
}

BPoint Layer::ConvertFromTop(BPoint pt)
{
	if (fParent!=NULL)
	{
		return(fParent->ConvertFromTop(pt-fFrame.LeftTop()));
	}
	else
		return(pt);
}

//! Converts the passed region from screen coordinates
BRegion Layer::ConvertFromTop(BRegion *reg)
{
	BRegion newreg;
	
	for(int32 i=0; i<reg->CountRects();i++)
		newreg.Include(ConvertFromTop(reg->RectAt(i)));
	
	return newreg;
}

//! Converts the passed rectangle from screen coordinates
BRect Layer::ConvertFromTop(BRect rect)
{
	if (fParent!=NULL)
		return(fParent->ConvertFromTop(rect.OffsetByCopy(fFrame.LeftTop().x*-1,
			fFrame.LeftTop().y*-1)) );
	else
		return(rect);
}

//! Sends a B_VIEW_RESIZE message to the client BWindow
void Layer::SendViewResizedMsg()
{
	if( fServerWin && fFlags & B_FRAME_EVENTS )
	{
		BMessage msg;
		msg.what = B_VIEW_RESIZED;
		msg.AddInt64( "when", real_time_clock_usecs() );
		msg.AddInt32( "_token", fViewToken );
		msg.AddFloat( "width", fFrame.Width() );
		msg.AddFloat( "height", fFrame.Height() );
		
		// no need for that... it's here because of backward compatibility
		msg.AddPoint( "where", fFrame.LeftTop() );
		
		fServerWin->SendMessageToClient( &msg );
	}
}

//! Sends a B_VIEW_MOVED message to the client BWindow
void Layer::SendViewMovedMsg()
{
	if( fServerWin && fFlags & B_FRAME_EVENTS )
	{
		BMessage msg;
		msg.what = B_VIEW_MOVED;
		msg.AddInt64( "when", real_time_clock_usecs() );
		msg.AddInt32( "_token", fViewToken );
		msg.AddPoint( "where", fFrame.LeftTop() );
		
		fServerWin->SendMessageToClient( &msg );
	}
}

//! Sends an _UPDATE_ message to the client BWindow
void Layer::SendUpdateMsg()
{
	if( fServerWin )
	{
		BMessage msg;
		msg.what = _UPDATE_;
		msg.AddRect("_rect", ConvertFromTop(fUpdateReg.Frame()) );
		msg.AddRect("debug_rect", fUpdateReg.Frame() );
		msg.AddInt32("_token",fViewToken);
		
		fServerWin->SendMessageToClient( &msg );
	}
}

Layer *Layer::VirtualTopChild() const
{
	fCurrent = fTopChild;
	return fCurrent;
}

Layer *Layer::VirtualLowerSibling() const
{
	fCurrent = fCurrent->fLowerSibling;
	return fCurrent;
}

Layer *Layer::VirtualUpperSibling() const
{
	fCurrent = fCurrent->fUpperSibling;
	return fCurrent;
}

Layer* Layer::VirtualBottomChild() const
{
	fCurrent = fBottomChild;
	return fCurrent;
}
