/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <GroupView.h>


BGroupView::BGroupView(enum orientation orientation, float spacing)
	: BView(NULL, 0, new BGroupLayout(orientation, spacing))
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


BGroupView::BGroupView(const char* name, enum orientation orientation,
	float spacing)
	: BView(name, 0, new BGroupLayout(orientation, spacing))
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


BGroupView::~BGroupView()
{
}


void
BGroupView::SetLayout(BLayout* layout)
{
	// only BGroupLayouts are allowed
	if (!dynamic_cast<BGroupLayout*>(layout))
		return;

	BView::SetLayout(layout);
}


BGroupLayout*
BGroupView::GroupLayout() const
{
	return dynamic_cast<BGroupLayout*>(GetLayout());
}
