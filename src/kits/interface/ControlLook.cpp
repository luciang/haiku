/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * Distributed under the terms of the MIT License.
 */
#include <ControlLook.h>

#include <stdio.h>

#include <Control.h>
#include <GradientLinear.h>
#include <Region.h>
#include <Shape.h>
#include <String.h>
#include <View.h>

namespace BPrivate {

static const float kEdgeBevelLightTint = 0.59;
static const float kEdgeBevelShadowTint = 1.0735;


BControlLook::BControlLook()
{
}


BControlLook::~BControlLook()
{
}


BAlignment
BControlLook::DefaultLabelAlignment() const
{
	return BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER);
}


float
BControlLook::DefaultLabelSpacing() const
{
	return 4.0;//ceilf(be_plain_font->Size() / 4.0);
}


uint32
BControlLook::Flags(BControl* control) const
{
	uint32 flags = 0;

	if (!control->IsEnabled())
		flags |= B_DISABLED;

	if (control->IsFocus())
		flags |= B_FOCUSED;

	if (control->Value() == B_CONTROL_ON)
		flags |= B_ACTIVATED;

	return flags;
}


// #pragma mark -


void
BControlLook::DrawButtonFrame(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, base, 1.0, 1.0, flags, borders);
}


void
BControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, enum orientation orientation)
{
	if (!rect.IsValid() || !updateRect.Intersects(rect))
		return;

	// the surface edges

	// colors
	rgb_color buttonBgColor = tint_color(base, B_LIGHTEN_1_TINT);
	rgb_color maxLightColor;

	rgb_color bevelColor1;
	rgb_color bevelColor2;

	if ((flags & B_DISABLED) == 0) {
		maxLightColor = tint_color(base, 0.2);
		bevelColor1 = tint_color(base, 1.08);
		bevelColor2 = base;
	} else {
		maxLightColor = tint_color(base, 0.7);
		bevelColor1 = base;
		bevelColor2 = buttonBgColor;
		buttonBgColor = maxLightColor;
	}

	if (flags & B_ACTIVATED) {
		view->BeginLineArray(4);

		bevelColor1 = tint_color(bevelColor1, B_DARKEN_1_TINT);
		bevelColor2 = tint_color(bevelColor2, B_DARKEN_1_TINT);

		// shadow along left/top borders
		if (borders & B_LEFT_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.left, rect.bottom), bevelColor1);
			rect.left++;
		}
		if (borders & B_TOP_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), bevelColor1);
			rect.top++;
		}

		// softer shadow along left/top borders
		if (borders & B_LEFT_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.left, rect.bottom), bevelColor2);
			rect.left++;
		}
		if (borders & B_TOP_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), bevelColor2);
			rect.top++;
		}

		view->EndLineArray();
	} else {
		_DrawFrame(view, rect,
			maxLightColor, maxLightColor,
			bevelColor1, bevelColor1,
			buttonBgColor, buttonBgColor, borders);
	}

	// the actual surface top

	float topTint = 0.49;
	float middleTint1 = 0.62;
	float middleTint2 = 0.76;
	float bottomTint = 0.90;

	if (flags & B_ACTIVATED) {
		topTint = 1.11;
		bottomTint = 1.08;
	}

	if (flags & B_DISABLED) {
		topTint = (topTint + B_NO_TINT) / 2;
		middleTint1 = (middleTint1 + B_NO_TINT) / 2;
		middleTint2 = (middleTint2 + B_NO_TINT) / 2;
		bottomTint = (bottomTint + B_NO_TINT) / 2;
	} else if (flags & B_HOVER) {
		static const float kHoverTintFactor = 0.85;
		topTint *= kHoverTintFactor;
		middleTint1 *= kHoverTintFactor;
		middleTint2 *= kHoverTintFactor;
		bottomTint *= kHoverTintFactor;
	}

	if (flags & B_ACTIVATED) {
		_FillGradient(view, rect, base, topTint, bottomTint, orientation);
	} else {
		_FillGlossyGradient(view, rect, base, topTint, middleTint1,
			middleTint2, bottomTint, orientation);
	}
}


void
BControlLook::DrawMenuBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !updateRect.Intersects(rect))
		return;

	// the surface edges

	// colors
	float topTint;
	float bottomTint;

	if (flags & B_ACTIVATED) {
		rgb_color bevelColor1 = tint_color(base, 1.40);
		rgb_color bevelColor2 = tint_color(base, 1.25);

		topTint = 1.25;
		bottomTint = 1.20;

		_DrawFrame(view, rect,
			bevelColor1, bevelColor1,
			bevelColor2, bevelColor2,
			borders & B_TOP_BORDER);
	} else {
		rgb_color cornerColor = tint_color(base, 0.9);
		rgb_color bevelColorTop = tint_color(base, 0.5);
		rgb_color bevelColorLeft = tint_color(base, 0.7);
		rgb_color bevelColorRightBottom = tint_color(base, 1.08);

		topTint = 0.69;
		bottomTint = 1.03;

		_DrawFrame(view, rect,
			bevelColorLeft, bevelColorTop,
			bevelColorRightBottom, bevelColorRightBottom,
			cornerColor, cornerColor,
			borders);
	}

	// the actual surface top

	_FillGradient(view, rect, base, topTint, bottomTint);
}


void
BControlLook::DrawMenuFieldFrame(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, base, 0.6, 1.0, flags, borders);
}


void
BControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, bool popupIndicator,
	uint32 flags)
{
	if (popupIndicator) {
		BRect leftRect(rect);
		leftRect.right -= 10;
	
		BRect rightRect(rect);
		rightRect.left = rightRect.right - 9;

		DrawMenuFieldBackground(view, leftRect, updateRect, base, flags,
			B_LEFT_BORDER | B_TOP_BORDER | B_BOTTOM_BORDER);

		rgb_color indicatorBase;
		rgb_color markColor;
		if (flags & B_DISABLED) {
			indicatorBase = tint_color(base, 1.05);
			markColor = tint_color(base, 1.35);
		} else {
			indicatorBase = tint_color(base, 1.12);
			markColor = tint_color(base, 1.65);
		}

		DrawMenuFieldBackground(view, rightRect, updateRect, indicatorBase,
			flags, B_RIGHT_BORDER | B_TOP_BORDER | B_BOTTOM_BORDER);

		// popup marker
		BPoint center(roundf((rightRect.left + rightRect.right) / 2.0),
					  roundf((rightRect.top + rightRect.bottom) / 2.0));
		BPoint triangle[3];
		triangle[0] = center + BPoint(-2.5, -0.5);
		triangle[1] = center + BPoint(2.5, -0.5);
		triangle[2] = center + BPoint(0.0, 2.0);

		uint32 viewFlags = view->Flags();
		view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);

		view->SetHighColor(markColor);
		view->FillTriangle(triangle[0], triangle[1], triangle[2]);
	
		view->SetFlags(viewFlags);

		rect = leftRect;
	} else {
		DrawMenuFieldBackground(view, rect, updateRect, base, flags);
	}
}

