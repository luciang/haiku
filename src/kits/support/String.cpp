//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		String.cpp
//	Author(s):		Marc Flerackers (mflerackers@androme.be)
//					Stefano Ceccherini (burton666@libero.it)
//
//	Description:	String class supporting common string operations.  
//------------------------------------------------------------------------------

// Standard Includes -----------------------------------------------------------
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// System Includes -------------------------------------------------------------
#include <Debug.h>
#include <String.h>

// Temporary Includes
#include "string_helper.h"


#define ENABLE_INLINES 0 // Set this to 1 to make some private methods inline


// define proper names for case-option of _DoReplace()
#define KEEP_CASE false
#define IGNORE_CASE true

/*!
	\class BString
	\brief String class supporting common string operations
	
	BString is a string allocation and manipulation class. The object
	takes care to allocate and free memory for you, so it will always be
	"big enough" to store your strings.
	
	\author <a href='mailto:mflerackers@androme.be>Marc Flerackers</a>
	\author <a href='mailto:burton666@freemail.it>Stefano Ceccherini</a>
*/	

/*!	\var char* BString::_privateData
	\brief BString's storage for data
*/

// constructor
/*!	\brief Creates an uninitialized BString.
*/
BString::BString()
	:_privateData(NULL)	
{
}


// constructor
/*! \brief Creates a BString and initializes it to the given string.
	\param str Pointer to a NULL terminated string.
*/
BString::BString(const char* str)
	:_privateData(NULL)
{
	if (str!= NULL)
		_Init(str, strlen(str));
}


// copy constructor
/*! \brief Creates a BString and makes it a copy of the supplied one.
	\param string the BString object to be copied.
*/
BString::BString(const BString &string)
	:_privateData(NULL)			
{
	_Init(string.String(), string.Length());
}


// constructor
/*! \brief Creates a BString and initializes it to the given string.
	\param str Pointer to a NULL terminated string.
	\param maxLength The amount of characters you want to copy from the original
		string.
*/
BString::BString(const char *str, int32 maxLength)
	:_privateData(NULL)		
{
	if (str != NULL)
	{
		int32 len = (int32)strlen(str);
		_Init(str, min_c(len, maxLength));
	}
}


// destructor
/*! \brief Frees all resources associated with the object.
	
	Frees the memory allocated by the BString object.
*/
BString::~BString()
{
	if (_privateData)
		free(_privateData - sizeof(int32));
}


/*---- Access --------------------------------------------------------------*/
// String, implemented inline in the header
/*! \fn const char* BString::String() const
	\brief Returns a pointer to the object string, NULL terminated.
	
	Returns a pointer to the object string, guaranteed to be NULL
	terminated. You can't modify or free the pointer. Once the BString
	object is deleted, the pointer becomes invalid.
	
	\return A pointer to the object string. 
*/


// Length, implemented inline in the header
/*!	\fn int32 BString::Length() const
	\brief Returns the length of the string, measured in bytes.
	\return The length of the string, measured in bytes.
*/

		
// CountChars
/*! \brief Returns the length of the object measured in characters.
	\return An integer which is the number of characters in the string.
	
	Counts the number of UTF8 characters contained in the string.
*/
int32
BString::CountChars() const
{
	int32 count = 0;
	
	const char *start = _privateData;
	
	/* String's end. This way we don't have to check for '\0' */
	/* but just compare two pointers (which should be faster) */  
	const char *end = _privateData + Length();
	
#if 0
	// ejaesler: Left in memoriam of one man's foolish disregard for the
	// maxim "Premature optimization is the root of all evil"
	while (*ptr)
	{
		// Jump to next UTF8 character
		// ejaesler: BGA's nifty function
		ptr += utf8_char_len(*ptr);
		count++;
	}
#endif

	while (start++ != end)
	{
		count++;

		// Jump to next UTF8 character
		 for (; (*start & 0xc0) == 0x80; start++);
	}

	
	return count;
}


/*---- Assignment ----------------------------------------------------------*/
// equal operator
/*! \brief Makes a copy of the given BString object.
	\param string The string object to copy.
	\return
		The function always returns \c *this .
*/
BString&
BString::operator=(const BString &string)
{
	if (&string != this) // Avoid auto-assignment
		_DoAssign(string.String(), string.Length());
	return *this;
}


// equal operator
/*! \brief Re-initializes the object to the given string.
	\param str Pointer to a string.
	\return
		The function always returns \c *this .
*/
BString&
BString::operator=(const char *str)
{
	if (str != NULL)
		_DoAssign(str, strlen(str));	
	else
		_GrowBy(-Length()); // Empties the string
	
	return *this;
}


// equal operator
/*! \brief Re-initializes the object to the given character.
	\param c The character which you want to initialize the string to.
	\return
		The function always returns \c *this .
*/
BString&
BString::operator=(char c)
{
	int32 curLen = Length();
	
	if (curLen != 1)
		_GrowBy(1 - curLen);
	_privateData[0] = c;
	
	return *this;
}


// SetTo
/*! \brief Re-initializes the object to the given string.
	\param str Pointer to a string.
	\param length Amount of characters to copy from the original string.
	\return
		The function always returns \c *this .
*/
BString&
BString::SetTo(const char *str, int32 length)
{
	if (str != NULL)
	{
		int32 len = (int32)strlen(str);
		_DoAssign(str, min_c(length, len));
	}
	else 
		_GrowBy(-Length()); // Empties the string
	
	return *this;
}


// SetTo
/*! \brief Makes a copy of the given BString object.
	\param from The string object to copy.
	\return
		The function always returns \c *this .
*/
BString&
BString::SetTo(const BString &from)
{
	if (&from != this) // Avoid auto-assignment
		_DoAssign(from.String(), from.Length());
	return *this;
}


// Adopt
/*! \brief Adopt's data of the given BString object, freeing the original object.
	\param from The string object to adopt.
	\return
		The function always returns \c *this .
*/
BString&
BString::Adopt(BString &from)
{
	if (&from == this) // Avoid auto-adoption
		return *this;
		
	if (_privateData)
		free(_privateData - sizeof(int32));

	/* "steal" the data from the given BString */
	_privateData = from._privateData;
	from._privateData = NULL;

	return *this;
}


