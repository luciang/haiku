/*****************************************************************************/
// TIFFView
// Written by Michael Wilber, OBOS Translation Kit Team
// Picking the compression method added by Stephan Aßmus, <stippi@yellowbites.com>
//
// TIFFView.h
//
// This BView based object displays information about the TIFFTranslator.
//
//
// Copyright (c) 2003 OpenBeOS Project
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

#ifndef TIFFVIEW_H
#define TIFFVIEW_H

#include <View.h>

class BMenuField;
class TIFFTranslatorSettings;

class TIFFView : public BView {
public:
	TIFFView(const BRect &frame, const char *name, uint32 resize,
		uint32 flags, TIFFTranslatorSettings* psettings);
		// sets up the view
		
	~TIFFView();
		// releases the TIFFTranslator settings

	virtual void AllAttached();
	virtual void MessageReceived(BMessage *message);

	virtual	void Draw(BRect area);
		// draws information about the TIFFTranslator

	enum {
		MSG_COMPRESSION_CHANGED	= 'cmch',
	};

private:
	BMenuField*				fCompressionMF;

	TIFFTranslatorSettings*	fSettings;
		// the actual settings for the translator,
		// shared with the translator
};

#endif // #ifndef TIFFVIEW_H
