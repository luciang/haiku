/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Adi Oanca <adioanca@mymail.ro>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef _DRAW_STATE_H_
#define _DRAW_STATE_H_


#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Point.h>
#include <View.h> // for B_FONT_ALL

#include "RGBColor.h"
#include "FontManager.h"
#include "ServerFont.h"
#include "PatternHandler.h"

class BRegion;

namespace BPrivate {
	class LinkReceiver;
	class LinkSender;
};


class DrawState {
	public:
							DrawState();
							DrawState(const DrawState& from);
		virtual				~DrawState();

		DrawState&			operator=(const DrawState& from);

		DrawState*			PushState();
		DrawState*			PopState();
		DrawState*			PreviousState() const { return fPreviousState; }

		void				ReadFontFromLink(BPrivate::LinkReceiver& link);
								// NOTE: ReadFromLink() does not read Font state!!
								// It was separate in ServerWindow, and I didn't
								// want to change it without knowing implications.
		void				ReadFromLink(BPrivate::LinkReceiver& link);
		void				WriteToLink(BPrivate::LinkSender& link) const;

							// coordinate transformation
		void				SetOrigin(const BPoint& origin);
		void				OffsetOrigin(const BPoint& offset);
		const BPoint&		Origin() const
								{ return fOrigin; }

		void				SetScale(float scale);
		float				Scale() const
								{ return fScale; }

							// additional clipping as requested by client
		void				SetClippingRegion(const BRegion& region);
		const BRegion*		ClippingRegion() const
								{ return fClippingRegion; }
/*	inline	int32				CountClippingRects() const
									{ return fClippingRegion ? fClippingRegion.CountRects() : 0; }
	inline	BRect				ClippingRectAt(int32 index) const;*/

							// color
		void				SetHighColor(const RGBColor& color);
		const RGBColor&		HighColor() const
								{ return fHighColor; }

		void				SetLowColor(const RGBColor& color);
		const RGBColor&		LowColor() const
								{ return fLowColor; }

		void				SetPattern(const Pattern& pattern);
		const Pattern&		GetPattern() const
								{ return fPattern; }

							// drawing/blending mode
		void				SetDrawingMode(drawing_mode mode);
		drawing_mode		GetDrawingMode() const
								{ return fDrawingMode; }

		void				SetBlendingMode(source_alpha srcMode,
								alpha_function fncMode);
		source_alpha		AlphaSrcMode() const
								{ return fAlphaSrcMode; }
		alpha_function		AlphaFncMode() const
								{ return fAlphaFncMode; }

							// pen
		void				SetPenLocation(const BPoint& location);
		const BPoint&		PenLocation() const;

		void				SetPenSize(float size);
		float				PenSize() const;

							// font
		void				SetFont(const ServerFont& font,
								uint32 flags = B_FONT_ALL);
		const ServerFont&	Font() const
								{ return fFont; }

		// overrides aliasing flag contained in SeverFont::Flags())
		void				SetForceFontAliasing(bool aliasing);
		bool				ForceFontAliasing() const
								{ return fFontAliasing; }

							// postscript style settings
		void				SetLineCapMode(cap_mode mode);
		cap_mode			LineCapMode() const
								{ return fLineCapMode; }

		void				SetLineJoinMode(join_mode mode);
		join_mode			LineJoinMode() const
								{ return fLineJoinMode; }

		void				SetMiterLimit(float limit);
		float				MiterLimit() const
								{ return fMiterLimit; }

							// convenience functions
		inline BPoint		Transform(const BPoint& point) const;
		inline BRect		Transform(const BRect& rect) const;
		void				PrintToStream() const;

		void				SetSubPixelPrecise(bool precise);
		bool				SubPixelPrecise() const
								{ return fSubPixelPrecise; }

	protected:
		BPoint				fOrigin;
		float				fScale;

		BRegion*			fClippingRegion;

		RGBColor			fHighColor;
		RGBColor			fLowColor;
		Pattern				fPattern;

		drawing_mode		fDrawingMode;
		source_alpha		fAlphaSrcMode;
		alpha_function		fAlphaFncMode;

		BPoint				fPenLocation;
		float				fPenSize;

		ServerFont			fFont;
		// overrides font aliasing flag
		bool				fFontAliasing;

		// This is not part of the normal state stack.
		// Layer will update it in PushState/PopState.
		// A BView can have a flag "B_SUBPIXEL_PRECISE",
		// I never knew what it does on R5, but I can use
		// it in Painter to actually draw stuff with
		// sub-pixel coordinates. It means
		// StrokeLine(BPoint(10, 5), BPoint(20, 9));
		// will look different from
		// StrokeLine(BPoint(10.3, 5.8), BPoint(20.6, 9.5));
		bool				fSubPixelPrecise;

		cap_mode			fLineCapMode;
		join_mode			fLineJoinMode;
		float				fMiterLimit;
		// "internal", used to calculate the size
		// of the font (again) when the scale changes
		float				fUnscaledFontSize;

		DrawState*			fPreviousState;
};


// inline implementations

// Transform
BPoint
DrawState::Transform(const BPoint& point) const
{
	BPoint p(point);
	p += fOrigin;
	p.x *= fScale;
	p.y *= fScale;
	return p;
}

// Transform
BRect
DrawState::Transform(const BRect& rect) const
{
	return BRect(Transform(rect.LeftTop()),
				 Transform(rect.LeftBottom()));
}
/*
// ClippingRectAt
BRect
DrawState::ClippingRectAt(int32 index) const
{
	BRect r;
	if (fClippingRegion) {
		r = fClippingRegion.RectAt(index);
	}
	return r;
}*/

#endif	/* _DRAW_STATE_H_ */
