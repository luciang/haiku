/*
 * Copyright 2007-2008, Christof Lutteroth, lutteroth@cs.auckland.ac.nz
 * Copyright 2007-2008, James Kim, jkim202@ec.auckland.ac.nz
 * Distributed under the terms of the MIT License.
 */

#ifndef	Y_TAB_H
#define	Y_TAB_H

#include "Variable.h"

namespace BALM {
	
class BALMLayout;

/**
 * Horizontal grid line (y-tab).
 */
class YTab : public Variable {
	
protected:
						YTab(BALMLayout* ls);

protected:
	/**
	 * Property signifying if there is a constraint which relates
	 * this tab to a different tab that is further to the top.
	 * Only used for reverse engineering.
	 */
	bool				fTopLink;

public:
	friend class			Area;
	friend class			Row;
	friend class			BALMLayout;

};

}	// namespace BALM

using BALM::YTab;

#endif	// Y_TAB_H
