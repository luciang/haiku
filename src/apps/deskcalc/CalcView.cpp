/*
 * Copyright 2006 Haiku, Inc. All Rights Reserved.
 * Copyright 1997, 1998 R3 Software Ltd. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Timothy Wayper <timmy@wunderbear.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 */

#include "CalcView.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <Alert.h>
#include <Application.h>
#include <AppFileInfo.h>
#include <Beep.h>
#include <Bitmap.h>
#include <Clipboard.h>
#include <File.h>
#include <Font.h>
#include <MenuItem.h>
#include <Message.h>
#include <PlaySound.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Roster.h>

#include "CalcApplication.h"
#include "CalcOptions.h"
#include "ExpressionParser.h"
#include "ExpressionTextView.h"


const uint8 K_COLOR_OFFSET		= 32;
const float K_FONT_YPROP		= 0.6f;
const float K_FONT_XPROP		= 0.6f;
const float K_DISPLAY_YPROP		= 0.2f;

enum {
	K_OPTIONS_AUTO_NUM_LOCK		= 'opan',
	K_OPTIONS_AUDIO_FEEDBACK	= 'opaf',
	K_OPTIONS_SHOW_KEYPAD		= 'opsk'
};

// default calculator key pad layout
const char *kDefaultKeypadDescription =
	"7   8   9   (   )  \n"
	"4   5   6   *   /  \n"
	"1   2   3   +   -  \n"
	"0   .   BS  =   C  \n";


struct CalcView::CalcKey {
	char		label[8];
	char		code[8];
	char		keymap[4];
	uint32		flags;
	float		width;
};


CalcView *CalcView::Instantiate(BMessage *archive)
{
	if (!validate_instantiation(archive, "CalcView"))
		return NULL; 

	return new CalcView(archive);
}


