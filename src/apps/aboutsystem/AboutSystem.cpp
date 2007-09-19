/*
 * Copyright (c) 2005-2007, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Author:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 */


#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <Resources.h>
#include <Screen.h>
#include <ScrollView.h>
#include <String.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include <cpu_type.h>

#include <stdio.h>
#include <time.h>
#include <sys/utsname.h>


#define SCROLL_CREDITS_VIEW 'mviv'


static const char *UptimeToString(char string[], size_t size);
static const char *MemUsageToString(char string[], size_t size);


class AboutApp : public BApplication {
	public:
		AboutApp(void);
};

class AboutWindow : public BWindow {
	public:
				AboutWindow(void);
		bool	QuitRequested(void);
};

class AboutView : public BView {
	public:
				AboutView(const BRect &r);
				~AboutView(void);
				
		virtual void AttachedToWindow();
		virtual void Pulse();
		
		virtual void FrameResized(float width, float height);
		virtual void Draw(BRect update);
		virtual void MessageReceived(BMessage *msg);
		virtual void MouseDown(BPoint pt);

	private:
		BStringView		*fMemView;
		BStringView		*fUptimeView;
		BView			*fInfoView;
		BTextView		*fCreditsView;
		
		BBitmap			*fLogo;
		
		BPoint			fDrawPoint;
		
		bigtime_t		fLastActionTime;
		BMessageRunner	*fScrollRunner;
};


//	#pragma mark -


AboutApp::AboutApp(void)
	: BApplication("application/x-vnd.Haiku-About")
{
	AboutWindow *window = new AboutWindow();
	window->Show();
}


//	#pragma mark -


AboutWindow::AboutWindow()
	: BWindow(BRect(0, 0, 500, 300), "About This System", B_TITLED_WINDOW, 
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE)
{
	AddChild(new AboutView(Bounds()));

	MoveTo((BScreen().Frame().Width() - Bounds().Width()) / 2,
		(BScreen().Frame().Height() - Bounds().Height()) / 2 );
}


bool
AboutWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


