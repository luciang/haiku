/*****************************************************************************/
// BMPTranslator
// BMPTranslator.cpp
//
// This BTranslator based object is for opening and writing BMP files.
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
#include "BMPTranslator.h"
#include "BMPView.h"

#define min(x,y) ((x < y) ? x : y)
#define max(x,y) ((x > y) ? x : y)

// The input formats that this translator supports.
translation_format gInputFormats[] = {
	{
		B_TRANSLATOR_BITMAP,
		B_TRANSLATOR_BITMAP,
		BBT_IN_QUALITY,
		BBT_IN_CAPABILITY,
		"image/x-be-bitmap",
		"Be Bitmap Format (BMPTranslator)"
	},
	{
		B_BMP_FORMAT,
		B_TRANSLATOR_BITMAP,
		BMP_IN_QUALITY,
		BMP_IN_CAPABILITY,
		"image/x-bmp",
		"BMP image"
	}
};

// The output formats that this translator supports.
translation_format gOutputFormats[] = {
	{
		B_TRANSLATOR_BITMAP,
		B_TRANSLATOR_BITMAP,
		BBT_OUT_QUALITY,
		BBT_OUT_CAPABILITY,
		"image/x-be-bitmap",
		"Be Bitmap Format (BMPTranslator)"
	},
	{
		B_BMP_FORMAT,
		B_TRANSLATOR_BITMAP,
		BMP_OUT_QUALITY,
		BMP_OUT_CAPABILITY,
		"image/x-bmp",
		"BMP image (MS format)"
	}
};