void
BControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !updateRect.Intersects(rect))
		return;

	// the surface edges

	// colors
	rgb_color cornerColor = tint_color(base, 0.85);
	rgb_color bevelColor1 = tint_color(base, 0.3);
	rgb_color bevelColor2 = tint_color(base, 0.5);
	rgb_color bevelColor3 = tint_color(base, 1.03);

	if (flags & B_DISABLED) {
		cornerColor = tint_color(base, 0.8);
		bevelColor1 = tint_color(base, 0.7);
		bevelColor2 = tint_color(base, 0.8);
		bevelColor3 = tint_color(base, 1.01);
	} else {
		cornerColor = tint_color(base, 0.85);
		bevelColor1 = tint_color(base, 0.3);
		bevelColor2 = tint_color(base, 0.5);
		bevelColor3 = tint_color(base, 1.03);
	}

	_DrawFrame(view, rect,
		bevelColor2, bevelColor1,
		bevelColor3, bevelColor3,
		cornerColor, cornerColor,
		borders);

	// the actual surface top

	float topTint = 0.49;
	float middleTint1 = 0.62;
	float middleTint2 = 0.76;
	float bottomTint = 0.90;

	if (flags & B_DISABLED) {
		topTint = (topTint + B_NO_TINT) / 2;
		middleTint1 = (middleTint1 + B_NO_TINT) / 2;
		middleTint2 = (middleTint2 + B_NO_TINT) / 2;
		bottomTint = (bottomTint + B_NO_TINT) / 2;
	} else if (flags & B_HOVER) {
		static const float kHoverTintFactor = 0.85;
		topTint *= kHoverTintFactor;
		middleTint1 *= kHoverTintFactor;
		middleTint2 *= kHoverTintFactor;
		bottomTint *= kHoverTintFactor;
	}

	_FillGlossyGradient(view, rect, base, topTint, middleTint1,
		middleTint2, bottomTint);
}

void
BControlLook::DrawMenuBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !updateRect.Intersects(rect))
		return;

	// the surface edges

	rgb_color bevelLightColor;
	rgb_color bevelShadowColor;
	rgb_color background = tint_color(base, 0.75);

	if (flags & B_DISABLED) {
		bevelLightColor = tint_color(background, 0.80);
		bevelShadowColor = tint_color(background, 1.07);
	} else {
		bevelLightColor = tint_color(background, 0.6);
		bevelShadowColor = tint_color(background, 1.12);
	}

	_DrawFrame(view, rect,
		bevelLightColor, bevelLightColor,
		bevelShadowColor, bevelShadowColor,
		borders);

	// the actual surface top

	view->SetHighColor(background);
	view->FillRect(rect);
}


void
BControlLook::DrawMenuItemBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !updateRect.Intersects(rect))
		return;

	// the surface edges

	float topTint;
	float bottomTint;
	rgb_color selectedColor = base;

	if (flags & B_ACTIVATED) {
		topTint = 0.9;
		bottomTint = 1.05;
		selectedColor = tint_color(base, 1.26);
	} else if (flags & B_DISABLED) {
		topTint = 0.80;
		bottomTint = 1.07;
	} else {
		topTint = 0.6;
		bottomTint = 1.12;
	}

	rgb_color bevelLightColor = tint_color(selectedColor, topTint);
	rgb_color bevelShadowColor = tint_color(selectedColor, bottomTint);

	_DrawFrame(view, rect,
		bevelLightColor, bevelLightColor,
		bevelShadowColor, bevelShadowColor,
		borders);

	// the actual surface top

	view->SetLowColor(selectedColor);
//	_FillGradient(view, rect, selectedColor, topTint, bottomTint);
	_FillGradient(view, rect, selectedColor, bottomTint, topTint);
}


void
BControlLook::DrawStatusBar(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, const rgb_color& barColor, float progressPosition)
{
	if (!rect.Intersects(updateRect))
		return;

	_DrawOuterResessedFrame(view, rect, base, 0.6);

	// colors 
	rgb_color dark1BorderColor = tint_color(base, 1.3);
	rgb_color dark2BorderColor = tint_color(base, 1.2);
	rgb_color dark1FilledBorderColor = tint_color(barColor, 1.20);
	rgb_color dark2FilledBorderColor = tint_color(barColor, 1.45);

	BRect filledRect(rect);
	filledRect.right = progressPosition - 1;

	BRect nonfilledRect(rect);
	nonfilledRect.left = progressPosition;

	bool filledSurface = filledRect.Width() > 0;
	bool nonfilledSurface = nonfilledRect.Width() > 0;

	if (filledSurface) {
		_DrawFrame(view, filledRect,
			dark1FilledBorderColor, dark1FilledBorderColor,
			dark2FilledBorderColor, dark2FilledBorderColor);

		_FillGlossyGradient(view, filledRect, barColor, 0.55, 0.68, 0.76, 0.90);
	}

	if (nonfilledSurface) {
		_DrawFrame(view, nonfilledRect, dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor,
			B_TOP_BORDER | B_BOTTOM_BORDER | B_RIGHT_BORDER);

		if (nonfilledRect.left < nonfilledRect.right) {
			// shadow from fill bar, or left border
			rgb_color leftBorder = dark1BorderColor;
			if (filledSurface)
				leftBorder = tint_color(base, 0.50);
			view->SetHighColor(leftBorder);
			view->StrokeLine(nonfilledRect.LeftTop(),
				nonfilledRect.LeftBottom());
			nonfilledRect.left++;
		}

		_FillGradient(view, nonfilledRect, base, 0.25, 0.06);
	}
}


void
BControlLook::DrawCheckBox(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags)
{
	if (!rect.Intersects(updateRect))
		return;

	rgb_color dark1BorderColor;
	rgb_color dark2BorderColor;
	rgb_color navigationColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if (flags & B_DISABLED) {
		_DrawOuterResessedFrame(view, rect, base, 0.0);

		dark1BorderColor = tint_color(base, 1.15);
		dark2BorderColor = tint_color(base, 1.15);
	} else if (flags & B_CLICKED) {
		dark1BorderColor = tint_color(base, 1.50);
		dark2BorderColor = tint_color(base, 1.48);

		_DrawFrame(view, rect,
			dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor);

		dark2BorderColor = dark1BorderColor;
	} else {
		_DrawOuterResessedFrame(view, rect, base, 0.6);

		dark1BorderColor = tint_color(base, 1.40);
		dark2BorderColor = tint_color(base, 1.38);
	}

	if (flags & B_FOCUSED) {
		dark1BorderColor = navigationColor;
		dark2BorderColor = navigationColor;
	}

	_DrawFrame(view, rect,
		dark1BorderColor, dark1BorderColor,
		dark2BorderColor, dark2BorderColor);

	if (flags & B_DISABLED) {
		_FillGradient(view, rect, base, 0.4, 0.2);
	} else {
		_FillGradient(view, rect, base, 0.15, 0.0);
	}

	rgb_color markColor;
	if (_RadioButtonAndCheckBoxMarkColor(base, markColor, flags)) {
		view->SetHighColor(markColor);

		rect.InsetBy(2, 2);
		view->SetPenSize(max_c(1.0, ceilf(rect.Width() / 3.5)));
		view->SetDrawingMode(B_OP_OVER);

		view->StrokeLine(rect.LeftTop(), rect.RightBottom());
		view->StrokeLine(rect.LeftBottom(), rect.RightTop());
	}
}


void
BControlLook::DrawRadioButton(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags)
{
	if (!rect.Intersects(updateRect))
		return;

	rgb_color borderColor;
	rgb_color bevelLight;
	rgb_color bevelShadow;
	rgb_color navigationColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if (flags & B_DISABLED) {
		borderColor = tint_color(base, 1.15);
		bevelLight = base;
		bevelShadow = base;
	} else if (flags & B_CLICKED) {
		borderColor = tint_color(base, 1.50);
		bevelLight = borderColor;
		bevelShadow = borderColor;
	} else {
		borderColor = tint_color(base, 1.45);
		bevelLight = tint_color(base, 0.55);
		bevelShadow = tint_color(base, 1.11);
	}

	if (flags & B_FOCUSED) {
		borderColor = navigationColor;
	}

	BGradientLinear bevelGradient;
	bevelGradient.AddColor(bevelShadow, 0);
	bevelGradient.AddColor(bevelLight, 255);
	bevelGradient.SetStart(rect.LeftTop());
	bevelGradient.SetEnd(rect.RightBottom());

	view->FillEllipse(rect, bevelGradient);
	rect.InsetBy(1, 1);

	bevelGradient.MakeEmpty();
	bevelGradient.AddColor(borderColor, 0);
	bevelGradient.AddColor(tint_color(borderColor, 0.8), 255);
	view->FillEllipse(rect, bevelGradient);
	rect.InsetBy(1, 1);

	float topTint;
	float bottomTint;
	if (flags & B_DISABLED) {
		topTint = 0.4;
		bottomTint = 0.2;
	} else {
		topTint = 0.15;
		bottomTint = 0.0;
	}

	BGradientLinear gradient;
	_MakeGradient(gradient, rect, base, topTint, bottomTint);
	view->FillEllipse(rect, gradient);

	rgb_color markColor;
	if (_RadioButtonAndCheckBoxMarkColor(base, markColor, flags)) {
		view->SetHighColor(markColor);
		rect.InsetBy(3, 3);
		view->FillEllipse(rect);
	}
}


