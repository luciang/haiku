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

//----------------------------------------------------------------------------
//
//	Include
//
//----------------------------------------------------------------------------

#include "JPEGTranslator.h"


//----------------------------------------------------------------------------
//
//	Global variables initialization
//
//----------------------------------------------------------------------------

// Set these accordingly
#define JPEG_ACRONYM "JPEG"
#define JPEG_FORMAT 'JPEG'
#define JPEG_MIME_STRING "image/jpeg"
#define JPEG_DESCRIPTION "JPEG image"

// The translation kit's native file type
#define B_TRANSLATOR_BITMAP_MIME_STRING "image/x-be-bitmap"
#define B_TRANSLATOR_BITMAP_DESCRIPTION "Be Bitmap image"

// Translation Kit required globals
char translatorName[] = "JPEG translator";
char translatorInfo[] = "© 2002-2003, Shard

Based on IJG library © 1991-1998, Thomas G. Lane
          http://www.ijg.org/files/
with \"Lossless\" encoding support patch by Ken Murchison
          http://www.oceana.com/ftp/ljpeg/

With some colorspace conversion routines by Magnus Hellman
          http://www.bebits.com/app/802
";
int32 translatorVersion = 273;	// 256 = v1.0.0

// Define the formats we know how to read
translation_format inputFormats[] = {
	{ JPEG_FORMAT, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		JPEG_MIME_STRING, JPEG_DESCRIPTION },
	{ B_TRANSLATOR_BITMAP, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		B_TRANSLATOR_BITMAP_MIME_STRING, B_TRANSLATOR_BITMAP_DESCRIPTION },
	{ 0, 0, 0, 0, 0, 0 },
};

// Define the formats we know how to write
translation_format outputFormats[] = {
	{ JPEG_FORMAT, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		JPEG_MIME_STRING, JPEG_DESCRIPTION },
	{ B_TRANSLATOR_BITMAP, B_TRANSLATOR_BITMAP, 0.5, 0.5,
		B_TRANSLATOR_BITMAP_MIME_STRING, B_TRANSLATOR_BITMAP_DESCRIPTION },
	{ 0, 0, 0, 0, 0, 0 },
};

bool AreSettingsRunning = false;


//----------------------------------------------------------------------------
//
//	Functions :: SSlider
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	Constructor
//---------------------------------------------------
SSlider::SSlider(BRect frame, const char *name, const char *label, BMessage *message, int32 minValue, int32 maxValue, orientation posture, thumb_style thumbType, uint32 resizingMode, uint32 flags)
:	BSlider(frame, name, label, message, minValue, maxValue, posture, thumbType, resizingMode, flags)
{
	rgb_color bar_color = { 0, 0, 229, 255 };
	UseFillColor(true, &bar_color);
}

//---------------------------------------------------
//	Update status string - show actual value
//---------------------------------------------------
char*
SSlider::UpdateText() const
{
	sprintf( (char*)statusLabel, "%ld", Value());
	return (char*)statusLabel;
}

//---------------------------------------------------
//	BSlider::ResizeToPreferred + Resize width if it's too small to show label and status
//---------------------------------------------------
void
SSlider::ResizeToPreferred()
{
	int32 width = (int32)ceil(StringWidth( Label()) + StringWidth("9999"));
	if (width < 230) width = 230;
	float w, h;
	GetPreferredSize(&w, &h);
	ResizeTo(width, h);
}


