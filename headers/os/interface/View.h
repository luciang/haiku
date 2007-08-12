/*
 * Copyright 2001-2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Erik Jaesler (erik@cgsoftware.com)
 */
#ifndef	_VIEW_H
#define	_VIEW_H


#include <Alignment.h>
#include <BeBuild.h>
#include <Font.h>
#include <Handler.h>
#include <InterfaceDefs.h>
#include <Rect.h>
#include <Size.h>


// mouse button
enum {
	B_PRIMARY_MOUSE_BUTTON = 0x01,
	B_SECONDARY_MOUSE_BUTTON = 0x02,
	B_TERTIARY_MOUSE_BUTTON = 0x04
};

// mouse transit
enum {
	B_ENTERED_VIEW = 0,
	B_INSIDE_VIEW,
	B_EXITED_VIEW,
	B_OUTSIDE_VIEW
};

// event mask
enum {
	B_POINTER_EVENTS		= 0x00000001,
	B_KEYBOARD_EVENTS		= 0x00000002
};

// event mask options
enum {
	B_LOCK_WINDOW_FOCUS		= 0x00000001,
	B_SUSPEND_VIEW_FOCUS	= 0x00000002,
	B_NO_POINTER_HISTORY	= 0x00000004
};

enum {
	B_TRACK_WHOLE_RECT,
	B_TRACK_RECT_CORNER
};

// set font mask
enum {
	B_FONT_FAMILY_AND_STYLE	= 0x00000001,
	B_FONT_SIZE				= 0x00000002,
	B_FONT_SHEAR			= 0x00000004,
	B_FONT_ROTATION			= 0x00000008,
	B_FONT_SPACING     		= 0x00000010,
	B_FONT_ENCODING			= 0x00000020,
	B_FONT_FACE				= 0x00000040,
	B_FONT_FLAGS			= 0x00000080,
	B_FONT_FALSE_BOLD_WIDTH	= 0x00000100,
	B_FONT_ALL				= 0x000001FF
};

// view flags
const uint32 B_FULL_UPDATE_ON_RESIZE 	= 0x80000000UL;	/* 31 */
const uint32 _B_RESERVED1_ 				= 0x40000000UL;	/* 30 */
const uint32 B_WILL_DRAW 				= 0x20000000UL;	/* 29 */
const uint32 B_PULSE_NEEDED 			= 0x10000000UL;	/* 28 */
const uint32 B_NAVIGABLE_JUMP 			= 0x08000000UL;	/* 27 */
const uint32 B_FRAME_EVENTS				= 0x04000000UL;	/* 26 */
const uint32 B_NAVIGABLE 				= 0x02000000UL;	/* 25 */
const uint32 B_SUBPIXEL_PRECISE 		= 0x01000000UL;	/* 24 */
const uint32 B_DRAW_ON_CHILDREN 		= 0x00800000UL;	/* 23 */
const uint32 B_INPUT_METHOD_AWARE 		= 0x00400000UL;	/* 23 */
const uint32 _B_RESERVED7_ 				= 0x00200000UL;	/* 22 */
const uint32 B_SUPPORTS_LAYOUT			= 0x00100000UL;	/* 21 */
const uint32 B_INVALIDATE_AFTER_LAYOUT	= 0x00080000UL;	/* 20 */

#define _RESIZE_MASK_ (0xffff)

const uint32 _VIEW_TOP_ 	= 1UL;
const uint32 _VIEW_LEFT_ 	= 2UL;
const uint32 _VIEW_BOTTOM_ 	= 3UL;
const uint32 _VIEW_RIGHT_ 	= 4UL;
const uint32 _VIEW_CENTER_ 	= 5UL;

inline uint32 _rule_(uint32 r1, uint32 r2, uint32 r3, uint32 r4)
	{ return ((r1 << 12) | (r2 << 8) | (r3 << 4) | r4); }

#define B_FOLLOW_NONE 0
#define B_FOLLOW_ALL_SIDES	_rule_(_VIEW_TOP_, _VIEW_LEFT_, _VIEW_BOTTOM_, _VIEW_RIGHT_)
#define B_FOLLOW_ALL  		B_FOLLOW_ALL_SIDES

