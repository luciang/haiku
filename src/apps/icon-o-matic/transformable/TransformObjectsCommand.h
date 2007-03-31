/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef TRANSFORM_OBJECTS_COMMAND_H
#define TRANSFORM_OBJECTS_COMMAND_H

#include "TransformBox.h"
#include "TransformCommand.h"

namespace BPrivate {
namespace Icon {
	class Transformable;
}
}
using BPrivate::Icon::Transformable;

class TransformObjectsCommand : public TransformCommand,
								public TransformBoxListener {
 public:
								TransformObjectsCommand(
										TransformBox* box,
										Transformable** const objects,
										const double* originals,
										int32 count,

										BPoint pivot,
										BPoint translation,
										double rotation,
										double xScale,
										double yScale,

										const char* name,
										int32 nameIndex);
	virtual						~TransformObjectsCommand();
	
	// Command interface
	virtual	status_t			InitCheck();

	// TransformBoxListener interface
	virtual	void				TransformBoxDeleted(
									const TransformBox* box);
 protected:
 	// TransformCommand interface
	virtual	status_t			_SetTransformation(BPoint pivotDiff,
												   BPoint translationDiff,
												   double rotationDiff,
												   double xScaleDiff,
												   double yScaleDiff) const;

			TransformBox*		fTransformBox;
			Transformable**		fObjects;
			double*				fOriginals;
			int32				fCount;
};

#endif // TRANSFORM_OBJECTS_COMMAND_H
