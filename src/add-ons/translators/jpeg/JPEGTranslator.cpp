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


#include "JPEGTranslator.h"

#include "exif_parser.h"

#include <TabView.h>


#define MARKER_EXIF	0xe1

// Set these accordingly
#define JPEG_ACRONYM "JPEG"
#define JPEG_FORMAT 'JPEG'
#define JPEG_MIME_STRING "image/jpeg"
#define JPEG_DESCRIPTION "JPEG image"

// The translation kit's native file type
#define B_TRANSLATOR_BITMAP_MIME_STRING "image/x-be-bitmap"
#define B_TRANSLATOR_BITMAP_DESCRIPTION "Be Bitmap Format (JPEGTranslator)"

// Translation Kit required globals
char translatorName[] = "JPEG Images";
char translatorInfo[] =
	"©2002-2003, Marcin Konicki\n"
	"©2005-2007, Haiku\n"
	"\n"
	"Based on IJG library © 1991-1998, Thomas G. Lane\n"
	"          http://www.ijg.org/files/\n"
	"with \"Lossless\" encoding support patch by Ken Murchison\n"
	"          http://www.oceana.com/ftp/ljpeg/\n"
	"\n"
	"With some colorspace conversion routines by Magnus Hellman\n"
	"          http://www.bebits.com/app/802\n";

int32 translatorVersion = 0x120;

// Define the formats we know how to read
translation_format inputFormats[] = {
	{ JPEG_FORMAT, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		JPEG_MIME_STRING, JPEG_DESCRIPTION },
	{ B_TRANSLATOR_BITMAP, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		B_TRANSLATOR_BITMAP_MIME_STRING, B_TRANSLATOR_BITMAP_DESCRIPTION },
	{}
};

// Define the formats we know how to write
translation_format outputFormats[] = {
	{ JPEG_FORMAT, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		JPEG_MIME_STRING, JPEG_DESCRIPTION },
	{ B_TRANSLATOR_BITMAP, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		B_TRANSLATOR_BITMAP_MIME_STRING, B_TRANSLATOR_BITMAP_DESCRIPTION },
	{}
};

// Main functions of translator :)
static status_t Copy(BPositionIO *in, BPositionIO *out);
static status_t Compress(BPositionIO *in, BPositionIO *out,
	const jmp_buf* longJumpBuffer);
static status_t Decompress(BPositionIO *in, BPositionIO *out,
	BMessage* ioExtension, const jmp_buf* longJumpBuffer);
static status_t Error(j_common_ptr cinfo, status_t error = B_ERROR);


bool gAreSettingsRunning = false;


//!	Make settings to defaults
void
LoadDefaultSettings(jpeg_settings *settings)
{
	settings->Smoothing = 0;
	settings->Quality = 95;
	settings->Progressive = true;
	settings->OptimizeColors = true;
	settings->SmallerFile = false;
	settings->B_GRAY1_as_B_RGB24 = false;
	settings->Always_B_RGB32 = true;
	settings->PhotoshopCMYK = true;
	settings->ShowReadWarningBox = true;
}


//!	Save settings to config file
void
SaveSettings(jpeg_settings *settings)
{
	// Make path to settings file
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return;

	path.Append(SETTINGS_FILE);

	// Open settings file (create it if there's no file) and write settings			
	FILE *file = NULL;
	if ((file = fopen(path.Path(), "wb+"))) {
		fwrite(settings, sizeof(jpeg_settings), 1, file);
		fclose(file);
	}
}


/*!
	Load settings from config file
	If can't find it make them default and try to save
*/
void
LoadSettings(jpeg_settings *settings)
{
	// Make path to settings file
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) {
		LoadDefaultSettings(settings);
		return;
	}

	path.Append(SETTINGS_FILE);

	// Open settings file (create it if there's no file) and write settings			
	FILE *file = NULL;
	if ((file = fopen(path.Path(), "rb")) != NULL) {
		if (!fread(settings, sizeof(jpeg_settings), 1, file)) {
			// settings struct has changed size
			// Load default settings, and Save them
			fclose(file);
			LoadDefaultSettings(settings);
			SaveSettings(settings);
		} else
			fclose(file);
	} else if ((file = fopen(path.Path(), "wb+")) != NULL) {
		LoadDefaultSettings(settings);
		fwrite(settings, sizeof(jpeg_settings), 1, file);
		fclose(file);
	}
}


static bool
x_flipped(int32 orientation)
{
	return orientation == 2 || orientation == 3
		|| orientation == 6 || orientation == 7;
}


static bool
y_flipped(int32 orientation)
{
	return orientation == 3 || orientation == 4
		|| orientation == 7 || orientation == 8;
}


static int32
dest_index(uint32 width, uint32 height, uint32 x, uint32 y, int32 orientation)
{
	if (orientation > 4) {
		uint32 temp = x;
		x = y;
		y = temp;
	}
	if (y_flipped(orientation))
		y = height - 1 - y;
	if (x_flipped(orientation))
		x = width - 1 - x;

	return y * width + x;
}


//	#pragma mark - conversion for compression


inline void
convert_from_gray1_to_gray8(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	while (index < inRowBytes) {
		unsigned char c = in[index++];
		for (int b = 128; b; b = b>>1) {
			unsigned char color;
			if (c & b)
				color = 0;
			else
				color = 255;
			out[index2++] = color;
		}
	}
}