CalcView::CalcView(BRect frame, rgb_color rgbBaseColor, BMessage *settings)
	: BView(frame, "DeskCalc", B_FOLLOW_ALL_SIDES,
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	  fColums(5),
	  fRows(4),

	  fBaseColor(rgbBaseColor),
	  fExpressionBGColor((rgb_color){ 0, 0, 0, 255 }),

	  fWidth(1),
	  fHeight(1),

	  fKeypadDescription(strdup(kDefaultKeypadDescription)),
	  fKeypad(NULL),

#ifdef __HAIKU__
	  fCalcIcon(new BBitmap(BRect(0, 0, 15, 15), 0, B_RGBA32)),
#else
	  fCalcIcon(new BBitmap(BRect(0, 0, 15, 15), 0, B_CMAP8)),
#endif

	  fPopUpMenu(NULL),
	  fAutoNumlockItem(NULL),
	  fAudioFeedbackItem(NULL),
	  fShowKeypadItem(NULL),
	  fAboutItem(NULL),

	  fOptions(new CalcOptions()),
	  fShowKeypad(true)
{
	// create expression text view
	fExpressionTextView = new ExpressionTextView(_ExpressionRect(), this);
	AddChild(fExpressionTextView);

	_LoadSettings(settings);

	// tell the app server not to erase our b/g
	SetViewColor(B_TRANSPARENT_32_BIT);

	// parse calculator description
	_ParseCalcDesc(fKeypadDescription);
	
	// colorize based on base color.
	_Colorize();

	// create pop-up menu system
	_CreatePopUpMenu();

	_FetchAppIcon(fCalcIcon);
}


CalcView::CalcView(BMessage* archive)
	: BView(archive),
	  fColums(5),
	  fRows(4),

	  fBaseColor((rgb_color){ 128, 128, 128, 255 }),
	  fExpressionBGColor((rgb_color){ 0, 0, 0, 255 }),

	  fWidth(1),
	  fHeight(1),

	  fKeypadDescription(strdup(kDefaultKeypadDescription)),
	  fKeypad(NULL),

#ifdef __HAIKU__
	  fCalcIcon(new BBitmap(BRect(0, 0, 15, 15), 0, B_RGBA32)),
#else
	  fCalcIcon(new BBitmap(BRect(0, 0, 15, 15), 0, B_CMAP8)),
#endif

	  fPopUpMenu(NULL),
	  fAutoNumlockItem(NULL),
	  fAudioFeedbackItem(NULL),
	  fShowKeypadItem(NULL),
	  fAboutItem(NULL),

	  fOptions(new CalcOptions()),
	  fShowKeypad(true)
{
	// create expression text view
	fExpressionTextView = new ExpressionTextView(_ExpressionRect(), this);
	AddChild(fExpressionTextView);

	// read data from archive
	_LoadSettings(archive);
	
	// create pop-up menu system
	_CreatePopUpMenu();

	_FetchAppIcon(fCalcIcon);
}


CalcView::~CalcView()
{
	delete fKeypad;
	delete fOptions;
	free(fKeypadDescription);
}


void
CalcView::AttachedToWindow()
{
	SetFont(be_bold_font);
	
	BRect frame(Frame());
	FrameResized(frame.Width(), frame.Height());

	fPopUpMenu->SetTargetForItems(this);
	_ShowKeypad(fOptions->show_keypad);
}


void
CalcView::MessageReceived(BMessage* message)
{
	// check if message was dropped
	if (message->WasDropped()) {
		// pass message on to paste
		if (message->IsSourceRemote())
			Paste(message);

	} else {

		// act on posted message type
		switch (message->what) {
		
			// handle "cut"
			case B_CUT:
				Cut();
				break;
			
			// handle copy
			case B_COPY:
				Copy();
				break;
			
			// handle paste
			case B_PASTE:
				// access system clipboard
				if (be_clipboard->Lock()){ 
					BMessage *clipper = be_clipboard->Data();
					//clipper->PrintToStream();
					Paste(clipper);
					be_clipboard->Unlock(); 
					} // end if	
				break;
			
			// (replicant) about box requested
			case B_ABOUT_REQUESTED:
				AboutRequested();
				break;

			case K_OPTIONS_AUTO_NUM_LOCK:
				fOptions->auto_num_lock = !fOptions->auto_num_lock;
				fAutoNumlockItem->SetMarked(fOptions->auto_num_lock);
				break;

			case K_OPTIONS_AUDIO_FEEDBACK:
				fOptions->audio_feedback = !fOptions->audio_feedback;
				fAudioFeedbackItem->SetMarked(fOptions->audio_feedback);
				break;

			case K_OPTIONS_SHOW_KEYPAD:
				fOptions->show_keypad = !fOptions->show_keypad;
				fShowKeypadItem->SetMarked(fOptions->show_keypad);
				_ShowKeypad(fOptions->show_keypad);
				break;
			
			default:
				BView::MessageReceived(message);
				break;
		}
	}
}


void
CalcView::Draw(BRect updateRect)
{
	BRect expressionRect(_ExpressionRect());
	if (updateRect.Intersects(expressionRect)) {
		if (!fShowKeypad && expressionRect.Height() >= fCalcIcon->Bounds().Height()) {
			// render calc icon
			expressionRect.left = fExpressionTextView->Frame().right + 2;
			SetHighColor(fBaseColor);
			FillRect(updateRect & expressionRect);

			if (fCalcIcon->ColorSpace() == B_RGBA32) {
				SetDrawingMode(B_OP_ALPHA);
				SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			} else {
				SetDrawingMode(B_OP_OVER);
			}

			BPoint iconPos;
			iconPos.x = expressionRect.right - (expressionRect.Width()
				+ fCalcIcon->Bounds().Width()) / 2.0;
			iconPos.y = expressionRect.top + (expressionRect.Height()
				- fCalcIcon->Bounds().Height()) / 2.0;
			DrawBitmap(fCalcIcon, iconPos);

			SetDrawingMode(B_OP_COPY);
		}

		// render border around expression text view
		expressionRect = fExpressionTextView->Frame();
		expressionRect.InsetBy(-2, -2);
		BeginLineArray(8);

		rgb_color lightShadow = tint_color(fBaseColor, B_DARKEN_1_TINT);
		rgb_color darkShadow = tint_color(fBaseColor, B_DARKEN_3_TINT);

		AddLine(BPoint(expressionRect.left, expressionRect.bottom),
				BPoint(expressionRect.left, expressionRect.top),
				lightShadow);
		AddLine(BPoint(expressionRect.left + 1, expressionRect.top),
				BPoint(expressionRect.right, expressionRect.top),
				lightShadow);
		AddLine(BPoint(expressionRect.right, expressionRect.top + 1),
				BPoint(expressionRect.right, expressionRect.bottom),
				fLightColor);
		AddLine(BPoint(expressionRect.left + 1, expressionRect.bottom),
				BPoint(expressionRect.right - 1, expressionRect.bottom),
				fLightColor);

		expressionRect.InsetBy(1, 1);
		AddLine(BPoint(expressionRect.left, expressionRect.bottom),
				BPoint(expressionRect.left, expressionRect.top),
				darkShadow);
		AddLine(BPoint(expressionRect.left + 1, expressionRect.top),
				BPoint(expressionRect.right, expressionRect.top),
				darkShadow);
		AddLine(BPoint(expressionRect.right, expressionRect.top + 1),
				BPoint(expressionRect.right, expressionRect.bottom),
				fBaseColor);
		AddLine(BPoint(expressionRect.left + 1, expressionRect.bottom),
				BPoint(expressionRect.right - 1, expressionRect.bottom),
				fBaseColor);

		EndLineArray();
	}

	if (!fShowKeypad)
		return;

	// calculate grid sizes
	BRect keypadRect(_KeypadRect());
	float sizeDisp = keypadRect.top;
	float sizeCol = fWidth / (float)fColums;
	float sizeRow = (fHeight - sizeDisp) / (float)fRows;

	if (updateRect.Intersects(keypadRect)) {
		// TODO: support pressed keys

		// paint keypad b/g
		SetHighColor(fBaseColor);
		FillRect(updateRect & keypadRect);
		
		// render key main grid
		BeginLineArray(((fColums + fRows) << 1) + 1);
		
		// render cols
		AddLine(BPoint(0.0, sizeDisp),
				BPoint(0.0, fHeight),
				fLightColor);
		for (int col = 1; col < fColums; col++) {
			AddLine(BPoint(col * sizeCol - 1.0, sizeDisp),
					BPoint(col * sizeCol - 1.0, fHeight),
					fDarkColor);
			AddLine(BPoint(col * sizeCol, sizeDisp),
					BPoint(col * sizeCol, fHeight),
					fLightColor);
		}
		AddLine(BPoint(fColums * sizeCol, sizeDisp),
				BPoint(fColums * sizeCol, fHeight),
				fDarkColor);
			
		// render rows
		for (int row = 0; row < fRows; row++) {
			AddLine(BPoint(0.0, sizeDisp + row * sizeRow - 1.0),
					BPoint(fWidth, sizeDisp + row * sizeRow - 1.0),
					fDarkColor);
			AddLine(BPoint(0.0, sizeDisp + row * sizeRow),
					BPoint(fWidth, sizeDisp + row * sizeRow),
					fLightColor);
		}
		AddLine(BPoint(0.0, sizeDisp + fRows * sizeRow),
				BPoint(fWidth, sizeDisp + fRows * sizeRow),
				fDarkColor);
			
		// main grid complete
		EndLineArray();
		
		// render key symbols
		float halfSizeCol = sizeCol * 0.5f;
		SetHighColor(fButtonTextColor);
		SetLowColor(fBaseColor);
		SetDrawingMode(B_OP_COPY);

		float fontSize =
			min_c(((fHeight - sizeDisp) / (float)fRows) * K_FONT_YPROP,
			(fWidth / (float)fColums) * K_FONT_XPROP);

		SetFontSize(fontSize);

		float baselineOffset = ((fHeight - sizeDisp) / (float)fRows)
								* (1.0 - K_FONT_YPROP) * 0.5;
		CalcKey *key = fKeypad;
		for (int row = 0; row < fRows; row++) {
			for (int col = 0; col < fColums; col++) {
				float halfSymbolWidth = StringWidth(key->label) * 0.5f;
				DrawString(key->label,
						   BPoint(col * sizeCol + halfSizeCol - halfSymbolWidth,
								  sizeDisp + (row + 1) * sizeRow - baselineOffset));
				key++;
			}
		}
	}
}


void
CalcView::MouseDown(BPoint point)
{
	// ensure this view is the current focus
	if (!IsFocus())	
		MakeFocus();

	// read mouse buttons state
	int32 buttons = 0;
	Window()->CurrentMessage()->FindInt32("buttons", &buttons);

	// display popup menu if not primary mouse button
	if ((B_PRIMARY_MOUSE_BUTTON & buttons) == 0) {
		BMenuItem* selected;
		if ((selected = fPopUpMenu->Go(ConvertToScreen(point))) != NULL
			&& selected->Message() != NULL)
			MessageReceived(selected->Message());
		return;
	}

	if (!fShowKeypad)
		return;

	// calculate grid sizes
	float sizeDisp = fHeight * K_DISPLAY_YPROP;
	float sizeCol = fWidth / (float)fColums;
	float sizeRow = (fHeight - sizeDisp) / (float)fRows;
	
	// calculate location within grid
	int gridCol = (int)floorf(point.x / sizeCol);
	int gridRow = (int)floorf((point.y - sizeDisp) / sizeRow);
	
	// check limits
	if ((gridCol >= 0) && (gridCol < fColums) &&
		(gridRow >= 0) && (gridRow < fRows)) {
	
		// process key press
		_PressKey(gridRow * fColums + gridCol);
	}
}


void
CalcView::KeyDown(const char *bytes, int32 numBytes)
{
 	// if single byte character...
	if (numBytes == 1) { 
		
		//printf("Key pressed: %c\n", bytes[0]);
		
		switch (bytes[0]) {
		
			case B_ENTER:
				// translate to evaluate key
				_PressKey("=");
				break;
			
			case B_LEFT_ARROW:
			case B_BACKSPACE:
				// translate to backspace key
				_PressKey("BS");
				break;
			
			case B_SPACE:
			case B_ESCAPE:
			case 'c':
				// translate to clear key
				_PressKey("C");
				break;
			
			// bracket translation
			case '[':
			case '{':
				_PressKey("(");
				break;
	
			case ']':
			case '}':
				_PressKey(")");
				break;
			
			default: {
				// scan the keymap array for match
				int keys = fRows * fColums;
				for (int i = 0; i < keys; i++) {
					if (fKeypad[i].keymap[0] == bytes[0]) {
						_PressKey(i);
						return;
					}
				}
				break;
			}
		}
	}
}


void
CalcView::MakeFocus(bool focused)
{
	if (focused) {
		// set num lock
		if (fOptions->auto_num_lock) {
			set_keyboard_locks(B_NUM_LOCK |
							   (modifiers() & (B_CAPS_LOCK | B_SCROLL_LOCK)));
		}
	}

	// pass on request to text view
	fExpressionTextView->MakeFocus(focused);
}


void
CalcView::FrameResized(float width, float height)
{
	fWidth = width;
	fHeight = height;

	// layout expression text view
	BRect frame = _ExpressionRect();
	frame.InsetBy(2, 2);

	if (!fShowKeypad)
		frame.right -= ceilf(fCalcIcon->Bounds().Width() * 1.5);

	fExpressionTextView->MoveTo(frame.LeftTop());
	fExpressionTextView->ResizeTo(frame.Width(), frame.Height());

	frame.OffsetTo(B_ORIGIN);
	frame.InsetBy(2, 2);
	fExpressionTextView->SetTextRect(frame);

	// configure expression text view font size and color
	float sizeDisp = fShowKeypad ? fHeight * K_DISPLAY_YPROP : fHeight;
	BFont font(be_bold_font);
	font.SetSize(sizeDisp * K_FONT_YPROP);
//	fExpressionTextView->SetViewColor(fExpressionBGColor);
//	fExpressionTextView->SetLowColor(fExpressionBGColor);
//	fExpressionTextView->SetFontAndColor(&font, B_FONT_ALL, &fExpressionTextColor);
fExpressionTextView->SetFontAndColor(&font, B_FONT_ALL);
}


void
CalcView::AboutRequested()
{
	BAlert* alert = new BAlert("about",
		"DeskCalc v2.1.0\n\n"
		"written by Timothy Wayper,\nStephan Aßmus and Ingo Weinhold\n\n"
		B_UTF8_COPYRIGHT"1997, 1998 R3 Software Ltd.\n"
		B_UTF8_COPYRIGHT"2006-2009 Haiku, Inc.\n\n"
		"All Rights Reserved.", "Cool");
	alert->Go(NULL);
}


status_t
CalcView::Archive(BMessage* archive, bool deep) const
{
	fExpressionTextView->RemoveSelf();

	// passed on request to parent
	status_t ret = BView::Archive(archive, deep);

	const_cast<CalcView*>(this)->AddChild(fExpressionTextView);

	// save app signature for replicant add-on loading
    if (ret == B_OK)
    	ret = archive->AddString("add_on", kAppSig);

	// save all the options
	if (ret == B_OK)
		ret = SaveSettings(archive);

	// add class info last
	if (ret == B_OK)
		ret = archive->AddString("class", "CalcView");

	return ret;
}


void
CalcView::Cut()
{
	Copy();	// copy data to clipboard
	fExpressionTextView->Clear(); // remove data
}


void
CalcView::Copy()
{
	// access system clipboard
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		BMessage *clipper = be_clipboard->Data();
		clipper->what = B_MIME_DATA;
		// TODO: should check return for errors!
		BString expression = fExpressionTextView->Text();
		clipper->AddData("text/plain",
						 B_MIME_TYPE,
						 expression.String(),
						 expression.Length());
		//clipper->PrintToStream();
		be_clipboard->Commit(); 
		be_clipboard->Unlock(); 
	}
}

 
void
CalcView::Paste(BMessage *message)
{
	// handle color drops first		
	// read incoming color
	const rgb_color* dropColor = NULL;
	ssize_t dataSize;
	if (message->FindData("RGBColor",
						  B_RGB_COLOR_TYPE,
						  (const void**)&dropColor,
						  &dataSize) == B_OK
		&& dataSize == sizeof(rgb_color)) {
			
		// calculate view relative drop point
		BPoint dropPoint = ConvertFromScreen(message->DropPoint());
		
		// calculate current keypad area
		float sizeDisp = fHeight * K_DISPLAY_YPROP;
		BRect keypadRect(0.0, sizeDisp, fWidth, fHeight);
		
		// check location of color drop
		if (keypadRect.Contains(dropPoint) && dropColor) {
			fBaseColor = *dropColor;
			_Colorize();
			// redraw keypad
			Invalidate(keypadRect);
		}

	} else {
		// look for text/plain MIME data
		const char* text;
		ssize_t numBytes;
		if (message->FindData("text/plain",
							  B_MIME_TYPE,
							  (const void**)&text,
							  &numBytes) == B_OK) {
			BString temp;
			temp.Append(text, numBytes);
			fExpressionTextView->Insert(temp.String());
		}
	}
}


