/*

Copyright (c) 2002-2003, Marcin 'Shard' Konicki
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation and/or
      other materials provided with the distribution.
    * Name "Marcin Konicki", "Shard" or any combination of them,
      must not be used to endorse or promote products derived from this
      software without specific prior written permission from Marcin Konicki.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#ifndef _JPEGTRANSLATOR_H_
#define _JPEGTRANSLATOR_H_


#include <Alert.h>
#include <Application.h>
#include <CheckBox.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Slider.h>
#include <StringView.h>
#include <TabView.h>
#include <TranslationKit.h>
#include <TranslatorAddOn.h>

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>


// Settings
#define SETTINGS_FILE	"JPEGTranslator"

// View messages
#define VIEW_MSG_SET_QUALITY 'JSCQ'
#define VIEW_MSG_SET_SMOOTHING 'JSCS'
#define VIEW_MSG_SET_PROGRESSIVE 'JSCP'
#define VIEW_MSG_SET_OPTIMIZECOLORS 'JSBQ'
#define	VIEW_MSG_SET_SMALLERFILE 'JSSF'
#define	VIEW_MSG_SET_GRAY1ASRGB24 'JSGR'
#define	VIEW_MSG_SET_ALWAYSRGB32 'JSAC'
#define	VIEW_MSG_SET_PHOTOSHOPCMYK 'JSPC'
#define	VIEW_MSG_SET_SHOWREADERRORBOX 'JSEB'

// View labels
#define VIEW_LABEL_QUALITY "Output quality"
#define VIEW_LABEL_SMOOTHING "Output smoothing strength"
#define VIEW_LABEL_PROGRESSIVE "Use progressive compression"
#define VIEW_LABEL_OPTIMIZECOLORS "Prevent colors 'washing out'"
#define	VIEW_LABEL_SMALLERFILE "Make file smaller (sligthtly worse quality)"
#define	VIEW_LABEL_GRAY1ASRGB24 "Write black-and-white images as RGB24"
#define	VIEW_LABEL_ALWAYSRGB32 "Read greyscale images as RGB32"
#define	VIEW_LABEL_PHOTOSHOPCMYK "Use CMYK code with 0 for 100% ink coverage"
#define	VIEW_LABEL_SHOWREADERRORBOX "Show warning messages"


//!	Settings storage structure
struct jpeg_settings {
	// compression
	uchar	Smoothing;			// default: 0
	uchar	Quality;			// default: 95
	bool	Progressive;		// default: true
	bool	OptimizeColors;		// default: true
	bool	SmallerFile;		// default: false	only used if (OptimizeColors == true)
	bool	B_GRAY1_as_B_RGB24;	// default: false	if false gray1 converted to gray8, else to rgb24
	// decompression
	bool	Always_B_RGB32;		// default: true
	bool	PhotoshopCMYK;		// default: true
	bool	ShowReadWarningBox;	// default: true
};


/*!
	Slider used in TranslatorView
	With status showing actual value
*/
class SSlider : public BSlider {
	public:
				SSlider(BRect frame, const char *name, const char *label,
					BMessage *message, int32 minValue, int32 maxValue,
					orientation posture = B_HORIZONTAL,
					thumb_style thumbType = B_BLOCK_THUMB,
					uint32 resizingMode = B_FOLLOW_LEFT | B_FOLLOW_TOP,
					uint32 flags = B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS);
	virtual const char* UpdateText() const;
		void	ResizeToPreferred();

	private:
		mutable char fStatusLabel[12];
};


class SView : public BView {
public:
	SView(BRect frame, const char *name);
	virtual void AttachedToWindow();
};

//!	Configuration view for reading settings
class TranslatorReadView : public SView {
	public:
		TranslatorReadView(BRect frame, const char* name, jpeg_settings* settings);

		virtual void	AttachedToWindow();
		virtual void	MessageReceived(BMessage* message);

	private:
		jpeg_settings*	fSettings;
		BCheckBox*		fAlwaysRGB32;
		BCheckBox*		fPhotoshopCMYK;
		BCheckBox*		fShowErrorBox;
};


//! Configuration view for writing settings
class TranslatorWriteView : public SView {
	public:
		TranslatorWriteView(BRect frame, const char* name, jpeg_settings* settings);

		virtual void	AttachedToWindow();
		virtual void	MessageReceived(BMessage* message);

	private:
		jpeg_settings*	fSettings;
		SSlider*		fQualitySlider;
		SSlider*		fSmoothingSlider;
		BCheckBox*		fProgress;
		BCheckBox*		fOptimizeColors;
		BCheckBox*		fSmallerFile;
		BCheckBox*		fGrayAsRGB24;
};

class TranslatorAboutView : public SView {
	public:
		TranslatorAboutView(BRect frame, const char* name);
};

//!	Configuration view
class TranslatorView : public BTabView {
	public:
		TranslatorView(BRect frame, const char *name);
		virtual ~TranslatorView();

	private:
		jpeg_settings	fSettings;
};

//!	Window used for configuration
class TranslatorWindow : public BWindow {
	public:
		TranslatorWindow(bool quitOnClose = true);
};


//---------------------------------------------------
//	"Initializers" for jpeglib
//	based on default ones,
//	modified to work on BPositionIO instead of FILE
//---------------------------------------------------
EXTERN(void) be_jpeg_stdio_src(j_decompress_ptr cinfo, BPositionIO *infile);	// from "be_jdatasrc.cpp"
EXTERN(void) be_jpeg_stdio_dest(j_compress_ptr cinfo, BPositionIO *outfile);	// from "be_jdatadst.cpp"

//---------------------------------------------------
//	Error output functions
//	based on the one from jerror.c
//	modified to use settings
//	(so user can decide to show dialog-boxes or not)
//---------------------------------------------------
EXTERN(struct jpeg_error_mgr *) be_jpeg_std_error (struct jpeg_error_mgr * err,
	jpeg_settings * settings, const jmp_buf* longJumpBuffer);
	// implemented in "be_jerror.cpp"

#endif // _JPEGTRANSLATOR_H_