#define B_FOLLOW_LEFT		_rule_(0, _VIEW_LEFT_, 0, _VIEW_LEFT_)
#define B_FOLLOW_RIGHT		_rule_(0, _VIEW_RIGHT_, 0, _VIEW_RIGHT_)
#define B_FOLLOW_LEFT_RIGHT	_rule_(0, _VIEW_LEFT_, 0, _VIEW_RIGHT_)
#define B_FOLLOW_H_CENTER	_rule_(0, _VIEW_CENTER_, 0, _VIEW_CENTER_)

#define B_FOLLOW_TOP		_rule_(_VIEW_TOP_, 0, _VIEW_TOP_, 0)
#define B_FOLLOW_BOTTOM		_rule_(_VIEW_BOTTOM_, 0, _VIEW_BOTTOM_, 0)
#define B_FOLLOW_TOP_BOTTOM	_rule_(_VIEW_TOP_, 0, _VIEW_BOTTOM_, 0)
#define B_FOLLOW_V_CENTER	_rule_(_VIEW_CENTER_, 0, _VIEW_CENTER_, 0)

class BBitmap;
class BCursor;
class BLayout;
class BLayoutContext;
class BLayoutItem;
class BMessage;
class BPicture;
class BPolygon;
class BRegion;
class BScrollBar;
class BScrollView;
class BShape;
class BShelf;
class BString;
class BWindow;
struct _array_data_;
struct _array_hdr_;
struct overlay_restrictions;

namespace BPrivate {
	class ViewState;
};


class BView : public BHandler {
public:
							BView(const char* name, uint32 flags,
								BLayout* layout = NULL);
							BView(BRect frame, const char* name,
								  uint32 resizeMask, uint32 flags);
	virtual					~BView();

							BView(BMessage* data);
	static	BArchivable*	Instantiate(BMessage* data);
	virtual	status_t		Archive(BMessage* data, bool deep = true) const;

	virtual	void			AttachedToWindow();
	virtual	void			AllAttached();
	virtual	void			DetachedFromWindow();
	virtual	void			AllDetached();

	virtual	void			MessageReceived(BMessage* msg);

			void			AddChild(BView* child, BView* before = NULL);
			bool			AddChild(BLayoutItem* child);
			bool			RemoveChild(BView* child);
			int32			CountChildren() const;
			BView*			ChildAt(int32 index) const;
			BView*			NextSibling() const;
			BView*			PreviousSibling() const;
			bool			RemoveSelf();

			BWindow*		Window() const;

	virtual	void			Draw(BRect updateRect);
	virtual	void			MouseDown(BPoint where);
	virtual	void			MouseUp(BPoint where);
	virtual	void			MouseMoved(BPoint where,
									   uint32 code,
									   const BMessage* a_message);
	virtual	void			WindowActivated(bool state);
	virtual	void			KeyDown(const char* bytes, int32 numBytes);
	virtual	void			KeyUp(const char* bytes, int32 numBytes);
	virtual	void			Pulse();
	virtual	void			FrameMoved(BPoint new_position);
	virtual	void			FrameResized(float new_width, float new_height);

	virtual	void			TargetedByScrollView(BScrollView* scroll_view);
			void			BeginRectTracking(BRect startRect,
											  uint32 style = B_TRACK_WHOLE_RECT);
			void			EndRectTracking();

			void			GetMouse(BPoint* location,
									 uint32* buttons,
									 bool checkMessageQueue = true);

			void			DragMessage(BMessage* aMessage,
										BRect dragRect,
										BHandler* reply_to = NULL);
			void			DragMessage(BMessage* aMessage,
										BBitmap* anImage,
										BPoint offset,
										BHandler* reply_to = NULL);
			void			DragMessage(BMessage* aMessage,
										BBitmap* anImage,
										drawing_mode dragMode,
										BPoint offset,
										BHandler* reply_to = NULL);

			BView*			FindView(const char* name) const;
			BView*			Parent() const;
			BRect			Bounds() const;
			BRect			Frame() const;
			void			ConvertToScreen(BPoint* pt) const;
			BPoint			ConvertToScreen(BPoint pt) const;
			void			ConvertFromScreen(BPoint* pt) const;
			BPoint			ConvertFromScreen(BPoint pt) const;
			void			ConvertToScreen(BRect* r) const;
			BRect			ConvertToScreen(BRect r) const;
			void			ConvertFromScreen(BRect* r) const;
			BRect			ConvertFromScreen(BRect r) const;
			void			ConvertToParent(BPoint* pt) const;
			BPoint			ConvertToParent(BPoint pt) const;
			void			ConvertFromParent(BPoint* pt) const;
			BPoint			ConvertFromParent(BPoint pt) const;
			void			ConvertToParent(BRect* r) const;
			BRect			ConvertToParent(BRect r) const;
			void			ConvertFromParent(BRect* r) const;
			BRect			ConvertFromParent(BRect r) const;
			BPoint			LeftTop() const;