status_t
CalcView::_LoadSettings(BMessage* archive)
{
	if (!archive)
		return B_BAD_VALUE;

	// record calculator description
	const char* calcDesc;
	if (archive->FindString("calcDesc", &calcDesc) < B_OK)
		calcDesc = kDefaultKeypadDescription;

	// save calculator description for reference
	free(fKeypadDescription);
	fKeypadDescription = strdup(calcDesc);

	// read grid dimensions
	if (archive->FindInt16("cols", &fColums) < B_OK)
		fColums = 5;
	if (archive->FindInt16("rows", &fRows) < B_OK)
		fRows = 4;
	
	// read color scheme
	const rgb_color* color;
	ssize_t size;
	if (archive->FindData("rgbBaseColor", B_RGB_COLOR_TYPE,
						  (const void**)&color, &size) < B_OK
						  || size != sizeof(rgb_color)) {
		fBaseColor = (rgb_color){ 128, 128, 128, 255 };
		puts("Missing rgbBaseColor from CalcView archive!\n");
	} else {
		fBaseColor = *color;
	}
		
	if (archive->FindData("rgbDisplay", B_RGB_COLOR_TYPE,
						  (const void**)&color, &size) < B_OK
						  || size != sizeof(rgb_color)) {
		fExpressionBGColor = (rgb_color){ 0, 0, 0, 255 };
		puts("Missing rgbBaseColor from CalcView archive!\n");
	} else {
		fExpressionBGColor = *color;
	}
	
	// load options
	fOptions->LoadSettings(archive);

	// load display text
	const char* display;
	if (archive->FindString("displayText", &display) < B_OK) {
		puts("Missing expression text from CalcView archive.\n");
	} else {
		// init expression text
		fExpressionTextView->SetText(display);
	}

	// load expression history
	fExpressionTextView->LoadSettings(archive);

	// parse calculator description
	_ParseCalcDesc(fKeypadDescription);
	
	// colorize based on base color.
	_Colorize();

	return B_OK;
}