void
BControlLook::DrawScrollBarBackground(BView* view, BRect& rect1, BRect& rect2,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	enum orientation orientation)
{
	DrawScrollBarBackground(view, rect1, updateRect, base, flags, orientation);
	DrawScrollBarBackground(view, rect2, updateRect, base, flags, orientation);
}

void
BControlLook::DrawScrollBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	float gradient1Tint;
	float gradient2Tint;
	float darkEdge1Tint;
	float darkEdge2Tint;
	float shadowTint;

	if (flags & B_DISABLED) {
		gradient1Tint = 0.9;
		gradient2Tint = 0.8;
		darkEdge1Tint = B_DARKEN_2_TINT;
		darkEdge2Tint = B_DARKEN_2_TINT;
		shadowTint = gradient1Tint;
	} else {
		gradient1Tint = 1.10;
		gradient2Tint = 1.05;
		darkEdge1Tint = B_DARKEN_3_TINT;
		darkEdge2Tint = B_DARKEN_2_TINT;
		shadowTint = gradient1Tint;
	}

	rgb_color darkEdge1 = tint_color(base, darkEdge1Tint);
	rgb_color darkEdge2 = tint_color(base, darkEdge2Tint);
	rgb_color shadow = tint_color(base, shadowTint);

	if (orientation == B_HORIZONTAL) {
		// dark vertical line on left edge
		if (rect.Width() > 0) {
			view->SetHighColor(darkEdge1);
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			rect.left++;
		}
		// dark vertical line on right edge
		if (rect.Width() >= 0) {
			view->SetHighColor(darkEdge2);
			view->StrokeLine(rect.RightTop(), rect.RightBottom());
			rect.right--;
		}
		// vertical shadow line after left edge
		if (rect.Width() >= 0) {
			view->SetHighColor(shadow);
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			rect.left++;
		}
		// fill
		if (rect.Width() >= 0) {
			_FillGradient(view, rect, base, gradient1Tint, gradient2Tint,
				orientation);
		}
	} else {
		// dark vertical line on top edge
		if (rect.Height() > 0) {
			view->SetHighColor(darkEdge1);
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			rect.top++;
		}
		// dark vertical line on bottom edge
		if (rect.Height() >= 0) {
			view->SetHighColor(darkEdge2);
			view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
			rect.bottom--;
		}
		// horizontal shadow line after top edge
		if (rect.Height() >= 0) {
			view->SetHighColor(shadow);
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			rect.top++;
		}
		// fill
		if (rect.Height() >= 0) {
			_FillGradient(view, rect, base, gradient1Tint, gradient2Tint,
				orientation);
		}
	}
}


void
BControlLook::DrawScrollViewFrame(BView* view, BRect& rect,
	const BRect& updateRect, BRect verticalScrollBarFrame,
	BRect horizontalScrollBarFrame, const rgb_color& base,
	border_style border, uint32 flags, uint32 _borders)
{
	if (border == B_NO_BORDER)
		return;

	bool excludeScrollCorner = border == B_FANCY_BORDER
		&& horizontalScrollBarFrame.IsValid()
		&& verticalScrollBarFrame.IsValid();

	uint32 borders = _borders;
	if (excludeScrollCorner) {
		rect.bottom = horizontalScrollBarFrame.top;
		rect.right = verticalScrollBarFrame.left;
		borders &= ~(B_RIGHT_BORDER | B_BOTTOM_BORDER);
	}

	rgb_color scrollbarFrameColor = tint_color(base, B_DARKEN_2_TINT);

	if (border == B_FANCY_BORDER)
		_DrawOuterResessedFrame(view, rect, base, 1.0, 1.0, borders);

	if (flags & B_FOCUSED) {
		rgb_color focusColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		_DrawFrame(view, rect, focusColor, focusColor, focusColor, focusColor,
			borders);
	} else {
		_DrawFrame(view, rect, scrollbarFrameColor, scrollbarFrameColor,
			scrollbarFrameColor, scrollbarFrameColor, borders);
	}

	if (excludeScrollCorner) {
		horizontalScrollBarFrame.InsetBy(-1, -1);
		// do not overdraw the top edge
		horizontalScrollBarFrame.top += 2;
		borders = _borders;
		borders &= ~B_TOP_BORDER;
		_DrawOuterResessedFrame(view, horizontalScrollBarFrame, base,
			1.0, 1.0, borders);
		_DrawFrame(view, horizontalScrollBarFrame, scrollbarFrameColor,
			scrollbarFrameColor, scrollbarFrameColor, scrollbarFrameColor,
			borders);


		verticalScrollBarFrame.InsetBy(-1, -1);
		// do not overdraw the left edge
		verticalScrollBarFrame.left += 2;
		borders = _borders;
		borders &= ~B_LEFT_BORDER;
		_DrawOuterResessedFrame(view, verticalScrollBarFrame, base,
			1.0, 1.0, borders);
		_DrawFrame(view, verticalScrollBarFrame, scrollbarFrameColor,
			scrollbarFrameColor, scrollbarFrameColor, scrollbarFrameColor,
			borders);
	}
}


void
BControlLook::DrawArrowShape(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 direction, uint32 flags, float tint)
{
	BPoint tri1, tri2, tri3;
	float hInset = rect.Width() / 3;
	float vInset = rect.Height() / 3;
	rect.InsetBy(hInset, vInset);

	switch (direction) {
		case B_LEFT_ARROW:
			tri1.Set(rect.right, rect.top);
			tri2.Set(rect.right - rect.Width() / 1.33,
				(rect.top + rect.bottom + 1) /2 );
			tri3.Set(rect.right, rect.bottom + 1);
			break;
		case B_RIGHT_ARROW:
			tri1.Set(rect.left, rect.bottom + 1);
			tri2.Set(rect.left + rect.Width() / 1.33,
				(rect.top + rect.bottom + 1) / 2);
			tri3.Set(rect.left, rect.top);
			break;
		case B_UP_ARROW:
			tri1.Set(rect.left, rect.bottom);
			tri2.Set((rect.left + rect.right + 1) / 2,
				rect.bottom - rect.Height() / 1.33);
			tri3.Set(rect.right + 1, rect.bottom);
			break;
		case B_DOWN_ARROW:
		default:
			tri1.Set(rect.left, rect.top);
			tri2.Set((rect.left + rect.right + 1) / 2,
				rect.top + rect.Height() / 1.33);
			tri3.Set(rect.right + 1, rect.top);
			break;
	}
	// offset triangle if down
	if (flags & B_ACTIVATED)
		view->MovePenTo(BPoint(1, 1));
	else
		view->MovePenTo(BPoint(0, 0));

	BShape arrowShape;
	arrowShape.MoveTo(tri1);
	arrowShape.LineTo(tri2);
	arrowShape.LineTo(tri3);

	if (flags & B_DISABLED)
		tint = (tint + B_NO_TINT + B_NO_TINT) / 3;

	view->SetHighColor(tint_color(base, tint));

	float penSize = view->PenSize();
	drawing_mode mode = view->DrawingMode();

	view->SetPenSize(ceilf(hInset / 2.0));
	view->SetDrawingMode(B_OP_OVER);
	view->StrokeShape(&arrowShape);

	view->SetPenSize(penSize);
	view->SetDrawingMode(mode);
}


