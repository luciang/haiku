/*
 * Copyright 2006-2007, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef ICON_VIEW_H
#define ICON_VIEW_H


#include "Icon.h"

#include <View.h>


class BBitmap;

namespace BPrivate {
namespace Icon {
	class IconRenderer;
}
}
using namespace BPrivate::Icon;

class IconView : public BView,
				 public IconListener {
 public:
								IconView(BRect frame, const char* name);
	virtual						~IconView();

	// BView interface
	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);

	// IconListener interface
	virtual	void				AreaInvalidated(const BRect& area);

	// IconView
			void				SetIcon(Icon* icon);
			void				SetIconBGColor(const rgb_color& color);

 private:
			BBitmap*			fBitmap;
			Icon*				fIcon;
			IconRenderer*		fRenderer;
			BRect				fDirtyIconArea;

			double				fScale;
};

#endif // ICON_VIEW_H
