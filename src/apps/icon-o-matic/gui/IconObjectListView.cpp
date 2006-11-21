/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#include "IconObjectListView.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include "CommandStack.h"
#include "IconObject.h"
#include "Property.h"
#include "PropertyObject.h"
#include "Selection.h"
#include "SetPropertiesCommand.h"

using std::nothrow;

// constructor
IconObjectListView::IconObjectListView()
	: PropertyListView(),

	  fSelection(NULL),
	  fCommandStack(NULL),
	  fObject(NULL),
	  fIgnoreObjectChange(false)
{
}

// destructor
IconObjectListView::~IconObjectListView()
{
	SetSelection(NULL);
	_SetObject(NULL);
}

// Draw
void
IconObjectListView::Draw(BRect updateRect)
{
	PropertyListView::Draw(updateRect);

	if (fObject)
		return;

	// display helpful messages
	const char* message1 = "Click on an object in";
	const char* message2 = "any of the other lists to";
	const char* message3 = "edit it's properties here.";

	SetHighColor(tint_color(LowColor(), B_DARKEN_2_TINT));
	font_height fh;
	GetFontHeight(&fh);
	BRect b(Bounds());

	BPoint middle;
	float textHeight = (fh.ascent + fh.descent) * 1.5;
	middle.y = (b.top + b.bottom) / 2.0 - textHeight;
	middle.x = (b.left + b.right - StringWidth(message1)) / 2.0;
	DrawString(message1, middle);

	middle.y += textHeight;
	middle.x = (b.left + b.right - StringWidth(message2)) / 2.0;
	DrawString(message2, middle);

	middle.y += textHeight;
	middle.x = (b.left + b.right - StringWidth(message3)) / 2.0;
	DrawString(message3, middle);
}

// PropertyChanged
void
IconObjectListView::PropertyChanged(const Property* previous,
									const Property* current)
{
	if (!fCommandStack || !fObject)
		return;

	PropertyObject* oldObject = new (nothrow) PropertyObject();
	if (oldObject)
		oldObject->AddProperty(previous->Clone());

	PropertyObject* newObject = new (nothrow) PropertyObject();
	if (newObject)
		newObject->AddProperty(current->Clone());

	IconObject** objects = new (nothrow) IconObject*[1];
	if (objects)
		objects[0] = fObject;

	Command* command = new (nothrow) SetPropertiesCommand(objects, 1,
														  oldObject,
														  newObject);
	fIgnoreObjectChange = true;
	fCommandStack->Perform(command);
	fIgnoreObjectChange = false;
}

// PasteProperties
void
IconObjectListView::PasteProperties(const PropertyObject* object)
{
	// TODO: command for this
	if (fObject)
		fObject->SetToPropertyObject(object);

	PropertyListView::PasteProperties(object);
}

// IsEditingMultipleObjects
bool
IconObjectListView::IsEditingMultipleObjects()
{
	return false;
}

// #pragma mark -

// ObjectChanged
void
IconObjectListView::ObjectChanged(const Observable* object)
{
	if (object == fSelection) {
		Selectable* selected = fSelection->SelectableAt(0);
		_SetObject(dynamic_cast<IconObject*>(selected));
	}

	if (object == fObject/* && !fIgnoreObjectChange*/) {
//printf("IconObjectListView::ObjectChanged(fObject)\n");
		SetTo(fObject->MakePropertyObject());
	}
}

// #pragma mark -

// SetSelection
void
IconObjectListView::SetSelection(Selection* selection)
{
	if (fSelection == selection)
		return;

	if (fSelection)
		fSelection->RemoveObserver(this);

	fSelection = selection;

	if (fSelection)
		fSelection->AddObserver(this);
}

// SetCommandStack
void
IconObjectListView::SetCommandStack(CommandStack* stack)
{
	fCommandStack = stack;
}

// #pragma mark -

// _SetObject
void
IconObjectListView::_SetObject(IconObject* object)
{
	if (fObject == object)
		return;

	if (fObject) {
		fObject->RemoveObserver(this);
		fObject->Release();
	}

	fObject = object;
	PropertyObject* propertyObject = NULL;

	if (fObject) {
		fObject->Acquire();
		fObject->AddObserver(this);
		propertyObject = fObject->MakePropertyObject();
	}

	SetTo(propertyObject);
}