rgb_color
BControlLook::SliderBarColor(const rgb_color& base)
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT);
}


void
BControlLook::DrawSliderBar(BView* view, BRect rect, const BRect& updateRect,
	const rgb_color& base, rgb_color leftFillColor, rgb_color rightFillColor,
	float sliderScale, uint32 flags, enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	// separate the bar in two sides
	float sliderPosition;
	BRect leftBarSide = rect;
	BRect rightBarSide = rect;

	if (orientation == B_HORIZONTAL) {
		sliderPosition = floorf(rect.left + 2 + (rect.Width() - 2)
			* sliderScale);
		leftBarSide.right = sliderPosition - 1;
		rightBarSide.left = sliderPosition;
	} else {
		sliderPosition = floorf(rect.top + 2 + (rect.Height() - 2)
			* sliderScale);
		leftBarSide.bottom = sliderPosition - 1;
		rightBarSide.top = sliderPosition;
	}

	// fill the background for the corners, exclude the middle bar for now 
	BRegion region(rect);
	region.Exclude(rightBarSide);
	view->ConstrainClippingRegion(&region);

	view->PushState();

	DrawSliderBar(view, rect, updateRect, base, leftFillColor, flags,
		orientation);

	view->PopState();	

	region.Set(rect);
	region.Exclude(leftBarSide);
	view->ConstrainClippingRegion(&region);

	view->PushState();

	DrawSliderBar(view, rect, updateRect, base, rightFillColor, flags,
		orientation);

	view->PopState();	

	view->ConstrainClippingRegion(NULL);
}


void
BControlLook::DrawSliderBar(BView* view, BRect rect, const BRect& updateRect,
	const rgb_color& base, rgb_color fillColor, uint32 flags,
	enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	// separate the rect into corners
	BRect leftCorner(rect);
	BRect rightCorner(rect);
	BRect barRect(rect);

	if (orientation == B_HORIZONTAL) {
		leftCorner.right = leftCorner.left + leftCorner.Height();
		rightCorner.left = rightCorner.right - rightCorner.Height();
		barRect.left += ceilf(barRect.Height() / 2);
		barRect.right -= ceilf(barRect.Height() / 2);
	} else {
		leftCorner.bottom = leftCorner.top + leftCorner.Width();
		rightCorner.top = rightCorner.bottom - rightCorner.Width();
		barRect.top += ceilf(barRect.Width() / 2);
		barRect.bottom -= ceilf(barRect.Width() / 2);
	}

	// fill the background for the corners, exclude the middle bar for now 
	BRegion region(rect);
	region.Exclude(barRect);
	view->ConstrainClippingRegion(&region);

	view->SetHighColor(base);
	view->FillRect(rect);

	// figure out the tints to be used
	float edgeLightTint;
	float edgeShadowTint;
	float frameLightTint;
	float frameShadowTint;
	float fillLightTint;
	float fillShadowTint;

	if (flags & B_DISABLED) {
		edgeLightTint = 1.0;
		edgeShadowTint = 1.0;
		frameLightTint = 1.20;
		frameShadowTint = 1.25;
		fillLightTint = 0.9;
		fillShadowTint = 1.05;

		fillColor.red = uint8(fillColor.red * 0.4 + base.red * 0.6);
		fillColor.green = uint8(fillColor.green * 0.4 + base.green * 0.6);
		fillColor.blue = uint8(fillColor.blue * 0.4 + base.blue * 0.6);
	} else {
		edgeLightTint = 0.65;
		edgeShadowTint = 1.07;
		frameLightTint = 1.40;
		frameShadowTint = 1.50;
		fillLightTint = 0.8;
		fillShadowTint = 1.1;
	}

	rgb_color edgeLightColor = tint_color(base, edgeLightTint);
	rgb_color edgeShadowColor = tint_color(base, edgeShadowTint);
	rgb_color frameLightColor = tint_color(fillColor, frameLightTint);
	rgb_color frameShadowColor = tint_color(fillColor, frameShadowTint);
	rgb_color fillLightColor = tint_color(fillColor, fillLightTint);
	rgb_color fillShadowColor = tint_color(fillColor, fillShadowTint);

	if (orientation == B_HORIZONTAL) {
		_DrawRoundBarCorner(view, leftCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 1.0, 1.0, 0.0, -1.0, orientation);
	
		_DrawRoundBarCorner(view, rightCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 0.0, 1.0, -1.0, -1.0, orientation);
	} else {
		_DrawRoundBarCorner(view, leftCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 1.0, 1.0, -1.0, 0.0, orientation);
	
		_DrawRoundBarCorner(view, rightCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 1.0, 0.0, -1.0, -1.0, orientation);
	}

	view->ConstrainClippingRegion(NULL);

	view->BeginLineArray(4);
	if (orientation == B_HORIZONTAL) {
		view->AddLine(barRect.LeftTop(), barRect.RightTop(), edgeShadowColor);
		view->AddLine(barRect.LeftBottom(), barRect.RightBottom(),
			edgeLightColor);
		barRect.InsetBy(0, 1);
		view->AddLine(barRect.LeftTop(), barRect.RightTop(), frameShadowColor);
		view->AddLine(barRect.LeftBottom(), barRect.RightBottom(),
			frameLightColor);
		barRect.InsetBy(0, 1);
	} else {
		view->AddLine(barRect.LeftTop(), barRect.LeftBottom(), edgeShadowColor);
		view->AddLine(barRect.RightTop(), barRect.RightBottom(),
			edgeLightColor);
		barRect.InsetBy(1, 0);
		view->AddLine(barRect.LeftTop(), barRect.LeftBottom(), frameShadowColor);
		view->AddLine(barRect.RightTop(), barRect.RightBottom(),
			frameLightColor);
		barRect.InsetBy(1, 0);
	}
	view->EndLineArray();

	_FillGradient(view, barRect, fillColor, fillShadowTint, fillLightTint,
		orientation);
}


void
BControlLook::DrawSliderThumb(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	// figure out frame color
	rgb_color frameLightColor;
	rgb_color frameShadowColor;
	rgb_color shadowColor = (rgb_color){ 0, 0, 0, 60 };

	if (flags & B_FOCUSED) {
		// focused
		frameLightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		frameShadowColor = frameLightColor;
	} else {
		// figure out the tints to be used
		float frameLightTint;
		float frameShadowTint;
	
		if (flags & B_DISABLED) {
			frameLightTint = 1.30;
			frameShadowTint = 1.35;
			shadowColor.alpha = 30;
		} else {
			frameLightTint = 1.6;
			frameShadowTint = 1.65;
		}
	
		frameLightColor = tint_color(base, frameLightTint);
		frameShadowColor = tint_color(base, frameShadowTint);
	}

	BRect originalRect(rect);
	rect.right--;
	rect.bottom--;

	_DrawFrame(view, rect, frameLightColor, frameLightColor,
		frameShadowColor, frameShadowColor);

	flags &= ~B_ACTIVATED;
	DrawButtonBackground(view, rect, updateRect, base, flags);

	// thumb shadow
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(shadowColor);
	originalRect.left++;
	originalRect.top++;
	view->StrokeLine(originalRect.LeftBottom(), originalRect.RightBottom());
	originalRect.bottom--;
	view->StrokeLine(originalRect.RightTop(), originalRect.RightBottom());

	// thumb edge
	if (orientation == B_HORIZONTAL) {
		rect.InsetBy(0, floorf(rect.Height() / 4));
		rect.left = floorf((rect.left + rect.right) / 2);
		rect.right = rect.left + 1;
		shadowColor = tint_color(base, B_DARKEN_2_TINT);
		shadowColor.alpha = 128;
		view->SetHighColor(shadowColor);
		view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
		rgb_color lightColor = tint_color(base, B_LIGHTEN_2_TINT);
		lightColor.alpha = 128;
		view->SetHighColor(lightColor);
		view->StrokeLine(rect.RightTop(), rect.RightBottom());
	} else {
		rect.InsetBy(floorf(rect.Width() / 4), 0);
		rect.top = floorf((rect.top + rect.bottom) / 2);
		rect.bottom = rect.top + 1;
		shadowColor = tint_color(base, B_DARKEN_2_TINT);
		shadowColor.alpha = 128;
		view->SetHighColor(shadowColor);
		view->StrokeLine(rect.LeftTop(), rect.RightTop());
		rgb_color lightColor = tint_color(base, B_LIGHTEN_2_TINT);
		lightColor.alpha = 128;
		view->SetHighColor(lightColor);
		view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
	}

	view->SetDrawingMode(B_OP_COPY);
}