inline void
convert_from_gray1_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	while (index < inRowBytes) {
		unsigned char c = in[index++];
		for (int b = 128; b; b = b>>1) {
			unsigned char color;
			if (c & b)
				color = 0;
			else
				color = 255;
			out[index2++] = color;
			out[index2++] = color;
			out[index2++] = color;
		}
	}
}


inline void
convert_from_cmap8_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	const color_map * map = system_colors();
	int32 index = 0;
	int32 index2 = 0;
	while (index < inRowBytes) {
		rgb_color color = map->color_list[in[index++]];
		
		out[index2++] = color.red;
		out[index2++] = color.green;
		out[index2++] = color.blue;
	}
}


inline void
convert_from_15_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	int16 in_pixel;
	while (index < inRowBytes) {
		in_pixel = in[index] | (in[index+1] << 8);
		index += 2;
		
		out[index2++] = (((in_pixel & 0x7c00)) >> 7) | (((in_pixel & 0x7c00)) >> 12);
		out[index2++] = (((in_pixel & 0x3e0)) >> 2) | (((in_pixel & 0x3e0)) >> 7);
		out[index2++] = (((in_pixel & 0x1f)) << 3) | (((in_pixel & 0x1f)) >> 2);
	}
}


inline void
convert_from_15b_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	int16 in_pixel;
	while (index < inRowBytes) {
		in_pixel = in[index+1] | (in[index] << 8);
		index += 2;
		
		out[index2++] = (((in_pixel & 0x7c00)) >> 7) | (((in_pixel & 0x7c00)) >> 12);
		out[index2++] = (((in_pixel & 0x3e0)) >> 2) | (((in_pixel & 0x3e0)) >> 7);
		out[index2++] = (((in_pixel & 0x1f)) << 3) | (((in_pixel & 0x1f)) >> 2);
	}
}


inline void
convert_from_16_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	int16 in_pixel;
	while (index < inRowBytes) {
		in_pixel = in[index] | (in[index+1] << 8);
		index += 2;
		
		out[index2++] = (((in_pixel & 0xf800)) >> 8) | (((in_pixel & 0xf800)) >> 13);
		out[index2++] = (((in_pixel & 0x7e0)) >> 3) | (((in_pixel & 0x7e0)) >> 9);
		out[index2++] = (((in_pixel & 0x1f)) << 3) | (((in_pixel & 0x1f)) >> 2);
	}
}


inline void
convert_from_16b_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	int16 in_pixel;
	while (index < inRowBytes) {
		in_pixel = in[index+1] | (in[index] << 8);
		index += 2;
		
		out[index2++] = (((in_pixel & 0xf800)) >> 8) | (((in_pixel & 0xf800)) >> 13);
		out[index2++] = (((in_pixel & 0x7e0)) >> 3) | (((in_pixel & 0x7e0)) >> 9);
		out[index2++] = (((in_pixel & 0x1f)) << 3) | (((in_pixel & 0x1f)) >> 2);
	}
}


inline void
convert_from_24_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	int32 index = 0;
	int32 index2 = 0;
	while (index < inRowBytes) {
		out[index2++] = in[index+2];
		out[index2++] = in[index+1];
		out[index2++] = in[index];
		index+=3;
	}
}


inline void
convert_from_32_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	inRowBytes /= 4;

	for (int32 i = 0; i < inRowBytes; i++) {
		out[0] = in[2];
		out[1] = in[1];
		out[2] = in[0];

		in += 4;
		out += 3;
	}
}


inline void
convert_from_32b_to_24(uint8* in, uint8* out, int32 inRowBytes)
{
	inRowBytes /= 4;

	for (int32 i = 0; i < inRowBytes; i++) {
		out[0] = in[1];
		out[1] = in[2];
		out[2] = in[3];

		in += 4;
		out += 3;
	}
}


//	#pragma mark - conversion for decompression


inline void
convert_from_CMYK_to_32_photoshop(uint8* in, uint8* out, int32 inRowBytes, int32 xStep)
{
	for (int32 i = 0; i < inRowBytes; i += 4) {
		int32 black = in[3];
		out[0] = in[2] * black / 255;
		out[1] = in[1] * black / 255;
		out[2] = in[0] * black / 255;
		out[3] = 255;

		in += 4;
		out += xStep;
	}
}


//!	!!! UNTESTED !!!
inline void
convert_from_CMYK_to_32(uint8* in, uint8* out, int32 inRowBytes, int32 xStep)
{
	for (int32 i = 0; i < inRowBytes; i += 4) {
		int32 black = 255 - in[3];
		out[0] = ((255 - in[2]) * black) / 255;
		out[1] = ((255 - in[1]) * black) / 255;
		out[2] = ((255 - in[0]) * black) / 255;
		out[3] = 255;

		in += 4;
		out += xStep;
	}
}


//!	RGB24 8:8:8 to xRGB 8:8:8:8
inline void
convert_from_24_to_32(uint8* in, uint8* out, int32 inRowBytes, int32 xStep)
{
	for (int32 i = 0; i < inRowBytes; i += 3) {
		out[0] = in[2];
		out[1] = in[1];
		out[2] = in[0];
		out[3] = 255;

		in += 3;
		out += xStep;
	}
}


//! 8-bit to 8-bit, only need when rotating the image
void
translate_8(uint8* in, uint8* out, int32 inRowBytes, int32 xStep)
{
	for (int32 i = 0; i < inRowBytes; i++) {
		out[0] = in[0];

		in++;
		out += xStep;
	}
}


//	#pragma mark - SView


SView::SView(BRect frame, const char *name)
	: BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetLowColor(ViewColor());
}