			void			GetClippingRegion(BRegion* region) const;
	virtual	void			ConstrainClippingRegion(BRegion* region);
			void			ClipToPicture(BPicture* picture,
										  BPoint where = B_ORIGIN,
										  bool sync = true);
			void			ClipToInversePicture(BPicture* picture,
												 BPoint where = B_ORIGIN,
												 bool sync = true);

	virtual	void			SetDrawingMode(drawing_mode mode);
			drawing_mode 	DrawingMode() const;

			void			SetBlendingMode(source_alpha srcAlpha,
											alpha_function alphaFunc);
			void	 		GetBlendingMode(source_alpha* srcAlpha,
											alpha_function* alphaFunc) const;

	virtual	void			SetPenSize(float size);
			float			PenSize() const;

			void			SetViewCursor(const BCursor* cursor, bool sync=true);

	virtual	void			SetViewColor(rgb_color c);
			void			SetViewColor(uchar r, uchar g, uchar b, uchar a = 255);
			rgb_color		ViewColor() const;

			void			SetViewBitmap(const BBitmap* bitmap,
										  BRect srcRect, BRect dstRect,
										  uint32 followFlags = B_FOLLOW_TOP|B_FOLLOW_LEFT,
										  uint32 options = B_TILE_BITMAP);
			void			SetViewBitmap(const BBitmap* bitmap,
										  uint32 followFlags = B_FOLLOW_TOP|B_FOLLOW_LEFT,
										  uint32 options = B_TILE_BITMAP);
			void			ClearViewBitmap();

			status_t		SetViewOverlay(const BBitmap* overlay,
										   BRect srcRect, BRect dstRect,
										   rgb_color* colorKey,
										   uint32 followFlags = B_FOLLOW_TOP|B_FOLLOW_LEFT,
										   uint32 options = 0);
			status_t		SetViewOverlay(const BBitmap* overlay, rgb_color* colorKey,
										   uint32 followFlags = B_FOLLOW_TOP|B_FOLLOW_LEFT,
										   uint32 options = 0);
			void			ClearViewOverlay();

	virtual	void			SetHighColor(rgb_color a_color);
			void			SetHighColor(uchar r, uchar g, uchar b, uchar a = 255);
			rgb_color		HighColor() const;

	virtual	void			SetLowColor(rgb_color a_color);
			void			SetLowColor(uchar r, uchar g, uchar b, uchar a = 255);
			rgb_color		LowColor() const;

			void			SetLineMode(cap_mode lineCap,
										join_mode lineJoin,
										float miterLimit = B_DEFAULT_MITER_LIMIT);
			join_mode		LineJoinMode() const;
			cap_mode		LineCapMode() const;
			float			LineMiterLimit() const;

			void			SetOrigin(BPoint pt);
			void			SetOrigin(float x, float y);
			BPoint			Origin() const;

			void			PushState();
			void			PopState();

			void			MovePenTo(BPoint pt);
			void			MovePenTo(float x, float y);
			void			MovePenBy(float x, float y);
			BPoint			PenLocation() const;
			void			StrokeLine(BPoint toPt,
									   pattern p = B_SOLID_HIGH);
			void			StrokeLine(BPoint pt0,
									   BPoint pt1,
									   pattern p = B_SOLID_HIGH);
			void			BeginLineArray(int32 count);
			void			AddLine(BPoint pt0, BPoint pt1, rgb_color col);
			void			EndLineArray();

			void			StrokePolygon(const BPolygon* aPolygon,
										  bool closed = true,
										  pattern p = B_SOLID_HIGH);
			void			StrokePolygon(const BPoint* ptArray,
										  int32 numPts,
										  bool closed = true,
										  pattern p = B_SOLID_HIGH);
			void			StrokePolygon(const BPoint* ptArray,
										  int32 numPts,
										  BRect bounds,
										  bool closed = true,
										  pattern p = B_SOLID_HIGH);
			void			FillPolygon(const BPolygon* aPolygon,
										pattern p = B_SOLID_HIGH);
			void			FillPolygon(const BPoint* ptArray,
										int32 numPts,
										pattern p = B_SOLID_HIGH);
			void			FillPolygon(const BPoint* ptArray,
										int32 numPts,
										BRect bounds,
										pattern p = B_SOLID_HIGH);