AboutView::AboutView(const BRect &rect)
	: BView(rect, "aboutview", B_FOLLOW_ALL, B_WILL_DRAW | B_PULSE_NEEDED),
	fLastActionTime(system_time()),
	fScrollRunner(NULL)
{
	fLogo = BTranslationUtils::GetBitmap('PNG ', "haikulogo.png");
	if (fLogo) {
		fDrawPoint.x = (225-fLogo->Bounds().Width()) / 2;
		fDrawPoint.y = 0;
	}

	// Begin Construction of System Information controls

	font_height height;
	float labelHeight, textHeight;

	system_info systemInfo;
	get_system_info(&systemInfo);

	be_plain_font->GetHeight(&height);
	textHeight = height.ascent + height.descent + height.leading;

	be_bold_font->GetHeight(&height);
	labelHeight = height.ascent + height.descent + height.leading;

	BRect r(0, 0, 225, Bounds().bottom);
	if (fLogo)
		r.OffsetBy(0, fLogo->Bounds().Height());

	fInfoView = new BView(r, "infoview", B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM, B_WILL_DRAW);
	fInfoView->SetViewColor(235, 235, 235);
	AddChild(fInfoView);

	// Add all the various labels for system infomation

	BStringView *stringView;

	// OS Version
	r.Set(5, 5, 224, labelHeight + 5);
	stringView = new BStringView(r, "oslabel", "Version:");
	stringView->SetFont(be_bold_font);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	// we update "labelHeight" to the actual height of the string view
	labelHeight = stringView->Bounds().Height();

	r.OffsetBy(0, labelHeight);
	r.bottom = r.top + textHeight;

	char string[256];
	strcpy(string, "Unknown");

	// the version is stored in the BEOS:APP_VERSION attribute of libbe.so
	BPath path;
	if (find_directory(B_BEOS_LIB_DIRECTORY, &path) == B_OK) {
		path.Append("libbe.so");

		BAppFileInfo appFileInfo;
		version_info versionInfo;
		BFile file;
		if (file.SetTo(path.Path(), B_READ_ONLY) == B_OK
			&& appFileInfo.SetTo(&file) == B_OK
			&& appFileInfo.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND) == B_OK
			&& versionInfo.short_info[0] != '\0')
			strcpy(string, versionInfo.short_info);
	}

	// Add revision from uname() info
	utsname unameInfo;
	if (uname(&unameInfo) == 0) {
		long revision;
		if (sscanf(unameInfo.version, "r%ld", &revision) == 1) {
			char version[16];
			snprintf(version, sizeof(version), "%ld", revision);
			strlcat(string, " (Revision ", sizeof(string));
			strlcat(string, version, sizeof(string));
			strlcat(string, ")", sizeof(string));
		}
	}

	stringView = new BStringView(r, "ostext", string);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	// CPU count, type and clock speed
	r.OffsetBy(0, textHeight * 1.5);
	r.bottom = r.top + labelHeight;

	if (systemInfo.cpu_count > 1)
		sprintf(string, "%ld Processors:", systemInfo.cpu_count);
	else
		strcpy(string, "Processor:");

	stringView = new BStringView(r, "cpulabel", string);
	stringView->SetFont(be_bold_font);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();


	BString cpuType;
	cpuType << get_cpu_vendor_string(systemInfo.cpu_type) 
		<< " " << get_cpu_model_string(&systemInfo);

	r.OffsetBy(0, labelHeight);
	r.bottom = r.top + textHeight;
	stringView = new BStringView(r, "cputext", cpuType.String());
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	r.OffsetBy(0, textHeight);
	r.bottom = r.top + textHeight;

	int32 clockSpeed = get_rounded_cpu_speed();
	if (clockSpeed < 1000)
		sprintf(string,"%ld MHz", clockSpeed);
	else
		sprintf(string,"%.2f GHz", clockSpeed / 1000.0f);

	stringView = new BStringView(r, "mhztext", string);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	// RAM
	r.OffsetBy(0, textHeight * 1.5);
	r.bottom = r.top + labelHeight;
	stringView = new BStringView(r, "ramlabel", "Memory:");
	stringView->SetFont(be_bold_font);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	r.OffsetBy(0, labelHeight);
	r.bottom = r.top + textHeight;

	fMemView = new BStringView(r, "ramtext", "");
	fInfoView->AddChild(fMemView);
	//fMemView->ResizeToPreferred();
	
	fMemView->SetText(MemUsageToString(string, sizeof(string)));

	// Kernel build time/date
	r.OffsetBy(0, textHeight * 1.5);
	r.bottom = r.top + labelHeight;
	stringView = new BStringView(r, "kernellabel", "Kernel:");
	stringView->SetFont(be_bold_font);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	r.OffsetBy(0, labelHeight);
	r.bottom = r.top + textHeight;

	snprintf(string, sizeof(string), "%s %s",
		systemInfo.kernel_build_date, systemInfo.kernel_build_time);

	stringView = new BStringView(r, "kerneltext", string);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	// Uptime
	r.OffsetBy(0, textHeight * 1.5);
	r.bottom = r.top + labelHeight;
	stringView = new BStringView(r, "uptimelabel", "Time Running:");
	stringView->SetFont(be_bold_font);
	fInfoView->AddChild(stringView);
	stringView->ResizeToPreferred();

	r.OffsetBy(0, labelHeight);
	r.bottom = r.top + textHeight;

	fUptimeView = new BStringView(r, "uptimetext", "");
	fInfoView->AddChild(fUptimeView);
	// string width changes, so we don't do ResizeToPreferred()

	fUptimeView->SetText(UptimeToString(string, sizeof(string)));
	
	// Begin construction of the credits view
	r = Bounds();
	r.left += fInfoView->Bounds().right + 1;
	r.right -= B_V_SCROLL_BAR_WIDTH;

	fCreditsView = new BTextView(r, "credits",
		r.OffsetToCopy(0, 0).InsetByCopy(5, 5), B_FOLLOW_ALL);
	fCreditsView->SetFlags(fCreditsView->Flags() | B_FRAME_EVENTS );
	fCreditsView->SetStylable(true);
	fCreditsView->MakeEditable(false);
	fCreditsView->SetWordWrap(true);

	BScrollView *creditsScroller = new BScrollView("creditsScroller",
		fCreditsView, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS, false, true, B_PLAIN_BORDER);
	AddChild(creditsScroller);

	rgb_color darkgrey = { 100, 100, 100, 255 };
	rgb_color haikuGreen = { 42, 131, 36, 255 };
	rgb_color haikuOrange = { 255, 69, 0, 255 };
	rgb_color haikuYellow = { 255, 176, 0, 255 };
	rgb_color linkBlue = { 80, 80, 200, 255 };

	BFont font(be_bold_font);
	font.SetSize(font.Size() + 4);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuGreen);
	fCreditsView->Insert("Haiku\n");

	font.SetSize(be_bold_font->Size());
	font.SetFace(B_BOLD_FACE | B_ITALIC_FACE);

	time_t time = ::time(NULL);
	struct tm* tm = localtime(&time);
	int32 year = tm->tm_year + 1900;
	if (year < 2007)
		year = 2007;
	snprintf(string, sizeof(string),
		"Copyright " B_UTF8_COPYRIGHT "2001-%ld Haiku, Inc.\n", year);

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(string);

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("http://haiku-os.org\n\n");

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuOrange);
	fCreditsView->Insert("Team Leads:\n");

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Axel Dörfler\n"
		"Phil Greenway\n"
		"Philippe Houdoin\n"
		"Waldemar Kornewald\n"
		"Marcus Overhagen\n"
		"Ingo Weinhold\n"
		"Jonathan Yoder\n"
		"\n");

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuOrange);
	fCreditsView->Insert("Developers:\n");

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Ithamar R. Adema\n"
		"Stephan Aßmus\n"
		"Andrew Bachmann\n"
		"Stefano Ceccherini\n"
		"Rudolf Cornelissen\n"
		"Jérôme Duval\n"
		"Waldemar Kornewald\n"
		"Ryan Leavengood\n"
		"Michael Lotz\n"
		"Michael Pfeiffer\n"
		"Niels Reedijk\n"
		"François Revol\n"
		"Hugo Santos\n"
		"Bryan Varner\n"
		"Siarzhuk Zharski\n"
		"\n");

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuOrange);
	fCreditsView->Insert("Contributors:\n");

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Bruno G. Albuquerque\n"
		"Andrea Anzani\n"
		"Bruce Cameron\n"
		"Greg Crain\n"
		"Tyler Dauwalder\n"
		"Oliver Ruiz Dorantes\n"
		"John Drinkwater\n"
		"Cian Duffy\n"
		"Marc Flerackers\n"
		"Daniel Furrer\n"
		"Troeglazov Gerasim\n"
		"Matthijs Hollemans\n"
		"Morgan Howe\n"
		"Erik Jaesler\n"
		"Carwyn Jones\n"
		"Vasilis Kaoutsis\n"
		"Euan Kirkhope\n"
		"Jan Klötzke\n"
		"Marcin Konicki\n"
		"Kurtis Kopf\n"
		"Thomas Kurschel\n"
		"Elad Lahav\n"
		"Santiago Lema\n"
		"Oscar Lesta\n"
		"Jerome Leveque\n"
		"Graham MacDonald\n"
		"Brian Matzon\n"
		"Christopher ML Zumwalt May\n"
		"Andrew McCall\n"
		"David McPaul\n"
		"Michele (zuMi)\n"
		"Misza\n"
		"MrSiggler\n"
		"Alan Murta\n"
		"Frans Van Nispen\n"
		"Adi Oanca\n"
		"Pahtz\n"
		"Michael Paine\n"
		"Michael Phipps\n"
		"Jeremy Rand\n"
		"Hartmut Reh\n"
		"David Reid\n"
		"Daniel Reinhold\n"
		"Samuel Rodriguez Perez\n"
		"Thomas Roell\n"
		"Rafael Romo\n"
		"Reznikov Sergei\n"
		"Zousar Shaker\n"
		"Jonas Sundström\n"
		"Daniel Switkin\n"
		"Atsushi Takamatsu\n"
		"Oliver Tappe\n"
		"Jason Vandermark\n"
		"Sandor Vroemisse\n"
		"Nathan Whitehorn\n"
		"Michael Wilber\n"
		"Ulrich Wimboeck\n"
		"Gabe Yoder\n"
		"Gerald Zajac\n"
		"Łukasz Zemczak\n"
		"JiSheng Zhang\n"
		"\n" B_UTF8_ELLIPSIS " and probably some more we forgot to mention (sorry!)"
		"\n\n");

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuOrange);
	fCreditsView->Insert("Special Thanks To:\n");

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert("Travis Geiselbrecht (and his NewOS kernel)\n");
	fCreditsView->Insert("Michael Phipps (project founder)\n\n");

	font.SetSize(be_bold_font->Size() + 4);
	font.SetFace(B_BOLD_FACE);
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuGreen);
	fCreditsView->Insert("\nCopyrights\n\n");

	font.SetSize(be_bold_font->Size());
	font.SetFace(B_BOLD_FACE | B_ITALIC_FACE);

	// GNU copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("The GNU Project\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert("Contains software from the GNU Project, "
		"released under the GPL and LGPL licences:\n");
	fCreditsView->Insert("	- GNU C Library,\n");
	fCreditsView->Insert("	- GNU coretools, diffutils, findutils, gawk, bison, m4, make,\n");
	fCreditsView->Insert("	- Bourne Again Shell.\n");
	fCreditsView->Insert("Copyright " B_UTF8_COPYRIGHT " The Free Software Foundation.\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("www.gnu.org\n\n");

	// FFMpeg copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("FFMpeg libavcodec\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert("Copyright " B_UTF8_COPYRIGHT " 2000-2007 Fabrice Bellard, et al.\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("www.ffmpeg.org\n\n");

	// AGG copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("AntiGrain Geometry\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert("Copyright " B_UTF8_COPYRIGHT " 2002-2006 Maxim Shemanarev (McSeem).\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("www.antigrain.com\n\n");

	// PDFLib copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("PDFLib\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1997-2006 PDFlib GmbH and Thomas Merz. "
		"All rights reserved.\n"
		"PDFlib and the PDFlib logo are registered trademarks of PDFlib GmbH.\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("www.pdflib.com\n\n");

	// FreeType copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("FreeType2\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert("Portions of this software are copyright " B_UTF8_COPYRIGHT " 1996-2006 The FreeType"
		" Project.  All rights reserved.\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("www.freetype.org\n\n");

	// Mesa3D (http://www.mesa3d.org) copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("Mesa\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1999-2006 Brian Paul. "
		"Mesa3D project.  All rights reserved.\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &linkBlue);
	fCreditsView->Insert("www.mesa3d.org\n\n");

	// SGI's GLU implementation copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("GLU\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1991-2000 Silicon Graphics, Inc. "
		"SGI's Software FreeB license.  All rights reserved.\n\n");

	// GLUT implementation copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("GLUT\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1994-1997 Mark Kilgard. "
		"All rights reserved.\n"
		"Copyright " B_UTF8_COPYRIGHT " 1997 Be Inc.\n"
		"Copyright " B_UTF8_COPYRIGHT " 1999 Jake Hamby. \n\n");

	// OpenGroup & DEC (BRegion backend) copyright
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("BRegion backend (XFree86)\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1987, 1988, 1998  The Open Group.\n"
		"Copyright " B_UTF8_COPYRIGHT " 1987, 1988 Digital Equipment Corporation, Maynard, Massachusetts.\n"
		"All rights reserved.\n\n");

	// Konatu font
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("Konatu font\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 2002- MASUDA mitiya.\n"
		"MIT license. All rights reserved.\n\n");

	// expat copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("expat\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1998, 1999, 2000 Thai Open Source Software Center Ltd and Clark Cooper.\n"
		"Copyright " B_UTF8_COPYRIGHT " 2001, 2002, 2003 Expat maintainers.\n\n");

	// zlib copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("zlib\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1995-2004 Jean-loup Gailly and Mark Adler.\n\n");

	// zip copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("Info-ZIP\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1990-2002 Info-ZIP. All rights reserved.\n\n");

	// bzip2 copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("bzip2\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " 1996-2005 Julian R Seward. All rights reserved.\n\n");

	// VIM copyrights
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &haikuYellow);
	fCreditsView->Insert("Vi IMproved\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &darkgrey);
	fCreditsView->Insert(
		"Copyright " B_UTF8_COPYRIGHT " Bram Moolenaar et al.\n\n");

}


