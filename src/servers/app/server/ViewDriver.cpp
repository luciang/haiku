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
//	File Name:		ViewDriver.cpp
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	BView/BWindow combination graphics module
//  
//------------------------------------------------------------------------------

#include <stdio.h>
#include <iostream.h>
#include <Message.h>
#include <Region.h>
#include <Bitmap.h>
#include <OS.h>
#include <GraphicsDefs.h>
#include <Font.h>

#include "Angle.h"
#include "PortLink.h"
#include "ServerProtocol.h"
#include "ServerBitmap.h"
#include "ViewDriver.h"
#include "ServerConfig.h"
#include "ServerCursor.h"
#include "ServerFont.h"
#include "FontFamily.h"
#include "LayerData.h"

enum
{
VDWIN_CLEAR=100,

VDWIN_SHOWCURSOR,
VDWIN_HIDECURSOR,
VDWIN_OBSCURECURSOR,
VDWIN_MOVECURSOR,
VDWIN_SETCURSOR,
};

ViewDriver *viewdriver_global;


VDView::VDView(BRect bounds)
	: BView(bounds,"viewdriver_view",B_FOLLOW_ALL, B_WILL_DRAW)
{
	SetViewColor(B_TRANSPARENT_32_BIT);
	viewbmp=new BBitmap(bounds,B_CMAP8,true);

	// This link for sending mouse messages to the OBAppServer.
	// This is only to take the place of the Input Server. I suppose I could write
	// an addon filter to be more like the Input Server, but then I wouldn't be working
	// on this thing! :P
	serverlink=new PortLink(find_port(SERVER_INPUT_PORT));

	// Create a cursor which isn't just a box
	cursor=new BBitmap(BRect(0,0,20,20),B_CMAP8,true);
	BView *v=new BView(cursor->Bounds(),"v", B_FOLLOW_NONE, B_WILL_DRAW);
	hide_cursor=0;

	cursor->Lock();
	cursor->AddChild(v);

	v->SetHighColor(255,255,255,0);
	v->FillRect(cursor->Bounds());
	v->SetHighColor(255,0,0,255);
	v->FillTriangle(cursor->Bounds().LeftTop(),cursor->Bounds().RightTop(),cursor->Bounds().LeftBottom());

	cursor->RemoveChild(v);
	cursor->Unlock();
	
	cursorframe=cursor->Bounds();
	oldcursorframe=cursor->Bounds();
}

VDView::~VDView(void)
{
	delete serverlink;
	delete viewbmp;
	delete cursor;
}

void VDView::AttachedToWindow(void)
{
}

void VDView::Draw(BRect rect)
{
	if(viewbmp)
	{
		DrawBitmapAsync(viewbmp,oldcursorframe,oldcursorframe);
		DrawBitmapAsync(viewbmp,rect,rect);
	
		if(hide_cursor==0 && obscure_cursor==false)
		{
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmapAsync(cursor,cursor->Bounds(),cursorframe);
			SetDrawingMode(B_OP_COPY);
		}
		Sync();
	}
}

// These functions emulate the Input Server by sending the *exact* same kind of messages
// to the server's port. Being we're using a regular window, it would make little sense
// to do anything else.

void VDView::MouseDown(BPoint pt)
{
	// Attach data:
	// 1) int64 - time of mouse click
	// 2) float - x coordinate of mouse click
	// 3) float - y coordinate of mouse click
	// 4) int32 - modifier keys down
	// 5) int32 - buttons down
	// 6) int32 - clicks
#ifdef ENABLE_INPUT_SERVER_EMULATION
	BPoint p;

	uint32 buttons,
		mod=modifiers(),
		clicks=1;		// can't get the # of clicks without a *lot* of extra work :(

	int64 time=(int64)real_time_clock();

	GetMouse(&p,&buttons);

	serverlink->SetOpCode(B_MOUSE_DOWN);
	serverlink->Attach(&time, sizeof(int64));
	serverlink->Attach(&pt.x,sizeof(float));
	serverlink->Attach(&pt.y,sizeof(float));
	serverlink->Attach(&mod, sizeof(uint32));
	serverlink->Attach(&buttons, sizeof(uint32));
	serverlink->Attach(&clicks, sizeof(uint32));
	serverlink->Flush();
#endif
}

