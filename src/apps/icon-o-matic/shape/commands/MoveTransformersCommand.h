/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef MOVE_TRANSFORMERS_COMMAND_H
#define MOVE_TRANSFORMERS_COMMAND_H


#include "Command.h"

// TODO: make a templated "move items" command?

namespace BPrivate {
namespace Icon {
	class Shape;
	class Transformer;
}
}
using namespace BPrivate::Icon;

class MoveTransformersCommand : public Command {
 public:
								MoveTransformersCommand(
									Shape* shape,
									Transformer** transformers,
									int32 count,
									int32 toIndex);
	virtual						~MoveTransformersCommand();
	
	virtual	status_t			InitCheck();

	virtual	status_t			Perform();
	virtual status_t			Undo();

	virtual void				GetName(BString& name);

 private:
			Shape*				fContainer;
			Transformer**		fTransformers;
			int32*				fIndices;
			int32				fToIndex;
			int32				fCount;
};

#endif // MOVE_TRANSFORMERS_COMMAND_H
