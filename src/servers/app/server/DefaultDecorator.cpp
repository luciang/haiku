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
//	File Name:		DefaultDecorator.cpp
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	Fallback decorator for the app_server
//  
//------------------------------------------------------------------------------
#include <Rect.h>
#include "DisplayDriver.h"
#include <View.h>
#include "LayerData.h"
#include "ColorUtils.h"
#include "DefaultDecorator.h"
#include "PatternHandler.h"
#include "RGBColor.h"
#include "RectUtils.h"
#include <stdio.h>

#define USE_VIEW_FILL_HACK

#ifdef DEBUG_DECORATOR
#include <stdio.h>
#endif

DefaultDecorator::DefaultDecorator(BRect rect, int32 wlook, int32 wfeel, int32 wflags)
 : Decorator(rect,wlook,wfeel,wflags)
{

	taboffset=0;
	titlepixelwidth=0;

	framecolors=new RGBColor[5];
	framecolors[0].SetColor(255,255,255);
	framecolors[1].SetColor(216,216,216);
	framecolors[2].SetColor(152,152,152);
	framecolors[3].SetColor(136,136,136);
	framecolors[4].SetColor(96,96,96);

	_SetFocus();

	_DoLayout();
	
	// This flag is used to determine whether or not we're moving the tab
	slidetab=false;
	solidhigh=0xFFFFFFFFFFFFFFFFLL;
	solidlow=0;

//	tab_highcol=_colors->window_tab;
//	tab_lowcol=_colors->window_tab;

#ifdef DEBUG_DECORATOR
printf("DefaultDecorator:\n");
printf("\tFrame (%.1f,%.1f,%.1f,%.1f)\n",rect.left,rect.top,rect.right,rect.bottom);
#endif
}

DefaultDecorator::~DefaultDecorator(void)
{
#ifdef DEBUG_DECORATOR
printf("DefaultDecorator: ~DefaultDecorator()\n");
#endif
	delete [] framecolors;
}

click_type DefaultDecorator::Clicked(BPoint pt, int32 buttons, int32 modifiers)
{
#ifdef DEBUG_DECORATOR
printf("DefaultDecorator: Clicked\n");
printf("\tPoint: (%.1f,%.1f)\n",pt.x,pt.y);
printf("\tButtons:\n");
if(buttons==0)
	printf("\t\tNone\n");
else
{
	if(buttons & B_PRIMARY_MOUSE_BUTTON)
		printf("\t\tPrimary\n");
	if(buttons & B_SECONDARY_MOUSE_BUTTON)
		printf("\t\tSecondary\n");
	if(buttons & B_TERTIARY_MOUSE_BUTTON)
		printf("\t\tTertiary\n");
}
printf("\tModifiers:\n");
if(modifiers==0)
	printf("\t\tNone\n");
else
{
	if(modifiers & B_CAPS_LOCK)
		printf("\t\tCaps Lock\n");
	if(modifiers & B_NUM_LOCK)
		printf("\t\tNum Lock\n");
	if(modifiers & B_SCROLL_LOCK)
		printf("\t\tScroll Lock\n");
	if(modifiers & B_LEFT_COMMAND_KEY)
		printf("\t\t Left Command\n");
	if(modifiers & B_RIGHT_COMMAND_KEY)
		printf("\t\t Right Command\n");
	if(modifiers & B_LEFT_CONTROL_KEY)
		printf("\t\tLeft Control\n");
	if(modifiers & B_RIGHT_CONTROL_KEY)
		printf("\t\tRight Control\n");
	if(modifiers & B_LEFT_OPTION_KEY)
		printf("\t\tLeft Option\n");
	if(modifiers & B_RIGHT_OPTION_KEY)
		printf("\t\tRight Option\n");
	if(modifiers & B_LEFT_SHIFT_KEY)
		printf("\t\tLeft Shift\n");
	if(modifiers & B_RIGHT_SHIFT_KEY)
		printf("\t\tRight Shift\n");
	if(modifiers & B_MENU_KEY)
		printf("\t\tMenu\n");
}
#endif
	if(_closerect.Contains(pt))
		return CLICK_CLOSE;

	if(_zoomrect.Contains(pt))
		return CLICK_ZOOM;
	
	if(_resizerect.Contains(pt) && _look==B_DOCUMENT_WINDOW_LOOK)
		return CLICK_RESIZE;

	// Clicking in the tab?
	if(_tabrect.Contains(pt))
	{
		// Here's part of our window management stuff
//		if(buttons==B_PRIMARY_MOUSE_BUTTON && !GetFocus())
//			return CLICK_MOVETOFRONT;
		if(buttons==B_SECONDARY_MOUSE_BUTTON)
			return CLICK_MOVETOBACK;
		return CLICK_DRAG;
	}

	// We got this far, so user is clicking on the border?
	BRect brect(_frame);
	brect.top+=19;
	BRect clientrect(brect.InsetByCopy(3,3));
	if(brect.Contains(pt) && !clientrect.Contains(pt))
	{
		if(_resizerect.Contains(pt))
			return CLICK_RESIZE;
		
		return CLICK_DRAG;
	}

	// Guess user didn't click anything
	return CLICK_NONE;
}