// SetTo
/*! \brief Makes a copy of the given BString object.
	\param from The string object to copy.
	\param length Amount of characters to copy from the original BString.
	\return
		The function always returns \c *this .
*/
BString&
BString::SetTo(const BString &string, int32 length)
{
	if (&string != this) // Avoid auto-assignment
		_DoAssign(string.String(), min_c(length, string.Length()));
	return *this;
}


// Adopt
/*! \brief Adopt's data of the given BString object, freeing the original object.
	\param from The string object to adopt.
	\param length Amount of characters to get from the original BString.
	\return
		The function always returns \c *this .
*/
BString&
BString::Adopt(BString &from, int32 length)
{
	if (&from == this) // Avoid auto-adoption
		return *this;
		
	if (_privateData)
		free(_privateData - sizeof(int32));

	/* "steal" the data from the given BString */
	_privateData = from._privateData;
	from._privateData = NULL;

	if (length < Length())
		_privateData = _GrowBy(length - Length()); //Negative

	return *this;
}


// SetTo
/*! \brief Initializes the object to a string composed by a character you specify.
	\param c The character you want to initialize the BString.
	\param count The number of characters you want the BString to be composed by.
	\return
		The function always returns \c *this .
*/
BString&
BString::SetTo(char c, int32 count)
{
	int32 curLen = Length();
	
	if (curLen != count)
		_GrowBy(count - curLen);
		
	memset(_privateData, c, count);
	
	return *this;	
}


/*---- Substring copying ---------------------------------------------------*/

// CopyInto
/*! \brief Copy the BString data (or part of it) into another BString.
	\param into The BString where to copy the object.
	\param fromOffset The offset (zero based) where to begin the copy
	\param length The amount of bytes to copy.
	\return This function always returns *this .
*/
BString &
BString::CopyInto(BString &into, int32 fromOffset, int32 length) const
{
	if (&into != this)
		into.SetTo(String() + fromOffset, length);
	return into;
}


// CopyInto
/*! \brief Copy the BString data (or part of it) into the supplied buffer.
	\param into The buffer where to copy the object.
	\param fromOffset The offset (zero based) where to begin the copy
	\param length The amount of bytes to copy.
*/
void
BString::CopyInto(char *into, int32 fromOffset, int32 length) const
{
	if (into != NULL)
	{
		int32 len = Length() - fromOffset;
		len = min_c(len, length);
		memcpy(into, _privateData + fromOffset, len);
	}
}


/*---- Appending -----------------------------------------------------------*/
// plus operator
/*!	\brief Appends the given string to the object.
	\param str A pointer to the string to append.
	\return This function always returns *this .
*/
BString&
BString::operator+=(const char *str)
{
	if (str != NULL)
		_DoAppend(str, strlen(str));
	return *this;
}


// plus operator
/*!	\brief Appends the given character to the object.
	\param c The character to append.
	\return This function always returns *this .
*/
BString&
BString::operator+=(char c)
{
	_GrowBy(1);
	_privateData[Length() - 1] = c;
	
	return *this;
}


// Append
/*!	\brief Appends the given BString to the object.
	\param string The BString to append.
	\param length The maximum bytes to get from the original object.
	\return This function always returns *this .
*/
BString&
BString::Append(const BString &string, int32 length)
{
	_DoAppend(string.String(), min_c(length, string.Length()));
	return *this;
}


// Append
/*!	\brief Appends the given string to the object.
	\param str A pointer to the string to append.
	\param length The maximum bytes to get from the original string.
	\return This function always returns *this .
*/
BString&
BString::Append(const char *str, int32 length)
{
	if (str != NULL) 
	{
		int32 len = (int32)strlen(str);
		_DoAppend(str, min_c(len, length));
	}	
	return *this;
}


// Append
/*!	\brief Appends the given character to the object.
	\param c The character to append.
	\param count The number of characters to append.
	\return This function always returns *this .
*/
BString&
BString::Append(char c, int32 count)
{
	int32 len = Length();
	_GrowBy(count);
	memset(_privateData + len, c, count);

	return *this;
}


/*---- Prepending ----------------------------------------------------------*/
// Prepend
/*!	\brief Prepends the given string to the object.
	\param str A pointer to the string to prepend.
	\return This function always returns *this .
*/
BString&
BString::Prepend(const char *str)
{
	if (str != NULL)
		_DoPrepend(str, strlen(str));
	return *this;
}


// Prepend
/*!	\brief Prepends the given BString to the object.
	\param string The BString object to prepend.
	\return This function always returns *this .
*/
BString&
BString::Prepend(const BString &string)
{
	if (&string != this)
		_DoPrepend(string.String(), string.Length());
	return *this;
}


// Prepend
/*!	\brief Prepends the given string to the object.
	\param str A pointer to the string to prepend.
	\param length The maximum amount of bytes to get from the string.
	\return This function always returns *this .
*/
BString&
BString::Prepend(const char *str, int32 length)
{
	if (str != NULL) 
	{
		int32 len = (int32)strlen(str);
		_DoPrepend(str, min_c(len, length));		
	}
	return *this;
}


// Prepend
/*!	\brief Prepends the given BString to the object.
	\param string The BString object to prepend.
	\param len The maximum amount of bytes to get from the BString.
	\return This function always returns *this .
*/
BString&
BString::Prepend(const BString &string, int32 len)
{
	if (&string != this)
		_DoPrepend(string.String(), min_c(len, string.Length()));
	return *this;
}


// Prepend
/*!	\brief Prepends the given character to the object.
	\param c The character to prepend.
	\param count The amount of characters to prepend.
	\return This function always returns *this .
*/
BString&
BString::Prepend(char c, int32 count)
{
	_OpenAtBy(0, count);
	memset(_privateData, c, count);
	
	return *this;
}


