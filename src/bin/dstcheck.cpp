/* 
 * Copyright 2004, Jérôme Duval, jerome.duval@free.fr.
 * Distributed under the terms of the MIT License.
 */


#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <String.h>
#include <TextView.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "dstcheck"


const uint32 TIMEDALERT_UPDATE = 'taup';

class TimedAlert : public BAlert {
	public:
		TimedAlert(const char *title, const char *text, const char *button1,
			const char *button2 = NULL, const char *button3 = NULL, 
			button_width width = B_WIDTH_AS_USUAL, alert_type type = B_INFO_ALERT);
		void MessageReceived(BMessage *);
		void Show();

		static void GetLabel(BString &string);

	private:
		BMessageRunner *fRunner;
};


TimedAlert::TimedAlert(const char *title, const char *text, const char *button1,
		const char *button2, const char *button3,
		button_width width, alert_type type)
	: BAlert(title, text, button1, button2, button3, width, type),
	fRunner(NULL)
{
}


void
TimedAlert::Show()
{
	fRunner = new BMessageRunner(this, new BMessage(TIMEDALERT_UPDATE), 60000000);
	SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
	BAlert::Show();
}


void 
TimedAlert::MessageReceived(BMessage *msg)
{
	if (msg->what == TIMEDALERT_UPDATE) {
		BString string;
		GetLabel(string);	
		TextView()->SetText(string.String());
	} else
		BAlert::MessageReceived(msg);
}


void
TimedAlert::GetLabel(BString &string)
{
	string = B_TRANSLATE("Attention!\n\nBecause of the switch from daylight "
		"saving time, your computer's clock may be an hour off. Currently, "
		"your computer thinks it is ");

	time_t t;
	struct tm tm;
	char timestring[15];
	time(&t);
	localtime_r(&t, &tm);
	
	BCountry* here;
	be_locale_roster->GetDefaultCountry(&here);
	
	here->FormatTime(timestring, 15, t, false);
	
	string += timestring;

	string += B_TRANSLATE(".\n\nIs this the correct time?");
}


//	#pragma mark -


int
main(int argc, char **argv)
{
	BCatalog fCatalog;
	time_t t;
	struct tm tm;
	tzset();
	time(&t);
	localtime_r(&t, &tm);	

	char path[B_PATH_NAME_LENGTH];	
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, path, B_PATH_NAME_LENGTH) != B_OK) {
		fprintf(stderr, "%s: can't find settings directory\n", argv[0]);
		exit(1);
	}

	strcat(path, "/time_dststatus");
	bool newFile = false;
	bool dst = false;
	int fd = open(path, O_RDWR | O_EXCL | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		newFile = false;
		fd = open(path, O_RDWR);
		if (fd < 0) {
			perror("couldn't open dst status settings file");
			exit(1);
		}

		char dst_byte;
		read(fd, &dst_byte, 1);

		dst = dst_byte == '1';
	} else {
		dst = tm.tm_isdst;
	}

	if (dst != tm.tm_isdst || argc > 1) {
		BApplication app("application/x-vnd.Haiku-cmd-dstconfig");
		be_locale->GetAppCatalog(&fCatalog);

		BString string;
		TimedAlert::GetLabel(string);

		int32 index = (new TimedAlert("timedAlert", string.String(),
			B_TRANSLATE("Ask me later"), B_TRANSLATE("Yes"),
			B_TRANSLATE("No")))->Go();
		if (index == 0)
			exit(0);

		if (index == 2) {
			index = (new BAlert("dstcheck",
				B_TRANSLATE("Would you like to set the clock using the Time and"
				"\nDate preference utility?"), 
				B_TRANSLATE("No"), B_TRANSLATE("Yes")))->Go();

			if (index == 1)
				be_roster->Launch("application/x-vnd.Haiku-Time");
		}
	}

	lseek(fd, 0, SEEK_SET);
	char dst_byte = tm.tm_isdst ? '1' : '0';
	write(fd, &dst_byte, 1);
	close(fd);

	return 0;
}

