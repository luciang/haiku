/*
 * Copyright 2002-2006, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Oliver Siebenmarck
 *		Andrew McCall, mccall@digitalparadise.co.uk
 *		Michael Wilber
 */
#ifndef TRANSLATOR_LIST_VIEW_H
#define TRANSLATOR_LIST_VIEW_H


#include <ListView.h>
#include <TranslationDefs.h>


class TranslatorItem : public BStringItem {
	public:
		TranslatorItem(translator_id id, const char* name);
		virtual ~TranslatorItem();

		translator_id ID() const { return fID; }

	private:
		translator_id	fID;
};

class TranslatorListView : public BListView {
	public:
		TranslatorListView(BRect rect, const char *name,
			list_view_type type = B_SINGLE_SELECTION_LIST);
		virtual ~TranslatorListView();

		virtual void MessageReceived(BMessage *message);
		virtual void MouseMoved(BPoint point, uint32 transit, const BMessage *msg);

		void SortItems();
};

#endif	// TRANSLATOR_LIST_VIEW_H
