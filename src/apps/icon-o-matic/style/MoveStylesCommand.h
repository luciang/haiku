/*
 * Copyright 2006-2007, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef MOVE_STYLES_COMMAND_H
#define MOVE_STYLES_COMMAND_H


#include "Command.h"

// TODO: make a templated "move items" command?

namespace BPrivate {
namespace Icon {
	class Style;
	class StyleContainer;
}
}
using namespace BPrivate::Icon;

class MoveStylesCommand : public Command {
 public:
								MoveStylesCommand(
									StyleContainer* container,
									Style** styles,
									int32 count,
									int32 toIndex);
	virtual						~MoveStylesCommand();
	
	virtual	status_t			InitCheck();

	virtual	status_t			Perform();
	virtual status_t			Undo();

	virtual void				GetName(BString& name);

 private:
			StyleContainer*		fContainer;
			Style**				fStyles;
			int32*				fIndices;
			int32				fToIndex;
			int32				fCount;
};

#endif // MOVE_STYLES_COMMAND_H
