
#ifndef	VIEW_LAYER_H
#define VIEW_LAYER_H

#include <Region.h>
#include <String.h>


class DrawingEngine;
class WindowLayer;

class ViewLayer {
 public:
							ViewLayer(BRect frame,
									  const char* name,
									  uint32 reizeMode,
									  uint32 flags,
									  rgb_color viewColor);

	virtual					~ViewLayer();

	inline	BRect			Frame() const
								{ return fFrame; }
			BRect			Bounds() const;

	inline	rgb_color		ViewColor() const
								{ return fViewColor; }

			void			AttachedToWindow(WindowLayer* window);
			void			DetachedFromWindow();

			// tree stuff
			void			AddChild(ViewLayer* layer);
			bool			RemoveChild(ViewLayer* layer);

	inline	ViewLayer*		Parent() const
								{ return fParent; }

			ViewLayer*		FirstChild() const;
			ViewLayer*		PreviousChild() const;
			ViewLayer*		NextChild() const;
			ViewLayer*		LastChild() const;

			ViewLayer*		TopLayer();

			uint32			CountChildren() const;

			// coordinate conversion
			void			ConvertToTop(BPoint* point) const;
			void			ConvertToTop(BRect* rect) const;
			void			ConvertToTop(BRegion* region) const; 

			// settings
			void			SetName(const char* string);
	inline	const char*		Name() const
								{ return fName.String(); }

			void			MoveBy(int32 dx, int32 dy);
			void			ResizeBy(int32 dx, int32 dy, BRegion* dirtyRegion);
			void			ScrollBy(int32 dx, int32 dy);

			void			Draw(DrawingEngine* drawingEngine,
								 BRegion* effectiveClipping,
								 bool deep = false);

			bool			IsHidden() const;
			void			Hide();
			void			Show();

			// clipping
			void			RebuildClipping(bool deep = false);
			BRegion&		ScreenClipping() const;

			// debugging
			void			PrintToStream() const;

private:
			void			_InvalidateScreenClipping(bool deep = false);

			BString			fName;
			// area within parent coordinate space
			BRect			fFrame;
			// scrolling offset
			BPoint			fScrollingOffset;

			rgb_color		fViewColor;

			uint32			fResizeMode;
			uint32			fFlags;
			int32			fShowLevel;

			WindowLayer*	fWindow;
			ViewLayer*		fParent;

			ViewLayer*		fFirstChild;
			ViewLayer*		fPreviousSibling;
			ViewLayer*		fNextSibling;
			ViewLayer*		fLastChild;

			// used for traversing the childs
	mutable	ViewLayer*		fCurrentChild;

			// clipping
			BRegion			fLocalClipping;

	mutable	BRegion			fScreenClipping;
	mutable	bool			fScreenClippingValid;

};

#endif // LAYER_H
