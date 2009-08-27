/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#include "PathListView.h"

#include <new>
#include <stdio.h>

#include <Application.h>
#include <ListItem.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Mime.h>
#include <Window.h>

#include "AddPathsCommand.h"
#include "CleanUpPathCommand.h"
#include "CommandStack.h"
#include "MovePathsCommand.h"
#include "Observer.h"
#include "RemovePathsCommand.h"
#include "ReversePathCommand.h"
#include "RotatePathIndicesCommand.h"
#include "Shape.h"
#include "ShapeContainer.h"
#include "Selection.h"
#include "UnassignPathCommand.h"
#include "Util.h"
#include "VectorPath.h"

using std::nothrow;

static const float kMarkWidth		= 14.0;
static const float kBorderOffset	= 3.0;
static const float kTextOffset		= 4.0;

class PathListItem : public SimpleItem,
					 public Observer {
 public:
					PathListItem(VectorPath* p,
								 PathListView* listView,
								 bool markEnabled)
						: SimpleItem(""),
						  path(NULL),
						  fListView(listView),
						  fMarkEnabled(markEnabled),
						  fMarked(false)
					{
						SetPath(p);
					}

	virtual			~PathListItem()
					{
						SetPath(NULL);
					}

	// SimpleItem interface
	virtual	void	Draw(BView* owner, BRect itemFrame, uint32 flags)
	{
		SimpleItem::DrawBackground(owner, itemFrame, flags);

		// text
		owner->SetHighColor(0, 0, 0, 255);
		font_height fh;
		owner->GetFontHeight(&fh);
		BString truncatedString(Text());
		owner->TruncateString(&truncatedString, B_TRUNCATE_MIDDLE,
							  itemFrame.Width()
							  - kBorderOffset
							  - kMarkWidth
							  - kTextOffset
							  - kBorderOffset);
		float height = itemFrame.Height();
		float textHeight = fh.ascent + fh.descent;
		BPoint pos;
		pos.x = itemFrame.left
					+ kBorderOffset + kMarkWidth + kTextOffset;
		pos.y = itemFrame.top
					 + ceilf((height - textHeight) / 2.0 + fh.ascent);
		owner->DrawString(truncatedString.String(), pos);

		if (!fMarkEnabled)
			return;

		// mark
		BRect markRect = itemFrame;
		markRect.left += kBorderOffset;
		markRect.right = markRect.left + kMarkWidth;
		markRect.top = (markRect.top + markRect.bottom - kMarkWidth) / 2.0;
		markRect.bottom = markRect.top + kMarkWidth;
		owner->SetHighColor(tint_color(owner->LowColor(), B_DARKEN_1_TINT));
		owner->StrokeRect(markRect);
		markRect.InsetBy(1, 1);
		owner->SetHighColor(tint_color(owner->LowColor(), 1.04));
		owner->FillRect(markRect);
		if (fMarked) {
			markRect.InsetBy(2, 2);
			owner->SetHighColor(tint_color(owner->LowColor(),
								B_DARKEN_4_TINT));
			owner->SetPenSize(2);
			owner->StrokeLine(markRect.LeftTop(), markRect.RightBottom());
			owner->StrokeLine(markRect.LeftBottom(), markRect.RightTop());
			owner->SetPenSize(1);
		}
	}

	// Observer interface
	virtual	void	ObjectChanged(const Observable* object)
					{
						UpdateText();
					}

	// PathListItem
			void	SetPath(VectorPath* p)
					{
						if (p == path)
							return;

						if (path) {
							path->RemoveObserver(this);
							path->Release();
						}

						path = p;

						if (path) {
							path->Acquire();
							path->AddObserver(this);
							UpdateText();
						}
					}
			void	UpdateText()
					{
						SetText(path->Name());
						Invalidate();
					}

			void	SetMarkEnabled(bool enabled)
					{
						if (fMarkEnabled == enabled)
							return;
						fMarkEnabled = enabled;
						Invalidate();
					}
			void	SetMarked(bool marked)
					{
						if (fMarked == marked)
							return;
						fMarked = marked;
						Invalidate();
					}

			void Invalidate()
					{
						// :-/
						if (fListView->LockLooper()) {
							fListView->InvalidateItem(
								fListView->IndexOf(this));
							fListView->UnlockLooper();
						}
					}

	VectorPath* 	path;
 private:
	PathListView*	fListView;
	bool			fMarkEnabled;
	bool			fMarked;
};