void
BControlLook::DrawSliderTriangle(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	enum orientation orientation)
{
	DrawSliderTriangle(view, rect, updateRect, base, base, flags, orientation);
}


void
BControlLook::DrawSliderTriangle(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& fill,
	uint32 flags, enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	// figure out frame color
	rgb_color frameLightColor;
	rgb_color frameShadowColor;
	rgb_color shadowColor = (rgb_color){ 0, 0, 0, 60 };

	float topTint = 0.49;
	float middleTint1 = 0.62;
	float middleTint2 = 0.76;
	float bottomTint = 0.90;

	if (flags & B_DISABLED) {
		topTint = (topTint + B_NO_TINT) / 2;
		middleTint1 = (middleTint1 + B_NO_TINT) / 2;
		middleTint2 = (middleTint2 + B_NO_TINT) / 2;
		bottomTint = (bottomTint + B_NO_TINT) / 2;
	} else if (flags & B_HOVER) {
		static const float kHoverTintFactor = 0.85;
		topTint *= kHoverTintFactor;
		middleTint1 *= kHoverTintFactor;
		middleTint2 *= kHoverTintFactor;
		bottomTint *= kHoverTintFactor;
	}

	if (flags & B_FOCUSED) {
		// focused
		frameLightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		frameShadowColor = frameLightColor;
	} else {
		// figure out the tints to be used
		float frameLightTint;
		float frameShadowTint;
	
		if (flags & B_DISABLED) {
			frameLightTint = 1.30;
			frameShadowTint = 1.35;
			shadowColor.alpha = 30;
		} else {
			frameLightTint = 1.6;
			frameShadowTint = 1.65;
		}
	
		frameLightColor = tint_color(base, frameLightTint);
		frameShadowColor = tint_color(base, frameShadowTint);
	}

	// make room for the shadow
	rect.right--;
	rect.bottom--;

	uint32 viewFlags = view->Flags();
	view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	float center = (rect.left + rect.right) / 2;

	BShape shape;
	shape.MoveTo(BPoint(rect.left + 0.5, rect.bottom + 0.5));
	shape.LineTo(BPoint(rect.right + 0.5, rect.bottom + 0.5));
	shape.LineTo(BPoint(rect.right + 0.5, rect.bottom - 1 + 0.5));
	shape.LineTo(BPoint(center + 0.5, rect.top + 0.5));
	shape.LineTo(BPoint(rect.left + 0.5, rect.bottom - 1 + 0.5));
	shape.Close();

	view->MovePenTo(BPoint(1, 1));

	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(shadowColor);
	view->StrokeShape(&shape);

	view->MovePenTo(B_ORIGIN);

	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(frameLightColor);
	view->StrokeShape(&shape);

	rect.InsetBy(1, 1);
	shape.Clear();
	shape.MoveTo(BPoint(rect.left, rect.bottom + 1));
	shape.LineTo(BPoint(rect.right + 1, rect.bottom + 1));
	shape.LineTo(BPoint(center + 0.5, rect.top));
	shape.Close();

	BGradientLinear gradient;
	if (flags & B_DISABLED) {
		_MakeGradient(gradient, rect, fill, topTint, bottomTint);
	} else {
		_MakeGlossyGradient(gradient, rect, fill, topTint, middleTint1,
			middleTint2, bottomTint);
	}

	view->FillShape(&shape, gradient);

	view->SetFlags(viewFlags);
}


void
BControlLook::DrawSliderHashMarks(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, int32 count,
	hash_mark_location location, uint32 flags, enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	rgb_color lightColor;
	rgb_color darkColor;

	if (flags & B_DISABLED) {
		lightColor = tint_color(base, 0.9);
		darkColor = tint_color(base, 1.07);
	} else {
		lightColor = tint_color(base, 0.8);
		darkColor = tint_color(base, 1.14);
	}

	int32 hashMarkCount = max_c(count, 2);
		// draw at least two hashmarks at min/max if
		// fHashMarks != B_HASH_MARKS_NONE
	float factor;
	float startPos;
	if (orientation == B_HORIZONTAL) {
		factor = (rect.Width() - 2) / (hashMarkCount - 1);
		startPos = rect.left + 1;
	} else {
		factor = (rect.Height() - 2) / (hashMarkCount - 1);
		startPos = rect.top + 1;
	}

	if (location & B_HASH_MARKS_TOP) {
		view->BeginLineArray(hashMarkCount * 2);

		if (orientation == B_HORIZONTAL) {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(pos, rect.top),
							  BPoint(pos, rect.top + 4), darkColor);
				view->AddLine(BPoint(pos + 1, rect.top),
							  BPoint(pos + 1, rect.top + 4), lightColor);

				pos += factor;
			}
		} else {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(rect.left, pos),
							  BPoint(rect.left + 4, pos), darkColor);
				view->AddLine(BPoint(rect.left, pos + 1),
							  BPoint(rect.left + 4, pos + 1), lightColor);

				pos += factor;
			}
		}

		view->EndLineArray();
	}

	if (location & B_HASH_MARKS_BOTTOM) {

		view->BeginLineArray(hashMarkCount * 2);

		if (orientation == B_HORIZONTAL) {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(pos, rect.bottom - 4),
							  BPoint(pos, rect.bottom), darkColor);
				view->AddLine(BPoint(pos + 1, rect.bottom - 4),
							  BPoint(pos + 1, rect.bottom), lightColor);

				pos += factor;
			}
		} else {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(rect.right - 4, pos),
							  BPoint(rect.right, pos), darkColor);
				view->AddLine(BPoint(rect.right - 4, pos + 1),
							  BPoint(rect.right, pos + 1), lightColor);

				pos += factor;
			}
		}

		view->EndLineArray();
	}
}


