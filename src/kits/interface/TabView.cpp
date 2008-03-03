/*
 * Copyright (c) 2001-2008, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:	
 *		Marc Flerackers (mflerackers@androme.be)
 *		Jérôme Duval (korli@users.berlios.de)
 */
#include <TabView.h>

#include <string.h>

#include <List.h>
#include <Message.h>
#include <PropertyInfo.h>
#include <Rect.h>
#include <String.h>


static property_info sPropertyList[] = {
	{
		"Selection",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_INT32_TYPE }
	},
	{}
};



BTab::BTab(BView *tabView)
	:
	fEnabled(true),
	fSelected(false),
	fFocus(false),
	fView(tabView)
{
}


BTab::BTab(BMessage *archive)
	:
	fSelected(false),
	fFocus(false),
	fView(NULL)
{
	bool disable;

	if (archive->FindBool("_disable", &disable) != B_OK)
		SetEnabled(true);
	else
		SetEnabled(!disable);
}


BTab::~BTab()
{
	if (!fView)
		return;

	if (fSelected)
		fView->RemoveSelf();

	delete fView;
}


BArchivable *
BTab::Instantiate(BMessage *archive)
{
	if (validate_instantiation(archive, "BTab"))
		return new BTab(archive);

	return NULL;
}


status_t
BTab::Archive(BMessage *archive, bool deep) const
{
	status_t err = BArchivable::Archive(archive, deep);
	if (err != B_OK)
		return err;

	if (!fEnabled)
		err = archive->AddBool("_disable", false);

	return err;
}


status_t
BTab::Perform(uint32 d, void *arg)
{
	return BArchivable::Perform(d, arg);
}


const char *
BTab::Label() const
{
	if (fView)
		return fView->Name();
	else
		return NULL;
}


void
BTab::SetLabel(const char *label)
{
	if (!label || !fView)
		return;

	fView->SetName(label);
}


bool
BTab::IsSelected() const
{
	return fSelected;
}


void
BTab::Select(BView *owner)
{	
	if (!owner || !View() || !owner->Window())
		return;

	owner->AddChild(fView);
	//fView->Show();

	fSelected = true;
}


void
BTab::Deselect()
{
	if (View())
		View()->RemoveSelf();

	fSelected = false;
}


void
BTab::SetEnabled(bool enabled)
{
	fEnabled = enabled;
}


bool
BTab::IsEnabled() const
{
	return fEnabled;
}


void
BTab::MakeFocus(bool inFocus)
{
	fFocus = inFocus;
}


bool
BTab::IsFocus() const
{
	return fFocus;
}


void
BTab::SetView(BView *view)
{
	if (!view || fView == view)
		return;

	if (fView != NULL) {
		fView->RemoveSelf();
		delete fView;
	}
	fView = view;
}


BView *
BTab::View() const
{
	return fView;
}


void
BTab::DrawFocusMark(BView *owner, BRect frame)
{
	float width = owner->StringWidth(Label());

	owner->SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
	// TODO: remove offset
	float offset = frame.Height() / 2.0;
	owner->StrokeLine(BPoint((frame.left + frame.right - width + offset) / 2.0,
			frame.bottom - 3),
		BPoint((frame.left + frame.right + width + offset) / 2.0,
			frame.bottom - 3));
}


void
BTab::DrawLabel(BView *owner, BRect frame)
{
	if (Label() == NULL)
		return;

	BString label = Label();
	float frameWidth = frame.Width();
	float width = owner->StringWidth(label.String());
	font_height fh;

	if (width > frameWidth) {
		BFont font;
		owner->GetFont(&font);
		font.TruncateString(&label, B_TRUNCATE_END, frameWidth);
		width = frameWidth;
		font.GetHeight(&fh);
	} else {
		owner->GetFontHeight(&fh);
	}

	owner->SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	owner->DrawString(label.String(),
		BPoint((frame.left + frame.right - width) / 2.0,
 			(frame.top + frame.bottom - fh.ascent - fh.descent) / 2.0
 			+ fh.ascent));
}


