/*
 * Copyright 2004-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef HASH_MAP_H
#define HASH_MAP_H

//#include <Debug.h>

#include <util/OpenHashTable.h>

#include "AutoLocker.h"
#include "Locker.h"


// HashMapElement
template<typename Key, typename Value>
class HashMapElement {
private:
	typedef HashMapElement<Key, Value> Element;

public:
	HashMapElement()
		:
		fKey(),
		fValue()
	{
	}

	HashMapElement(const Key& key, const Value& value)
		:
		fKey(key),
		fValue(value)
	{
	}

	Key				fKey;
	Value			fValue;
	HashMapElement*	fNext;
};


// HashMapTableDefinition
template<typename Key, typename Value>
struct HashMapTableDefinition {
	typedef Key							KeyType;
	typedef	HashMapElement<Key, Value>	ValueType;

	size_t HashKey(const KeyType& key) const
		{ return key.GetHashCode(); }
	size_t Hash(const ValueType* value) const
		{ return HashKey(value->fKey); }
	bool Compare(const KeyType& key, const ValueType* value) const
		{ return value->fKey == key; }
	ValueType*& GetLink(ValueType* value) const
		{ return value->fNext; }
};


// HashMap
template<typename Key, typename Value>
class HashMap {
public:
	class Entry {
	public:
		Entry() {}
		Entry(const Key& key, Value value) : key(key), value(value) {}

		Key		key;
		Value	value;
	};

	class Iterator {
	private:
		typedef HashMapElement<Key, Value>	Element;
	public:
		Iterator(const Iterator& other)
			:
			fMap(other.fMap),
			fIterator(other.fIterator),
			fElement(other.fElement)
		{
		}

		bool HasNext() const
		{
			return fIterator.HasNext();
		}

		Entry Next()
		{
			fElement = fIterator.Next();
			if (fElement == NULL)
				return Entry();

			return Entry(fElement->fKey, fElement->fValue);
		}

		Entry Remove()
		{
			if (fElement == NULL)
				return Entry();

			Entry result(fElement->fKey, fElement->fValue);

			fMap->fTable.RemoveUnchecked(fElement);
			delete fElement;
			fElement = NULL;

			return result;
		}

		Iterator& operator=(const Iterator& other)
		{
			fMap = other.fMap;
			fIterator = other.fIterator;
			fElement = other.fElement;
			return *this;
		}

	private:
		Iterator(HashMap<Key, Value>* map)
			:
			fMap(map),
			fIterator(map->fTable.GetIterator()),
			fElement(NULL)
		{
		}

	private:
		friend class HashMap<Key, Value>;
		typedef BOpenHashTable<HashMapTableDefinition<Key, Value> >
			ElementTable;

		HashMap<Key, Value>*			fMap;
		typename ElementTable::Iterator	fIterator;
		Element*						fElement;
	};

	HashMap();
	~HashMap();

	status_t InitCheck() const;

	status_t Put(const Key& key, const Value& value);
	Value Remove(const Key& key);
	void Clear();
	Value Get(const Key& key) const;

	bool ContainsKey(const Key& key) const;

	int32 Size() const;

	Iterator GetIterator();

protected:
	typedef BOpenHashTable<HashMapTableDefinition<Key, Value> > ElementTable;
	typedef HashMapElement<Key, Value>	Element;
	friend class Iterator;

protected:
	ElementTable	fTable;
};


// SynchronizedHashMap
template<typename Key, typename Value>
class SynchronizedHashMap : public Locker {
public:
	typedef typename HashMap<Key, Value>::Entry Entry;
	typedef typename HashMap<Key, Value>::Iterator Iterator;

	SynchronizedHashMap() : Locker("synchronized hash map")	{}
	~SynchronizedHashMap()	{ Lock(); }

	status_t InitCheck() const
	{
		return fMap.InitCheck();
	}

	status_t Put(const Key& key, const Value& value)
	{
		MapLocker locker(this);
		if (!locker.IsLocked())
			return B_ERROR;
		return fMap.Put(key, value);
	}

	Value Remove(const Key& key)
	{
		MapLocker locker(this);
		if (!locker.IsLocked())
			return Value();
		return fMap.Remove(key);
	}

	void Clear()
	{
		MapLocker locker(this);
		return fMap.Clear();
	}

	Value Get(const Key& key) const
	{
		const Locker* lock = this;
		MapLocker locker(const_cast<Locker*>(lock));
		if (!locker.IsLocked())
			return Value();
		return fMap.Get(key);
	}

	bool ContainsKey(const Key& key) const
	{
		const Locker* lock = this;
		MapLocker locker(const_cast<Locker*>(lock));
		if (!locker.IsLocked())
			return false;
		return fMap.ContainsKey(key);
	}

	int32 Size() const
	{
		const Locker* lock = this;
		MapLocker locker(const_cast<Locker*>(lock));
		return fMap.Size();
	}

	Iterator GetIterator()
	{
		return fMap.GetIterator();
	}

	// for debugging only
	const HashMap<Key, Value>& GetUnsynchronizedMap() const	{ return fMap; }
	HashMap<Key, Value>& GetUnsynchronizedMap()				{ return fMap; }

protected:
	typedef AutoLocker<Locker> MapLocker;

	HashMap<Key, Value>	fMap;
};

// HashKey32
template<typename Value>
struct HashKey32 {
	HashKey32() {}
	HashKey32(const Value& value) : value(value) {}

	uint32 GetHashCode() const
	{
		return (uint32)value;
	}

	HashKey32<Value> operator=(const HashKey32<Value>& other)
	{
		value = other.value;
		return *this;
	}

	bool operator==(const HashKey32<Value>& other) const
	{
		return (value == other.value);
	}

	bool operator!=(const HashKey32<Value>& other) const
	{
		return (value != other.value);
	}

	Value	value;
};


// HashKey64
template<typename Value>
struct HashKey64 {
	HashKey64() {}
	HashKey64(const Value& value) : value(value) {}

	uint32 GetHashCode() const
	{
		uint64 v = (uint64)value;
		return (uint32)(v >> 32) ^ (uint32)v;
	}

	HashKey64<Value> operator=(const HashKey64<Value>& other)
	{
		value = other.value;
		return *this;
	}

	bool operator==(const HashKey64<Value>& other) const
	{
		return (value == other.value);
	}

	bool operator!=(const HashKey64<Value>& other) const
	{
		return (value != other.value);
	}

	Value	value;
};


// HashMap

// constructor
template<typename Key, typename Value>
HashMap<Key, Value>::HashMap()
	:
	fTable()
{
	fTable.Init();
}


// destructor
template<typename Key, typename Value>
HashMap<Key, Value>::~HashMap()
{
	Clear();
}


// InitCheck
template<typename Key, typename Value>
status_t
HashMap<Key, Value>::InitCheck() const
{
	return (fTable.TableSize() > 0 ? B_OK : B_NO_MEMORY);
}


// Put
template<typename Key, typename Value>
status_t
HashMap<Key, Value>::Put(const Key& key, const Value& value)
{
	Element* element = fTable.Lookup(key);
	if (element) {
		// already contains the key: just set the new value
		element->fValue = value;
		return B_OK;
	}

	// does not contain the key yet: create an element and add it
	element = new(std::nothrow) Element(key, value);
	if (!element)
		return B_NO_MEMORY;

	status_t error = fTable.Insert(element);
	if (error != B_OK)
		delete element;

	return error;
}


// Remove
template<typename Key, typename Value>
Value
HashMap<Key, Value>::Remove(const Key& key)
{
	Element* element = fTable.Lookup(key);
	if (element == NULL)
		return Value();

	fTable.Remove(element);
	Value value = element->fValue;
	delete element;

	return value;
}


// Clear
template<typename Key, typename Value>
void
HashMap<Key, Value>::Clear()
{
	// clear the table and delete the elements
	Element* element = fTable.Clear(true);
	while (element != NULL) {
		Element* next = element->fNext;
		delete element;
		element = next;
	}
}


// Get
template<typename Key, typename Value>
Value
HashMap<Key, Value>::Get(const Key& key) const
{
	if (Element* element = fTable.Lookup(key))
		return element->fValue;
	return Value();
}


// ContainsKey
template<typename Key, typename Value>
bool
HashMap<Key, Value>::ContainsKey(const Key& key) const
{
	return fTable.Lookup(key) != NULL;
}


// Size
template<typename Key, typename Value>
int32
HashMap<Key, Value>::Size() const
{
	return fTable.CountElements();
}


// GetIterator
template<typename Key, typename Value>
typename HashMap<Key, Value>::Iterator
HashMap<Key, Value>::GetIterator()
{
	return Iterator(this);
}


#endif	// HASH_MAP_H
