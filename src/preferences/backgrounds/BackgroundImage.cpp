/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

//  Classes used for setting up and managing background images
//

#include <Bitmap.h>
#include <Node.h>
#include <TranslationKit.h>
#include <View.h>
#include <Window.h>
#include <Message.h>
#include <Entry.h>
#include <Path.h>
#include <Screen.h>
#include <String.h>

#include <fs_attr.h>
#include <stdlib.h>

#include "BackgroundImage.h"
#include "BackgroundsView.h"

const char *kBackgroundImageInfo 			= "be:bgndimginfo";
const char *kBackgroundImageInfoOffset 		= "be:bgndimginfooffset";
const char *kBackgroundImageInfoEraseText	= "be:bgndimginfoerasetext";
const char *kBackgroundImageInfoMode 		= "be:bgndimginfomode";
const char *kBackgroundImageInfoWorkspaces 	= "be:bgndimginfoworkspaces";
const char *kBackgroundImageInfoPath 		= "be:bgndimginfopath";
const char *kBackgroundImageInfoSet 		= "be:bgndimginfoset";
const char *kBackgroundImageInfoCacheMode	= "be:bgndimginfocachemode";
const char *kBackgroundImageSetPeriod		= "be:bgndimgsetperiod";
const char *kBackgroundImageRandomChange	= "be:bgndimgrandomchange";
const char *kBackgroundImageCacheMode		= "be:bgndimgcachemode";

BackgroundImage *
BackgroundImage::GetBackgroundImage(const BNode *node, bool isDesktop, 
	BackgroundsView* view)
{
	BackgroundImage *result = new BackgroundImage(node, isDesktop, view);
	attr_info info;
	if (node->GetAttrInfo(kBackgroundImageInfo, &info) != B_OK)
		return result;

	BMessage container;
	char *buffer = new char [info.size];

	status_t error = node->ReadAttr(kBackgroundImageInfo, info.type, 0, buffer, 
		(size_t)info.size);
	if (error == info.size)
		error = container.Unflatten(buffer);

	delete [] buffer;

	if (error != B_OK)
		return NULL;

	PRINT_OBJECT(container);

	uint32 imageSetPeriod = 0;
	uint32 globalCacheMode = 0;
	bool randomChange = false;
	uint32 maxImageSet = 0;

	if (isDesktop) {
		container.FindInt32(kBackgroundImageSetPeriod, (int32 *)&imageSetPeriod);
		container.FindInt32(kBackgroundImageCacheMode, (int32 *)&globalCacheMode);
		container.FindBool(kBackgroundImageRandomChange, &randomChange);
	}

	for (int32 index = 0; ; index++) {
		const char *path;
		uint32 workspaces = B_ALL_WORKSPACES;
		Mode mode = kTiled;
		bool eraseTextWidgetBackground = true;
		BPoint offset;
		uint32 imageSet = 0;
		uint32 cacheMode = 0;
		if (container.FindString(kBackgroundImageInfoPath, index, &path) != B_OK)
			break;

		BPath bpath(path);
		int32 imageIndex = view->AddImage(bpath);
		if (imageIndex < 0)
			imageIndex = -imageIndex - 1;

		container.FindInt32(kBackgroundImageInfoWorkspaces, index, 
			(int32 *)&workspaces);
		container.FindInt32(kBackgroundImageInfoMode, index, (int32 *)&mode);
		container.FindBool(kBackgroundImageInfoEraseText, index, 
			&eraseTextWidgetBackground);
		container.FindPoint(kBackgroundImageInfoOffset, index, &offset);

		if (isDesktop) {
			container.FindInt32(kBackgroundImageInfoSet, index, 
				(int32 *)&imageSet);
			container.FindInt32(kBackgroundImageInfoCacheMode, index, 
				(int32 *)&cacheMode);
		}

		BackgroundImage::BackgroundImageInfo *imageInfo = new
			BackgroundImage::BackgroundImageInfo(workspaces, imageIndex, 
				mode, offset, eraseTextWidgetBackground, imageSet, cacheMode);

		//imageInfo->UnloadBitmap(globalCacheMode);

		if (imageSet > maxImageSet)
			maxImageSet = imageSet;

		result->Add(imageInfo);
	}

	if (result) {
		result->fImageSetCount = maxImageSet + 1;
		result->fRandomChange = randomChange;
		result->fImageSetPeriod = imageSetPeriod;
		result->fCacheMode = globalCacheMode;
		if (result->fImageSetCount > 1)
			result->fShowingImageSet = random()%result->fImageSetCount;
	}

	return result;
}


BackgroundImage::BackgroundImageInfo::BackgroundImageInfo(uint32 workspaces,
	int32 imageIndex, Mode mode, BPoint offset, bool eraseTextWidget, 
	uint32 imageSet, uint32 cacheMode)
	:
	fWorkspace(workspaces),
	fImageIndex(imageIndex),
	fMode(mode),
	fOffset(offset),
	fEraseTextWidgetBackground(eraseTextWidget),
	fImageSet(imageSet),
	fCacheMode(cacheMode)
{
}


