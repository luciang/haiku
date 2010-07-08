/*
 * Copyright 2010, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <LayoutItem.h>

#include <Layout.h>
#include <LayoutUtils.h>


BLayoutItem::BLayoutItem()
	:
	fLayout(NULL),
	fLayoutData(NULL)
{
}


BLayoutItem::BLayoutItem(BMessage* from)
	:
	BArchivable(BUnarchiver::PrepareArchive(from)),
	fLayout(NULL),
	fLayoutData(NULL)
{
	BUnarchiver(from).Finish();
}


BLayoutItem::~BLayoutItem()
{
}


BLayout*
BLayoutItem::Layout() const
{
	return fLayout;
}


bool
BLayoutItem::HasHeightForWidth()
{
	// no "height for width" by default
	return false;
}


void
BLayoutItem::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	// no "height for width" by default
}


BView*
BLayoutItem::View()
{
	return NULL;
}


void
BLayoutItem::InvalidateLayout()
{
	if (fLayout)
		fLayout->InvalidateLayout();
}


void*
BLayoutItem::LayoutData() const
{
	return fLayoutData;
}


void
BLayoutItem::SetLayoutData(void* data)
{
	fLayoutData = data;
}


void
BLayoutItem::AlignInFrame(BRect frame)
{
	BSize maxSize = MaxSize();
	BAlignment alignment = Alignment();

	if (HasHeightForWidth()) {
		// The item has height for width, so we do the horizontal alignment
		// ourselves and restrict the height max constraint respectively.
		if (maxSize.width < frame.Width()
			&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
			frame.left += (int)((frame.Width() - maxSize.width)
				* alignment.horizontal);
			frame.right = frame.left + maxSize.width;
		}
		alignment.horizontal = B_ALIGN_USE_FULL_WIDTH;

		float minHeight;
		GetHeightForWidth(frame.Width(), &minHeight, NULL, NULL);
		
		frame.bottom = frame.top + max_c(frame.Height(), minHeight);
		maxSize.height = minHeight;
	}

	SetFrame(BLayoutUtils::AlignInFrame(frame, maxSize, alignment));
}


status_t
BLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BArchivable::Archive(into, deep);

	if (err == B_OK)
		err = archiver.Finish();

	return err;
}


status_t
BLayoutItem::AllArchived(BMessage* into) const
{
	BArchiver archiver(into);
	return BArchivable::AllArchived(into);
}


status_t
BLayoutItem::AllUnarchived(const BMessage* from)
{
	return BArchivable::AllUnarchived(from);
}


void
BLayoutItem::SetLayout(BLayout* layout)
{
	fLayout = layout;
}
