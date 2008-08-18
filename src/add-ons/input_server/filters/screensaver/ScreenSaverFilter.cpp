/*
 * Copyright 2003-2007, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Phipps
 *		Jérôme Duval, jerome.duval@free.fr
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "ScreenSaverFilter.h"

#include <Application.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <Roster.h>
#include <Screen.h>

#include <Debug.h>

#include <new>

#define CALLED() SERIAL_PRINT(("%s\n", __PRETTY_FUNCTION__))


static const int32 kNeverBlankCornerSize = 10;
static const int32 kBlankCornerSize = 5;
static const bigtime_t kCornerDelay = 1000000LL;
	// one second

static const int32 kMsgCheckTime = 'SSCT';
static const int32 kMsgCornerInvoke = 'Scin';


extern "C" _EXPORT BInputServerFilter* instantiate_input_filter();


/** required C func to build the IS Filter */

BInputServerFilter*
instantiate_input_filter()
{
	return new (std::nothrow) ScreenSaverFilter();
}


//	#pragma mark -


ScreenSaverController::ScreenSaverController(ScreenSaverFilter *filter)
	: BLooper("screensaver controller", B_LOW_PRIORITY),
	fFilter(filter)
{
	CALLED();
}


void
ScreenSaverController::MessageReceived(BMessage *message)
{
	CALLED();
	SERIAL_PRINT(("what %lx\n", message->what));

	switch (message->what) {
		case B_NODE_MONITOR:
			fFilter->ReloadSettings();
			break;
		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char *signature;
			if (message->FindString("be:signature", &signature) == B_OK 
				&& strcasecmp(signature, SCREEN_BLANKER_SIG) == 0) {
				fFilter->SetEnabled(message->what == B_SOME_APP_LAUNCHED);
			}
			SERIAL_PRINT(("mime_sig %s\n", signature));
			break;
		}

		case kMsgSuspendScreenSaver:
			//fFilter->Suspend(msg);
			break;

		case kMsgCheckTime:
			fFilter->CheckTime();
			break;

		case kMsgCornerInvoke:
			fFilter->CheckCornerInvoke();
			break;

		default:
			BLooper::MessageReceived(message);
	}
}


//	#pragma mark -


ScreenSaverFilter::ScreenSaverFilter() 
	:
	fLastEventTime(0),
	fBlankTime(0),
	fSnoozeTime(0),
	fCurrentCorner(NO_CORNER),
	fEnabled(false),
	fFrameNum(0),
	fRunner(NULL),
	fCornerRunner(NULL),
	fWatchingDirectory(false), 
	fWatchingFile(false)
{
	CALLED();
	fController = new (std::nothrow) ScreenSaverController(this);
	if (fController == NULL)
		return;

	fController->Run();

	ReloadSettings();
	be_roster->StartWatching(fController);
}


ScreenSaverFilter::~ScreenSaverFilter()
{
	delete fCornerRunner;
	delete fRunner;

	if (fWatchingFile || fWatchingDirectory)
		watch_node(&fNodeRef, B_STOP_WATCHING, fController);

	be_roster->StopWatching(fController);

	if (fController->Lock())
		fController->Quit();
}


/*!
	Starts watching the settings file, or if that doesn't exist, the directory
	the settings file will be placed into.
*/
void
ScreenSaverFilter::_WatchSettings()
{
	BEntry entry(fSettings.Path().Path());
	if (entry.Exists()) {
		if (fWatchingFile)
			return;

		if (fWatchingDirectory) {
			watch_node(&fNodeRef, B_STOP_WATCHING, fController);
			fWatchingDirectory = false;
		}
		if (entry.GetNodeRef(&fNodeRef) == B_OK
			&& watch_node(&fNodeRef, B_WATCH_ALL, fController) == B_OK)
			fWatchingFile = true;
	} else {
		if (fWatchingDirectory)
			return;

		if (fWatchingFile) {
			watch_node(&fNodeRef, B_STOP_WATCHING, fController);
			fWatchingFile = false;
		}
		BEntry dir;
		if (entry.GetParent(&dir) == B_OK
			&& dir.GetNodeRef(&fNodeRef) == B_OK
			&& watch_node(&fNodeRef, B_WATCH_DIRECTORY, fController) == B_OK)
			fWatchingDirectory = true;
	}
}


void
ScreenSaverFilter::_Invoke()
{
	CALLED();
	if (fCurrentCorner == fNeverBlankCorner && fNeverBlankCorner != NO_CORNER
		|| fSettings.TimeFlags() == SAVER_DISABLED
		|| fEnabled
		|| be_roster->IsRunning(SCREEN_BLANKER_SIG))
		return;

	SERIAL_PRINT(("we run screenblanker\n"));
	if (be_roster->Launch(SCREEN_BLANKER_SIG) == B_OK)
		fEnabled = true;
}