//----------------------------------------------------------------------------
//
//	Functions :: TranslatorReadView
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	Constructor
//---------------------------------------------------
TranslatorReadView::TranslatorReadView(const char *name, SETTINGS *settings, float x, float y)
:	SView(name, x, y),
	Settings(settings)
{
	alwaysrgb32 = new BCheckBox( BRect(10, GetPreferredHeight(), 10, GetPreferredHeight()), "alwaysrgb32", VIEW_LABEL_ALWAYSRGB32, new BMessage(VIEW_MSG_SET_ALWAYSRGB32));
	alwaysrgb32->SetFont(be_plain_font);
	if (Settings->Always_B_RGB32)
		alwaysrgb32->SetValue(1);

	AddChild(alwaysrgb32);

	photoshopCMYK = new BCheckBox( BRect(10, GetPreferredHeight(), 10, GetPreferredHeight()), "photoshopCMYK", VIEW_LABEL_PHOTOSHOPCMYK, new BMessage(VIEW_MSG_SET_PHOTOSHOPCMYK));
	photoshopCMYK->SetFont(be_plain_font);
	if (Settings->PhotoshopCMYK)
		photoshopCMYK->SetValue(1);

	AddChild(photoshopCMYK);

	showerrorbox = new BCheckBox( BRect(10, GetPreferredHeight(), 10, GetPreferredHeight()), "progress", VIEW_LABEL_SHOWREADERRORBOX, new BMessage(VIEW_MSG_SET_SHOWREADERRORBOX));
	showerrorbox->SetFont(be_plain_font);
	if (Settings->ShowReadWarningBox)
		showerrorbox->SetValue(1);

	AddChild(showerrorbox);

	ResizeToPreferred();
}

//---------------------------------------------------
//	Attached to window - set children target
//---------------------------------------------------
void
TranslatorReadView::AttachedToWindow()
{
	alwaysrgb32->SetTarget(this);
	photoshopCMYK->SetTarget(this);
	showerrorbox->SetTarget(this);
}

//---------------------------------------------------
//	MessageReceived - receive GUI changes, save settings
//---------------------------------------------------
void
TranslatorReadView::MessageReceived(BMessage *message)
{
	switch (message->what)
	{
		case VIEW_MSG_SET_ALWAYSRGB32:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->Always_B_RGB32 = value;
				SaveSettings(Settings);
			}
			break;
		}
		case VIEW_MSG_SET_PHOTOSHOPCMYK:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->PhotoshopCMYK = value;
				SaveSettings(Settings);
			}
			break;
		}
		case VIEW_MSG_SET_SHOWREADERRORBOX:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->ShowReadWarningBox = value;
				SaveSettings(Settings);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


//----------------------------------------------------------------------------
//
//	Functions :: TranslatorWriteView
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	Constructor
//---------------------------------------------------
TranslatorWriteView::TranslatorWriteView(const char *name, SETTINGS *settings, float x, float y)
:	SView(name, x, y),
	Settings(settings)
{
	quality = new SSlider( BRect(10, GetPreferredHeight(), 10, GetPreferredHeight()), "quality", VIEW_LABEL_QUALITY, new BMessage(VIEW_MSG_SET_QUALITY), 0, 100);
	quality->SetHashMarks(B_HASH_MARKS_BOTTOM);
	quality->SetHashMarkCount(10);
	quality->SetLimitLabels("Low", "High");
	quality->SetFont(be_plain_font);
	quality->SetValue(Settings->Quality);

	AddChild(quality);

	smoothing = new SSlider( BRect(10, GetPreferredHeight()+10, 10, GetPreferredHeight()), "smoothing", VIEW_LABEL_SMOOTHING, new BMessage(VIEW_MSG_SET_SMOOTHING), 0, 100);
	smoothing->SetHashMarks(B_HASH_MARKS_BOTTOM);
	smoothing->SetHashMarkCount(10);
	smoothing->SetLimitLabels("None", "High");
	smoothing->SetFont(be_plain_font);
	smoothing->SetValue(Settings->Smoothing);

	AddChild(smoothing);

	progress = new BCheckBox( BRect(10, GetPreferredHeight()+10, 10, GetPreferredHeight()), "progress", VIEW_LABEL_PROGRESSIVE, new BMessage(VIEW_MSG_SET_PROGRESSIVE));
	progress->SetFont(be_plain_font);
	if (Settings->Progressive)
		progress->SetValue(1);

	AddChild(progress);

	optimizecolors = new BCheckBox( BRect(10, GetPreferredHeight()+5, 10, GetPreferredHeight()+5), "optimizecolors", VIEW_LABEL_OPTIMIZECOLORS, new BMessage(VIEW_MSG_SET_OPTIMIZECOLORS));
	optimizecolors->SetFont(be_plain_font);
	if (Settings->OptimizeColors)
		optimizecolors->SetValue(1);
	
	AddChild(optimizecolors);

	smallerfile = new BCheckBox( BRect(25, GetPreferredHeight()+5, 25, GetPreferredHeight()+5), "smallerfile", VIEW_LABEL_SMALLERFILE, new BMessage(VIEW_MSG_SET_SMALLERFILE));
	smallerfile->SetFont(be_plain_font);
	if (Settings->SmallerFile)
		smallerfile->SetValue(1);
	if (!Settings->OptimizeColors)
		smallerfile->SetEnabled(false);

	AddChild(smallerfile);

	gray1asrgb24 = new BCheckBox( BRect(10, GetPreferredHeight()+5, 25, GetPreferredHeight()+5), "gray1asrgb24", VIEW_LABEL_GRAY1ASRGB24, new BMessage(VIEW_MSG_SET_GRAY1ASRGB24));
	gray1asrgb24->SetFont(be_plain_font);
	if (Settings->B_GRAY1_as_B_RGB24)
		gray1asrgb24->SetValue(1);

	AddChild(gray1asrgb24);

	ResizeToPreferred();
}