class ShapePathListener : public PathContainerListener,
						  public ShapeContainerListener {
 public:
	ShapePathListener(PathListView* listView)
		: fListView(listView),
		  fShape(NULL)
	{
	}
	virtual ~ShapePathListener()
	{
		SetShape(NULL);
	}

	// PathContainerListener interface
	virtual void PathAdded(VectorPath* path, int32 index)
	{
		fListView->_SetPathMarked(path, true);
	}
	virtual void PathRemoved(VectorPath* path)
	{
		fListView->_SetPathMarked(path, false);
	}

	// ShapeContainerListener interface
	virtual void ShapeAdded(Shape* shape, int32 index) {}
	virtual void ShapeRemoved(Shape* shape)
	{
		fListView->SetCurrentShape(NULL);
	}

	// ShapePathListener
	void SetShape(Shape* shape)
	{
		if (fShape == shape)
			return;

		if (fShape)
			fShape->Paths()->RemoveListener(this);

		fShape = shape;

		if (fShape)
			fShape->Paths()->AddListener(this);
	}

	Shape* CurrentShape() const
	{
		return fShape;
	}

 private:
	PathListView*	fListView;
	Shape*			fShape;
};

// #pragma mark -

enum {
	MSG_ADD					= 'addp',

	MSG_ADD_RECT			= 'addr',
	MSG_ADD_CIRCLE			= 'addc',
	MSG_ADD_ARC				= 'adda',

	MSG_DUPLICATE			= 'dupp',

	MSG_REVERSE				= 'rvrs',
	MSG_CLEAN_UP			= 'clup',
	MSG_ROTATE_INDICES_CW	= 'ricw',
	MSG_ROTATE_INDICES_CCW	= 'ricc',

	MSG_REMOVE				= 'remp',
};

// constructor
PathListView::PathListView(BRect frame,
						   const char* name,
						   BMessage* message, BHandler* target)
	: SimpleListView(frame, name,
					 NULL, B_SINGLE_SELECTION_LIST),
	  fMessage(message),
	  fMenu(NULL),

	  fPathContainer(NULL),
	  fShapeContainer(NULL),
	  fCommandStack(NULL),

	  fCurrentShape(NULL),
	  fShapePathListener(new ShapePathListener(this))
{
	SetTarget(target);
}

// destructor
PathListView::~PathListView()
{
	_MakeEmpty();
	delete fMessage;

	if (fPathContainer)
		fPathContainer->RemoveListener(this);

	if (fShapeContainer)
		fShapeContainer->RemoveListener(fShapePathListener);

	delete fShapePathListener;
}

// SelectionChanged
void
PathListView::SelectionChanged()
{
	SimpleListView::SelectionChanged();

	if (!fSyncingToSelection) {
		// NOTE: single selection list
		PathListItem* item
			= dynamic_cast<PathListItem*>(ItemAt(CurrentSelection(0)));
		if (fMessage) {
			BMessage message(*fMessage);
			message.AddPointer("path", item ? (void*)item->path : NULL);
			Invoke(&message);
		}
	}

	_UpdateMenu();
}

// MouseDown
void
PathListView::MouseDown(BPoint where)
{
	if (!fCurrentShape) {
		SimpleListView::MouseDown(where);
		return;
	}

	bool handled = false;
	int32 index = IndexOf(where);
	PathListItem* item = dynamic_cast<PathListItem*>(ItemAt(index));
	if (item) {
		BRect itemFrame(ItemFrame(index));
		itemFrame.right = itemFrame.left
							+ kBorderOffset + kMarkWidth
							+ kTextOffset / 2.0;
		VectorPath* path = item->path;
		if (itemFrame.Contains(where) && fCommandStack) {
			// add or remove the path to the shape
			::Command* command;
			if (fCurrentShape->Paths()->HasPath(path)) {
				command = new UnassignPathCommand(
								fCurrentShape, path);
			} else {
				VectorPath* paths[1];
				paths[0] = path;
				command = new AddPathsCommand(
								fCurrentShape->Paths(),
								paths, 1, false,
								fCurrentShape->Paths()->CountPaths());
			}
			fCommandStack->Perform(command);
			handled = true;
		}
	}

	if (!handled)
		SimpleListView::MouseDown(where);
}

