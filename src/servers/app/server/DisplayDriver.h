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
//	File Name:		DisplayDriver.h
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	Mostly abstract class which handles all graphics output
//					for the server
//  
//------------------------------------------------------------------------------
#ifndef _DISPLAY_DRIVER_H_
#define _DISPLAY_DRIVER_H_

#include <GraphicsCard.h>
#include <OS.h>

#include <View.h>
#include <Font.h>
#include <Rect.h>
#include <Locker.h>
#include "RGBColor.h"

class ServerCursor;
class ServerBitmap;
class LayerData;

#ifndef ROUND
	#define ROUND(a)	( (long)(a+.5) )
#endif

/*!
	\brief Data structure for passing cursor information to hardware drivers.
*/
typedef struct
{
	uchar *xormask, *andmask;
	int32 width, height;
	int32 hotx, hoty;

} cursor_data;

#ifndef HOOK_DEFINE_CURSOR

#define HOOK_DEFINE_CURSOR		0
#define HOOK_MOVE_CURSOR		1
#define HOOK_SHOW_CURSOR		2
#define HOOK_DRAW_LINE_8BIT		3
#define HOOK_DRAW_LINE_16BIT	12
#define HOOK_DRAW_LINE_32BIT	4
#define HOOK_DRAW_RECT_8BIT		5
#define HOOK_DRAW_RECT_16BIT	13
#define HOOK_DRAW_RECT_32BIT	6
#define HOOK_BLIT				7
#define HOOK_DRAW_ARRAY_8BIT	8
#define HOOK_DRAW_ARRAY_16BIT	14	// Not implemented in current R5 drivers
#define HOOK_DRAW_ARRAY_32BIT	9
#define HOOK_SYNC				10
#define HOOK_INVERT_RECT		11

#endif

/*!
	\class DisplayDriver DisplayDriver.h
	\brief Mostly abstract class which handles all graphics output for the server.

	The DisplayDriver is called in order to handle all messiness associated with
	a particular rendering context, such as the screen, and the methods to
	handle organizing information related to it along with writing to the context
	itself.
	
	While all virtual functions are technically optional, the default versions
	do very little, so implementing them all more or less required.
*/

class DisplayDriver
{
public:
	DisplayDriver(void);
	virtual ~DisplayDriver(void);
	virtual bool Initialize(void);
	virtual void Shutdown(void);
	virtual void CopyBits(BRect src, BRect dest);
	virtual void DrawBitmap(ServerBitmap *bmp, BRect src, BRect dest, LayerData *d);
	virtual void DrawString(const char *string, int32 length, BPoint pt, LayerData *d, escapement_delta *delta=NULL);

	virtual void FillArc(BRect r, float angle, float span, LayerData *d, int8 *pat);
	virtual void FillBezier(BPoint *pts, LayerData *d, int8 *pat);
	virtual void FillEllipse(BRect r, LayerData *d, int8 *pat);
	virtual void FillPolygon(BPoint *ptlist, int32 numpts, BRect rect, LayerData *d, int8 *pat);
	virtual void FillRect(BRect r, LayerData *d, int8 *pat);
	virtual void FillRoundRect(BRect r, float xrad, float yrad, LayerData *d, int8 *pat);
//	virtual void FillShape(SShape *sh, LayerData *d, int8 *pat);
	virtual void FillTriangle(BPoint *pts, BRect r, LayerData *d, int8 *pat);

	virtual void HideCursor(void);
	virtual bool IsCursorHidden(void);
	virtual void MoveCursorTo(float x, float y);
	virtual void InvertRect(BRect r);
	virtual void ShowCursor(void);
	virtual void ObscureCursor(void);
	virtual void SetCursor(ServerCursor *cursor);

	virtual void StrokeArc(BRect r, float angle, float span, LayerData *d, int8 *pat);
	virtual void StrokeBezier(BPoint *pts, LayerData *d, int8 *pat);
	virtual void StrokeEllipse(BRect r, LayerData *d, int8 *pat);
	virtual void StrokeLine(BPoint start, BPoint end, LayerData *d, int8 *pat);
	virtual void StrokePolygon(BPoint *ptlist, int32 numpts, BRect rect, LayerData *d, int8 *pat, bool is_closed=true);
	virtual void StrokeRect(BRect r, LayerData *d, int8 *pat);
	virtual void StrokeRoundRect(BRect r, float xrad, float yrad, LayerData *d, int8 *pat);
//	virtual void StrokeShape(SShape *sh, LayerData *d, int8 *pat);
	virtual void StrokeTriangle(BPoint *pts, BRect r, LayerData *d, int8 *pat);
	virtual void StrokeLineArray(BPoint *pts, int32 numlines, RGBColor *colors, LayerData *d);
	virtual void SetMode(int32 mode);
	virtual bool DumpToFile(const char *path);
	virtual ServerBitmap *DumpToBitmap(void);

	virtual float StringWidth(const char *string, int32 length, LayerData *d);
	virtual float StringHeight(const char *string, int32 length, LayerData *d);

	virtual void GetBoundingBoxes(const char *string, int32 count, font_metric_mode mode,
			escapement_delta *delta, BRect *rectarray, LayerData *d);
	virtual void GetEscapements(const char *string, int32 charcount, escapement_delta *delta, 
			escapement_delta *escapements, escapement_delta *offsets, LayerData *d);
	virtual void GetEdges(const char *string, int32 charcount, edge_info *edgearray, LayerData *d);
	virtual void GetHasGlyphs(const char *string, int32 charcount, bool *hasarray);
	virtual void GetTruncatedStrings( const char **instrings, int32 stringcount, uint32 mode, 
			float maxwidth, char **outstrings);
	
	virtual status_t SetDPMSState(uint32 state);
	uint32 GetDPMSState(void);
	uint32 GetDPMSCapabilities(void);
	uint8 GetDepth(void);
	uint16 GetHeight(void);
	uint16 GetWidth(void);
	uint32 GetBytesPerRow(void);
	int32 GetMode(void);
	bool IsCursorObscured(bool state);

protected:
	bool _Lock(bigtime_t timeout=B_INFINITE_TIMEOUT);
	void _Unlock(void);
	void _SetDepth(uint8 d);
	void _SetHeight(uint16 h);
	void _SetWidth(uint16 w);
	void _SetMode(int32 m);
	void _SetBytesPerRow(uint32 bpr);
	void _SetDPMSCapabilities(uint32 caps);
	void _SetDPMSState(uint32 state);
	ServerCursor *_GetCursor(void);

private:
	BLocker *_locker;
	uint8 _buffer_depth;
	uint16 _buffer_width;
	uint16 _buffer_height;
	int32 _buffer_mode;
	uint32 _bytes_per_row;
	bool _is_cursor_hidden;
	bool _is_cursor_obscured;
	ServerCursor *_cursor;
	uint32 _dpms_state;
	uint32 _dpms_caps;
};

#endif
