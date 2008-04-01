/*
 * Copyright 2008, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 * 
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 */
#ifndef WIZARD_PAGE_VIEW_H
#define WIZARD_PAGE_VIEW_H


#include <Button.h>
#include <Message.h>
#include <TextView.h>
#include <View.h>


class WizardPageView : public BView
{
public:
	WizardPageView(BMessage* settings, BRect frame, const char* name, 
		uint32 resizingMode = B_FOLLOW_ALL, uint32 flags = B_WILL_DRAW);
	virtual ~WizardPageView();
	
	virtual void PageCompleted();

	virtual BTextView* CreateDescription(BRect frame, const char* name,
		const char* description);
	
	virtual void MakeHeading(BTextView* view);
	virtual void LayoutDescriptionVertically(BTextView* view);

protected:
	BMessage* fSettings;

private:
	void _BuildUI();
};

#endif	// WIZARD_PAGE_VIEW_H
