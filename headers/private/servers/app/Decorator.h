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
//	File Name:		Decorator.h
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//					Stephan Aßmus <superstippi@gmx.de>
//	Description:	Base class for window decorators
//  
//------------------------------------------------------------------------------
#ifndef _DECORATOR_H_
#define _DECORATOR_H_

#include <Rect.h>
#include <Region.h>
#include <String.h>
#include <Window.h>

#include "ColorSet.h"
#include "LayerData.h"

class DisplayDriver;
class ServerFont;
class BRegion;

typedef enum {
	DEC_NONE = 0,
	DEC_ZOOM,
	DEC_CLOSE,
	DEC_MINIMIZE,
	DEC_TAB,
	DEC_DRAG,
	DEC_MOVETOBACK,
	DEC_MOVETOFRONT,
	DEC_SLIDETAB,
	
	DEC_RESIZE,
	CLICK_RESIZE_L,
	CLICK_RESIZE_T, 
	CLICK_RESIZE_R,
	CLICK_RESIZE_B,
	CLICK_RESIZE_LT,
	CLICK_RESIZE_RT, 
	CLICK_RESIZE_LB,
	CLICK_RESIZE_RB
} click_type;

class Decorator {
 public:
								Decorator(BRect rect,
										  int32 wlook,
										  int32 wfeel,
										  int32 wflags);
	virtual						~Decorator();

			void				SetColors(const ColorSet &cset);
			void				SetDriver(DisplayDriver *driver);
			void				SetFlags(int32 wflags);
			void				SetFeel(int32 wfeel);
			void				SetFont(ServerFont *font);
			void				SetLook(int32 wlook);
			
			void				SetClose(bool is_down);
			void				SetMinimize(bool is_down);
			void				SetZoom(bool is_down);

	virtual	void				SetTitle(const char *string);
		
			int32				GetLook() const;
			int32				GetFeel() const;
			int32				GetFlags() const;

			const char*			GetTitle() const;

			// we need to know its border(frame). WinBorder's _frame rect
			// must expand to include Decorator borders. Otherwise we can't
			// draw the border. We also add GetTabRect because I feel we'll need it
			BRect				GetBorderRect() const;
			BRect				GetTabRect() const;
		
			bool				GetClose();
			bool				GetMinimize();
			bool				GetZoom();

	virtual	void				GetSizeLimits(float* minWidth, float* minHeight,
											  float* maxWidth, float* maxHeight) const;


			void				SetFocus(bool is_active);
			bool				GetFocus()
									{ return fIsFocused; };
			ColorSet			GetColors()
									{ return (_colors) ? *_colors : ColorSet(); }
	
	virtual	void				GetFootprint(BRegion *region);
	virtual	click_type			Clicked(BPoint pt, int32 buttons,
										int32 modifiers);

	virtual	void				MoveBy(float x, float y);
	virtual	void				MoveBy(BPoint pt);
	virtual	void				ResizeBy(float x, float y);
	virtual	void				ResizeBy(BPoint pt);

	virtual	void				SetTabLocation(float location) {}
	virtual	float				TabLocation() const
									{ return 0.0; }

	virtual	void				Draw(BRect r);
	virtual	void				Draw();
	virtual	void				DrawClose();
	virtual	void				DrawFrame();
	virtual	void				DrawMinimize();
	virtual	void				DrawTab();
	virtual	void				DrawTitle();
	virtual	void				DrawZoom();
	
 protected:
			int32				_ClipTitle(float width);

	/*!
		\brief Returns the number of characters in the title
		\return The title character count
	*/
			int32				_TitleWidth() const
									{ return fTitle.CountChars(); }

	virtual	void				_DoLayout();

	virtual	void				_DrawFrame(BRect r);
	virtual	void				_DrawTab(BRect r);
	
	virtual	void				_DrawClose(BRect r);
	virtual	void				_DrawTitle(BRect r);
	virtual	void				_DrawZoom(BRect r);
	virtual	void				_DrawMinimize(BRect r);

	virtual	void				_SetFocus();
	virtual	void				_SetColors();
	
			ColorSet*			_colors;
			DisplayDriver*		_driver;
			DrawData			_drawdata;

			int32				_look;
			int32				_feel;
			int32				_flags;

			BRect				_zoomrect;
			BRect				_closerect;
			BRect				_minimizerect;
			BRect				_tabrect;
			BRect				_frame;
			BRect				_resizerect;
			BRect				_borderrect;

 private:
			bool				fClosePressed;
			bool				fZoomPressed;
			bool				fMinimizePressed;

			bool				fIsFocused;

			BString				fTitle;
};

// add-on stuff
typedef float get_version(void);
typedef Decorator* create_decorator(BRect rect, int32 wlook, int32 wfeel, int32 wflags);

#endif
