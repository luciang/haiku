//------------------------------------------------------------------------------
//	Copyright (c) 2001-2005, Haiku, Inc.
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
//	File Name:		DisplayDriverImpl.cpp
//	Authors:		DarkWyrm <bpmagic@columbus.rr.com>
//					Gabe Yoder <gyoder@stny.rr.com>
//					Stephan Aßmus <superstippi@gmx.de>
//	Description:	Mostly abstract class which handles all graphics output
//					for the server
//  
//------------------------------------------------------------------------------
#include <stdio.h>

#include <Accelerant.h>

#include "Angle.h"
#include "CursorData.h"
#include "FontFamily.h"
#include "RectUtils.h"
#include "ServerCursor.h"
#include "Utils.h"

#include "DisplayDriverImpl.h"

// TODO: Implement all functions.  Bounds checking needs to be
// handled by the public drawing functions.
// Add clipping and make sure public functions have Lock & Unlock.

static Blitter blitter;

/*!
	\brief Sets up internal variables needed by all DisplayDriverImpl subclasses
*/
DisplayDriverImpl::DisplayDriverImpl()
	: DisplayDriver(),
	  fLocker("DisplayDriver lock"),
	  fCursorHandler(this)
{
}


/*!
	\brief Does nothing
*/
DisplayDriverImpl::~DisplayDriverImpl()
{
}

/*!
	\brief Called for all BView::CopyBits calls
	\param src Source rectangle.
	\param dest Destination rectangle.
	
	If the destination is not the same size as the source, the source should be scaled to fit.
*/
void DisplayDriverImpl::CopyBits(const BRect &src, const BRect &dest, const DrawData *d)
{
	if(!d)
		return;
	
	Lock();
	
	if(fCursorHandler.IntersectsCursor(dest))
		fCursorHandler.DriverHide();
	Blit(src,dest,d);
	fCursorHandler.DriverShow();
	Unlock();
}

/*!
	\brief A screen-to-screen blit (of sorts) which copies a BRegion
	\param src Source region
	\param lefttop Offset to which the region will be copied
*/
void
DisplayDriverImpl::CopyRegion(BRegion *src, const BPoint &lefttop)
{
	// TODO: Implement DisplayDriverImpl::CopyRegion
}

/*!
	\brief Inverts the colors in the rectangle.
	\param r Rectangle of the area to be inverted. Guaranteed to be within bounds.
*/
void DisplayDriverImpl::InvertRect(const BRect &r)
{
}

/*!
	\brief Called for all BView::DrawBitmap calls
	\param region Destination rects in screen coordinates
	\param bmp Bitmap to be drawn. It will always be non-NULL and valid. The color 
	space is not guaranteed to match.
	\param src Source rectangle
	\param dest Destination rectangle. Source will be scaled to fit if not the same size.
	\param d Data structure containing any other data necessary for the call. Always non-NULL.
*/
void
DisplayDriverImpl::DrawBitmap(BRegion *region, ServerBitmap *bitmap,
						  const BRect &source, const BRect &dest,
						  const DrawData *d)
{
	// TODO: make sure we all mean the same region -> see PicturePlayer line 257
	if (!region)
		return;

	Lock();
	
	FBBitmap		frameBuffer;
	FBBitmap		*bmp = &frameBuffer;
	
	blitter.Select(32, 32);
	
	if(!AcquireBuffer(&frameBuffer))
	{
		debugger("ERROR: Couldn't acquire framebuffer in DrawBitmap()\n");
		Unlock();
		return;
	}
	
	if(fCursorHandler.IntersectsCursor(dest))
		fCursorHandler.DriverHide();

	uint8 colorspace_size = (bitmap->BitsPerPixel() + 7) / 8;
	
	int32 count = region->CountRects();
	
	BRect bitmaprect(bitmap->Bounds());
	BRect sourcerect(source);
	
	BRect destrect(dest);
	destrect.right *= d->scale;
	destrect.bottom *= d->scale;
	
	if(sourcerect.left < 0) sourcerect.left = 0;
	if(sourcerect.top < 0) sourcerect.top = 0;
	
	if(sourcerect.Width() > bitmaprect.Width())
		sourcerect.right = bitmaprect.left + bitmaprect.Width();
	
	if(sourcerect.Height() > bitmaprect.Height())
		sourcerect.bottom = bitmaprect.top + bitmaprect.Height();
	
	int32 sourcewidth = (int32)sourcerect.Width() + 1;
	int32 sourceheight = (int32)sourcerect.Height() + 1;
	
	int32 destwidth = (int32)destrect.Width() + 1;
	int32 destheight = (int32)destrect.Height() + 1;
	
	int32 xscale_factor = (sourcewidth << 16) / destwidth;
	int32 yscale_factor = (sourceheight << 16) / destheight;
	
	uint8 *src_bits = (uint8 *)bitmap->Bits();
	uint8 *dest_bits = (uint8*)bmp->Bits();
	
	int32 src_row = bitmap->BytesPerRow();
	int32 dest_row = bmp->BytesPerRow();
	
	src_bits += uint32((sourcerect.top * src_row) + (sourcerect.left * colorspace_size));
	
	integer_rect src_integer_rectangle = BRect_to_integer_rect(sourcerect);
	integer_rect dst_integer_rectangle = BRect_to_integer_rect(destrect);
	
	int32 xscale_position = 0, yscale_position = 0, clipped_xscale_position = 0;
	
	for(int32 c = 0; c < count; c++)
	{
		integer_rect screen_integer_rect = BRect_to_integer_rect(region->RectAt(c));
		integer_rect src_integer_rect = src_integer_rectangle;
		integer_rect dst_integer_rect = dst_integer_rectangle;
		
		xscale_position = 0, yscale_position = 0, clipped_xscale_position = 0;
		
		if(dst_integer_rect.x < screen_integer_rect.x)
		{
			dst_integer_rect.x -= screen_integer_rect.x;
			dst_integer_rect.w += dst_integer_rect.x;
			src_integer_rect.x -= dst_integer_rect.x;
			dst_integer_rect.x = screen_integer_rect.x;
		}
		
		if(dst_integer_rect.y < screen_integer_rect.y)
		{
			dst_integer_rect.y -= screen_integer_rect.y;
			dst_integer_rect.h += dst_integer_rect.y;
			src_integer_rect.y -= dst_integer_rect.y;
			dst_integer_rect.y = screen_integer_rect.y;
		}
		
		if(dst_integer_rect.w > screen_integer_rect.w)
			dst_integer_rect.w = screen_integer_rect.w;
		
		if(dst_integer_rect.h > screen_integer_rect.h)
			dst_integer_rect.h = screen_integer_rect.h;
		
		if(src_integer_rect.x > src_integer_rectangle.x)
		{
			int32 x_scale_pixel_multiply = src_integer_rect.x - src_integer_rectangle.x;
			clipped_xscale_position = xscale_factor * x_scale_pixel_multiply;
		}
		
		if(src_integer_rect.y > src_integer_rectangle.y)
		{
			int32 y_scale_pixel_multiply = src_integer_rect.y - src_integer_rectangle.y;
			yscale_position = yscale_factor * y_scale_pixel_multiply;
		}
		
		if(dst_integer_rect.w > 0 && dst_integer_rect.h > 0)
		{
			uint8 *src_data = src_bits;
			uint8 *dst_data = (uint8 *)dest_bits + dst_integer_rect.y * dest_row + dst_integer_rect.x * colorspace_size;
			
			while(dst_integer_rect.h--)
			{
				xscale_position = clipped_xscale_position;
				uint8 *s = (uint8 *)((uint8 *)src_data + (yscale_position >> 16) * src_row);
				uint8 *d = (uint8 *)((uint8 *)dst_data);
				
				blitter.Draw(s, d, dst_integer_rect.w, xscale_position, xscale_factor);
				#if 0
				for(int32 x = 0; x < dst_integer_rect.w; x++)
				{
					*d++ = s[xscale_position >> 16];
					xscale_position += xscale_factor;
				}
				#endif
				dst_data += dest_row;
				yscale_position += yscale_factor;
			}
		}
	}
	
	fCursorHandler.DriverShow();
	ReleaseBuffer();
	Unlock();
	Invalidate(destrect);
}

void DisplayDriverImpl::CopyRegionList(BList* list, BList* pList, int32 rCount, BRegion* clipReg)
{
	Lock();
	
	FBBitmap		frameBuffer;
	FBBitmap		*bmp = &frameBuffer;
	
	if(!AcquireBuffer(&frameBuffer))
	{
		debugger("ERROR: Couldn't acquire framebuffer in CopyRegionList()\n");
		Unlock();
		return;
	}
	
	fCursorHandler.DriverHide();
	
	
	uint32		bytesPerPixel	= bmp->BytesPerRow() / bmp->Bounds().IntegerWidth();
	BList		rectList;
	int32		i, k;
	uint8		*bitmapBits = (uint8*)bmp->Bits();
	int32		Bwidth		= bmp->Bounds().IntegerWidth() + 1;
	int32		Bheight		= bmp->Bounds().IntegerHeight() + 1;

	for(k=0; k < rCount; k++)
	{
		BRegion		*reg = (BRegion*)list->ItemAt(k);
		
		int32		rectCount = reg->CountRects();
		for(i=0; i < rectCount; i++)
		{
			BRect		r		= reg->RectAt(i);
			uint8		*rectCopy;
			uint8		*srcAddress;
			uint8		*destAddress;
			int32		firstRow, lastRow;
			int32		firstCol, lastCol;
			int32		copyLength;
			int32		copyRows;
	
			firstRow	= (int32)(r.top < 0? 0: r.top);
			lastRow		= (int32)(r.bottom > (Bheight-1)? (Bheight-1): r.bottom);
			firstCol	= (int32)(r.left < 0? 0: r.left);
			lastCol		= (int32)(r.right > (Bwidth-1)? (Bwidth-1): r.right);
			copyLength	= (lastCol - firstCol + 1) < 0? 0: (lastCol - firstCol + 1);
			copyRows	= (lastRow - firstRow + 1) < 0? 0: (lastRow - firstRow + 1);
	
			rectCopy	= (uint8*)malloc(copyLength * copyRows * bytesPerPixel);
	
			srcAddress	= bitmapBits + (((firstRow) * Bwidth + firstCol) * bytesPerPixel);
			destAddress	= rectCopy;
	
			for (int32 j = 0; j < copyRows; j++)
			{
				uint8		*destRowAddress	= destAddress + (j * copyLength * bytesPerPixel);
				uint8		*srcRowAddress	= srcAddress + (j * Bwidth * bytesPerPixel);
				memcpy(destRowAddress, srcRowAddress, copyLength * bytesPerPixel );
			}
		
			rectList.AddItem(rectCopy);
		}
	}

	int32		item = 0;
	for(k=0; k < rCount; k++)
	{
		BRegion		*reg = (BRegion*)list->ItemAt(k);
	
		int32		rectCount = reg->CountRects();
		for(i=0; i < rectCount; i++)
		{
			BRect		r		= reg->RectAt(i);
			uint8		*rectCopy;
			uint8		*srcAddress;
			uint8		*destAddress;
			int32		firstRow, lastRow;
			int32		firstCol, lastCol;
			int32		copyLength, copyLength2;
			int32		copyRows, copyRows2;
	
			firstRow	= (int32)(r.top < 0? 0: r.top);
			lastRow		= (int32)(r.bottom > (Bheight-1)? (Bheight-1): r.bottom);
			firstCol	= (int32)(r.left < 0? 0: r.left);
			lastCol		= (int32)(r.right > (Bwidth-1)? (Bwidth-1): r.right);
			copyLength	= (lastCol - firstCol + 1) < 0? 0: (lastCol - firstCol + 1);
			copyRows	= (lastRow - firstRow + 1) < 0? 0: (lastRow - firstRow + 1);
	
			rectCopy	= (uint8*)rectList.ItemAt(item++);
	
			srcAddress	= rectCopy;
	
			r.Set(firstCol, firstRow, lastCol, lastRow);
			r.OffsetBy( *((BPoint*)pList->ItemAt(k%rCount)) );
			firstRow	= (int32)(r.top < 0? 0: r.top);
			lastRow		= (int32)(r.bottom > (Bheight-1)? (Bheight-1): r.bottom);
			firstCol	= (int32)(r.left < 0? 0: r.left);
			lastCol		= (int32)(r.right > (Bwidth-1)? (Bwidth-1): r.right);
			copyLength2	= (lastCol - firstCol + 1) < 0? 0: (lastCol - firstCol + 1);
			copyRows2	= (lastRow - firstRow + 1) < 0? 0: (lastRow - firstRow + 1);
	
			destAddress	= bitmapBits + (((firstRow) * Bwidth + firstCol) * bytesPerPixel);
	
			int32		minLength	= copyLength < copyLength2? copyLength: copyLength2;
			int32		minRows		= copyRows < copyRows2? copyRows: copyRows2;
	
			for (int32 j = 0; j < minRows; j++)
			{
				uint8		*destRowAddress	= destAddress + (j * Bwidth * bytesPerPixel);
				uint8		*srcRowAddress	= srcAddress + (j * copyLength * bytesPerPixel);
				memcpy(destRowAddress, srcRowAddress, minLength * bytesPerPixel );
			}
		}
	}

	for(i=0; i < rectList.CountItems(); i++)
	{
		void		*rectCopy;
		rectCopy	= rectList.ItemAt(i);
		if (rectCopy)
			free(rectCopy);
	}
	rectList.MakeEmpty();

	fCursorHandler.DriverShow();
	
	BRect		inval(bmp->Bounds());
	ReleaseBuffer();
	Unlock();

//	ConstrainClippingRegion(clipReg);
	Invalidate(inval);
//	ConstrainClippingRegion(NULL);
}

