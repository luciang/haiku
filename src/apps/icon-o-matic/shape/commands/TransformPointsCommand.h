/*
 * Copyright 2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef TRANSFORM_POINTS_COMMAND_H
#define TRANSFORM_POINTS_COMMAND_H

#include "TransformBox.h"
#include "TransformCommand.h"

class Transformable;
class VectorPath;
struct control_point;

class TransformPointsCommand : public TransformCommand,
							   public TransformBoxListener {
 public:
								TransformPointsCommand(
										TransformBox* box,

										VectorPath* path,
										const int32* indices,
										const control_point* points,
										int32 count,

										BPoint pivot,
										BPoint translation,
										double rotation,
										double xScale,
										double yScale,

										const char* name,
										int32 nameIndex);
	virtual						~TransformPointsCommand();
	
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

			VectorPath*			fPath;

			int32*				fIndices;
			control_point*		fPoints;
			int32				fCount;
};

#endif // TRANSFORM_POINTS_COMMAND_H