void
SView::AttachedToWindow()
{
	BView::AttachedToWindow();
	ResizeTo(Parent()->Bounds().Width(), Parent()->Bounds().Height());
}


//	#pragma mark -


SSlider::SSlider(BRect frame, const char *name, const char *label,
		BMessage *message, int32 minValue, int32 maxValue, orientation posture,
		thumb_style thumbType, uint32 resizingMode, uint32 flags)
	: BSlider(frame, name, label, message, minValue, maxValue,
		posture, thumbType, resizingMode, flags)
{
	rgb_color barColor = { 0, 0, 229, 255 };
	UseFillColor(true, &barColor);
}


//!	Update status string - show actual value
const char*
SSlider::UpdateText() const
{
	snprintf(fStatusLabel, sizeof(fStatusLabel), "%ld", Value());
	return fStatusLabel;
}


//!	BSlider::ResizeToPreferred + Resize width if it's too small to show label and status
void
SSlider::ResizeToPreferred()
{
	int32 width = (int32)ceil(StringWidth(Label()) + StringWidth("9999"));
	if (width < 230)
		width = 230;

	float w, h;
	GetPreferredSize(&w, &h);
	ResizeTo(width, h);
}


//	#pragma mark -


TranslatorReadView::TranslatorReadView(BRect frame, const char *name,
	jpeg_settings *settings)
	: SView(frame, name),
	fSettings(settings)
{
	BRect rect(5, 5, 30, 30);
	fAlwaysRGB32 = new BCheckBox(rect, "alwaysrgb32", VIEW_LABEL_ALWAYSRGB32,
		new BMessage(VIEW_MSG_SET_ALWAYSRGB32));
	fAlwaysRGB32->SetFont(be_plain_font);
	if (fSettings->Always_B_RGB32)
		fAlwaysRGB32->SetValue(1);

	AddChild(fAlwaysRGB32);
	fAlwaysRGB32->ResizeToPreferred();
	rect.OffsetBy(0, fAlwaysRGB32->Bounds().Height() + 5);
	
	fPhotoshopCMYK = new BCheckBox(rect, "photoshopCMYK", VIEW_LABEL_PHOTOSHOPCMYK,
		new BMessage(VIEW_MSG_SET_PHOTOSHOPCMYK));
	fPhotoshopCMYK->SetFont(be_plain_font);
	if (fSettings->PhotoshopCMYK)
		fPhotoshopCMYK->SetValue(1);

	AddChild(fPhotoshopCMYK);
	fPhotoshopCMYK->ResizeToPreferred();
	rect.OffsetBy(0, fPhotoshopCMYK->Bounds().Height() + 5);
	
	fShowErrorBox = new BCheckBox(rect, "error", VIEW_LABEL_SHOWREADERRORBOX,
		new BMessage(VIEW_MSG_SET_SHOWREADERRORBOX));
	fShowErrorBox->SetFont(be_plain_font);
	if (fSettings->ShowReadWarningBox)
		fShowErrorBox->SetValue(1);

	AddChild(fShowErrorBox);

	fShowErrorBox->ResizeToPreferred();
}


void
TranslatorReadView::AttachedToWindow()
{
	SView::AttachedToWindow();
	
	fAlwaysRGB32->SetTarget(this);
	fPhotoshopCMYK->SetTarget(this);
	fShowErrorBox->SetTarget(this);
}


void
TranslatorReadView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case VIEW_MSG_SET_ALWAYSRGB32:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->Always_B_RGB32 = value;
				SaveSettings(fSettings);
			}
			break;
		}
		case VIEW_MSG_SET_PHOTOSHOPCMYK:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->PhotoshopCMYK = value;
				SaveSettings(fSettings);
			}
			break;
		}
		case VIEW_MSG_SET_SHOWREADERRORBOX:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->ShowReadWarningBox = value;
				SaveSettings(fSettings);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark - TranslatorWriteView


