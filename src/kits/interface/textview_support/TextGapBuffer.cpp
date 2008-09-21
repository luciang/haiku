/*
 * Copyright 2001-2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stefano Ceccherini (burton666@libero.it)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <utf8_functions.h>

#include <File.h>
#include <InterfaceDefs.h> // for B_UTF8_BULLET

#include "TextGapBuffer.h"

namespace BPrivate {

TextGapBuffer::TextGapBuffer()
	:	fExtraCount(2048),
		fItemCount(0),
		fBuffer(NULL),
		fBufferCount(fExtraCount + fItemCount),
		fGapIndex(fItemCount),
		fGapCount(fBufferCount - fGapIndex),
		fScratchBuffer(NULL),
		fScratchSize(0),
		fPasswordMode(false)
{
	fBuffer = (char *)malloc(fExtraCount + fItemCount);
	fScratchBuffer = NULL;
}


TextGapBuffer::~TextGapBuffer()
{
	free(fBuffer);
	free(fScratchBuffer);
}


void
TextGapBuffer::InsertText(const char *inText, int32 inNumItems, int32 inAtIndex)
{
	if (inNumItems < 1)
		return;
	
	inAtIndex = (inAtIndex > fItemCount) ? fItemCount : inAtIndex;
	inAtIndex = (inAtIndex < 0) ? 0 : inAtIndex;
		
	if (inAtIndex != fGapIndex)
		MoveGapTo(inAtIndex);
	
	if (fGapCount < inNumItems)
		SizeGapTo(inNumItems + fExtraCount);
		
	memcpy(fBuffer + fGapIndex, inText, inNumItems);
	
	fGapCount -= inNumItems;
	fGapIndex += inNumItems;
	fItemCount += inNumItems;
}


void
TextGapBuffer::InsertText(BFile *file, int32 fileOffset, int32 inNumItems, int32 inAtIndex)
{
	off_t fileSize;

	if (file->GetSize(&fileSize) != B_OK
		|| !file->IsReadable())
		return;
	
	// Clamp the text length to the file size
	fileSize -= fileOffset;

	if (fileSize < inNumItems)
		inNumItems = fileSize;

	if (inNumItems < 1)
		return;

	inAtIndex = (inAtIndex > fItemCount) ? fItemCount : inAtIndex;
	inAtIndex = (inAtIndex < 0) ? 0 : inAtIndex;
		
	if (inAtIndex != fGapIndex)
		MoveGapTo(inAtIndex);
	
	if (fGapCount < inNumItems)
		SizeGapTo(inNumItems + fExtraCount);
	
	// Finally, read the data and put it into the buffer
	if (file->ReadAt(fileOffset, fBuffer + fGapIndex, inNumItems) > 0) {
		fGapCount -= inNumItems;
		fGapIndex += inNumItems;
		fItemCount += inNumItems;
	}
}


void
TextGapBuffer::RemoveRange(int32 start, int32 end)
{
	long inAtIndex = start;
	long inNumItems = end - start;
	
	if (inNumItems < 1)
		return;
	
	inAtIndex = (inAtIndex > fItemCount - 1) ? (fItemCount - 1) : inAtIndex;
	inAtIndex = (inAtIndex < 0) ? 0 : inAtIndex;
	
	MoveGapTo(inAtIndex);
	
	fGapCount += inNumItems;
	fItemCount -= inNumItems;
	
	if (fGapCount > fExtraCount)
		SizeGapTo(fExtraCount);	
}


void
TextGapBuffer::MoveGapTo(int32 toIndex)
{
	if (toIndex == fGapIndex)
		return;
	
	long gapEndIndex = fGapIndex + fGapCount;
	long srcIndex = 0;
	long dstIndex = 0;
	long count = 0;
	if (toIndex > fGapIndex) {
		long trailGapCount = fBufferCount - gapEndIndex;
		srcIndex = toIndex + (gapEndIndex - toIndex);
		dstIndex =  fGapIndex;
		count = fGapCount + (toIndex - srcIndex);
		count = (count > trailGapCount) ? trailGapCount : count;
	} else {
		srcIndex = toIndex;
		dstIndex = toIndex + (gapEndIndex - fGapIndex);
		count = gapEndIndex - dstIndex;
	}
	
	if (count > 0)
		memmove(fBuffer + dstIndex, fBuffer + srcIndex, count);	

	fGapIndex = toIndex;
}


void
TextGapBuffer::SizeGapTo(long inCount)
{
	if (inCount == fGapCount)
		return;
		
	fBuffer = (char *)realloc(fBuffer, fItemCount + inCount);
	memmove(fBuffer + fGapIndex + inCount, 
			fBuffer + fGapIndex + fGapCount, 
			fBufferCount - (fGapIndex + fGapCount));

	fGapCount = inCount;
	fBufferCount = fItemCount + fGapCount;
}


const char *
TextGapBuffer::GetString(int32 fromOffset, int32 *_numBytes)
{
	char *result = "";
	if (_numBytes == NULL)
		return result;

	int32 numBytes = *_numBytes;
	if (numBytes < 1)
		return result;
	
	bool isStartBeforeGap = (fromOffset < fGapIndex);
	bool isEndBeforeGap = ((fromOffset + numBytes - 1) < fGapIndex);

	if (isStartBeforeGap == isEndBeforeGap) {
		result = fBuffer + fromOffset;
		if (!isStartBeforeGap)
			result += fGapCount;
	
	} else {
		if (fScratchSize < numBytes) {
			fScratchBuffer = (char *)realloc(fScratchBuffer, numBytes);
			fScratchSize = numBytes;
		}
		
		for (long i = 0; i < numBytes; i++)
			fScratchBuffer[i] = RealCharAt(fromOffset + i);

		result = fScratchBuffer;
	}

	// TODO: this could be improved. We are overwriting what we did some lines ago,
	// we could just avoid to do that.
	if (fPasswordMode) {
		uint32 numChars = UTF8CountChars(result, numBytes);
		uint32 charLen = UTF8CountBytes(B_UTF8_BULLET, 1);
		uint32 newSize = numChars * charLen;

		if ((uint32)fScratchSize < newSize) {
			fScratchBuffer = (char *)realloc(fScratchBuffer, newSize);
			fScratchSize = newSize;
		}
		result = fScratchBuffer;

		char *scratchPtr = result;
		for (uint32 i = 0; i < numChars; i++) {
			memcpy(scratchPtr, B_UTF8_BULLET, charLen);
			scratchPtr += charLen;
		}

		*_numBytes = newSize;	
	}

	return result;
}


bool 
TextGapBuffer::FindChar(char inChar, long fromIndex, long *ioDelta)
{
	long numChars = *ioDelta;
	for (long i = 0; i < numChars; i++) {
		char realChar = RealCharAt(fromIndex + i);
		if ((realChar & 0xc0) == 0x80)
			continue;
		if (realChar == inChar) {
			*ioDelta = i;
			return true;
		}
	}
	
	return false;
}


const char *
TextGapBuffer::Text()
{
	const char *realText = RealText();
	
	if (fPasswordMode) {
		const uint32 numChars = UTF8CountChars(realText, Length());
		const uint32 bulletCharLen = UTF8CountBytes(B_UTF8_BULLET, 1);
		uint32 newSize = numChars * bulletCharLen + 1;
		
		if ((uint32)fScratchSize < newSize) {
			fScratchBuffer = (char *)realloc(fScratchBuffer, newSize);
			fScratchSize = newSize;
		}
		
		char *scratchPtr = fScratchBuffer;
		for (uint32 i = 0; i < numChars; i++) {
			memcpy(scratchPtr, B_UTF8_BULLET, bulletCharLen);
			scratchPtr += bulletCharLen;
		}
		scratchPtr = '\0';

		return fScratchBuffer;
	}
	
	return realText;
}


const char *
TextGapBuffer::RealText()
{
	MoveGapTo(fItemCount);
	fBuffer[fItemCount] = '\0';
	
	return fBuffer;
}


void
TextGapBuffer::GetString(int32 offset, int32 length, char *buffer)
{
	if (buffer == NULL)
		return;

	int32 textLen = Length();
	
	if (offset < 0 || offset > (textLen - 1) || length < 1) {
		buffer[0] = '\0';
		return;
	}
	
	length = ((offset + length) > textLen) ? textLen - offset : length;

	bool isStartBeforeGap = (offset < fGapIndex);
	bool isEndBeforeGap = ((offset + length - 1) < fGapIndex);

	if (isStartBeforeGap == isEndBeforeGap) {
		char *source = fBuffer + offset;
		if (!isStartBeforeGap)
			source += fGapCount;
	
		memcpy(buffer, source, length);
	
	} else {		
		// if we are here, it can only be that start is before gap,
		// and the end is after gap.

		int32 beforeLen = fGapIndex - offset;
		int32 afterLen = length - beforeLen;

		memcpy(buffer, fBuffer + offset, beforeLen);
		memcpy(buffer + beforeLen, fBuffer + fGapIndex, afterLen);
			
	}
	
	buffer[length] = '\0';
}


bool
TextGapBuffer::PasswordMode() const
{
	return fPasswordMode;
}


void
TextGapBuffer::SetPasswordMode(bool state)
{
	fPasswordMode = state;
}

} // namespace BPrivate