//---------------------------------------------------
//	Attached to window - set children target
//---------------------------------------------------
void
TranslatorWriteView::AttachedToWindow()
{
	quality->SetTarget(this);
	smoothing->SetTarget(this);
	progress->SetTarget(this);
	optimizecolors->SetTarget(this);
	smallerfile->SetTarget(this);
	gray1asrgb24->SetTarget(this);
}

//---------------------------------------------------
//	MessageReceived - receive GUI changes, save settings
//---------------------------------------------------
void
TranslatorWriteView::MessageReceived(BMessage *message)
{
	switch (message->what)
	{
		case VIEW_MSG_SET_QUALITY:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->Quality = value;
				SaveSettings(Settings);
			}
			break;
		}
		case VIEW_MSG_SET_SMOOTHING:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->Smoothing = value;
				SaveSettings(Settings);
			}
			break;
		}
		case VIEW_MSG_SET_PROGRESSIVE:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->Progressive = value;
				SaveSettings(Settings);
			}
			break;
		}
		case VIEW_MSG_SET_OPTIMIZECOLORS:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->OptimizeColors = value;
				SaveSettings(Settings);
			}
			smallerfile->SetEnabled(Settings->OptimizeColors);
			break;
		}
		case VIEW_MSG_SET_SMALLERFILE:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->SmallerFile = value;
				SaveSettings(Settings);
			}
			break;
		}
		case VIEW_MSG_SET_GRAY1ASRGB24:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				Settings->B_GRAY1_as_B_RGB24 = value;
				SaveSettings(Settings);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


//----------------------------------------------------------------------------
//
//	Functions :: TranslatorAboutView
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	Constructor
//---------------------------------------------------
TranslatorAboutView::TranslatorAboutView(const char *name, float x, float y)
:	SView(name, x, y)
{
	BStringView *title = new BStringView( BRect(10, 0, 10, 0), "Title", translatorName);
	title->SetFont(be_bold_font);

	AddChild(title);

	BRect rect = title->Bounds();
	float space = title->StringWidth("    ");

	char versionString[16];
	sprintf(versionString, "v%d.%d.%d", (int)(translatorVersion >> 8), (int)((translatorVersion >> 4) & 0xf), (int)(translatorVersion & 0xf));
	
	BStringView *version = new BStringView( BRect(rect.right+space, rect.top, rect.right+space, rect.top), "Version", versionString);
	version->SetFont(be_plain_font);
	version->SetFontSize( 9);
	// Make version be in the same line as title
	version->ResizeToPreferred();
	version->MoveBy(0, rect.bottom-version->Frame().bottom);
	
	AddChild(version);

	// Now for each line in translatorInfo add BStringView
	BStringView *copyright;
	const char *current = translatorInfo;
	char *temp = translatorInfo;
	while (*current != 0) {
		// Find next line char
		temp = strchr(current, 0x0a);
		// If found replace it with 0 so current will look like ending here
		if (temp)
			*temp = 0;
		// Add BStringView showing what's under current
		copyright = new BStringView( BRect(10, GetPreferredHeight(), 10, GetPreferredHeight()), "Copyright", current);
		copyright->SetFont(be_plain_font);
		copyright->SetFontSize( 9);
		AddChild(copyright);

		// If there was next line, move current there and put next line char back
		if (temp) {
			current = temp+1;
			*temp = 0x0a;
		} else
		// If there was no next line char break loop
			break;
	}

	ResizeToPreferred();
}


