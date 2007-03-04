// List.h
//
// Copyright (c) 2003, Ingo Weinhold (bonefish@cs.tu-berlin.de)
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// You can alternatively use *this file* under the terms of the the MIT
// license included in this package.

#ifndef LIST_H
#define LIST_H

#include <new>
#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>

template<typename ITEM>
class DefaultDefaultItemCreator {
public:
	static inline ITEM GetItem() { return ITEM(0); }
};

/*!
	\class List
	\brief A generic list implementation.
*/
template<typename ITEM,
		 typename DEFAULT_ITEM_SUPPLIER = DefaultDefaultItemCreator<ITEM> >
class List {
public:
	typedef ITEM					item_t;
	typedef List					list_t;

private:
	static item_t					sDefaultItem;
	static const size_t				kDefaultChunkSize = 10;
	static const size_t				kMaximalChunkSize = 1024 * 1024;

public:
	List(size_t chunkSize = kDefaultChunkSize);
	~List();

	inline const item_t &GetDefaultItem() const;
	inline item_t &GetDefaultItem();

	bool AddItem(const item_t &item, int32 index);
	bool AddItem(const item_t &item);
//	bool AddList(list_t *list, int32 index);
//	bool AddList(list_t *list);

	bool RemoveItem(const item_t &item);
	bool RemoveItem(int32 index);

	void MakeEmpty();

	int32 CountItems() const;
	bool IsEmpty() const;
	const item_t &ItemAt(int32 index) const;
	item_t &ItemAt(int32 index);
	const item_t *Items() const;
	int32 IndexOf(const item_t &item) const;
	bool HasItem(const item_t &item) const;

private:
	inline static void _MoveItems(item_t* items, int32 offset, int32 count);
	bool _Resize(size_t count);

private:
	size_t	fCapacity;
	size_t	fChunkSize;
	int32	fItemCount;
	item_t	*fItems;
};

// sDefaultItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
typename List<ITEM, DEFAULT_ITEM_SUPPLIER>::item_t
	List<ITEM, DEFAULT_ITEM_SUPPLIER>::sDefaultItem(
		DEFAULT_ITEM_SUPPLIER::GetItem());

// constructor
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
List<ITEM, DEFAULT_ITEM_SUPPLIER>::List(size_t chunkSize)
	: fCapacity(0),
	  fChunkSize(chunkSize),
	  fItemCount(0),
	  fItems(NULL)
{
	if (fChunkSize == 0 || fChunkSize > kMaximalChunkSize)
		fChunkSize = kDefaultChunkSize;
	_Resize(0);
}

// destructor
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
List<ITEM, DEFAULT_ITEM_SUPPLIER>::~List()
{
	MakeEmpty();
	free(fItems);
}

// GetDefaultItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
inline
const typename List<ITEM, DEFAULT_ITEM_SUPPLIER>::item_t &
List<ITEM, DEFAULT_ITEM_SUPPLIER>::GetDefaultItem() const
{
	return sDefaultItem;
}

// GetDefaultItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
inline
typename List<ITEM, DEFAULT_ITEM_SUPPLIER>::item_t &
List<ITEM, DEFAULT_ITEM_SUPPLIER>::GetDefaultItem()
{
	return sDefaultItem;
}

// _MoveItems
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
inline
void
List<ITEM, DEFAULT_ITEM_SUPPLIER>::_MoveItems(item_t* items, int32 offset, int32 count)
{
	if (count > 0 && offset != 0)
		memmove(items + offset, items, count * sizeof(item_t));
}

// AddItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::AddItem(const item_t &item, int32 index)
{
	bool result = (index >= 0 && index <= fItemCount
				   && _Resize(fItemCount + 1));
	if (result) {
		_MoveItems(fItems + index, 1, fItemCount - index - 1);
		new(fItems + index) item_t(item);
	}
	return result;
}

// AddItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::AddItem(const item_t &item)
{
	bool result = true;
	if ((int32)fCapacity > fItemCount) {
		new(fItems + fItemCount) item_t(item);
		fItemCount++;
	} else {
		if ((result = _Resize(fItemCount + 1)))
			new(fItems + (fItemCount - 1)) item_t(item);
	}
	return result;
}