void
BTab::DrawTab(BView *owner, BRect frame, tab_position position, bool full)
{
	rgb_color no_tint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenmax = tint_color(no_tint, B_LIGHTEN_MAX_TINT);
	rgb_color darken2 = tint_color(no_tint, B_DARKEN_2_TINT);
	rgb_color darken3 = tint_color(no_tint, B_DARKEN_3_TINT);
	rgb_color darken4 = tint_color(no_tint, B_DARKEN_4_TINT);
	rgb_color darkenmax = tint_color(no_tint, B_DARKEN_MAX_TINT);

	owner->SetHighColor(darkenmax);
	owner->SetLowColor(no_tint);
	// NOTE: "frame" goes from the beginning of the left slope to the beginning
	// of the right slope - "lableFrame" is the frame between both slopes
	BRect lableFrame = frame;
	lableFrame.left = lableFrame.left + frame.Height() / 2.0;
	DrawLabel(owner, lableFrame);

	owner->SetDrawingMode(B_OP_OVER);

	owner->BeginLineArray(12);

	int32 slopeWidth = (int32)ceilf(frame.Height() / 2.0);

	if (position != B_TAB_ANY) {
		// full height left side
		owner->AddLine(BPoint(frame.left, frame.bottom),
			BPoint(frame.left + slopeWidth, frame.top), darken3);
		owner->AddLine(BPoint(frame.left, frame.bottom + 1),
			BPoint(frame.left + slopeWidth, frame.top + 1), lightenmax);
	} else {
		// upper half of left side
		owner->AddLine(BPoint(frame.left + slopeWidth / 2,
				frame.bottom - slopeWidth),
			BPoint(frame.left + slopeWidth, frame.top), darken3);
		owner->AddLine(BPoint(frame.left + slopeWidth / 2 + 2,
				frame.bottom - slopeWidth - 1),
			BPoint(frame.left + slopeWidth, frame.top + 1), lightenmax);
	}

	// lines along the top
	owner->AddLine(BPoint(frame.left + slopeWidth, frame.top),
		BPoint(frame.right, frame.top), darken3);
	owner->AddLine(BPoint(frame.left + slopeWidth, frame.top + 1),
		BPoint(frame.right, frame.top + 1), lightenmax);

	if (full) { 
		// full height right side
		owner->AddLine(BPoint(frame.right, frame.top),
			BPoint(frame.right + slopeWidth + 2, frame.bottom), darken2);
		owner->AddLine(BPoint(frame.right, frame.top + 1),
			BPoint(frame.right + slopeWidth + 1, frame.bottom), darken4);
	} else {
		// upper half of right side
		owner->AddLine(BPoint(frame.right, frame.top),
			BPoint(frame.right + slopeWidth / 2 + 1,
				frame.bottom - slopeWidth), darken2);
		owner->AddLine(BPoint(frame.right, frame.top + 1),
			BPoint(frame.right + slopeWidth / 2,
				frame.bottom - slopeWidth), darken4);
	}

	owner->EndLineArray();
}


void BTab::_ReservedTab1() {}
void BTab::_ReservedTab2() {}
void BTab::_ReservedTab3() {}
void BTab::_ReservedTab4() {}
void BTab::_ReservedTab5() {}
void BTab::_ReservedTab6() {}
void BTab::_ReservedTab7() {}
void BTab::_ReservedTab8() {}
void BTab::_ReservedTab9() {}
void BTab::_ReservedTab10() {}
void BTab::_ReservedTab11() {}
void BTab::_ReservedTab12() {}

BTab &BTab::operator=(const BTab &)
{
	// this is private and not functional, but exported
	return *this;
}


//	#pragma mark -


BTabView::BTabView(BRect frame, const char *name, button_width width, 
	uint32 resizingMode, uint32 flags)
	: BView(frame, name, resizingMode, flags)
{
	SetFont(be_bold_font);

	_InitObject();

	fTabWidthSetting = width;
}


BTabView::~BTabView()
{
	for (int32 i = 0; i < CountTabs(); i++) {
		delete TabAt(i);
	}

	delete fTabList;
}


BTabView::BTabView(BMessage *archive)
	: BView(archive),
	fFocus(-1)
{
	fContainerView = NULL;
	fTabList = new BList;

	int16 width;

	if (archive->FindInt16("_but_width", &width) == B_OK)
		fTabWidthSetting = (button_width)width;
	else
		fTabWidthSetting = B_WIDTH_AS_USUAL;

	if (archive->FindFloat("_high", &fTabHeight) != B_OK) {
		font_height fh;
		GetFontHeight(&fh);
		fTabHeight = fh.ascent + fh.descent + fh.leading + 8.0f;
	}

	fFocus = -1;

	if (archive->FindInt32("_sel", &fSelection) != B_OK)
		fSelection = 0;

	if (fContainerView == NULL)
		fContainerView = ChildAt(0);

	int32 i = 0;
	BMessage tabMsg;

	while (archive->FindMessage("_l_items", i, &tabMsg) == B_OK) {
		BArchivable *archivedTab = instantiate_object(&tabMsg);

		if (archivedTab) {
			BTab *tab = dynamic_cast<BTab *>(archivedTab);

			BMessage viewMsg;
			if (archive->FindMessage("_view_list", i, &viewMsg) == B_OK) {
				BArchivable *archivedView = instantiate_object(&viewMsg);
				if (archivedView)
					AddTab(dynamic_cast<BView*>(archivedView), tab);
			}
		}

		tabMsg.MakeEmpty();
		i++;
	}
}


