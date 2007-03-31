/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef FREEZE_TRANSFORMATION_COMMAND_H
#define FREEZE_TRANSFORMATION_COMMAND_H

#include "Command.h"

class Shape;

namespace BPrivate {
namespace Icon {
	class Transformable;
}
}
using BPrivate::Icon::Transformable;

class FreezeTransformationCommand : public Command {
 public:
								FreezeTransformationCommand(
									Shape** const shapes,
									int32 count);
	virtual						~FreezeTransformationCommand();
	
	virtual	status_t			InitCheck();

	virtual	status_t			Perform();
	virtual status_t			Undo();

	virtual void				GetName(BString& name);

 private:
			void				_ApplyTransformation(Shape* shape,
									const Transformable& transform);

			Shape**				fShapes;
			double*				fOriginalTransformations;
			int32				fCount;
};

#endif // FREEZE_TRANSFORMATION_COMMAND_H