BackgroundImage::BackgroundImageInfo::~BackgroundImageInfo()
{
}


//	#pragma mark -


BackgroundImage::BackgroundImage(const BNode *node, bool desktop, BackgroundsView* view)
	:
	fIsDesktop(desktop),
	fDefinedByNode(*node),
	fView(NULL),
	fBackgroundsView(view),
	fShowingBitmap(NULL),
	fBitmapForWorkspaceList(1, true),
	fImageSetPeriod(0),
	fShowingImageSet(0),
	fImageSetCount(0),
	fCacheMode(0),
	fRandomChange(false)
{
}


BackgroundImage::~BackgroundImage()
{
}


void 
BackgroundImage::Add(BackgroundImageInfo *info)
{
	fBitmapForWorkspaceList.AddItem(info);
}


void
BackgroundImage::RemoveAll()
{
	for (int32 index = 0; index < fBitmapForWorkspaceList.CountItems();) {
		BackgroundImageInfo *info = fBitmapForWorkspaceList.ItemAt(index);
		if (info->fImageSet != fShowingImageSet)
			index++;
		else
			fBitmapForWorkspaceList.RemoveItemAt(index);
	}
}


void 
BackgroundImage::Show(BView *view, int32 workspace)
{
	fView = view;

	BackgroundImageInfo *info = ImageInfoForWorkspace(workspace);
	if (info) {
		/*BPoseView *poseView = dynamic_cast<BPoseView *>(fView);
		if (poseView)
			poseView->SetEraseWidgetTextBackground(info->fEraseTextWidgetBackground);*/
		Show(info, fView);
	}
}


void 
BackgroundImage::Show(BackgroundImageInfo *info, BView *view)
{
	BBitmap *bitmap = fBackgroundsView->GetImage(info->fImageIndex)->GetBitmap();	

	if (!bitmap)
		return;

	BRect viewBounds(view->Bounds());

	display_mode mode;
	BScreen().GetMode(&mode);
	float x_ratio = viewBounds.Width() / mode.virtual_width;
	float y_ratio = viewBounds.Height() / mode.virtual_height;

	BRect bitmapBounds(bitmap->Bounds());
	BRect destinationBitmapBounds(bitmapBounds);
	destinationBitmapBounds.right *= x_ratio;
	destinationBitmapBounds.bottom *= y_ratio;
	BPoint offset(info->fOffset);
	offset.x *= x_ratio;
	offset.y *= y_ratio;

	uint32 tile = 0;
	uint32 followFlags = B_FOLLOW_TOP | B_FOLLOW_LEFT;

	// figure out the display mode and the destination bounds for the bitmap
	switch (info->fMode) {
		case kCentered:
			if (fIsDesktop) {
				destinationBitmapBounds.OffsetBy(
					(viewBounds.Width() - destinationBitmapBounds.Width()) / 2,
					(viewBounds.Height() - destinationBitmapBounds.Height()) / 2);
				break;
			}
			// else fall thru
		case kScaledToFit:
			if (fIsDesktop) {
				destinationBitmapBounds = viewBounds;
				followFlags = B_FOLLOW_ALL;
				break;
			}
			// else fall thru
		case kAtOffset:
			{
				destinationBitmapBounds.OffsetTo(offset);
				break;
			}
		case kTiled:
			// Original Backgrounds Preferences center the tiled paper but the Tracker don't do that
			//if (fIsDesktop) {
				destinationBitmapBounds.OffsetBy(
					(viewBounds.Width() - destinationBitmapBounds.Width()) / 2,
					(viewBounds.Height() - destinationBitmapBounds.Height()) / 2);
			//}
			tile = B_TILE_BITMAP;
			break;
	}

	// switch to the bitmap and force a redraw
	view->SetViewBitmap(bitmap, bitmapBounds, destinationBitmapBounds,
		followFlags, tile);
	view->Invalidate();

	/*if(fShowingBitmap != info) {
		if(fShowingBitmap)
			fShowingBitmap->UnloadBitmap(fCacheMode);
		fShowingBitmap = info;
	}*/
}


void 
BackgroundImage::Remove()
{
	if (fShowingBitmap) {
		fView->ClearViewBitmap();
		fView->Invalidate();
		/*BPoseView *poseView = dynamic_cast<BPoseView *>(fView);
		// make sure text widgets draw the default way, erasing their background
		if (poseView)
			poseView->SetEraseWidgetTextBackground(true);*/
	}
	fShowingBitmap = NULL;
}