/*---- Inserting ----------------------------------------------------------*/
// Insert
/*! \brief Inserts the given string at the given position into the object's data.
	\param str A pointer to the string to insert.
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(const char *str, int32 pos)
{
	if (str != NULL)
	{
		if (pos < 0)
		{
			str -= pos;
			pos = 0;
		}
		_privateData = _OpenAtBy(pos, strlen(str));
		memcpy(_privateData + pos, str, strlen(str));
	}
	return *this;
}


// Insert
/*! \brief Inserts the given string at the given position into the object's data.
	\param str A pointer to the string to insert.
	\param length The amount of bytes to insert.	
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(const char *str, int32 length, int32 pos)
{
	if (str != NULL)
	{
		if (pos < 0) 
		{
			str -= pos;
			pos = 0;
		}
		int32 len = (int32)strlen(str);
		len = min_c(len, length);
		_OpenAtBy(pos, len);
		memcpy(_privateData + pos, str, len);
	}
	return *this;
}


// Insert
/*! \brief Inserts the given string at the given position into the object's data.
	\param str A pointer to the string to insert.
	\param fromOffset
	\param length The amount of bytes to insert.	
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(const char *str, int32 fromOffset, int32 length, int32 pos)
{
	if (str != NULL) 
	{
		int32 len = (int32)strlen(str);
		len = min_c(len - fromOffset, length);
		_privateData = _OpenAtBy(pos, len);
		memcpy(_privateData + pos, str + fromOffset, len);
	}
	return *this;
}


// Insert
/*! \brief Inserts the given BString at the given position into the object's data.
	\param string The BString object to insert.
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(const BString &string, int32 pos)
{
	if (&string != this)
		Insert(string.String(), pos); //TODO: Optimize
	return *this;				  
}


// Insert
/*! \brief Inserts the given BString at the given position into the object's data.
	\param string The BString object to insert.
	\param length The amount of bytes to insert.
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(const BString &string, int32 length, int32 pos)
{
	if (&string != this)
		Insert(string.String(), length, pos); //TODO: Optimize
	return *this;
}


// Insert
/*! \brief Inserts the given string at the given position into the object's data.
	\param string The BString object to insert.
	\param fromOffset
	\param length The amount of bytes to insert.
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(const BString &string, int32 fromOffset, int32 length, int32 pos)
{
	if (&string != this)
		Insert(string.String(), fromOffset, length, pos); //TODO: Optimize
	return *this;
}


// Insert
/*! \brief Inserts the given character at the given position into the object's data.
	\param c The character to insert.
	\param count The amount of bytes to insert.
	\param pos The offset into the BString's data where to insert the string.
	\return This function always returns *this .
*/
BString&
BString::Insert(char c, int32 count, int32 pos)
{
	_OpenAtBy(pos, count);
	memset(_privateData + pos, c, count);
	
	return *this;
}


/*---- Removing -----------------------------------------------------------*/
// Truncate
/*! \brief Truncate the string to the new length.
	\param newLength The new lenght of the string.
	\param lazy Currently unused. (?)
	\return This function always returns *this .
*/
BString&
BString::Truncate(int32 newLength, bool lazy)
{
	if (newLength < 0)
		newLength = 0;
	
	int32 curLen = Length();
		
	if (newLength < curLen) 
	{

	//TODO: Implement lazy truncate?
	
		_GrowBy(newLength - curLen); //Negative	
		_privateData[newLength] = '\0';
	}
	return *this;
}


// Remove
/*! \brief Removes some bytes, starting at the given offset
	\param from The offset from which you want to start removing
	\param length The number of bytes to remove
	\return This function always returns *this .
*/
BString&
BString::Remove(int32 from, int32 length)
{
	_ShrinkAtBy(from, length);
	return *this;
}


// Remove
/*! \brief Removes the first occurrence of the given BString.
	\param string The BString to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveFirst(const BString &string)
{
	int32 pos = _ShortFindAfter(string.String(), string.Length());
	
	if (pos >= 0)
		_ShrinkAtBy(pos, string.Length());
	
	return *this;
}


// Remove
/*! \brief Removes the last occurrence of the given BString.
	\param string The BString to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveLast(const BString &string)
{
	int32 pos = _FindBefore(string.String(), Length(), string.Length());
	
	if (pos >= 0)
		_ShrinkAtBy(pos, string.Length());
		
	return *this;
}


// Remove
/*! \brief Removes all occurrences of the given BString.
	\param string The BString to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveAll(const BString &string)
{
	return _DoReplace( string.String(), "", 0x7FFFFFFF, 0, KEEP_CASE);
}


// Remove
/*! \brief Removes the first occurrence of the given string.
	\param str A pointer to the string to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveFirst(const char *str)
{
	if (str != NULL) 
	{
		int32 pos = _ShortFindAfter(str, strlen(str));
		if (pos >= 0)
			_ShrinkAtBy(pos, strlen(str));
	}
	return *this;
}


// Remove
/*! \brief Removes the last occurrence of the given string.
	\param str A pointer to the string to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveLast(const char *str)
{
	if (str != NULL)
	{
		int32 len = strlen(str);
		int32 pos = _FindBefore(str, Length(), len);
		if (pos >= 0)
			_ShrinkAtBy(pos, len);
	}
	return *this;
}


// Remove
/*! \brief Removes all occurrences of the given string.
	\param str A pointer to the string to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveAll(const char *str)
{
	return _DoReplace( str, "", 0x7FFFFFFF, 0, KEEP_CASE);
}


// Remove
/*! \brief Removes all the characters specified.
	\param setOfCharsToRemove The set of characters to remove.
	\return This function always returns *this .
*/
BString&
BString::RemoveSet(const char *setOfCharsToRemove)
{
	char* buf;
	if (setOfCharsToRemove && (buf=LockBuffer( Length())) != NULL) {
		char* oldPos = buf;
		char* newPos = buf;
		int32 len;
		int32 lenToGo = Length();
		while( (len = strcspn( oldPos, setOfCharsToRemove)) < lenToGo) {
			if (oldPos>newPos)
				memmove( newPos, oldPos, len);
			newPos += len++;
			oldPos += len;
			lenToGo -= len;
		}
		if (oldPos>newPos)
			memmove( newPos, oldPos, lenToGo);
		newPos += lenToGo;
		*newPos = 0;
		UnlockBuffer( newPos-buf);
	}		
	return *this;
}