//----------------------------------------------------------------------------
//
//	Functions :: TranslatorView
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	Constructor
//---------------------------------------------------
TranslatorView::TranslatorView(const char *name)
:	SView(name),tabWidth(30), tabHeight(7+be_plain_font->Size()), activeChild(0)
{
	// Set global var to true
	AreSettingsRunning = true;

	// Without this strings are not correctly aliased
	// THX to Jack Burton for info :)
	SetLowColor( ViewColor());

	// Load settings to global Settings struct
	LoadSettings(&Settings);

	// Add left and top margins
	float top = tabHeight+15;
	float left = 0;

	// This will remember longest string width
	int32 nameWidth = 0;

	SView *view = new TranslatorWriteView("Write", &Settings, left, top);
	AddChild(view);
	nameWidth = StringWidth(view->Name());

	view = new TranslatorReadView("Read", &Settings, left, top);
	AddChild(view);
	if (nameWidth < StringWidth(view->Name()))
		nameWidth = StringWidth(view->Name());

	view = new TranslatorAboutView("About", left, top);
	AddChild(view);
	if (nameWidth < StringWidth(view->Name()))
		nameWidth = StringWidth(view->Name());

	tabWidth += nameWidth;
	if (tabWidth * CountChildren() > GetPreferredWidth())
		ResizePreferredBy((tabWidth * CountChildren()) - GetPreferredWidth(), 0);

	// Add right and bottom margins
	ResizePreferredBy(10, 15);

	ResizeToPreferred();

	// Make TranslatorView resize itself with parent
	SetFlags( Flags() | B_FOLLOW_ALL);
}

//---------------------------------------------------
//	Attached to window - resize parent to preferred
//---------------------------------------------------
void
TranslatorView::AttachedToWindow()
{
	// Hide all children except first one
	BView *child = NULL;
	int32 index = 1;
	while (child = ChildAt(index++))
		child->Hide();
	
	// Hack for DataTranslations which doesn't resize visible area to requested by view
	// which makes some parts of bigger than usual translationviews out of visible area
	// so if it was loaded to DataTranslations resize window if needed
	BWindow *window = Window();
	if (!strcmp(window->Name(), "DataTranslations")) {
		BView *view = Parent();
		if (view) {
			BRect frame = view->Frame();
			if (frame.Width() < GetPreferredWidth() || (frame.Height()-48) < GetPreferredHeight()) {
				float x = ceil(GetPreferredWidth() - frame.Width());
				float y = ceil(GetPreferredHeight() - (frame.Height()-48));
				if (x < 0) x = 0;
				if (y < 0) y = 0;

				// DataTranslations has main view called "Background"
				// change it's resizing mode so it will always resize with window
				// also make sure view will be redrawed after resize
				view = window->FindView("Background");
				if (view) {
					view->SetResizingMode(B_FOLLOW_ALL);
					view->SetFlags(B_FULL_UPDATE_ON_RESIZE);
				}

				// The same with "Info..." button, except redrawing, which isn't needed
				view = window->FindView("Info…");
				if (view)
					view->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);

				window->ResizeBy( x, y);

				// Let user resize window if resizing option is not already there...
				uint32 flags = window->Flags();
				if (flags & B_NOT_RESIZABLE) {
					// ...but first prevent too small window (so "Info..." button will not look strange ;)
					// max will be 800x600 which should be enough for now
					window->SetSizeLimits(400, 800, 66, 600);

					flags ^= B_NOT_RESIZABLE;
					window->SetFlags(flags);
				}
			}
		}
	}
}