status_t
CalcView::SaveSettings(BMessage* archive) const
{
	status_t ret = archive ? B_OK : B_BAD_VALUE;

	// record grid dimensions
	if (ret == B_OK)
		ret = archive->AddInt16("cols", fColums);
	if (ret == B_OK)
		ret = archive->AddInt16("rows", fRows);
	
	// record color scheme
	if (ret == B_OK)
		ret = archive->AddData("rgbBaseColor", B_RGB_COLOR_TYPE,
							   &fBaseColor, sizeof(rgb_color));
	if (ret == B_OK)
		ret = archive->AddData("rgbDisplay", B_RGB_COLOR_TYPE,
							   &fExpressionBGColor, sizeof(rgb_color));

	// record current options
	if (ret == B_OK)
		ret = fOptions->SaveSettings(archive);

	// record display text
	if (ret == B_OK)
		ret = archive->AddString("displayText", fExpressionTextView->Text());

	// record expression history
	if (ret == B_OK)
		ret = fExpressionTextView->SaveSettings(archive);

	// record calculator description
	if (ret == B_OK)
		ret = archive->AddString("calcDesc", fKeypadDescription);

	return ret;
}


void
CalcView::Evaluate()
{
	BString expression = fExpressionTextView->Text();

	if (expression.Length() == 0) {
		beep();
		return;
	}

	// audio feedback
	if (fOptions->audio_feedback) {
		BEntry zimp("zimp.AIFF");
		entry_ref zimp_ref;
		zimp.GetRef(&zimp_ref);
		play_sound(&zimp_ref, true, false, false);
	}

	// evaluate expression
	BString value;

	try {
		ExpressionParser parser;
		value = parser.Evaluate(expression.String());
	} catch (ParseException e) {
		BString error(e.message.String());
		error << " at " << (e.position + 1);
		fExpressionTextView->SetText(error.String());
		return;
	}

	// render new result to display
	fExpressionTextView->SetExpression(value.String());
}