BArchivable *
BTabView::Instantiate(BMessage *archive)
{
	if ( validate_instantiation(archive, "BTabView"))
		return new BTabView(archive);

	return NULL;
}


status_t
BTabView::Archive(BMessage *archive, bool deep) const
{
	if (CountTabs() > 0)
		TabAt(Selection())->View()->RemoveSelf();

	status_t ret = BView::Archive(archive, deep);

	if (ret == B_OK)
		ret = archive->AddInt16("_but_width", fTabWidthSetting);
	if (ret == B_OK)
		ret = archive->AddFloat("_high", fTabHeight);
	if (ret == B_OK)
		ret = archive->AddInt32("_sel", fSelection);

	if (ret == B_OK && deep) {
		for (int32 i = 0; i < CountTabs(); i++) {
			BMessage tabArchive;
			BTab *tab = TabAt(i);

			if (!tab)
				continue;
			ret = tab->Archive(&tabArchive, true);
			if (ret == B_OK)
				ret = archive->AddMessage("_l_items", &tabArchive);

			if (!tab->View())
				continue;

			BMessage viewArchive;
			ret = tab->View()->Archive(&viewArchive, true);
			if (ret == B_OK)
				ret = archive->AddMessage("_view_list", &viewArchive);
		}
	}

	if (CountTabs() > 0) {
		if (TabAt(Selection())->View() && ContainerView())
			TabAt(Selection())->Select(ContainerView());
	}

	return ret;
}


status_t
BTabView::Perform(perform_code d, void *arg)
{
	return BView::Perform(d, arg);
}


void
BTabView::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


void
BTabView::AttachedToWindow()
{
	BView::AttachedToWindow();

	Select(fSelection);
}


void
BTabView::AllAttached()
{
	BView::AllAttached();
}


void
BTabView::AllDetached()
{
	BView::AllDetached();
}


void
BTabView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BTabView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		{
			BMessage reply(B_REPLY);
			bool handled = false;

			BMessage specifier;
			int32 index;
			int32 form;
			const char *property;
			if (message->GetCurrentSpecifier(&index, &specifier, &form, &property) == B_OK) {
				if (strcmp(property, "Selection") == 0) {
					if (message->what == B_GET_PROPERTY) {
						reply.AddInt32("result", fSelection);
						handled = true;
					} else {
						// B_GET_PROPERTY
						int32 selection;
						if (message->FindInt32("data", &selection) == B_OK) {
							Select(selection);
							reply.AddInt32("error", B_OK);
							handled = true;
						}
					}
				}
			}
			
			if (handled)
				message->SendReply(&reply);
			else
				BView::MessageReceived(message);
			break;
		}
		
		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaX = 0.0f;
			float deltaY = 0.0f;
			message->FindFloat("be:wheel_delta_x", &deltaX);
			message->FindFloat("be:wheel_delta_y", &deltaY);
			
			if (deltaX == 0.0f && deltaY == 0.0f)
				return;
			
			if (deltaY == 0.0f)
				deltaY = deltaX;
				
			int32 selection = Selection();
			int32 numTabs = CountTabs();
			if (deltaY > 0  && selection < numTabs - 1) {
				//move to the right tab.
				Select(Selection() + 1);
			} else if (deltaY < 0 && selection > 0 && numTabs > 1) {
				//move to the left tab.
				Select(selection - 1);
			}
		}
		default:
			BView::MessageReceived(message);
			break;	
	}
}


void
BTabView::FrameMoved(BPoint newLocation)
{
	BView::FrameMoved(newLocation);
}


void
BTabView::FrameResized(float width,float height)
{
	BView::FrameResized(width, height);
}


