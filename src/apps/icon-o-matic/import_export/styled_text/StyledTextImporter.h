/*
 * Copyright 2008, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		François Revol <revol@free.fr>
 */
#ifndef STYLED_TEXT_IMPORTER_H
#define STYLED_TEXT_IMPORTER_H


#include "Importer.h"
#include <Entry.h>

class BMessage;
class BShape;
struct text_run;
struct text_run_array;

namespace BPrivate {
namespace Icon {
	class Icon;
	class Style;
	class VectorPath;
	class PathContainer;
	class ShapeContainer;
	class StyleContainer;
}
}

struct style_map {
	text_run *run;
	Style *style;
};

class StyledTextImporter : public Importer {
 public:
								StyledTextImporter();
	virtual						~StyledTextImporter();

			status_t			Import(Icon* icon,
									   BMessage* clipping);
			status_t			Import(Icon* icon,
									   const entry_ref* ref);

 private:
			status_t			_Import(Icon* icon, const char *text, 
										text_run_array *runs);

			status_t			_AddStyle(Icon *icon, text_run *run);
			status_t			_AddPaths(Icon *icon, BShape *shape);
			status_t			_AddShape(Icon *icon, BShape *shape, text_run *run);

			struct style_map	*fStyleMap;
			int32				fStyleCount;
};

#endif // STYLED_TEXT_IMPORTER_H
