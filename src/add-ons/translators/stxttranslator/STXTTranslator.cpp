/*****************************************************************************/
// STXTTranslator
// Written by Michael Wilber, OBOS Translation Kit Team
//
// STXTTranslator.cpp
//
// This BTranslator based object is for opening and writing 
// StyledEdit (STXT) files.
//
//
// Copyright (c) 2002 OpenBeOS Project
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the 
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included 
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
/*****************************************************************************/

#include <string.h>
#include <stdio.h>
#include "STXTTranslator.h"
#include "STXTView.h"

#define min(x,y) ((x < y) ? x : y)
#define max(x,y) ((x > y) ? x : y)

#define READ_BUFFER_SIZE 512

// The input formats that this translator supports.
translation_format gInputFormats[] = {
	{
		B_TRANSLATOR_TEXT,
		B_TRANSLATOR_TEXT,
		TEXT_IN_QUALITY,
		TEXT_IN_CAPABILITY,
		"text/plain",
		"Plain text file"
	},
	{
		B_STYLED_TEXT_FORMAT,
		B_TRANSLATOR_TEXT,
		STXT_IN_QUALITY,
		STXT_IN_CAPABILITY,
		"text/x-vnd.Be-stxt",
		"Be styled text file"
	}
};

// The output formats that this translator supports.
translation_format gOutputFormats[] = {
	{
		B_TRANSLATOR_TEXT,
		B_TRANSLATOR_TEXT,
		TEXT_OUT_QUALITY,
		TEXT_OUT_CAPABILITY,
		"text/plain",
		"Plain text file"
	},
	{
		B_STYLED_TEXT_FORMAT,
		B_TRANSLATOR_TEXT,
		STXT_OUT_QUALITY,
		STXT_OUT_CAPABILITY,
		"text/x-vnd.Be-stxt",
		"Be styled text file"
	}
};

// ---------------------------------------------------------------
// make_nth_translator
//
// Creates a STXTTranslator object to be used by BTranslatorRoster
//
// Preconditions:
//
// Parameters: n,		The translator to return. Since
//						STXTTranslator only publishes one
//						translator, it only returns a
//						STXTTranslator if n == 0
//
//             you, 	The image_id of the add-on that
//						contains code (not used).
//
//             flags,	Has no meaning yet, should be 0.
//
// Postconditions:
//
// Returns: NULL if n is not zero,
//          a new STXTTranslator if n is zero
// ---------------------------------------------------------------
BTranslator *
make_nth_translator(int32 n, image_id you, uint32 flags, ...)
{
	if (!n)
		return new STXTTranslator();
	else
		return NULL;
}

// ---------------------------------------------------------------
// Constructor
//
// Sets up the version info and the name of the translator so that
// these values can be returned when they are requested.
//
// Preconditions:
//
// Parameters:
//
// Postconditions:
//
// Returns:
// ---------------------------------------------------------------
STXTTranslator::STXTTranslator()
	:	BTranslator()
{
	strcpy(fName, "StyledEdit Files");
	sprintf(fInfo, "StyledEdit file translator v%d.%d.%d %s",
		STXT_TRANSLATOR_VERSION / 100, (STXT_TRANSLATOR_VERSION / 10) % 10,
		STXT_TRANSLATOR_VERSION % 10, __DATE__);
}

// ---------------------------------------------------------------
// Destructor
//
// Does nothing
//
// Preconditions:
//
// Parameters:
//
// Postconditions:
//
// Returns:
// ---------------------------------------------------------------
STXTTranslator::~STXTTranslator()
{
}

// ---------------------------------------------------------------
// TranslatorName
//
// Returns the short name of the translator.
//
// Preconditions:
//
// Parameters:
//
// Postconditions:
//
// Returns: a const char * to the short name of the translator
// ---------------------------------------------------------------	
const char *
STXTTranslator::TranslatorName() const
{
	return fName;
}

// ---------------------------------------------------------------
// TranslatorInfo
//
// Returns a more verbose name for the translator than the one
// TranslatorName() returns. This usually includes version info.
//
// Preconditions:
//
// Parameters:
//
// Postconditions:
//
// Returns: a const char * to the verbose name of the translator
// ---------------------------------------------------------------
const char *
STXTTranslator::TranslatorInfo() const
{
	return fInfo;
}

