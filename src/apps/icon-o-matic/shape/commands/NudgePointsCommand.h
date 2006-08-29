/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef NUDGE_POINTS_ACTION_H
#define NUDGE_POINTS_ACTION_H

#include <String.h>

#include "TransformCommand.h"

class VectorPath;

struct control_point;

class NudgePointsCommand : public TransformCommand {
 public:
								NudgePointsCommand(VectorPath* path,

												   const int32* indices,
												   const control_point* points,
												   int32 count);

	virtual						~NudgePointsCommand();
	
	virtual	status_t			InitCheck();

 protected:
	virtual	status_t			_SetTransformation(BPoint pivot,
												   BPoint translation,
												   double rotation,
												   double xScale,
												   double yScale) const;

			VectorPath*			fPath;

			int32*				fIndices;
			control_point*		fPoints;
			int32				fCount;
};

#endif // NUDGE_POINTS_ACTION_H