// MessageReceived
void
PathListView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_ADD:
			if (fCommandStack != NULL) {
				VectorPath* path;
				AddPathsCommand* command;
				new_path(fPathContainer, &path, &command);
				fCommandStack->Perform(command);
			}
			break;

		case MSG_ADD_RECT:
			if (fCommandStack != NULL) {
				VectorPath* path;
				AddPathsCommand* command;
				new_path(fPathContainer, &path, &command);
				if (path != NULL) {
					path->AddPoint(BPoint(16, 16));
					path->AddPoint(BPoint(16, 48));
					path->AddPoint(BPoint(48, 48));
					path->AddPoint(BPoint(48, 16));
					path->SetClosed(true);
				}
				fCommandStack->Perform(command);
			}
			break;

		case MSG_ADD_CIRCLE:
			// TODO: ask for number of secions
			if (fCommandStack != NULL) {
				VectorPath* path;
				AddPathsCommand* command;
				new_path(fPathContainer, &path, &command);
				if (path != NULL) {
					// add four control points defining a circle:
					//   a 
					// b   d
					//   c
					BPoint a(32, 16);
					BPoint b(16, 32);
					BPoint c(32, 48);
					BPoint d(48, 32);
					
					path->AddPoint(a);
					path->AddPoint(b);
					path->AddPoint(c);
					path->AddPoint(d);
			
					path->SetClosed(true);
			
					float controlDist = 0.552284 * 16;
					path->SetPoint(0, a, a + BPoint(controlDist, 0.0),
										 a + BPoint(-controlDist, 0.0), true);
					path->SetPoint(1, b, b + BPoint(0.0, -controlDist),
										 b + BPoint(0.0, controlDist), true);
					path->SetPoint(2, c, c + BPoint(-controlDist, 0.0),
										 c + BPoint(controlDist, 0.0), true);
					path->SetPoint(3, d, d + BPoint(0.0, controlDist),
										 d + BPoint(0.0, -controlDist), true);
				}
				fCommandStack->Perform(command);
			}
			break;

		case MSG_DUPLICATE:
			if (fCommandStack != NULL) {
				PathListItem* item = dynamic_cast<PathListItem*>(
					ItemAt(CurrentSelection(0)));
				if (item == NULL)
					break;

				VectorPath* path;
				AddPathsCommand* command;
				new_path(fPathContainer, &path, &command, item->path);
				fCommandStack->Perform(command);
			}
			break;

		case MSG_REVERSE:
			if (fCommandStack != NULL) {
				PathListItem* item = dynamic_cast<PathListItem*>(
					ItemAt(CurrentSelection(0)));
				if (item == NULL)
					break;

				ReversePathCommand* command
					= new (nothrow) ReversePathCommand(item->path);
				fCommandStack->Perform(command);
			}
			break;

		case MSG_CLEAN_UP:
			if (fCommandStack != NULL) {
				PathListItem* item = dynamic_cast<PathListItem*>(
					ItemAt(CurrentSelection(0)));
				if (item == NULL)
					break;

				CleanUpPathCommand* command
					= new (nothrow) CleanUpPathCommand(item->path);
				fCommandStack->Perform(command);
			}
			break;

		case MSG_ROTATE_INDICES_CW:
		case MSG_ROTATE_INDICES_CCW:
			if (fCommandStack != NULL) {
				PathListItem* item = dynamic_cast<PathListItem*>(
					ItemAt(CurrentSelection(0)));
				if (item == NULL)
					break;

				RotatePathIndicesCommand* command
					= new (nothrow) RotatePathIndicesCommand(item->path,
					message->what == MSG_ROTATE_INDICES_CW);
				fCommandStack->Perform(command);
			}
			break;

		case MSG_REMOVE:
			RemoveSelected();
			break;

		default:
			SimpleListView::MessageReceived(message);
			break;
	}
}

// MakeDragMessage
void
PathListView::MakeDragMessage(BMessage* message) const
{
	SimpleListView::MakeDragMessage(message);
	message->AddPointer("container", fPathContainer);
	int32 count = CountSelectedItems();
	for (int32 i = 0; i < count; i++) {
		PathListItem* item = dynamic_cast<PathListItem*>(
			ItemAt(CurrentSelection(i)));
		if (item)
			message->AddPointer("path", (void*)item->path);
		else
			break;
	}
}

// AcceptDragMessage
bool
PathListView::AcceptDragMessage(const BMessage* message) const
{
	return SimpleListView::AcceptDragMessage(message);
}

// SetDropTargetRect
void
PathListView::SetDropTargetRect(const BMessage* message, BPoint where)
{
	SimpleListView::SetDropTargetRect(message, where);
}

// MoveItems
void
PathListView::MoveItems(BList& items, int32 toIndex)
{
	if (!fCommandStack || !fPathContainer)
		return;

	int32 count = items.CountItems();
	VectorPath** paths = new (nothrow) VectorPath*[count];
	if (!paths)
		return;

	for (int32 i = 0; i < count; i++) {
		PathListItem* item
			= dynamic_cast<PathListItem*>((BListItem*)items.ItemAtFast(i));
		paths[i] = item ? item->path : NULL;
	}

	MovePathsCommand* command
		= new (nothrow) MovePathsCommand(fPathContainer,
										 paths, count, toIndex);
	if (!command) {
		delete[] paths;
		return;
	}

	fCommandStack->Perform(command);
}