			void			StrokeTriangle(BPoint pt1,
										   BPoint pt2,
										   BPoint pt3,
										   BRect bounds,
										   pattern p = B_SOLID_HIGH);
			void			StrokeTriangle(BPoint pt1,
										   BPoint pt2,
										   BPoint pt3,
										   pattern p = B_SOLID_HIGH);
			void			FillTriangle(BPoint pt1,
										 BPoint pt2,
										 BPoint pt3,
										 pattern p = B_SOLID_HIGH);
			void			FillTriangle(BPoint pt1,
										 BPoint pt2,
										 BPoint pt3,
										 BRect bounds,
										 pattern p = B_SOLID_HIGH);

			void			StrokeRect(BRect r, pattern p = B_SOLID_HIGH);
			void			FillRect(BRect r, pattern p = B_SOLID_HIGH);
			void			FillRegion(BRegion* a_region, pattern p= B_SOLID_HIGH);
			void			InvertRect(BRect r);

			void			StrokeRoundRect(BRect r,
											float xRadius,
											float yRadius,
											pattern p = B_SOLID_HIGH);
			void			FillRoundRect(BRect r,
										  float xRadius,
										  float yRadius,
										  pattern p = B_SOLID_HIGH);

			void			StrokeEllipse(BPoint center,
										  float xRadius,
										  float yRadius,
										  pattern p = B_SOLID_HIGH);
			void			StrokeEllipse(BRect r, pattern p = B_SOLID_HIGH);
			void			FillEllipse(BPoint center,
										float xRadius,
										float yRadius,
										pattern p = B_SOLID_HIGH);
			void			FillEllipse(BRect r, pattern p = B_SOLID_HIGH);

			void			StrokeArc(BPoint center,
									  float xRadius,
									  float yRadius,
									  float start_angle,
									  float arc_angle,
									  pattern p = B_SOLID_HIGH);
			void			StrokeArc(BRect r,
									  float start_angle,
									  float arc_angle,
									  pattern p = B_SOLID_HIGH);
			void			FillArc(BPoint center,
									float xRadius,
									float yRadius,
									float start_angle,
									float arc_angle,
									pattern p = B_SOLID_HIGH);
			void			FillArc(BRect r,
									float start_angle,
									float arc_angle,
									pattern p = B_SOLID_HIGH);

			void			StrokeBezier(BPoint* controlPoints,
										 pattern p = B_SOLID_HIGH);
			void			FillBezier(BPoint* controlPoints,
									   pattern p = B_SOLID_HIGH);
	
			void			StrokeShape(BShape* shape,
										pattern p = B_SOLID_HIGH);
			void			FillShape(BShape* shape,
									  pattern p = B_SOLID_HIGH);

			void			CopyBits(BRect src, BRect dst);
			void			DrawBitmapAsync(const BBitmap* aBitmap,
											BRect srcRect,
											BRect dstRect);
			void			DrawBitmapAsync(const BBitmap* aBitmap);
			void			DrawBitmapAsync(const BBitmap* aBitmap, BPoint where);
			void			DrawBitmapAsync(const BBitmap* aBitmap, BRect dstRect);
			void			DrawBitmap(const BBitmap* aBitmap,
									   BRect srcRect,
									   BRect dstRect);
			void			DrawBitmap(const BBitmap* aBitmap);
			void			DrawBitmap(const BBitmap* aBitmap, BPoint where);
			void			DrawBitmap(const BBitmap* aBitmap, BRect dstRect);

			void			DrawChar(char aChar);
			void			DrawChar(char aChar, BPoint location);
			void			DrawString(const char* aString,
									   escapement_delta* delta = NULL);
			void			DrawString(const char* aString, BPoint location,
									   escapement_delta* delta = NULL);
			void			DrawString(const char* aString, int32 length,
									   escapement_delta* delta = NULL);
			void			DrawString(const char* aString,
									   int32 length,
									   BPoint location,
									   escapement_delta* delta = 0L);

	virtual void            SetFont(const BFont* font, uint32 mask = B_FONT_ALL);