void DefaultDecorator::_DoLayout(void)
{
//debugger("");
#ifdef DEBUG_DECORATOR
printf("DefaultDecorator: Do Layout\n");
#endif
	// Here we determine the size of every rectangle that we use
	// internally when we are given the size of the client rectangle.
	
	// Current version simply makes everything fit inside the rect
	// instead of building around it. This will change.
	
	_tabrect=_frame;
	_resizerect=_frame;
	_borderrect=_frame;
	_closerect=_frame;

	
	switch(GetLook())
	{
		case B_FLOATING_WINDOW_LOOK:
		case B_MODAL_WINDOW_LOOK:
		
		// We're going to make the frame 5 pixels wide, no matter what. R5's decorator frame
		// requires the skills of a gaming master to click on the tiny frame if resizing is necessary,
		// and there *are* apps which do this
//			borderwidth=3;
//			break;
		case B_BORDERED_WINDOW_LOOK:
		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
			borderwidth=5;
			break;
		default:
			borderwidth=0;
	}

	textoffset=(_look==B_FLOATING_WINDOW_LOOK)?5:7;

	_closerect.left+=(_look==B_FLOATING_WINDOW_LOOK)?2:4;
	_closerect.top+=(_look==B_FLOATING_WINDOW_LOOK)?6:4;
	_closerect.right=_closerect.left+10;
	_closerect.bottom=_closerect.top+10;
	
	
	_borderrect.top+=19;

	if(borderwidth)
	{
		// Set up the border rectangles to handle the window's frame
		rightborder=leftborder=topborder=bottomborder=_borderrect;
		
		// We want the rectangles to intersect because of the beveled intersections, so all
		// that is necessary is to set the short dimension of each side
		leftborder.right=leftborder.left+borderwidth;
		rightborder.left=rightborder.right-borderwidth;
		topborder.bottom=topborder.top+borderwidth;
		bottomborder.top=bottomborder.bottom-borderwidth;
	}
	
	_resizerect.top=_resizerect.bottom-18;
	_resizerect.left=_resizerect.right-18;
	
	_tabrect.bottom=_tabrect.top+18;
	if(strlen(GetTitle())>1)
	{
		if(_driver)
			titlepixelwidth=_driver->StringWidth(GetTitle(),_TitleWidth(), &_layerdata);
		else
			titlepixelwidth=10;
		
		if(_closerect.right+textoffset+titlepixelwidth+35< _frame.Width()-1)
			_tabrect.right=_tabrect.left+titlepixelwidth;
	}
	else
		_tabrect.right=_tabrect.left+_tabrect.Width()/2;

	if(_look==B_FLOATING_WINDOW_LOOK)
		_tabrect.top+=4;

	_zoomrect=_tabrect;
	_zoomrect.top+=(_look==B_FLOATING_WINDOW_LOOK)?2:4;
	_zoomrect.right-=4;
	_zoomrect.bottom-=4;
	_zoomrect.left=_zoomrect.right-10;
	_zoomrect.bottom=_zoomrect.top+10;
	
}