void
CalcView::FlashKey(const char* bytes, int32 numBytes)
{
	BString temp;
	temp.Append(bytes, numBytes);
	int32 key = _KeyForLabel(temp.String());
	if (key >= 0)
		_FlashKey(key);
}


// #pragma mark -


void
CalcView::_ParseCalcDesc(const char* keypadDescription)
{
	// TODO: should calculate dimensions from desc here!
	fKeypad = new CalcKey[fRows * fColums];

	// scan through calculator description and assemble keypad
	CalcKey *key = fKeypad;
	const char *p = keypadDescription;

	while (*p != 0) {
		// copy label
		char *l = key->label;
		while (!isspace(*p))
			*l++ = *p++;
		*l = '\0';

		// set code
		if (strcmp(key->label, "=") == 0)
			strcpy(key->code, "\n");
		else
			strcpy(key->code, key->label);

		// set keymap
		if (strlen(key->label) == 1) {
			strcpy(key->keymap, key->label);
		} else {
			*key->keymap = '\0';
		} // end if

		// add this to the expression text view, so that it
		// will forward the respective KeyDown event to us
		fExpressionTextView->AddKeypadLabel(key->label);

		// advance	
		while (isspace(*p))
			++p;
		key++;
	}
}


void
CalcView::_PressKey(int key)
{
	assert(key < (fRows * fColums));
	assert(key >= 0);

	// check for backspace
	if (strcmp(fKeypad[key].label, "BS") == 0) {
		fExpressionTextView->BackSpace();
	} else if (strcmp(fKeypad[key].label, "C") == 0) {
		// C means clear
		fExpressionTextView->Clear();
	} else {
		// check for evaluation order
		if (fKeypad[key].code[0] == '\n') {
			Evaluate();
		} else {
			// insert into expression text
			fExpressionTextView->Insert(fKeypad[key].code);
		
			// audio feedback
			if (fOptions->audio_feedback) {
				BEntry zimp("key.AIFF");
				entry_ref zimp_ref;
				zimp.GetRef(&zimp_ref);
				play_sound(&zimp_ref, true, false, true);
			}
		}
	}

	// redraw display
//	_InvalidateExpression();
}