void VDView::MouseMoved(BPoint pt, uint32 transit, const BMessage *msg)
{
	// Attach data:
	// 1) int64 - time of mouse click
	// 2) float - x coordinate of mouse click
	// 3) float - y coordinate of mouse click
	// 4) int32 - buttons down
#ifdef ENABLE_INPUT_SERVER_EMULATION
	BPoint p;
	uint32 buttons;
	int64 time=(int64)real_time_clock();

	serverlink->SetOpCode(B_MOUSE_MOVED);
	serverlink->Attach(&time,sizeof(int64));
	serverlink->Attach(&pt.x,sizeof(float));
	serverlink->Attach(&pt.y,sizeof(float));
	GetMouse(&p,&buttons);
	serverlink->Attach(&buttons,sizeof(int32));
	serverlink->Flush();
#endif
}

void VDView::MouseUp(BPoint pt)
{
	// Attach data:
	// 1) int64 - time of mouse click
	// 2) float - x coordinate of mouse click
	// 3) float - y coordinate of mouse click
	// 4) int32 - modifier keys down
#ifdef ENABLE_INPUT_SERVER_EMULATION
	BPoint p;

	uint32 buttons,
		mod=modifiers();

	int64 time=(int64)real_time_clock();

	GetMouse(&p,&buttons);

	serverlink->SetOpCode(B_MOUSE_UP);
	serverlink->Attach(&time, sizeof(int64));
	serverlink->Attach(&pt.x,sizeof(float));
	serverlink->Attach(&pt.y,sizeof(float));
	serverlink->Attach(&mod, sizeof(uint32));
	serverlink->Flush();
#endif
}

VDWindow::VDWindow(void)
	: BWindow(BRect(100,60,740,540),"OpenBeOS App Server",B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
{
	view=new VDView(Bounds());
	AddChild(view);
}

VDWindow::~VDWindow(void)
{
}

void VDWindow::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case VDWIN_SHOWCURSOR:
		{
			if(view->hide_cursor>0)
				view->hide_cursor--;

			if(view->hide_cursor==0)
				view->Invalidate(view->cursorframe);

			break;
		}
		case VDWIN_HIDECURSOR:
		{
			view->hide_cursor++;
			if(view->hide_cursor==1)
				view->Invalidate(view->cursorframe);
			break;
		}
		case VDWIN_OBSCURECURSOR:
		{
			view->obscure_cursor=true;
			view->Invalidate(view->cursorframe);
			break;
		}
		case VDWIN_MOVECURSOR:
		{
			float x,y;
			msg->FindFloat("x",&x);
			msg->FindFloat("y",&y);

			// this was changed because an extra message was
			// sent even though the mouse was never moved
			if(view->cursorframe.left!=x || view->cursorframe.top!=y)
			{
				if(view->obscure_cursor)
					view->obscure_cursor=false;
	
				view->oldcursorframe=view->cursorframe;
				view->cursorframe.OffsetTo(x,y);
			}

			if(view->hide_cursor==0)
				view->Invalidate(view->oldcursorframe);
			break;
		}
		case VDWIN_SETCURSOR:
		{
			ServerBitmap *cdata;
			msg->FindPointer("SCursor",(void**)&cdata);

			if(cdata!=NULL)
			{
				BBitmap *bmp=new BBitmap(cdata->Bounds(), B_RGBA32);

				// Copy the server bitmap in the cursor to a BBitmap
				uint8	*sbmppos=(uint8*)cdata->Bits(),
						*bbmppos=(uint8*)bmp->Bits();

				int32 bytes=cdata->BytesPerRow(),
					bbytes=bmp->BytesPerRow();

				for(int i=0;i<cdata->Bounds().IntegerHeight();i++)
					memcpy(bbmppos+(i*bbytes), sbmppos+(i*bytes), bytes);

				// Replace the bitmap
				delete view->cursor;
				view->cursor=bmp;
				view->Invalidate(view->cursorframe);
				break;
			}
			break;
		}
		default:
			BWindow::MessageReceived(msg);
			break;
	}
}

bool VDWindow::QuitRequested(void)
{
	port_id serverport=find_port(SERVER_PORT_NAME);

	if(serverport!=B_NAME_NOT_FOUND)
		write_port(serverport,B_QUIT_REQUESTED,NULL,0);

	return true;
}