	#if !_PR3_COMPATIBLE_
			void            GetFont(BFont* font) const;
	#else
			void            GetFont(BFont* font);
	#endif
			void			TruncateString(BString* in_out,
										   uint32 mode,
										   float width) const;
			float			StringWidth(const char* string) const;
			float			StringWidth(const char* string, int32 length) const;
			void			GetStringWidths(char* stringArray[],
											int32 lengthArray[],
											int32 numStrings,
											float widthArray[]) const;
			void			SetFontSize(float size);
			void			ForceFontAliasing(bool enable);
			void			GetFontHeight(font_height* height) const;

			void			Invalidate(BRect invalRect);
			void			Invalidate(const BRegion* invalRegion);
			void			Invalidate();

			void			SetDiskMode(char* filename, long offset);

			void			BeginPicture(BPicture* a_picture);
			void			AppendToPicture(BPicture* a_picture);
			BPicture*		EndPicture();

			void			DrawPicture(const BPicture* a_picture);
			void			DrawPicture(const BPicture* a_picture, BPoint where);
			void			DrawPicture(const char* filename, long offset, BPoint where);
			void			DrawPictureAsync(const BPicture* a_picture);
			void			DrawPictureAsync(const BPicture* a_picture, BPoint where);
			void			DrawPictureAsync(const char* filename, long offset,
											 BPoint where);

			status_t		SetEventMask(uint32 mask, uint32 options=0);
			uint32			EventMask();
			status_t		SetMouseEventMask(uint32 mask, uint32 options=0);

	virtual	void			SetFlags(uint32 flags);
			uint32			Flags() const;
	virtual	void			SetResizingMode(uint32 mode);
			uint32			ResizingMode() const;
			void			MoveBy(float dh, float dv);
			void			MoveTo(BPoint where);
			void			MoveTo(float x, float y);
			void			ResizeBy(float dh, float dv);
			void			ResizeTo(float width, float height);
			void			ResizeTo(BSize size);
			void			ScrollBy(float dh, float dv);
			void			ScrollTo(float x, float y);
	virtual	void			ScrollTo(BPoint where);
	virtual	void			MakeFocus(bool focusState = true);
			bool			IsFocus() const;

	virtual	void			Show();
	virtual	void			Hide();
			bool			IsHidden() const;
			bool			IsHidden(const BView* looking_from) const;

			void			Flush() const;
			void			Sync() const;

	virtual	void			GetPreferredSize(float* width, float* height);
	virtual	void			ResizeToPreferred();

			BScrollBar*		ScrollBar(orientation posture) const;

	virtual BHandler*		ResolveSpecifier(BMessage* msg,
											 int32 index,
											 BMessage* specifier,
											 int32 form,
											 const char* property);
	virtual status_t		GetSupportedSuites(BMessage* data);

			bool			IsPrinting() const;
			void			SetScale(float scale) const;
			float			Scale() const;
								// new for Haiku

	virtual status_t		Perform(perform_code d, void* arg);

	virtual	void			DrawAfterChildren(BRect r);


			// layout related

	virtual	BSize			MinSize();
	virtual	BSize			MaxSize();
	virtual	BSize			PreferredSize();
	virtual	BAlignment		Alignment();

			void			SetExplicitMinSize(BSize size);
			void			SetExplicitMaxSize(BSize size);
			void			SetExplicitPreferredSize(BSize size);
			void			SetExplicitAlignment(BAlignment alignment);

			BSize			ExplicitMinSize() const;
			BSize			ExplicitMaxSize() const;
			BSize			ExplicitPreferredSize() const;
			BAlignment		ExplicitAlignment() const;

	virtual	bool			HasHeightForWidth();
	virtual	void			GetHeightForWidth(float width, float* min,
								float* max, float* preferred);

	virtual	void			SetLayout(BLayout* layout);
			BLayout*		GetLayout() const;

	virtual	void			InvalidateLayout(bool descendants = false);
			void			EnableLayoutInvalidation();
			void			DisableLayoutInvalidation();
			bool			IsLayoutValid() const;

			BLayoutContext*	LayoutContext() const;

			void			Layout(bool force);
			void			Relayout();

protected:
	virtual	void			DoLayout();

private:
			void			_Layout(bool force, BLayoutContext* context);

private:
	struct LayoutData;