// MoveInto
/*! \brief Move the BString data (or part of it) into another BString.
	\param into The BString where to move the object.
	\param from The offset (zero based) where to begin the move
	\param length The amount of bytes to move.
	\return This function always returns *this .
*/
BString&
BString::MoveInto(BString &into, int32 from, int32 length)
{
	if (&into == this)
		return *this;
	
	int32 len = Length() - from;
	
	len = min_c(len, length);
	
	into.SetTo(String() + from, length);
	
	if (from + length <= Length())
		_privateData = _ShrinkAtBy(from, len);

	return *this;
}


// MoveInto
/*! \brief Move the BString data (or part of it) into the given buffer.
	\param into The buffer where to move the object.
	\param from The offset (zero based) where to begin the move
	\param length The amount of bytes to move.
*/
void
BString::MoveInto(char *into, int32 from, int32 length)
{
	if (into != NULL)
	{
		memcpy(into, String() + from, length);
		if (from + length <= Length())
			_privateData = _ShrinkAtBy(from, length);
		into[length] = '\0';
	}		 
}


/*---- Compare functions ---------------------------------------------------*/
bool
BString::operator<(const char *string) const
{
	return strcmp(String(), string) < 0;
}


bool
BString::operator<=(const char *string) const
{
	return strcmp(String(), string) <= 0;
}


bool
BString::operator==(const char *string) const
{
	return strcmp(String(), string) == 0;
}


bool
BString::operator>=(const char *string) const
{
	return strcmp(String(), string) >= 0;
}


bool
BString::operator>(const char *string) const
{
	return strcmp(String(), string) > 0;
}


/*---- strcmp-style compare functions --------------------------------------*/
int
BString::Compare(const BString &string) const
{
	return strcmp(String(), string.String());
}


int
BString::Compare(const char *string) const
{
	return strcmp(String(), string);
}


int
BString::Compare(const BString &string, int32 n) const
{
	return strncmp(String(), string.String(), n);
}


int
BString::Compare(const char *string, int32 n) const
{
	return strncmp(String(), string, n);
}


int
BString::ICompare(const BString &string) const
{
	return strcasecmp(String(), string.String());
}


int
BString::ICompare(const char *str) const
{
	return strcasecmp(String(), str);
}


int
BString::ICompare(const BString &string, int32 n) const
{
	return strncasecmp(String(), string.String(), n);
}


int
BString::ICompare(const char *str, int32 n) const
{
	return strncasecmp(String(), str, n);
}


/*---- Searching -----------------------------------------------------------*/
// FindFirst
/*! \brief Find the first occurrence of the given BString.
	\param string The BString to search for.
	\return The offset(zero based) into the data
		where the given BString has been found.
*/
int32
BString::FindFirst(const BString &string) const
{
	return _ShortFindAfter(string.String(), string.Length());
}


// FindFirst
/*! \brief Find the first occurrence of the given string.
	\param string The string to search for.
	\return The offset(zero based) into the data
		where the given string has been found.
*/
int32
BString::FindFirst(const char *string) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _ShortFindAfter(string, strlen(string));
}


// FindFirst
/*! \brief Find the first occurrence of the given BString,
		starting from the given offset.
	\param string The BString to search for.
	\param fromOffset The offset where to start the search.
	\return An integer which is the offset(zero based) into the data
		where the given BString has been found.
*/
int32
BString::FindFirst(const BString &string, int32 fromOffset) const
{
	return _FindAfter(string.String(), fromOffset, string.Length());
}


// FindFirst
/*! \brief Find the first occurrence of the given string,
		starting from the given offset.
	\param string The string to search for.
	\param fromOffset The offset where to start the search.
	\return The offset(zero based) into the data
		where the given string has been found.
*/
int32
BString::FindFirst(const char *string, int32 fromOffset) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _FindAfter(string, fromOffset, strlen(string));
}


// FindFirst
/*! \brief Find the first occurrence of the given character.
	\param c The character to search for.
	\return The offset(zero based) into the data
		where the given character has been found.
*/
int32
BString::FindFirst(char c) const
{	
	char *start = _privateData;
	char *end = _privateData + Length(); /* String's end */
	
	/* Scans the string until we found the character, */
	/* or we hit the string's end */
	while(start != end && *start != c)
		start++;
	
	if (start == end)
		return B_ERROR;
			
	return start - _privateData;
}


// FindFirst
/*! \brief Find the first occurrence of the given character,
		starting from the given offset.
	\param c The character to search for.
	\param fromOffset The offset where to start the search.
	\return The offset(zero based) into the data
		where the given character has been found.
*/
int32
BString::FindFirst(char c, int32 fromOffset) const
{
	char *start = _privateData + fromOffset;
	char *end = _privateData + Length(); /* String's end */
	
	/* Scans the string until we found the character, */
	/* or we hit the string's end */
	while(start < end && *start != c)
		start++;
	
	if (start >= end)
		return B_ERROR;
			
	return start - _privateData;
}


// FindLast
/*! \brief Find the last occurrence of the given BString.
	\param string The BString to search for.
	\return The offset(zero based) into the data
		where the given BString has been found.
*/
int32
BString::FindLast(const BString &string) const
{
	return _FindBefore(string.String(), Length(), string.Length());
}


// FindLast
/*! \brief Find the last occurrence of the given string.
	\param string The string to search for.
	\return The offset(zero based) into the data
		where the given string has been found.
*/
int32
BString::FindLast(const char *string) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _FindBefore(string, Length(), strlen(string));
}


// FindLast
/*! \brief Find the last occurrence of the given BString,
		starting from the given offset, and going backwards.
	\param string The BString to search for.
	\param beforeOffset The offset where to start the search.
	\return An integer which is the offset(zero based) into the data
		where the given BString has been found.
*/
int32
BString::FindLast(const BString &string, int32 beforeOffset) const
{
	return _FindBefore(string.String(), beforeOffset, string.Length()); 
}


