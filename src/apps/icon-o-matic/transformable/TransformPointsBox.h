/*
 * Copyright 2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef TRANSFORM_POINTS_BOX_H
#define TRANSFORM_POINTS_BOX_H

#include "CanvasTransformBox.h"

class CanvasView;
class PathManipulator;
class VectorPath;

struct control_point;

class TransformPointsBox : public CanvasTransformBox {
 public:
								TransformPointsBox(
									CanvasView* view,
									PathManipulator* manipulator,
									VectorPath* path,
									const int32* indices,
									int32 count);
	virtual						~TransformPointsBox();

	// Observer interface (Manipulator is an Observer)
	virtual	void				ObjectChanged(const Observable* object);

	// TransformBox interface
	virtual	void				Update(bool deep = true);

	virtual	TransformCommand*	MakeCommand(const char* commandName,
											uint32 nameIndex);

	// TransformPointsBox
			void				Cancel();

 private:
			PathManipulator*	fManipulator;
			VectorPath*			fPath;

			int32*				fIndices;
			int32				fCount;

			control_point*		fPoints;
};

#endif // TRANSFORM_POINTS_BOX_H

