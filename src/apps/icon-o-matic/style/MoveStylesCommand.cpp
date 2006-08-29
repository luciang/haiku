/*
 * Copyright 2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#include "MoveStylesCommand.h"

#include <new>
#include <stdio.h>

#include "StyleContainer.h"
#include "Style.h"

using std::nothrow;

// constructor
MoveStylesCommand::MoveStylesCommand(StyleContainer* container,
									 Style** styles,
									 int32 count,
									 int32 toIndex)
	: Command(),
	  fContainer(container),
	  fStyles(styles),
	  fIndices(count > 0 ? new (nothrow) int32[count] : NULL),
	  fToIndex(toIndex),
	  fCount(count)
{
	if (!fContainer || !fStyles || !fIndices)
		return;

	// init original style indices and
	// adjust toIndex compensating for items that
	// are removed before that index
	int32 itemsBeforeIndex = 0;
	for (int32 i = 0; i < fCount; i++) {
		fIndices[i] = fContainer->IndexOf(fStyles[i]);
		if (fIndices[i] >= 0 && fIndices[i] < fToIndex)
			itemsBeforeIndex++;
	}
	fToIndex -= itemsBeforeIndex;
}

// destructor
MoveStylesCommand::~MoveStylesCommand()
{
	delete[] fStyles;
	delete[] fIndices;
}

// InitCheck
status_t
MoveStylesCommand::InitCheck()
{
	if (!fContainer || !fStyles || !fIndices)
		return B_NO_INIT;

	// analyse the move, don't return B_OK in case
	// the container state does not change...

	int32 index = fIndices[0];
		// NOTE: fIndices == NULL if fCount < 1

	if (index != fToIndex) {
		// a change is guaranteed
		return B_OK;
	}

	// the insertion index is the same as the index of the first
	// moved item, a change only occures if the indices of the
	// moved items is not contiguous
	bool isContiguous = true;
	for (int32 i = 1; i < fCount; i++) {
		if (fIndices[i] != index + 1) {
			isContiguous = false;
			break;
		}
		index = fIndices[i];
	}
	if (isContiguous) {
		// the container state will not change because of the move
		return B_ERROR;
	}

	return B_OK;
}

// Perform
status_t
MoveStylesCommand::Perform()
{
	status_t ret = B_OK;

	// remove styles from container
	for (int32 i = 0; i < fCount; i++) {
		if (fStyles[i] && !fContainer->RemoveStyle(fStyles[i])) {
			ret = B_ERROR;
			break;
		}
	}
	if (ret < B_OK)
		return ret;

	// add styles to container at the insertion index
	int32 index = fToIndex;
	for (int32 i = 0; i < fCount; i++) {
		if (fStyles[i] && !fContainer->AddStyle(fStyles[i], index++)) {
			ret = B_ERROR;
			break;
		}
	}

	return ret;
}

// Undo
status_t
MoveStylesCommand::Undo()
{
	status_t ret = B_OK;

	// remove styles from container
	for (int32 i = 0; i < fCount; i++) {
		if (fStyles[i] && !fContainer->RemoveStyle(fStyles[i])) {
			ret = B_ERROR;
			break;
		}
	}
	if (ret < B_OK)
		return ret;

	// add styles to container at remembered indices
	for (int32 i = 0; i < fCount; i++) {
		if (fStyles[i] && !fContainer->AddStyle(fStyles[i], fIndices[i])) {
			ret = B_ERROR;
			break;
		}
	}

	return ret;
}

// GetName
void
MoveStylesCommand::GetName(BString& name)
{
	if (fCount > 1)
		name << "Move Styles";
	else
		name << "Move Style";
}
