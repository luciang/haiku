/*
 * Copyright 2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef	_FLATTENABLE_H
#define	_FLATTENABLE_H


#include <SupportDefs.h>


class BFlattenable {
	public:
		virtual	bool IsFixedSize() const = 0;
		virtual	type_code TypeCode() const = 0;
		virtual	ssize_t FlattenedSize() const = 0;
		virtual	status_t Flatten(void* buffer, ssize_t size) const = 0;
		virtual	bool AllowsTypeCode(type_code code) const;
		virtual	status_t Unflatten(type_code code, const void* buffer, ssize_t size) = 0;

		virtual	~BFlattenable();

	private:
		void _ReservedFlattenable1();
		virtual	void _ReservedFlattenable2();
		virtual	void _ReservedFlattenable3();
};

#endif	// _FLATTENABLE_H
