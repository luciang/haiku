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
#ifndef MODEL_H
#define MODEL_H

#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <List.h>
#include <Menu.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>

#include "GlobalDefs.h"


enum {
	MSG_START_CANCEL = 1000,
	MSG_RECURSE_LINKS,
	MSG_RECURSE_DIRS,
	MSG_SKIP_DOT_DIRS,
	MSG_CASE_SENSITIVE,
	MSG_ESCAPE_TEXT,
	MSG_TEXT_ONLY,
	MSG_INVOKE_PE,
	MSG_MENU_SHOW_LINES,
	MSG_CHECKBOX_SHOW_LINES,
	MSG_SEARCH_TEXT,
	MSG_INVOKE_ITEM,
	MSG_SELECT_HISTORY,
	MSG_NODE_MONITOR_PULSE,
	MSG_START_NODE_MONITORING,

	MSG_REPORT_FILE_NAME,
	MSG_REPORT_RESULT,
	MSG_REPORT_ERROR,
	MSG_SEARCH_FINISHED,

	MSG_NEW_WINDOW,
	MSG_OPEN_PANEL,
	MSG_REFS_RECEIVED,
	MSG_TRY_QUIT,
	MSG_QUIT_NOW,
	
	MSG_TRIM_SELECTION,
	MSG_COPY_TEXT,
	MSG_SELECT_IN_TRACKER,
	MSG_SELECT_ALL,
	MSG_OPEN_SELECTION
};

enum state_t {
	STATE_IDLE = 0,
	STATE_SEARCH,
	STATE_CANCEL,
	STATE_UPDATE
};

class Model {
public:
								Model();
	
			status_t			LoadPrefs();
			status_t			SavePrefs();

			void				AddToHistory(const char* text);
			void				FillHistoryMenu(BMenu* menu) const;

public:
			// The directory we were invoked from.
			entry_ref			fDirectory;

			// The selected files we were invoked upon.
			BMessage			fSelectedFiles;

			// Whether we need to look into subdirectories.
			bool				fRecurseDirs;

			// Whether we need to follow symbolic links.
			bool				fRecurseLinks;

			// Whether we should skip subdirectories that start with a dot.
			bool				fSkipDotDirs;

			// Whether the search is case sensitive.
			bool				fCaseSensitive;

			// Whether the search pattern will be escaped.
			bool				fEscapeText;

			// Whether we look at text files only.
			bool				fTextOnly;

			// Whether we open the item in Pe and jump to the correct line.
			bool				fInvokePe;

			// Whether to show the contents of matching files.
			bool				fShowContents;

			// The dimensions of the window.
			BRect				fFrame;

			// What are we doing.
			state_t				fState;

			// Current directory of the filepanel
			BString				fFilePanelPath;

			// Grep string encoding ?
			uint32				fEncoding;
		
private:
			bool				_LoadHistory(BList& items) const;
			status_t			_SaveHistory(const BList& items) const;
			void				_FreeHistory(const BList& items) const;
			status_t			_OpenFile(BFile* file, const char* name,
									uint32 openMode = B_READ_ONLY, 
									directory_which which
										= B_USER_SETTINGS_DIRECTORY,
									BVolume* volume = NULL) const;
};

#endif // MODEL_H