void
BTabView::KeyDown(const char *bytes, int32 numBytes)
{
	if (IsHidden())
		return;

	switch (bytes[0]) {
		case B_DOWN_ARROW:
		case B_LEFT_ARROW: {
			int32 focus = fFocus - 1;
			if (focus < 0)
				focus = CountTabs() - 1;
			SetFocusTab(focus, true);
			break;
		}

		case B_UP_ARROW:
		case B_RIGHT_ARROW: {
			int32 focus = fFocus + 1;
			if (focus >= CountTabs())
				focus = 0;
			SetFocusTab(focus, true);
			break;
		}

		case B_RETURN:
		case B_SPACE:
			Select(FocusTab());
			break;

		default:
			BView::KeyDown(bytes, numBytes);
	}
}


void
BTabView::MouseDown(BPoint point)
{
	if (point.y > fTabHeight)
		return;

	for (int32 i = 0; i < CountTabs(); i++) {
		if (TabFrame(i).Contains(point)
			&& i != Selection()) {
			Select(i);
			return;
		}
	}

	BView::MouseDown(point);
}


void
BTabView::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


void
BTabView::MouseMoved(BPoint point, uint32 transit, const BMessage *message)
{
	BView::MouseMoved(point, transit, message);
}


void
BTabView::Pulse()
{
	BView::Pulse();
}


void
BTabView::Select(int32 index)
{
	if (index < 0 || index >= CountTabs())
		index = Selection();

	BTab *tab = TabAt(Selection());
	if (tab)
		tab->Deselect();

	tab = TabAt(index);
	if (tab && ContainerView()) {
		if(index == 0)
			fTabOffset = 0.0f;
		tab->Select(ContainerView());
		fSelection = index;
	}

	Invalidate();
	
	if(index != 0 && !Bounds().Contains(TabFrame(index))){
		if(!Bounds().Contains(TabFrame(index).LeftTop()))
			fTabOffset += TabFrame(index).left - Bounds().left - 20.0f;
		else
			fTabOffset += TabFrame(index).right - Bounds().right + 20.0f;

		Invalidate();
	}

	/*MakeFocus();
	SetFocusTab(index, true);
	FocusTab();*/
}


int32
BTabView::Selection() const
{
	return fSelection;
}


void
BTabView::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);

	SetFocusTab(Selection(), focused);
}


void
BTabView::SetFocusTab(int32 tab, bool focused)
{
	if (tab >= CountTabs())
		tab = 0;

	if(tab < 0)
		tab = CountTabs() - 1;

	if (focused) {
		if (tab == fFocus)
			return;

		if (fFocus != -1){
			if(TabAt (fFocus) != NULL)
				TabAt(fFocus)->MakeFocus(false);
			Invalidate(TabFrame(fFocus));
		}
		if(TabAt(tab) != NULL){
			TabAt(tab)->MakeFocus(true);
			Invalidate(TabFrame(tab));
			fFocus = tab;
		}
	} else if (fFocus != -1) {
		TabAt(fFocus)->MakeFocus(false);
		Invalidate(TabFrame(fFocus));
		fFocus = -1;
	}
}


int32
BTabView::FocusTab() const
{
	return fFocus;
}


void
BTabView::Draw(BRect updateRect)
{
	DrawBox(DrawTabs());

	if (IsFocus() && fFocus != -1)
		TabAt(fFocus)->DrawFocusMark(this, TabFrame(fFocus));
}


BRect
BTabView::DrawTabs()
{
	for (int32 i = 0; i < CountTabs(); i++) {
		TabAt(i)->DrawTab(this, TabFrame(i),
			i == fSelection ? B_TAB_FRONT : (i == 0) ? B_TAB_FIRST : B_TAB_ANY,
			i + 1 != fSelection);
	}

	if (fSelection < CountTabs())
		return TabFrame(fSelection);

	return BRect();
}