//---------------------------------------------------
//	DrawTabs
//---------------------------------------------------
void
TranslatorView::Draw(BRect updateRect)
{
	// This is needed because DataTranslations app hides children
	// after user changes translator
	if (ChildAt(activeChild)->IsHidden())
		ChildAt(activeChild)->Show();

	// Prepare colors used for drawing "tabs"
	rgb_color dark_line_color = tint_color( ViewColor(), B_DARKEN_2_TINT);
	rgb_color darkest_line_color = tint_color( ViewColor(), B_DARKEN_3_TINT);
	rgb_color light_line_color = tint_color( ViewColor(), B_LIGHTEN_MAX_TINT);
	rgb_color text_color = ui_color(B_MENU_ITEM_TEXT_COLOR);

	int32 index = 0;
	BView *child = NULL;
	float left = 0;

	// Clear
	SetHighColor( ViewColor());
	FillRect( BRect(0, 0, Frame().right, tabHeight));

	while (child = ChildAt(index)) {
		// Draw outline
		SetHighColor(dark_line_color);
		StrokeLine( BPoint(left, 10), BPoint(left, tabHeight));
		StrokeArc( BPoint(left+10, 10), 10, 10, 90, 90);
		StrokeLine( BPoint(left+10, 0), BPoint(left+tabWidth-10, 0));
		StrokeArc( BPoint(left+tabWidth-10, 10), 9, 10, 0, 90);
		StrokeLine( BPoint(left+tabWidth-1, 10), BPoint(left+tabWidth-1, tabHeight));
		// Draw "shadow" on the right side
		SetHighColor(darkest_line_color);
		StrokeArc( BPoint(left+tabWidth-10, 10), 10, 10, 0, 50);
		StrokeLine( BPoint(left+tabWidth, 10), BPoint(left+tabWidth, tabHeight-1));
		// Draw label
		SetHighColor(text_color);
		DrawString( child->Name(), BPoint(left+(tabWidth/2)-(StringWidth(child->Name())/2), 3+be_plain_font->Size()));
		// Draw "light" on left and top side
		SetHighColor(light_line_color);
		StrokeArc( BPoint(left+10, 10), 9, 9, 90, 90);
		StrokeLine( BPoint(left+1, 10), BPoint(left+1, tabHeight));
		StrokeLine( BPoint(left+10, 1), BPoint(left+tabWidth-8, 1));
		// Draw bottom edge
		if (activeChild != index)
			StrokeLine( BPoint(left-2,tabHeight), BPoint(left+tabWidth,tabHeight));
		else
			StrokeLine( BPoint(left-2,tabHeight), BPoint(left+1,tabHeight));

		left += tabWidth+2;
		index++;
	}
	// Draw bottom edge to the rigth side
	StrokeLine( BPoint(left-2,tabHeight), BPoint(Bounds().Width(),tabHeight));
}

//---------------------------------------------------
//	MouseDown, check if on tab, if so change tab if needed
//---------------------------------------------------
void
TranslatorView::MouseDown(BPoint where)
{
	// If user clicked on tabs part of view
	if (where.y <= tabHeight)
		// If there is a tab (not whole width is occupied by tabs)
		if (where.x < tabWidth*CountChildren()) {
			// Which tab was selected?
			int32 index = where.x / tabWidth;
			if (activeChild != index) {
				// Hide current visible child
				ChildAt(activeChild)->Hide();
				// This loop is needed because it looks like in DataTranslations
				// view gets hidden more than one time when user changes translator
				while (ChildAt(index)->IsHidden())
					ChildAt(index)->Show();
				// Remember which one is currently visible
				activeChild = index;
				// Redraw
				Draw( Frame());
			}
		}
}


//----------------------------------------------------------------------------
//
//	Functions :: TranslatorWindow
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	Constructor
//---------------------------------------------------
TranslatorWindow::TranslatorWindow(bool quit_on_close)
:	BWindow(BRect(100, 100, 100, 100), "JPEG Settings", B_TITLED_WINDOW, B_NOT_ZOOMABLE)
{
	BRect extent(0, 0, 0, 0);
	BView *config = NULL;
	MakeConfig(NULL, &config, &extent);

	AddChild(config);
	ResizeTo(extent.Width(), extent.Height());

	// Make application quit after this window close
	if (quit_on_close)
		SetFlags(Flags() | B_QUIT_ON_WINDOW_CLOSE);
}


//----------------------------------------------------------------------------
//
//	Functions :: main
//
//----------------------------------------------------------------------------

//---------------------------------------------------
//	main function
//---------------------------------------------------
int
main() {
	BApplication app("application/x-vnd.Shard.JPEGTranslator");
	
	TranslatorWindow *window = new TranslatorWindow();
	window->Show();
	
	app.Run();
	return 0;
}

