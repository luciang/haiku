#ifndef _DEFAULT_DECORATOR_H_
#define _DEFAULT_DECORATOR_H_

#include "Decorator.h"
#include <Region.h>

class DefaultDecorator: public Decorator
{
public:
	DefaultDecorator(BRect frame, int32 wlook, int32 wfeel, int32 wflags);
	~DefaultDecorator(void);
	
	void MoveBy(float x, float y);
	void MoveBy(BPoint pt);
	void Draw(BRect r);
	void Draw(void);
	BRegion *GetFootprint(void);
	click_type Clicked(BPoint pt, int32 buttons, int32 modifiers);

protected:
	void _DrawClose(BRect r);
	void _DrawFrame(BRect r);
	void _DrawTab(BRect r);
	void _DrawTitle(BRect r);
	void _DrawZoom(BRect r);
	void _DoLayout(void);
	void _SetFocus(void);
	void DrawBlendedRect(BRect r, bool down);
	uint32 taboffset;

	RGBColor tab_highcol, tab_lowcol;
	RGBColor button_highcol, button_lowcol;
	RGBColor frame_highcol, frame_midcol, frame_lowcol, frame_highercol,
		frame_lowercol;
	RGBColor textcol;
	uint64 solidhigh, solidlow;

	bool slidetab;
	int textoffset;
};

#endif