void
BControlLook::DrawActiveTab(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, uint32 borders)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	rgb_color edgeShadowColor;
	rgb_color edgeLightColor;
	rgb_color frameShadowColor;
	rgb_color frameLightColor;
	rgb_color bevelShadowColor;
	rgb_color bevelLightColor;
	BGradientLinear fillGradient;
	fillGradient.SetStart(rect.LeftTop() + BPoint(3, 3));
	fillGradient.SetEnd(rect.LeftBottom() + BPoint(3, -3));

	if (flags & B_DISABLED) {
		edgeShadowColor = base;
		edgeLightColor = base;
		frameShadowColor = tint_color(base, 1.30);
		frameLightColor = tint_color(base, 1.25);
		bevelShadowColor = tint_color(base, 1.07);
		bevelLightColor = tint_color(base, 0.8);
		fillGradient.AddColor(tint_color(base, 0.85), 0);
		fillGradient.AddColor(base, 255);
	} else {
		edgeShadowColor = tint_color(base, 1.03);
		edgeLightColor = tint_color(base, 0.80);
		frameShadowColor = tint_color(base, 1.30);
		frameLightColor = tint_color(base, 1.30);
		bevelShadowColor = tint_color(base, 1.07);
		bevelLightColor = tint_color(base, 0.6);
		fillGradient.AddColor(tint_color(base, 0.75), 0);
		fillGradient.AddColor(tint_color(base, 1.03), 255);
	}

	static const float kRoundCornerRadius = 4;

	// left/top corner
	BRect cornerRect(rect);
	cornerRect.right = cornerRect.left + kRoundCornerRadius;
	cornerRect.bottom = cornerRect.top + kRoundCornerRadius;

	BRegion clipping(rect);
	clipping.Exclude(cornerRect);

	_DrawRoundCornerLeftTop(view, cornerRect, updateRect, base, edgeShadowColor,
		frameLightColor, bevelLightColor, fillGradient);

	// left/top corner
	cornerRect.right = rect.right;
	cornerRect.left = cornerRect.right - kRoundCornerRadius;

	clipping.Exclude(cornerRect);

	_DrawRoundCornerRightTop(view, cornerRect, updateRect, base, edgeShadowColor,
		edgeLightColor, frameLightColor, frameShadowColor, bevelLightColor,
		bevelShadowColor, fillGradient);

	// rest of frame and fill
	view->ConstrainClippingRegion(&clipping);

	_DrawFrame(view, rect, edgeShadowColor, edgeShadowColor, edgeLightColor,
		edgeLightColor,
		borders & (B_LEFT_BORDER | B_TOP_BORDER | B_RIGHT_BORDER));
	if ((borders & B_LEFT_BORDER) == 0)
		rect.left++;
	if ((borders & B_RIGHT_BORDER) == 0)
		rect.right--;

	_DrawFrame(view, rect, frameLightColor, frameLightColor, frameShadowColor,
		frameShadowColor, B_LEFT_BORDER | B_TOP_BORDER | B_RIGHT_BORDER);

	_DrawFrame(view, rect, bevelLightColor, bevelLightColor, bevelShadowColor,
		bevelShadowColor);

	view->FillRect(rect, fillGradient);

	view->ConstrainClippingRegion(NULL);
}


void
BControlLook::DrawInactiveTab(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, uint32 borders)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	rgb_color edgeShadowColor;
	rgb_color edgeLightColor;
	rgb_color frameShadowColor;
	rgb_color frameLightColor;
	rgb_color bevelShadowColor;
	rgb_color bevelLightColor;
	BGradientLinear fillGradient;
	fillGradient.SetStart(rect.LeftTop() + BPoint(3, 3));
	fillGradient.SetEnd(rect.LeftBottom() + BPoint(3, -3));

	if (flags & B_DISABLED) {
		edgeShadowColor = base;
		edgeLightColor = base;
		frameShadowColor = tint_color(base, 1.30);
		frameLightColor = tint_color(base, 1.25);
		bevelShadowColor = tint_color(base, 1.07);
		bevelLightColor = tint_color(base, 0.8);
		fillGradient.AddColor(tint_color(base, 0.85), 0);
		fillGradient.AddColor(base, 255);
	} else {
		edgeShadowColor = tint_color(base, 1.03);
		edgeLightColor = tint_color(base, 0.80);
		frameShadowColor = tint_color(base, 1.30);
		frameLightColor = tint_color(base, 1.30);
		bevelShadowColor = tint_color(base, 1.17);
		bevelLightColor = tint_color(base, 1.10);
		fillGradient.AddColor(tint_color(base, 1.12), 0);
		fillGradient.AddColor(tint_color(base, 1.08), 255);
	}

	// active tabs stand out at the top, but this is an inactive tab
	view->SetHighColor(base);
	view->FillRect(BRect(rect.left, rect.top, rect.right, rect.top + 4));
	rect.top += 4;

	// frame and fill
	_DrawFrame(view, rect, edgeShadowColor, edgeShadowColor, edgeLightColor,
		edgeLightColor,
		borders & (B_LEFT_BORDER | B_TOP_BORDER | B_RIGHT_BORDER));

	_DrawFrame(view, rect, frameLightColor, frameLightColor, frameShadowColor,
		frameShadowColor,
		borders & (B_LEFT_BORDER | B_TOP_BORDER | B_RIGHT_BORDER));

	_DrawFrame(view, rect, bevelShadowColor, bevelShadowColor, bevelLightColor,
		bevelLightColor, B_LEFT_BORDER & ~borders);

	view->FillRect(rect, fillGradient);
}


// #pragma mark -


void
BControlLook::DrawBorder(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, border_style border, uint32 flags, uint32 borders)
{
	if (border == B_NO_BORDER)
		return;

	rgb_color scrollbarFrameColor = tint_color(base, B_DARKEN_2_TINT);
	if (flags & B_FOCUSED)
		scrollbarFrameColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if (border == B_FANCY_BORDER)
		_DrawOuterResessedFrame(view, rect, base, 1.0, 1.0, borders);

	_DrawFrame(view, rect, scrollbarFrameColor, scrollbarFrameColor,
		scrollbarFrameColor, scrollbarFrameColor, borders);
}


void
BControlLook::DrawRaisedBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	rgb_color lightColor;
	rgb_color shadowColor;

	if (flags & B_DISABLED) {
		lightColor = base;
		shadowColor = base;
	} else {
		lightColor = tint_color(base, 0.85);
		shadowColor = tint_color(base, 1.07);
	}

	_DrawFrame(view, rect, lightColor, lightColor, shadowColor, shadowColor,
		borders);
}


void
BControlLook::DrawTextControlBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.Intersects(updateRect))
		return;

	rgb_color dark1BorderColor;
	rgb_color dark2BorderColor;
	rgb_color navigationColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if (flags & B_DISABLED) {
		_DrawOuterResessedFrame(view, rect, base, 0.0, 1.0, borders);

		dark1BorderColor = tint_color(base, 1.15);
		dark2BorderColor = tint_color(base, 1.15);
	} else if (flags & B_CLICKED) {
		dark1BorderColor = tint_color(base, 1.50);
		dark2BorderColor = tint_color(base, 1.49);

		_DrawFrame(view, rect,
			dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor);

		dark2BorderColor = dark1BorderColor;
	} else {
		_DrawOuterResessedFrame(view, rect, base, 0.6, 1.0, borders);

		dark1BorderColor = tint_color(base, 1.40);
		dark2BorderColor = tint_color(base, 1.38);
	}

	if ((flags & B_DISABLED) == 0 && (flags & B_FOCUSED)) {
		dark1BorderColor = navigationColor;
		dark2BorderColor = navigationColor;
	}

	_DrawFrame(view, rect,
		dark1BorderColor, dark1BorderColor,
		dark2BorderColor, dark2BorderColor, borders);
}


void
BControlLook::DrawGroupFrame(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 borders)
{
	rgb_color frameColor = tint_color(base, 1.30);
	rgb_color bevelLight = tint_color(base, 0.8);
	rgb_color bevelShadow = tint_color(base, 1.03);

	_DrawFrame(view, rect, bevelShadow, bevelShadow, bevelLight, bevelLight,
		borders);

	_DrawFrame(view, rect, frameColor, frameColor, frameColor, frameColor,
		borders);

	_DrawFrame(view, rect, bevelLight, bevelLight, bevelShadow, bevelShadow,
		borders);
}


void
BControlLook::DrawLabel(BView* view, const char* label, BRect rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	DrawLabel(view, label, rect, updateRect, base, flags,
		DefaultLabelAlignment());
}