	friend class BBitmap;
	friend class BLayout;
	friend class BPrintJob;
	friend class BScrollBar;
	friend class BShelf;
	friend class BTabView;
	friend class BWindow;

	virtual	void			_ReservedView11();
	virtual	void			_ReservedView12();
	virtual	void			_ReservedView13();
	virtual	void			_ReservedView14();
	virtual	void			_ReservedView15();
	virtual	void			_ReservedView16();

							BView(const BView&);
			BView&			operator=(const BView&);

			void			_InitData(BRect frame, const char* name, uint32 resizeMask,
								uint32 flags);
			status_t		_SetViewBitmap(const BBitmap* bitmap,BRect srcRect,
								BRect dstRect, uint32 followFlags, uint32 options);
			void			DoBezier(int32 gr, BPoint* controlPoints, pattern p);
			void			DoShape(int32 gr, BShape* shape, pattern p);
			void			DoPictureClip(BPicture* picture, BPoint where, bool invert,
								bool sync);

			bool			do_owner_check() const;
			bool			do_owner_check_no_pick() const;
			void			check_lock() const;
			void			check_lock_no_pick() const;

			void			_SetOwner(BWindow* newOwner);
			void			handle_tick();
			char*			test_area(int32 length);
			void			removeCommArray();
			void			SetScroller(BScrollBar* sb);
			void			UnsetScroller(BScrollBar* sb);
			void			RealScrollTo(BPoint);
			void			fetch_font();
			uchar			font_encoding() const;
			BShelf*			_Shelf() const;
			void			_SetShelf(BShelf* shelf);

			void			_MoveTo(int32 x, int32 y);
			void			_ResizeBy(int32 deltaWidth, int32 deltaHeight);
			void			_ParentResizedBy(int32 x, int32 y);

			void			_ConvertToScreen(BPoint* pt, bool checkLock) const;
			void			_ConvertFromScreen(BPoint* pt, bool checkLock) const;

			void			_ConvertToParent(BPoint* pt, bool checkLock) const;
			void			_ConvertFromParent(BPoint* pt, bool checkLock) const;


			void			_Activate(bool state);
			void			_Attach();
			void			_Detach();
			void			_Draw(BRect updateRect);
			void			_Pulse();

			void			_UpdateStateForRemove();
			void			_UpdatePattern(::pattern pattern);

			void			_FlushIfNotInTransaction();

			bool			_CreateSelf();
			bool			_AddChildToList(BView* child, BView* before = NULL);
			bool			_RemoveChildFromList(BView* child);

			bool			_AddChild(BView *child, BView *before);
			bool			_RemoveSelf();

			// Debugging methods
			void 			PrintToStream();
			void			PrintTree();

			int32			server_token;
			uint32			fFlags;
			BPoint			fParentOffset;
			BWindow*		fOwner;
			BView*			fParent;
			BView*			fNextSibling;
			BView*			fPreviousSibling;
			BView*			fFirstChild;

			int16 			fShowLevel;
			bool			fTopLevelView;
			bool			fNoISInteraction;
			BPicture*		cpicture;
			_array_data_*	comm;

			BScrollBar*		fVerScroller;
			BScrollBar*		fHorScroller;
			bool			f_is_printing;
			bool			fAttached;
			bool			_unused_bool1;
			bool			_unused_bool2;
			BPrivate::ViewState*	fState;
			BRect			fBounds;
			BShelf*			fShelf;
			uint32			fEventMask;
			uint32			fEventOptions;

			LayoutData*		fLayoutData;

			uint32			_reserved[8];
};


// inline definitions ----------------------------------------------------------

inline void
BView::ScrollTo(float x, float y)
{
	ScrollTo(BPoint(x, y));
}

inline void
BView::SetViewColor(uchar r, uchar g, uchar b, uchar a)
{
	rgb_color color;
	color.red = r;		color.green = g;
	color.blue = b;		color.alpha = a;
	SetViewColor(color);
}

inline void
BView::SetHighColor(uchar r, uchar g, uchar b, uchar a)
{
	rgb_color color;
	color.red = r;		color.green = g;
	color.blue = b;		color.alpha = a;
	SetHighColor(color);
}

inline void
BView::SetLowColor(uchar r, uchar g, uchar b, uchar a)
{
	rgb_color color;
	color.red = r;		color.green = g;
	color.blue = b;		color.alpha = a;
	SetLowColor(color);
}

#endif	// _VIEW_H