// These don't use the copy constructor!
/*
// AddList
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::AddList(list_t *list, int32 index)
{
	bool result = (list && index >= 0 && index <= fItemCount);
	if (result && list->fItemCount > 0) {
		int32 count = list->fItemCount;
		result = _Resize(fItemCount + count);
		if (result) {
			_MoveItems(fItems + index, count, fItemCount - index - count);
			memcpy(fItems + index, list->fItems,
				   list->fItemCount * sizeof(item_t));
		}
	}
	return result;
}

// AddList
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::AddList(list_t *list)
{
	bool result = (list);
	if (result && list->fItemCount > 0) {
		int32 index = fItemCount;
		int32 count = list->fItemCount;
		result = _Resize(fItemCount + count);
		if (result) {
			memcpy(fItems + index, list->fItems,
				   list->fItemCount * sizeof(item_t));
		}
	}
	return result;
}
*/

// RemoveItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::RemoveItem(const item_t &item)
{
	int32 index = IndexOf(item);
	bool result = (index >= 0);
	if (result)
		RemoveItem(index);
	return result;
}

// RemoveItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::RemoveItem(int32 index)
{
	if (index >= 0 && index < fItemCount) {
		fItems[index].~item_t();
		_MoveItems(fItems + index + 1, -1, fItemCount - index - 1);
		_Resize(fItemCount - 1);
		return true;
	}
	return false;
}

// MakeEmpty
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
void
List<ITEM, DEFAULT_ITEM_SUPPLIER>::MakeEmpty()
{
	for (int32 i = 0; i < fItemCount; i++)
		fItems[i].~item_t();
	_Resize(0);
}

// CountItems
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
int32
List<ITEM, DEFAULT_ITEM_SUPPLIER>::CountItems() const
{
	return fItemCount;
}

// IsEmpty
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::IsEmpty() const
{
	return (fItemCount == 0);
}

// ItemAt
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
const typename List<ITEM, DEFAULT_ITEM_SUPPLIER>::item_t &
List<ITEM, DEFAULT_ITEM_SUPPLIER>::ItemAt(int32 index) const
{
	if (index >= 0 && index < fItemCount)
		return fItems[index];
	return sDefaultItem;
}

// ItemAt
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
typename List<ITEM, DEFAULT_ITEM_SUPPLIER>::item_t &
List<ITEM, DEFAULT_ITEM_SUPPLIER>::ItemAt(int32 index)
{
	if (index >= 0 && index < fItemCount)
		return fItems[index];
	return sDefaultItem;
}

// Items
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
const typename List<ITEM, DEFAULT_ITEM_SUPPLIER>::item_t *
List<ITEM, DEFAULT_ITEM_SUPPLIER>::Items() const
{
	return fItems;
}

// IndexOf
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
int32
List<ITEM, DEFAULT_ITEM_SUPPLIER>::IndexOf(const item_t &item) const
{
	for (int32 i = 0; i < fItemCount; i++) {
		if (fItems[i] == item)
			return i;
	}
	return -1;
}

// HasItem
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::HasItem(const item_t &item) const
{
	return (IndexOf(item) >= 0);
}

// _Resize
template<typename ITEM, typename DEFAULT_ITEM_SUPPLIER>
bool
List<ITEM, DEFAULT_ITEM_SUPPLIER>::_Resize(size_t count)
{
	bool result = true;
	// calculate the new capacity
	int32 newSize = count;
	if (newSize <= 0)
		newSize = 1;
	newSize = ((newSize - 1) / fChunkSize + 1) * fChunkSize;
	// resize if necessary
	if ((size_t)newSize != fCapacity) {
		item_t* newItems
			= (item_t*)realloc(fItems, newSize * sizeof(item_t));
		if (newItems) {
			fItems = newItems;
			fCapacity = newSize;
		} else
			result = false;
	}
	if (result)
		fItemCount = count;
	return result;
}

#endif	// LIST_H
