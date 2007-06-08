/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <LayoutUtils.h>

#include <View.h>


// // AddSizesFloat
// float
// BLayoutUtils::AddSizesFloat(float a, float b)
// {
// 	float sum = a + b + 1;
// 	if (sum >= B_SIZE_UNLIMITED)
// 		return B_SIZE_UNLIMITED;
// 	
// 	return sum;
// }
// 
// // AddSizesFloat
// float
// BLayoutUtils::AddSizesFloat(float a, float b, float c)
// {
// 	return AddSizesFloat(AddSizesFloat(a, b), c);
// }


// AddSizesInt32
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b)
{
	if (a >= B_SIZE_UNLIMITED - b)
		return B_SIZE_UNLIMITED;
	return a + b;
}


// AddSizesInt32
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b, int32 c)
{
	return AddSizesInt32(AddSizesInt32(a, b), c);
}


// AddDistances
float
BLayoutUtils::AddDistances(float a, float b)
{
	float sum = a + b + 1;
	if (sum >= B_SIZE_UNLIMITED)
		return B_SIZE_UNLIMITED;
	
	return sum;
}


// AddDistances
float
BLayoutUtils::AddDistances(float a, float b, float c)
{
	return AddDistances(AddDistances(a, b), c);
}


// // SubtractSizesFloat
// float
// BLayoutUtils::SubtractSizesFloat(float a, float b)
// {
// 	if (a < b)
// 		return -1;
// 	return a - b - 1;
// }


// SubtractSizesInt32
int32
BLayoutUtils::SubtractSizesInt32(int32 a, int32 b)
{
	if (a < b)
		return 0;
	return a - b;
}


// SubtractDistances
float
BLayoutUtils::SubtractDistances(float a, float b)
{
	if (a < b)
		return -1;
	return a - b - 1;
}


// FixSizeConstraints
void
BLayoutUtils::FixSizeConstraints(float& min, float& max, float& preferred)
{
	if (max < min)
		max = min;
	if (preferred < min)
		preferred = min;
	else if (preferred > max)
		preferred = max;
}


// FixSizeConstraints
void
BLayoutUtils::FixSizeConstraints(BSize& min, BSize& max, BSize& preferred)
{
	FixSizeConstraints(min.width, max.width, preferred.width);
	FixSizeConstraints(min.height, max.height, preferred.height);
}


// ComposeSize	
BSize
BLayoutUtils::ComposeSize(BSize size, BSize layoutSize)
{
	if (!size.IsWidthSet())
		size.width = layoutSize.width;
	if (!size.IsHeightSet())
		size.height = layoutSize.height;

	return size;
}


// ComposeAlignment
BAlignment
BLayoutUtils::ComposeAlignment(BAlignment alignment, BAlignment layoutAlignment)
{
	if (!alignment.IsHorizontalSet())
		alignment.horizontal = layoutAlignment.horizontal;
	if (!alignment.IsVerticalSet())
		alignment.vertical = layoutAlignment.vertical;

	return alignment;
}


// AlignInFrame
BRect
BLayoutUtils::AlignInFrame(BRect frame, BSize maxSize, BAlignment alignment)
{
	// align according to the given alignment
	if (maxSize.width < frame.Width()
		&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
		frame.left += (int)((frame.Width() - maxSize.width)
			* alignment.RelativeHorizontal());
		frame.right = frame.left + maxSize.width;
	}
	if (maxSize.height < frame.Height()
		&& alignment.vertical != B_ALIGN_USE_FULL_HEIGHT) {
		frame.top += (int)((frame.Height() - maxSize.height)
			* alignment.RelativeVertical());
		frame.bottom = frame.top + maxSize.height;
	}

	return frame;
}


// AlignInFrame
void
BLayoutUtils::AlignInFrame(BView* view, BRect frame)
{
 	BSize maxSize = view->MaxSize();
 	BAlignment alignment = view->Alignment();
 
 	if (view->HasHeightForWidth()) {
 		// The view has height for width, so we do the horizontal alignment
 		// ourselves and restrict the height max constraint respectively.
 
 		if (maxSize.width < frame.Width()
 			&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
 			frame.OffsetBy(floor((frame.Width() - maxSize.width)
 				* alignment.RelativeHorizontal()), 0);
 			frame.right = frame.left + maxSize.width;
 		}
 		alignment.horizontal = B_ALIGN_USE_FULL_WIDTH;
 
 		float minHeight;
 		float maxHeight;
 		float preferredHeight;
 		view->GetHeightForWidth(frame.Width(), &minHeight, &maxHeight,
 			&preferredHeight);
 		
 		frame.bottom = frame.top + max_c(frame.Height(), minHeight);
 		maxSize.height = minHeight;
 	}
 
 	frame = AlignInFrame(frame, maxSize, alignment);
 	view->MoveTo(frame.LeftTop());
 	view->ResizeTo(frame.Size());
}