// FindLast
/*! \brief Find the last occurrence of the given string,
		starting from the given offset, and going backwards.
	\param string The string to search for.
	\return The offset(zero based) into the data
		where the given string has been found.
*/
int32
BString::FindLast(const char *string, int32 beforeOffset) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _FindBefore(string, beforeOffset, strlen(string));
}


// FindLast
/*! \brief Find the last occurrence of the given character.
	\param c The character to search for.
	\return The offset(zero based) into the data
		where the given character has been found.
*/
int32
BString::FindLast(char c) const
{
	char *start = _privateData;
	char *end = _privateData + Length(); /* String's end */
	
	/* Scans the string backwards until we found the character, */
	/* or we reach the string's start */
	while(end != start && *end != c)
		end--;
	
	if (end == start)
		return B_ERROR;
			
	return end - _privateData;
}


// FindLast
/*! \brief Find the last occurrence of the given character,
		starting from the given offset and going backwards.
	\param c The character to search for.
	\param beforeOffset The offset where to start the search.
	\return The offset(zero based) into the data
		where the given character has been found.
*/
int32
BString::FindLast(char c, int32 beforeOffset) const
{
	char *start = _privateData;
	char *end = _privateData + Length() - beforeOffset;
	
	/* Scans the string backwards until we found the character, */
	/* or we reach the string's start */
	while(end > start && *end != c)
		end--;
	
	if (end <= start)
		return B_ERROR;
			
	return end - _privateData;
}


int32
BString::IFindFirst(const BString &string) const
{
	return _IFindAfter(string.String(), 0, string.Length());
}


int32
BString::IFindFirst(const char *string) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _IFindAfter(string, 0, strlen(string));
}


int32
BString::IFindFirst(const BString &string, int32 fromOffset) const
{
	return _IFindAfter(string.String(), fromOffset, string.Length());
}


int32
BString::IFindFirst(const char *string, int32 fromOffset) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _IFindAfter(string, fromOffset, strlen(string));
}


int32
BString::IFindLast(const BString &string) const
{
	return _IFindBefore(string.String(), Length(), string.Length());
}


int32
BString::IFindLast(const char *string) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _IFindBefore(string, Length(), strlen(string));
}


int32
BString::IFindLast(const BString &string, int32 beforeOffset) const
{
	return _IFindBefore(string.String(), beforeOffset, string.Length());
}


int32
BString::IFindLast(const char *string, int32 beforeOffset) const
{
	if (string == NULL)
		return B_BAD_VALUE;
	return _IFindBefore(string, beforeOffset, strlen(string));
}


/*---- Replacing -----------------------------------------------------------*/
BString&
BString::ReplaceFirst(char replaceThis, char withThis)
{
	int32 pos = FindFirst(replaceThis);
	
	if (pos >= 0)
		_privateData[pos] = withThis;
	
	return *this;
}


BString&
BString::ReplaceLast(char replaceThis, char withThis)
{
	int32 pos = FindLast(replaceThis);
	
	if (pos >= 0)
		_privateData[pos] = withThis;
	
	return *this;
}


BString&
BString::ReplaceAll(char replaceThis, char withThis, int32 fromOffset)
{
	for (int32 pos;;) 
	{
		pos = FindFirst(replaceThis, fromOffset);
		if (pos < 0)
			break;
		_privateData[pos] = withThis;
		fromOffset = pos;
	}
	
	return *this;
}


BString&
BString::Replace(char replaceThis, char withThis, int32 maxReplaceCount, int32 fromOffset)
{
	for (int32 pos ; maxReplaceCount > 0 ; maxReplaceCount--) 
	{
		pos = FindFirst(replaceThis, fromOffset);
		if (pos < 0)
			break;
		
		_privateData[pos] = withThis;
		fromOffset = pos;
		
	}
	return *this;
}


BString&
BString::ReplaceFirst(const char *replaceThis, const char *withThis)
{
	return _DoReplace( replaceThis, withThis, 1, 0, KEEP_CASE);
}


BString&
BString::ReplaceLast(const char *replaceThis, const char *withThis)
{
	if (replaceThis == NULL)
		return *this;
		
	int32 firstStringLength = strlen(replaceThis);	
	int32 pos = _FindBefore(replaceThis, Length(), firstStringLength);
	
	if (pos >= 0) 
	{
		int32 len = (withThis ? strlen(withThis) : 0);
		int32 difference = len - firstStringLength;
		
		if (difference > 0)
			_OpenAtBy(pos, difference);
		else if (difference < 0)
			_ShrinkAtBy(pos, -difference);
		
		memcpy(_privateData + pos, withThis, len);
	}
		
	return *this;
}


BString&
BString::ReplaceAll(const char *replaceThis, const char *withThis, int32 fromOffset)
{
	return _DoReplace( replaceThis, withThis, 0x7FFFFFFF, fromOffset, KEEP_CASE);
}


BString&
BString::Replace(const char *replaceThis, const char *withThis, int32 maxReplaceCount, int32 fromOffset)
{
	return _DoReplace( replaceThis, withThis, maxReplaceCount, fromOffset, KEEP_CASE);
}


BString&
BString::IReplaceFirst(char replaceThis, char withThis)
{
	char tmp[2] = { replaceThis, '\0' };
	int32 pos = _IFindAfter(tmp, 0, 1);
	
	if (pos >= 0)
		_privateData[pos] = withThis;

	return *this;
}


BString&
BString::IReplaceLast(char replaceThis, char withThis)
{
	char tmp[2] = { replaceThis, '\0' };	
	int32 pos = _IFindBefore(tmp, Length(), 1);
	
	if (pos >= 0)
		_privateData[pos] = withThis;
	
	return *this;
}


BString&
BString::IReplaceAll(char replaceThis, char withThis, int32 fromOffset)
{
	char tmp[2] = { replaceThis, '\0' };
	
	for (int32 pos;;) 
	{
		pos = _IFindAfter(tmp, fromOffset, 1);
		if (pos < 0)
			break;
		_privateData[pos] = withThis;
		fromOffset = pos;
	}
	return *this;
}