//---------------------------------------------------
//	Hook to create and return our configuration view
//---------------------------------------------------
status_t
MakeConfig(BMessage *ioExtension, BView **outView, BRect *outExtent)
{
	*outView = new TranslatorView("TranslatorView");
	*outExtent = (*outView)->Frame();
	return B_OK;
}

//---------------------------------------------------
//	Determine whether or not we can handle this data
//---------------------------------------------------
status_t
Identify(BPositionIO *inSource, const translation_format *inFormat, BMessage *ioExtension, translator_info *outInfo, uint32 outType)
{

	if ((outType != 0) && (outType != B_TRANSLATOR_BITMAP) && (outType != JPEG_FORMAT))
		return B_NO_TRANSLATOR;

	// !!! You might need to make this buffer bigger to test for your native format
	off_t position = inSource->Position();
	char header[sizeof(TranslatorBitmap)];
	status_t err = inSource->Read(header, sizeof(TranslatorBitmap));
	inSource->Seek( position, SEEK_SET);
	if (err < B_OK) return err;

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

//---------------------------------------------------
//	Arguably the most important method in the add-on
//---------------------------------------------------
status_t
Translate(BPositionIO *inSource, const translator_info *inInfo, BMessage *ioExtension, uint32 outType, BPositionIO *outDestination)
{
	// If no specific type was requested, convert to the interchange format
	if (outType == 0) outType = B_TRANSLATOR_BITMAP;
	
	// What action to take, based on the findings of Identify()
	if (outType == inInfo->type) {
		return Copy(inSource, outDestination);
	} else if (inInfo->type == B_TRANSLATOR_BITMAP && outType == JPEG_FORMAT) {
		return Compress(inSource, outDestination);
	} else if (inInfo->type == JPEG_FORMAT && outType == B_TRANSLATOR_BITMAP) {
		return Decompress(inSource, outDestination);
	}

	return B_NO_TRANSLATOR;
}

//---------------------------------------------------
//	The user has requested the same format for input and output, so just copy
//---------------------------------------------------
status_t
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

//---------------------------------------------------
//	Encode into the native format
//---------------------------------------------------
status_t
Compress(BPositionIO *in, BPositionIO *out)
{
	// Load Settings
	SETTINGS Settings;
	LoadSettings(&Settings);

	// Read info about bitmap
	TranslatorBitmap header;
	status_t err = in->Read(&header, sizeof(TranslatorBitmap));
	if (err < B_OK) return err;
	else if (err < (int)sizeof(TranslatorBitmap)) return B_ERROR;
	
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
	void (*converter)(uchar *inscanline, uchar *outscanline, int inrow_bytes) = NULL;

	// Default color info
	J_COLOR_SPACE jpg_color_space = JCS_RGB;
	int jpg_input_components = 3;
	int32 out_row_bytes;
	int padding = 0;

	switch ((color_space)B_BENDIAN_TO_HOST_INT32(header.colors))
	{
		case B_CMAP8:
		{
			converter = convert_from_cmap8_to_24;
			padding = in_row_bytes - width;
			break;
		}
		case B_GRAY1:
		{
			if (Settings.B_GRAY1_as_B_RGB24) {
				converter = convert_from_gray1_to_24;
			} else {
				jpg_input_components = 1;
				jpg_color_space = JCS_GRAYSCALE;
				converter = convert_from_gray1_to_gray8;
			}
			padding = in_row_bytes - (width/8);
			break;
		}
		case B_GRAY8:
		{
			jpg_input_components = 1;
			jpg_color_space = JCS_GRAYSCALE;
			padding = in_row_bytes - width;
			break;
		}
		case B_RGB15:
		case B_RGBA15:
		{
			converter = convert_from_15_to_24;
			padding = in_row_bytes - (width * 2);
			break;
		}
		case B_RGB15_BIG:
		case B_RGBA15_BIG:
		{
			converter = convert_from_15b_to_24;
			padding = in_row_bytes - (width * 2);
			break;
		}
		case B_RGB16:
		{
			converter = convert_from_16_to_24;
			padding = in_row_bytes - (width * 2);
			break;
		}
		case B_RGB16_BIG:
		{
			converter = convert_from_16b_to_24;
			padding = in_row_bytes - (width * 2);
			break;
		}
		case B_RGB24:
		{
			converter = convert_from_24_to_24;
			padding = in_row_bytes - (width * 3);
			break;
		}
		case B_RGB24_BIG:
		{
			padding = in_row_bytes - (width * 3);
			break;
		}
		case B_RGB32:
		case B_RGBA32:
		{
			converter = convert_from_32_to_24;
			padding = in_row_bytes - (width * 4);
			break;
		}
		case B_RGB32_BIG:
		case B_RGBA32_BIG:
		{
			converter = convert_from_32b_to_24;
			padding = in_row_bytes - (width * 4);
			break;
		}
		case B_CMYK32:
		{
			jpg_color_space = JCS_CMYK;
			jpg_input_components = 4;
			padding = in_row_bytes - (width * 4);
			break;
		}
		default:
		{
			(new BAlert("Wrong type", "Color space not implemented.", "Quit"))->Go();
			return B_ERROR;
		}
	}
	out_row_bytes = jpg_input_components * width;

	// Set basic things needed for jpeg writing
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = be_jpeg_std_error(&jerr, &Settings);
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
	if( Settings.OptimizeColors) {
		int index = 0;
		while( index < cinfo.num_components) {
			cinfo.comp_info[index].h_samp_factor = 1;
			cinfo.comp_info[index].v_samp_factor = 1;
			// This will make file smaller, but with worse quality more or less
			// like with 93%-94% (but it's subjective opinion) on tested images
			// but with smaller size (between 92% and 93% on tested images)
			if(Settings.SmallerFile)
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
	jpeg_set_quality(&cinfo, Settings.Quality, true);
	
	// Set progressive compression if needed
	// if not, turn on optimizing in libjpeg
	if (Settings.Progressive)
		jpeg_simple_progression(&cinfo);
	else
		cinfo.optimize_coding = TRUE;

	// Set smoothing (effect like Blur)
	cinfo.smoothing_factor = Settings.Smoothing;

	// Initialize compression
	jpeg_start_compress(&cinfo, TRUE);

	// Declare scanlines
	JSAMPROW in_scanline = NULL;
	JSAMPROW out_scanline = NULL;
	JSAMPROW writeline;	// Pointer to in_scanline (default) or out_scanline (if there will be conversion)

	// Allocate scanline
	// Use libjpeg memory allocation functions, so in case of error it will free them itself
	in_scanline = (unsigned char *) (cinfo.mem->alloc_large) ( (j_common_ptr)&cinfo, JPOOL_PERMANENT, in_row_bytes);

	// We need 2nd scanline storage ony for conversion
	if (converter != NULL) {
		// There will be conversion, allocate second scanline...
		// Use libjpeg memory allocation functions, so in case of error it will free them itself
	    out_scanline = (unsigned char *) (cinfo.mem->alloc_large) ( (j_common_ptr)&cinfo, JPOOL_PERMANENT, out_row_bytes);
		// ... and make it the one to write to file
		writeline = out_scanline;
	} else
		writeline = in_scanline;

	while (cinfo.next_scanline < cinfo.image_height) {
		// Read scanline
		err = in->Read(in_scanline, in_row_bytes);
		if (err < in_row_bytes)
			return (err < B_OK) ? Error( (j_common_ptr)&cinfo, err) : Error( (j_common_ptr)&cinfo, B_ERROR);

		// Convert if needed
		if (converter != NULL)
			converter(in_scanline, out_scanline, in_row_bytes-padding);

		// Write scanline
	   	jpeg_write_scanlines(&cinfo, &writeline, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	
	return B_OK;
}

//---------------------------------------------------
//	Decode the native format
//---------------------------------------------------
status_t
Decompress(BPositionIO *in, BPositionIO *out)
{
	// Load Settings
	SETTINGS Settings;
	LoadSettings(&Settings);

	// Set basic things needed for jpeg reading
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = be_jpeg_std_error(&jerr, &Settings);
	jpeg_create_decompress(&cinfo);
	be_jpeg_stdio_src(&cinfo, in);

	// Read info about image
	jpeg_read_header(&cinfo, TRUE);

	// Default color info
	color_space out_color_space = B_RGB32;
	int out_color_components = 4;

	// Function pointer to convert function
	// It will point to proper function if needed
	void (*converter)(uchar *inscanline, uchar *outscanline, int inrow_bytes) = convert_from_24_to_32;

	// If color space isn't rgb
	if (cinfo.out_color_space != JCS_RGB) {
		switch (cinfo.out_color_space)
		{
			case JCS_UNKNOWN:		/* error/unspecified */
				(new BAlert("From Type", "Jpeg uses unknown color type", "Quit"))->Go();
				break;
			case JCS_GRAYSCALE:		/* monochrome */
				// Check if user wants to read only as RGB32 or not
				if (!Settings.Always_B_RGB32) {
					// Grayscale
					out_color_space = B_GRAY8;
					out_color_components = 1;
					converter = NULL;
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
				if (Settings.PhotoshopCMYK)
					converter = convert_from_CMYK_to_32_photoshop;
				else
					converter = convert_from_CMYK_to_32;
				break;
			default:
				(new BAlert("From Type", "Jpeg uses hmm... i don't know really :(", "Quit"))->Go();
				break;
		}
	}

	// Initialize decompression
	jpeg_start_decompress(&cinfo);

	// !!! Initialize this bounds rect to the size of your image
	BRect bounds( 0, 0, cinfo.output_width-1, cinfo.output_height-1);

	// Bytes count in one line of image (scanline)
	int64 row_bytes = cinfo.output_width * out_color_components;
	
	// Fill out the B_TRANSLATOR_BITMAP's header
	TranslatorBitmap header;
	header.magic = B_HOST_TO_BENDIAN_INT32(B_TRANSLATOR_BITMAP);
	header.bounds.left = B_HOST_TO_BENDIAN_FLOAT(bounds.left);
	header.bounds.top = B_HOST_TO_BENDIAN_FLOAT(bounds.top);
	header.bounds.right = B_HOST_TO_BENDIAN_FLOAT(bounds.right);
	header.bounds.bottom = B_HOST_TO_BENDIAN_FLOAT(bounds.bottom);
	header.colors = (color_space)B_HOST_TO_BENDIAN_INT32(out_color_space);
	header.rowBytes = B_HOST_TO_BENDIAN_INT32(row_bytes);
	header.dataSize = B_HOST_TO_BENDIAN_INT32(row_bytes * cinfo.output_height);

	// Write out the header
	status_t err = out->Write(&header, sizeof(TranslatorBitmap));
	if (err < B_OK) return Error( (j_common_ptr)&cinfo, err);
	else if (err < (int)sizeof(TranslatorBitmap)) return Error( (j_common_ptr)&cinfo, B_ERROR);

	// Declare scanlines
	JSAMPROW in_scanline = NULL;
	JSAMPROW out_scanline = NULL;
	JSAMPROW writeline;	// Pointer to in_scanline or out_scanline (if there will be conversion)

	// Allocate scanline
	// Use libjpeg memory allocation functions, so in case of error it will free them itself
    in_scanline = (unsigned char *) (cinfo.mem->alloc_large) ( (j_common_ptr)&cinfo, JPOOL_PERMANENT, row_bytes);

	// We need 2nd scanline storage only for conversion
	if (converter != NULL) {
		// There will be conversion, allocate second scanline...
		// Use libjpeg memory allocation functions, so in case of error it will free them itself
	    out_scanline = (unsigned char *) (cinfo.mem->alloc_large) ( (j_common_ptr)&cinfo, JPOOL_PERMANENT, row_bytes);
		// ... and make it the one to write to file
		writeline = out_scanline;
	} else
		writeline = in_scanline;

	while (cinfo.output_scanline < cinfo.output_height) {
		// Read scanline
		jpeg_read_scanlines(&cinfo, &in_scanline, 1);

		// Convert if needed
		if( converter != NULL)
			converter(in_scanline, out_scanline, row_bytes);

  		// Write the scanline buffer to the output stream
		err = out->Write(writeline, row_bytes);
		if (err < row_bytes)
			return (err < B_OK) ? Error( (j_common_ptr)&cinfo, err) : Error( (j_common_ptr)&cinfo, B_ERROR);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	return B_OK;
}

//---------------------------------------------------
//	Frees jpeg alocated memory
//	Returns given error (B_ERROR by default)
//---------------------------------------------------
status_t
Error(j_common_ptr cinfo, status_t error)
{
	jpeg_destroy(cinfo);
	return error;
}