AboutView::~AboutView(void)
{
	delete fScrollRunner;
}


void
AboutView::AttachedToWindow(void)
{
	BView::AttachedToWindow();
	Window()->SetPulseRate(500000);
	SetEventMask(B_POINTER_EVENTS);
}


void
AboutView::MouseDown(BPoint pt)
{
	BRect r(92, 26, 105, 31);
	if (r.Contains(pt))
		printf("Easter Egg\n");
	
	if (Bounds().Contains(pt)) {
		fLastActionTime = system_time();
		delete fScrollRunner;
		fScrollRunner = NULL;
	}
}


void
AboutView::FrameResized(float width, float height)
{
	BRect r = fCreditsView->Bounds();
	r.OffsetTo(B_ORIGIN);
	r.InsetBy(3, 3);
	fCreditsView->SetTextRect(r);
}


void
AboutView::Draw(BRect update)
{
	if (fLogo)
		DrawBitmap(fLogo, fDrawPoint);
}


void
AboutView::Pulse(void)
{
	char string[255];
	
	fUptimeView->SetText(UptimeToString(string, sizeof(string)));
	fMemView->SetText(MemUsageToString(string, sizeof(string)));
	
	if (fScrollRunner == NULL && (system_time() > fLastActionTime + 10000000)) {
		BMessage message(SCROLL_CREDITS_VIEW);
		//fScrollRunner = new BMessageRunner(this, &message, 300000, -1);
	}
}