void DefaultDecorator::MoveBy(float x, float y)
{
	MoveBy(BPoint(x,y));
}

void DefaultDecorator::MoveBy(BPoint pt)
{
#ifdef DEBUG_DECORATOR
printf("DefaultDecorator: Move By (%.1f, %.1f)\n",pt.x,pt.y);
#endif
	// Move all internal rectangles the appropriate amount
	_frame.OffsetBy(pt);
	_closerect.OffsetBy(pt);
	_tabrect.OffsetBy(pt);
	_resizerect.OffsetBy(pt);
	_borderrect.OffsetBy(pt);
	_zoomrect.OffsetBy(pt);
}

BRegion * DefaultDecorator::GetFootprint(void)
{
#ifdef DEBUG_DECORATOR
printf("DefaultDecorator: Get Footprint\n");
#endif
	// This function calculates the decorator's footprint in coordinates
	// relative to the layer. This is most often used to set a WinBorder
	// object's visible region.
	
	BRegion *reg=new BRegion(_borderrect);
	reg->Include(_tabrect);
	return reg;
}

void DefaultDecorator::_DrawTitle(BRect r)
{
	// Designed simply to redraw the title when it has changed on
	// the client side.
	_layerdata.highcolor=_colors->window_tab_text;
	_layerdata.lowcolor=(GetFocus())?_colors->window_tab:_colors->inactive_window_tab;

	int32 titlecount=_ClipTitle((_zoomrect.left-5)-(_closerect.right+textoffset));
	BString titlestr=GetTitle();
	if(titlecount<titlestr.CountChars())
	{
		titlestr.Truncate(titlecount-1);
		titlestr+="...";
		titlecount+=2;
	}
	_driver->DrawString(titlestr.String(),titlecount,
		BPoint(_closerect.right+textoffset,_closerect.bottom+1),&_layerdata);
}

void DefaultDecorator::_SetFocus(void)
{
	// SetFocus() performs necessary duties for color swapping and
	// other things when a window is deactivated or activated.
	
	if(GetFocus())
	{
		button_highcol.SetColor(tint_color(_colors->window_tab.GetColor32(),B_LIGHTEN_2_TINT));
		button_lowcol.SetColor(tint_color(_colors->window_tab.GetColor32(),B_DARKEN_2_TINT));
		textcol=_colors->window_tab_text;
	}
	else
	{
		button_highcol.SetColor(tint_color(_colors->inactive_window_tab.GetColor32(),B_LIGHTEN_2_TINT));
		button_lowcol.SetColor(tint_color(_colors->inactive_window_tab.GetColor32(),B_DARKEN_2_TINT));
		textcol=_colors->inactive_window_tab_text;
	}
}

void DefaultDecorator::Draw(BRect update)
{
#ifdef DEBUG_DECORATOR
printf("DefaultDecorator: Draw(%.1f,%.1f,%.1f,%.1f)\n",update.left,update.top,update.right,update.bottom);
#endif
	// We need to draw a few things: the tab, the resize thumb, the borders,
	// and the buttons

	_DrawTab(update);

	// Draw the top view's client area - just a hack :)
	_layerdata.highcolor=_colors->document_background;

	if(_borderrect.Intersects(update))
		_driver->FillRect(_borderrect & update,&_layerdata,(int8*)&solidhigh);
	
	_DrawFrame(update);

}

void DefaultDecorator::Draw(void)
{
	// Easy way to draw everything - no worries about drawing only certain
	// things

	// Draw the top view's client area - just a hack :)
//	_layerdata.highcolor=_colors->document_background;

//	_driver->FillRect(_borderrect,&_layerdata,(int8*)&solidhigh);

	DrawFrame();

	DrawTab();
}

