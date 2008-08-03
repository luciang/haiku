/*
 * Copyright 2005-2007, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 */
#ifndef DESKTOP_SETTINGS_PRIVATE_H
#define DESKTOP_SETTINGS_PRIVATE_H


#include "DesktopSettings.h"
#include "ServerFont.h"

#include <Locker.h>

struct server_read_only_memory;


class DesktopSettingsPrivate {
	public:
		DesktopSettingsPrivate(server_read_only_memory* shared);
		~DesktopSettingsPrivate();

		status_t		Save(uint32 mask = kAllSettings);

		void			SetDefaultPlainFont(const ServerFont& font);
		const ServerFont& DefaultPlainFont() const;

		void			SetDefaultBoldFont(const ServerFont& font);
		const ServerFont& DefaultBoldFont() const;

		void			SetDefaultFixedFont(const ServerFont& font);
		const ServerFont& DefaultFixedFont() const;

		void			SetScrollBarInfo(const scroll_bar_info &info);
		const scroll_bar_info& ScrollBarInfo() const;

		void			SetMenuInfo(const menu_info &info);
		const menu_info& MenuInfo() const;

		void			SetMouseMode(mode_mouse mode);
		mode_mouse		MouseMode() const;
		bool			FocusFollowsMouse() const;

		void			SetShowAllDraggers(bool show);
		bool			ShowAllDraggers() const;

		void			SetWorkspacesCount(int32 number);
		int32			WorkspacesCount() const;

		void			SetWorkspacesMessage(int32 index, BMessage& message);
		const BMessage*	WorkspacesMessage(int32 index) const;

		void			SetUIColor(color_which which, const rgb_color color);
		rgb_color		UIColor(color_which which) const;

		void			SetSubpixelAntialiasing(bool subpix);
		bool			SubpixelAntialiasing() const;
		void			SetHinting(bool hinting);
		bool			Hinting() const;
		void			SetSubpixelAverageWeight(uint8 averageWeight);
		uint8			SubpixelAverageWeight() const;
		void			SetSubpixelOrderingRegular(bool SubpixelOrdering);
		bool			IsSubpixelOrderingRegular() const;

	private:
		void			_SetDefaults();
		status_t		_Load();
		status_t		_GetPath(BPath& path);

		ServerFont		fPlainFont;
		ServerFont		fBoldFont;
		ServerFont		fFixedFont;

		scroll_bar_info	fScrollBarInfo;
		menu_info		fMenuInfo;
		mode_mouse		fMouseMode;
		bool			fShowAllDraggers;
		int32			fWorkspacesCount;
		BMessage		fWorkspaceMessages[kMaxWorkspaces];

		server_read_only_memory& fShared;
};

#endif	/* DESKTOP_SETTINGS_PRIVATE_H */