void
BControlLook::DrawLabel(BView* view, const char* label, BRect rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	const BAlignment& alignment)
{
	if (!rect.Intersects(updateRect))
		return;

	// setup the text color
	rgb_color color;
	if (base.red + base.green + base.blue > 128 * 3)
		color = tint_color(base, B_DARKEN_MAX_TINT);
	else
		color = tint_color(base, B_LIGHTEN_MAX_TINT);

	if (flags & B_DISABLED) {
		color.red = (uint8)(((int32)base.red + color.red + 1) / 2);
		color.green = (uint8)(((int32)base.green + color.green + 1) / 2);
		color.blue = (uint8)(((int32)base.blue + color.blue + 1) / 2);
	}

	view->SetHighColor(color);
	view->SetDrawingMode(B_OP_OVER);

	// truncate the label if necessary and get the width and height
	BString truncatedLabel(label);
	
	BFont font;
	view->GetFont(&font);

	float width = rect.Width();
	font.TruncateString(&truncatedLabel, B_TRUNCATE_END, width);
	width = font.StringWidth(truncatedLabel.String());

	font_height fontHeight;
	font.GetHeight(&fontHeight);
	float height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);

	// handle alignment
	BPoint location;

	switch (alignment.horizontal) {
	default:
		case B_ALIGN_LEFT:
			location.x = rect.left;
			break;
		case B_ALIGN_RIGHT:
			location.x = rect.right - width;
			break;
		case B_ALIGN_CENTER:
			location.x = (rect.left + rect.right - width) / 2.0f;
			break;
	}

	switch (alignment.vertical) {
		case B_ALIGN_TOP:
			location.y = rect.top + ceilf(fontHeight.ascent);
			break;
		default:
		case B_ALIGN_MIDDLE:
			location.y = floorf((rect.top + rect.bottom - height) / 2.0f + 0.5f)
				+ ceilf(fontHeight.ascent);
			break;
		case B_ALIGN_BOTTOM:
			location.y = rect.bottom - ceilf(fontHeight.descent);
			break;
	}

	view->DrawString(truncatedLabel.String(), location);
}


// #pragma mark -


void
BControlLook::_DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	float contrast, float brightness, uint32 flags, uint32 borders)
{
	// colors
	rgb_color dark1BorderColor;
	rgb_color dark2BorderColor;

	if ((flags & B_DISABLED) == 0) {
		dark1BorderColor = tint_color(base, 1.33);
		dark2BorderColor = tint_color(base, 1.45);

		if (flags & B_DEFAULT_BUTTON) {
			dark2BorderColor = tint_color(dark1BorderColor, 1.5);
			dark1BorderColor = tint_color(dark1BorderColor, 1.35);
		}
	} else {
		dark1BorderColor = tint_color(base, 1.147);
		dark2BorderColor = tint_color(base, 1.24);

		if (flags & B_DEFAULT_BUTTON) {
			dark1BorderColor = tint_color(dark1BorderColor, 1.14);
			dark2BorderColor = tint_color(dark1BorderColor, 1.12);
		}
	}

	if (flags & B_ACTIVATED) {
		rgb_color temp = dark2BorderColor;
		dark2BorderColor = dark1BorderColor;
		dark1BorderColor = temp;
	}

	// indicate focus by changing main button border
	if (flags & B_FOCUSED) {
		dark1BorderColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		dark2BorderColor = dark1BorderColor;
	}

	if (flags & B_DEFAULT_BUTTON) {
		float focusTint = 1.2;
		if (flags & B_DISABLED)
			focusTint = (B_NO_TINT + focusTint) / 2;

		rgb_color focusColor = tint_color(base, focusTint);
		view->SetHighColor(base);

		view->StrokeRect(rect);
		rect.InsetBy(1.0, 1.0);

		view->SetHighColor(focusColor);
		view->StrokeRect(rect);
		rect.InsetBy(1.0, 1.0);
		view->StrokeRect(rect);
		rect.InsetBy(1.0, 1.0);

		// bevel around external border
		_DrawOuterResessedFrame(view, rect, focusColor,
			contrast * (((flags & B_DISABLED) ? 0.3 : 0.8)),
			brightness * (((flags & B_DISABLED) ? 1.0 : 0.9)),
			borders);
	} else {
		// bevel around external border
		_DrawOuterResessedFrame(view, rect, base,
			contrast * ((flags & B_DISABLED) ? 0.0 : 1.0), brightness * 1.0,
			borders);
	}

	_DrawFrame(view, rect, dark1BorderColor, dark1BorderColor,
		dark2BorderColor, dark2BorderColor, borders);
}


void
BControlLook::_DrawOuterResessedFrame(BView* view, BRect& rect,
	const rgb_color& base, float contrast, float brightness, uint32 borders)
{
	// colors
	float tintLight = kEdgeBevelLightTint;
	float tintShadow = kEdgeBevelShadowTint;

	if (contrast == 0.0) {
		tintLight = B_NO_TINT;
		tintShadow = B_NO_TINT;
	} else if (contrast != 1.0) {
		tintLight = B_NO_TINT + (tintLight - B_NO_TINT) * contrast;
		tintShadow = B_NO_TINT + (tintShadow - B_NO_TINT) * contrast;
	}

	rgb_color borderBevelShadow = tint_color(base, tintShadow);
	rgb_color borderBevelLight = tint_color(base, tintLight);

	if (brightness < 1.0) {
		borderBevelShadow.red = uint8(borderBevelShadow.red * brightness);
		borderBevelShadow.green = uint8(borderBevelShadow.green * brightness);
		borderBevelShadow.blue = uint8(borderBevelShadow.blue * brightness);
		borderBevelLight.red = uint8(borderBevelLight.red * brightness);
		borderBevelLight.green = uint8(borderBevelLight.green * brightness);
		borderBevelLight.blue = uint8(borderBevelLight.blue * brightness);
	}

	_DrawFrame(view, rect, borderBevelShadow, borderBevelShadow,
		borderBevelLight, borderBevelLight, borders);
}


void
BControlLook::_DrawFrame(BView* view, BRect& rect, const rgb_color& left,
	const rgb_color& top, const rgb_color& right, const rgb_color& bottom,
	uint32 borders)
{
	view->BeginLineArray(4);

	if (borders & B_LEFT_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.top), left);
		rect.left++;
	}
	if (borders & B_TOP_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.top),
			BPoint(rect.right, rect.top), top);
		rect.top++;
	}
	if (borders & B_RIGHT_BORDER) {
		view->AddLine(
			BPoint(rect.right, rect.top),
			BPoint(rect.right, rect.bottom), right);
		rect.right--;
	}
	if (borders & B_BOTTOM_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.right, rect.bottom), bottom);
		rect.bottom--;
	}

	view->EndLineArray();
}


void
BControlLook::_DrawFrame(BView* view, BRect& rect, const rgb_color& left,
	const rgb_color& top, const rgb_color& right, const rgb_color& bottom,
	const rgb_color& rightTop, const rgb_color& leftBottom, uint32 borders)
{
	view->BeginLineArray(6);

	if (borders & B_TOP_BORDER) {
		if (borders & B_RIGHT_BORDER) {
			view->AddLine(
				BPoint(rect.left, rect.top),
				BPoint(rect.right - 1, rect.top), top);
			view->AddLine(
				BPoint(rect.right, rect.top),
				BPoint(rect.right, rect.top), rightTop);
		} else {
			view->AddLine(
				BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), top);
		}
		rect.top++;
	}

	if (borders & B_LEFT_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.top),
			BPoint(rect.left, rect.bottom - 1), left);
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.bottom), leftBottom);
		rect.left++;
	}

	if (borders & B_BOTTOM_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.right, rect.bottom), bottom);
		rect.bottom--;
	}

	if (borders & B_RIGHT_BORDER) {
		view->AddLine(
			BPoint(rect.right, rect.bottom),
			BPoint(rect.right, rect.top), right);
		rect.right--;
	}

	view->EndLineArray();
}


