/*
 * Copyright 2005, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * DrawingMode implementing B_OP_SELECT on B_RGBA32.
 *
 */

#ifndef DRAWING_MODE_SELECT_SUBPIX_H
#define DRAWING_MODE_SELECT_SUBPIX_H

#include "DrawingMode.h"
#include "GlobalSubpixelSettings.h"

// BLEND_SELECT_SUBPIX
#define BLEND_SELECT_SUBPIX(d, r, g, b, a1, a2, a3) \
{ \
	BLEND_SUBPIX(d, r, g, b, a1, a2, a3); \
}


// blend_solid_hspan_select_subpix
void
blend_solid_hspan_select_subpix(int x, int y, unsigned len, const color_type& c,
	const uint8* covers, agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color high = pattern->HighColor();
	rgb_color low = pattern->LowColor();
	rgb_color color;
	const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
	const int subpixelM = 1;
	const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;
	do {
		if (pattern->IsHighColor(x, y)) {
			if (compare(p, high, low, &color)){
				BLEND_SELECT_SUBPIX(p, color.red, color.green, color.blue,
					covers[subpixelL], covers[subpixelM], covers[subpixelR]);
			}
		}
		covers += 3;
		p += 4;
		x++;
		len -= 3;
	} while (len);
}

#endif // DRAWING_MODE_SELECT_SUBPIX_H

