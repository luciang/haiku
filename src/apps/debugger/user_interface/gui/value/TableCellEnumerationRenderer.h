/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef TABLE_CELL_ENUMERATION_RENDERER_H
#define TABLE_CELL_ENUMERATION_RENDERER_H


#include <Referenceable.h>

#include "TableCellIntegerRenderer.h"


class TableCellEnumerationRenderer : public TableCellIntegerRenderer {
public:
								TableCellEnumerationRenderer(Config* config);

	virtual	void				RenderValue(Value* value, BRect rect,
									BView* targetView);
	virtual	float				PreferredValueWidth(Value* value,
									BView* targetView);
};


#endif	// TABLE_CELL_ENUMERATION_RENDERER_H