/*!
	\brief Called for all BView::FillArc calls
	\param r Rectangle enclosing the entire arc
	\param angle Starting angle for the arc in degrees
	\param span Span of the arc in degrees. Ending angle = angle+span.
	\param d  Object holding the bazillion other options
*/
void DisplayDriverImpl::FillArc(const BRect &r, const float &angle, const float &span, const DrawData *d)
{
	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	float xc = (r.left+r.right)/2;
	float yc = (r.top+r.bottom)/2;
	float rx = r.Width()/2;
	float ry = r.Height()/2;
	int Rx2 = ROUND(rx*rx);
	int Ry2 = ROUND(ry*ry);
	int twoRx2 = 2*Rx2;
	int twoRy2 = 2*Ry2;
	int p;
	int x=0;
	int y = (int)ry;
	int px = 0;
	int py = twoRx2 * y;
	int startx, endx;
	int starty, endy;
	int xclip, startclip, endclip;
	int startQuad, endQuad;
	bool useQuad1, useQuad2, useQuad3, useQuad4;
	bool shortspan = false;
	DrawData data;

	// Watch out for bozos giving us whacko spans
	if ( (span >= 360) || (span <= -360) )
	{
		FillEllipse(r,d);
		fCursorHandler.DriverShow();
		return;
	}

	Lock();

	data = *d;
	data.pensize = 1;

	if ( span > 0 )
	{
		startQuad = (int)(angle/90)%4+1;
		endQuad = (int)((angle+span)/90)%4+1;
		startx = ROUND(.5*r.Width()*fabs(cos(angle*M_PI/180)));
		endx = ROUND(.5*r.Width()*fabs(cos((angle+span)*M_PI/180)));
	}
	else
	{
		endQuad = (int)(angle/90)%4+1;
		startQuad = (int)((angle+span)/90)%4+1;
		endx = ROUND(.5*r.Width()*fabs(cos(angle*M_PI/180)));
		startx = ROUND(.5*r.Width()*fabs(cos((angle+span)*M_PI/180)));
	}

	starty = ROUND(ry*sqrt(1-(double)startx*startx/(rx*rx)));
	endy = ROUND(ry*sqrt(1-(double)endx*endx/(rx*rx)));

	if ( startQuad != endQuad )
	{
		useQuad1 = (endQuad > 1) && (startQuad > endQuad);
		useQuad2 = ((startQuad == 1) && (endQuad > 2)) || ((startQuad > endQuad) && (endQuad > 2));
		useQuad3 = ((startQuad < 3) && (endQuad == 4)) || ((startQuad < 3) && (endQuad < startQuad));
		useQuad4 = (startQuad < 4) && (startQuad > endQuad);
	}
	else
	{
		if ( (span < 90) && (span > -90) )
		{
			useQuad1 = false;
			useQuad2 = false;
			useQuad3 = false;
			useQuad4 = false;
			shortspan = true;
		}
		else
		{
			useQuad1 = (startQuad != 1);
			useQuad2 = (startQuad != 2);
			useQuad3 = (startQuad != 3);
			useQuad4 = (startQuad != 4);
		}
	}

	if ( useQuad1 )
		StrokeLine(BPoint(xc,yc-y),BPoint(xc+x,yc-y),&data);
	if ( useQuad2 )
		StrokeLine(BPoint(xc,yc-y),BPoint(xc-x,yc-y),&data);
	if ( useQuad3 )
		StrokeLine(BPoint(xc,yc+y),BPoint(xc-x,yc+y),&data);
	if ( useQuad4 )
		StrokeLine(BPoint(xc,yc+y),BPoint(xc+x,yc+y),&data);

	p = ROUND (Ry2 - (Rx2 * ry) + (.25 * Rx2));
	while (px < py)
	{
		x++;
		px += twoRy2;
		if ( p < 0 )
			p += Ry2 + px;
		else
		{
			y--;
			py -= twoRx2;
			p += Ry2 + px - py;
		}

		if ( useQuad1 )
			StrokeLine(BPoint(xc,yc-y),BPoint(xc+x,yc-y),&data);
		if ( useQuad2 )
			StrokeLine(BPoint(xc,yc-y),BPoint(xc-x,yc-y),&data);
		if ( useQuad3 )
			StrokeLine(BPoint(xc,yc+y),BPoint(xc-x,yc+y),&data);
		if ( useQuad4 )
			StrokeLine(BPoint(xc,yc+y),BPoint(xc+x,yc+y),&data);
		if ( !shortspan )
		{
			if ( startQuad == 1 )
			{
				if ( x <= startx )
					StrokeLine(BPoint(xc,yc-y),BPoint(xc+x,yc-y),&data);
				else
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc,yc-y),BPoint(xc+xclip,yc-y),&data);
				}
			}
			else if ( startQuad == 2 )
			{
				if ( x >= startx )
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc-x,yc-y),BPoint(xc-xclip,yc-y),&data);
				}
			}
			else if ( startQuad == 3 )
			{
				if ( x <= startx )
					StrokeLine(BPoint(xc-x,yc+y),BPoint(xc,yc+y),&data);
				else
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc-xclip,yc+y),BPoint(xc,yc+y),&data);
				}
			}
			else if ( startQuad == 4 )
			{
				if ( x >= startx )
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc+xclip,yc+y),BPoint(xc+x,yc+y),&data);
				}
			}

			if ( endQuad == 1 )
			{
				if ( x >= endx )
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc+xclip,yc-y),BPoint(xc+x,yc-y),&data);
				}
			}
			else if ( endQuad == 2 )
			{
				if ( x <= endx )
					StrokeLine(BPoint(xc-x,yc-y),BPoint(xc,yc-y),&data);
				else
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc-xclip,yc-y),BPoint(xc,yc-y),&data);
				}
			}
			else if ( endQuad == 3 )
			{
				if ( x >= endx )
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc-x,yc+y),BPoint(xc-xclip,yc+y),&data);
				}
			}
			else if ( endQuad == 4 )
			{
				if ( x <= endx )
					StrokeLine(BPoint(xc,yc+y),BPoint(xc+x,yc+y),&data);
				else
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc,yc+y),BPoint(xc+xclip,yc+y),&data);
				}
			}
		}
		else
		{
			startclip = ROUND(y*startx/(double)starty);
			endclip = ROUND(y*endx/(double)endy);
			if ( startQuad == 1 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc+endclip,yc-y),BPoint(xc+x,yc-y),&data);
				else
					StrokeLine(BPoint(xc+endclip,yc-y),BPoint(xc+startclip,yc-y),&data);
			}
			else if ( startQuad == 2 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc-x,yc-y),BPoint(xc-startclip,yc-y),&data);
				else
					StrokeLine(BPoint(xc-endclip,yc-y),BPoint(xc-startclip,yc-y),&data);
			}
			else if ( startQuad == 3 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc-x,yc+y),BPoint(xc-endclip,yc+y),&data);
				else
					StrokeLine(BPoint(xc-startclip,yc+y),BPoint(xc-endclip,yc+y),&data);
			}
			else if ( startQuad == 4 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc+startclip,yc+y),BPoint(xc+x,yc+y),&data);
				else
					StrokeLine(BPoint(xc+startclip,yc+y),BPoint(xc+endclip,yc+y),&data);
			}
		}
	}

	p = ROUND(Ry2*(x+.5)*(x+.5) + Rx2*(y-1)*(y-1) - Rx2*Ry2);
	while (y>0)
	{
		y--;
		py -= twoRx2;
		if (p>0)
			p += Rx2 - py;
		else
		{
			x++;
			px += twoRy2;
			p += Rx2 - py +px;
		}

		if ( useQuad1 )
			StrokeLine(BPoint(xc,yc-y),BPoint(xc+x,yc-y),&data);
		if ( useQuad2 )
			StrokeLine(BPoint(xc,yc-y),BPoint(xc-x,yc-y),&data);
		if ( useQuad3 )
			StrokeLine(BPoint(xc,yc+y),BPoint(xc-x,yc+y),&data);
		if ( useQuad4 )
			StrokeLine(BPoint(xc,yc+y),BPoint(xc+x,yc+y),&data);
		if ( !shortspan )
		{
			if ( startQuad == 1 )
			{
				if ( x <= startx )
					StrokeLine(BPoint(xc,yc-y),BPoint(xc+x,yc-y),&data);
				else
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc,yc-y),BPoint(xc+xclip,yc-y),&data);
				}
			}
			else if ( startQuad == 2 )
			{
				if ( x >= startx )
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc-x,yc-y),BPoint(xc-xclip,yc-y),&data);
				}
			}
			else if ( startQuad == 3 )
			{
				if ( x <= startx )
					StrokeLine(BPoint(xc-x,yc+y),BPoint(xc,yc+y),&data);
				else
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc-xclip,yc+y),BPoint(xc,yc+y),&data);
				}
			}
			else if ( startQuad == 4 )
			{
				if ( x >= startx )
				{
					xclip = ROUND(y*startx/(double)starty);
					StrokeLine(BPoint(xc+xclip,yc+y),BPoint(xc+x,yc+y),&data);
				}
			}

			if ( endQuad == 1 )
			{
				if ( x >= endx )
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc+xclip,yc-y),BPoint(xc+x,yc-y),&data);
				}
			}
			else if ( endQuad == 2 )
			{
				if ( x <= endx )
					StrokeLine(BPoint(xc-x,yc-y),BPoint(xc,yc-y),&data);
				else
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc-xclip,yc-y),BPoint(xc,yc-y),&data);
				}
			}
			else if ( endQuad == 3 )
			{
				if ( x >= endx )
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc-x,yc+y),BPoint(xc-xclip,yc+y),&data);
				}
			}
			else if ( endQuad == 4 )
			{
				if ( x <= endx )
					StrokeLine(BPoint(xc,yc+y),BPoint(xc+x,yc+y),&data);
				else
				{
					xclip = ROUND(y*endx/(double)endy);
					StrokeLine(BPoint(xc,yc+y),BPoint(xc+xclip,yc+y),&data);
				}
			}
		}
		else
		{
			startclip = ROUND(y*startx/(double)starty);
			endclip = ROUND(y*endx/(double)endy);
			if ( startQuad == 1 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc+endclip,yc-y),BPoint(xc+x,yc-y),&data);
				else
					StrokeLine(BPoint(xc+endclip,yc-y),BPoint(xc+startclip,yc-y),&data);
			}
			else if ( startQuad == 2 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc-x,yc-y),BPoint(xc-startclip,yc-y),&data);
				else
					StrokeLine(BPoint(xc-endclip,yc-y),BPoint(xc-startclip,yc-y),&data);
			}
			else if ( startQuad == 3 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc-x,yc+y),BPoint(xc-endclip,yc+y),&data);
				else
					StrokeLine(BPoint(xc-startclip,yc+y),BPoint(xc-endclip,yc+y),&data);
			}
			else if ( startQuad == 4 )
			{
				if ( (x <= startx) && (x >= endx) )
					StrokeLine(BPoint(xc+startclip,yc+y),BPoint(xc+x,yc+y),&data);
				else
					StrokeLine(BPoint(xc+startclip,yc+y),BPoint(xc+endclip,yc+y),&data);
			}
		}
	}
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Called for all BView::FillBezier calls.
	\param pts 4-element array of BPoints in the order of start, end, and then the two control
	points. 
	\param d draw data