TranslatorWriteView::TranslatorWriteView(BRect frame, const char *name,
	jpeg_settings *settings)
	: SView(frame, name),
	fSettings(settings)
{
	BRect rect(10, 10, 20, 30);
	fQualitySlider = new SSlider(rect, "quality", VIEW_LABEL_QUALITY,
		new BMessage(VIEW_MSG_SET_QUALITY), 0, 100);
	fQualitySlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fQualitySlider->SetHashMarkCount(10);
	fQualitySlider->SetLimitLabels("Low", "High");
	fQualitySlider->SetFont(be_plain_font);
	fQualitySlider->SetValue(fSettings->Quality);
	AddChild(fQualitySlider);
	fQualitySlider->ResizeToPreferred();

	rect.OffsetBy(0, fQualitySlider->Bounds().Height() + 5);
	
	fSmoothingSlider = new SSlider(rect, "smoothing", VIEW_LABEL_SMOOTHING,
		new BMessage(VIEW_MSG_SET_SMOOTHING), 0, 100);
	fSmoothingSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fSmoothingSlider->SetHashMarkCount(10);
	fSmoothingSlider->SetLimitLabels("None", "High");
	fSmoothingSlider->SetFont(be_plain_font);
	fSmoothingSlider->SetValue(fSettings->Smoothing);
	AddChild(fSmoothingSlider);
	fSmoothingSlider->ResizeToPreferred();

	rect.OffsetBy(0, fSmoothingSlider->Bounds().Height() + 5);
	
	fProgress = new BCheckBox(rect, "progress", VIEW_LABEL_PROGRESSIVE,
		new BMessage(VIEW_MSG_SET_PROGRESSIVE));
	fProgress->SetFont(be_plain_font);
	if (fSettings->Progressive)
		fProgress->SetValue(1);

	AddChild(fProgress);
	fProgress->ResizeToPreferred();
	
	rect.OffsetBy(0, fProgress->Bounds().Height() + 5);
	
	fOptimizeColors = new BCheckBox(rect, "optimizecolors", VIEW_LABEL_OPTIMIZECOLORS,
		new BMessage(VIEW_MSG_SET_OPTIMIZECOLORS));
	fOptimizeColors->SetFont(be_plain_font);
	if (fSettings->OptimizeColors)
		fOptimizeColors->SetValue(1);

	AddChild(fOptimizeColors);
	fOptimizeColors->ResizeToPreferred();
	rect.OffsetBy(0, fOptimizeColors->Bounds().Height() + 5);
	
	fSmallerFile = new BCheckBox(rect, "smallerfile", VIEW_LABEL_SMALLERFILE,
		new BMessage(VIEW_MSG_SET_SMALLERFILE));
	fSmallerFile->SetFont(be_plain_font);
	if (fSettings->SmallerFile)
		fSmallerFile->SetValue(1);
	if (!fSettings->OptimizeColors)
		fSmallerFile->SetEnabled(false);

	AddChild(fSmallerFile);
	fSmallerFile->ResizeToPreferred();
	rect.OffsetBy(0, fSmallerFile->Bounds().Height() + 5);
	
	fGrayAsRGB24 = new BCheckBox(rect, "gray1asrgb24", VIEW_LABEL_GRAY1ASRGB24,
		new BMessage(VIEW_MSG_SET_GRAY1ASRGB24));
	fGrayAsRGB24->SetFont(be_plain_font);
	if (fSettings->B_GRAY1_as_B_RGB24)
		fGrayAsRGB24->SetValue(1);

	AddChild(fGrayAsRGB24);

	fGrayAsRGB24->ResizeToPreferred();
}


void
TranslatorWriteView::AttachedToWindow()
{
	SView::AttachedToWindow();
	
	fQualitySlider->SetTarget(this);
	fSmoothingSlider->SetTarget(this);
	fProgress->SetTarget(this);
	fOptimizeColors->SetTarget(this);
	fSmallerFile->SetTarget(this);
	fGrayAsRGB24->SetTarget(this);
}


void
TranslatorWriteView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case VIEW_MSG_SET_QUALITY:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->Quality = value;
				SaveSettings(fSettings);
			}
			break;
		}
		case VIEW_MSG_SET_SMOOTHING:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->Smoothing = value;
				SaveSettings(fSettings);
			}
			break;
		}
		case VIEW_MSG_SET_PROGRESSIVE:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->Progressive = value;
				SaveSettings(fSettings);
			}
			break;
		}
		case VIEW_MSG_SET_OPTIMIZECOLORS:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->OptimizeColors = value;
				SaveSettings(fSettings);
			}
			fSmallerFile->SetEnabled(fSettings->OptimizeColors);
			break;
		}
		case VIEW_MSG_SET_SMALLERFILE:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->SmallerFile = value;
				SaveSettings(fSettings);
			}
			break;
		}
		case VIEW_MSG_SET_GRAY1ASRGB24:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				fSettings->B_GRAY1_as_B_RGB24 = value;
				SaveSettings(fSettings);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark -


TranslatorAboutView::TranslatorAboutView(BRect frame, const char *name)
	: SView(frame, name)
{
	BStringView *title = new BStringView(BRect(10, 0, 10, 0), "Title",
		translatorName);
	title->SetFont(be_bold_font);

	AddChild(title);
	title->ResizeToPreferred();

	BRect rect = title->Bounds();
	float space = title->StringWidth("    ");

	char versionString[16];
	sprintf(versionString, "v%d.%d.%d", (int)(translatorVersion >> 8),
		(int)((translatorVersion >> 4) & 0xf), (int)(translatorVersion & 0xf));

	BStringView *version = new BStringView(BRect(rect.right + space, rect.top,
		rect.right+space, rect.top), "Version", versionString);
	version->SetFont(be_plain_font);
	version->SetFontSize(9);
	// Make version be in the same line as title
	version->ResizeToPreferred();
	version->MoveBy(0, rect.bottom-version->Frame().bottom);

	AddChild(version);

	// Now for each line in translatorInfo add a BStringView
	char* current = translatorInfo;
	int32 index = 1;
	BRect stringFrame = title->Frame();
	while (current != NULL && current[0]) {
		char text[128];
		char* newLine = strchr(current, '\n');
		if (newLine == NULL) {
			strlcpy(text, current, sizeof(text));
			current = NULL;
		} else {
			strlcpy(text, current, min_c((int32)sizeof(text), newLine + 1 - current));
			current = newLine + 1;
		}
		stringFrame.OffsetBy(0, stringFrame.Height() + 2);
		BStringView* string = new BStringView(stringFrame, "copyright", text);
		if (index > 3)
			string->SetFontSize(9);
		AddChild(string);
		string->ResizeToPreferred();

		index++;
	}

	ResizeToPreferred();
}


//	#pragma mark -


TranslatorView::TranslatorView(BRect frame, const char *name)
	: BTabView(frame, name)
{
	// Set global var to true
	gAreSettingsRunning = true;

	// Load settings to global settings struct
	LoadSettings(&fSettings);

	BRect contentSize = ContainerView()->Bounds();
	SView *view = new TranslatorWriteView(contentSize, "Write", &fSettings);
	AddTab(view);
	view = new TranslatorReadView(contentSize, "Read", &fSettings);
	AddTab(view);
	view = new TranslatorAboutView(contentSize, "About");
	AddTab(view);

	ResizeToPreferred();

	// Make TranslatorView resize itself with parent
	SetFlags(Flags() | B_FOLLOW_ALL);
}