// ---------------------------------------------------------------
// TranslatorVersion
//
// Returns the integer representation of the current version of
// this translator.
//
// Preconditions:
//
// Parameters:
//
// Postconditions:
//
// Returns:
// ---------------------------------------------------------------
int32 
STXTTranslator::TranslatorVersion() const
{
	return STXT_TRANSLATOR_VERSION;
}

// ---------------------------------------------------------------
// InputFormats
//
// Returns a list of input formats supported by this translator.
//
// Preconditions:
//
// Parameters:	out_count,	The number of input formats
//							support is returned here.
//
// Postconditions:
//
// Returns: the list of input formats and the number of input
// formats through the out_count parameter
// ---------------------------------------------------------------
const translation_format *
STXTTranslator::InputFormats(int32 *out_count) const
{
	if (out_count)
		*out_count = 2;

	return gInputFormats;
}

// ---------------------------------------------------------------
// OutputFormats
//
// Returns a list of output formats supported by this translator.
//
// Preconditions:
//
// Parameters:	out_count,	The number of output formats
//							support is returned here.
//
// Postconditions:
//
// Returns: the list of output formats and the number of output
// formats through the out_count parameter
// ---------------------------------------------------------------	
const translation_format *
STXTTranslator::OutputFormats(int32 *out_count) const
{
	if (out_count)
		*out_count = 2;

	return gOutputFormats;
}

status_t
identify_stxt_header(const TranslatorStyledTextStreamHeader &header,
	BPositionIO *inSource, translator_info *outInfo, uint32 outType,
	TranslatorStyledTextTextHeader *ptxtheader = NULL)
{
	char buffer[20];
	
	// Check the TEXT header
	TranslatorStyledTextTextHeader txtheader;
	if (inSource->Read(buffer, 16) != 16)
		return B_NO_TRANSLATOR;
		
	memcpy(&txtheader, buffer, 16);
	if (swap_data(B_UINT32_TYPE, &txtheader, 16,
		B_SWAP_BENDIAN_TO_HOST) != B_OK)
		return B_ERROR;
		
	if (txtheader.header.magic != 'TEXT' ||
		txtheader.header.header_size !=
			sizeof(TranslatorStyledTextTextHeader) ||
		txtheader.charset != B_UNICODE_UTF8)
		return B_NO_TRANSLATOR;

	// skip the text data
	off_t seekresult, pos;
	pos = header.header.header_size +
		txtheader.header.header_size +
		txtheader.header.data_size;
	seekresult = inSource->Seek(txtheader.header.data_size,
		SEEK_CUR);
	if (seekresult < pos)
		return B_NO_TRANSLATOR;
	if (seekresult > pos)
		return B_ERROR;

	// check the STYL header (not all STXT files have this)
	ssize_t read = 0;
	TranslatorStyledTextStyleHeader stlheader;
	read = inSource->Read(buffer, 20);
	if (read != 20 && read != 0)
		return B_NO_TRANSLATOR;
	
	// If there is a STYL header
	if (read == 20) {
		memcpy(&stlheader, buffer, 20);
		if (swap_data(B_UINT32_TYPE, &stlheader, 20,
			B_SWAP_BENDIAN_TO_HOST) != B_OK)
			return B_ERROR;
		
		if (stlheader.header.magic != 'STYL' ||
			stlheader.header.header_size !=
				sizeof(TranslatorStyledTextStyleHeader))
			return B_NO_TRANSLATOR;	
	}
	
	// if output TEXT header is supplied, fill it with data
	if (ptxtheader) {
		ptxtheader->header.magic = txtheader.header.magic;
		ptxtheader->header.header_size = txtheader.header.header_size;
		ptxtheader->header.data_size = txtheader.header.data_size;
		ptxtheader->charset = txtheader.charset;
	}
	
	// return information about the data in the stream
	outInfo->type = B_STYLED_TEXT_FORMAT;
	outInfo->group = B_TRANSLATOR_TEXT;
	outInfo->quality = STXT_IN_QUALITY;
	outInfo->capability = STXT_IN_CAPABILITY;
	strcpy(outInfo->name, "Be styled text file");
	strcpy(outInfo->MIME, "text/x-vnd.Be-stxt");
	
	return B_OK;
}