BString&
BString::IReplace(char replaceThis, char withThis, int32 maxReplaceCount, int32 fromOffset)
{
	char tmp[2] = { replaceThis, '\0' };
	
	if (_privateData == NULL)
		return *this;
		
	for (int32 pos ; maxReplaceCount > 0 ; maxReplaceCount--) 
	{	
		pos = _IFindAfter(tmp, fromOffset, 1);
		if (pos < 0)
			break;
		
		_privateData[pos] = withThis;
		fromOffset = pos;
	}
	return *this;
}


BString&
BString::IReplaceFirst(const char *replaceThis, const char *withThis)
{
	return _DoReplace( replaceThis, withThis, 1, 0, IGNORE_CASE);
}


BString&
BString::IReplaceLast(const char *replaceThis, const char *withThis)
{
	if (replaceThis == NULL)
		return *this;
		
	int32 firstStringLength = strlen(replaceThis);		
	int32 pos = _IFindBefore(replaceThis, Length(), firstStringLength);
	
	if (pos >= 0) 
	{
		int32 len = (withThis ? strlen(withThis) : 0);
		int32 difference = len - firstStringLength;
		
		if (difference > 0)
			_OpenAtBy(pos, difference);
		else if (difference < 0)
			_ShrinkAtBy(pos, -difference);
		
		memcpy(_privateData + pos, withThis, len);
	}
		
	return *this;
}


BString&
BString::IReplaceAll(const char *replaceThis, const char *withThis, int32 fromOffset)
{
	return _DoReplace( replaceThis, withThis, 0x7FFFFFFF, fromOffset, IGNORE_CASE);
}


BString&
BString::IReplace(const char *replaceThis, const char *withThis, int32 maxReplaceCount, int32 fromOffset)
{
	return _DoReplace( replaceThis, withThis, maxReplaceCount, fromOffset, IGNORE_CASE);
}


BString&
BString::ReplaceSet(const char *setOfChars, char with)
{
	int32 offset = 0;
	int32 length = Length();
	
	for (int32 pos;;) 
	{
		pos = strcspn(String() + offset, setOfChars);
		if (pos >= length)
			break;
		
		offset += pos;
		
		if (offset >= length)
			break;

		_privateData[offset] = with;
		offset++;
	}

	return *this;
}


BString&
BString::ReplaceSet(const char *setOfChars, const char *with)
{
	if (with == NULL)
		return *this; //TODO: do something smart
	
	int32 offset = 0;
	int32 withLen = strlen(with);
	
	for (int32 pos;;) 
	{
		pos = strcspn(String() + offset, setOfChars);
		if (pos >= Length())
			break;
		
		offset += pos;
		
		if (offset >= Length())
			break;
		
		_OpenAtBy(offset, withLen - 1);
		memcpy(_privateData + offset, with, withLen);
		offset += withLen;
	}
	
	return *this;
}


/*---- Unchecked char access -----------------------------------------------*/

// operator[]
/*! \brief Returns a reference to the data at the given offset.
	
	This function can be used to read a byte or to change its value.
	\param index The index (zero based) of the byte to get.
	\return Returns a reference to the specified byte.
*/
char &
BString::operator[](int32 index)
{
	return _privateData[index];
}


/*---- Fast low-level manipulation -----------------------------------------*/
char*
BString::LockBuffer(int32 maxLength)
{
	_SetUsingAsCString(true); //debug
	
	int32 len = Length();
	
	if (maxLength > len)
		_GrowBy(maxLength - len);

	return _privateData;
}


BString&
BString::UnlockBuffer(int32 length)
{
	_SetUsingAsCString(false); //debug
	
	int32 len = length;

	if (len < 0)
		len = (_privateData == NULL) ? 0 : strlen(_privateData);

	if (len != Length())
		_GrowBy(len - Length());
		
	return *this;
}


/*---- Uppercase<->Lowercase ------------------------------------------------*/
// ToLower
/*! \brief Converts the BString to lowercase
	\return This function always returns *this .
*/
BString&
BString::ToLower()
{
	int32 length = Length();
	for (int32 count = 0; count < length; count++)
			_privateData[count] = tolower(_privateData[count]);
	
	return *this;
}


// ToUpper
/*! \brief Converts the BString to uppercase
	\return This function always returns *this .
*/
BString&
BString::ToUpper()
{			
	int32 length = Length();
	for (int32 count = 0; count < length; count++)
			_privateData[count] = toupper(_privateData[count]);
	
	return *this;
}


// Capitalize
/*! \brief Converts the first character to uppercase, rest to lowercase
	\return This function always returns *this .
*/
BString&
BString::Capitalize()
{
	if (_privateData == NULL)
		return *this;
		
	_privateData[0] = toupper(_privateData[0]);
	int32 length = Length();
		
	for (int32 count = 1; count < length; count++)
			_privateData[count] = tolower(_privateData[count]);

	return *this;
}


// CapitalizeEachWord
/*! \brief Converts the first character of every word to uppercase, rest to lowercase.
	
	Converts the first character of every "word" (series of alpabetical characters
	separated by non alphabetical characters) to uppercase, and the rest to lowercase.
	\return This function always returns *this .
*/
BString&
BString::CapitalizeEachWord()
{
	if (_privateData == NULL)
		return *this;
		
	int32 count = 0;
	int32 length = Length();
		
	do {
		// Find the first alphabetical character...
		for(; count < length; count++) 
		{
			if (isalpha(_privateData[count])) 
			{
				// ...found! Convert it to uppercase.
				_privateData[count] = toupper(_privateData[count]);
				count++;
				break;
			}
		}
		// Now find the first non-alphabetical character,
		// and meanwhile, turn to lowercase all the alphabetical ones
		for(; count < length; count++) 
		{
			if (isalpha(_privateData[count]))
				_privateData[count] = tolower(_privateData[count]);
			else
				break;
		}
	} while (count < length);
				
	return *this;
}


