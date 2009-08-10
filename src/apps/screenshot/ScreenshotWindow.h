/*
 * Copyright Karsten Heimrich, host.haiku@gmx.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <String.h>
#include <Window.h>

#include "PreviewView.h"

class BBitmap;
class BBox;
class BButton;
class BCardLayout;
class BCheckBox;
class BFilePanel;
class BMenu;
class BRadioButton;
class BTextControl;
class BTextView;


class ScreenshotWindow : public BWindow {
public:
							ScreenshotWindow(bigtime_t delay = 0,
								bool includeBorder = false,
								bool includeMouse = false,
								bool grabActiveWindow = false,
								bool showConfigWindow = false,
								bool saveScreenshotSilent = false);
	virtual					~ScreenshotWindow();

	virtual	void			MessageReceived(BMessage* message);

private:
			void			_InitWindow();
			void			_SetupFirstLayoutItem(BCardLayout* layout);
			void			_SetupSecondLayoutItem(BCardLayout* layout);
			void			_DisallowChar(BTextView* textView);
			void			_SetupTranslatorMenu(BMenu* translatorMenu,
								const BMessage& settings);
			void			_SetupOutputPathMenu(BMenu* outputPathMenu,
								const BMessage& settings);
			void			_AddItemToPathMenu(const char* path,
								BString& label, int32 index, bool markItem);
			void			_CenterAndShow();

			void			_UpdatePreviewPanel();
			BString			_FindValidFileName(const char* name) const;
			int32			_PathIndexInMenu(const BString& path) const;

			BMessage		_ReadSettings() const;
			void			_WriteSettings() const;

			void			_TakeScreenshot();
			status_t		_GetActiveWindowFrame(BRect* frame);
			void			_MakeTabSpaceTransparent();

			status_t		_SaveScreenshot();
			void			_SaveScreenshotSilent() const;

private:
			PreviewView*	fPreview;
			BRadioButton*	fActiveWindow;
			BRadioButton*	fWholeDesktop;
			BTextControl*	fDelayControl;
			BCheckBox*		fWindowBorder;
			BCheckBox*		fShowMouse;
			BButton*		fBackToSave;
			BButton*		fTakeScreenshot;
			BTextControl*	fNameControl;
			BMenu*			fTranslatorMenu;
			BMenu*			fOutputPathMenu;
			BBitmap*		fScreenshot;
			BFilePanel*		fOutputPathPanel;
			BMenuItem*		fLastSelectedPath;

			bigtime_t		fDelay;
			float			fTabHeight;

			bool			fIncludeBorder;
			bool			fIncludeMouse;
			bool			fGrabActiveWindow;
			bool			fShowConfigWindow;

			int32			fTranslator;
			int32			fImageFileType;
};