// data must be able to hold at least 64 bytes of data
status_t
identify_txt_header(uint8 *data, int32 nread,
	BPositionIO *inSource, translator_info *outInfo, uint32 outType)
{
	uint8 ch;
	ssize_t readlater = 0;
	readlater = inSource->Read(data + nread, 64 - nread);
	if (readlater < 0)
		return B_NO_TRANSLATOR;
	
	nread += readlater;
	for (int32 i = 0; i < nread; i++) {
		ch = data[i];
		// if any null characters or
		// control characters (other than a few)
		// are found, abort, as the data is
		// probably not plain text
		if (ch < 32 && 
			ch != 0x08 && // backspace
			ch != 0x09 && // tab
			ch != 0x0A && // line feed
			ch != 0x0C && // form feed
			ch != 0x0D)   // carriage return
			return B_NO_TRANSLATOR;
	}

	// return information about the data in the stream
	outInfo->type = B_TRANSLATOR_TEXT;
	outInfo->group = B_TRANSLATOR_TEXT;
	outInfo->quality = TEXT_IN_QUALITY;
	outInfo->capability = TEXT_IN_CAPABILITY;
	strcpy(outInfo->name, "Plain text file");
	strcpy(outInfo->MIME, "text/plain");
	
	return B_OK;
}

// ---------------------------------------------------------------
// Identify
//
// Examines the data from inSource and determines if it is in a
// format that this translator knows how to work with.
//
// Preconditions:
//
// Parameters:	inSource,	where the data to examine is
//
//				inFormat,	a hint about the data in inSource,
//							it is ignored since it is only a hint
//
//				ioExtension,	configuration settings for the
//								translator
//
//				outInfo,	information about what data is in
//							inSource and how well this translator
//							can handle that data is stored here
//
//				outType,	The format that the user wants
//							the data in inSource to be
//							converted to
//
// Postconditions:
//
// Returns: B_NO_TRANSLATOR,	if this translator can't handle
//								the data in inSource
//
// B_ERROR,	if there was an error converting the data to the host
//			format
//
// B_BAD_VALUE, if the settings in ioExtension are bad
//
// B_OK,	if this translator understand the data and there were
//			no errors found
// ---------------------------------------------------------------
status_t
STXTTranslator::Identify(BPositionIO *inSource,
	const translation_format *inFormat, BMessage *ioExtension,
	translator_info *outInfo, uint32 outType)
{
	if (!outType)
		outType = B_TRANSLATOR_TEXT;
	if (outType != B_TRANSLATOR_TEXT && outType != B_STYLED_TEXT_FORMAT)
		return B_NO_TRANSLATOR;
		
	uint8 buffer[64];
	status_t nread = 0;
	// Read in the header to determine
	// if the data is supported
	nread = inSource->Read(buffer, 16);
	if (nread < 0)
		return nread;

	// read in enough data to fill the stream header
	if (nread == 16) {
		TranslatorStyledTextStreamHeader header;
		memcpy(&header, buffer, 16);
		if (swap_data(B_UINT32_TYPE, &header, 16, B_SWAP_BENDIAN_TO_HOST)
			!= B_OK)
			return B_ERROR;
		
		if (header.header.magic == B_STYLED_TEXT_FORMAT && 
			header.header.header_size == 
			sizeof(TranslatorStyledTextStreamHeader) &&
			header.header.data_size == 0 &&
			header.version == 100)
			return identify_stxt_header(header, inSource, outInfo, outType);
	}
	
	// if the data is not styled text, check if it is ASCII text
	return identify_txt_header(buffer, nread, inSource, outInfo, outType);
}