/*----- Escaping and Deescaping --------------------------------------------*/
BString&
BString::CharacterEscape(const char *original, const char *setOfCharsToEscape, char escapeWith)
{
	SetTo(original);
	CharacterEscape(setOfCharsToEscape, escapeWith);
	
	return *this;
}


BString&
BString::CharacterEscape(const char *setOfCharsToEscape, char escapeWith)
{
	if (setOfCharsToEscape == NULL || _privateData == NULL)
		return *this;
	
	int32 offset = 0;
	
	for(int32 pos;;) 
	{
		pos = strcspn(_privateData + offset, setOfCharsToEscape);
		offset += pos;
		if (offset >= Length())
			break;
		_OpenAtBy(offset, 1);
		memset(_privateData + offset, escapeWith, 1);
		offset += 2;
	}
		
	return *this;
}


BString&
BString::CharacterDeescape(const char *original, char escapeChar)
{
	SetTo(original);	
	CharacterDeescape(escapeChar);
		
	return *this;
}


BString&
BString::CharacterDeescape(char escapeChar)
{
	int32 pos;	
	while ((pos = FindFirst(escapeChar)) >= 0)
		_ShrinkAtBy(pos, 1);
			
	return *this;
}


/*---- Simple sprintf replacement calls ------------------------------------*/
/*---- Slower than sprintf but type and overflow safe ----------------------*/
BString&
BString::operator<<(const char *str)
{
	if (str != NULL)
		_DoAppend(str, strlen(str));
	return *this;	
}


BString&
BString::operator<<(const BString &string)
{
	_DoAppend(string.String(), string.Length());
	return *this;
}


BString&
BString::operator<<(char c)
{
	_DoAppend(&c, 1);	
	return *this;
}


BString&
BString::operator<<(int i)
{
	char num[64];
	sprintf(num, "%d", i);
	
	return *this << num;
}


BString&
BString::operator<<(unsigned int i)
{
	char num[64];
	sprintf(num, "%u", i);
	
	return *this << num;
}


BString&
BString::operator<<(uint32 i)
{
	char num[64];
	sprintf(num, "%lu", i);
	
	return *this << num;
}


BString&
BString::operator<<(int32 i)
{
	char num[64];
	sprintf(num, "%ld", i);
	
	return *this << num;
}


BString&
BString::operator<<(uint64 i)
{
	char num[64];
	sprintf(num, "%llu", i);
	
	return *this << num;
}


BString&
BString::operator<<(int64 i)
{
	char num[64];
	sprintf(num, "%lld", i);
	
	return *this << num;
}


BString&
BString::operator<<(float f)
{
	char num[64];
	sprintf(num, "%.2f", f);
	
	return *this << num;
}


/*---- Private or Reserved ------------------------------------------------*/
void
BString::_Init(const char* str, int32 len)
{
	ASSERT(str != NULL);
	ASSERT(_privateData == NULL);

	_privateData = (char*)malloc(len + sizeof(int32) + 1);	
	_privateData += sizeof(int32);
	
	memcpy(_privateData, str, len);
	
	_SetLength(len);
	_privateData[len] = '\0';	
}


#if ENABLE_INLINES
inline
#endif
void
BString::_DoAssign(const char *str, int32 len)
{
	ASSERT(str != NULL);	
	int32 curLen = Length();
	
	if (len != curLen)
		_GrowBy(len - curLen);
	
	memcpy(_privateData, str, len);
}


#if ENABLE_INLINES
inline
#endif
void
BString::_DoAppend(const char *str, int32 len)
{
	ASSERT(str != NULL);
	
	int32 length = Length();
	_GrowBy(len);
	memcpy(_privateData + length, str, len);
}


char*
BString::_GrowBy(int32 size)
{		
	int32 newLen = Length() + size; 	
	ASSERT(newLen >= 0);
	
	if (_privateData != NULL)
		_privateData -= sizeof(int32);
		
	_privateData = (char*)realloc(_privateData, 
		newLen + sizeof(int32) + 1);
		
	_privateData += sizeof(int32);
	
	_SetLength(newLen);	
	_privateData[newLen] = '\0';
	
	return _privateData;
}


char *
BString::_OpenAtBy(int32 offset, int32 length)
{
	ASSERT(offset >= 0);
			
	int32 oldLength = Length();
	
	if (_privateData != NULL)
		_privateData -= sizeof(int32);
	
	_privateData = (char*)realloc(_privateData, oldLength + length + sizeof(int32) + 1);
	_privateData += sizeof(int32);
	
	memmove(_privateData + offset + length, _privateData + offset,
			oldLength - offset);
	
	_SetLength(oldLength + length);
	_privateData[Length()] = '\0';
	
	return _privateData;	
}


char*
BString::_ShrinkAtBy(int32 offset, int32 length)
{	
	int32 oldLength = Length();
	
	if (offset > oldLength || offset + length > oldLength)
		return _privateData;

	memmove(_privateData + offset, _privateData + offset + length,
		oldLength - offset - length);
	
	_privateData -= sizeof(int32);	
	_privateData = (char*)realloc(_privateData, oldLength - length + sizeof(int32) + 1);
	_privateData += sizeof(int32);
	
	_SetLength(oldLength - length);	
	_privateData[Length()] = '\0';
	
	return _privateData;
}


#if ENABLE_INLINES
inline
#endif
void
BString::_DoPrepend(const char *str, int32 count)
{
	ASSERT(str != NULL);
	_OpenAtBy(0, count);
	memcpy(_privateData, str, count);
}


/* XXX: These could be inlined too, if they are too slow */
int32
BString::_FindAfter(const char *str, int32 offset, int32 strlen) const
{	
	ASSERT(str != NULL);
	
	if (offset > Length())
		return B_ERROR;

	char *ptr = strstr(String() + offset, str);

	if (ptr != NULL)
		return ptr - String();
	
	return B_ERROR;
}


int32
BString::_IFindAfter(const char *str, int32 offset, int32 strlen) const
{
	ASSERT(str != NULL);

	if (offset > Length())
		return B_ERROR;

	char *ptr = strcasestr(String() + offset, str);

	if (ptr != NULL)
		return ptr - String();

	return B_ERROR;
}


