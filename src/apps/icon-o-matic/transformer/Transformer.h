/*
 * Copyright 2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#ifndef TRANSFORMER_H
#define TRANSFORMER_H

#include "IconObject.h"

class VertexSource {
 public:
								VertexSource();
	virtual						~VertexSource();

    virtual	void				rewind(unsigned path_id) = 0;
    virtual	unsigned			vertex(double* x, double* y) = 0;

	virtual	void				SetLast();
};


class Transformer : public VertexSource,
					public IconObject {
 public:
								Transformer(VertexSource& source,
											const char* name);
	virtual						~Transformer();

    virtual	void				rewind(unsigned path_id);
    virtual	unsigned			vertex(double* x, double* y);

	virtual	void				SetSource(VertexSource& source);

 protected:
			VertexSource&		fSource;
};

#endif // TRANSFORMER_H
