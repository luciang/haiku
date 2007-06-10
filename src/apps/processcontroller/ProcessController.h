/*
	ProcessController © 2000, Georges-Edouard Berenger, All Rights Reserved.
	Copyright (C) 2004 beunited.org 

	This library is free software; you can redistribute it and/or 
	modify it under the terms of the GNU Lesser General Public 
	License as published by the Free Software Foundation; either 
	version 2.1 of the License, or (at your option) any later version. 

	This library is distributed in the hope that it will be useful, 
	but WITHOUT ANY WARRANTY; without even the implied warranty of 
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
	Lesser General Public License for more details. 

	You should have received a copy of the GNU Lesser General Public 
	License along with this library; if not, write to the Free Software 
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA	
*/
#ifndef _PCVIEW_H_
#define _PCVIEW_H_


#include "AutoIcon.h"

#include <View.h>


class BMessageRunner;
class ThreadBarMenu;


class ProcessController : public BView {
	public:
						ProcessController(BRect frame, bool temp = false);
						ProcessController(BMessage *data);
						ProcessController();
		virtual			~ProcessController();

		virtual	void	MessageReceived(BMessage *message);
		virtual	void	AttachedToWindow();
		virtual	void	MouseDown(BPoint where);
		virtual	void	Draw(BRect updateRect);
				void	DoDraw (bool force);
		static	ProcessController* Instantiate(BMessage* data);
		virtual	status_t Archive(BMessage *data, bool deep = true) const;

		void			AboutRequested();
		void			Update();
		void			DefaultColors();

		// TODO: move those into private, and have getter methods
		AutoIcon		fProcessControllerIcon;
		AutoIcon		fProcessorIcon;
		AutoIcon		fTrackerIcon;
		AutoIcon		fDeskbarIcon;
		AutoIcon		fTerminalIcon;

	private:
		void			Init();

		bool			fTemp;
		float			fMemoryUsage;
		float			fLastBarHeight[B_MAX_CPU_COUNT];
		float			fLastMemoryHeight;
		double			fCPUTimes[B_MAX_CPU_COUNT];
		bigtime_t		fPrevActive[B_MAX_CPU_COUNT];
		bigtime_t		fPrevTime;
		BMessageRunner*	fMessageRunner;
		rgb_color		frame_color, active_color, idle_color, memory_color, swap_color;
};

extern	ProcessController*	gPCView;
extern	int32				gCPUcount;
extern	rgb_color			gIdleColor;
extern	rgb_color			gIdleColorSelected;
extern	rgb_color			gKernelColor;
extern	rgb_color			gKernelColorSelected;
extern	rgb_color			gUserColor;
extern	rgb_color			gUserColorSelected;
extern	rgb_color			gFrameColor;
extern	rgb_color			gFrameColorSelected;
extern	rgb_color			gMenuBackColor;
extern	rgb_color			gMenuBackColorSelected;
extern	rgb_color			gWhiteSelected;
extern	ThreadBarMenu*		gCurrentThreadBarMenu;
extern	thread_id			gPopupThreadID;
extern	const char*			kDeskbarItemName;
extern	bool				gInDeskbar;

#define kBarWidth 100
#define kMargin	12

#endif // _PCVIEW_H_
