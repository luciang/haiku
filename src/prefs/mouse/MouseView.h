// ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
//
//	Copyright (c) 2003, OpenBeOS
//
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//
//  File:        MouseView.h
//  Author:      Jérôme Duval, Andrew McCall (mccall@digitalparadise.co.uk)
//  Description: Media Preferences
//  Created :   December 10, 2003
// 
// ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
#ifndef MOUSE_VIEW_H
#define MOUSE_VIEW_H


#include <Box.h>
#include <Bitmap.h>
#include <Button.h>
#include <Slider.h>
#include <PopUpMenu.h>


class MouseView : public BBox {
	public:
		MouseView(BRect frame);

		virtual void AttachedToWindow();
		virtual void MouseDown(BPoint where);
		virtual void Draw(BRect frame);
		virtual void Pulse();

		void Init();

		BSlider		*dcSpeedSlider, *mouseSpeedSlider, *mouseAccSlider;
		BPopUpMenu	*mouseTypeMenu, *focusMenu, *mouseMapMenu;

		bigtime_t 	fClickSpeed;
		int32 		fMouseSpeed;
		int32 		fMouseAcc;
		int32 		fMouseType;
		mode_mouse 	fMouseMode;
		mouse_map 	fMouseMap, fCurrentMouseMap;
		int32		fCurrentButton;
		uint32		fButtons;
		uint32		fOldButtons;
				
	private:
		typedef BBox inherited;
					
		BBitmap 	*fDoubleClickBitmap, *fSpeedBitmap, *fAccelerationBitmap;
		BBitmap		*fMouseBitmap, *fMouseDownBitmap;
};

#endif	/* MOUSE_VIEW_H */