TranslatorView::~TranslatorView()
{
	gAreSettingsRunning = false;
}


//!	Attached to window - resize parent to preferred
void
TranslatorView::AttachedToWindow()
{
	BTabView::AttachedToWindow();
}


void
TranslatorView::Select(int32 index)
{
	BTabView::Select(index);
}


//	#pragma mark -


TranslatorWindow::TranslatorWindow(bool quitOnClose)
	: BWindow(BRect(100, 100, 100, 100), "JPEG Settings", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
{
	BRect extent(0, 0, 0, 0);
	BView *config = NULL;
	MakeConfig(NULL, &config, &extent);

	AddChild(config);
	ResizeTo(extent.Width(), extent.Height());

	// Make application quit after this window close
	if (quitOnClose)
		SetFlags(Flags() | B_QUIT_ON_WINDOW_CLOSE);
}


//	#pragma mark - Translator Add-On



/*! Hook to create and return our configuration view */
status_t
MakeConfig(BMessage *ioExtension, BView **outView, BRect *outExtent)
{
	*outView = new TranslatorView(BRect(0, 0, 320, 300), "TranslatorView");
	*outExtent = (*outView)->Frame();
	return B_OK;
}

/*! Determine whether or not we can handle this data */
status_t
Identify(BPositionIO *inSource, const translation_format *inFormat,
	BMessage *ioExtension, translator_info *outInfo, uint32 outType)
{
	if (outType != 0 && outType != B_TRANSLATOR_BITMAP && outType != JPEG_FORMAT)
		return B_NO_TRANSLATOR;

	// !!! You might need to make this buffer bigger to test for your native format
	off_t position = inSource->Position();
	char header[sizeof(TranslatorBitmap)];
	status_t err = inSource->Read(header, sizeof(TranslatorBitmap));
	inSource->Seek(position, SEEK_SET);
	if (err < B_OK)
		return err;

	if (B_BENDIAN_TO_HOST_INT32(((TranslatorBitmap *)header)->magic) == B_TRANSLATOR_BITMAP) {
		outInfo->type = inputFormats[1].type;
		outInfo->translator = 0;
		outInfo->group = inputFormats[1].group;
		outInfo->quality = inputFormats[1].quality;
		outInfo->capability = inputFormats[1].capability;
		strcpy(outInfo->name, inputFormats[1].name);
		strcpy(outInfo->MIME, inputFormats[1].MIME);
	} else {
		// First 3 bytes in jpg files are always the same from what i've seen so far
		// check them
		if (header[0] == (char)0xff && header[1] == (char)0xd8 && header[2] == (char)0xff) {
		/* this below would be safer but it slows down whole thing
		
			struct jpeg_decompress_struct cinfo;
			struct jpeg_error_mgr jerr;
			cinfo.err = jpeg_std_error(&jerr);
			jpeg_create_decompress(&cinfo);
			be_jpeg_stdio_src(&cinfo, inSource);
			// now try to read header
			// it can't be read before checking first 3 bytes
			// because it will hang up if there is no header (not jpeg file)
			int result = jpeg_read_header(&cinfo, FALSE);
			jpeg_destroy_decompress(&cinfo);
			if (result == JPEG_HEADER_OK) {
		*/		outInfo->type = inputFormats[0].type;
				outInfo->translator = 0;
				outInfo->group = inputFormats[0].group;
				outInfo->quality = inputFormats[0].quality;
				outInfo->capability = inputFormats[0].capability;
				strcpy(outInfo->name, inputFormats[0].name);
				strcpy(outInfo->MIME, inputFormats[0].MIME);
				return B_OK;
		/*	} else
				return B_NO_TRANSLATOR;
		*/
		} else
			return B_NO_TRANSLATOR;
	}

	return B_OK;
}

/*!	Arguably the most important method in the add-on */
status_t
Translate(BPositionIO *inSource, const translator_info *inInfo,
	BMessage *ioExtension, uint32 outType, BPositionIO *outDestination)
{
	// If no specific type was requested, convert to the interchange format
	if (outType == 0)
		outType = B_TRANSLATOR_BITMAP;

	// Setup a "breakpoint" since throwing exceptions does not seem to work
	// at all in an add-on. (?)
	// In the be_jerror.cpp we implement a handler for critical library errors
	// (be_error_exit()) and there we use the longjmp() function to return to
	// this place. If this happens, it is as if the setjmp() call is called
	// a second time, but this time the return value will be 1. The first
	// invokation will return 0.
	jmp_buf longJumpBuffer;
	int jmpRet = setjmp(longJumpBuffer);
	if (jmpRet == 1)
		return B_ERROR;

	try {
		// What action to take, based on the findings of Identify()
		if (outType == inInfo->type) {
			return Copy(inSource, outDestination);
		} else if (inInfo->type == B_TRANSLATOR_BITMAP
				&& outType == JPEG_FORMAT) {
			return Compress(inSource, outDestination, &longJumpBuffer);
		} else if (inInfo->type == JPEG_FORMAT
				&& outType == B_TRANSLATOR_BITMAP) {
			return Decompress(inSource, outDestination, ioExtension,
				&longJumpBuffer);
		}
	} catch (...) {
		fprintf(stderr, "libjpeg encoutered a critical error "
			"(caught C++ exception).\n");
		return B_ERROR;
	}

	return B_NO_TRANSLATOR;
}

/*!	The user has requested the same format for input and output, so just copy */
static status_t
Copy(BPositionIO *in, BPositionIO *out)
{
	int block_size = 65536;
	void *buffer = malloc(block_size);
	char temp[1024];
	if (buffer == NULL) {
		buffer = temp;
		block_size = 1024;
	}
	status_t err = B_OK;
	
	// Read until end of file or error
	while (1) {
		ssize_t to_read = block_size;
		err = in->Read(buffer, to_read);
		// Explicit check for EOF
		if (err == -1) {
			if (buffer != temp) free(buffer);
			return B_OK;
		}
		if (err <= B_OK) break;
		to_read = err;
		err = out->Write(buffer, to_read);
		if (err != to_read) if (err >= 0) err = B_DEVICE_FULL;
		if (err < B_OK) break;
	}
	
	if (buffer != temp) free(buffer);
	return (err >= 0) ? B_OK : err;
}


/*!	Encode into the native format */
static status_t
Compress(BPositionIO *in, BPositionIO *out, const jmp_buf* longJumpBuffer)
{
	// Load Settings
	jpeg_settings settings;
	LoadSettings(&settings);

	// Read info about bitmap
	TranslatorBitmap header;
	status_t err = in->Read(&header, sizeof(TranslatorBitmap));
	if (err < B_OK)
		return err;
	else if (err < (int)sizeof(TranslatorBitmap))
		return B_ERROR;

	// Grab dimension, color space, and size information from the stream
	BRect bounds;
	bounds.left = B_BENDIAN_TO_HOST_FLOAT(header.bounds.left);
	bounds.top = B_BENDIAN_TO_HOST_FLOAT(header.bounds.top);
	bounds.right = B_BENDIAN_TO_HOST_FLOAT(header.bounds.right);
	bounds.bottom = B_BENDIAN_TO_HOST_FLOAT(header.bounds.bottom);

	int32 in_row_bytes = B_BENDIAN_TO_HOST_INT32(header.rowBytes);

	int width = bounds.IntegerWidth() + 1;
	int height = bounds.IntegerHeight() + 1;

	// Function pointer to convert function
	// It will point to proper function if needed
	void (*converter)(uchar *inscanline, uchar *outscanline,
		int32 inRowBytes) = NULL;

	// Default color info
	J_COLOR_SPACE jpg_color_space = JCS_RGB;
	int jpg_input_components = 3;
	int32 out_row_bytes;
	int padding = 0;

	switch ((color_space)B_BENDIAN_TO_HOST_INT32(header.colors)) {
		case B_CMAP8:
			converter = convert_from_cmap8_to_24;
			padding = in_row_bytes - width;
			break;

		case B_GRAY1:
			if (settings.B_GRAY1_as_B_RGB24) {
				converter = convert_from_gray1_to_24;
			} else {
				jpg_input_components = 1;
				jpg_color_space = JCS_GRAYSCALE;
				converter = convert_from_gray1_to_gray8;
			}
			padding = in_row_bytes - (width/8);
			break;

		case B_GRAY8:
			jpg_input_components = 1;
			jpg_color_space = JCS_GRAYSCALE;
			padding = in_row_bytes - width;
			break;

		case B_RGB15:
		case B_RGBA15:
			converter = convert_from_15_to_24;
			padding = in_row_bytes - (width * 2);
			break;

		case B_RGB15_BIG:
		case B_RGBA15_BIG:
			converter = convert_from_15b_to_24;
			padding = in_row_bytes - (width * 2);
			break;

		case B_RGB16:
			converter = convert_from_16_to_24;
			padding = in_row_bytes - (width * 2);
			break;

		case B_RGB16_BIG:
			converter = convert_from_16b_to_24;
			padding = in_row_bytes - (width * 2);
			break;

		case B_RGB24:
			converter = convert_from_24_to_24;
			padding = in_row_bytes - (width * 3);
			break;

		case B_RGB24_BIG:
			padding = in_row_bytes - (width * 3);
			break;

		case B_RGB32:
		case B_RGBA32:
			converter = convert_from_32_to_24;
			padding = in_row_bytes - (width * 4);
			break;

		case B_RGB32_BIG:
		case B_RGBA32_BIG:
			converter = convert_from_32b_to_24;
			padding = in_row_bytes - (width * 4);
			break;

		case B_CMYK32:
			jpg_color_space = JCS_CMYK;
			jpg_input_components = 4;
			padding = in_row_bytes - (width * 4);
			break;

		default:
			fprintf(stderr, "Wrong type: Color space not implemented.\n");
			return B_ERROR;
	}
	out_row_bytes = jpg_input_components * width;

	// Set basic things needed for jpeg writing
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = be_jpeg_std_error(&jerr, &settings, longJumpBuffer);
	jpeg_create_compress(&cinfo);
	be_jpeg_stdio_dest(&cinfo, out);

	// Set basic values
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = jpg_input_components;
	cinfo.in_color_space = jpg_color_space;
	jpeg_set_defaults(&cinfo);

	// Set better accuracy
	cinfo.dct_method = JDCT_ISLOW;

	// This is needed to prevent some colors loss
	// With it generated jpegs are as good as from Fireworks (at last! :D)
	if (settings.OptimizeColors) {
		int index = 0;
		while (index < cinfo.num_components) {
			cinfo.comp_info[index].h_samp_factor = 1;
			cinfo.comp_info[index].v_samp_factor = 1;
			// This will make file smaller, but with worse quality more or less
			// like with 93%-94% (but it's subjective opinion) on tested images
			// but with smaller size (between 92% and 93% on tested images)
			if (settings.SmallerFile)
				cinfo.comp_info[index].quant_tbl_no = 1;
			// This will make bigger file, but also better quality ;]
			// from my tests it seems like useless - better quality with smaller
			// can be acheived without this
//			cinfo.comp_info[index].dc_tbl_no = 1;
//			cinfo.comp_info[index].ac_tbl_no = 1;
			index++;
		}
	}

	// Set quality
	jpeg_set_quality(&cinfo, settings.Quality, true);

	// Set progressive compression if needed
	// if not, turn on optimizing in libjpeg
	if (settings.Progressive)
		jpeg_simple_progression(&cinfo);
	else
		cinfo.optimize_coding = TRUE;

	// Set smoothing (effect like Blur)
	cinfo.smoothing_factor = settings.Smoothing;

	// Initialize compression
	jpeg_start_compress(&cinfo, TRUE);

	// Declare scanlines
	JSAMPROW in_scanline = NULL;
	JSAMPROW out_scanline = NULL;
	JSAMPROW writeline;	// Pointer to in_scanline (default) or out_scanline (if there will be conversion)

	// Allocate scanline
	// Use libjpeg memory allocation functions, so in case of error it will free them itself
	in_scanline = (unsigned char *)(cinfo.mem->alloc_large)((j_common_ptr)&cinfo,
		JPOOL_PERMANENT, in_row_bytes);

	// We need 2nd scanline storage ony for conversion
	if (converter != NULL) {
		// There will be conversion, allocate second scanline...
		// Use libjpeg memory allocation functions, so in case of error it will free them itself
	    out_scanline = (unsigned char *)(cinfo.mem->alloc_large)((j_common_ptr)&cinfo,
	    	JPOOL_PERMANENT, out_row_bytes);
		// ... and make it the one to write to file
		writeline = out_scanline;
	} else
		writeline = in_scanline;

	while (cinfo.next_scanline < cinfo.image_height) {
		// Read scanline
		err = in->Read(in_scanline, in_row_bytes);
		if (err < in_row_bytes)
			return err < B_OK ? Error((j_common_ptr)&cinfo, err) 
				: Error((j_common_ptr)&cinfo, B_ERROR);

		// Convert if needed
		if (converter != NULL)
			converter(in_scanline, out_scanline, in_row_bytes - padding);

		// Write scanline
	   	jpeg_write_scanlines(&cinfo, &writeline, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	return B_OK;
}


/*!	Decode the native format */
static status_t
Decompress(BPositionIO *in, BPositionIO *out, BMessage* ioExtension,
	const jmp_buf* longJumpBuffer)
{
	// Load Settings
	jpeg_settings settings;
	LoadSettings(&settings);

	// Set basic things needed for jpeg reading
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = be_jpeg_std_error(&jerr, &settings, longJumpBuffer);
	jpeg_create_decompress(&cinfo);
	be_jpeg_stdio_src(&cinfo, in);

	jpeg_save_markers(&cinfo, MARKER_EXIF, 131072);
		// make sure the EXIF tag is stored

	// Read info about image
	jpeg_read_header(&cinfo, TRUE);

	BMessage exif;

	// parse EXIF data and add it ioExtension, if any
	jpeg_marker_struct* marker = cinfo.marker_list;
	while (marker != NULL) {
		if (marker->marker == MARKER_EXIF
			&& !strncmp((char*)marker->data, "Exif", 4)) {
			if (ioExtension != NULL) {
				// Strip EXIF header from TIFF data
				ioExtension->AddData("exif", B_RAW_TYPE,
					(uint8 *)marker->data + 6, marker->data_length - 6);
			}

			BMemoryIO io(marker->data + 6, marker->data_length - 6);
			convert_exif_to_message(io, exif);
		}
		marker = marker->next;
	}

	// Default color info
	color_space outColorSpace = B_RGB32;
	int outColorComponents = 4;

	// Function pointer to convert function
	// It will point to proper function if needed
	void (*converter)(uchar *inScanLine, uchar *outScanLine,
		int32 inRowBytes, int32 xStep) = convert_from_24_to_32;

	// If color space isn't rgb
	if (cinfo.out_color_space != JCS_RGB) {
		switch (cinfo.out_color_space) {
			case JCS_UNKNOWN:		/* error/unspecified */
				fprintf(stderr, "From Type: Jpeg uses unknown color type\n");
				break;
			case JCS_GRAYSCALE:		/* monochrome */
				// Check if user wants to read only as RGB32 or not
				if (!settings.Always_B_RGB32) {
					// Grayscale
					outColorSpace = B_GRAY8;
					outColorComponents = 1;
					converter = translate_8;
				} else {
					// RGB
					cinfo.out_color_space = JCS_RGB;
					cinfo.output_components = 3;
					converter = convert_from_24_to_32;
				}
				break;
			case JCS_YCbCr:		/* Y/Cb/Cr (also known as YUV) */
				cinfo.out_color_space = JCS_RGB;
				converter = convert_from_24_to_32;
				break;
			case JCS_YCCK:		/* Y/Cb/Cr/K */
				// Let libjpeg convert it to CMYK
				cinfo.out_color_space = JCS_CMYK;
				// Fall through to CMYK since we need the same settings
			case JCS_CMYK:		/* C/M/Y/K */
				// Use proper converter
				if (settings.PhotoshopCMYK)
					converter = convert_from_CMYK_to_32_photoshop;
				else
					converter = convert_from_CMYK_to_32;
				break;
			default:
				fprintf(stderr, "From Type: Jpeg uses hmm... i don't know really :(\n");
				break;
		}
	}

	// Initialize decompression
	jpeg_start_decompress(&cinfo);

	// retrieve orientation from settings/EXIF
	int32 orientation;
	if (ioExtension == NULL
		|| ioExtension->FindInt32("exif:orientation", &orientation) != B_OK) {
		if (exif.FindInt32("Orientation", &orientation) != B_OK)
			orientation = 1;
	}

	if (orientation != 1 && converter == NULL)
		converter = translate_8;

	int32 outputWidth = orientation > 4 ? cinfo.output_height : cinfo.output_width;
	int32 outputHeight = orientation > 4 ? cinfo.output_width : cinfo.output_height;

	int32 destOffset = dest_index(outputWidth, outputHeight,
		0, 0, orientation) * outColorComponents;
	int32 xStep = dest_index(outputWidth, outputHeight,
		1, 0, orientation) * outColorComponents - destOffset;
	int32 yStep = dest_index(outputWidth, outputHeight,
		0, 1, orientation) * outColorComponents - destOffset;
	bool needAll = orientation != 1;

	// Initialize this bounds rect to the size of your image
	BRect bounds(0, 0, outputWidth - 1, outputHeight - 1);

#if 0
printf("destOffset = %ld, xStep = %ld, yStep = %ld, input: %ld x %ld, output: %ld x %ld, orientation %ld\n",
	destOffset, xStep, yStep, (int32)cinfo.output_width, (int32)cinfo.output_height,
	bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1, orientation);
#endif

	// Bytes count in one line of image (scanline)
	int32 inRowBytes = cinfo.output_width * cinfo.output_components;
	int32 rowBytes = (bounds.IntegerWidth() + 1) * outColorComponents;
	int32 dataSize = cinfo.output_width * cinfo.output_height
		* outColorComponents;

	// Fill out the B_TRANSLATOR_BITMAP's header
	TranslatorBitmap header;
	header.magic = B_HOST_TO_BENDIAN_INT32(B_TRANSLATOR_BITMAP);
	header.bounds.left = B_HOST_TO_BENDIAN_FLOAT(bounds.left);
	header.bounds.top = B_HOST_TO_BENDIAN_FLOAT(bounds.top);
	header.bounds.right = B_HOST_TO_BENDIAN_FLOAT(bounds.right);
	header.bounds.bottom = B_HOST_TO_BENDIAN_FLOAT(bounds.bottom);
	header.colors = (color_space)B_HOST_TO_BENDIAN_INT32(outColorSpace);
	header.rowBytes = B_HOST_TO_BENDIAN_INT32(rowBytes);
	header.dataSize = B_HOST_TO_BENDIAN_INT32(dataSize);

	// Write out the header
	status_t err = out->Write(&header, sizeof(TranslatorBitmap));
	if (err < B_OK)
		return Error((j_common_ptr)&cinfo, err);
	else if (err < (int)sizeof(TranslatorBitmap))
		return Error((j_common_ptr)&cinfo, B_ERROR);

	// Declare scanlines
	JSAMPROW inScanLine = NULL;
	uint8* dest = NULL;
	uint8* destLine = NULL;

	// Allocate scanline
	// Use libjpeg memory allocation functions, so in case of error it will free them itself
    inScanLine = (unsigned char *)(cinfo.mem->alloc_large)((j_common_ptr)&cinfo,
    	JPOOL_PERMANENT, inRowBytes);

	// We need 2nd scanline storage only for conversion
	if (converter != NULL) {
		// There will be conversion, allocate second scanline...
		// Use libjpeg memory allocation functions, so in case of error it will free
		// them itself
	    dest = (uint8*)(cinfo.mem->alloc_large)((j_common_ptr)&cinfo,
	    	JPOOL_PERMANENT, needAll ? dataSize : rowBytes);
	    destLine = dest + destOffset;
	} else
		destLine = inScanLine;

	while (cinfo.output_scanline < cinfo.output_height) {
		// Read scanline
		jpeg_read_scanlines(&cinfo, &inScanLine, 1);

		// Convert if needed
		if (converter != NULL)
			converter(inScanLine, destLine, inRowBytes, xStep);

		if (!needAll) {
	  		// Write the scanline buffer to the output stream
			ssize_t bytesWritten = out->Write(destLine, rowBytes);
			if (bytesWritten < rowBytes) {
				return bytesWritten < B_OK
					? Error((j_common_ptr)&cinfo, bytesWritten)
					: Error((j_common_ptr)&cinfo, B_ERROR);
			}
		} else
			destLine += yStep;
	}

	if (needAll) {
		ssize_t bytesWritten = out->Write(dest, dataSize);
		if (bytesWritten < dataSize) {
			return bytesWritten < B_OK
				? Error((j_common_ptr)&cinfo, bytesWritten)
				: Error((j_common_ptr)&cinfo, B_ERROR);
		}
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return B_OK;
}

/*!
	Frees jpeg alocated memory
	Returns given error (B_ERROR by default)
*/
static status_t
Error(j_common_ptr cinfo, status_t error)
{
	jpeg_destroy(cinfo);
	return error;
}


//	#pragma mark -


int
main(int, char**)
{
	BApplication app("application/x-vnd.Haiku-JPEGTranslator");

	TranslatorWindow *window = new TranslatorWindow();
	window->Show();

	app.Run();
	return 0;
}

