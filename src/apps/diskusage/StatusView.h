/*
 * Copyright (c) 2008 Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 * Distributed under the terms of the MIT/X11 license.
 *
 * Copyright (c) 1999 Mike Steed. You are free to use and distribute this software
 * as long as it is accompanied by it's documentation and this copyright notice.
 * The software comes with no warranty, etc.
 */
#ifndef STATUS_VIEW_H
#define STATUS_VIEW_H


#include <View.h>
#include <StringView.h>
#include <Rect.h>


struct FileInfo;

class StatusView: public BView {
public:
								StatusView(BRect frame);
	virtual						~StatusView();

			void				Show(const FileInfo* info);

private:
			BStringView*		fPathView;
			BStringView*		fSizeView;
			BStringView*		fCountView;
			const FileInfo*		fCurrentFileInfo;
};


#endif // STATUS_VIEW_H
