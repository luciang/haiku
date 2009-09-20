/*
 * Copyright 2004-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Philippe Houdoin
 * 		Fredrik Modéen
 */


#ifndef INTERFACES_LIST_VIEW_H
#define INTERFACES_LIST_VIEW_H

#include <String.h>
#include <ListView.h>
#include <ListItem.h>
#include <Bitmap.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include "Setting.h"

class InterfaceListItem : public BListItem {
public:
	InterfaceListItem(const char* name);
	~InterfaceListItem();

	void DrawItem(BView* owner, BRect bounds, bool complete);
	void Update(BView* owner, const BFont* font);
		
	inline const char*		Name()			{ return fSetting->Name(); }
	inline bool				Enabled()		{ return fEnabled; } 
	inline void				SetEnabled(bool enable){ fEnabled = enable; }
	inline Setting*			GetSetting()	{ return fSetting; } 

private:
	void 					_InitIcon();

	bool					fEnabled;
	BBitmap* 				fIcon;
	Setting*				fSetting;
};


class InterfacesListView : public BListView {
public:
	InterfacesListView(BRect rect, const char* name,
		uint32 resizingMode = B_FOLLOW_LEFT | B_FOLLOW_TOP);
	virtual ~InterfacesListView();

	InterfaceListItem* FindItem(const char* name);

protected:
	virtual void AttachedToWindow();
	virtual void DetachedFromWindow();

	virtual void MessageReceived(BMessage* message);

private:
	status_t	_InitList();
	status_t	_UpdateList();
};

#endif /*INTERFACES_LIST_VIEW_H*/