void
CalcView::_PressKey(const char *label)
{
	int32 key = _KeyForLabel(label);
	if (key >= 0)
		_PressKey(key);
}


int32
CalcView::_KeyForLabel(const char *label) const
{
	int keys = fRows * fColums;
	for (int i = 0; i < keys; i++) {
		if (strcmp(fKeypad[i].label, label) == 0) {
			return i;
		}
	}
	return -1;
}


void
CalcView::_FlashKey(int32 key)
{
	// TODO ...
}


void
CalcView::_Colorize()
{
	// calculate light and dark color from base color
	fLightColor.red		= (uint8)(fBaseColor.red * 1.25);
	fLightColor.green	= (uint8)(fBaseColor.green * 1.25);
	fLightColor.blue	= (uint8)(fBaseColor.blue * 1.25);
	fLightColor.alpha	= 255;
	
	fDarkColor.red		= (uint8)(fBaseColor.red * 0.75);
	fDarkColor.green	= (uint8)(fBaseColor.green * 0.75);
	fDarkColor.blue		= (uint8)(fBaseColor.blue * 0.75);
	fDarkColor.alpha	= 255;

	// keypad text color
	uint8 lightness = (fBaseColor.red + fBaseColor.green + fBaseColor.blue) / 3;
	if (lightness > 200)
		fButtonTextColor = (rgb_color){ 0, 0, 0, 255 };
	else
		fButtonTextColor = (rgb_color){ 255, 255, 255, 255 };

	// expression text color
	lightness = (fExpressionBGColor.red +
				 fExpressionBGColor.green + fExpressionBGColor.blue) / 3;
	if (lightness > 200)
		fExpressionTextColor = (rgb_color){ 0, 0, 0, 255 };
	else
		fExpressionTextColor = (rgb_color){ 255, 255, 255, 255 };
}