status_t
translate_from_stxt(BPositionIO *inSource, BPositionIO *outDestination,
		uint32 outType, const TranslatorStyledTextTextHeader &txtheader)
{
	if (inSource->Seek(0, SEEK_SET) != 0)
		return B_ERROR;
	
	bool btoplain;
	if (outType == B_TRANSLATOR_TEXT)
		btoplain = true;
	else if (outType == B_STYLED_TEXT_FORMAT)
		btoplain = false;
	else
		return B_BAD_VALUE;
	
	uint8 buffer[READ_BUFFER_SIZE];
	ssize_t nread = 0, nwritten = 0, nreed = 0, ntotalread = 0;

	// skip to the actual text data when outputting a
	// plain text file
	if (btoplain) {
		if (inSource->Seek(32, SEEK_CUR) != 32)
			return B_ERROR;
	}
	
	// Read data from inSource 
	// When outputing B_TRANSLATOR_TEXT, the loop stops when all of
	// the text data has been read and written.
	// When outputting B_STYLED_TEXT_FORMAT, the loop stops when all
	// of the data from inSource has been read and written.
	if (btoplain)
		nreed = min(READ_BUFFER_SIZE,
			txtheader.header.data_size - ntotalread);
	else
		nreed = READ_BUFFER_SIZE;
	nread = inSource->Read(buffer, nreed);
	while (nread > 0) {
		nwritten = outDestination->Write(buffer, nread);
		if (nwritten != nread)
			return B_ERROR;

		if (btoplain) {
			ntotalread += nread;
			nreed = min(READ_BUFFER_SIZE,
				txtheader.header.data_size - ntotalread);
		} else
			nreed = READ_BUFFER_SIZE;
		nread = inSource->Read(buffer, nreed);
	}
	
	if (btoplain && static_cast<ssize_t>(txtheader.header.data_size) !=
		ntotalread)
		// If not all of the text data was able to be read...
		return B_NO_TRANSLATOR;
	else
		return B_OK;
}

// outputs the Stream and Text headers from the B_STYLED_TEXT_FORMAT
// to outDestination, setting the data_size member of the text header
// to text_data_size
status_t
output_headers(BPositionIO *outDestination, uint32 text_data_size)
{
	const int32 kHeadersSize = sizeof(TranslatorStyledTextStreamHeader) +
		sizeof(TranslatorStyledTextTextHeader);
	status_t result;
	TranslatorStyledTextStreamHeader stxtheader;
	TranslatorStyledTextTextHeader txtheader;
	
	uint8 buffer[kHeadersSize];
	
	stxtheader.header.magic = 'STXT';
	stxtheader.header.header_size = sizeof(TranslatorStyledTextStreamHeader);
	stxtheader.header.data_size = 0;
	stxtheader.version = 100;
	memcpy(buffer, &stxtheader, stxtheader.header.header_size);
	
	txtheader.header.magic = 'TEXT';
	txtheader.header.header_size = sizeof(TranslatorStyledTextTextHeader);
	txtheader.header.data_size = text_data_size;
	txtheader.charset = B_UNICODE_UTF8;
	memcpy(buffer + stxtheader.header.header_size, &txtheader,
		txtheader.header.header_size);
	
	// write out headers in Big Endian byte order
	result = swap_data(B_UINT32_TYPE, buffer, kHeadersSize,
		B_SWAP_HOST_TO_BENDIAN);
	if (result == B_OK) {
		ssize_t nwritten = 0;
		nwritten = outDestination->Write(buffer, kHeadersSize);
		if (nwritten != kHeadersSize)
			return B_ERROR;
		else
			return B_OK;
	}
	
	return result;
}