void
BTabView::DrawBox(BRect selTabRect)
{
	BRect rect = Bounds();
	BRect lastTabRect = TabFrame(CountTabs() - 1);

	rgb_color noTint = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color lightenMax = tint_color(noTint, B_LIGHTEN_MAX_TINT);
	rgb_color darken1 = tint_color(noTint, B_DARKEN_1_TINT);
	rgb_color darken2 = tint_color(noTint, B_DARKEN_2_TINT);
	rgb_color darken4 = tint_color(noTint, B_DARKEN_4_TINT);

	BeginLineArray(12);

	int32 offset = (int32)ceilf(selTabRect.Height() / 2.0);

	// outer lines
	AddLine(BPoint(rect.left, rect.bottom - 1),
			BPoint(rect.left, selTabRect.bottom), darken2);
	if (selTabRect.left >= rect.left + 1)
		AddLine(BPoint(rect.left + 1, selTabRect.bottom),
				BPoint(selTabRect.left, selTabRect.bottom), darken2);
	if (lastTabRect.right + offset + 1 <= rect.right - 1)
		AddLine(BPoint(lastTabRect.right + offset + 1, selTabRect.bottom),
				BPoint(rect.right - 1, selTabRect.bottom), darken2);
	AddLine(BPoint(rect.right, selTabRect.bottom + 2),
			BPoint(rect.right, rect.bottom), darken2);
	AddLine(BPoint(rect.right - 1, rect.bottom),
			BPoint(rect.left + 2, rect.bottom), darken2);

	// inner lines
	rect.InsetBy(1, 1);
	selTabRect.bottom += 1;

	AddLine(BPoint(rect.left, rect.bottom - 2),
			BPoint(rect.left, selTabRect.bottom), lightenMax);
	if (selTabRect.left >= rect.left + 1)
		AddLine(BPoint(rect.left + 1, selTabRect.bottom),
				BPoint(selTabRect.left, selTabRect.bottom), lightenMax);
	if (selTabRect.right + offset + 1 <= rect.right - 2)
		AddLine(BPoint(selTabRect.right + offset + 1, selTabRect.bottom),
				BPoint(rect.right - 2, selTabRect.bottom), lightenMax);
	AddLine(BPoint(rect.right, selTabRect.bottom),
			BPoint(rect.right, rect.bottom), darken4);
	AddLine(BPoint(rect.right - 1, rect.bottom),
			BPoint(rect.left, rect.bottom), darken4);

	// soft inner bevel at right/bottom
	rect.right--;
	rect.bottom--;

	AddLine(BPoint(rect.right, selTabRect.bottom + 1),
			BPoint(rect.right, rect.bottom), darken1);
	AddLine(BPoint(rect.right - 1, rect.bottom),
			BPoint(rect.left + 1, rect.bottom), darken1);

	EndLineArray();
}

#define X_OFFSET 0.0f

BRect
BTabView::TabFrame(int32 tab_index) const
{
	// TODO: fix to remove "offset" in DrawTab and DrawLabel ...
	switch (fTabWidthSetting) {
		case B_WIDTH_FROM_LABEL:
		{
			float x = 6.0f;
			for (int32 i = 0; i < tab_index; i++){
				x += StringWidth(TabAt(i)->Label()) + 20.0f;
			}

			return BRect(x - fTabOffset, 0.0f,
				x - fTabOffset + StringWidth(TabAt(tab_index)->Label()) + 20.0f , fTabHeight);

			
			/*float x = X_OFFSET;
			for (int32 i = 0; i < tab_index; i++)
				x += StringWidth(TabAt(i)->Label()) + 20.0f;

			return BRect(x, 0.0f,
				x + StringWidth(TabAt(tab_index)->Label()) + 20.0f, fTabHeight);*/
		}

		case B_WIDTH_FROM_WIDEST:
		{
			float width = 0.0f;

			for (int32 i = 0; i < CountTabs(); i++) {
				float tabWidth = StringWidth(TabAt(i)->Label()) + 20.0f;
				if (tabWidth > width)
					width = tabWidth;
			}
			return BRect((6.0f + tab_index * width) - fTabOffset, 0.0f,
				(6.0f + tab_index * width + width) - fTabOffset, fTabHeight);
			/*float width = 0.0f;

			for (int32 i = 0; i < CountTabs(); i++) {
				float tabWidth = StringWidth(TabAt(i)->Label()) + 20.0f;

				if (tabWidth > width)
					width = tabWidth;
			}

			return BRect(X_OFFSET + tab_index * width, 0.0f,
				X_OFFSET + tab_index * width + width, fTabHeight);*/
		}

		case B_WIDTH_AS_USUAL:
		default:
			return BRect((6.0f + tab_index * 100.0f) - fTabOffset, 0.0f,
						(6.0f + tab_index * 100.0f + 100.0f) - fTabOffset, fTabHeight);
			/*return BRect(X_OFFSET + tab_index * 100.0f, 0.0f,
				X_OFFSET + tab_index * 100.0f + 100.0f, fTabHeight);*/
	}
}


void
BTabView::SetFlags(uint32 flags)
{
	BView::SetFlags(flags);
}