void VDWindow::WindowActivated(bool active)
{
	// This is just to hide the regular system cursor so we can see our own
	if(active)
		be_app->HideCursor();
	else
		be_app->ShowCursor();
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

ViewDriver::ViewDriver(void)
{
	viewdriver_global=this;
	screenwin=new VDWindow();
	framebuffer=screenwin->view->viewbmp;
	serverlink=screenwin->view->serverlink;
	hide_cursor=0;
	_SetWidth(640);
	_SetHeight(480);
	_SetDepth(8);
	_SetMode(B_8_BIT_640x480);
}

ViewDriver::~ViewDriver(void)
{
	if(is_initialized)
	{
		screenwin->Lock();
		screenwin->Quit();
		screenwin=NULL;
	}
}

bool ViewDriver::Initialize(void)
{
	_Lock();
	drawview=new BView(framebuffer->Bounds(),"drawview",B_FOLLOW_ALL, B_WILL_DRAW);
	framebuffer->AddChild(drawview);

	hide_cursor=0;
	obscure_cursor=false;

	is_initialized=true;

	// We can afford to call the above functions without locking
	// because the window is locked until Show() is first called
	screenwin->Show();
	_Unlock();
	return true;
}

void ViewDriver::Shutdown(void)
{
	_Lock();
	is_initialized=false;
	_Unlock();
}

void ViewDriver::SetMode(int32 space)
{
	screenwin->Lock();
	int16 w=640,h=480;
	color_space s=B_CMAP8;

	switch(space)
	{
		case B_32_BIT_800x600:
		case B_16_BIT_800x600:
		case B_8_BIT_800x600:
		{
			w=800; h=600;
			break;
		}
		case B_32_BIT_1024x768:
		case B_16_BIT_1024x768:
		case B_8_BIT_1024x768:
		{
			w=1024; h=768;
			break;
		}
		default:
			break;
	}
	screenwin->ResizeTo(w-1,h-1);

	switch(space)
	{
		case B_32_BIT_640x480:
		case B_32_BIT_800x600:
		case B_32_BIT_1024x768:
			s=B_RGBA32;
			_SetDepth(32);
			break;
		case B_16_BIT_640x480:
		case B_16_BIT_800x600:
		case B_16_BIT_1024x768:
			s=B_RGBA15;
			_SetDepth(15);
			break;
		case B_8_BIT_640x480:
		case B_8_BIT_800x600:
		case B_8_BIT_1024x768:
			s=B_CMAP8;
			_SetDepth(8);
			break;
		default:
			_SetDepth(8);
			break;
	}
	
	
	delete framebuffer;

	// don't forget to update the internal vars!
	_SetWidth(w);
	_SetHeight(h);
	_SetMode(space);

	screenwin->view->viewbmp=new BBitmap(BRect(0,0,w-1,h-1),s,true);
	framebuffer=screenwin->view->viewbmp;
	drawview=new BView(framebuffer->Bounds(),"drawview",B_FOLLOW_ALL, B_WILL_DRAW);
	framebuffer->AddChild(drawview);

	framebuffer->Lock();
	drawview->SetHighColor(80,85,152);
	drawview->FillRect(drawview->Bounds());
	drawview->Sync();
	framebuffer->Unlock();

	_SetBytesPerRow(framebuffer->BytesPerRow());
	screenwin->view->Invalidate();
	screenwin->Unlock();
}

void ViewDriver::CopyBits(BRect src, BRect dest)
{
	screenwin->Lock();
	framebuffer->Lock();
	drawview->CopyBits(src,dest);
	drawview->Sync();
	screenwin->view->Invalidate(src);
	screenwin->view->Invalidate(dest);
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::DrawBitmap(ServerBitmap *bitmap, BRect src, BRect dest)
{
#ifdef DEBUG_DRIVER_MODULE
printf("ViewDriver:: DrawBitmap unimplemented()\n");
#endif
}

void ViewDriver::DrawChar(char c, BPoint pt, LayerData *d)
{
	char str[2];
	str[0]=c;
	str[1]='\0';
	DrawString(str, 1, pt, d);
}
/*
void ViewDriver::DrawString(const char *string, int32 length, BPoint pt, LayerData *d, escapement_delta *delta=NULL)
{
#ifdef DEBUG_DRIVER_MODULE
printf("ViewDriver:: DrawString(\"%s\",%ld,BPoint(%f,%f))\n",string,length,pt.x,pt.y);
#endif
	if(!d)
		return;
	BRect r;
	
	screenwin->Lock();
	framebuffer->Lock();
	
	SetLayerData(d,true);	// set all layer data and additionally set the font-related data

	drawview->DrawString(string,length,pt,delta);
	drawview->Sync();
	
	// calculate the invalid rectangle
	font_height fh;
	BFont font;
	drawview->GetFont(&font);
	drawview->GetFontHeight(&fh);
	r.left=pt.x;
	r.right=pt.x+font.StringWidth(string);
	r.top=pt.y-fh.ascent;
	r.bottom=pt.y+fh.descent;
	screenwin->view->Invalidate(r);
	
	framebuffer->Unlock();
	screenwin->Unlock();
	
}
*/
/*
bool ViewDriver::DumpToFile(const char *path)
{
	return false;
}
*/
void ViewDriver::FillArc(BRect r, float angle, float span, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->FillArc(r,angle,span,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::FillBezier(BPoint *pts, LayerData *d, int8 *pat)
{
	if(!pat || !pts)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->FillBezier(pts,*((pattern*)pat));
	drawview->Sync();
	
	// Invalidate the whole view until I get around to adding in the invalid rect calc code
	screenwin->view->Invalidate();
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::FillEllipse(BRect r, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->FillEllipse(r,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::FillPolygon(BPoint *ptlist, int32 numpts, BRect rect, LayerData *d, int8 *pat)
{
#ifdef DEBUG_DRIVER_MODULE
printf("ViewDriver:: FillPolygon unimplemented\n");
#endif
	if(!pat || !ptlist)
		return;
}

void ViewDriver::FillRect(BRect r, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->FillRect(r,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::FillRoundRect(BRect r, float xrad, float yrad, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->FillRoundRect(r,xrad,yrad,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::FillTriangle(BPoint *pts, BRect r, LayerData *d, int8 *pat)
{
	if(!pat || !pts)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	BPoint first=pts[0],second=pts[1],third=pts[2];
	SetLayerData(d);
	drawview->FillTriangle(first,second,third,r,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::HideCursor(void)
{
	screenwin->Lock();
	_Lock();

	hide_cursor++;
	screenwin->PostMessage(VDWIN_HIDECURSOR);

	_Unlock();
	screenwin->Unlock();
}

void ViewDriver::InvertRect(BRect r)
{
	screenwin->Lock();
	framebuffer->Lock();
	drawview->InvertRect(r);
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();
}

bool ViewDriver::IsCursorHidden(void)
{
	screenwin->Lock();
	bool value=(hide_cursor>0)?true:false;
	screenwin->Unlock();
	return value;
}

void ViewDriver::ObscureCursor(void)
{
	screenwin->Lock();
	screenwin->PostMessage(VDWIN_OBSCURECURSOR);
	screenwin->Unlock();
}

void ViewDriver::MoveCursorTo(float x, float y)
{
	screenwin->Lock();
	BMessage *msg=new BMessage(VDWIN_MOVECURSOR);
	msg->AddFloat("x",x);
	msg->AddFloat("y",y);
	screenwin->PostMessage(msg);
	screenwin->Unlock();
}

void ViewDriver::SetCursor(ServerCursor *cursor)
{
	if(cursor!=NULL)
	{
		screenwin->Lock();
		BBitmap *bmp=new BBitmap(cursor->Bounds(),B_RGBA32);
	
		// Copy the server bitmap in the cursor to a BBitmap
		uint8	*sbmppos=(uint8*)cursor->Bits(),
				*bbmppos=(uint8*)bmp->Bits();
	
		int32 bytes=cursor->BytesPerRow(),
			bbytes=bmp->BytesPerRow();
	
		for(int i=0;i<=cursor->Bounds().IntegerHeight();i++)
			memcpy(bbmppos+(i*bbytes), sbmppos+(i*bytes), bytes);

		// Replace the bitmap
		delete screenwin->view->cursor;
		screenwin->view->cursor=bmp;
		screenwin->view->Invalidate(screenwin->view->cursorframe);
		screenwin->Unlock();
	}
}

void ViewDriver::ShowCursor(void)
{
	screenwin->Lock();
	if(hide_cursor>0)
	{
		hide_cursor--;
		screenwin->PostMessage(VDWIN_SHOWCURSOR);
	}
	screenwin->Unlock();

}

void ViewDriver::StrokeArc(BRect r, float angle, float span, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->StrokeArc(r,angle,span,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::StrokeBezier(BPoint *pts, LayerData *d, int8 *pat)
{
	if(!pat || !pts)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->StrokeBezier(pts,*((pattern*)pat));
	drawview->Sync();
	
	// Invalidate the whole view until I get around to adding in the invalid rect calc code
	screenwin->view->Invalidate();
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::StrokeEllipse(BRect r, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->StrokeEllipse(r,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::StrokeLine(BPoint start, BPoint end, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->StrokeLine(start,end,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(BRect(start,end));
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::StrokeLineArray(BPoint *pts, int32 numlines, RGBColor *colors, LayerData *d)
{
#ifdef DEBUG_DRIVER_MODULE
printf("ViewDriver:: StrokeLineArray unimplemented\n");
#endif
}

void ViewDriver::StrokePolygon(BPoint *ptlist, int32 numpts, BRect rect, LayerData *d, int8 *pat, bool is_closed=true)
{
	if(!pat || !ptlist)
		return;
	screenwin->Lock();
	framebuffer->Lock();

	BRegion invalid;

	SetLayerData(d);
	drawview->BeginLineArray(numpts+2);
	for(int i=1;i<numpts;i++)
	{
		drawview->AddLine(ptlist[i-1],ptlist[i],d->highcolor.GetColor32());
		invalid.Include(BRect(ptlist[i-1],ptlist[i]));
	}

	if(is_closed)
	{
		drawview->AddLine(ptlist[numpts-1],ptlist[0],d->highcolor.GetColor32());
		invalid.Include(BRect(ptlist[numpts-1],ptlist[0]));
	}
	drawview->EndLineArray();

	drawview->Sync();
	screenwin->view->Invalidate(invalid.Frame());
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::StrokeRect(BRect r, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->StrokeRect(r,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();
}

void ViewDriver::StrokeRoundRect(BRect r, float xrad, float yrad, LayerData *d, int8 *pat)
{
	if(!pat || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	SetLayerData(d);
	drawview->StrokeRoundRect(r,xrad,yrad,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::StrokeTriangle(BPoint *pts, BRect r, LayerData *d, int8 *pat)
{
	if(!pat || !pts || !d)
		return;
	screenwin->Lock();
	framebuffer->Lock();
	BPoint first=pts[0],second=pts[1],third=pts[2];
	SetLayerData(d);
	drawview->StrokeTriangle(first,second,third,r,*((pattern*)pat));
	drawview->Sync();
	screenwin->view->Invalidate(r);
	framebuffer->Unlock();
	screenwin->Unlock();

}

void ViewDriver::SetLayerData(LayerData *d, bool set_font_data=false)
{
	if(!d)
		return;

	drawview->SetPenSize(d->pensize);
	drawview->SetDrawingMode(d->draw_mode);
	drawview->SetHighColor(d->highcolor.GetColor32());
	drawview->SetLowColor(d->lowcolor.GetColor32());
	drawview->SetScale(d->scale);
	drawview->MovePenTo(d->penlocation);
	if(set_font_data)
	{
		BFont font;
		ServerFont *sf=d->font;

		if(!sf)
			return;

		FontStyle *style=d->font->Style();

		if(!style)
			return;
		
		FontFamily *family=(FontFamily *)style->Family();
		if(!family)
			return;
		
		font.SetFamilyAndStyle((font_family)family->Name(),style->Name());
		font.SetFlags(sf->Flags());
		font.SetEncoding(sf->Encoding());
		font.SetSize(sf->Size());
		font.SetRotation(sf->Rotation());
		font.SetShear(sf->Shear());
		font.SetSpacing(sf->Spacing());
		drawview->SetFont(&font);
	}
}

float ViewDriver::StringWidth(const char *string, int32 length, LayerData *d)
{
	if(!string || !d || !d->font)
		return 0.0;
	screenwin->Lock();

	ServerFont *font=d->font;
	FontStyle *style=font->Style();

	if(!style)
		return 0.0;

	FT_Face face;
	FT_GlyphSlot slot;
	FT_UInt glyph_index, previous=0;
	FT_Vector pen,delta;
	int16 error=0;
	int32 strlength,i;
	float returnval;

	error=FT_New_Face(ftlib, style->GetPath(), 0, &face);
	if(error)
		return 0.0;

	slot=face->glyph;

	bool use_kerning=FT_HAS_KERNING(face) && font->Spacing()==B_STRING_SPACING;
	
	error=FT_Set_Char_Size(face, 0,int32(font->Size())*64,72,72);
	if(error)
		return 0.0;

	// set the pen position in 26.6 cartesian space coordinates
	pen.x=0;
	
	slot=face->glyph;
	
	strlength=strlen(string);
	if(length<strlength)
		strlength=length;

	for(i=0;i<strlength;i++)
	{
		// get kerning and move pen
		if(use_kerning && previous && glyph_index)
		{
			FT_Get_Kerning(face, previous, glyph_index,ft_kerning_default, &delta);
			pen.x+=delta.x;
		}

		error=FT_Load_Char(face,string[i],FT_LOAD_MONOCHROME);

		// increment pen position
		pen.x+=slot->advance.x;
		previous=glyph_index;
	}
	screenwin->Unlock();

	FT_Done_Face(face);

	returnval=pen.x>>6;
	return returnval;
}

float ViewDriver::StringHeight(const char *string, int32 length, LayerData *d)
{
	if(!string || !d || !d->font)
		return 0.0;
	screenwin->Lock();

	ServerFont *font=d->font;
	FontStyle *style=font->Style();

	if(!style)
		return 0.0;

	FT_Face face;
	FT_GlyphSlot slot;
	int16 error=0;
	int32 strlength,i;
	float returnval=0.0,ascent=0.0,descent=0.0;

	error=FT_New_Face(ftlib, style->GetPath(), 0, &face);
	if(error)
		return 0.0;

	slot=face->glyph;
	
	error=FT_Set_Char_Size(face, 0,int32(font->Size())*64,72,72);
	if(error)
		return 0.0;

	slot=face->glyph;
	
	strlength=strlen(string);
	if(length<strlength)
		strlength=length;

	for(i=0;i<strlength;i++)
	{
		FT_Load_Char(face,string[i],FT_LOAD_RENDER);
		if(slot->metrics.horiBearingY<slot->metrics.height)
			descent=MAX((slot->metrics.height-slot->metrics.horiBearingY)>>6,descent);
		else
			ascent=MAX(slot->bitmap.rows,ascent);
	}
	screenwin->Unlock();

	FT_Done_Face(face);

	returnval=ascent+descent;
	return returnval;
}

void ViewDriver::DrawString(const char *string, int32 length, BPoint pt, LayerData *d, escapement_delta *edelta=NULL)
{
	if(!string || !d || !d->font)
		return;
	screenwin->Lock();

	pt.y--;	// because of Be's backward compatibility hack

	ServerFont *font=d->font;
	FontStyle *style=font->Style();

	if(!style)
		return;

	FT_Face face;
	FT_GlyphSlot slot;
	FT_Matrix rmatrix,smatrix;
	FT_UInt glyph_index, previous=0;
	FT_Vector pen,delta,space,nonspace;
	int16 error=0;
	int32 strlength,i;
	Angle rotation(font->Rotation()), shear(font->Shear());

	bool antialias=( (font->Size()<18 && font->Flags()& B_DISABLE_ANTIALIASING==0)
		|| font->Flags()& B_FORCE_ANTIALIASING)?true:false;

	// Originally, I thought to do this shear checking here, but it really should be
	// done in BFont::SetShear()
	float shearangle=shear.Value();
	if(shearangle>135)
		shearangle=135;
	if(shearangle<45)
		shearangle=45;

	if(shearangle>90)
		shear=90+((180-shearangle)*2);
	else
		shear=90-(90-shearangle)*2;
	
	error=FT_New_Face(ftlib, style->GetPath(), 0, &face);
	if(error)
		return;

	slot=face->glyph;

	bool use_kerning=FT_HAS_KERNING(face) && font->Spacing()==B_STRING_SPACING;
	
	error=FT_Set_Char_Size(face, 0,int32(font->Size())*64,72,72);
	if(error)
		return;

	// if we do any transformation, we do a call to FT_Set_Transform() here
	
	// First, rotate
	rmatrix.xx = (FT_Fixed)( rotation.Cosine()*0x10000); 
	rmatrix.xy = (FT_Fixed)(-rotation.Sine()*0x10000); 
	rmatrix.yx = (FT_Fixed)( rotation.Sine()*0x10000); 
	rmatrix.yy = (FT_Fixed)( rotation.Cosine()*0x10000); 
	
	// Next, shear
	smatrix.xx = (FT_Fixed)(0x10000); 
	smatrix.xy = (FT_Fixed)(-shear.Cosine()*0x10000); 
	smatrix.yx = (FT_Fixed)(0); 
	smatrix.yy = (FT_Fixed)(0x10000); 

	FT_Matrix_Multiply(&rmatrix,&smatrix);
	
	// Set up the increment value for escapement padding
	space.x=int32(d->edelta.space * rotation.Cosine()*64);
	space.y=int32(d->edelta.space * rotation.Sine()*64);
	nonspace.x=int32(d->edelta.nonspace * rotation.Cosine()*64);
	nonspace.y=int32(d->edelta.nonspace * rotation.Sine()*64);
	
	// set the pen position in 26.6 cartesian space coordinates
	pen.x=(int32)pt.x * 64;
	pen.y=(int32)pt.y * 64;
	
	slot=face->glyph;

	
	strlength=strlen(string);
	if(length<strlength)
		strlength=length;

	for(i=0;i<strlength;i++)
	{
		FT_Set_Transform(face,&smatrix,&pen);

		// Handle escapement padding option
		if((uint8)string[i]<=0x20)
		{
			pen.x+=space.x;
			pen.y+=space.y;
		}
		else
		{
			pen.x+=nonspace.x;
			pen.y+=nonspace.y;
		}

	
		// get kerning and move pen
		if(use_kerning && previous && glyph_index)
		{
			FT_Get_Kerning(face, previous, glyph_index,ft_kerning_default, &delta);
			pen.x+=delta.x;
			pen.y+=delta.y;
		}

		error=FT_Load_Char(face,string[i],
			((antialias)?FT_LOAD_RENDER:FT_LOAD_RENDER | FT_LOAD_MONOCHROME) );

		if(!error)
		{
			if(antialias)
				BlitGray2RGB32(&slot->bitmap,
					BPoint(slot->bitmap_left,pt.y-(slot->bitmap_top-pt.y)), d);
			else
				BlitMono2RGB32(&slot->bitmap,
					BPoint(slot->bitmap_left,pt.y-(slot->bitmap_top-pt.y)), d);
		}

		// increment pen position
		pen.x+=slot->advance.x;
		pen.y+=slot->advance.y;
		previous=glyph_index;
	}

	// TODO: implement properly
	// calculate the invalid rectangle
	BRect r;
	r.left=MIN(pt.x,pen.x>>6);
	r.right=MAX(pt.x,pen.x>>6);
	r.top=pt.y-face->height;
	r.bottom=pt.y+face->height;

	screenwin->view->Invalidate(r);
	screenwin->Unlock();

	FT_Done_Face(face);
}

void ViewDriver::BlitMono2RGB32(FT_Bitmap *src, BPoint pt, LayerData *d)
{
	rgb_color color=d->highcolor.GetColor32();
	
	// pointers to the top left corner of the area to be copied in each bitmap
	uint8 *srcbuffer, *destbuffer;
	
	// index pointers which are incremented during the course of the blit
	uint8 *srcindex, *destindex, *rowptr, value;
	
	// increment values for the index pointers
	int32 srcinc=src->pitch, destinc=framebuffer->BytesPerRow();
	
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

	if(y+srcheight>framebuffer->Bounds().IntegerHeight())
	{
		if(y>pt.y)
			y--;
		srcheight-=(y+srcheight-1)-framebuffer->Bounds().IntegerHeight();
	}

	if(x+srcwidth>framebuffer->Bounds().IntegerWidth())
	{
		if(x>pt.x)
			x--;
		srcwidth-=(x+srcwidth-1)-framebuffer->Bounds().IntegerWidth();
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
	destbuffer=(uint8*)framebuffer->Bits()+int32( (pt.y*framebuffer->BytesPerRow())+(pt.x*4) );

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

}

void ViewDriver::BlitGray2RGB32(FT_Bitmap *src, BPoint pt, LayerData *d)
{
	// pointers to the top left corner of the area to be copied in each bitmap
	uint8 *srcbuffer=NULL, *destbuffer=NULL;
	
	// index pointers which are incremented during the course of the blit
	uint8 *srcindex=NULL, *destindex=NULL, *rowptr=NULL;
	
	rgb_color highcolor=d->highcolor.GetColor32(), lowcolor=d->lowcolor.GetColor32();	float rstep,gstep,bstep,astep;

	rstep=float(highcolor.red-lowcolor.red)/255.0;
	gstep=float(highcolor.green-lowcolor.green)/255.0;
	bstep=float(highcolor.blue-lowcolor.blue)/255.0;
	astep=float(highcolor.alpha-lowcolor.alpha)/255.0;
	
	// increment values for the index pointers
	int32 x=(int32)pt.x,
		y=(int32)pt.y,
		srcinc=src->pitch,
//		destinc=dest->BytesPerRow(),
		destinc=framebuffer->BytesPerRow(),
		srcwidth=src->width,
		srcheight=src->rows,
		incval=0;
	
	int16 i,j;
	
	// starting point in source bitmap
	srcbuffer=(uint8*)src->buffer;

	// starting point in destination bitmap
//	destbuffer=(uint8*)dest->Bits()+(y*dest->BytesPerRow()+(x*4));
	destbuffer=(uint8*)framebuffer->Bits()+(y*framebuffer->BytesPerRow()+(x*4));


	if(y<0)
	{
		if(y<pt.y)
			y++;
		
		incval=0-y;
		
		srcbuffer+=incval * srcinc;
		srcheight-=incval;
		destbuffer+=incval * destinc;
	}

	if(y+srcheight>framebuffer->Bounds().IntegerHeight())
	{
		if(y>pt.y)
			y--;
		srcheight-=(y+srcheight-1)-framebuffer->Bounds().IntegerHeight();
	}

	if(x+srcwidth>framebuffer->Bounds().IntegerWidth())
	{
		if(x>pt.x)
			x--;
		srcwidth-=(x+srcwidth-1)-framebuffer->Bounds().IntegerWidth();
	}
	
	if(x<0)
	{
		if(x<pt.x)
			x++;
		incval=0-x;
		srcbuffer+=incval;
		srcwidth-=incval;
		destbuffer+=incval*4;
	}

	int32 value;

	srcindex=srcbuffer;
	destindex=destbuffer;

	for(i=0; i<srcheight; i++)
	{
		rowptr=destindex;		

		for(j=0;j<srcwidth;j++)
		{
			value=*(srcindex+j) ^ 255;

			if(value!=255)
			{
				if(d->draw_mode==B_OP_COPY)
				{
					rowptr[0]=uint8(highcolor.blue-(value*bstep));
					rowptr[1]=uint8(highcolor.green-(value*gstep));
					rowptr[2]=uint8(highcolor.red-(value*rstep));
					rowptr[3]=255;
				}
				else
					if(d->draw_mode==B_OP_OVER)
					{
						if(highcolor.alpha>127)
						{
							rowptr[0]=uint8(highcolor.blue-(value*(float(highcolor.blue-rowptr[0])/255.0)));
							rowptr[1]=uint8(highcolor.green-(value*(float(highcolor.green-rowptr[1])/255.0)));
							rowptr[2]=uint8(highcolor.red-(value*(float(highcolor.red-rowptr[2])/255.0)));
							rowptr[3]=255;
						}
					}
			}
			rowptr+=4;

		}
		
		srcindex+=srcinc;
		destindex+=destinc;
	}
}

rgb_color ViewDriver::GetBlitColor(rgb_color src, rgb_color dest, LayerData *d, bool use_high=true)
{
	rgb_color returncolor={0,0,0,0};
	int16 value;
	if(!d)
		return returncolor;
	
	switch(d->draw_mode)
	{
		case B_OP_COPY:
		{
			return src;
		}
		case B_OP_ADD:
		{
			value=src.red+dest.red;
			returncolor.red=(value>255)?255:value;

			value=src.green+dest.green;
			returncolor.green=(value>255)?255:value;

			value=src.blue+dest.blue;
			returncolor.blue=(value>255)?255:value;
			return returncolor;
		}
		case B_OP_SUBTRACT:
		{
			value=src.red-dest.red;
			returncolor.red=(value<0)?0:value;

			value=src.green-dest.green;
			returncolor.green=(value<0)?0:value;

			value=src.blue-dest.blue;
			returncolor.blue=(value<0)?0:value;
			return returncolor;
		}
		case B_OP_BLEND:
		{
			value=int16(src.red+dest.red)>>1;
			returncolor.red=value;

			value=int16(src.green+dest.green)>>1;
			returncolor.green=value;

			value=int16(src.blue+dest.blue)>>1;
			returncolor.blue=value;
			return returncolor;
		}
		case B_OP_MIN:
		{
			
			return ( uint16(src.red+src.blue+src.green) > 
				uint16(dest.red+dest.blue+dest.green) )?dest:src;
		}
		case B_OP_MAX:
		{
			return ( uint16(src.red+src.blue+src.green) < 
				uint16(dest.red+dest.blue+dest.green) )?dest:src;
		}
		case B_OP_OVER:
		{
			return (use_high && src.alpha>127)?src:dest;
		}
		case B_OP_INVERT:
		{
			returncolor.red=dest.red ^ 255;
			returncolor.green=dest.green ^ 255;
			returncolor.blue=dest.blue ^ 255;
			return (use_high && src.alpha>127)?returncolor:dest;
		}
		// This is a pain in the arse to implement, so I'm saving it for the real
		// server
		case B_OP_ALPHA:
		{
			return src;
		}
		case B_OP_ERASE:
		{
			// This one's tricky. 
			return (use_high && src.alpha>127)?d->lowcolor.GetColor32():dest;
		}
		case B_OP_SELECT:
		{
			// This one's tricky, too. We are passed a color in src. If it's the layer's
			// high color or low color, we check for a swap.
			if(d->highcolor==src)
				return (use_high && d->highcolor==dest)?d->lowcolor.GetColor32():dest;
			
			if(d->lowcolor==src)
				return (use_high && d->lowcolor==dest)?d->highcolor.GetColor32():dest;

			return dest;
		}
		default:
		{
			break;
		}
	}
	return returncolor;
}