// convert the plain text from inSource to plain or styled text
// in outDestination
status_t
translate_from_text(BPositionIO *inSource, BPositionIO *outDestination,
	uint32 outType)
{
	// find the length of the text
	off_t size = 0;
	size = inSource->Seek(0, SEEK_END);
	if (size < 0)
		return B_ERROR;
		
	if (inSource->Seek(0, SEEK_SET) != 0)
		return B_ERROR;
		
	bool btoplain;
	if (outType == B_TRANSLATOR_TEXT)
		btoplain = true;
	else if (outType == B_STYLED_TEXT_FORMAT)
		btoplain = false;
	else
		return B_BAD_VALUE;
	
	// output styled text headers if outputting
	// in the B_STYLED_TEXT_FORMAT
	if (!btoplain) {
		status_t headresult;
		headresult = output_headers(outDestination,
			static_cast<uint32>(size));
		if (headresult != B_OK)
			return headresult;
	}
	
	uint8 buffer[READ_BUFFER_SIZE];
	ssize_t nread = 0, nwritten = 0;

	// output the actual text part of the data
	nread = inSource->Read(buffer, READ_BUFFER_SIZE);
	while (nread > 0) {
		nwritten = outDestination->Write(buffer, nread);
		if (nwritten != nread)
			return B_ERROR;
				
		nread = inSource->Read(buffer, READ_BUFFER_SIZE);
	}
		
	return B_OK;
}

// ---------------------------------------------------------------
// Translate
//
// Translates the data in inSource to the type outType and stores
// the translated data in outDestination.
//
// Preconditions:
//
// Parameters:	inSource,	the data to be translated
// 
//				inInfo,	hint about the data in inSource (not used)
//
//				ioExtension,	configuration options for the
//								translator
//
//				outType,	the type to convert inSource to
//
//				outDestination,	where the translated data is
//								put
//
// Postconditions:
//
// Returns: B_BAD_VALUE, if the options in ioExtension are bad
//
// B_NO_TRANSLATOR, if this translator doesn't understand the data
//
// B_ERROR, if there was an error allocating memory or converting
//          data
//
// B_OK, if all went well
// ---------------------------------------------------------------
status_t
STXTTranslator::Translate(BPositionIO *inSource,
		const translator_info *inInfo, BMessage *ioExtension,
		uint32 outType, BPositionIO *outDestination)
{
	if (!outType)
		outType = B_TRANSLATOR_TEXT;
	if (outType != B_TRANSLATOR_TEXT && outType != B_STYLED_TEXT_FORMAT)
		return B_NO_TRANSLATOR;

	uint8 buffer[64];
	status_t nread = 0, result;
	translator_info outInfo;
	// Read in the header to determine
	// if the data is supported
	nread = inSource->Read(buffer, 16);
	if (nread < 0)
		return nread;

	// read in enough data to fill the stream header
	if (nread == 16) {
		TranslatorStyledTextStreamHeader header;
		memcpy(&header, buffer, 16);
		if (swap_data(B_UINT32_TYPE, &header, 16, B_SWAP_BENDIAN_TO_HOST)
			!= B_OK)
			return B_ERROR;
		
		if (header.header.magic == B_STYLED_TEXT_FORMAT && 
			header.header.header_size == 
			sizeof(TranslatorStyledTextStreamHeader) &&
			header.header.data_size == 0 &&
			header.version == 100) {
			TranslatorStyledTextTextHeader txtheader;
			result = identify_stxt_header(header, inSource, &outInfo, outType,
				&txtheader);
			return translate_from_stxt(inSource, outDestination, outType,
				txtheader);
		}
	}
	
	// if the data is not styled text, check if it is ASCII text
	result = identify_txt_header(buffer, nread, inSource, &outInfo, outType);
	if (result == B_OK)
		return translate_from_text(inSource, outDestination, outType);
	else
		return result;
}

// ---------------------------------------------------------------
// MakeConfigurationView
//
// Makes a BView object for configuring / displaying info about
// this translator. 
//
// Preconditions:
//
// Parameters:	ioExtension,	configuration options for the
//								translator
//
//				outView,		the view to configure the
//								translator is stored here
//
//				outExtent,		the bounds of the view are
//								stored here
//
// Postconditions:
//
// Returns:
// ---------------------------------------------------------------
status_t
STXTTranslator::MakeConfigurationView(BMessage *ioExtension, BView **outView,
	BRect *outExtent)
{
	if (!outView || !outExtent)
		return B_BAD_VALUE;

	STXTView *view = new STXTView(BRect(0, 0, 225, 175),
		"STXTTranslator Settings", B_FOLLOW_ALL, B_WILL_DRAW);
	*outView = view;
	*outExtent = view->Bounds();

	return B_OK;
}
