/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef STANDARD_ERROR_OUTPUT_H
#define STANDARD_ERROR_OUTPUT_H


#include "ErrorOutput.h"


class StandardErrorOutput : public ErrorOutput {
	virtual	void				PrintErrorVarArgs(const char* format,
									va_list args);
};


#endif	// STANDARD_ERROR_OUTPUT_H
