/*
 * Copyright 2004-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Mike Berg <mike@berg-net.us>
 *		Julun <host.haiku@gmx.de>
 *
 */
#ifndef SECTIONEDIT_H
#define SECTIONEDIT_H


#include <Control.h>


class BBitmap;
class BList;


class TSection {
	public:
				TSection(BRect frame)
					: fFrame(frame) {}

		BRect	Bounds() const
				{
					BRect frame(fFrame);
					return frame.OffsetByCopy(B_ORIGIN);
				}

		void 	SetFrame(BRect frame)
				{	fFrame = frame;	}

		BRect	Frame() const
				{	return fFrame;	}

	private:
		BRect 	fFrame;
};


class TSectionEdit: public BControl {
	public:
						TSectionEdit(BRect frame, const char *name,
							uint32 sections);
		virtual			~TSectionEdit();

		virtual void 	AttachedToWindow();
		virtual void 	Draw(BRect updateRect);
		virtual void 	MouseDown(BPoint point);
		virtual void	MakeFocus(bool focused = true);
		virtual void 	KeyDown(const char *bytes, int32 numBytes);

		uint32			CountSections() const;
		int32			FocusIndex() const;
		BRect			SectionArea() const;

	protected:
		virtual void 	InitView();

		// hooks
		virtual void 	DrawBorder(const BRect& updateRect);
		virtual void 	DrawSection(uint32 index, bool isFocus) {}
		virtual void 	DrawSeparator(uint32 index) {}

		virtual void 	SectionFocus(uint32 index) {}
		virtual void 	SectionChange(uint32 index, uint32 value) {}
		virtual void 	SetSections(BRect area) {}
		virtual float 	SeparatorWidth() const;

		virtual void 	DoUpPress() {}
		virtual void 	DoDownPress() {}

		virtual void 	DispatchMessage();
		virtual void 	BuildDispatch(BMessage *message) = 0;

	protected:
		BList 			*fSectionList;

		BRect 			fUpRect;
		BRect 			fDownRect;
		BRect 			fSectionArea;

		int32 			fFocus;
		uint32 			fSectionCount;
		uint32 			fHoldValue;

		bool 			fShowFocus;
};

#endif