void
BTabView::SetResizingMode(uint32 mode)
{
	BView::SetResizingMode(mode);
}


void
BTabView::GetPreferredSize(float *width, float *height)
{
	BView::GetPreferredSize(width, height);
}


void
BTabView::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


BHandler *
BTabView::ResolveSpecifier(BMessage *message, int32 index,
	BMessage *specifier, int32 what, const char *property)
{
	BPropertyInfo propInfo(sPropertyList);

	if (propInfo.FindMatch(message, 0, specifier, what, property) >= B_OK)
		return this;

	return BView::ResolveSpecifier(message, index, specifier, what,
		property);
}


status_t
BTabView::GetSupportedSuites(BMessage *message)
{
	message->AddString("suites", "suite/vnd.Be-tab-view");

	BPropertyInfo propInfo(sPropertyList);
	message->AddFlat("messages", &propInfo);

	return BView::GetSupportedSuites(message);
}


void
BTabView::AddTab(BView *target, BTab *tab)
{
	if (tab == NULL)
		tab = new BTab(target);
	else
		tab->SetView(target);

	fTabList->AddItem(tab);
}


BTab *
BTabView::RemoveTab(int32 index)
{
	if (index < 0 || index >= CountTabs())
		return NULL;

	BTab *tab = (BTab *)fTabList->RemoveItem(index);
	if (tab == NULL)
		return NULL;

	tab->Deselect();

	if (index <= fSelection && fSelection != 0)
		fSelection--;

	if(CountTabs() == 0)
		fFocus = -1;
	else
		Select(fSelection);

	if (fFocus == CountTabs() - 1 || CountTabs() == 0)
		SetFocusTab(fFocus, false);
	else
		SetFocusTab(fFocus, true);

	return tab;
}


BTab *
BTabView::TabAt(int32 index) const
{
	return (BTab *)fTabList->ItemAt(index);
}


void
BTabView::SetTabWidth(button_width width)
{
	fTabWidthSetting = width;

	Invalidate();
}


button_width
BTabView::TabWidth() const
{
	return fTabWidthSetting;
}


void
BTabView::SetTabHeight(float height)
{
	if (fTabHeight == height)
		return;

	fContainerView->MoveBy(0.0f, height - fTabHeight);
	fContainerView->ResizeBy(0.0f, height - fTabHeight);

	fTabHeight = height;

	Invalidate();
}


float
BTabView::TabHeight() const
{
	return fTabHeight;
}


BView *
BTabView::ContainerView() const
{
	return fContainerView;
}


int32
BTabView::CountTabs() const
{
	return fTabList->CountItems();
}


BView *
BTabView::ViewForTab(int32 tabIndex) const
{
	BTab *tab = TabAt(tabIndex);
	if (tab)
		return tab->View();

	return NULL;
}


void
BTabView::_InitObject()
{
	fTabList = new BList;

	fTabWidthSetting = B_WIDTH_AS_USUAL;
	fSelection = 0;
	fFocus = -1;
	fTabOffset = 0.0f;

	rgb_color color = ui_color(B_PANEL_BACKGROUND_COLOR);

	SetViewColor(color);
	SetLowColor(color);

	font_height fh;
	GetFontHeight(&fh);
	fTabHeight = fh.ascent + fh.descent + fh.leading + 8.0f;

	BRect bounds = Bounds();

	bounds.top += TabHeight();
	bounds.InsetBy(3.0f, 3.0f);

	fContainerView = new BView(bounds, "view container", B_FOLLOW_ALL,
		B_WILL_DRAW);

	fContainerView->SetViewColor(color);
	fContainerView->SetLowColor(color);

	AddChild(fContainerView);
}


void BTabView::_ReservedTabView1() {}
void BTabView::_ReservedTabView2() {}
void BTabView::_ReservedTabView3() {}
void BTabView::_ReservedTabView4() {}
void BTabView::_ReservedTabView5() {}
void BTabView::_ReservedTabView6() {}
void BTabView::_ReservedTabView7() {}
void BTabView::_ReservedTabView8() {}
void BTabView::_ReservedTabView9() {}
void BTabView::_ReservedTabView10() {}
void BTabView::_ReservedTabView11() {}
void BTabView::_ReservedTabView12() {}


BTabView::BTabView(const BTabView &tabView)
	: BView(tabView)
{
	// this is private and not functional, but exported
}


BTabView &BTabView::operator=(const BTabView &)
{
	// this is private and not functional, but exported
	return *this;
}