BackgroundImage::BackgroundImageInfo *
BackgroundImage::ImageInfoForWorkspace(int32 workspace) const
{
	uint32 workspaceMask = 1;

	for (; workspace; workspace--)
		workspaceMask *= 2;

	int32 count = fBitmapForWorkspaceList.CountItems();

	// do a simple lookup for the most likely candidate bitmap -
	// pick the imageInfo that is only defined for this workspace over one
	// that supports multiple workspaces
	BackgroundImageInfo *result = NULL;
	for (int32 index = 0; index < count; index++) {
		BackgroundImageInfo *info = fBitmapForWorkspaceList.ItemAt(index);
		if (info->fImageSet != fShowingImageSet)
			continue;

		if (fIsDesktop) {
			if (info->fWorkspace == workspaceMask)
				return info;
			if (info->fWorkspace & workspaceMask)
				result = info;
		} else
			return info;
	}
	
	return result;
}


void 
BackgroundImage::WorkspaceActivated(BView *view, int32 workspace, bool state)
{
	if (!fIsDesktop) {
		// we only care for desktop bitmaps
		return;
	}

	if (!state) {
		// we only care comming into a new workspace, not leaving one
		return;
	}

	BackgroundImageInfo *info = ImageInfoForWorkspace(workspace);
	if (info != fShowingBitmap) {
		if (info)
			Show(info, view);
		else {
			/*if (BPoseView *poseView = dynamic_cast<BPoseView *>(view))
				poseView->SetEraseWidgetTextBackground(true);*/
			view->ClearViewBitmap();
			view->Invalidate();
		}
		fShowingBitmap = info;
	}
}


void 
BackgroundImage::ScreenChanged(BRect, color_space)
{
	if (!fIsDesktop || !fShowingBitmap)
		return;

	/*if (fShowingBitmap->fMode == kCentered) {
		BRect viewBounds(fView->Bounds());
		BRect bitmapBounds(fShowingBitmap->fBitmap->Bounds());
		BRect destinationBitmapBounds(bitmapBounds);
		destinationBitmapBounds.OffsetBy(
			(viewBounds.Width() - bitmapBounds.Width()) / 2,
			(viewBounds.Height() - bitmapBounds.Height()) / 2);

		fView->SetViewBitmap(fShowingBitmap->fBitmap, bitmapBounds, destinationBitmapBounds,
			B_FOLLOW_NONE, 0);
		fView->Invalidate();
	}*/
}


status_t
BackgroundImage::SetBackgroundImage(BNode *node)
{
	status_t err;
	BMessage container;
	int32 count = fBitmapForWorkspaceList.CountItems();

	for (int32 index = 0; index < count; index++) {
		BackgroundImageInfo *info = fBitmapForWorkspaceList.ItemAt(index);
		if (fBackgroundsView->GetImage(info->fImageIndex) == NULL)
			continue;

		container.AddBool(kBackgroundImageInfoEraseText,
			info->fEraseTextWidgetBackground);
		container.AddString(kBackgroundImageInfoPath,
			fBackgroundsView->GetImage(info->fImageIndex)->GetPath().Path());
		container.AddInt32(kBackgroundImageInfoWorkspaces, info->fWorkspace);
		container.AddPoint(kBackgroundImageInfoOffset, info->fOffset);
		container.AddInt32(kBackgroundImageInfoMode, info->fMode);

		if (fIsDesktop)
			container.AddInt32(kBackgroundImageInfoSet, info->fImageSet);
	}

	PRINT_OBJECT(container);

	char buffer[container.FlattenedSize()];
	if ((err = container.Flatten(buffer, container.FlattenedSize())) != B_OK)
		return err;

	ssize_t size = node->WriteAttr(kBackgroundImageInfo, 0, 0, buffer,
		container.FlattenedSize());
	if (size <= 0)
		return B_ERROR;

	return B_OK;
}


/*BackgroundImage *
BackgroundImage::Refresh(BackgroundImage *oldBackgroundImage,
	const BNode *fromNode, bool desktop, BPoseView *poseView)
{
	if (oldBackgroundImage) {
		oldBackgroundImage->Remove();
		delete oldBackgroundImage;
	}
	
	BackgroundImage *result = GetBackgroundImage(fromNode, desktop);
	if (result && poseView->ViewMode() != kListMode)
		result->Show(poseView, current_workspace());
	return result;
}


void 
BackgroundImage::ChangeImageSet(BPoseView *poseView)
{
	if(fRandomChange) {
		if(fImageSetCount > 1) {
			uint32 oldShowingImageSet = fShowingImageSet;
			while(oldShowingImageSet == fShowingImageSet)
				fShowingImageSet = random()%fImageSetCount;
		} else
			fShowingImageSet = 0;
	} else {
		fShowingImageSet++;
		if(fShowingImageSet > fImageSetCount - 1)
			fShowingImageSet = 0;
	}
	
	this->Show(poseView, current_workspace());
}*/


//	#pragma mark -


Image::Image(BPath path)
	:
	fBitmap(NULL),
	fPath(path)
{
	name = path.Leaf();
}


Image::~Image()
{
	delete fBitmap;
}


BBitmap*
Image::GetBitmap()
{
	if (!fBitmap)
		fBitmap = BTranslationUtils::GetBitmap(fPath.Path());

	return fBitmap;
}
