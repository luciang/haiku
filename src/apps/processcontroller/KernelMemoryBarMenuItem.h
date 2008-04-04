/*
	ProcessController @ 2000, Georges-Edouard Berenger, All Rights Reserved.
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
#ifndef _KERNEL_MEMORY_BAR_MENU_ITEM_H_
#define _KERNEL_MEMORY_BAR_MENU_ITEM_H_


#include <MenuItem.h>


class KernelMemoryBarMenuItem : public BMenuItem {
	public:
		KernelMemoryBarMenuItem(system_info& systemInfo);
		virtual	void	DrawContent();
		virtual	void	GetContentSize(float* _width, float* _height);

		void			DrawBar(bool force);
		void			UpdateSituation(float committedMemory, float fCachedMemory);

	private:
		float	fCachedMemory;
		float	fPhysicalMemory;
		float	fCommittedMemory;
		double	fLastSum;
		float	fGrenze1;
		float	fGrenze2;
};

#endif // _KERNEL_MEMORY_BAR_MENU_ITEM_H_