//void
//BControlLook::_DrawShadowFrame(BView* view, BRect& rect, const rgb_color& base,
//	uint32 borders)
//{
//	view->BeginLineArray(4);
//
//	bevelColor1 = tint_color(base, 1.2);
//	bevelColor2 = tint_color(base, 1.1);
//
//	// shadow along left/top borders
//	if (rect.Height() > 0 && borders & B_LEFT_BORDER) {
//		view->AddLine(BPoint(rect.left, rect.top),
//			BPoint(rect.left, rect.bottom), bevelColor1);
//		rect.left++;
//	}
//	if (rect.Width() > 0 && borders & B_TOP_BORDER) {
//		view->AddLine(BPoint(rect.left, rect.top),
//			BPoint(rect.right, rect.top), bevelColor1);
//		rect.top++;
//	}
//
//	// softer shadow along left/top borders
//	if (rect.Height() > 0 && borders & B_LEFT_BORDER) {
//		view->AddLine(BPoint(rect.left, rect.top),
//			BPoint(rect.left, rect.bottom), bevelColor2);
//		rect.left++;
//	}
//	if (rect.Width() > 0 && borders & B_TOP_BORDER) {
//		view->AddLine(BPoint(rect.left, rect.top),
//			BPoint(rect.right, rect.top), bevelColor2);
//		rect.top++;
//	}
//
//	view->EndLineArray();
//}


void
BControlLook::_FillGradient(BView* view, const BRect& rect,
	const rgb_color& base, float topTint, float bottomTint,
	enum orientation orientation)
{
	BGradientLinear gradient;
	_MakeGradient(gradient, rect, base, topTint, bottomTint, orientation);
	view->FillRect(rect, gradient);
}


void
BControlLook::_FillGlossyGradient(BView* view, const BRect& rect,
	const rgb_color& base, float topTint, float middle1Tint,
	float middle2Tint, float bottomTint, enum orientation orientation)
{
	BGradientLinear gradient;
	_MakeGlossyGradient(gradient, rect, base, topTint, middle1Tint,
		middle2Tint, bottomTint, orientation);
	view->FillRect(rect, gradient);
}


void
BControlLook::_MakeGradient(BGradientLinear& gradient, const BRect& rect,
	const rgb_color& base, float topTint, float bottomTint,
	enum orientation orientation) const
{
	gradient.AddColor(tint_color(base, topTint), 0);
	gradient.AddColor(tint_color(base, bottomTint), 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());
}


void
BControlLook::_MakeGlossyGradient(BGradientLinear& gradient, const BRect& rect,
	const rgb_color& base, float topTint, float middle1Tint,
	float middle2Tint, float bottomTint,
	enum orientation orientation) const
{
	gradient.AddColor(tint_color(base, topTint), 0);
	gradient.AddColor(tint_color(base, middle1Tint), 132);
	gradient.AddColor(tint_color(base, middle2Tint), 136);
	gradient.AddColor(tint_color(base, bottomTint), 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());
}


bool
BControlLook::_RadioButtonAndCheckBoxMarkColor(const rgb_color& base,
	rgb_color& color, uint32 flags) const
{
	if ((flags & (B_ACTIVATED | B_CLICKED)) == 0) {
		// no mark to be drawn at all
		return false;
	}

	// TODO: Get from UI settings
	color.red = 27;
	color.green = 82;
	color.blue = 140;

	float mix = 1.0;

	if (flags & B_DISABLED) {
		// activated, but disabled
		mix = 0.4;
	} else if (flags & B_CLICKED) {
		if (flags & B_ACTIVATED) {
			// loosing activation
			mix = 0.7;
		} else {
			// becoming activated
			mix = 0.3;
		}
	} else {
		// simply activated
	}

	color.red = uint8(color.red * mix + base.red * (1.0 - mix));
	color.green = uint8(color.green * mix + base.green * (1.0 - mix));
	color.blue = uint8(color.blue * mix + base.blue * (1.0 - mix));

	return true;
}


void
BControlLook::_DrawRoundBarCorner(BView* view, BRect& rect,
	const BRect& updateRect,
	const rgb_color& edgeLightColor, const rgb_color& edgeShadowColor,
	const rgb_color& frameLightColor, const rgb_color& frameShadowColor,
	const rgb_color& fillLightColor, const rgb_color& fillShadowColor,
	float leftInset, float topInset, float rightInset, float bottomInset,
	enum orientation orientation)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	BGradientLinear gradient;
	gradient.AddColor(edgeShadowColor, 0);
	gradient.AddColor(edgeLightColor, 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());

	view->FillEllipse(rect, gradient);

	rect.left += leftInset;
	rect.top += topInset;
	rect.right += rightInset;
	rect.bottom += bottomInset;

	gradient.MakeEmpty();
	gradient.AddColor(frameShadowColor, 0);
	gradient.AddColor(frameLightColor, 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());

	view->FillEllipse(rect, gradient);

	rect.left += leftInset;
	rect.top += topInset;
	rect.right += rightInset;
	rect.bottom += bottomInset;

	gradient.MakeEmpty();
	gradient.AddColor(fillShadowColor, 0);
	gradient.AddColor(fillLightColor, 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());

	view->FillEllipse(rect, gradient);
}


void
BControlLook::_DrawRoundCornerLeftTop(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& edgeColor,
	const rgb_color& frameColor, const rgb_color& bevelColor,
	const BGradientLinear& fillGradient)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	BRegion clipping(rect);
	view->ConstrainClippingRegion(&clipping);

	// background
	view->SetHighColor(base);
	view->FillRect(rect);

	// outer edge
	BRect ellipseRect(rect);
	ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
	ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

	view->SetHighColor(edgeColor);
	view->FillEllipse(ellipseRect);

	// frame
	ellipseRect.InsetBy(1, 1);
	view->SetHighColor(frameColor);
	view->FillEllipse(ellipseRect);

	// bevel
	ellipseRect.InsetBy(1, 1);
	view->SetHighColor(bevelColor);
	view->FillEllipse(ellipseRect);

	// fill
	ellipseRect.InsetBy(1, 1);
	view->FillEllipse(ellipseRect, fillGradient);

	view->ConstrainClippingRegion(NULL);
}

void
BControlLook::_DrawRoundCornerRightTop(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& edgeTopColor, const rgb_color& edgeRightColor,
	const rgb_color& frameTopColor, const rgb_color& frameRightColor,
	const rgb_color& bevelTopColor, const rgb_color& bevelRightColor,
	const BGradientLinear& fillGradient)
{
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	BRegion clipping(rect);
	view->ConstrainClippingRegion(&clipping);

	// background
	view->SetHighColor(base);
	view->FillRect(rect);

	// outer edge
	BRect ellipseRect(rect);
	ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
	ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

	BGradientLinear gradient;
	gradient.AddColor(edgeTopColor, 0);
	gradient.AddColor(edgeRightColor, 255);
	gradient.SetStart(rect.LeftTop());
	gradient.SetEnd(rect.RightBottom());
	view->FillEllipse(ellipseRect, gradient);

	// frame
	ellipseRect.InsetBy(1, 1);
	rect.right--;
	rect.top++;
	if (frameTopColor == frameRightColor) {
		view->SetHighColor(frameTopColor);
		view->FillEllipse(ellipseRect);
	} else {
		gradient.SetColor(0, frameTopColor);
		gradient.SetColor(1, frameRightColor);
		gradient.SetStart(rect.LeftTop());
		gradient.SetEnd(rect.RightBottom());
		view->FillEllipse(ellipseRect, gradient);
	}

	// bevel
	ellipseRect.InsetBy(1, 1);
	rect.right--;
	rect.top++;
	gradient.SetColor(0, bevelTopColor);
	gradient.SetColor(1, bevelRightColor);
	gradient.SetStart(rect.LeftTop());
	gradient.SetEnd(rect.RightBottom());
	view->FillEllipse(ellipseRect, gradient);

	// fill
	ellipseRect.InsetBy(1, 1);
	view->FillEllipse(ellipseRect, fillGradient);

	view->ConstrainClippingRegion(NULL);
}


// NOTE: May come from a add-on in the future. Initialized in
// InterfaceDefs.cpp
BControlLook* be_control_look = NULL;

} // namespace BPrivate