// CopyItems
void
PathListView::CopyItems(BList& items, int32 toIndex)
{
	if (!fCommandStack || !fPathContainer)
		return;

	int32 count = items.CountItems();
	VectorPath* paths[count];

	for (int32 i = 0; i < count; i++) {
		PathListItem* item
			= dynamic_cast<PathListItem*>((BListItem*)items.ItemAtFast(i));
		paths[i] = item ? new (nothrow) VectorPath(*item->path) : NULL;
	}

	AddPathsCommand* command
		= new (nothrow) AddPathsCommand(fPathContainer,
										paths, count, true, toIndex);
	if (!command) {
		for (int32 i = 0; i < count; i++)
			delete paths[i];
		return;
	}

	fCommandStack->Perform(command);
}

// RemoveItemList
void
PathListView::RemoveItemList(BList& items)
{
	if (!fCommandStack || !fPathContainer)
		return;

	int32 count = items.CountItems();
	VectorPath* paths[count];
	for (int32 i = 0; i < count; i++) {
		PathListItem* item = dynamic_cast<PathListItem*>(
			(BListItem*)items.ItemAtFast(i));
		if (item)
			paths[i] = item->path;
		else
			paths[i] = NULL;
	}

	RemovePathsCommand* command
		= new (nothrow) RemovePathsCommand(fPathContainer,
										   paths, count);
	fCommandStack->Perform(command);
}

// CloneItem
BListItem*
PathListView::CloneItem(int32 index) const
{
	if (PathListItem* item = dynamic_cast<PathListItem*>(ItemAt(index))) {
		return new PathListItem(item->path,
								const_cast<PathListView*>(this),
								fCurrentShape != NULL);
	}
	return NULL;
}

// IndexOfSelectable
int32
PathListView::IndexOfSelectable(Selectable* selectable) const
{
	VectorPath* path = dynamic_cast<VectorPath*>(selectable);
	if (!path)
		return -1;

	for (int32 i = 0;
		 PathListItem* item = dynamic_cast<PathListItem*>(ItemAt(i));
		 i++) {
		if (item->path == path)
			return i;
	}

	return -1;
}

// SelectableFor
Selectable*
PathListView::SelectableFor(BListItem* item) const
{
	PathListItem* pathItem = dynamic_cast<PathListItem*>(item);
	if (pathItem)
		return pathItem->path;
	return NULL;
}

// #pragma mark -

// PathAdded
void
PathListView::PathAdded(VectorPath* path, int32 index)
{
	// NOTE: we are in the thread that messed with the
	// ShapeContainer, so no need to lock the
	// container, when this is changed to asynchronous
	// notifications, then it would need to be read-locked!
	if (!LockLooper())
		return;

	if (_AddPath(path, index))
		Select(index);

	UnlockLooper();
}

// PathRemoved
void
PathListView::PathRemoved(VectorPath* path)
{
	// NOTE: we are in the thread that messed with the
	// ShapeContainer, so no need to lock the
	// container, when this is changed to asynchronous
	// notifications, then it would need to be read-locked!
	if (!LockLooper())
		return;

	// NOTE: we're only interested in VectorPath objects
	_RemovePath(path);

	UnlockLooper();
}

// #pragma mark -

// SetPathContainer
void
PathListView::SetPathContainer(PathContainer* container)
{
	if (fPathContainer == container)
		return;

	// detach from old container
	if (fPathContainer)
		fPathContainer->RemoveListener(this);

	_MakeEmpty();

	fPathContainer = container;

	if (!fPathContainer)
		return;

	fPathContainer->AddListener(this);

	// sync
//	if (!fPathContainer->ReadLock())
//		return;

	int32 count = fPathContainer->CountPaths();
	for (int32 i = 0; i < count; i++)
		_AddPath(fPathContainer->PathAtFast(i), i);

//	fPathContainer->ReadUnlock();
}

// SetShapeContainer
void
PathListView::SetShapeContainer(ShapeContainer* container)
{
	if (fShapeContainer == container)
		return;

	// detach from old container
	if (fShapeContainer)
		fShapeContainer->RemoveListener(fShapePathListener);

	fShapeContainer = container;

	if (fShapeContainer)
		fShapeContainer->AddListener(fShapePathListener);
}

// SetCommandStack
void
PathListView::SetCommandStack(CommandStack* stack)
{
	fCommandStack = stack;
}