void DefaultDecorator::_DrawZoom(BRect r)
{
	// If this has been implemented, then the decorator has a Zoom button
	// which should be drawn based on the state of the member zoomstate
	BRect zr=r;
	zr.left+=zr.Width()/3;
	zr.top+=zr.Height()/3;

	DrawBlendedRect(zr,GetZoom());
	DrawBlendedRect(zr.OffsetToCopy(r.LeftTop()),GetZoom());
}

void DefaultDecorator::_DrawClose(BRect r)
{
	// Just like DrawZoom, but for a close button
	DrawBlendedRect(r,GetClose());
}

void DefaultDecorator::_DrawTab(BRect r)
{
	// If a window has a tab, this will draw it and any buttons which are
	// in it.
	if(_look==B_NO_BORDER_WINDOW_LOOK)
		return;
	
	_layerdata.highcolor=(GetFocus())?_colors->window_tab:_colors->inactive_window_tab;
	_driver->FillRect(_tabrect,&_layerdata,(int8*)&solidhigh);
	_layerdata.highcolor=framecolors[3];
	_driver->StrokeLine(_tabrect.LeftBottom(),_tabrect.RightBottom(),&_layerdata,(int8*)&solidhigh);

	_DrawTitle(_tabrect);

	// Draw the buttons if we're supposed to	
	if(!(_flags & B_NOT_CLOSABLE))
		_DrawClose(_closerect);
	if(!(_flags & B_NOT_ZOOMABLE))
		_DrawZoom(_zoomrect);
}

void DefaultDecorator::_SetColors(void)
{
	_SetFocus();
}

void DefaultDecorator::DrawBlendedRect(BRect r, bool down)
{
	// This bad boy is used to draw a rectangle with a gradient.
	// Note that it is not part of the Decorator API - it's specific
	// to just the DefaultDecorator. Called by DrawZoom and DrawClose

	// Actually just draws a blended square
	int32 w=r.IntegerWidth(),  h=r.IntegerHeight();

	rgb_color tmpcol,halfcol, startcol, endcol;
	float rstep,gstep,bstep,i;

	int steps=(w<h)?w:h;

	if(down)
	{
		startcol=button_lowcol.GetColor32();
		endcol=button_highcol.GetColor32();
	}
	else
	{
		startcol=button_highcol.GetColor32();
		endcol=button_lowcol.GetColor32();
	}

	halfcol=MakeBlendColor(startcol,endcol,0.5);

	rstep=float(startcol.red-halfcol.red)/steps;
	gstep=float(startcol.green-halfcol.green)/steps;
	bstep=float(startcol.blue-halfcol.blue)/steps;

	for(i=0;i<=steps; i++)
	{
		SetRGBColor(&tmpcol, uint8(startcol.red-(i*rstep)),
			uint8(startcol.green-(i*gstep)),
			uint8(startcol.blue-(i*bstep)));
		_layerdata.highcolor=tmpcol;
		_driver->StrokeLine(BPoint(r.left,r.top+i),
			BPoint(r.left+i,r.top),&_layerdata,(int8*)&solidhigh);

		SetRGBColor(&tmpcol, uint8(halfcol.red-(i*rstep)),
			uint8(halfcol.green-(i*gstep)),
			uint8(halfcol.blue-(i*bstep)));

		_layerdata.highcolor=tmpcol;
		_driver->StrokeLine(BPoint(r.left+steps,r.top+i),
			BPoint(r.left+i,r.top+steps),&_layerdata,(int8*)&solidhigh);

	}

//	_layerdata.highcolor=startcol;
//	_driver->FillRect(r,&_layerdata,(int8*)&solidhigh);
	_layerdata.highcolor=framecolors[3];
	_driver->StrokeRect(r,&_layerdata,(int8*)&solidhigh);
}

