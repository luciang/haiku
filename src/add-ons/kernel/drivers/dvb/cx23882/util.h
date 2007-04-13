/*
 * Copyright (c) 2004-2007 Marcus Overhagen <marcus@overhagen.de>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, 
 * merge, publish, distribute, sublicense, and/or sell copies of 
 * the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR 
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __UTIL_H
#define __UTIL_H

#include <OS.h>

area_id	map_mem(void **virt, void *phy, size_t size, uint32 protection, const char *name);
area_id alloc_mem(void **virt, void **phy, size_t size, uint32 protection, const char *name);

// generic macro for rounding, can only be used for power of 2 blocksize
#define ROUNDUP(size, blocksize) 	(((size) + (blocksize) - 1) & ~((blocksize) - 1))

#define atomic_read(a) atomic_or(a, 0)

#endif
