// KernelUtilsOrders.h
//
// Copyright (c) 2003, Ingo Weinhold (bonefish@cs.tu-berlin.de)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
// 
// Except as contained in this notice, the name of a copyright holder shall
// not be used in advertising or otherwise to promote the sale, use or other
// dealings in this Software without prior written authorization of the
// copyright holder.

#ifndef _KERNEL_UTILS_ORDERS_H
#define _KERNEL_UTILS_ORDERS_H

namespace KernelUtilsOrder {

// Ascending
/*!	\brief A compare function object implying and ascending order.

	The < operator must be defined on the template argument type.
*/
template<typename Value>
class Ascending {
public:
	inline int operator()(const Value &a, const Value &b) const
	{
		if (a < b)
			return -1;
		else if (b < a)
			return 1;
		return 0;
	}
};

// Descending
/*!	\brief A compare function object implying and descending order.

	The < operator must be defined on the template argument type.
*/
template<typename Value>
class Descending {
public:
	inline int operator()(const Value &a, const Value &b) const
	{
		if (a < b)
			return -1;
		else if (b < a)
			return 1;
		return 0;
	}
};

}	// namespace KernelUtilsOrder

#endif	// _KERNEL_UTILS_ORDERS_H
