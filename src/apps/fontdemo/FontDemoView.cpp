/*
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Mikael Konradson, mikael.konradson@gmail.com
 */


#include "FontDemoView.h"
#include "messages.h"

#include <Bitmap.h>
#include <Font.h>
#include <Message.h>
#include <Shape.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


FontDemoView::FontDemoView(BRect rect)
	: BView(rect, "FontDemoView", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS),
	fBitmap(NULL),
	fBufferView(NULL),
	fString(NULL),
	fOutLineLevel(0),
	fDrawingMode(B_OP_COPY),
	fBoundingBoxes(false),
	fDrawShapes(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetString("Haiku, inc.");
	SetFontSize(50.0);
	SetAntialiasing(true);

	_NewBitmap(Bounds());
}


FontDemoView::~FontDemoView()
{
	free(fString);
	free(fShapes);

	fBitmap->Lock();
	delete fBitmap;
}


void
FontDemoView::FrameResized(float width, float height)
{
	// TODO: We shouldnt invalidate the whole view when bounding boxes are working as wanted
	Invalidate(/*fBoxRegion.Frame()*/);
	BView::FrameResized(width, height);
}


void
FontDemoView::Draw(BRect updateRect)
{
	BRect rect = Bounds();
	fBufferView = _GetView(rect);
	_DrawView(fBufferView);

	fBufferView->Sync();
	DrawBitmap(fBitmap, rect);
	fBitmap->Unlock();
}


void
FontDemoView::_DrawView(BView* view)
{
	if (!view)
		return;
		
	view->SetDrawingMode(B_OP_COPY);
			

	BRect rect = view->Bounds();
	view->SetHighColor(255, 255, 255);
	view->FillRect(rect);

	if (!fString)
		return;

	view->SetFont(&fFont, B_FONT_ALL);

	const size_t size = strlen(fString);
	BRect boundBoxes[size];

	if (OutLineLevel())
		fFont.GetGlyphShapes(fString, size, fShapes);
	else
		fFont.GetBoundingBoxesAsGlyphs(fString, size, B_SCREEN_METRIC, boundBoxes);

	float escapementArray[size];
	//struct escapement_delta escapeDeltas[size];
	struct edge_info edgeInfo[size];
/*
	for (size_t j = 0; j < size; j++) {
		escapeDeltas[j].nonspace = 0.0f;
		escapeDeltas[j].space = 0.0f;
	}
*/
	fFont.GetEdges(fString, size, edgeInfo);
	fFont.GetEscapements(fString, size, /*escapeDeltas,*/ escapementArray);

	font_height fh;
	fFont.GetHeight(&fh);

	float xCoordArray[size];	
	float yCoordArray[size];

	float yCoord = (rect.Height() + fh.ascent - fh.descent) / 2;
	float xCoord = -rect.Width() / 2;
	const float xCenter = xCoord * -1;
	const float r = Rotation() * (PI/180.0);
	const float cosinus = cos(r);
	const float sinus = -sin(r);

	// When the bounding boxes workes properly we will invalidate only the
	// region area instead of the whole view.		

	fBoxRegion.MakeEmpty();

	for (size_t i = 0; i < size; i++) {
		xCoordArray[i] = 0.0f;
		yCoordArray[i] = 0.0f;

		const float height = boundBoxes[i].Height();
		const float	width = boundBoxes[i].Width();

		yCoordArray[i] = sinus * (xCoord - xCoordArray[i]);
		xCoordArray[i] = cosinus * xCoord;
		
		xCoordArray[i] += xCenter;
		yCoordArray[i] += yCoord;

		boundBoxes[i].OffsetBy(xCoordArray[i], yCoordArray[i]);

		if (OutLineLevel()) {
			view->MovePenTo(xCoordArray[i], yCoordArray[i]);
			view->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
			view->FillShape(fShapes[i]);
			view->SetPenSize(OutLineLevel());
			view->SetHighColor(0, 0, 0);
			view->StrokeShape(fShapes[i]);
		} else {
			view->SetHighColor(0, 0, 0);
			view->SetDrawingMode(fDrawingMode);
			view->DrawChar(fString[i], BPoint(xCoordArray[i], yCoordArray[i]));
		}

		if (BoundingBoxes() && !OutLineLevel()) {
			if (i % 2)
				view->SetHighColor(0, 255, 0);
			else
				view->SetHighColor(255, 0, 0);
			view->SetDrawingMode(B_OP_COPY);
			view->StrokeRect(boundBoxes[i]);
		}

		// add the bounding to the region.
		fBoxRegion.Include(boundBoxes[i]);

		xCoord += (escapementArray[i] /*+ escapeDeltas[i].nonspace + escapeDeltas[i].space*/)
			* FontSize() + Spacing();
		//printf("xCoord %f\n", xCoord);
	}
}


