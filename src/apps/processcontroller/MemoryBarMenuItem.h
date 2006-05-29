/*
	ProcessController © 2000, Georges-Edouard Berenger, All Rights Reserved.
	Copyright (C) 2004 beunited.org 

	This library is free software; you can redistribute it and/or 
	modify it under the terms of the GNU Lesser General Public 
	License as published by the Free Software Foundation; either 
	version 2.1 of the License, or (at your option) any later version. 

	This library is distributed in the hope that it will be useful, 
	but WITHOUT ANY WARRANTY; without even the implied warranty of 
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
	Lesser General Public License for more details. 

	You should have received a copy of the GNU Lesser General Public 
	License along with this library; if not, write to the Free Software 
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA	
*/
#ifndef _MEMORY_BAR_MENU_ITEM_H_
#define _MEMORY_BAR_MENU_ITEM_H_


#include <MenuItem.h>

class BBitmap;


class MemoryBarMenuItem : public BMenuItem {
	public:
						MemoryBarMenuItem(const char *label, team_id team,
							BBitmap* icon, bool deleteIcon, BMessage* message);
		virtual			~MemoryBarMenuItem();

		virtual	void	DrawContent();
		virtual	void	GetContentSize(float* _width, float* _height);

		void			DrawIcon();
		void			DrawBar(bool force);
		int				UpdateSituation(int commitedMemory);
		void			BarUpdate();
		void			Init();
		void			Reset(char* name, team_id team, BBitmap* icon, bool deleteIcon);

	private:
		int				fPhysicalMemory;
		int				fCommitedMemory;
		int				fWriteMemory;
		int				fAllMemory;
		int				fLastCommited;
		int				fLastWrite;
		int				fLastAll;
		team_id			fTeamID;
		BBitmap*		fIcon;
		float			fGrenze1;
		float			fGrenze2;
		bool			fDeleteIcon;
};

#endif // _MEMORY_BAR_MENU_ITEM_H_
