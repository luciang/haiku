// license: public domain
// authors: jonas.sundstrom@kirilla.com

#include "ZipOMaticActivity.h"

Activity::Activity (BRect a_rect, const char * a_name, uint32 a_resizing_mode, uint32 a_flags)
:	BView 				(a_rect, a_name, a_resizing_mode, a_flags),
	m_is_running		(false),
	m_barberpole_bitmap	(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	
	m_pattern.data[0] = 0x0f;
	m_pattern.data[1] = 0x1e;
	m_pattern.data[2] = 0x3c;
	m_pattern.data[3] = 0x78;
	m_pattern.data[4] = 0xf0;
	m_pattern.data[5] = 0xe1;
	m_pattern.data[6] = 0xc3;
	m_pattern.data[7] = 0x87;
	
	CreateBitmap();
};

Activity::~Activity()
{
	// subviews are deleted by superclass
	
	delete m_barberpole_bitmap;
}

void 
Activity::Start()
{
	m_is_running = true;
	Window()->SetPulseRate(100000);
	SetFlags(Flags() | B_PULSE_NEEDED);
	Invalidate();
}

void 
Activity::Pause()
{
	Window()->SetPulseRate(500000);
	SetFlags(Flags() & (~ B_PULSE_NEEDED));
	Invalidate();
}

void 
Activity::Stop()
{
	m_is_running = false;
	Window()->SetPulseRate(500000);
	SetFlags(Flags() & (~ B_PULSE_NEEDED));
	Invalidate();
}

bool 
Activity::IsRunning()	
{
	return m_is_running;
}

void 
Activity::Pulse()
{
	uchar tmp = m_pattern.data[7];
	
	for (int j = 7;  j > 0;  --j)
	{ 
		m_pattern.data[j]  =  m_pattern.data[j-1];
	}
	
	m_pattern.data[0] = tmp;
	
	Invalidate();
}

void 
Activity::Draw(BRect a_rect)
{
	DrawIntoBitmap(IsRunning());
	SetDrawingMode(B_OP_COPY);
	DrawBitmap(m_barberpole_bitmap);
}

void Activity::DrawIntoBitmap (bool running)
{
	if (m_barberpole_bitmap->Lock())
	{
		BRect a_rect  =  m_barberpole_bitmap->Bounds();

		m_barberpole_bitmap_view->SetDrawingMode(B_OP_COPY);
	
		rgb_color  color;
		color.red    =  0;
		color.green  =  0;
		color.blue   =  0;
		color.alpha  =  255;
		
		if (running)
			color.blue = 200;
		
		m_barberpole_bitmap_view->SetHighColor(color);

		// draw the pole
		a_rect.InsetBy(2,2);
		m_barberpole_bitmap_view->FillRect(a_rect, m_pattern);	
		
		// draw frame

		// left
		color.red    =  150;
		color.green  =  150;
		color.blue   =  150;
		m_barberpole_bitmap_view->SetHighColor(color);
		m_barberpole_bitmap_view->SetDrawingMode(B_OP_OVER);
		BPoint  point_a  =  m_barberpole_bitmap->Bounds().LeftTop();
		BPoint  point_b  =  m_barberpole_bitmap->Bounds().LeftBottom();
		point_b.y -= 1;
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);
		point_a.x += 1;
		point_b.x += 1;
		point_b.y -= 1;
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);

		// top
		point_a  =  m_barberpole_bitmap->Bounds().LeftTop();
		point_b  =  m_barberpole_bitmap->Bounds().RightTop();
		point_b.x -= 1;
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);
		point_a.y += 1;
		point_b.y += 1;
		point_b.x -= 1;
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);

		// right
		color.red    =  255;
		color.green  =  255;
		color.blue   =  255;
		m_barberpole_bitmap_view->SetHighColor(color);
		point_a  =  m_barberpole_bitmap->Bounds().RightTop();
		point_b  =  m_barberpole_bitmap->Bounds().RightBottom();
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);
		point_a.y += 1;
		point_a.x -= 1;
		point_b.x -= 1;
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);

		// bottom
		point_a  =  m_barberpole_bitmap->Bounds().LeftBottom();
		point_b  =  m_barberpole_bitmap->Bounds().RightBottom();
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);
		point_a.x += 1;
		point_a.y -= 1;
		point_b.y -= 1;
		m_barberpole_bitmap_view->StrokeLine(point_a, point_b);		
		
		// some blending
		color.red    =  150;
		color.green  =  150;
		color.blue   =  150;
		m_barberpole_bitmap_view->SetHighColor(color);
		m_barberpole_bitmap_view->SetDrawingMode(B_OP_SUBTRACT);
		m_barberpole_bitmap_view->StrokeRect(a_rect);
	
		a_rect.InsetBy(1,1);
		LightenBitmapHighColor(& color);
		m_barberpole_bitmap_view->StrokeRect(a_rect);
		
		a_rect.InsetBy(1,1);
		LightenBitmapHighColor(& color);
		m_barberpole_bitmap_view->StrokeRect(a_rect);
		
		a_rect.InsetBy(1,1);
		LightenBitmapHighColor(& color);
		m_barberpole_bitmap_view->StrokeRect(a_rect);
		
		a_rect.InsetBy(1,1);
		LightenBitmapHighColor(& color);
		m_barberpole_bitmap_view->StrokeRect(a_rect);
		
		m_barberpole_bitmap_view->Sync();
		m_barberpole_bitmap->Unlock();
	}
}

void Activity::LightenBitmapHighColor (rgb_color * a_color)
{
	a_color->red    -=  30;
	a_color->green  -=  30;
	a_color->blue   -=  30;
	
	m_barberpole_bitmap_view->SetHighColor(* a_color);
}

void Activity::CreateBitmap (void)
{
	BRect barberpole_rect  =  Bounds();
	m_barberpole_bitmap	=	new BBitmap(barberpole_rect, B_CMAP8, true);
	m_barberpole_bitmap_view  =  new BView(Bounds(), "buffer", B_FOLLOW_NONE, 0);
	m_barberpole_bitmap->AddChild(m_barberpole_bitmap_view);
}

void Activity::FrameResized (float a_width, float a_height)
{
	delete m_barberpole_bitmap;
	CreateBitmap();
	Invalidate();
}
