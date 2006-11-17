/*
 * Copyright 2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef SIMPLE_FILE_SAVER_H
#define SIMPLE_FILE_SAVER_H

#include "FileSaver.h"

class Exporter;

class SimpleFileSaver : public FileSaver {
 public:
								SimpleFileSaver(Exporter* exporter,
												const entry_ref& ref);
	virtual						~SimpleFileSaver();

	virtual	status_t			Save(Document* document);

 private:
			Exporter*			fExporter;
};

#endif // SIMPLE_FILE_SAVER_H