void
CalcView::_CreatePopUpMenu()
{
	// construct items
	fAutoNumlockItem = new BMenuItem("Enable Num Lock on start up",
		new BMessage(K_OPTIONS_AUTO_NUM_LOCK));
	fAudioFeedbackItem = new BMenuItem("Audio Feedback",
		new BMessage(K_OPTIONS_AUDIO_FEEDBACK));
	fShowKeypadItem = new BMenuItem("Show Keypad",
		new BMessage(K_OPTIONS_SHOW_KEYPAD));
	fAboutItem = new BMenuItem("About DeskCalc",
		new BMessage(B_ABOUT_REQUESTED));

	// apply current settings
	fAutoNumlockItem->SetMarked(fOptions->auto_num_lock);
	fAudioFeedbackItem->SetMarked(fOptions->audio_feedback);
	fShowKeypadItem->SetMarked(fOptions->show_keypad);

	// construct menu
	fPopUpMenu = new BPopUpMenu("pop-up", false, false);

	fPopUpMenu->AddItem(fAutoNumlockItem);
	fPopUpMenu->AddItem(fAudioFeedbackItem);
	fPopUpMenu->AddItem(fShowKeypadItem);
	fPopUpMenu->AddSeparatorItem();
	fPopUpMenu->AddItem(fAboutItem);
}


BRect
CalcView::_ExpressionRect() const
{
	BRect r(0.0, 0.0, fWidth, fHeight);
	if (fShowKeypad) {
		r.bottom = floorf(fHeight * K_DISPLAY_YPROP);
	}
	return r;
}


BRect
CalcView::_KeypadRect() const
{
	BRect r(0.0, 0.0, -1.0, -1.0);
	if (fShowKeypad) {
		r.right = fWidth;
		r.bottom = fHeight;
		r.top = floorf(fHeight * K_DISPLAY_YPROP) + 1;
	}
	return r;
}


void
CalcView::_ShowKeypad(bool show)
{
	if (fShowKeypad == show)
		return;

	fShowKeypad = show;
	if (fShowKeypadItem && fShowKeypadItem->IsMarked() ^ fShowKeypad)
			fShowKeypadItem->SetMarked(fShowKeypad);

	float height = fShowKeypad ? fHeight / K_DISPLAY_YPROP
							   : fHeight * K_DISPLAY_YPROP;

	BWindow* window = Window();
	if (window) {
		if (window->Bounds() == Frame()) {
			window->ResizeTo(fWidth, height);
			window->SetSizeLimits(100.0, 400.0, fShowKeypad ? 100.0 : 20.0, 400.0);
		} else
			ResizeTo(fWidth, height);
	}
}

 
void
CalcView::_FetchAppIcon(BBitmap* into)
{
	entry_ref appRef;
	be_roster->FindApp(kAppSig, &appRef);
	BFile file(&appRef, B_READ_ONLY);
	BAppFileInfo appInfo(&file);
	if (appInfo.GetIcon(into, B_MINI_ICON) != B_OK)
		memset(into->Bits(), 0, into->BitsLength());
}
