/*
 * Copyright 2002-2007, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		<unknown, please fill in who knows>
 *		Stefano Ceccherini (stefano.ceccherini@gmail.com)
 */
#ifndef MENU_BAR_H
#define MENU_BAR_H


#include <MenuBar.h>

class BMenuItem;
class MenuBar : public BMenuBar {
	public:
		MenuBar();

		virtual	void AttachedToWindow();

		void UpdateMenu();
		void Update();
		virtual void FrameResized(float width, float height);

	private:	
		void _BuildMenu();

		BMenuItem*	fAlwaysShowTriggersItem;
		BMenuItem*	fControlAsShortcutItem;
		BMenuItem*	fAltAsShortcutItem;
};

#endif	// MENU_BAR_H
