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
//	File Name:		WinBorder.h
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	Layer subclass which handles window management
//  
//------------------------------------------------------------------------------
#ifndef _WINBORDER_H_
#define _WINBORDER_H_

#include <Rect.h>
#include <String.h>
#include "Layer.h"

class ServerWindow;
class Decorator;
class DisplayDriver;

class WinBorder : public Layer
{
public:
	WinBorder(const BRect &r, const char *name, const int32 look, const int32 feel, 
		const int32 flags, ServerWindow *win);
	virtual	~WinBorder(void);
	virtual void RequestDraw(void);
	virtual void RequestDraw(const BRect &r);
//	void MoveBy(BPoint pt);
//	void MoveBy(float x, float y);
//	void ResizeBy(BPoint pt);
//	void ResizeBy(float x, float y);
	void MouseDown(int8 *buffer);
	void MouseMoved(int8 *buffer);
	void MouseUp(int8 *buffer);
	void UpdateColors(void);
	void UpdateDecorator(void);
	void UpdateFont(void);
	void UpdateScreen(void);
	void RebuildRegions(const bool &recursive=true);
	void SetFocus(const bool &active=false);
	ServerWindow *Window(void) { return _win; }
	Decorator *GetDecorator(void) { return _decorator; }
protected:
	ServerWindow *_win;
	BString *_title;
	Decorator *_decorator;
	int32 _flags;
	BRect _clientframe;
	int32 _mbuttons,_kmodifiers;
	BPoint _mousepos;
	bool _update;
	bool _hresizewin,_vresizewin;
	DisplayDriver *_driver;
};

bool is_moving_window(void);
void set_is_moving_window(bool state);
bool is_resizing_window(void);
void set_is_resizing_window(bool state);
void set_active_winborder(WinBorder *win);
WinBorder * get_active_winborder(void);
void set_is_resizing_window(bool state);
void set_is_sliding_tab(bool state);
bool is_sliding_tab(void);

#endif