void
ScreenSaverFilter::ReloadSettings()
{
	CALLED();
	bool isFirst = !fWatchingDirectory && !fWatchingFile;

	_WatchSettings();

	if (fWatchingDirectory && !isFirst) {
		// there is no settings file yet
		return;
	}

	delete fCornerRunner;
	delete fRunner;
	fRunner = fCornerRunner = NULL;

	fSettings.Load();

	fBlankCorner = fSettings.BlankCorner();
	fNeverBlankCorner = fSettings.NeverBlankCorner();
	fBlankTime = fSnoozeTime = fSettings.BlankTime();
	CheckTime();

	if (fBlankCorner != NO_CORNER || fNeverBlankCorner != NO_CORNER) {
		BMessage invoke(kMsgCornerInvoke);
		fCornerRunner = new (std::nothrow) BMessageRunner(fController,
			&invoke, B_INFINITE_TIMEOUT);
			// will be reset in Filter()
	}

	BMessage check(kMsgCheckTime);
	fRunner = new (std::nothrow) BMessageRunner(fController, &check, fSnoozeTime);
	if (fRunner->InitCheck() != B_OK) {
		SERIAL_PRINT(("fRunner init failed\n"));
	}
}


void
ScreenSaverFilter::_Banish() 
{
	CALLED();
	if (!fEnabled)
		return;

	SERIAL_PRINT(("we quit screenblanker\n"));

	// Don't care if it fails
	BMessenger blankerMessenger(SCREEN_BLANKER_SIG, -1, NULL);
	blankerMessenger.SendMessage(B_QUIT_REQUESTED);
	fEnabled = false;
}


void
ScreenSaverFilter::CheckTime()
{
	CALLED();
	bigtime_t now = system_time();
	if (now >= fLastEventTime + fBlankTime)  
		_Invoke();

	// TODO: this doesn't work correctly - since the BMessageRunner is not
	// restarted, the next check will be too far away

	// If the screen saver is on OR it was time to come on but it didn't (corner),
	// snooze for blankTime.
	// Otherwise, there was an event in the middle of the last snooze, so snooze
	// for the remainder.
	if (fEnabled || fLastEventTime + fBlankTime <= now)
		fSnoozeTime = fBlankTime;
	else
		fSnoozeTime = fLastEventTime + fBlankTime - now;

	if (fRunner)
		fRunner->SetInterval(fSnoozeTime);
}


void
ScreenSaverFilter::CheckCornerInvoke()
{
	bigtime_t inactivity = system_time() - fLastEventTime;

	if (fCurrentCorner == fBlankCorner && fBlankCorner != NO_CORNER
		&& inactivity >= kCornerDelay)
		_Invoke();
}


void
ScreenSaverFilter::_UpdateRectangles()
{
	// TODO: make this better if possible at all (in a clean way)
	CALLED();

	fBlankRect = _ScreenCorner(fBlankCorner, kBlankCornerSize);
	fNeverBlankRect = _ScreenCorner(fNeverBlankCorner, kNeverBlankCornerSize);
}


BRect
ScreenSaverFilter::_ScreenCorner(screen_corner corner, uint32 cornerSize)
{
	BRect frame = BScreen().Frame();

	switch (corner) {
		case UP_LEFT_CORNER:
			return BRect(frame.left, frame.top, frame.left + cornerSize - 1,
				frame.top + cornerSize - 1);

		case UP_RIGHT_CORNER:
			return BRect(frame.right - cornerSize + 1, frame.top, frame.right,
				frame.top + cornerSize - 1);

		case DOWN_RIGHT_CORNER:
			return BRect(frame.right - cornerSize + 1, frame.bottom - cornerSize + 1,
				frame.right, frame.bottom);

		case DOWN_LEFT_CORNER:
			return BRect(frame.left, frame.bottom - cornerSize + 1,
				frame.left + cornerSize - 1, frame.bottom);

		default:
			// return an invalid rectangle
			return BRect(-1, -1, -2, -2);
	}
}


filter_result
ScreenSaverFilter::Filter(BMessage *message, BList *outList)
{
	fLastEventTime = system_time();

	switch (message->what) {
		case B_MOUSE_MOVED:
		{
			BPoint where;
			if (message->FindPoint("where", &where) != B_OK)
				break;

			if ((fFrameNum++ % 32) == 0) {
				// Every so many frames, update
				// Used so that we don't update the screen coord's so often
				// Ideally, we would get a message when the screen changes.
				// R5 doesn't do this.
				_UpdateRectangles();
			}

			if (fBlankRect.Contains(where)) {
				fCurrentCorner = fBlankCorner;

				// start screen blanker after one second of inactivity
				if (fCornerRunner != NULL
					&& fCornerRunner->SetInterval(kCornerDelay) != B_OK)
					_Invoke();
				break;
			}

			if (fNeverBlankRect.Contains(where))
				fCurrentCorner = fNeverBlankCorner;
			else
				fCurrentCorner = NO_CORNER;
			break;
		}

		case B_KEY_UP:
		case B_KEY_DOWN:
		{
			// we ignore the Print-Screen key to make screen shots of
			// screen savers possible
			int32 key;
			if (fEnabled && message->FindInt32("key", &key) == B_OK && key == 0xe)
				return B_DISPATCH_MESSAGE;
		}
	}

	_Banish();
	return B_DISPATCH_MESSAGE;
}

