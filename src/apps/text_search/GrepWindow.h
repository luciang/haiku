/*
 * Copyright (c) 1998-2007 Matthijs Hollemans
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef GREP_WINDOW_H
#define GREP_WINDOW_H

#include <InterfaceKit.h>
#include <FilePanel.h>

#include "Model.h"
#include "GrepListView.h"

class BMessageRunner;
class ChangesIterator;
class Grepper;

class GrepWindow : public BWindow {
public:
								GrepWindow(BMessage* message);
	virtual						~GrepWindow();
	
	virtual	void				FrameResized(float width, float height);
	virtual	void				FrameMoved(BPoint origin);
	virtual	void				MenusBeginning();
	virtual	void				MenusEnded();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Quit();
	
private:
			void				_InitRefsReceived(entry_ref* directory,
									BMessage* message);
			void				_SetWindowTitle();
			void				_CreateMenus();
			void				_CreateViews();
			void				_LayoutViews();
			void				_TileIfMultipleWindows();
	
			void				_LoadPrefs();
			void				_SavePrefs();

			void				_StartNodeMonitoring();
			void				_StopNodeMonitoring();

			void				_OnStartCancel();
			void				_OnSearchFinished();
			void				_OnNodeMonitorEvent(BMessage* message);
			void				_OnNodeMonitorPulse();
			void				_OnReportFileName(BMessage* message);
			void				_OnReportResult(BMessage* message);
			void				_OnReportError(BMessage* message);
			void				_OnRecurseLinks();
			void				_OnRecurseDirs();
			void				_OnSkipDotDirs();
			void				_OnEscapeText();
			void				_OnCaseSensitive();
			void				_OnTextOnly();
			void				_OnInvokePe();
			void				_OnCheckboxShowLines();
			void				_OnMenuShowLines();
			void				_OnInvokeItem();
			void				_OnSearchText();
			void				_OnHistoryItem(BMessage* message);
			void				_OnTrimSelection();
			void				_OnCopyText();
			void				_OnSelectInTracker();
			void				_OnQuitNow();
			void				_OnAboutRequested();
			void				_OnFileDrop(BMessage* message);
			void				_OnRefsReceived(BMessage* message);
			void				_OnOpenPanel();
			void				_OnOpenPanelCancel();
			void				_OnSelectAll(BMessage* message);
			void				_OnNewWindow();
		
			bool				_OpenInPe(const entry_ref& ref, int32 lineNum);
			void				_RemoveFolderListDuplicates(BList* folderList);
			status_t			_OpenFoldersInTracker(BList* folderList);
			bool				_AreAllFoldersOpenInTracker(BList* folderList);
			status_t			_SelectFilesInTracker(BList* folderList,
									BMessage* refsMessage);

private:
			BTextControl*		fSearchText;
			GrepListView*		fSearchResults;
		
			BMenuBar*			fMenuBar;
			BMenu*				fFileMenu;
			BMenuItem*			fNew;
			BMenuItem*			fOpen;
			BMenuItem*			fClose;
			BMenuItem*			fAbout;
			BMenuItem*			fQuit;
			BMenu*				fActionMenu;
			BMenuItem*			fSelectAll;
			BMenuItem*			fSearch;
			BMenuItem*			fTrimSelection;
			BMenuItem*			fCopyText;
			BMenuItem*			fSelectInTracker;
			BMenuItem*			fOpenSelection;
			BMenu*				fPreferencesMenu;
			BMenuItem*			fRecurseLinks;
			BMenuItem*			fRecurseDirs;
			BMenuItem*			fSkipDotDirs;
			BMenuItem*			fCaseSensitive;
			BMenuItem*			fEscapeText;
			BMenuItem*			fTextOnly;
			BMenuItem*			fInvokePe;
			BMenuItem*			fShowLinesMenuitem;
			BMenu*				fHistoryMenu;
			BMenu*				fEncodingMenu;
			BMenuItem*			fUTF8;
			BMenuItem*			fShiftJIS;
			BMenuItem*			fEUC;
			BMenuItem*			fJIS;

			BCheckBox*			fShowLinesCheckbox;
			BButton*			fButton;

			Grepper*			fGrepper;
			BString				fOldPattern;
			Model*				fModel;
			bigtime_t			fLastNodeMonitorEvent;
			ChangesIterator*	fChangesIterator;
			BMessageRunner*		fChangesPulse;

			BFilePanel*			fFilePanel;
};

#endif // GREP_WINDOW_H