*/
void DisplayDriverImpl::FillBezier(BPoint *pts, const DrawData *d)
{
	Lock();

	BezierCurve curve(pts);
	
	if(fCursorHandler.IntersectsCursor(curve.Frame()))
		fCursorHandler.DriverHide();
		
	FillPolygon(curve.GetPointArray(), curve.points.CountItems(), curve.Frame(), d);
	
	fCursorHandler.DriverShow();
	Unlock();
}

/*!
	\brief Called for all BView::FillEllipse calls
	\param r BRect enclosing the ellipse to be drawn.
	\param d DrawData containing the endless options
*/
void DisplayDriverImpl::FillEllipse(const BRect &r, const DrawData *d)
{
	float xc = (r.left+r.right)/2;
	float yc = (r.top+r.bottom)/2;
	float rx = r.Width()/2;
	float ry = r.Height()/2;
	int Rx2 = ROUND(rx*rx);
	int Ry2 = ROUND(ry*ry);
	int twoRx2 = 2*Rx2;
	int twoRy2 = 2*Ry2;
	int p;
	int x=0;
	int y = (int)ry;
	int px = 0;
	int py = twoRx2 * y;
	DrawData data;

	Lock();

	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	data = *d;
	data.pensize = 1;

	StrokeLine(BPoint(xc,yc-y),BPoint(xc,yc-y),&data);
	StrokeLine(BPoint(xc,yc+y),BPoint(xc,yc+y),&data);

	p = ROUND (Ry2 - (Rx2 * ry) + (.25 * Rx2));
	while (px < py)
	{
		x++;
		px += twoRy2;
		if ( p < 0 )
			p += Ry2 + px;
		else
		{
			y--;
			py -= twoRx2;
			p += Ry2 + px - py;
		}

               	StrokeLine(BPoint(xc-x,yc-y),BPoint(xc+x,yc-y),&data);
                StrokeLine(BPoint(xc-x,yc+y),BPoint(xc+x,yc+y),&data);
	}

	p = ROUND(Ry2*(x+.5)*(x+.5) + Rx2*(y-1)*(y-1) - Rx2*Ry2);
	while (y>0)
	{
		y--;
		py -= twoRx2;
		if (p>0)
			p += Rx2 - py;
		else
		{
			x++;
			px += twoRy2;
			p += Rx2 - py +px;
		}

               	StrokeLine(BPoint(xc-x,yc-y),BPoint(xc+x,yc-y),&data);
                StrokeLine(BPoint(xc-x,yc+y),BPoint(xc+x,yc+y),&data);
	}
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Called for all BView::FillPolygon calls
	\param ptlist Array of BPoints defining the polygon.
	\param numpts Number of points in the BPoint array.
	\param d      The 50 bazillion drawing options (inluding clip region)
*/
void DisplayDriverImpl::FillPolygon(BPoint *ptlist, int32 numpts, const BRect &bounds, const DrawData *d)
{
	// Here's the plan.  Record all line segments in polygon.  If a line segments crosses
	// the y-value of a point not in the segment, split the segment into 2 segments.
	// Once we have gone through all of the segments, sort them primarily on y-value
	// and secondarily on x-value.  Step through each y-value in the bounding rectangle
	// and look for intersections with line segments.  First intersection is start of
	// horizontal line, second intersection is end of horizontal line.  Continue for
	// all pairs of intersections.  Watch out for horizontal line segments.
	if ( !ptlist || (numpts < 3) )
		return;

	Lock();

	if(fCursorHandler.IntersectsCursor(bounds))
		fCursorHandler.DriverHide();
	
	BPoint *currentPoint, *nextPoint;
	BPoint tempNextPoint;
	BPoint tempCurrentPoint;
	int currentIndex, bestIndex, i, j, y;
	LineCalc *segmentArray = new LineCalc[2*numpts];
	int numSegments = 0;
	int minX, minY, maxX, maxY;

	minX = ROUND(ptlist[0].x);
	maxX = ROUND(ptlist[0].x);
	minY = ROUND(ptlist[0].y);
	maxY = ROUND(ptlist[0].y);
	
	// Generate the segment list
	currentPoint = ptlist;
	currentIndex = 0;
	nextPoint = &ptlist[1];
	while (currentPoint)
	{
		if ( numSegments >= 2*numpts )
		{
			printf("ERROR: Insufficient memory allocated to segment array\n");
			delete[] segmentArray;
			fCursorHandler.DriverShow();
			Unlock();
			return;
		}

		if ( ROUND(currentPoint->x) < minX )
			minX = ROUND(currentPoint->x);
		if ( ROUND(currentPoint->x) > maxX )
			maxX = ROUND(currentPoint->x);
		if ( ROUND(currentPoint->y) < minY )
			minY = ROUND(currentPoint->y);
		if ( ROUND(currentPoint->y) > maxY )
			maxY = ROUND(currentPoint->y);

		for (i=0; i<numpts; i++)
		{
			if ( ((ptlist[i].y > currentPoint->y) && (ptlist[i].y < nextPoint->y)) ||
				((ptlist[i].y < currentPoint->y) && (ptlist[i].y > nextPoint->y)) )
			{
				segmentArray[numSegments].SetPoints(*currentPoint,*nextPoint);
				tempNextPoint.x = segmentArray[numSegments].GetX(ptlist[i].y);
				tempNextPoint.y = ptlist[i].y;
				nextPoint = &tempNextPoint;
			}
		}

		segmentArray[numSegments].SetPoints(*currentPoint,*nextPoint);
		numSegments++;
		if ( nextPoint == &tempNextPoint )
		{
			tempCurrentPoint = tempNextPoint;
			currentPoint = &tempCurrentPoint;
			nextPoint = &ptlist[(currentIndex+1)%numpts];
		}
		else if ( nextPoint == ptlist )
		{
			currentPoint = NULL;
		}
		else
		{
			currentPoint = nextPoint;
			currentIndex++;
			nextPoint = &ptlist[(currentIndex+1)%numpts];
		}
	}

	// Selection sort the segments.  Probably should replace this later.
	for (i=0; i<numSegments; i++)
	{
		bestIndex = i;
		for (j=i+1; j<numSegments; j++)
		{
			if ( (segmentArray[j].MinY() < segmentArray[bestIndex].MinY()) ||
				((segmentArray[j].MinY() == segmentArray[bestIndex].MinY()) &&
				 (segmentArray[j].MinX() < segmentArray[bestIndex].MinX())) )
				bestIndex = j;
		}
		if (bestIndex != i)
			segmentArray[i].Swap(segmentArray[bestIndex]);
	}

	if ( !d->clipReg )
	{
		// Draw the lines
		for (y=minY; y<=maxY; y++)
		{
			i = 0;
			while (i<numSegments)
			{
				if (segmentArray[i].MinY() > y)
					break;
				if (segmentArray[i].MaxY() < y)
				{
					i++;
					continue;
				}
				if (segmentArray[i].MinY() == segmentArray[i].MaxY())
				{
					if ( (segmentArray[i].MinX() < fDisplayMode.virtual_width) &&
						(segmentArray[i].MaxX() >= 0) )
						StrokePatternLine(ROUND(segmentArray[i].MinX()), y,
								  ROUND(segmentArray[i].MaxX()), y, d);
					i++;
				}
				else
				{
					if ( (segmentArray[i+1].GetX(y) < fDisplayMode.virtual_width) &&
						(segmentArray[i].GetX(y) >= 0) )
						StrokePatternLine(ROUND(segmentArray[i].GetX(y)), y,
								  ROUND(segmentArray[i+1].GetX(y)), y, d);
					i+=2;
				}
			}
		}
	}
	else
	{
		int numRects, rectIndex;
		int yStart, yEnd;
		BRect clipRect;
		numRects = d->clipReg->CountRects();
		for (rectIndex = 0; rectIndex < numRects; rectIndex++)
		{
			clipRect = d->clipReg->RectAt(rectIndex);
			if ( clipRect.bottom < minY )
				continue;
			if ( clipRect.top > maxY )
				continue;
			if ( clipRect.left < minX )
				continue;
			if ( clipRect.right > maxX )
				continue;
			yStart = MAX(minY,ROUND(clipRect.top));
			yEnd = MIN(maxY,ROUND(clipRect.bottom));

			// Draw the lines
			for (y=yStart; y<=yEnd; y++)
			{
				i = 0;
				while (i<numSegments)
				{
					if (segmentArray[i].MinY() > y)
						break;
					if (segmentArray[i].MaxY() < y)
					{
						i++;
						continue;
					}
					if (segmentArray[i].MinY() == segmentArray[i].MaxY())
					{
						if (segmentArray[i].MinX() > clipRect.right)
						{
							i++;
							continue;
						}
						if (segmentArray[i].MaxX() < clipRect.left)
						{
							i++;
							continue;
						}
						StrokePatternLine(ROUND(MAX(segmentArray[i].MinX(),clipRect.left)), y,
								  ROUND(MIN(segmentArray[i].MaxX(),clipRect.right)), y, d);
						i++;
					}
					else
					{
						if (segmentArray[i].GetX(y) > clipRect.right)
						{
							i+=2;
							continue;
						}
						if (segmentArray[i+1].GetX(y) < clipRect.left)
						{
							i+=2;
							continue;
						}
						StrokePatternLine(ROUND(MAX(segmentArray[i].GetX(y),clipRect.left)), y,
								  ROUND(MIN(segmentArray[i+1].GetX(y),clipRect.right)), y, d);
						i+=2;
					}
				}
			}
		}
	}
	delete[] segmentArray;
	fCursorHandler.DriverShow();
	Invalidate(bounds);
	Unlock();
}

/*!
	\brief Called for all BView::FillRect calls
	\param r BRect to be filled. Guaranteed to be in the frame buffer's coordinate space
	\param color The color used to fill the rectangle
*/
void DisplayDriverImpl::FillRect(const BRect &r, const RGBColor &color)
{
	Lock();
	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	FillSolidRect(r,color);
	fCursorHandler.DriverShow();
	Unlock();
}

/*!
	\brief Called for all BView::FillRect calls
	\param r BRect to be filled. Guaranteed to be in the frame buffer's coordinate space
	\param pattern The pattern used to fill the rectangle
	\param high_color The high color of the pattern
	\param low_color The low color of the pattern
*/
void DisplayDriverImpl::FillRect(const BRect &r, const DrawData *d)
{
	if(!d)
		return;
	
	Lock();
	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	if ( d->clipReg )
	{
		if ( d->clipReg->Intersects(r) )
		{
			BRegion reg(r);
			reg.IntersectWith(d->clipReg);
			int numRects = reg.CountRects();

			for(int32 i=0; i<numRects;i++)
				FillPatternRect(reg.RectAt(i),d);
		}
	}
	else
		FillPatternRect(r,d);
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Convenience function for server use
	\param r BRegion to be filled
	\param pattern The pattern used to fill the region
	\param high_color The high color of the pattern
	\param low_color The low color of the pattern
*/
void DisplayDriverImpl::FillRegion(BRegion& r, const DrawData *d)
{
	if(!d)
		return;
	
	Lock();

	if(fCursorHandler.IntersectsCursor(r.Frame()))
		fCursorHandler.DriverHide();
	
	int numRects;

	if ( d->clipReg )
	{
		BRegion drawReg = r;

		drawReg.IntersectWith(d->clipReg);
		numRects = drawReg.CountRects();

		for(int32 i=0; i<numRects;i++)
			FillPatternRect(drawReg.RectAt(i),d);
	}
	else
	{
		numRects = r.CountRects();

		for(int32 i=0; i<numRects;i++)
			FillPatternRect(r.RectAt(i),d);
	}
	fCursorHandler.DriverShow();
	Invalidate(r.Frame());
	Unlock();
}

/*!
	\brief Called for all BView::FillRoundRect calls
	\param r The rectangle itself
	\param xrad X radius of the corner arcs
	\param yrad Y radius of the corner arcs
	\param d    DrawData containing all other options
*/
void DisplayDriverImpl::FillRoundRect(const BRect &r, const float &xrad, const float &yrad, const DrawData *d)
{
	float arc_x;
	float yrad2 = yrad*yrad;
	int i;

	Lock();

	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	if ( d->clipReg )
	{
		int numRects, rectIndex;
		BRect clipRect;
		int left, right, y1, y2;
		int rectTop, rectBottom, rectLeft, rectRight;

		numRects = d->clipReg->CountRects();
		for (rectIndex=0; rectIndex<numRects; rectIndex++)
		{
			clipRect = d->clipReg->RectAt(rectIndex);
			rectTop = ROUND(clipRect.top);
			rectBottom = ROUND(clipRect.bottom);
			rectLeft = ROUND(clipRect.left);
			rectRight = ROUND(clipRect.right);
			for (i=0; i<=(int)yrad; i++)
			{
				arc_x = xrad*sqrt(1-i*i/yrad2);
				left = ROUND(r.left + xrad - arc_x);
				right = ROUND(r.right - xrad + arc_x);
				if ( (left > rectRight) || (right < rectLeft) )
					continue;
				y1 = ROUND(r.top + yrad - i);
				y2 = ROUND(r.bottom - yrad + i);
				if ( (y1 >= rectTop) && (y1 <= rectBottom) )
					StrokePatternLine(MAX(left,rectLeft), y1, MIN(right,rectRight), y1,d);
				if ( (y2 >= rectTop) && (y2 <= rectBottom) )
					StrokePatternLine(MAX(left,rectLeft), y2, MIN(right,rectRight), y2,d);
			}
		}
	}
	else
	{
		for (i=0; i<=(int)yrad; i++)
		{
			arc_x = xrad*sqrt(1-i*i/yrad2);
			StrokePatternLine(ROUND(r.left+xrad-arc_x), ROUND(r.top+yrad-i), ROUND(r.right-xrad+arc_x), ROUND(r.top+yrad-i),d);
			StrokePatternLine(ROUND(r.left+xrad-arc_x), ROUND(r.bottom-yrad+i), ROUND(r.right-xrad+arc_x), ROUND(r.bottom-yrad+i),d);
		}
	}
	FillPatternRect(BRect(r.left,r.top+yrad,r.right,r.bottom-yrad),d);
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

void DisplayDriverImpl::FillShape(const BRect &bounds, const int32 &opcount, const int32 *oplist, 
		const int32 &ptcount, const BPoint *ptlist, const DrawData *d)
{
	// TODO: Implement DisplayDriverImpl::FillShape. How, though? AGG backend?
	printf("DisplayDriverImpl::FillShape unimplemented\n");
}

/*!
	\brief Called for all BView::FillTriangle calls
	\param pts Array of 3 BPoints. Always non-NULL.
	\param setLine Horizontal line drawing routine which handles things like color and pattern.
*/
void DisplayDriverImpl::FillTriangle(BPoint *pts, const BRect &bounds, const DrawData *d)
{
	if ( !pts )
		return;

	Lock();

	if(fCursorHandler.IntersectsCursor(bounds))
		fCursorHandler.DriverHide();
	
	if ( d->clipReg )
	{
		// For now, cop out and use FillPolygon
		// Need to investigate if Triangle specific code would save processing time
		FillPolygon(pts,3,bounds,d);
		fCursorHandler.DriverShow();
	}
	else
	{
		BPoint first, second, third;

		// Sort points according to their y values and x values (y is primary)
		if ( (pts[0].y < pts[1].y) ||
			((pts[0].y == pts[1].y) && (pts[0].x <= pts[1].x)) )
		{
			first=pts[0];
			second=pts[1];
		}
		else
		{
			first=pts[1];
			second=pts[0];
		}
	
		if ( (second.y<pts[2].y) ||
			((second.y == pts[2].y) && (second.x <= pts[2].x)) )
		{
			third=pts[2];
		}
		else
		{
			// second is lower than "third", so we must ensure that this third point
			// isn't higher than our first point
			third=second;
			if ( (first.y<pts[2].y) ||
				((first.y == pts[2].y) && (first.x <= pts[2].x)) )
				second=pts[2];
			else
			{
				second=first;
				first=pts[2];
			}
		}
	
		// Now that the points are sorted, check to see if they all have the same
		// y value
		if(first.y==second.y && second.y==third.y)
		{
			BPoint start,end;
			start.y=first.y;
			end.y=first.y;
			start.x=MIN(first.x,MIN(second.x,third.x));
			end.x=MAX(first.x,MAX(second.x,third.x));
			StrokePatternLine(ROUND(start.x), ROUND(start.y), ROUND(end.x), ROUND(start.y), d);
			fCursorHandler.DriverShow();
			Unlock();
			return;
		}

		int32 i;

		// Special case #1: first and second in the same row
		if(first.y==second.y)
		{
			LineCalc lineA(first, third);
			LineCalc lineB(second, third);
		
			StrokePatternLine(ROUND(first.x), ROUND(first.y), ROUND(second.x), ROUND(first.y), d);
			for(i=(int32)first.y+1; i<=third.y; i++)
				StrokePatternLine(ROUND(lineA.GetX(i)), i, ROUND(lineB.GetX(i)), i, d);
			fCursorHandler.DriverShow();
			Unlock();
			return;
		}
	
		// Special case #2: second and third in the same row
		if(second.y==third.y)
		{
			LineCalc lineA(first, second);
			LineCalc lineB(first, third);
		
			StrokePatternLine(ROUND(second.x), ROUND(second.y), ROUND(third.x), ROUND(second.y), d);
			for(i=(int32)first.y; i<third.y; i++)
				StrokePatternLine(ROUND(lineA.GetX(i)), i, ROUND(lineB.GetX(i)), i, d);
			fCursorHandler.DriverShow();
			Unlock();
			return;
		}
	
		// Normal case.	
		LineCalc lineA(first, second);
		LineCalc lineB(first, third);
		LineCalc lineC(second, third);
	
		for(i=(int32)first.y; i<(int32)second.y; i++)
			StrokePatternLine(ROUND(lineA.GetX(i)), i, ROUND(lineB.GetX(i)), i, d);

		for(i=(int32)second.y; i<=third.y; i++)
			StrokePatternLine(ROUND(lineC.GetX(i)), i, ROUND(lineB.GetX(i)), i, d);
	}
	
	fCursorHandler.DriverShow();
	Invalidate(bounds);
	
	Unlock();
}


/*!
	\brief Called for all BView::StrokeArc calls
	\param r Rectangle enclosing the entire arc
	\param angle Starting angle for the arc in degrees
	\param span Span of the arc in degrees. Ending angle = angle+span.
	\param d The drawing data for the arc

	This is inefficient and should probably be reworked
*/
void DisplayDriverImpl::StrokeArc(const BRect &r, const float &angle, const float &span, const DrawData *d)
{
	float xc = (r.left+r.right)/2;
	float yc = (r.top+r.bottom)/2;
	float rx = r.Width()/2;
	float ry = r.Height()/2;
	int Rx2 = ROUND(rx*rx);
	int Ry2 = ROUND(ry*ry);
	int twoRx2 = 2*Rx2;
	int twoRy2 = 2*Ry2;
	int p;
	int x=0;
	int y = (int)ry;
	int px = 0;
	int py = twoRx2 * y;
	int startx, endx;
	int startQuad, endQuad;
	bool useQuad1, useQuad2, useQuad3, useQuad4;
	bool shortspan = false;

	Lock();

	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	// Watch out for bozos giving us whacko spans
	if ( (span >= 360) || (span <= -360) )
	{
		StrokeEllipse(r,d);
		fCursorHandler.DriverShow();
		Unlock();
		return;
	}

	if ( span > 0 )
	{
		startQuad = (int)(angle/90)%4+1;
		endQuad = (int)((angle+span)/90)%4+1;
		startx = ROUND(.5*r.Width()*fabs(cos(angle*M_PI/180)));
		endx = ROUND(.5*r.Width()*fabs(cos((angle+span)*M_PI/180)));
	}
	else
	{
		endQuad = (int)(angle/90)%4+1;
		startQuad = (int)((angle+span)/90)%4+1;
		endx = ROUND(.5*r.Width()*fabs(cos(angle*M_PI/180)));
		startx = ROUND(.5*r.Width()*fabs(cos((angle+span)*M_PI/180)));
	}

	if ( startQuad != endQuad )
	{
		useQuad1 = (endQuad > 1) && (startQuad > endQuad);
		useQuad2 = ((startQuad == 1) && (endQuad > 2)) || ((startQuad > endQuad) && (endQuad > 2));
		useQuad3 = ((startQuad < 3) && (endQuad == 4)) || ((startQuad < 3) && (endQuad < startQuad));
		useQuad4 = (startQuad < 4) && (startQuad > endQuad);
	}
	else
	{
		if ( (span < 90) && (span > -90) )
		{
			useQuad1 = false;
			useQuad2 = false;
			useQuad3 = false;
			useQuad4 = false;
			shortspan = true;
		}
		else
		{
			useQuad1 = (startQuad != 1);
			useQuad2 = (startQuad != 2);
			useQuad3 = (startQuad != 3);
			useQuad4 = (startQuad != 4);
		}
	}

	if ( useQuad1 || 
	     (!shortspan && (((startQuad == 1) && (x <= startx)) || ((endQuad == 1) && (x >= endx)))) || 
	     (shortspan && (startQuad == 1) && (x <= startx) && (x >= endx)) ) 
		StrokePoint(BPoint(xc+x,yc-y),d);
	if ( useQuad2 || 
	     (!shortspan && (((startQuad == 2) && (x >= startx)) || ((endQuad == 2) && (x <= endx)))) || 
	     (shortspan && (startQuad == 2) && (x >= startx) && (x <= endx)) ) 
		StrokePoint(BPoint(xc-x,yc-y),d);
	if ( useQuad3 || 
	     (!shortspan && (((startQuad == 3) && (x <= startx)) || ((endQuad == 3) && (x >= endx)))) || 
	     (shortspan && (startQuad == 3) && (x <= startx) && (x >= endx)) ) 
		StrokePoint(BPoint(xc-x,yc+y),d);
	if ( useQuad4 || 
	     (!shortspan && (((startQuad == 4) && (x >= startx)) || ((endQuad == 4) && (x <= endx)))) || 
	     (shortspan && (startQuad == 4) && (x >= startx) && (x <= endx)) ) 
		StrokePoint(BPoint(xc+x,yc+y),d);

	p = ROUND (Ry2 - (Rx2 * ry) + (.25 * Rx2));
	while (px < py)
	{
		x++;
		px += twoRy2;
		if ( p < 0 )
			p += Ry2 + px;
		else
		{
			y--;
			py -= twoRx2;
			p += Ry2 + px - py;
		}

		if ( useQuad1 || 
		     (!shortspan && (((startQuad == 1) && (x <= startx)) || ((endQuad == 1) && (x >= endx)))) || 
		     (shortspan && (startQuad == 1) && (x <= startx) && (x >= endx)) ) 
			StrokePoint(BPoint(xc+x,yc-y),d);
		if ( useQuad2 || 
		     (!shortspan && (((startQuad == 2) && (x >= startx)) || ((endQuad == 2) && (x <= endx)))) || 
		     (shortspan && (startQuad == 2) && (x >= startx) && (x <= endx)) ) 
			StrokePoint(BPoint(xc-x,yc-y),d);
		if ( useQuad3 || 
		     (!shortspan && (((startQuad == 3) && (x <= startx)) || ((endQuad == 3) && (x >= endx)))) || 
		     (shortspan && (startQuad == 3) && (x <= startx) && (x >= endx)) ) 
			StrokePoint(BPoint(xc-x,yc+y),d);
		if ( useQuad4 || 
		     (!shortspan && (((startQuad == 4) && (x >= startx)) || ((endQuad == 4) && (x <= endx)))) || 
		     (shortspan && (startQuad == 4) && (x >= startx) && (x <= endx)) ) 
			StrokePoint(BPoint(xc+x,yc+y),d);
	}

	p = ROUND(Ry2*(x+.5)*(x+.5) + Rx2*(y-1)*(y-1) - Rx2*Ry2);
	while (y>0)
	{
		y--;
		py -= twoRx2;
		if (p>0)
			p += Rx2 - py;
		else
		{
			x++;
			px += twoRy2;
			p += Rx2 - py +px;
		}

		if ( useQuad1 || 
		     (!shortspan && (((startQuad == 1) && (x <= startx)) || ((endQuad == 1) && (x >= endx)))) || 
		     (shortspan && (startQuad == 1) && (x <= startx) && (x >= endx)) ) 
			StrokePoint(BPoint(xc+x,yc-y),d);
		if ( useQuad2 || 
		     (!shortspan && (((startQuad == 2) && (x >= startx)) || ((endQuad == 2) && (x <= endx)))) || 
		     (shortspan && (startQuad == 2) && (x >= startx) && (x <= endx)) ) 
			StrokePoint(BPoint(xc-x,yc-y),d);
		if ( useQuad3 || 
		     (!shortspan && (((startQuad == 3) && (x <= startx)) || ((endQuad == 3) && (x >= endx)))) || 
		     (shortspan && (startQuad == 3) && (x <= startx) && (x >= endx)) ) 
			StrokePoint(BPoint(xc-x,yc+y),d);
		if ( useQuad4 || 
		     (!shortspan && (((startQuad == 4) && (x >= startx)) || ((endQuad == 4) && (x <= endx)))) || 
		     (shortspan && (startQuad == 4) && (x >= startx) && (x <= endx)) ) 
			StrokePoint(BPoint(xc+x,yc+y),d);
	}
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Called for all BView::StrokeBezier calls.
	\param pts 4-element array of BPoints in the order of start, end, and then the two control points. 
	\param d draw data
*/
void DisplayDriverImpl::StrokeBezier(BPoint *pts, const DrawData *d)
{
	int i, numLines;

	Lock();
	BezierCurve curve(pts);
	
	if(fCursorHandler.IntersectsCursor(curve.Frame()))
		fCursorHandler.DriverHide();
	
	numLines = curve.points.CountItems()-1;
	for (i=0; i<numLines; i++)
		StrokeLine(*((BPoint*)curve.points.ItemAt(i)),*((BPoint*)curve.points.ItemAt(i+1)),d);
	
	fCursorHandler.DriverShow();
	Invalidate(curve.Frame());
	Unlock();
}

/*!
	\brief Called for all BView::StrokeEllipse calls
	\param r BRect enclosing the ellipse to be drawn.
	\param d Drawing data for the ellipse
*/
void DisplayDriverImpl::StrokeEllipse(const BRect &r, const DrawData *d)
{
	float xc = (r.left+r.right)/2;
	float yc = (r.top+r.bottom)/2;
	float rx = r.Width()/2;
	float ry = r.Height()/2;
	int Rx2 = ROUND(rx*rx);
	int Ry2 = ROUND(ry*ry);
	int twoRx2 = 2*Rx2;
	int twoRy2 = 2*Ry2;
	int p;
	int x=0;
	int y = (int)ry;
	int px = 0;
	int py = twoRx2 * y;
	int lastx, lasty;

	Lock();

	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	p = ROUND (Ry2 - (Rx2 * ry) + (.25 * Rx2));
	while (px < py)
	{
		lastx = x;
		lasty = y;
		x++;
		px += twoRy2;
		if ( p < 0 )
			p += Ry2 + px;
		else
		{
			y--;
			py -= twoRx2;
			p += Ry2 + px - py;
		}

		StrokeLine(BPoint(xc+lastx,yc-lasty),BPoint(xc+x,yc-y),d);
		StrokeLine(BPoint(xc-lastx,yc-lasty),BPoint(xc-x,yc-y),d);
		StrokeLine(BPoint(xc-lastx,yc+lasty),BPoint(xc-x,yc+y),d);
		StrokeLine(BPoint(xc+lastx,yc+lasty),BPoint(xc+x,yc+y),d);
	}

	p = ROUND(Ry2*(x+.5)*(x+.5) + Rx2*(y-1)*(y-1) - Rx2*Ry2);
	while (y>0)
	{
		lastx = x;
		lasty = y;
		y--;
		py -= twoRx2;
		if (p>0)
			p += Rx2 - py;
		else
		{
			x++;
			px += twoRy2;
			p += Rx2 - py +px;
		}

		StrokeLine(BPoint(xc+lastx,yc-lasty),BPoint(xc+x,yc-y),d);
		StrokeLine(BPoint(xc-lastx,yc-lasty),BPoint(xc-x,yc-y),d);
		StrokeLine(BPoint(xc-lastx,yc+lasty),BPoint(xc-x,yc+y),d);
		StrokeLine(BPoint(xc+lastx,yc+lasty),BPoint(xc+x,yc+y),d);
	}
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Draws a line. Really.
	\param start Starting point
	\param end Ending point
	\param c The color of the line
*/
void DisplayDriverImpl::StrokeLine(const BPoint &start, const BPoint &end, const RGBColor &color)
{
	Lock();
	if(fCursorHandler.IntersectsCursor(BRect(start,end)))
		fCursorHandler.DriverHide();
	StrokeSolidLine(ROUND(start.x),ROUND(start.y),ROUND(end.x),ROUND(end.y),color);
	
	fCursorHandler.DriverShow();
	Invalidate(BRect(start,end));
	Unlock();
}

/*!
	\brief Draws a line. Really.
	\param start Starting point
	\param end Ending point
	\param d The relevant drawing data for this line
*/
void DisplayDriverImpl::StrokeLine(const BPoint &start, const BPoint &end, const DrawData *d)
{
	Lock();
	
	if(fCursorHandler.IntersectsCursor(BRect(start,end)))
		fCursorHandler.DriverHide();
	
	if ( d->pensize == 1 )
	{
		if ( d->clipReg )
		{
			int i, numRects;
			double left, right, y1, y2;
			BRect clipRect;
			LineCalc line(start,end);
			numRects = d->clipReg->CountRects();
			for (i=0; i<numRects; i++)
			{
				clipRect = d->clipReg->RectAt(i);
				left = MAX(line.MinX(), clipRect.left);
				right = MIN(line.MaxX(), clipRect.right);
				if ( right < left )
					continue;
				y1 = line.GetY(left);
				y2 = line.GetY(right);
				if ( MAX(y1,y2) < clipRect.top )
					continue;
				if ( MIN(y1,y2) > clipRect.bottom )
					continue;
                                if ( y1 < clipRect.top )
                                {
                                	y1 = clipRect.top;
					left = line.GetX(y1);
                                }
				if ( y1 > clipRect.bottom )
                                {
                                	y1 = clipRect.bottom;
					left = line.GetX(y1);
                                }
                                if ( y2 < clipRect.top )
                                {
                                	y2 = clipRect.top;
					right = line.GetX(y2);
                                }
                                if ( y2 > clipRect.bottom )
                                {
                                	y2 = clipRect.bottom;
					right = line.GetX(y2);
                                }
				StrokePatternLine(ROUND(left),ROUND(y1),ROUND(right),ROUND(y2),d);
			}
		}
		else
			StrokePatternLine(ROUND(start.x),ROUND(start.y),ROUND(end.x),ROUND(end.y),d);
	}
	else
	{
		BPoint corners[4];
		double halfWidth = .5*d->pensize;
		corners[0] = start;
		corners[1] = start;
		corners[2] = end;
		corners[3] = end;
		if ( (start.x == end.x) && (start.y == end.y) )
		{
			FillRect(BRect(start.x-halfWidth,start.y-halfWidth,start.x+halfWidth,start.y+halfWidth),d);
		}
		else
		{
			if ( start.x == end.x )
			{
				corners[0].x += halfWidth;
				corners[1].x -= halfWidth;
				corners[2].x -= halfWidth;
				corners[3].x += halfWidth;
			}
			else if ( start.y == end.y )
			{
				corners[0].y -= halfWidth;
				corners[1].y += halfWidth;
				corners[2].y += halfWidth;
				corners[3].y -= halfWidth;
			}
			else
			{
				double angle, xoffset, yoffset;
				// TODO try to find a way to avoid atan2, sin, and cos
				angle = atan2(end.y-start.y, end.x-start.x) + M_PI/4;
				xoffset = halfWidth*cos(angle);
				yoffset = halfWidth*sin(angle);
				corners[0].x += xoffset;
				corners[0].y += yoffset;
				corners[1].x -= xoffset;
				corners[1].y -= yoffset;
				corners[2].x -= xoffset;
				corners[2].y -= yoffset;
				corners[3].x += xoffset;
				corners[3].y += yoffset;
			}
			FillPolygon(corners,4,BRect(start,end),d);
		}
	}
	fCursorHandler.DriverShow();
	Invalidate(BRect(start,end));
	Unlock();
}

void DisplayDriverImpl::StrokePoint(const BPoint& pt, const RGBColor &color)
{
	StrokeLine(pt, pt, color);
	Invalidate(BRect(pt,pt));
}

void DisplayDriverImpl::StrokePoint(const BPoint& pt, const DrawData *d)
{
	StrokeLine(pt, pt, d);
	Invalidate(BRect(pt,pt));
}

/*!
	\brief Called for all BView::StrokePolygon calls
	\param ptlist Array of BPoints defining the polygon.
	\param numpts Number of points in the BPoint array.
	\param d      DrawData containing all of the other options
*/
void DisplayDriverImpl::StrokePolygon(BPoint *ptlist, int32 numpts, const BRect &bounds, const DrawData *d, bool is_closed)
{
	if(!ptlist)
		return;

	Lock();
	if(fCursorHandler.IntersectsCursor(bounds))
		fCursorHandler.DriverHide();
	
	for(int32 i=0; i<(numpts-1); i++)
		StrokeLine(ptlist[i],ptlist[i+1],d);
	if(is_closed)
		StrokeLine(ptlist[numpts-1],ptlist[0],d);
	
	fCursorHandler.DriverShow();
	Invalidate(bounds);
	Unlock();
}

/*!
	\brief Called by Decorator
	\param r BRect to be drawn
	\param color Color in which to draw
*/
void DisplayDriverImpl::StrokeRect(const BRect &r, const RGBColor &color)
{
	Lock();
	
	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	StrokeSolidRect(r,color);
	
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Called for all BView::StrokeRect calls
	\param r BRect to be drawn
	\param d      DrawData containing all of the other options
*/
void DisplayDriverImpl::StrokeRect(const BRect &r, const DrawData *d)
{
	Lock();
	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	StrokeLine(r.LeftTop(),r.RightTop(),d);
	StrokeLine(r.LeftTop(),r.LeftBottom(),d);
	StrokeLine(r.RightTop(),r.RightBottom(),d);
	StrokeLine(r.LeftBottom(),r.RightBottom(),d);
	
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

/*!
	\brief Convenience function for server use
	\param r BRegion to be stroked
	\param d Data structure containing any other data necessary for the call. Always non-NULL.

*/
void DisplayDriverImpl::StrokeRegion(BRegion& r, const DrawData *d)
{
	Lock();
	
	if(fCursorHandler.IntersectsCursor(r.Frame()))
		fCursorHandler.DriverHide();
	
	for(int32 i=0; i<r.CountRects();i++)
		StrokeRect(r.RectAt(i),d);
	
	fCursorHandler.DriverShow();
	Invalidate(r.Frame());
	Unlock();
}

/*!
	\brief Called for all BView::StrokeRoundRect calls
	\param r The rect itself
	\param xrad X radius of the corner arcs
	\param yrad Y radius of the corner arcs
	\param d    DrawData containing all other options
*/
void DisplayDriverImpl::StrokeRoundRect(const BRect &r, const float &xrad, const float &yrad, const DrawData *d)
{
	int hLeft, hRight;
	int vTop, vBottom;
	float bLeft, bRight, bTop, bBottom;

	hLeft = (int)ROUND(r.left + xrad);
	hRight = (int)ROUND(r.right - xrad);
	vTop = (int)ROUND(r.top + yrad);
	vBottom = (int)ROUND(r.bottom - yrad);
	bLeft = hLeft + xrad;
	bRight = hRight -xrad;
	bTop = vTop + yrad;
	bBottom = vBottom - yrad;

	Lock();
	
	if(fCursorHandler.IntersectsCursor(r))
		fCursorHandler.DriverHide();
	
	StrokeArc(BRect(bRight, r.top, r.right, bTop), 0, 90, d);
	StrokeLine(BPoint(hRight, r.top), BPoint(hLeft, r.top), d);
	
	StrokeArc(BRect(r.left,r.top,bLeft,bTop), 90, 90, d);
	StrokeLine(BPoint(r.left, vTop), BPoint(r.left, vBottom), d);

	StrokeArc(BRect(r.left,bBottom,bLeft,r.bottom), 180, 90, d);
	StrokeLine(BPoint(hLeft, r.bottom), BPoint(hRight, r.bottom), d);

	StrokeArc(BRect(bRight,bBottom,r.right,r.bottom), 270, 90, d);
	StrokeLine(BPoint(r.right, vBottom), BPoint(r.right, vTop), d);
	
	fCursorHandler.DriverShow();
	Invalidate(r);
	Unlock();
}

void DisplayDriverImpl::StrokeShape(const BRect &bounds, const int32 &opcount, const int32 *oplist, 
		const int32 &ptcount, const BPoint *ptlist, const DrawData *d)
{
	// TODO: Implement DisplayDriverImpl::StrokeShape. ShapeIterator subclass?
	printf("DisplayDriverImpl::StrokeShape unimplemented\n");
}

/*!
	\brief Called for all BView::StrokeTriangle calls
*/
void DisplayDriverImpl::StrokeTriangle(BPoint *pts, const BRect &bounds, const DrawData *d)
{
	Lock();
	if(fCursorHandler.IntersectsCursor(bounds))
		fCursorHandler.DriverHide();

	StrokeLine(pts[0],pts[1],d);
	StrokeLine(pts[1],pts[2],d);
	StrokeLine(pts[2],pts[0],d);
	
	fCursorHandler.DriverShow();
	Invalidate(bounds);
	Unlock();
}


void DisplayDriverImpl::DrawString(const char *string, const int32 &length, const BPoint &pt, const RGBColor &color, escapement_delta *delta)
{
	DrawData d;
	d.highcolor=color;
	
	if(delta)
		d.edelta=*delta;
	DrawString(string,length,pt,&d);
}

/*!
	\brief Utilizes the font engine to draw a string to the frame buffer
	\param string String to be drawn. Always non-NULL.
	\param length Number of characters in the string to draw. Always greater than 0. If greater
	than the number of characters in the string, draw the entire string.
	\param pt Point at which the baseline starts. Characters are to be drawn 1 pixel above
	this for backwards compatibility. While the point itself is guaranteed to be inside
	the frame buffers coordinate range, the clipping of each individual glyph must be
	performed by the driver itself.
	\param d Data structure containing any other data necessary for the call. Always non-NULL.
*/
void DisplayDriverImpl::DrawString(const char *string, const int32 &length, const BPoint &pt, DrawData *d)
{
	if(!string || !d)
		return;
	
	Lock();
	
	// TODO: properly calculate intersecting rectangle with cursor in DisplayDriverImpl::DrawString
	
	// Rough guesstimate for size
	BRect intersection(pt.x, pt.x, pt.y, pt.y);
	intersection.top -= d->font.Size() * 1.5;
	intersection.right += d->font.Size() * 1.5 * length;
	
	if (fCursorHandler.IntersectsCursor(intersection))
		fCursorHandler.DriverHide();
	
	BPoint point(pt);
	const ServerFont *font = &(d->font);
	
	bool antialias = true;
	if (font->Size() < 18 && font->Flags() & B_DISABLE_ANTIALIASING)
		antialias = false;
	
	// Originally, I thought to do this shear checking here, but it really should be
	// done in BFont::SetShear()
	Angle shear(font->Shear());
	if (font->Shear() != 90) {
		float shearangle = shear.Value();
		if (shearangle > 135)
			shearangle = 135;
		if (shearangle < 45)
			shearangle = 45;
		
		if(shearangle > 90)
			shear = 90 + ((180 - shearangle) * 2);
		else
			shear = 90 - (90 - shearangle) * 2;
	}
	
	FT_Face face = font->GetFTFace();
	if (!face) {
		Unlock();
		return;
	}
	
	uint16 error = FT_Set_Char_Size(face, 0, int32(font->Size()) * 64, 72, 72);
	if (error) {
		Unlock();
		return;
	}
	
	bool use_kerning = font->HasKerning() && font->Spacing() == B_STRING_SPACING;
	
	FT_Vector space, nonspace;
	Angle rotation(font->Rotation());
	
	// First, rotate
	FT_Matrix rmatrix;
	rmatrix.xx = (FT_Fixed)( rotation.Cosine()*0x10000);
	rmatrix.xy = (FT_Fixed)(-rotation.Sine()*0x10000);
	rmatrix.yx = (FT_Fixed)( rotation.Sine()*0x10000);
	rmatrix.yy = (FT_Fixed)( rotation.Cosine()*0x10000);
	
	// Next, shear
	FT_Matrix smatrix;
	smatrix.xx = (FT_Fixed)(0x10000); 
	smatrix.xy = (FT_Fixed)(-shear.Cosine()*0x10000);
	smatrix.yx = (FT_Fixed)(0);
	smatrix.yy = (FT_Fixed)(0x10000);
	
	// Multiply togheter
	FT_Matrix_Multiply(&rmatrix, &smatrix);
	
	// Set up the increment value for escapement padding
	space.x = int32(d->edelta.space * rotation.Cosine() * 64);
	space.y = int32(d->edelta.space * rotation.Sine() * 64);
	nonspace.x = int32(d->edelta.nonspace * rotation.Cosine() * 64);
	nonspace.y = int32(d->edelta.nonspace * rotation.Sine() * 64);
		
	// set the pen position in 26.6 cartesian space coordinates
	FT_Vector pen;
	pen.x = (int32)point.x * 64;
	pen.y = (int32)point.y * 64;
	
	int32 strlength = strlen(string);
	if(length < strlength)
		strlength = length;
	
	FT_Vector delta;
	FT_GlyphSlot slot = face->glyph;
	FT_UInt glyph_index = 0, previous = 0;
	for (int32 i = 0; i < strlength; i++) {
		FT_Set_Transform(face, &smatrix, &pen);
		
		// Handle escapement padding option
		if((uint8)string[i] <= 0x20) {
			pen.x += space.x;
			pen.y += space.y;
		} else {
			pen.x += nonspace.x;
			pen.y += nonspace.y;
		}
		
		// get kerning and move pen
		if(use_kerning && previous && glyph_index) {
			FT_Get_Kerning(face, previous, glyph_index, ft_kerning_default, &delta);
			pen.x += delta.x;
			pen.y += delta.y;
		}
		
		error = FT_Load_Char(face, string[i], (antialias ? FT_LOAD_RENDER : FT_LOAD_RENDER | FT_LOAD_MONOCHROME));
		if (error)
			continue;
		
		if (antialias)
			BlitGray2RGB32(&slot->bitmap, BPoint(slot->bitmap_left, point.y - (slot->bitmap_top - point.y)), d);
		else
			BlitMono2RGB32(&slot->bitmap, BPoint(slot->bitmap_left, point.y - (slot->bitmap_top - point.y)), d);
		
		// increment pen position
		pen.x += slot->advance.x;
		pen.y += slot->advance.y;
		previous = glyph_index;
	}

	// TODO: implement calculation of invalid rectangle in DisplayDriverImpl::DrawString properly
	BRect r;
	r.left = MIN(point.x, pen.x >> 6);
	r.right = MAX(point.x, pen.x >> 6);
	r.top = point.y - face->height;
	r.bottom = point.y + face->height;
	
	fCursorHandler.DriverShow();
	Invalidate(r);
	
	// Update the caller's pen position
	d->penlocation.x = pen.x / 64;
	d->penlocation.y = pen.y / 64;
	
	Unlock();
}

/*!
	\brief Gets the width of a string in pixels
	\param string Source null-terminated string
	\param length Number of characters in the string
	\param d Data structure containing any other data necessary for the call. Always non-NULL.
	\return Width of the string in pixels
	
	This corresponds to BView::StringWidth.
*/
float DisplayDriverImpl::StringWidth(const char *string, int32 length, const DrawData *d)
{
	if(!string || !d)
		return 0.0;
	
	Lock();
	
	const ServerFont *font = &(d->font);
	
	FT_Face face = font->GetFTFace();
	if(!face) {
		Unlock();
		return 0.0;
	}
	
	uint16 error = FT_Set_Char_Size(face, 0, int32(font->Size()) * 64, 72, 72);
	if (error) {
		Unlock();
		return 0.0;
	}
	
	bool use_kerning = font->HasKerning() && font->Spacing() == B_STRING_SPACING;
	
	// set the pen position in 26.6 cartesian space coordinates
	FT_Vector pen, delta;
	pen.x = 0;
	
	int32 strlength = strlen(string);
	if (length < strlength)
		strlength = length;
	
	FT_GlyphSlot slot = face->glyph;
	FT_UInt glyph_index = 0, previous = 0;
	for (int32 i = 0; i < strlength; i++) {
		// get kerning and move pen
		if (use_kerning && previous && glyph_index) {
			FT_Get_Kerning(face, previous, glyph_index, ft_kerning_default, &delta);
			pen.x += delta.x;
		}
		
		error = FT_Load_Char(face, string[i], FT_LOAD_MONOCHROME);
		if (error)
			continue;
		
		// increment pen position
		pen.x += slot->advance.x;
		previous = glyph_index;
	}
	
	Unlock();
	return pen.x >> 6;
}

/*!
	\brief Gets the height of a string in pixels
	\param string Source null-terminated string
	\param length Number of characters in the string
	\param d Data structure containing any other data necessary for the call. Always non-NULL.
	\return Height of the string in pixels
	
	The height calculated in this function does not include any padding - just the
	precise maximum height of the characters within and does not necessarily equate
	with a font's height, i.e. the strings 'case' and 'alps' will have different values
	even when called with all other values equal.
*/
float DisplayDriverImpl::StringHeight(const char *string, int32 length, const DrawData *d)
{
	if (!string || !d)
		return 0.0;
	
	Lock();
	
	const ServerFont *font = &(d->font);
	
	FT_Face face = font->GetFTFace();
	if (!face) {
		Unlock();
		return 0.0;
	}
	
	uint16 error = FT_Set_Char_Size(face, 0, int32(font->Size()) * 64, 72, 72);
	
	int32 strlength = strlen(string);
	if (length < strlength)
		strlength = length;
	
	FT_GlyphSlot slot = face->glyph;
	float ascent = 0.0, descent = 0.0;
	for (int32 i = 0; i < strlength; i++) {
		error = FT_Load_Char(face, string[i], FT_LOAD_MONOCHROME);
		if (error)
			continue;
		
		if (slot->metrics.horiBearingY < slot->metrics.height)
			descent = MAX((slot->metrics.height - slot->metrics.horiBearingY) >> 6, descent);
		else
			ascent = MAX(slot->bitmap.rows, ascent);
	}
	
	Unlock();
	return ascent + descent;
}

/*!
	\brief Retrieves the bounding box each character in the string
	\param string Source null-terminated string
	\param count Number of characters in the string
	\param mode Metrics mode for either screen or printing
	\param delta Optional glyph padding. This value may be NULL.
	\param rectarray Array of BRect objects which will have at least count elements
	\param d Data structure containing any other data necessary for the call. Always non-NULL.

	See BFont::GetBoundingBoxes for more details on this function.
*/
void DisplayDriverImpl::GetBoundingBoxes(const char *string, int32 count, 
		font_metric_mode mode, escapement_delta *delta, BRect *rectarray, const DrawData *d)
{
	// TODO: Implement DisplayDriverImpl::GetBoundingBoxes
}

/*!
	\brief Retrieves the escapements for each character in the string
	\param string Source null-terminated string
	\param charcount Number of characters in the string
	\param delta Optional glyph padding. This value may be NULL.
	\param escapements Array of escapement_delta objects which will have at least charcount elements
	\param offsets Actual offset values when iterating over the string. This array will also 
		have at least charcount elements and the values placed therein will reflect 
		the current kerning/spacing mode.
	\param d Data structure containing any other data necessary for the call. Always non-NULL.
	
	See BFont::GetEscapements for more details on this function.
*/
void DisplayDriverImpl::GetEscapements(const char *string, int32 charcount, 
		escapement_delta *delta, escapement_delta *escapements, escapement_delta *offsets, const DrawData *d)
{
	// TODO: Implement DisplayDriverImpl::GetEscapements
}

/*!
	\brief Retrieves the inset values of each glyph from its escapement values
	\param string Source null-terminated string
	\param charcount Number of characters in the string
	\param edgearray Array of edge_info objects which will have at least charcount elements
	\param d Data structure containing any other data necessary for the call. Always non-NULL.

	See BFont::GetEdges for more details on this function.
*/
void DisplayDriverImpl::GetEdges(const char *string, int32 charcount, edge_info *edgearray, const DrawData *d)
{
	// TODO: Implement DisplayDriverImpl::GetEdges
}

/*!
	\brief Determines whether a font contains a certain string of characters
	\param string Source null-terminated string
	\param charcount Number of characters in the string
	\param hasarray Array of booleans which will have at least charcount elements

	See BFont::GetHasGlyphs for more details on this function.
*/
void DisplayDriverImpl::GetHasGlyphs(const char *string, int32 charcount, bool *hasarray)
{
	// TODO: Implement DisplayDriverImpl::GetHasGlyphs
}

/*!
	\brief Truncates an array of strings to a certain width
	\param instrings Array of null-terminated strings
	\param stringcount Number of strings passed to the function
	\param mode Truncation mode
	\param maxwidth Maximum width for all strings
	\param outstrings String array provided by the caller into which the truncated strings are
		to be placed.

	See BFont::GetTruncatedStrings for more details on this function.
*/
void DisplayDriverImpl::GetTruncatedStrings(const char **instrings,const int32 &stringcount, 
	const uint32 &mode, const float &maxwidth, char **outstrings)
{
	// TODO: Implement DisplayDriverImpl::GetTruncatedStrings
}

/*!
	\brief Hides the cursor.
	
	Hide calls are not nestable, unlike that of the BApplication class. Subclasses should
	call _SetCursorHidden(true) somewhere within this function to ensure that data is
	maintained accurately. Subclasses must include a call to DisplayDriver::HideCursor
	for proper state tracking.
*/
void
DisplayDriverImpl::HideCursor()
{
	Lock();
	fCursorHandler.Hide();	
	Unlock();
}

/*!
	\brief Returns whether the cursor is visible or not.
	\return true if hidden or obscured, false if not.

*/
bool
DisplayDriverImpl::IsCursorHidden()
{
	Lock();
	bool value = fCursorHandler.IsHidden();
	Unlock();

	return value;
}

/*!
	\brief Moves the cursor to the given point.

	The coordinates passed to MoveCursorTo are guaranteed to be within the frame buffer's
	range, but the cursor data itself will need to be clipped. A check to see if the 
	cursor is obscured should be made and if so, a call to _SetCursorObscured(false) 
	should be made the cursor in addition to displaying at the passed coordinates.
*/
void
DisplayDriverImpl::MoveCursorTo(const float &x, const float &y)
{
	Lock();
	fCursorHandler.MoveTo(BPoint(x, y));
	Unlock();
}

/*!
	\brief Shows the cursor.
	
	Show calls are not nestable, unlike that of the BApplication class. Subclasses should
	call _SetCursorHidden(false) somewhere within this function to ensure that data is
	maintained accurately. Subclasses must call DisplayDriver::ShowCursor at some point
	to ensure proper state tracking.
*/
void
DisplayDriverImpl::ShowCursor()
{
	Lock();
	fCursorHandler.Show();
	Unlock();
}

/*!
	\brief Obscures the cursor.
	
	Obscure calls are not nestable. Subclasses should call DisplayDriver::ObscureCursor
	somewhere within this function to ensure that data is maintained accurately. A check
	will be made by the system before the next MoveCursorTo call to show the cursor if
	it is obscured.
*/
void
DisplayDriverImpl::ObscureCursor()
{
	Lock();
	fCursorHandler.Obscure();	
	Unlock();

}

/*!
	\brief Changes the cursor.
	\param cursor The new cursor. Guaranteed to be non-NULL.
	
	The driver does not take ownership of the given cursor. Subclasses should make
	a copy of the cursor passed to it. The default version of this function hides the
	cursory, replaces it, and shows the cursor if previously visible.
*/
void
DisplayDriverImpl::SetCursor(ServerCursor *cursor)
{
	Lock();
	fCursorHandler.SetCursor(cursor);
	Unlock();
}

//! Returns the cursor's current position
BPoint
DisplayDriverImpl::GetCursorPosition()
{
	Lock();
	BPoint pos = fCursorHandler.GetPosition();
	Unlock();
	
	return pos;
}

/*!
	\brief Returns whether or not the cursor is currently obscured
	\return True if obscured, false if not.
*/
bool
DisplayDriverImpl::IsCursorObscured(bool state)
{
	Lock();
	bool obscured = fCursorHandler.IsObscured();
	Unlock();
	
	return obscured;
}

/*!
	\brief Locks the driver
	\param timeout Optional timeout specifier
	\return True if the lock was successful, false if not.
	
	The return value need only be checked if a timeout was specified. Each public
	member function should lock the driver before doing anything else. Functions
	internal to the driver (protected/private) need not do this.
*/
bool
DisplayDriverImpl::Lock(bigtime_t timeout)
{
	if (timeout == B_INFINITE_TIMEOUT)
		return fLocker.Lock();
	
	return (fLocker.LockWithTimeout(timeout) == B_OK) ? true : false;
}

/*!
	\brief Unlocks the driver
*/
void
DisplayDriverImpl::Unlock()
{
	fLocker.Unlock();
}

/*!
	\brief Dumps the contents of the frame buffer to a file.
	\param path Path and leaf of the file to be created without an extension
	\return False if unimplemented or unsuccessful. True if otherwise.
	
	Subclasses should add an extension based on what kind of file is saved
*/
bool DisplayDriverImpl::DumpToFile(const char *path)
{
	return false;
}

/*!
	\brief Returns a new ServerBitmap containing the contents of the frame buffer
	\return A new ServerBitmap containing the contents of the frame buffer or NULL if unsuccessful
*/
ServerBitmap *DisplayDriverImpl::DumpToBitmap()
{
	return NULL;
}

/*!
	\brief Draws a series of lines - optimized for speed
	\param numlines Number of lines to be drawn
	\param linedata Array of LineArrayData objects
	\param d current DrawData settings
*/
void DisplayDriverImpl::StrokeLineArray(const int32 &numlines, const LineArrayData *linedata,const DrawData *d)
{
	if(!d || !linedata)
		return;
	
	const LineArrayData *dataindex=linedata;
	BRect r(0,0,0,0);
	DrawData drawdata;

	Lock();
	
	fCursorHandler.DriverHide();
	
	drawdata = *d;
	
	r.Set(linedata->pt1.x,linedata->pt1.y,linedata->pt1.x,linedata->pt1.y);
	
	for (int32 i=0; i<numlines; i++)
	{
		dataindex=(const LineArrayData*) &linedata[i];
		drawdata.highcolor = dataindex->color;
		
		// Keep track of the invalid region
		if (dataindex->pt1.x < r.left)
			r.left = dataindex->pt1.x;
		if (dataindex->pt1.y < r.top)
			r.top = dataindex->pt1.y;
		if (dataindex->pt1.x > r.right)
			r.right = dataindex->pt1.x;
		if (dataindex->pt1.y > r.bottom)
			r.bottom = dataindex->pt1.y;
		
		if (dataindex->pt2.x < r.left)
			r.left = dataindex->pt2.x;
		if (dataindex->pt2.y < r.top)
			r.top = dataindex->pt2.y;
		if (dataindex->pt2.x > r.right)
			r.right = dataindex->pt2.x;
		if (dataindex->pt2.y > r.bottom)
			r.bottom = dataindex->pt2.y;
		
		StrokeLine(dataindex->pt1,dataindex->pt2,&drawdata);
	}
	
	Invalidate(r);
	
	fCursorHandler.DriverShow();
	
	Unlock();
}


/*!
	\brief Returns data about the rendering device
	\param info Pointer to an object to receive the device info
	\return B_OK if this function is supported, B_UNSUPPORTED if not
	
	The default implementation of this returns B_UNSUPPORTED and does nothing.

	From Accelerant.h:
	
	uint32	version;			// structure version number
	char 	name[32];			// a name the user will recognize the device by
	char	chipset[32];		// the chipset used by the device
	char	serial_no[32];		// serial number for the device
	uint32	memory;				// amount of memory on the device, in bytes
	uint32	dac_speed;			// nominal DAC speed, in MHz

*/
status_t DisplayDriverImpl::GetDeviceInfo(accelerant_device_info *info)
{
	return B_ERROR;
}

/*!
	\brief Returns data about the rendering device
	\param mode_list Pointer to receive a list of modes.
	\param count The number of modes in mode_list
	\return B_OK if this function is supported, B_UNSUPPORTED if not
	
	The default implementation of this returns B_UNSUPPORTED and does nothing.
*/
status_t DisplayDriverImpl::GetModeList(display_mode **mode_list, uint32 *count)
{
	return B_UNSUPPORTED;
}

/*!
	\brief Obtains the minimum and maximum pixel throughput
	\param mode Structure to receive the data for the given mode
	\param low Recipient of the minimum clock rate
	\param high Recipient of the maximum clock rate
	\return 
	- \c B_OK: Everything is kosher
	- \c B_UNSUPPORTED: The function is unsupported
	- \c B_ERROR: No known pixel clock limits
	
	
	This function returns the minimum and maximum "pixel clock" rates, in 
	thousands-of-pixels per second, that are possible for the given mode. See 
	BScreen::GetPixelClockLimits() for more information.

	The default implementation of this returns B_UNSUPPORTED and does nothing.
*/
status_t DisplayDriverImpl::GetPixelClockLimits(display_mode *mode, uint32 *low, uint32 *high)
{
	return B_UNSUPPORTED;
}

/*!
	\brief Obtains the timing constraints of the current display mode. 
	\param dtc Object to receive the constraints
	\return 
	- \c B_OK: Everything is kosher
	- \c B_UNSUPPORTED: The function is unsupported
	- \c B_ERROR: No known timing constraints
	
	The default implementation of this returns B_UNSUPPORTED and does nothing.
*/
status_t DisplayDriverImpl::GetTimingConstraints(display_timing_constraints *dtc)
{
	return B_UNSUPPORTED;
}

/*!
	\brief Obtains the timing constraints of the current display mode. 
	\param dtc Object to receive the constraints
	\return 
	- \c B_OK: Everything is kosher
	- \c B_UNSUPPORTED: The function is unsupported
	
	The default implementation of this returns B_UNSUPPORTED and does nothing. 
	This is mostly the responsible of the hardware driver if the DisplayDriverImpl 
	interfaces with	actual hardware.
*/
status_t DisplayDriverImpl::ProposeMode(display_mode *candidate, const display_mode *low, const display_mode *high)
{
	return B_UNSUPPORTED;
}

/*!
	\brief Waits for the device's vertical retrace
	\param timeout Amount of time to wait until retrace. Default is B_INFINITE_TIMEOUT
	\return 
	- \c B_OK: Everything is kosher
	- \c B_ERROR: The function timed out before retrace
	- \c B_UNSUPPORTED: The function is unsupported
	
	The default implementation of this returns B_UNSUPPORTED and does nothing. 
*/
status_t DisplayDriverImpl::WaitForRetrace(bigtime_t timeout)
{
	return B_UNSUPPORTED;
}


/*!
	\brief Obtains the current cursor for the driver.
	\return Pointer to the current cursor object.
	
	Do NOT delete this pointer - change pointers via SetCursor. This call will be 
	necessary for blitting the cursor to the screen and other such tasks.
*/
ServerCursor *DisplayDriverImpl::_GetCursor()
{
	Lock();
	ServerCursor *c=fCursorHandler.GetCursor();
	Unlock();
	
	return c;
}

void DisplayDriverImpl::HLinePatternThick(int32 x1, int32 x2, int32 y)
{
}

void DisplayDriverImpl::VLinePatternThick(int32 x1, int32 x2, int32 y)
{
}

/*!
	\brief Draws a point of a specified thickness
	\param x The x coordinate (not guaranteed to be in bounds)
	\param y The y coordinate (not guaranteed to be in bounds)
	\param thick The thickness of the point
	\param pat The PatternHandler which detemines pixel colors
	Must be implemented in subclasses
*/
void DisplayDriverImpl::SetThickPatternPixel(int x, int y)
{
}

/*!
	\brief Draws a horizontal line
	\param x1 The first x coordinate (guaranteed to be in bounds)
	\param x2 The second x coordinate (guaranteed to be in bounds)
	\param y The y coordinate (guaranteed to be in bounds)
	\param pat The PatternHandler which detemines pixel colors
	Must be implemented in subclasses
*/

void DisplayDriverImpl::BlitMono2RGB32(FT_Bitmap *src, const BPoint &pt, const DrawData *d)
{
	rgb_color color=d->highcolor.GetColor32();
	
	// pointers to the top left corner of the area to be copied in each bitmap
	uint8 *srcbuffer, *destbuffer;
	FBBitmap framebuffer;
	
	if(!AcquireBuffer(&framebuffer))
	{
		printf("ERROR: Couldn't acquire framebuffer in BlitMono2RGB32\n");
		return;
	}
	
	// index pointers which are incremented during the course of the blit
	uint8 *srcindex, *destindex, *rowptr, value;
	
	// increment values for the index pointers
	int32 srcinc=src->pitch, destinc=framebuffer.BytesPerRow();
	
	int16 i,j,k, srcwidth=src->pitch, srcheight=src->rows;
	int32 x=(int32)pt.x,y=(int32)pt.y;
	
	// starting point in source bitmap
	srcbuffer=(uint8*)src->buffer;

	if(y<0)
	{
		if(y<pt.y)
			y++;
		srcbuffer+=srcinc * (0-y);
		srcheight-=srcinc;
		destbuffer+=destinc * (0-y);
	}

	if(y+srcheight>framebuffer.Bounds().IntegerHeight())
	{
		if(y>pt.y)
			y--;
		srcheight-=(y+srcheight-1)-framebuffer.Bounds().IntegerHeight();
	}

	if(x+srcwidth>framebuffer.Bounds().IntegerWidth())
	{
		if(x>pt.x)
			x--;
		srcwidth-=(x+srcwidth-1)-framebuffer.Bounds().IntegerWidth();
	}
	
	if(x<0)
	{
		if(x<pt.x)
			x++;
		srcbuffer+=(0-x)>>3;
		srcwidth-=0-x;
		destbuffer+=(0-x)*4;
	}
	
	// starting point in destination bitmap
	destbuffer=(uint8*)framebuffer.Bits()+int32( (pt.y*framebuffer.BytesPerRow())+(pt.x*4) );

	srcindex=srcbuffer;
	destindex=destbuffer;

	for(i=0; i<srcheight; i++)
	{
		rowptr=destindex;		

		for(j=0;j<srcwidth;j++)
		{
			for(k=0; k<8; k++)
			{
				value=*(srcindex+j) & (1 << (7-k));
				if(value)
				{
					rowptr[0]=color.blue;
					rowptr[1]=color.green;
					rowptr[2]=color.red;
					rowptr[3]=color.alpha;
				}

				rowptr+=4;
			}

		}
		
		srcindex+=srcinc;
		destindex+=destinc;
	}
	ReleaseBuffer();
}

void
DisplayDriverImpl::BlitGray2RGB32(FT_Bitmap *src, const BPoint &pt, const DrawData *d)
{
	FBBitmap framebuffer;
	if(!AcquireBuffer(&framebuffer)) {
		printf("Couldn't acquire framebuffer in DisplayDriverImpl::BlitGray2RGB32\n");
		return;
	}
	
	rgb_color highcolor = d->highcolor.GetColor32();
	rgb_color lowcolor = d->lowcolor.GetColor32();
	
	float rstep, gstep, bstep, astep;
	rstep = float(highcolor.red - lowcolor.red) / 255.0;
	gstep = float(highcolor.green - lowcolor.green) / 255.0;
	bstep = float(highcolor.blue - lowcolor.blue) / 255.0;
	astep = float(highcolor.alpha - lowcolor.alpha) / 255.0;
	
	// increment values for the index pointers
	int32 x = (int32)pt.x;
	int32 y = (int32)pt.y;
	int32 srcinc = src->pitch;
	int32 destinc = framebuffer.BytesPerRow();
	int32 srcwidth = src->width;
	int32 srcheight = src->rows;
	int32 incval = 0;
	
	// pointers to the top left corner of the area to be copied in each bitmap
	uint8 *srcbuffer = (uint8 *)src->buffer;
	uint8 *destbuffer= (uint8 *)framebuffer.Bits() + (y * framebuffer.BytesPerRow() + (x * 4));
	
	if (y < 0) {
		if (y < pt.y)
			y++;
		
		incval = 0 - y;
		
		srcbuffer += incval * srcinc;
		srcheight -= incval;
		destbuffer += incval * destinc;
	}
	
	if (y + srcheight > framebuffer.Bounds().IntegerHeight()) {
		if(y > pt.y)
			y--;
		srcheight -= (y + srcheight - 1) - framebuffer.Bounds().IntegerHeight();
	}
	
	if (x + srcwidth > framebuffer.Bounds().IntegerWidth()) {
		if(x > pt.x)
			x--;
		srcwidth -= (x + srcwidth - 1) - framebuffer.Bounds().IntegerWidth();
	}
	
	if (x < 0) {
		if(x < pt.x)
			x++;
		incval = 0 - x;
		srcbuffer += incval;
		srcwidth -= incval;
		destbuffer += incval * 4;
	}
	
	// index pointers which are incremented during the course of the blit
	uint8 *srcindex = srcbuffer;
	uint8 *destindex = destbuffer;
	uint8 *rowptr = NULL;
	int32 value;
	
	for (int32 i = 0; i < srcheight; i++) {
		rowptr = destindex;
		
		for(int32 j = 0; j < srcwidth; j++) {
			value = *(srcindex + j) ^ 255;
			
			if (value != 255) {
				if(d->draw_mode == B_OP_COPY) {
					rowptr[0] = uint8(highcolor.blue - (value * bstep));
					rowptr[1] = uint8(highcolor.green - (value * gstep));
					rowptr[2] = uint8(highcolor.red - (value * rstep));
					rowptr[3] = 255;
				} else if (d->draw_mode == B_OP_OVER) {
					if(highcolor.alpha>127) {
						rowptr[0] = uint8(highcolor.blue - (value * (float(highcolor.blue - rowptr[0]) / 255.0)));
						rowptr[1] = uint8(highcolor.green - (value * (float(highcolor.green - rowptr[1]) / 255.0)));
						rowptr[2] = uint8(highcolor.red - (value * (float(highcolor.red - rowptr[2]) / 255.0)));
						rowptr[3] = 255;
					}
				}
			}
			rowptr += 4;
		}
		
		srcindex += srcinc;
		destindex += destinc;
	}
	
	ReleaseBuffer();
}

bool DisplayDriverImpl::AcquireBuffer(FBBitmap *bmp)
{
	return false;
}

void DisplayDriverImpl::ReleaseBuffer()
{
}

void DisplayDriverImpl::Blit(const BRect &src, const BRect &dest, const DrawData *d)
{
}

void DisplayDriverImpl::FillSolidRect(const BRect &rect, const RGBColor &color)
{
}

void DisplayDriverImpl::FillPatternRect(const BRect &rect, const DrawData *d)
{
}

/* Draws a line with pensize 1.  Coordinates are guarenteed to be in bounds */
void DisplayDriverImpl::StrokeSolidLine(int32 x1, int32 y1, int32 x2, int32 y2, const RGBColor &color)
{
}

// Draws a line with pensize 1.  Coordinates are guarenteed to be in bounds
void DisplayDriverImpl::StrokePatternLine(int32 x1, int32 y1, int32 x2, int32 y2, const DrawData *d)
{
}

void DisplayDriverImpl::StrokeSolidRect(const BRect &rect, const RGBColor &color)
{
}

void DisplayDriverImpl::CopyBitmap(ServerBitmap *bitmap, const BRect &source, const BRect &dest, const DrawData *d)
{
}

void DisplayDriverImpl::CopyToBitmap(ServerBitmap *target, const BRect &source)
{
}

void DisplayDriverImpl::Invalidate(const BRect &r)
{
}

void DisplayDriverImpl::ConstrainClippingRegion(BRegion *reg)
{
}