void DefaultDecorator::_DrawFrame(BRect invalid)
{

	// We need to test each side to determine whether or not it needs drawn. Additionally,
	// we must clip the lines drawn by this function to the invalid rectangle we are given
	
	#ifdef USE_VIEW_FILL_HACK
	_driver->FillRect(_borderrect,&_layerdata,(int8*)&solidhigh);
	#endif

	if(!borderwidth)
		return;
	
	// Data specifically for the StrokeLineArray call.
	int32 numlines=0, maxlines=20;

	BPoint points[maxlines*2];
	RGBColor colors[maxlines];
	
	// For quick calculation of gradients for each side. Top is same as left, right is same as
	// bottom
//	int8 rightindices[borderwidth],leftindices[borderwidth];
	int8 *rightindices=new int8[borderwidth],
		*leftindices=new int8[borderwidth];
	
	if(borderwidth==5)
	{
		leftindices[0]=2;
		leftindices[1]=0;
		leftindices[2]=1;
		leftindices[3]=3;
		leftindices[4]=2;

		rightindices[0]=2;
		rightindices[1]=0;
		rightindices[2]=1;
		rightindices[3]=3;
		rightindices[4]=4;
	}
	else
	{
		// TODO: figure out border colors for floating window look
		leftindices[0]=2;
		leftindices[1]=2;
		leftindices[2]=1;
		leftindices[3]=1;
		leftindices[4]=4;

		rightindices[0]=2;
		rightindices[1]=2;
		rightindices[2]=1;
		rightindices[3]=1;
		rightindices[4]=4;
	}
	
	// Variables used in each calculation
	int32 startx,endx,starty,endy,i;
	bool topcorner,bottomcorner,leftcorner,rightcorner;
	int8 step,colorindex;
	BRect r;
	BPoint start, end;
	
	// Right side
	if(TestRectIntersection(rightborder,invalid))
	{
		
		// We may not have to redraw the entire width of the frame itself. Rare case, but
		// it must be accounted for.
		startx=(int32) MAX(invalid.left,rightborder.left);
		endx=(int32) MIN(invalid.right,rightborder.right);

		// We'll need these flags to see if we must include the corners in final line
		// calculations
		r=(rightborder);
		r.bottom=r.top+borderwidth;
		topcorner=TestRectIntersection(invalid,r);

		r=rightborder;
		r.top=r.bottom-borderwidth;
		bottomcorner=TestRectIntersection(invalid,r);
		step=(borderwidth==5)?1:2;
		colorindex=0;
		
		// Generate the lines for this side
		for(i=startx+1; i<=endx; i++)
		{
			start.x=end.x=i;
			
			if(topcorner)
			{
				start.y=rightborder.top+(borderwidth-(i-rightborder.left));
				start.y=MAX(start.y,invalid.top);
			}
			else
				start.y=MAX(start.y+borderwidth,invalid.top);

			if(bottomcorner)
			{
				end.y=rightborder.bottom-(borderwidth-(i-rightborder.left));
				end.y=MIN(end.y,invalid.bottom);
			}
			else
				end.y=MIN(end.y-borderwidth,invalid.bottom);
							
			// Make the appropriate 
			points[numlines*2]=start;
			points[(numlines*2)+1]=end;
			colors[numlines]=framecolors[rightindices[colorindex]];
			colorindex+=step;
			numlines++;
		}
	}

	// Left side
	if(TestRectIntersection(leftborder,invalid))
	{
		
		// We may not have to redraw the entire width of the frame itself. Rare case, but
		// it must be accounted for.
		startx=(int32) MAX(invalid.left,leftborder.left);
		endx=(int32) MIN(invalid.right,leftborder.right);

		// We'll need these flags to see if we must include the corners in final line
		// calculations
		r=leftborder;
		r.bottom=r.top+borderwidth;
		topcorner=TestRectIntersection(invalid,r);

		r=leftborder;
		r.top=r.bottom-borderwidth;
		bottomcorner=TestRectIntersection(invalid,r);
		step=(borderwidth==5)?1:2;
		colorindex=0;
		
		// Generate the lines for this side
		for(i=startx; i<endx; i++)
		{
			start.x=end.x=i;
			
			if(topcorner)
			{
				start.y=leftborder.top+(i-leftborder.left);
				start.y=MAX(start.y,invalid.top);
			}
			else
				start.y=MAX(start.y+borderwidth,invalid.top);

			if(bottomcorner)
			{
				end.y=leftborder.bottom-(i-leftborder.left);
				end.y=MIN(end.y,invalid.bottom);
			}
			else
				end.y=MIN(end.y-borderwidth,invalid.bottom);
							
			// Make the appropriate 
			points[numlines*2]=start;
			points[(numlines*2)+1]=end;
			colors[numlines]=framecolors[leftindices[colorindex]];
			colorindex+=step;
			numlines++;
		}
	}

	// Top side
	if(TestRectIntersection(topborder,invalid))
	{
		
		// We may not have to redraw the entire width of the frame itself. Rare case, but
		// it must be accounted for.
		starty=(int32) MAX(invalid.top,topborder.top);
		endy=(int32) MIN(invalid.bottom,topborder.bottom);

		// We'll need these flags to see if we must include the corners in final line
		// calculations
		r=topborder;
		r.bottom=r.top+borderwidth;
		r.right=r.left+borderwidth;
		leftcorner=TestRectIntersection(invalid,r);

		r=topborder;
		r.top=r.bottom-borderwidth;
		r.left=r.right-borderwidth;
		
		rightcorner=TestRectIntersection(invalid,r);
		step=(borderwidth==5)?1:2;
		colorindex=0;
		
		// Generate the lines for this side
		for(i=starty; i<endy; i++)
		{
			start.y=end.y=i;
			
			if(leftcorner)
			{
				start.x=topborder.left+(i-topborder.top);
				start.x=MAX(start.x,invalid.left);
			}
			else
				start.x=MAX(start.x+borderwidth,invalid.left);

			if(rightcorner)
			{
				end.x=topborder.right-(i-topborder.top);
				end.x=MIN(end.x,invalid.right);
			}
			else
				end.x=MIN(end.x-borderwidth,invalid.right);
							
			// Make the appropriate 
			points[numlines*2]=start;
			points[(numlines*2)+1]=end;
			
			// Top side uses the same color order as the left one
			colors[numlines]=framecolors[leftindices[colorindex]];
			colorindex+=step;
			numlines++;
		}
	}

	// Bottom side
	if(TestRectIntersection(bottomborder,invalid))
	{
		
		// We may not have to redraw the entire width of the frame itself. Rare case, but
		// it must be accounted for.
		starty=(int32) MAX(invalid.top,bottomborder.top);
		endy=(int32) MIN(invalid.bottom,bottomborder.bottom);

		// We'll need these flags to see if we must include the corners in final line
		// calculations
		r=bottomborder;
		r.bottom=r.top+borderwidth;
		r.right=r.left+borderwidth;
		leftcorner=TestRectIntersection(invalid,r);

		r=bottomborder;
		r.top=r.bottom-borderwidth;
		r.left=r.right-borderwidth;
		
		rightcorner=TestRectIntersection(invalid,r);
		step=(borderwidth==5)?1:2;
		colorindex=0;
		
		// Generate the lines for this side
		for(i=starty+1; i<=endy; i++)
		{
			start.y=end.y=i;
			
			if(leftcorner)
			{
				start.x=bottomborder.left+(borderwidth-(i-bottomborder.top));
				start.x=MAX(start.x,invalid.left);
			}
			else
				start.x=MAX(start.x+borderwidth,invalid.left);

			if(rightcorner)
			{
				end.x=bottomborder.right-(borderwidth-(i-bottomborder.top));
				end.x=MIN(end.x,invalid.right);
			}
			else
				end.x=MIN(end.x-borderwidth,invalid.right);
							
			// Make the appropriate 
			points[numlines*2]=start;
			points[(numlines*2)+1]=end;
			
			// Top side uses the same color order as the left one
			colors[numlines]=framecolors[rightindices[colorindex]];
			colorindex+=step;
			numlines++;
		}
	}

	_driver->StrokeLineArray(points,numlines,colors,&_layerdata);
	
	delete rightindices;
	delete leftindices;
	
	// Draw the resize thumb if we're supposed to
	if(!(_flags & B_NOT_RESIZABLE))
	{
		pattern_union highcolor;
		highcolor.type64=0xffffffffffffffffLL;
		r=_resizerect;

//		int32 w=r.IntegerWidth(),  h=r.IntegerHeight();
		
		// This code is strictly for B_DOCUMENT_WINDOW looks
		if(_look==B_DOCUMENT_WINDOW_LOOK)
		{
			r.right-=4;
			r.bottom-=4;
			_layerdata.highcolor=framecolors[2];
			_driver->StrokeLine(r.LeftTop(),r.RightTop(),&_layerdata,highcolor.type8);
			_driver->StrokeLine(r.LeftTop(),r.LeftBottom(),&_layerdata,highcolor.type8);

			r.OffsetBy(1,1);
			_layerdata.highcolor=framecolors[0];
			_driver->StrokeLine(r.LeftTop(),r.RightTop(),&_layerdata,highcolor.type8);
			_driver->StrokeLine(r.LeftTop(),r.LeftBottom(),&_layerdata,highcolor.type8);
			
			r.OffsetBy(1,1);
			_layerdata.highcolor=framecolors[1];
			_driver->FillRect(r,&_layerdata,highcolor.type8);
			
/*			r.left+=2;
			r.top+=2;
			r.right-=3;
			r.bottom-=3;
*/
			r.right-=2;
			r.bottom-=2;
			int32 w=r.IntegerWidth(),  h=r.IntegerHeight();
		
			rgb_color halfcol, startcol, endcol;
			float rstep,gstep,bstep,i;
			
			int steps=(w<h)?w:h;
		
			startcol=framecolors[0].GetColor32();
			endcol=framecolors[4].GetColor32();
		
			halfcol=framecolors[0].MakeBlendColor(framecolors[4],0.5).GetColor32();
		
			rstep=(startcol.red-halfcol.red)/steps;
			gstep=(startcol.green-halfcol.green)/steps;
			bstep=(startcol.blue-halfcol.blue)/steps;
			
			// Explicitly locking the driver is normally unnecessary. However, we need to do
			// this because we are rapidly drawing a series of calls which would not necessarily
			// draw correctly if we didn't do so.
			_driver->Lock();
			for(i=0;i<=steps; i++)
			{
				_layerdata.highcolor.SetColor(uint8(startcol.red-(i*rstep)),
					uint8(startcol.green-(i*gstep)),
					uint8(startcol.blue-(i*bstep)));
				
				_driver->StrokeLine(BPoint(r.left,r.top+i),
					BPoint(r.left+i,r.top),&_layerdata,(int8*)&solidhigh);
		
				_layerdata.highcolor.SetColor(uint8(halfcol.red-(i*rstep)),
					uint8(halfcol.green-(i*gstep)),
					uint8(halfcol.blue-(i*bstep)));
				_driver->StrokeLine(BPoint(r.left+steps,r.top+i),
					BPoint(r.left+i,r.top+steps),&_layerdata,(int8*)&solidhigh);			
			}
			_driver->Unlock();
//			_layerdata.highcolor=framecolors[4];
//			_driver->StrokeRect(r,&_layerdata,(int8*)&solidhigh);
		}
		else
		{
			_layerdata.highcolor=framecolors[4];
			_driver->StrokeLine(BPoint(r.right,r.top),BPoint(r.right-3,r.top),
				&_layerdata,(int8*)&solidhigh);
			_driver->StrokeLine(BPoint(r.left,r.bottom),BPoint(r.left,r.bottom-3),
				&_layerdata,(int8*)&solidhigh);
		}
	}

	
}
