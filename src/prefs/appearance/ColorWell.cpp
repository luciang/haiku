#include "ColorWell.h"

ColorWell::ColorWell(BRect frame, BMessage *msg, bool is_rectangle=false)
	: BView(frame,"ColorWell", B_FOLLOW_ALL_SIDES, B_WILL_DRAW)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetLowColor(0,0,0);
	invoker=new BInvoker(msg,this);
	disabledcol.red=128;
	disabledcol.green=128;
	disabledcol.blue=128;
	disabledcol.alpha=255;
	is_enabled=true;
	is_rect=is_rectangle;
}

ColorWell::~ColorWell(void)
{
	delete invoker;
}

void ColorWell::SetTarget(BHandler *tgt)
{
	invoker->SetTarget(tgt);
}

void ColorWell::SetColor(rgb_color col)
{
	SetHighColor(col);
	currentcol=col;
	Draw(Bounds());
//	Invalidate();
	invoker->Invoke();
}

void ColorWell::SetColor(uint8 r,uint8 g, uint8 b)
{
	SetHighColor(r,g,b);
	currentcol.red=r;
	currentcol.green=g;
	currentcol.blue=b;
	Draw(Bounds());
	//Invalidate();
	invoker->Invoke();
}

void ColorWell::MessageReceived(BMessage *msg)
{
	// If we received a dropped message, try to see if it has color data
	// in it
	if(msg->WasDropped())
	{
		rgb_color *col;
		uint8 *ptr;
		ssize_t size;
		if(msg->FindData("RGBColor",(type_code)'RGBC',(const void**)&ptr,&size)==B_OK)
		{
			col=(rgb_color*)ptr;
			SetHighColor(*col);
		}
	}

	// The default
	BView::MessageReceived(msg);
}

void ColorWell::SetEnabled(bool value)
{
	if(is_enabled!=value)
	{
		is_enabled=value;
		Invalidate();
	}
}

void ColorWell::Draw(BRect update)
{
	if(is_enabled)
		SetHighColor(currentcol);
	else
		SetHighColor(disabledcol);

	if(is_rect)
	{
		FillRect(Bounds());
		if(is_enabled)
			StrokeRect(Bounds(),B_SOLID_LOW);
	}
	else
	{
		FillEllipse(Bounds());
		if(is_enabled)
			StrokeEllipse(Bounds(),B_SOLID_LOW);
	}
}

rgb_color ColorWell::Color(void) const
{
	return currentcol;
}

void ColorWell::SetMode(bool is_rectangle)
{
	is_rect=is_rectangle;
}