void
AboutView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case SCROLL_CREDITS_VIEW:
		{
			BScrollBar *scrollBar = fCreditsView->ScrollBar(B_VERTICAL);
			if (scrollBar == NULL)
				break;
			float max, min;
			scrollBar->GetRange(&min, &max);
			if (scrollBar->Value() < max)
				fCreditsView->ScrollBy(0, 5);
			
			break;
		}
		
		default:
			BView::MessageReceived(msg);
			break;
	}
}


//	#pragma mark -


static const char *
MemUsageToString(char string[], size_t size)
{
	system_info systemInfo;
	
	if (get_system_info(&systemInfo) < B_OK)
		return "Unknown";
	
	snprintf(string, size, "%d MB total, %d MB used (%d%%)", 
			int(systemInfo.max_pages / 256.0f + 0.5f),
			int(systemInfo.used_pages / 256.0f + 0.5f),
			int(100 * systemInfo.used_pages / systemInfo.max_pages));
	
	return string;
}


static const char *
UptimeToString(char string[], size_t size)
{
	int64 days, hours, minutes, seconds, remainder;
	int64 systime = system_time();

	days = systime / 86400000000LL;
	remainder = systime % 86400000000LL;

	hours = remainder / 3600000000LL;
	remainder = remainder % 3600000000LL;

	minutes = remainder / 60000000;
	remainder = remainder % 60000000;

	seconds = remainder / 1000000;

	char *str = string;
	if (days) {
		str += snprintf(str, size, "%lld day%s",days, days > 1 ? "s" : "");
	}
	if (hours) {
		str += snprintf(str, size - strlen(string), "%s%lld hour%s",
				str != string ? ", " : "",
				hours, hours > 1 ? "s" : "");	
	}
	if (minutes) {
		str += snprintf(str, size - strlen(string), "%s%lld minute%s",
				str != string ? ", " : "",
				minutes, minutes > 1 ? "s" : "");	
	}
	
	if (seconds || str == string) {
		// Haiku would be well-known to boot very fast.
		// Let's be ready to handle below minute uptime, zero second included ;-)  
		str += snprintf(str, size - strlen(string), "%s%lld second%s",
				str != string ? ", " : "",
				seconds, seconds > 1 ? "s" : "");	
	}

	return string;
}


int
main()
{
	AboutApp app;
	app.Run();
	return 0;
}