// SetMenu
void
PathListView::SetMenu(BMenu* menu)
{
	fMenu = menu;
	if (fMenu == NULL)
		return;

	fAddMI = new BMenuItem("Add", new BMessage(MSG_ADD));
	fAddRectMI = new BMenuItem("Add Rect", new BMessage(MSG_ADD_RECT));
	fAddCircleMI = new BMenuItem("Add Circle"/*B_UTF8_ELLIPSIS*/,
		new BMessage(MSG_ADD_CIRCLE));
//	fAddArcMI = new BMenuItem("Add Arc"B_UTF8_ELLIPSIS,
//		new BMessage(MSG_ADD_ARC));
	fDuplicateMI = new BMenuItem("Duplicate", new BMessage(MSG_DUPLICATE));
	fReverseMI = new BMenuItem("Reverse", new BMessage(MSG_REVERSE));
	fCleanUpMI = new BMenuItem("Clean Up", new BMessage(MSG_CLEAN_UP));
	fRotateIndicesRightMI = new BMenuItem("Rotate Indices Right",
		new BMessage(MSG_ROTATE_INDICES_CCW), 'R');
	fRotateIndicesLeftMI = new BMenuItem("Rotate Indices Left",
		new BMessage(MSG_ROTATE_INDICES_CW), 'R', B_SHIFT_KEY);
	fRemoveMI = new BMenuItem("Remove", new BMessage(MSG_REMOVE));

	fMenu->AddItem(fAddMI);
	fMenu->AddItem(fAddRectMI);
	fMenu->AddItem(fAddCircleMI);
//	fMenu->AddItem(fAddArcMI);

	fMenu->AddSeparatorItem();

	fMenu->AddItem(fDuplicateMI);
	fMenu->AddItem(fReverseMI);
	fMenu->AddItem(fCleanUpMI);

	fMenu->AddSeparatorItem();

	fMenu->AddItem(fRotateIndicesLeftMI);
	fMenu->AddItem(fRotateIndicesRightMI);

	fMenu->AddSeparatorItem();

	fMenu->AddItem(fRemoveMI);

	fMenu->SetTargetForItems(this);

	_UpdateMenu();
}

// SetCurrentShape
void
PathListView::SetCurrentShape(Shape* shape)
{
	if (fCurrentShape == shape)
		return;

	fCurrentShape = shape;
	fShapePathListener->SetShape(shape);

	_UpdateMarks();
}

// #pragma mark -

// _AddPath
bool
PathListView::_AddPath(VectorPath* path, int32 index)
{
	if (path) {
		 return AddItem(
		 	new PathListItem(path, this, fCurrentShape != NULL), index);
	}
	return false;
}

// _RemovePath
bool
PathListView::_RemovePath(VectorPath* path)
{
	PathListItem* item = _ItemForPath(path);
	if (item && RemoveItem(item)) {
		delete item;
		return true;
	}
	return false;
}

// _ItemForPath
PathListItem*
PathListView::_ItemForPath(VectorPath* path) const
{
	for (int32 i = 0;
		 PathListItem* item = dynamic_cast<PathListItem*>(ItemAt(i));
		 i++) {
		if (item->path == path)
			return item;
	}
	return NULL;
}

// #pragma mark -

// _UpdateMarks
void
PathListView::_UpdateMarks()
{
	int32 count = CountItems();
	if (fCurrentShape) {
		// enable display of marks and mark items whoes
		// path is contained in fCurrentShape
		for (int32 i = 0; i < count; i++) {
			PathListItem* item = dynamic_cast<PathListItem*>(ItemAt(i));
			if (!item)
				continue;
			item->SetMarkEnabled(true);
			item->SetMarked(fCurrentShape->Paths()->HasPath(item->path));
		}
	} else {
		// disable display of marks
		for (int32 i = 0; i < count; i++) {
			PathListItem* item = dynamic_cast<PathListItem*>(ItemAt(i));
			if (!item)
				continue;
			item->SetMarkEnabled(false);
		}
	}

	Invalidate();
}

// _SetPathMarked
void
PathListView::_SetPathMarked(VectorPath* path, bool marked)
{
	if (PathListItem* item = _ItemForPath(path)) {
		item->SetMarked(marked);
	}
}

// _UpdateMenu
void
PathListView::_UpdateMenu()
{
	if (!fMenu)
		return;

	bool gotSelection = CurrentSelection(0) >= 0;

	fDuplicateMI->SetEnabled(gotSelection);
	fReverseMI->SetEnabled(gotSelection);
	fRemoveMI->SetEnabled(gotSelection);
}