int32
BString::_ShortFindAfter(const char *str, int32 len) const
{
	ASSERT(str != NULL);
	
	char *ptr = strstr(String(), str);
	
	if (ptr != NULL)
		return ptr - String();
		
	return B_ERROR;
}


int32
BString::_FindBefore(const char *str, int32 offset, int32 strlen) const
{
	ASSERT(str != NULL);
	
	if (offset <= 0)
		return B_ERROR;
	
	const char *ptr = _privateData + offset - strlen;
	
	while (ptr >= _privateData)
	{	
		if (!memcmp(ptr, str, strlen))
			return ptr - _privateData; 
		ptr--;
	}
	
	return B_ERROR;
}


int32
BString::_IFindBefore(const char *str, int32 offset, int32 strlen) const
{
	ASSERT(str != NULL);
	
	if (offset <= 0)
		return B_ERROR;
			
	char *ptr1 = _privateData + offset - strlen;
	
	while (ptr1 >= _privateData) 
	{
		if (!strncasecmp(ptr1, str, strlen))
			return ptr1 - _privateData; 
		ptr1--;
	}
	
	return B_ERROR;
}


/*!
	BStringOBuf is helper classed used by _DoReplace()
*/
class BStringOBuf
{

public:
	BStringOBuf( uint32 startLen, float growFactor=1.5);
	~BStringOBuf();

	// methods to write into buffer (always append):
	uint32 Write( const char* data, uint32 len);
	
	// getters:
	BString& TheString();
	inline bool HasData() const 			{ return mBuf!=NULL; }

private:
	bool GrowBufferToFit( uint32 len);

	uint32 mBufLen;
	float mGrowFactor;
	char* mBuf;
	uint32 mCurrPos;
	BString mStr;

	// Hide copy-constructor and assignment:
	BStringOBuf( const BStringOBuf&);
	BStringOBuf operator=( const BStringOBuf&);
};


BString&
BString::_DoReplace(const char *findThis, const char *replaceWith, int32 maxReplaceCount, 
						  int32 fromOffset,	bool ignoreCase)
{
	if (findThis == NULL || maxReplaceCount <= 0 || fromOffset < 0 || fromOffset >= Length())
		return *this;
	
	typedef int32 (BString::*TFindMethod)(const char *, int32, int32) const;
	TFindMethod findMethod = ignoreCase
		? &BString::_IFindAfter
		: &BString::_FindAfter;
	int32 findLen = strlen( findThis);
	int32 replaceLen = replaceWith ? strlen( replaceWith) : 0;
	BStringOBuf tempIO( (int32)max_c( max_c( findLen, 128), Length()*1.2), 1.2);
	int32 lastSrcPos = fromOffset;
	int32 len;
	for(  int32 srcPos=0; 
			maxReplaceCount>0 && (srcPos = (this->*findMethod)( findThis, lastSrcPos, findLen))!=B_ERROR; 
			maxReplaceCount-- ) {
		len = srcPos-lastSrcPos;
		if (fromOffset && !tempIO.HasData())
			tempIO.Write( String(), fromOffset);
		tempIO.Write( String()+lastSrcPos, len);
		tempIO.Write( replaceWith, replaceLen);
		lastSrcPos = srcPos+findLen;
	}
	if (tempIO.HasData()) {
		// only copy remainder if we have actually changed anything
		if ((len = Length()-lastSrcPos)!=0) {
			if (fromOffset && !tempIO.HasData())
				tempIO.Write( String(), fromOffset);
			tempIO.Write( String()+lastSrcPos, len);
		}
		Adopt( tempIO.TheString());
	}
	return *this;
}

#if ENABLE_INLINES
inline
#endif
void
BString::_SetLength(int32 length)
{
	*((int32*)_privateData - 1) = length & 0x7fffffff;
}


#if DEBUG
// AFAIK, these are not implemented in BeOS R5
// XXX : Test these puppies
void
BString::_SetUsingAsCString(bool state)
{	
	//TODO: Implement ?		
}


void
BString::_AssertNotUsingAsCString() const
{
	//TODO: Implement ?
}
#endif


/*----- Non-member compare for sorting, etc. ------------------------------*/
int
Compare(const BString &string1, const BString &string2)
{
	return strcmp(string1.String(), string2.String());
}


int
ICompare(const BString &string1, const BString &string2)
{
	return strcasecmp(string1.String(), string2.String());
}


int
Compare(const BString *string1, const BString *string2)
{
	return strcmp(string1->String(), string2->String());
}


int
ICompare(const BString *string1, const BString *string2)
{
	return strcasecmp(string1->String(), string2->String());
}


/*----- Implementation of BStringOBuf ------------------------------*/
BStringOBuf::BStringOBuf(uint32 startLen, float growFactor)
	:mBufLen(startLen),
	mGrowFactor(max_c((float)1.0, growFactor)),
	mBuf(NULL),
	mCurrPos(0)
{
}


BStringOBuf::~BStringOBuf() 
{
	if (mBuf)
		mStr.UnlockBuffer(mCurrPos);
}


bool
BStringOBuf::GrowBufferToFit(uint32 len) 
{
	if (!mBuf || mCurrPos+len > mBufLen) {
		if (mBuf) {
			mStr.UnlockBuffer(mBufLen);
			mBufLen = (uint32)max_c(mGrowFactor*mBufLen, mGrowFactor*(mCurrPos+len));
		} else
			mBufLen = (uint32)max_c(mBufLen, mCurrPos+len);
		mBuf = mStr.LockBuffer(mBufLen);
		if (!mBuf)
			return false;
	}
	return true;
}


uint32
BStringOBuf::Write(const char* data, uint32 len)
{
	if (!len || !GrowBufferToFit( len))
		return 0;
	memcpy( mBuf+mCurrPos, data, len);
	mCurrPos += len;
	return len;
}


BString&
BStringOBuf::TheString()
{
	if (mBuf) {
		mBuf[mCurrPos] = '\0';
		mStr.UnlockBuffer( mCurrPos);
		mBuf = NULL;
	}
	return mStr;
}

