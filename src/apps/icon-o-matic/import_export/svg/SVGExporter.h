/*
 * Copyright 2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef SVG_EXPORTER_H
#define SVG_EXPORTER_H

#include <String.h>

#include "Exporter.h"

class Gradient;
class Shape;
class Style;
class Transformable;

class SVGExporter : public Exporter {
 public:
								SVGExporter();
	virtual						~SVGExporter();

	// Exporter
	virtual	status_t			Export(const Icon* icon, BPositionIO* stream);

	virtual	const char*			MIMEType();
	virtual	const char*			Extension();

	// SVGExporter
			void				SetOriginalEntry(const entry_ref* ref);

 private:
			bool				_DisplayWarning() const;

			status_t			_ExportShape(const Shape* shape,
											 BPositionIO* stream);
			status_t			_ExportGradient(const Gradient* gradient,
												BPositionIO* stream);
			void				_AppendMatrix(const Transformable* object,
											  BString& string) const;

			status_t			_GetFill(const Style* style,
										 char* string,
										 BPositionIO* stream);

 			int32				fGradientCount;
			entry_ref*			fOriginalEntry;
};


#endif // SVG_EXPORTER_H