void
FontDemoView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case TEXT_CHANGED_MSG:
		{
			const char* text = NULL;
			if (msg->FindString("_text", &text) == B_OK) {
				SetString(text);
				Invalidate(/*&fBoxRegion*/);
			}
			break;
		}

		case FONTSTYLE_CHANGED_MSG:
		{
			BMessage fontMessage;
			if (msg->FindMessage("_fontMessage", &fontMessage) != B_OK)
				return;

			const char* family;
			const char* style;
			if (fontMessage.FindString("_family", &family) != B_OK
				|| fontMessage.FindString("_style", &style) != B_OK)
				return;

			fFont.SetFamilyAndStyle(family, style);
			Invalidate();
			break;
		}

		case FONTFAMILY_CHANGED_MSG:
		{
			BMessage fontMessage;
			if (msg->FindMessage("_fontMessage", &fontMessage) != B_OK)
				return;

			const char* family;
			if (fontMessage.FindString("_family", &family) != B_OK)
				return;

			font_style style;
			if (get_font_style(const_cast<char*>(family), 0, &style) == B_OK) {
				fFont.SetFamilyAndStyle(family, style);
				Invalidate(/*&fBoxRegion*/);
			}
			break;
		}

		case FONTSIZE_MSG:
		{
			float size = 0.0;
			if (msg->FindFloat("_size", &size) == B_OK) {
				SetFontSize(size);
				Invalidate(/*&fBoxRegion*/);
			}
			break;
		}

		case FONTSHEAR_MSG:
		{
			float shear = 90.0;			
			if (msg->FindFloat("_shear", &shear) == B_OK) {
				SetFontShear(shear);
				Invalidate(/*&fBoxRegion*/);
			 }
			break;
		}

		case ROTATION_MSG:
		{
			float rotation = 0.0;			
			if (msg->FindFloat("_rotation", &rotation) == B_OK) {
				SetFontRotation(rotation);
				Invalidate(/*&fBoxRegion*/);
			 }
			break;
		}

		case SPACING_MSG:
		{
			float space = 0.0;			
			if (msg->FindFloat("_spacing", &space) == B_OK) {
				SetSpacing(space);
				Invalidate(/*&fBoxRegion*/);
			 }
			break;
		}

		case OUTLINE_MSG:
		{
			int8 outline = 0;			
			if (msg->FindInt8("_outline", &outline) == B_OK) {
				SetOutlineLevel(outline);
				Invalidate(/*&fBoxRegion*/);
			 }
			break;
		}	

		case ALIASING_MSG:
		{
			bool aliased = false;
			if (msg->FindBool("_aliased", &aliased) == B_OK) {
				SetAntialiasing(aliased);
				Invalidate(/*&fBoxRegion*/);
			}
			break;
		}
		
		case DRAWINGMODE_CHANGED_MSG:
		{
			if (msg->FindInt32("_mode", (int32 *)&fDrawingMode) == B_OK) {
				Invalidate(/*&fBoxRegion*/);
			}
			break;
		}
		
		case BOUNDING_BOX_MSG:
		{
			bool boundingbox = false;
			if (msg->FindBool("_boundingbox", &boundingbox) == B_OK) {
				SetDrawBoundingBoxes(boundingbox);
				Invalidate(/*&fBoxRegion*/);
			}
			break;
		}
		
		default:
			BView::MessageReceived(msg);
			break;
	}
}


void
FontDemoView::SetString(const char* string)
{
	free(fString);
	fString = strdup(string);
	free(fShapes);
	_AddShapes(fString);
}


const char*
FontDemoView::String() const
{
	return fString;
}


void
FontDemoView::SetFontSize(float size)
{
	fFont.SetSize(size);
}


void
FontDemoView::SetFontShear(float shear)
{
	fFont.SetShear(shear);
}


void
FontDemoView::SetFontRotation(float rotation)
{
	fFont.SetRotation(rotation);
}


void
FontDemoView::SetDrawBoundingBoxes(bool state)
{
	fBoundingBoxes = state;
}


void
FontDemoView::SetAntialiasing(bool state)
{
	fFont.SetFlags(state ? B_FORCE_ANTIALIASING : B_DISABLE_ANTIALIASING);
}


void
FontDemoView::SetSpacing(float space)
{
	fSpacing = space;
}


void
FontDemoView::SetOutlineLevel(int8 outline)
{
	fOutLineLevel = outline;
}


void
FontDemoView::_AddShapes(const char* string)
{
	const size_t size = strlen(string);
	fShapes = (BShape**)malloc(sizeof(BShape*)*size);

	for (size_t i = 0; i < size; i++) {
		fShapes[i] = new BShape();
	}
}


BView*
FontDemoView::_GetView(BRect rect)
{
	if (!fBitmap || fBitmap->Bounds() != rect)
		_NewBitmap(rect);

	fBitmap->Lock();
	return fBitmap->ChildAt(0);
}


void
FontDemoView::_NewBitmap(BRect rect)
{
	delete fBitmap;
	fBitmap = new BBitmap(rect, B_RGB16, true);

	if (fBitmap->Lock()) {
		BView* view = new BView(rect, "", B_FOLLOW_NONE, B_WILL_DRAW);
		fBitmap->AddChild(view);
		fBitmap->Unlock();
	} else {
		delete fBitmap;
		fBitmap = NULL;
	}
}