// ---------------------------------------------------------------
// make_nth_translator
//
// Creates a BMPTranslator object to be used by BTranslatorRoster
//
// Preconditions:
//
// Parameters: n,		The translator to return. Since
//						BMPTranslator only publishes one
//						translator, it only returns a
//						BMPTranslator if n == 0
//
//             you, 	The image_id of the add-on that
//						contains code (not used).
//
//             flags,	Has no meaning yet, should be 0.
//
// Postconditions:
//
// Returns: NULL if n is not zero,
//          a new BMPTranslator if n is zero
// ---------------------------------------------------------------
BTranslator *
make_nth_translator(int32 n, image_id you, uint32 flags, ...)
{
	if (!n)
		return new BMPTranslator();
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
BMPTranslator::BMPTranslator()
	:	BTranslator()
{
	strcpy(fName, "BMP Images");
	sprintf(fInfo, "BMP image translator v%d.%d.%d %s",
		static_cast<int>(BMP_TRANSLATOR_VERSION >> 8),
		static_cast<int>((BMP_TRANSLATOR_VERSION >> 4) & 0xf),
		static_cast<int>(BMP_TRANSLATOR_VERSION & 0xf), __DATE__);
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
BMPTranslator::~BMPTranslator()
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
BMPTranslator::TranslatorName() const
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
BMPTranslator::TranslatorInfo() const
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
BMPTranslator::TranslatorVersion() const
{
	return BMP_TRANSLATOR_VERSION;
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
BMPTranslator::InputFormats(int32 *out_count) const
{
	if (out_count) {
		*out_count = sizeof(gInputFormats) /
			sizeof(translation_format);
		return gInputFormats;
	} else
		return NULL;
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
BMPTranslator::OutputFormats(int32 *out_count) const
{
	if (out_count) {
		*out_count = sizeof(gOutputFormats) /
			sizeof(translation_format);
		return gOutputFormats;
	} else
		return NULL;
}

// ---------------------------------------------------------------
// get_padding
//
// Returns number of bytes of padding required at the end of
// the row by the BMP format
// 
//
// Preconditions: If bitsperpixel is zero, a division by zero
//                will occur, which is bad
//
// Parameters:	width, width of the row, in pixels
//
//				bitsperpixel, bitdepth of the image
//
// Postconditions:
//
// Returns: 
// ---------------------------------------------------------------	
int32
get_padding(uint32 width, uint16 bitsperpixel)
{
	int32 padding = 0;
	
	if (bitsperpixel > 8) {
		uint8 bytesPerPixel = bitsperpixel / 8;
		padding = (width * bytesPerPixel) % 4;
	} else {
		uint8 pixelsPerByte = 8 / bitsperpixel;
		if (!(width % pixelsPerByte))
			padding = (width / pixelsPerByte) % 4;
		else
			padding = ((width + pixelsPerByte -
				(width % pixelsPerByte)) /
					pixelsPerByte) % 4;
	}
	
	if (padding)
		padding = 4 - padding;
	
	return padding;
}

// ---------------------------------------------------------------
// get_rowbytes
//
// Returns number of bytes required to store a row of BMP pixels
// with a width of width and a bit depth of bitsperpixel.
// 
//
// Preconditions: If bitsperpixel is zero, a division by zero
//                will occur, which is bad
//
// Parameters:	width, width of the row, in pixels
//
//				bitsperpixel, bitdepth of the image
//
// Postconditions:
//
// Returns: 
// ---------------------------------------------------------------
int32
get_rowbytes(uint32 width, uint16 bitsperpixel)
{
	int32 rowbytes = 0;
	int32 padding = get_padding(width, bitsperpixel);
	
	if (bitsperpixel > 8) {
		uint8 bytesPerPixel = bitsperpixel / 8;
		rowbytes = (width * bytesPerPixel) + padding;
	} else {
		uint8 pixelsPerByte = 8 / bitsperpixel;
		rowbytes = (width / pixelsPerByte) +
			((width % pixelsPerByte) ? 1 : 0) + padding;
	}
	
	return rowbytes;
}

// ---------------------------------------------------------------
// identify_bits_header
//
// Determines if the data in inSource is in the
// B_TRANSLATOR_BITMAP ('bits') format. If it is, it returns 
// info about the data in inSource to outInfo and pheader.
//
// Preconditions:
//
// Parameters:	inSource,	The source of the image data
//
//				outInfo,	Information about the translator
//							is copied here
//
//				amtread,	Amount of data read from inSource
//							before this function was called
//
//				read,		Pointer to the data that was read
// 							in before this function was called
//
//				pheader,	The bits header is copied here after
//							it is read in from inSource
//
// Postconditions:
//
// Returns: B_NO_TRANSLATOR,	if the data does not look like
//								bits format data
//
// B_ERROR,	if the header data could not be converted to host
//			format
//
// B_OK,	if the data looks like bits data and no errors were
//			encountered
// ---------------------------------------------------------------
status_t 
identify_bits_header(BPositionIO *inSource, translator_info *outInfo,
	ssize_t amtread, uint8 *read, TranslatorBitmap *pheader = NULL)
{
	TranslatorBitmap header;
		
	memcpy(&header, read, amtread);
		// copy portion of header already read in
	// read in the rest of the header
	ssize_t size = sizeof(TranslatorBitmap) - amtread;
	if (inSource->Read((reinterpret_cast<uint8 *> (&header)) +
		amtread, size) != size)
		return B_NO_TRANSLATOR;
		
	// convert to host byte order
	if (swap_data(B_UINT32_TYPE, &header, sizeof(TranslatorBitmap),
		B_SWAP_BENDIAN_TO_HOST) != B_OK)
		return B_ERROR;
		
	// check if header values are reasonable
	if (header.colors != B_RGB32 &&
		header.colors != B_RGB32_BIG &&
		header.colors != B_RGBA32 &&
		header.colors != B_RGBA32_BIG &&
		header.colors != B_RGB24 &&
		header.colors != B_RGB24_BIG &&
		header.colors != B_RGB16 &&
		header.colors != B_RGB16_BIG &&
		header.colors != B_RGB15 &&
		header.colors != B_RGB15_BIG &&
		header.colors != B_RGBA15 &&
		header.colors != B_RGBA15_BIG &&
		header.colors != B_CMAP8 &&
		header.colors != B_GRAY8 &&
		header.colors != B_GRAY1 &&
		header.colors != B_CMYK32 &&
		header.colors != B_CMY32 &&
		header.colors != B_CMYA32 &&
		header.colors != B_CMY24)
		return B_NO_TRANSLATOR;
	if (header.rowBytes * (header.bounds.Height() + 1) != header.dataSize)
		return B_NO_TRANSLATOR;
			
	if (outInfo) {
		outInfo->type = B_TRANSLATOR_BITMAP;
		outInfo->group = B_TRANSLATOR_BITMAP;
		outInfo->quality = BBT_IN_QUALITY;
		outInfo->capability = BBT_IN_CAPABILITY;
		strcpy(outInfo->name, "Be Bitmap Format (BMPTranslator)");
		strcpy(outInfo->MIME, "image/x-be-bitmap");
	}
	
	if (pheader) {
		pheader->magic = header.magic;
		pheader->bounds = header.bounds;
		pheader->rowBytes = header.rowBytes;
		pheader->colors = header.colors;
		pheader->dataSize = header.dataSize;
	}
	
	return B_OK;
}

// ---------------------------------------------------------------
// identify_bmp_header
//
// Determines if the data in inSource is in the MS or OS/2 BMP
// format. If it is, it returns info about the data in inSource
// to outInfo, pfileheader, pmsheader, pfrommsformat and os2skip.
//
// Preconditions:
//
// Parameters:	inSource,	The source of the image data
//
//				outInfo,	Information about the translator
//							is copied here
//
//				amtread,	Amount of data read from inSource
//							before this function was called
//
//				read,		Pointer to the data that was read
// 							in before this function was called
//
//				pfileheader,	File header info for the BMP is
//								copied here after it is read from
//								the file.
//
//				pmsheader,		BMP header info read in from the
//								BMP file
//
//				pfrommsformat,	Set to true if BMP data is BMP
//								format, false if BMP data is OS/2
//								format.
//
//				pos2skip,	If data is in OS/2 format, the number
//							of bytes to skip between the header
//							data and image data is stored here
//
// Postconditions:
//
// Returns: B_NO_TRANSLATOR,	if the data does not look like
//								BMP format data
//
// B_ERROR,	if the header data could not be converted to host
//			format
//
// B_OK,	if the data looks like bits data and no errors were
//			encountered
// ---------------------------------------------------------------
status_t
identify_bmp_header(BPositionIO *inSource, translator_info *outInfo,
	ssize_t amtread, uint8 *read, BMPFileHeader *pfileheader = NULL,
	MSInfoHeader *pmsheader = NULL, bool *pfrommsformat = NULL,
	off_t *pos2skip = NULL)
{
	uint8 buf[40];
	BMPFileHeader fileHeader;
	
	memcpy(buf, read, amtread);
		// copy portion of fileHeader already read in
	// read in the rest of the fileHeader
	ssize_t size = 14 - amtread;
	if (inSource->Read(buf + amtread, size) != size)
		return B_NO_TRANSLATOR;
	
	// convert fileHeader to host byte order
	memcpy(&fileHeader.magic, buf, 2);
	memcpy(&fileHeader.fileSize, buf + 2, 4);
	memcpy(&fileHeader.reserved, buf + 6, 4);
	memcpy(&fileHeader.dataOffset, buf + 10, 4);
	if (swap_data(B_UINT16_TYPE, &fileHeader.magic, sizeof(uint16),
		B_SWAP_LENDIAN_TO_HOST) != B_OK)
		return B_ERROR;
	if (swap_data(B_UINT32_TYPE,
		(reinterpret_cast<uint8 *> (&fileHeader)) + 2, 12,
		B_SWAP_LENDIAN_TO_HOST) != B_OK)
		return B_ERROR;

	if (fileHeader.reserved != 0)
		return B_NO_TRANSLATOR;
		
	uint32 headersize = 0;
	if (inSource->Read(&headersize, 4) != 4)
		return B_NO_TRANSLATOR;
	if (swap_data(B_UINT32_TYPE, &headersize, 4,
		B_SWAP_LENDIAN_TO_HOST) != B_OK)
		return B_ERROR;
	
	if (headersize == sizeof(MSInfoHeader)) {
		// MS format
		
		if (fileHeader.dataOffset < 54)
			return B_NO_TRANSLATOR;
		
		MSInfoHeader msheader;
		msheader.size = headersize;
		if (inSource->Read(
			reinterpret_cast<uint8 *> (&msheader) + 4, 36) != 36)
			return B_NO_TRANSLATOR;
	
		// convert msheader to host byte order
		if (swap_data(B_UINT32_TYPE,
			reinterpret_cast<uint8 *> (&msheader) + 4, 36,
			B_SWAP_LENDIAN_TO_HOST) != B_OK)
			return B_ERROR;
	
		// check if msheader is valid
		if (msheader.width == 0 || msheader.height == 0)
			return B_NO_TRANSLATOR;
		if (msheader.planes != 1)
			return B_NO_TRANSLATOR;
		if ((msheader.bitsperpixel != 1 ||
				msheader.compression != BMP_NO_COMPRESS) &&
			(msheader.bitsperpixel != 4 ||
				msheader.compression != BMP_NO_COMPRESS) &&
			(msheader.bitsperpixel != 4 ||
				msheader.compression != BMP_RLE4_COMPRESS) &&
			(msheader.bitsperpixel != 8 ||
				msheader.compression != BMP_NO_COMPRESS) &&
			(msheader.bitsperpixel != 8 ||
				msheader.compression != BMP_RLE8_COMPRESS) &&
			(msheader.bitsperpixel != 24 ||
				msheader.compression != BMP_NO_COMPRESS) &&
			(msheader.bitsperpixel != 32 ||
				msheader.compression != BMP_NO_COMPRESS))
			return B_NO_TRANSLATOR;
		if (!msheader.imagesize && msheader.compression)
			return B_NO_TRANSLATOR;
		if (msheader.colorsimportant > msheader.colorsused)
			return B_NO_TRANSLATOR; 
			
		if (outInfo) {
			outInfo->type = B_BMP_FORMAT;
			outInfo->group = B_TRANSLATOR_BITMAP;
			outInfo->quality = BMP_IN_QUALITY;
			outInfo->capability = BMP_IN_CAPABILITY;
			sprintf(outInfo->name, "BMP image (MS format, %d bits",
				msheader.bitsperpixel);
			if (msheader.compression)
				strcat(outInfo->name, ", RLE)");
			else
				strcat(outInfo->name, ")");
			strcpy(outInfo->MIME, "image/x-bmp");
		}
		
		if (pfileheader) {
			pfileheader->magic = fileHeader.magic;
			pfileheader->fileSize = fileHeader.fileSize;
			pfileheader->reserved = fileHeader.reserved;
			pfileheader->dataOffset = fileHeader.dataOffset;
		}
		if (pmsheader) {
			pmsheader->size = msheader.size;
			pmsheader->width = msheader.width;
			pmsheader->height = msheader.height;
			pmsheader->planes = msheader.planes;
			pmsheader->bitsperpixel = msheader.bitsperpixel;
			pmsheader->compression = msheader.compression;
			pmsheader->imagesize = msheader.imagesize;
			pmsheader->xpixperm = msheader.xpixperm;
			pmsheader->ypixperm = msheader.ypixperm;
			pmsheader->colorsused = msheader.colorsused;
			pmsheader->colorsimportant = msheader.colorsimportant;
		}
		if (pfrommsformat)
			(*pfrommsformat) = true;
		
		return B_OK;

	} else if (headersize == sizeof(OS2InfoHeader)) {
		// OS/2 format

		if (fileHeader.dataOffset < 26)
			return B_NO_TRANSLATOR;
		
		OS2InfoHeader os2header;
		os2header.size = headersize;
		if (inSource->Read(
			reinterpret_cast<uint8 *> (&os2header) + 4, 8) != 8)
			return B_NO_TRANSLATOR;
	
		// convert msheader to host byte order
		if (swap_data(B_UINT32_TYPE,
			reinterpret_cast<uint8 *> (&os2header) + 4, 8,
			B_SWAP_LENDIAN_TO_HOST) != B_OK)
			return B_ERROR;
	
		// check if msheader is valid
		if (os2header.width == 0 || os2header.height == 0)
			return B_NO_TRANSLATOR;
		if (os2header.planes != 1)
			return B_NO_TRANSLATOR;
		if (os2header.bitsperpixel != 1 &&
			os2header.bitsperpixel != 4 &&
			os2header.bitsperpixel != 8 &&
			os2header.bitsperpixel != 24)
			return B_NO_TRANSLATOR;
		
		if (outInfo) {
			outInfo->type = B_BMP_FORMAT;
			outInfo->group = B_TRANSLATOR_BITMAP;
			outInfo->quality = BMP_IN_QUALITY;
			outInfo->capability = BMP_IN_CAPABILITY;
			sprintf(outInfo->name, "BMP image (OS/2 format, %d bits)",
				os2header.bitsperpixel);
			strcpy(outInfo->MIME, "image/x-bmp");
		}
		if (pfileheader && pmsheader) {
			pfileheader->magic = 'MB';
			pfileheader->fileSize = 0;
			pfileheader->reserved = 0;
			pfileheader->dataOffset = 0;

			pmsheader->size = 40;
			pmsheader->width = os2header.width;
			pmsheader->height = os2header.height;
			pmsheader->planes = 1;
			pmsheader->bitsperpixel = os2header.bitsperpixel;
			pmsheader->compression = BMP_NO_COMPRESS;
			pmsheader->imagesize = 0;
			pmsheader->xpixperm = 2835; // 72 dpi horizontal
			pmsheader->ypixperm = 2835; // 72 dpi vertical
			pmsheader->colorsused = 0;
			pmsheader->colorsimportant = 0;
			
			// determine fileSize / imagesize
			switch (pmsheader->bitsperpixel) {
				case 24:
					if (pos2skip && fileHeader.dataOffset > 26)
						(*pos2skip) = fileHeader.dataOffset - 26;
						
					pfileheader->dataOffset = 54;
					pmsheader->imagesize = get_rowbytes(pmsheader->width,
						pmsheader->bitsperpixel) * pmsheader->height;
					pfileheader->fileSize = pfileheader->dataOffset +
						pmsheader->imagesize;
			
					break;
				
				case 8:
				case 4:
				case 1:
				{
					uint16 ncolors = 1 << pmsheader->bitsperpixel;
					pmsheader->colorsused = ncolors;
					pmsheader->colorsimportant = ncolors;
					if (pos2skip && fileHeader.dataOffset >
						static_cast<uint32> (26 + (ncolors * 3)))
							(*pos2skip) = fileHeader.dataOffset -
								(26 + (ncolors * 3));

					pfileheader->dataOffset = 54 + (ncolors * 4);
					pmsheader->imagesize = get_rowbytes(pmsheader->width,
						pmsheader->bitsperpixel) * pmsheader->height;
					pfileheader->fileSize = pfileheader->dataOffset +
						pmsheader->imagesize;
					
					break;
				}
				
				default:
					break;
			}
		}
		if (pfrommsformat)
			(*pfrommsformat) = false;
		
		return B_OK;

	} else
		return B_NO_TRANSLATOR;
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
BMPTranslator::Identify(BPositionIO *inSource,
	const translation_format *inFormat, BMessage *ioExtension,
	translator_info *outInfo, uint32 outType)
{
	if (!outType)
		outType = B_TRANSLATOR_BITMAP;
	if (outType != B_TRANSLATOR_BITMAP && outType != B_BMP_FORMAT)
		return B_NO_TRANSLATOR;

	uint8 ch[4];
	uint32 nbits = B_TRANSLATOR_BITMAP;
	uint16 nbm = 'MB';
	
	// Convert the magic numbers to the various byte orders so that
	// I won't have to convert the data read in to see whether or not
	// it is a supported type
	if (swap_data(B_UINT32_TYPE, &nbits, 4, B_SWAP_HOST_TO_BENDIAN) != B_OK)
		return B_ERROR;
	if (swap_data(B_UINT16_TYPE, &nbm, 2, B_SWAP_HOST_TO_LENDIAN) != B_OK)
		return B_ERROR;
	
	// Read in the magic number and determine if it
	// is a supported type
	if (inSource->Read(ch, 4) != 4)
		return B_NO_TRANSLATOR;
		
	// Read settings from ioExtension
	bool bheaderonly = false, bdataonly = false;
	if (ioExtension) {
		if (ioExtension->FindBool(B_TRANSLATOR_EXT_HEADER_ONLY, &bheaderonly))
			// if failed, make sure bool is default value
			bheaderonly = false;
		if (ioExtension->FindBool(B_TRANSLATOR_EXT_DATA_ONLY, &bdataonly))
			// if failed, make sure bool is default value
			bdataonly = false;
			
		if (bheaderonly && bdataonly)
			// can't both "only write the header" and "only write the data"
			// at the same time
			return B_BAD_VALUE;
	}
	bheaderonly = bdataonly = false;
		// only allow writing of the entire image
		// (fix for buggy programs that lie about what they actually need)
	
	uint32 n32ch;
	memcpy(&n32ch, ch, sizeof(uint32));
	uint16 n16ch;
	memcpy(&n16ch, ch, sizeof(uint16));
	// if B_TRANSLATOR_BITMAP type	
	if (n32ch == nbits)
		return identify_bits_header(inSource, outInfo, 4, ch);
	// if BMP type in Little Endian byte order
	else if (n16ch == nbm)
		return identify_bmp_header(inSource, outInfo, 4, ch);
	else
		return B_NO_TRANSLATOR;
}

// ---------------------------------------------------------------
// translate_from_bits_to_bmp24
//
// Converts various varieties of the Be Bitmap format ('bits') to
// the MS BMP 24-bit format.
//
// Preconditions:
//
// Parameters:	inSource,	contains the bits data to convert
//
//				outDestination,	where the BMP data will be written
//
//				fromspace,	the format of the data in inSource
//
//				msheader,	contains information about the BMP
//							dimensions and filesize
//
// Postconditions:
//
// Returns: B_ERROR,	if memory couldn't be allocated or another
//						error occured
//
// B_OK,	if no errors occurred
// ---------------------------------------------------------------
status_t
translate_from_bits_to_bmp24(BPositionIO *inSource,
BPositionIO *outDestination, color_space fromspace, MSInfoHeader &msheader)
{
	int32 bitsBytesPerPixel = 0;
	switch (fromspace) {
		case B_RGB32:
		case B_RGB32_BIG:
		case B_RGBA32:
		case B_RGBA32_BIG:
		case B_CMY32:
		case B_CMYA32:
		case B_CMYK32:
			bitsBytesPerPixel = 4;
			break;
			
		case B_RGB24:
		case B_RGB24_BIG:
		case B_CMY24:
			bitsBytesPerPixel = 3;
			break;
			
		case B_RGB16:
		case B_RGB16_BIG:
		case B_RGBA15:
		case B_RGBA15_BIG:
		case B_RGB15:
		case B_RGB15_BIG:
			bitsBytesPerPixel = 2;
			break;
			
		case B_CMAP8:
		case B_GRAY8:
			bitsBytesPerPixel = 1;
			break;
			
		default:
			return B_ERROR;
	}
	int32 bitsRowBytes = msheader.width * bitsBytesPerPixel;
	int32 padding = get_padding(msheader.width, msheader.bitsperpixel);
	int32 bmpRowBytes =
		get_rowbytes(msheader.width, msheader.bitsperpixel);
	uint32 bmppixrow = 0;
	off_t bitsoffset = ((msheader.height - 1) * bitsRowBytes);
	inSource->Seek(bitsoffset, SEEK_CUR);
	uint8 *bmpRowData = new uint8[bmpRowBytes];
	if (!bmpRowData)
		return B_ERROR;
	uint8 *bitsRowData = new uint8[bitsRowBytes];
	if (!bitsRowData) {
		delete[] bmpRowData;
		bmpRowData = NULL;
		return B_ERROR;
	}
	memset(bmpRowData + (bmpRowBytes - padding), 0, padding);
	ssize_t rd = inSource->Read(bitsRowData, bitsRowBytes);
	const color_map *pmap = NULL;
	if (fromspace == B_CMAP8) {
		pmap = system_colors();
		if (!pmap)
			return B_ERROR;
	}
	while (rd == static_cast<ssize_t>(bitsRowBytes)) {
	
		for (uint32 i = 0; i < msheader.width; i++) {
			uint8 *bitspixel, *bmppixel;
			uint16 val;
			switch (fromspace) {
				case B_RGB32:
				case B_RGBA32:
				case B_RGB24:
					memcpy(bmpRowData + (i * 3),
						bitsRowData + (i * bitsBytesPerPixel), 3);
					break;
					
				case B_RGB16:
				case B_RGB16_BIG:
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					if (fromspace == B_RGB16)
						val = bitspixel[0] + (bitspixel[1] << 8);
					else
						val = bitspixel[1] + (bitspixel[0] << 8);
					bmppixel[0] =
						((val & 0x1f) << 3) | ((val & 0x1f) >> 2);
					bmppixel[1] =
						((val & 0x7e0) >> 3) | ((val & 0x7e0) >> 9);
					bmppixel[2] =
						((val & 0xf800) >> 8) | ((val & 0xf800) >> 13);
					break;

				case B_RGB15:
				case B_RGB15_BIG:
				case B_RGBA15:
				case B_RGBA15_BIG:
					// NOTE: the alpha data for B_RGBA15* is not used
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					if (fromspace == B_RGB15 || fromspace == B_RGBA15)
						val = bitspixel[0] + (bitspixel[1] << 8);
					else
						val = bitspixel[1] + (bitspixel[0] << 8);
					bmppixel[0] = 
						((val & 0x1f) << 3) | ((val & 0x1f) >> 2);
					bmppixel[1] =
						((val & 0x3e0) >> 2) | ((val & 0x3e0) >> 7);
					bmppixel[2] =
						((val & 0x7c00) >> 7) | ((val & 0x7c00) >> 12);
					break;
						
				case B_RGB32_BIG:
				case B_RGBA32_BIG:
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					bmppixel[0] = bitspixel[3];
					bmppixel[1] = bitspixel[2];
					bmppixel[2] = bitspixel[1];
					break;
						
				case B_RGB24_BIG:
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					bmppixel[0] = bitspixel[2];
					bmppixel[1] = bitspixel[1];
					bmppixel[2] = bitspixel[0];
					break;
				
				case B_CMAP8:
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					rgb_color c = pmap->color_list[bitspixel[0]];
					bmppixel[0] = c.blue;
					bmppixel[1] = c.green;
					bmppixel[2] = c.red;
					break;
					
				case B_GRAY8:
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					bmppixel[0] = bitspixel[0];
					bmppixel[1] = bitspixel[0];
					bmppixel[2] = bitspixel[0];
					break;
					
				case B_CMYK32:
				{
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					
					int32 comp = 255 - bitspixel[2] - bitspixel[3];
					bmppixel[0] = (comp < 0) ? 0 : comp;
					
					comp = 255 - bitspixel[1] - bitspixel[3];
					bmppixel[1] = (comp < 0) ? 0 : comp;
					
					comp = 255 - bitspixel[0] - bitspixel[3];
					bmppixel[2] = (comp < 0) ? 0 : comp;
					break;
				}
				
				case B_CMY32:
				case B_CMYA32:
				case B_CMY24:
					bitspixel = bitsRowData + (i * bitsBytesPerPixel);
					bmppixel = bmpRowData + (i * 3);
					bmppixel[0] = 255 - bitspixel[2];
					bmppixel[1] = 255 - bitspixel[1];
					bmppixel[2] = 255 - bitspixel[0];
					break;
						
				default:
					break;
			} // switch (fromspace)
		} // for for (uint32 i = 0; i < msheader.width; i++)
				
		outDestination->Write(bmpRowData, bmpRowBytes);
		bmppixrow++;
		// if I've read all of the pixel data, break
		// out of the loop so I don't try to read 
		// non-pixel data
		if (bmppixrow == msheader.height)
			break;

		inSource->Seek(bitsRowBytes * -2, SEEK_CUR);
		rd = inSource->Read(bitsRowData, bitsRowBytes);
	} // while (rd == bitsRowBytes)
	
	delete[] bmpRowData;
	bmpRowData = NULL;
	delete[] bitsRowData;
	bitsRowData = NULL;

	return B_OK;
}

// ---------------------------------------------------------------
// translate_from_bits8_to_bmp8
//
// Converts 8-bit Be Bitmaps ('bits') to the MS 8-bit BMP format
//
// Preconditions:
//
// Parameters:	inSource,	contains the bits data to convert
//
//				outDestination,	where the BMP data will be written
//
//				bitsRowBytes,	number of bytes in one row of
//								bits data
//
//				msheader,	contains information about the BMP
//							dimensions and filesize
//
// Postconditions:
//
// Returns: B_ERROR,	if memory couldn't be allocated or another
//						error occured
//
// B_OK,	if no errors occurred
// ---------------------------------------------------------------
status_t
translate_from_bits8_to_bmp8(BPositionIO *inSource,
	BPositionIO *outDestination, int32 bitsRowBytes, MSInfoHeader &msheader)
{
	int32 padding = get_padding(msheader.width, msheader.bitsperpixel);
	int32 bmpRowBytes =
		get_rowbytes(msheader.width, msheader.bitsperpixel);
	uint32 bmppixrow = 0;
	off_t bitsoffset = ((msheader.height - 1) * bitsRowBytes);
	inSource->Seek(bitsoffset, SEEK_CUR);
	uint8 *bmpRowData = new uint8[bmpRowBytes];
	if (!bmpRowData)
		return B_ERROR;
	uint8 *bitsRowData = new uint8[bitsRowBytes];
	if (!bitsRowData) {
		delete[] bmpRowData;
		bmpRowData = NULL;
		return B_ERROR;
	}
	memset(bmpRowData + (bmpRowBytes - padding), 0, padding);
	ssize_t rd = inSource->Read(bitsRowData, bitsRowBytes);
	while (rd == bitsRowBytes) {
		memcpy(bmpRowData, bitsRowData, msheader.width);
		outDestination->Write(bmpRowData, bmpRowBytes);
		bmppixrow++;
		// if I've read all of the pixel data, break
		// out of the loop so I don't try to read 
		// non-pixel data
		if (bmppixrow == msheader.height)
			break;

		inSource->Seek(bitsRowBytes * -2, SEEK_CUR);
		rd = inSource->Read(bitsRowData, bitsRowBytes);
	} // while (rd == bitsRowBytes)
	
	delete[] bmpRowData;
	bmpRowData = NULL;
	delete[] bitsRowData;
	bitsRowData = NULL;

	return B_OK;
}

// ---------------------------------------------------------------
// translate_from_bits1_to_bmp1
//
// Converts 1-bit Be Bitmaps ('bits') to the MS 1-bit BMP format
//
// Preconditions:
//
// Parameters:	inSource,	contains the bits data to convert
//
//				outDestination,	where the BMP data will be written
//
//				bitsRowBytes,	number of bytes in one row of
//								bits data
//
//				msheader,	contains information about the BMP
//							dimensions and filesize
//
// Postconditions:
//
// Returns: B_ERROR,	if memory couldn't be allocated or another
//						error occured
//
// B_OK,	if no errors occurred
// ---------------------------------------------------------------
status_t
translate_from_bits1_to_bmp1(BPositionIO *inSource,
	BPositionIO *outDestination, int32 bitsRowBytes, MSInfoHeader &msheader)
{
	uint8 pixelsPerByte = 8 / msheader.bitsperpixel;
	int32 bmpRowBytes =
		get_rowbytes(msheader.width, msheader.bitsperpixel);
	uint32 bmppixrow = 0;
	off_t bitsoffset = ((msheader.height - 1) * bitsRowBytes);
	inSource->Seek(bitsoffset, SEEK_CUR);
	uint8 *bmpRowData = new uint8[bmpRowBytes];
	if (!bmpRowData)
		return B_ERROR;
	uint8 *bitsRowData = new uint8[bitsRowBytes];
	if (!bitsRowData) {
		delete[] bmpRowData;
		bmpRowData = NULL;
		return B_ERROR;
	}
	ssize_t rd = inSource->Read(bitsRowData, bitsRowBytes);
	while (rd == bitsRowBytes) {
		uint32 bmppixcol = 0;
		memset(bmpRowData, 0, bmpRowBytes);
		for (int32 i = 0; (bmppixcol < msheader.width) && 
			(i < bitsRowBytes); i++) {
			// process each byte in the row
			uint8 pixels = bitsRowData[i];
			for (uint8 compbit = 128; (bmppixcol < msheader.width) &&
				compbit; compbit >>= 1) {
				// for each bit in the current byte, convert to a BMP palette
				// index and store that in the bmpRowData
				uint8 index;
				if (pixels & compbit)
					// 1 == black
					index = 1;
				else
					// 0 == white
					index = 0;
				bmpRowData[bmppixcol / pixelsPerByte] |= 
					index << (7 - (bmppixcol % pixelsPerByte));
				bmppixcol++;
			}
		}
				
		outDestination->Write(bmpRowData, bmpRowBytes);
		bmppixrow++;
		// if I've read all of the pixel data, break
		// out of the loop so I don't try to read 
		// non-pixel data
		if (bmppixrow == msheader.height)
			break;

		inSource->Seek(bitsRowBytes * -2, SEEK_CUR);
		rd = inSource->Read(bitsRowData, bitsRowBytes);
	} // while (rd == bitsRowBytes)
	
	delete[] bmpRowData;
	bmpRowData = NULL;
	delete[] bitsRowData;
	bitsRowData = NULL;

	return B_OK;
}

// ---------------------------------------------------------------
// write_bmp_headers
//
// Writes the MS BMP headers (fileHeader and msheader)
// to outDestination.
//
// Preconditions:
//
// Parameters:	outDestination,	where the headers are written to
//
// 				fileHeader, BMP file header data
//
//				msheader, BMP info header data
//
// Postconditions:
//
// Returns: B_ERROR, if something went wrong
//
// B_OK, if there were no problems writing out the headers
// ---------------------------------------------------------------
status_t
write_bmp_headers(BPositionIO *outDestination, BMPFileHeader &fileHeader,
	MSInfoHeader &msheader)
{
	uint8 bmpheaders[54];
	memcpy(bmpheaders, &fileHeader.magic, sizeof(uint16));
	memcpy(bmpheaders + 2, &fileHeader.fileSize, sizeof(uint32));
	memcpy(bmpheaders + 6, &fileHeader.reserved, sizeof(uint32));
	memcpy(bmpheaders + 10, &fileHeader.dataOffset, sizeof(uint32));
	memcpy(bmpheaders + 14, &msheader, sizeof(msheader));
	if (swap_data(B_UINT16_TYPE, bmpheaders, 2,
		B_SWAP_HOST_TO_LENDIAN) != B_OK)
		return B_ERROR;
	if (swap_data(B_UINT32_TYPE, bmpheaders + 2, 12,
		B_SWAP_HOST_TO_LENDIAN) != B_OK)
		return B_ERROR;
	if (swap_data(B_UINT32_TYPE, bmpheaders + 14,
		sizeof(MSInfoHeader), B_SWAP_HOST_TO_LENDIAN) != B_OK)
		return B_ERROR;
	if (outDestination->Write(bmpheaders, 54) != 54)
		return B_ERROR;
		
	return B_OK;
}

// ---------------------------------------------------------------
// translate_from_bits
//
// Convert the data in inSource from the Be Bitmap format ('bits')
// to the format specified in outType (either bits or BMP).
//
// Preconditions:
//
// Parameters:	inSource,	the bits data to translate
//
// 				amtread,	the amount of data already read from
//							inSource
//
//				read,		pointer to the data already read from
//							inSource
//
//				bheaderonly,	true if only the header should be
//								written out
//
//				bdataonly,	true if only the data should be
//							written out
//
//				outType,	the type of data to convert to
//
//				outDestination,	where the output is written to
//
// Postconditions:
//
// Returns: B_NO_TRANSLATOR,	if the data is not in a supported
//								format
//
// B_ERROR, if there was an error allocating memory or some other
//			error
//
// B_OK, if successfully translated the data from the bits format
// ---------------------------------------------------------------
status_t
translate_from_bits(BPositionIO *inSource, ssize_t amtread, uint8 *read,
	bool bheaderonly, bool bdataonly, uint32 outType,
	BPositionIO *outDestination)
{
	TranslatorBitmap bitsHeader;
		
	status_t result;
	result = identify_bits_header(inSource, NULL, amtread, read, &bitsHeader);
	if (result != B_OK)
		return result;
	
	// Translate B_TRANSLATOR_BITMAP to B_TRANSLATOR_BITMAP, easy enough :)	
	if (outType == B_TRANSLATOR_BITMAP) {
		// write out bitsHeader (only if configured to)
		if (bheaderonly || (!bheaderonly && !bdataonly)) {
			if (swap_data(B_UINT32_TYPE, &bitsHeader,
				sizeof(TranslatorBitmap), B_SWAP_HOST_TO_BENDIAN) != B_OK)
				return B_ERROR;
			if (outDestination->Write(&bitsHeader,
				sizeof(TranslatorBitmap)) != sizeof(TranslatorBitmap))
				return B_ERROR;
		}
		
		// write out the data (only if configured to)
		if (bdataonly || (!bheaderonly && !bdataonly)) {	
			uint8 buf[1024];
			uint32 remaining = B_BENDIAN_TO_HOST_INT32(bitsHeader.dataSize);
			ssize_t rd, writ;
			rd = inSource->Read(buf, 1024);
			while (rd > 0) {
				writ = outDestination->Write(buf, rd);
				if (writ < 0)
					break;
				remaining -= static_cast<uint32>(writ);
				rd = inSource->Read(buf, min(1024, remaining));
			}
		
			if (remaining > 0)
				return B_ERROR;
			else
				return B_OK;
		} else
			return B_OK;
		
	// Translate B_TRANSLATOR_BITMAP to B_BMP_FORMAT
	} else if (outType == B_BMP_FORMAT) {
		// Set up BMP header
		BMPFileHeader fileHeader;
		fileHeader.magic = 'MB';
		fileHeader.reserved = 0;
		
		MSInfoHeader msheader;
		msheader.size = 40;
		msheader.width = 
			static_cast<uint32> (bitsHeader.bounds.Width() + 1);
		msheader.height = 
			static_cast<uint32> (bitsHeader.bounds.Height() + 1);
		msheader.planes = 1;
		msheader.xpixperm = 2835; // 72 dpi horizontal
		msheader.ypixperm = 2835; // 72 dpi vertical
		msheader.colorsused = 0;
		msheader.colorsimportant = 0;
		
		// determine fileSize / imagesize
		switch (bitsHeader.colors) {
			case B_RGB32:
			case B_RGB32_BIG:
			case B_RGBA32:
			case B_RGBA32_BIG:
			case B_RGB24:
			case B_RGB24_BIG:
			case B_RGB16:
			case B_RGB16_BIG:
			case B_RGB15:
			case B_RGB15_BIG:
			case B_RGBA15:
			case B_RGBA15_BIG:
			case B_CMYK32:
			case B_CMY32:
			case B_CMYA32:
			case B_CMY24:
	
				fileHeader.dataOffset = 54;
				msheader.bitsperpixel = 24;
				msheader.compression = BMP_NO_COMPRESS;
				msheader.imagesize = get_rowbytes(msheader.width, 24) *
					msheader.height;
				fileHeader.fileSize = fileHeader.dataOffset +
					msheader.imagesize;
			
				break;
				
			case B_CMAP8:
			case B_GRAY8:
			
				msheader.colorsused = 256;
				msheader.colorsimportant = 256;
				fileHeader.dataOffset = 54 + (4 * 256);
				msheader.bitsperpixel = 8;
				msheader.compression = BMP_NO_COMPRESS;
				msheader.imagesize = get_rowbytes(msheader.width,
					msheader.bitsperpixel) * msheader.height;
				fileHeader.fileSize = fileHeader.dataOffset +
					msheader.imagesize;
					
				break;
				
			case B_GRAY1:
			
				msheader.colorsused = 2;
				msheader.colorsimportant = 2;
				fileHeader.dataOffset = 62;
				msheader.bitsperpixel = 1;
				msheader.compression = BMP_NO_COMPRESS;
				msheader.imagesize = get_rowbytes(msheader.width,
					msheader.bitsperpixel) * msheader.height;
				fileHeader.fileSize = fileHeader.dataOffset +
					msheader.imagesize;
					
				break;
				
			default:
				return B_NO_TRANSLATOR;
		}
		
		// write out the BMP headers
		if (bheaderonly || (!bheaderonly && !bdataonly)) {
			result = write_bmp_headers(outDestination, fileHeader, msheader);
			if (result != B_OK)
				return result;
		}
		if (bheaderonly)
			// if user only wants the header, bail out
			// before the data is written
			return result;

		// write out the BMP pixel data
		switch (bitsHeader.colors) {
			case B_RGB32:
			case B_RGB32_BIG:
			case B_RGBA32:
			case B_RGBA32_BIG:
			case B_RGB24:
			case B_RGB24_BIG:
			case B_RGB16:
			case B_RGB16_BIG:
			case B_RGB15:
			case B_RGB15_BIG:
			case B_RGBA15:
			case B_RGBA15_BIG:
			case B_CMYK32:
			case B_CMY32:
			case B_CMYA32:
			case B_CMY24:
				return translate_from_bits_to_bmp24(inSource, outDestination,
					bitsHeader.colors, msheader);
					
			case B_CMAP8:
			{
				// write Be's system palette to the BMP file
				uint8 pal[1024] = { 0 };
				const color_map *pmap = system_colors();
				if (!pmap)
					return B_ERROR;
				for (int32 i = 0; i < 256; i++) {
					uint8 *palent = pal + (i * 4);
					rgb_color c = pmap->color_list[i];
					palent[0] = c.blue;
					palent[1] = c.green;
					palent[2] = c.red;
				}
				if (outDestination->Write(pal, 1024) != 1024)
					return B_ERROR;

				return translate_from_bits8_to_bmp8(inSource, outDestination,
					bitsHeader.rowBytes, msheader);
			}
			
			case B_GRAY8:
			{
				// write out a grayscale palette to the BMP file
				uint8 pal[1024] = { 0 };
				for (int32 i = 0; i < 256; i++) {
					uint8 *palent = pal + (i * 4);
					palent[0] = i;
					palent[1] = i;
					palent[2] = i;
				}
				if (outDestination->Write(pal, 1024) != 1024)
					return B_ERROR;

				return translate_from_bits8_to_bmp8(inSource, outDestination,
					bitsHeader.rowBytes, msheader);
			}
				
			case B_GRAY1:
			{
				// write monochrome palette to the BMP file
				const uint32 monopal[] = { 0x00ffffff, 0x00000000 };
				if (outDestination->Write(monopal, 8) != 8)
					return B_ERROR;
				
				return translate_from_bits1_to_bmp1(inSource, outDestination,
					bitsHeader.rowBytes, msheader);
			}
				
			default:
				return B_NO_TRANSLATOR;
		}
	} else
		return B_NO_TRANSLATOR;
}

// ---------------------------------------------------------------
// translate_from_bmpnpal_to_bits
//
// Translates a non-palette BMP from inSource to the B_RGB32
// bits format.
//
// Preconditions:
//
// Parameters: inSource,	the BMP data to be translated
//
// outDestination,	where the bits data will be written to
//
// msheader,	header information about the BMP to be written
//
// Postconditions:
//
// Returns: B_ERROR, if there is an error allocating memory
//
// B_OK, if all went well
// ---------------------------------------------------------------
status_t
translate_from_bmpnpal_to_bits(BPositionIO *inSource,
	BPositionIO *outDestination, MSInfoHeader &msheader)
{
	int32 bitsRowBytes = msheader.width * 4;
	int32 bmpBytesPerPixel = msheader.bitsperpixel / 8;
	int32 bmpRowBytes =
		get_rowbytes(msheader.width, msheader.bitsperpixel);
	
	// Setup outDestination so that it can be written to
	// from the end of the file to the beginning instead of
	// the other way around
	off_t bitsFileSize = (bitsRowBytes * msheader.height) + 
		sizeof(TranslatorBitmap);
	if (outDestination->SetSize(bitsFileSize) != B_OK)
		// This call should work for BFile and BMallocIO objects,
		// but may not work for other BPositionIO based types
		return B_ERROR;
	off_t bitsoffset = (msheader.height - 1) * bitsRowBytes;
	outDestination->Seek(bitsoffset, SEEK_CUR);
	
	// allocate row buffers
	uint8 *bmpRowData = new uint8[bmpRowBytes];
	if (!bmpRowData)
		return B_ERROR;
	uint8 *bitsRowData = new uint8[bitsRowBytes];
	if (!bitsRowData) {
		delete[] bmpRowData;
		bmpRowData = NULL;
		return B_ERROR;
	}

	// perform the actual translation
	uint32 bmppixrow = 0;
	memset(bitsRowData, 0xff, bitsRowBytes);
	ssize_t rd = inSource->Read(bmpRowData, bmpRowBytes);
	while (rd == static_cast<ssize_t>(bmpRowBytes)) {
		uint8 *pBitsPixel = bitsRowData;
		uint8 *pBmpPixel = bmpRowData;
		for (uint32 i = 0; i < msheader.width; i++) {
			memcpy(pBitsPixel, pBmpPixel, 3);
			pBitsPixel += 4;
			pBmpPixel += bmpBytesPerPixel;
		}
				
		outDestination->Write(bitsRowData, bitsRowBytes);
		bmppixrow++;
		// if I've read all of the pixel data, break
		// out of the loop so I don't try to read 
		// non-pixel data
		if (bmppixrow == msheader.height)
			break;

		outDestination->Seek(bitsRowBytes * -2, SEEK_CUR);
		rd = inSource->Read(bmpRowData, bmpRowBytes);
	}
	
	delete[] bmpRowData;
	bmpRowData = NULL;
	delete[] bitsRowData;
	bitsRowData = NULL;

	return B_OK;
}

// ---------------------------------------------------------------
// translate_from_bmppal_to_bits
//
// Translates an uncompressed, palette BMP from inSource to 
// the B_RGB32 bits format.
//
// Preconditions:
//
// Parameters: inSource,	the BMP data to be translated
//
// outDestination,	where the bits data will be written to
//
// msheader,	header information about the BMP to be written
//
// palette,	BMP palette for the BMP image
//
// frommsformat, true if BMP in inSource is in MS format,
//				 false if it is in OS/2 format
//
// Postconditions:
//
// Returns: B_ERROR, if there is an error allocating memory
//
// B_OK, if all went well
// ---------------------------------------------------------------
status_t
translate_from_bmppal_to_bits(BPositionIO *inSource,
	BPositionIO *outDestination, MSInfoHeader &msheader,
	const uint8 *palette, bool frommsformat)
{
	uint16 pixelsPerByte = 8 / msheader.bitsperpixel;
	uint16 bitsPerPixel = msheader.bitsperpixel;
	uint8 palBytesPerPixel;
	if (frommsformat)
		palBytesPerPixel = 4;
	else
		palBytesPerPixel = 3;
	
	uint8 mask = 1;
	mask = (mask << bitsPerPixel) - 1;

	int32 bmpRowBytes =
		get_rowbytes(msheader.width, msheader.bitsperpixel);
	uint32 bmppixrow = 0;
	
	// Setup outDestination so that it can be written to
	// from the end of the file to the beginning instead of
	// the other way around
	int32 bitsRowBytes = msheader.width * 4;
	off_t bitsFileSize = (bitsRowBytes * msheader.height) + 
		sizeof(TranslatorBitmap);
	if (outDestination->SetSize(bitsFileSize) != B_OK)
		// This call should work for BFile and BMallocIO objects,
		// but may not work for other BPositionIO based types
		return B_ERROR;
	off_t bitsoffset = ((msheader.height - 1) * bitsRowBytes);
	outDestination->Seek(bitsoffset, SEEK_CUR);

	// allocate row buffers
	uint8 *bmpRowData = new uint8[bmpRowBytes];
	if (!bmpRowData)
		return B_ERROR;
	uint8 *bitsRowData = new uint8[bitsRowBytes];
	if (!bitsRowData) {
		delete[] bmpRowData;
		bmpRowData = NULL;
		return B_ERROR;
	}
	memset(bitsRowData, 0xff, bitsRowBytes);
	ssize_t rd = inSource->Read(bmpRowData, bmpRowBytes);
	while (rd == static_cast<ssize_t>(bmpRowBytes)) {
		for (uint32 i = 0; i < msheader.width; i++) {
			uint8 indices = (bmpRowData + (i / pixelsPerByte))[0];
			uint8 index;
			index = (indices >>
				(bitsPerPixel * ((pixelsPerByte - 1) - 
					(i % pixelsPerByte)))) & mask;
			memcpy(bitsRowData + (i * 4),
				palette + (index * palBytesPerPixel), 3);
		}
				
		outDestination->Write(bitsRowData, bitsRowBytes);
		bmppixrow++;
		// if I've read all of the pixel data, break
		// out of the loop so I don't try to read 
		// non-pixel data
		if (bmppixrow == msheader.height)
			break;

		outDestination->Seek(bitsRowBytes * -2, SEEK_CUR);
		rd = inSource->Read(bmpRowData, bmpRowBytes);
	}
	
	delete[] bmpRowData;
	bmpRowData = NULL;
	delete[] bitsRowData;
	bitsRowData = NULL;

	return B_OK;
}


// ---------------------------------------------------------------
// pixelcpy
//
// Copies count 32-bit pixels with a color value of pixel to dest.
//
// Preconditions:
//
// Parameters:	dest,	where the pixel data will be copied to
//
//				pixel,	the 32-bit color value to copy to dest
//						count times
//
//				count,	the number of times pixel is copied to
//						dest
//
// Postconditions:
//
// Returns:
// ---------------------------------------------------------------
void
pixelcpy(uint8 *dest, uint32 pixel, uint32 count)
{
	for (uint32 i = 0; i < count; i++) {
		memcpy(dest, &pixel, 3);
		dest += 4;
	}
}

// ---------------------------------------------------------------
// translate_from_bmppalr_to_bits
//
// Translates an RLE compressed, palette BMP from inSource to 
// the B_RGB32 bits format. Currently, this code is not as
// memory effcient as it could be. It assumes that the BMP
// from inSource is relatively small. 
//
// Preconditions:
//
// Parameters: inSource,	the BMP data to be translated
//
// outDestination,	where the bits data will be written to
//
// datasize, number of bytes of data needed for the bits output
//
// msheader,	header information about the BMP to be written
//
// palette,	BMP palette for data in inSource
//
// Postconditions:
//
// Returns: B_ERROR, if there is an error allocating memory
//
// B_OK, if all went well
// ---------------------------------------------------------------
status_t
translate_from_bmppalr_to_bits(BPositionIO *inSource,
	BPositionIO *outDestination, int32 datasize, MSInfoHeader &msheader,
	const uint8 *palette)
{
	uint16 pixelsPerByte = 8 / msheader.bitsperpixel;
	uint16 bitsPerPixel = msheader.bitsperpixel;
	uint8 mask = (1 << bitsPerPixel) - 1;

	uint8 count, indices, index;
	// Setup outDestination so that it can be written to
	// from the end of the file to the beginning instead of
	// the other way around
	int32 bitsRowBytes = msheader.width * 4;
	off_t bitsFileSize = (bitsRowBytes * msheader.height) + 
		sizeof(TranslatorBitmap);
	if (outDestination->SetSize(bitsFileSize) != B_OK)
		// This call should work for BFile and BMallocIO objects,
		// but may not work for other BPositionIO based types
		return B_ERROR;
	uint8 *bitsRowData = new uint8[bitsRowBytes];
	if (!bitsRowData)
		return B_ERROR;
	memset(bitsRowData, 0xff, bitsRowBytes);
	uint32 bmppixcol = 0, bmppixrow = 0;
	uint32 defaultcolor = 0;
	memcpy(&defaultcolor, palette, 4);
	// set bits output to last row in the image
	off_t bitsoffset = ((msheader.height - (bmppixrow + 1)) * bitsRowBytes) +
		(bmppixcol * 4);
	outDestination->Seek(bitsoffset, SEEK_CUR);
	ssize_t rd = inSource->Read(&count, 1);
	while (rd > 0) {
		// repeated color
		if (count) {
			// abort if all of the pixels in the row
			// have already been drawn to
			if (bmppixcol == msheader.width) {
				rd = -1;
				break;
			}
			// if count is greater than the number of
			// pixels remaining in the current row, 
			// only process the correct number of pixels
			// remaining in the row
			if (count + bmppixcol > msheader.width)
				count = msheader.width - bmppixcol;
			
			rd = inSource->Read(&indices, 1);
			if (rd != 1) {
				rd = -1;
				break;
			}
			for (uint8 i = 0; i < count; i++) {
				index = (indices >> (bitsPerPixel * ((pixelsPerByte - 1) - 
					(i % pixelsPerByte)))) & mask;
				memcpy(bitsRowData + (bmppixcol*4), palette + (index*4), 3);
				bmppixcol++;
			}
		// special code
		} else {
			uint8 code;
			rd = inSource->Read(&code, 1);
			if (rd != 1) {
				rd = -1;
				break;
			}
			switch (code) {
				// end of line
				case 0:
					// if there are columns remaing on this
					// line, set them to the color at index zero
					if (bmppixcol < msheader.width)
						pixelcpy(bitsRowData + (bmppixcol * 4),
							defaultcolor, msheader.width - bmppixcol);
					outDestination->Write(bitsRowData, bitsRowBytes);
					bmppixcol = 0;
					bmppixrow++;
					if (bmppixrow < msheader.height)
						outDestination->Seek(bitsRowBytes * -2, SEEK_CUR);
					break;
				
				// end of bitmap				
				case 1:
					// if at the end of a row
					if (bmppixcol == msheader.width) {
						outDestination->Write(bitsRowData, bitsRowBytes);
						bmppixcol = 0;
						bmppixrow++;
						if (bmppixrow < msheader.height)
							outDestination->Seek(bitsRowBytes * -2, SEEK_CUR);
					}
					
					while (bmppixrow < msheader.height) {
						pixelcpy(bitsRowData + (bmppixcol * 4), defaultcolor,
							msheader.width - bmppixcol);
						outDestination->Write(bitsRowData, bitsRowBytes);
						bmppixcol = 0;
						bmppixrow++;
						if (bmppixrow < msheader.height)
							outDestination->Seek(bitsRowBytes * -2, SEEK_CUR);
					}
					rd = 0;
						// break out of while loop
					break;
				
				// delta, skip several rows and/or columns and
				// fill the skipped pixels with the default color				
				case 2:
				{
					uint8 da[2], lastcol, dx, dy;
					rd = inSource->Read(da, 2);
					if (rd != 2) {
						rd = -1;
						break;
					}
					dx = da[0];
					dy = da[1];
					
					// abort if dx or dy is too large
					if ((dx + bmppixcol >= msheader.width) ||
						(dy + bmppixrow >= msheader.height)) {
						rd = -1;
						break;
					}	
								
					lastcol = bmppixcol;
								
					// set all pixels to the first entry in
					// the palette, for the number of rows skipped
					while (dy > 0) {
						pixelcpy(bitsRowData + (bmppixcol * 4), defaultcolor,
							msheader.width - bmppixcol);
						outDestination->Write(bitsRowData, bitsRowBytes);
						bmppixcol = 0;
						bmppixrow++;
						dy--;
						outDestination->Seek(bitsRowBytes * -2, SEEK_CUR);
					}
								
					if (bmppixcol < static_cast<uint32>(lastcol + dx)) {
						pixelcpy(bitsRowData + (bmppixcol * 4), defaultcolor,
							dx + lastcol - bmppixcol);
						bmppixcol = dx + lastcol;
					}
					
					break;
				}

				// code >= 3
				// read code uncompressed indices
				default:
					// abort if all of the pixels in the row
					// have already been drawn to
					if (bmppixcol == msheader.width) {
						rd = -1;
						break;
					}
					// if code is greater than the number of
					// pixels remaining in the current row, 
					// only process the correct number of pixels
					// remaining in the row
					if (code + bmppixcol > msheader.width)
						code = msheader.width - bmppixcol;
					
					uint8 uncomp[256];
					int32 padding;
					if (!(code % pixelsPerByte))
						padding = (code / pixelsPerByte) % 2;
					else
						padding = ((code + pixelsPerByte - 
							(code % pixelsPerByte)) / pixelsPerByte) % 2;
					int32 uncompBytes = (code / pixelsPerByte) + 
						((code % pixelsPerByte) ? 1 : 0) + padding;
					rd = inSource->Read(uncomp, uncompBytes);
					if (rd != uncompBytes) {
						rd = -1;
						break;
					}
					for (uint8 i = 0; i < code; i++) {
						indices = (uncomp + (i / pixelsPerByte))[0];
						index = (indices >>
							(bitsPerPixel * ((pixelsPerByte - 1) - 
								(i % pixelsPerByte)))) & mask;
						memcpy(bitsRowData + (bmppixcol * 4),
							palette + (index * 4), 3);
						bmppixcol++;
					}

					break;
			}
		}
		if (rd > 0)
			rd = inSource->Read(&count, 1);
	}
	
	delete[] bitsRowData;
	bitsRowData = NULL;
	
	if (!rd)
		return B_OK;
	else
		return B_NO_TRANSLATOR;
}

// ---------------------------------------------------------------
// translate_from_bmp
//
// Convert the data in inSource from the BMP format
// to the format specified in outType (either bits or BMP).
//
// Preconditions:
//
// Parameters:	inSource,	the bits data to translate
//
// 				amtread,	the amount of data already read from
//							inSource
//
//				read,		pointer to the data already read from
//							inSource
//
//				bheaderonly,	true if only the header should be
//								written out
//
//				bdataonly,	true if only the data should be
//							written out
//
//				outType,	the type of data to convert to
//
//				outDestination,	where the output is written to
//
// Postconditions:
//
// Returns: B_NO_TRANSLATOR,	if the data is not in a supported
//								format
//
// B_ERROR, if there was an error allocating memory or some other
//			error
//
// B_OK, if successfully translated the data from the bits format
// ---------------------------------------------------------------
status_t
translate_from_bmp(BPositionIO *inSource, ssize_t amtread, uint8 *read,
	bool bheaderonly, bool bdataonly, uint32 outType,
	BPositionIO *outDestination)
{
	BMPFileHeader fileHeader;
	MSInfoHeader msheader;
	bool frommsformat;
	off_t os2skip = 0;

	status_t result;
	result = identify_bmp_header(inSource, NULL, amtread, read,
		&fileHeader, &msheader, &frommsformat, &os2skip);
	if (result != B_OK)
		return result;
	
	// if the user wants to translate a BMP to a BMP, easy enough :)	
	if (outType == B_BMP_FORMAT) {
		// write out the BMP headers
		if (bheaderonly || (!bheaderonly && !bdataonly)) {
			result = write_bmp_headers(outDestination, fileHeader, msheader);
			if (result != B_OK)
				return result;
		}
		if (bheaderonly)
			// if the user only wants the header, 
			// bail before it is written
			return result;
			
		uint8 buf[1024];
		ssize_t rd;
		uint32 rdtotal = 54;
		if (!frommsformat && (msheader.bitsperpixel == 1 ||	
			msheader.bitsperpixel == 4 || msheader.bitsperpixel == 8)) {
			// if OS/2 paletted format, convert palette to MS format
			uint16 ncolors = 1 << msheader.bitsperpixel;
			rd = inSource->Read(buf, ncolors * 3);
			if (rd != ncolors * 3)
				return B_NO_TRANSLATOR;
			uint8 mspalent[4] = {0, 0, 0, 0};
			for (uint16 i = 0; i < ncolors; i++) {
				memcpy(mspalent, buf + (i * 3), 3);
				outDestination->Write(mspalent, 4);
			}
			rdtotal = fileHeader.dataOffset;
		}
		// if there is junk between the OS/2 headers and
		// the actual data, skip it
		if (!frommsformat && os2skip)
			inSource->Seek(os2skip, SEEK_CUR);

		rd = min(1024, fileHeader.fileSize - rdtotal);
		rd = inSource->Read(buf, rd);
		while (rd > 0) {
			outDestination->Write(buf, rd);
			rdtotal += rd;
			rd = min(1024, fileHeader.fileSize - rdtotal);
			rd = inSource->Read(buf, rd);
		}
		if (rd == 0)
			return B_OK;
		else
			return B_ERROR;
	
	// if translating a BMP to a Be Bitmap
	} else if (outType == B_TRANSLATOR_BITMAP) {
		TranslatorBitmap bitsHeader;
		bitsHeader.magic = B_TRANSLATOR_BITMAP;
		bitsHeader.bounds.left = 0;
		bitsHeader.bounds.top = 0;
		bitsHeader.bounds.right = msheader.width - 1;
		bitsHeader.bounds.bottom = msheader.height - 1;
		
		// read in palette and/or skip non-BMP data					
		uint8 bmppalette[1024];
		off_t nskip = 0;
		if (msheader.bitsperpixel == 1 ||
			msheader.bitsperpixel == 4 ||
			msheader.bitsperpixel == 8) {
			
			uint8 palBytesPerPixel;
			if (frommsformat)
				palBytesPerPixel = 4;
			else
				palBytesPerPixel = 3;
			
			if (!msheader.colorsused)
				msheader.colorsused = 1 << msheader.bitsperpixel;
			
			if (inSource->Read(bmppalette, msheader.colorsused *
				palBytesPerPixel) !=
					(off_t) msheader.colorsused * palBytesPerPixel)
				return B_NO_TRANSLATOR;
				
			// skip over non-BMP data
			if (frommsformat) {
				if (fileHeader.dataOffset > (msheader.colorsused * 
					palBytesPerPixel) + 54)
					nskip = fileHeader.dataOffset -
						((msheader.colorsused * palBytesPerPixel) + 54);
			} else
				nskip = os2skip;
		} else if (fileHeader.dataOffset > 54)
			// skip over non-BMP data
			nskip = fileHeader.dataOffset - 54;
			
		if (nskip > 0 && inSource->Seek(nskip, SEEK_CUR) < 0)
			return B_NO_TRANSLATOR;

		bitsHeader.rowBytes = msheader.width * 4;
		bitsHeader.colors = B_RGB32;
		int32 datasize = bitsHeader.rowBytes * msheader.height;
		bitsHeader.dataSize = datasize;
				
		// write out Be's Bitmap header
		if (bheaderonly || (!bheaderonly && !bdataonly)) {
			if (swap_data(B_UINT32_TYPE, &bitsHeader,
				sizeof(TranslatorBitmap), B_SWAP_HOST_TO_BENDIAN) != B_OK)
				return B_ERROR;
			outDestination->Write(&bitsHeader, sizeof(TranslatorBitmap));
		}
		if (bheaderonly)
			// if the user only wants the header, 
			// bail before the data is written
			return B_OK;
		
		// write out the actual image data
		switch (msheader.bitsperpixel) {
			case 32:
			case 24:
				return translate_from_bmpnpal_to_bits(inSource,
					outDestination, msheader);
				
			case 8:
				// 8 bit BMP with NO compression
				if (msheader.compression == BMP_NO_COMPRESS)
					return translate_from_bmppal_to_bits(inSource,
						outDestination, msheader, bmppalette, frommsformat);

				// 8 bit RLE compressed BMP
				else if (msheader.compression == BMP_RLE8_COMPRESS)
					return translate_from_bmppalr_to_bits(inSource,
						outDestination, datasize, msheader, bmppalette);
				else
					return B_NO_TRANSLATOR;
			
			case 4:
				// 4 bit BMP with NO compression
				if (!msheader.compression)	
					return translate_from_bmppal_to_bits(inSource,
						outDestination, msheader, bmppalette, frommsformat);

				// 4 bit RLE compressed BMP
				else if (msheader.compression == BMP_RLE4_COMPRESS)
					return translate_from_bmppalr_to_bits(inSource,
						outDestination, datasize, msheader, bmppalette);
				else
					return B_NO_TRANSLATOR;
			
			case 1:
				return translate_from_bmppal_to_bits(inSource,
					outDestination, msheader, bmppalette, frommsformat);
					
			default:
				return B_NO_TRANSLATOR;
		}
		
	} else
		return B_NO_TRANSLATOR;
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
BMPTranslator::Translate(BPositionIO *inSource,
		const translator_info *inInfo, BMessage *ioExtension,
		uint32 outType, BPositionIO *outDestination)
{
	if (!outType)
		outType = B_TRANSLATOR_BITMAP;
	if (outType != B_TRANSLATOR_BITMAP && outType != B_BMP_FORMAT)
		return B_NO_TRANSLATOR;
		
	inSource->Seek(0, SEEK_SET);
	
	uint8 ch[4];
	uint32 nbits = B_TRANSLATOR_BITMAP;
	uint16 nbm = 'MB';
	
	// Convert the magic numbers to the various byte orders so that
	// I won't have to convert the data read in to see whether or not
	// it is a supported type
	if (swap_data(B_UINT32_TYPE, &nbits, sizeof(uint32),
		B_SWAP_HOST_TO_BENDIAN) != B_OK)
		return B_ERROR;
	if (swap_data(B_UINT16_TYPE, &nbm, sizeof(uint16),
		B_SWAP_HOST_TO_LENDIAN) != B_OK)
		return B_ERROR;
	
	// Read in the magic number and determine if it
	// is a supported type
	if (inSource->Read(ch, 4) != 4)
		return B_NO_TRANSLATOR;
		
	// Read settings from ioExtension
	bool bheaderonly = false, bdataonly = false;
	if (ioExtension) {
		if (ioExtension->FindBool(B_TRANSLATOR_EXT_HEADER_ONLY, &bheaderonly))
			// if failed, make sure bool is default value
			bheaderonly = false;
		if (ioExtension->FindBool(B_TRANSLATOR_EXT_DATA_ONLY, &bdataonly))
			// if failed, make sure bool is default value
			bdataonly = false;
			
		if (bheaderonly && bdataonly)
			// can't both "only write the header" and "only write the data"
			// at the same time
			return B_BAD_VALUE;
	}
	bheaderonly = bdataonly = false;
		// only allow writing of the entire image
		// (fix for buggy programs that lie about what they actually need)
	
	uint32 n32ch;
	memcpy(&n32ch, ch, sizeof(uint32));
	uint16 n16ch;
	memcpy(&n16ch, ch, sizeof(uint16));
	// if B_TRANSLATOR_BITMAP type	
	if (n32ch == nbits)
		return translate_from_bits(inSource, 4, ch, bheaderonly, bdataonly,
			outType, outDestination);
	// if BMP type in Little Endian byte order
	else if (n16ch == nbm)
		return translate_from_bmp(inSource, 4, ch, bheaderonly, bdataonly,
			outType, outDestination);
	else
		return B_NO_TRANSLATOR;
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
BMPTranslator::MakeConfigurationView(BMessage *ioExtension, BView **outView,
	BRect *outExtent)
{
	if (!outView || !outExtent)
		return B_BAD_VALUE;

	BMPView *view = new BMPView(BRect(0, 0, 225, 175),
		"BMPTranslator Settings", B_FOLLOW_ALL, B_WILL_DRAW);
	if (!view)
		return B_NO_MEMORY;
		
	*outView = view;
	*outExtent = view->Bounds();

	return B_OK;
}
