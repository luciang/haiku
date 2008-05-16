/*
 * InfoWin.cpp - Media Player for the Haiku Operating System
 *
 * Copyright (C) 2006 Marcus Overhagen <marcus@overhagen.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */
#include "InfoWin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Debug.h>
#include <MediaDefs.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>

#include "Controller.h"
#include "ControllerObserver.h"


#define NAME "File Info"
#define MIN_WIDTH 400

#define BASE_HEIGHT (32 + 32)

//const rgb_color kGreen = { 152, 203, 152, 255 };
const rgb_color kRed =   { 203, 152, 152, 255 };
const rgb_color kBlue =  {   0,   0, 220, 255 };
const rgb_color kGreen = { 171, 221, 161, 255 };
const rgb_color kBlack = {   0,   0,   0, 255 };


// should later draw an icon
class InfoView : public BView {
public:
	InfoView(BRect frame, const char *name, float divider)
		: BView(frame, name, B_FOLLOW_ALL,
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
		, fDivider(divider)
		{ }
	virtual ~InfoView()
		{ }
	void Draw(BRect updateRect);
	float fDivider;
};


void
InfoView::Draw(BRect updateRect)
{
	SetHighColor(kGreen);
	BRect r(Bounds());
	r.right = r.left + fDivider;
	FillRect(r);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	r.left = r.right;
	FillRect(r);
}


// #pragma mark -


InfoWin::InfoWin(BPoint leftTop, Controller* controller)
	: BWindow(BRect(leftTop.x, leftTop.y, leftTop.x + MIN_WIDTH - 1,
		leftTop.y + 300), NAME, B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_RESIZABLE)
	, fController(controller)
	, fControllerObserver(new ControllerObserver(this,
		OBSERVE_FILE_CHANGES | OBSERVE_TRACK_CHANGES | OBSERVE_STAT_CHANGES))
{
	BRect rect = Bounds();

	// accomodate for big fonts
	float div = max_c(2 * 32, be_plain_font->StringWidth("Display Mode") + 10);

	fInfoView = new InfoView(rect, "background", div);
	fInfoView->SetViewColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	AddChild(fInfoView);

	BFont bigFont(be_plain_font);
	bigFont.SetSize(bigFont.Size() + 6);
	font_height fh;
	bigFont.GetHeight(&fh);
	fFilenameView = new BStringView(BRect(div + 10, 20,
										  rect.right - 10,
										  20 + fh.ascent + 5),
									"filename", "");
	fFilenameView->SetFont(&bigFont);
	fFilenameView->SetViewColor(fInfoView->ViewColor());
	fFilenameView->SetLowColor(fInfoView->ViewColor());
#ifdef B_BEOS_VERSION_DANO /* maybe we should support that as well ? */
	fFilenameView->SetTruncation(B_TRUNCATE_END);
#endif
	AddChild(fFilenameView);
									
	
	rect.top = BASE_HEIGHT;

	BRect lr(rect);
	BRect cr(rect);
	lr.right = div - 1;
	cr.left = div + 1;
	BRect tr;
	tr = lr.OffsetToCopy(0, 0).InsetByCopy(5, 1);
	fLabelsView = new BTextView(lr, "labels", tr, B_FOLLOW_BOTTOM);
	fLabelsView->SetViewColor(kGreen);
	fLabelsView->SetAlignment(B_ALIGN_RIGHT);
	fLabelsView->SetWordWrap(false);
	AddChild(fLabelsView);
	tr = cr.OffsetToCopy(0, 0).InsetByCopy(10, 1);
	fContentsView = new BTextView(cr, "contents", tr, B_FOLLOW_BOTTOM);
	fContentsView->SetWordWrap(false);
	AddChild(fContentsView);

	fLabelsView->MakeSelectable();
	fContentsView->MakeSelectable();

	fController->AddListener(fControllerObserver);
	Update();

	Show();
}


InfoWin::~InfoWin()
{
	fController->RemoveListener(fControllerObserver);
	delete fControllerObserver;

	//fInfoListView->MakeEmpty();
	//delete [] fInfoItems;
}


// #pragma mark -


void
InfoWin::FrameResized(float new_width, float new_height)
{
}


void
InfoWin::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_CONTROLLER_FILE_FINISHED:
			break;
		case MSG_CONTROLLER_FILE_CHANGED:
			Update(INFO_ALL);
			break;
		case MSG_CONTROLLER_VIDEO_TRACK_CHANGED:
			Update(/*INFO_VIDEO | INFO_STATS*/INFO_ALL);
			break;
		case MSG_CONTROLLER_AUDIO_TRACK_CHANGED:
			Update(/*INFO_AUDIO | INFO_STATS*/INFO_ALL);
			break;
		case MSG_CONTROLLER_VIDEO_STATS_CHANGED:
		case MSG_CONTROLLER_AUDIO_STATS_CHANGED:
			Update(/*INFO_STATS*/INFO_ALL);
			break;
		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


bool
InfoWin::QuitRequested()
{
	Hide();
	return false;
}


void
InfoWin::Pulse()
{
	if (IsHidden())
		return;
	Update(INFO_STATS);
}


// #pragma mark -


void
InfoWin::ResizeToPreferred()
{
#if 0
	float height = BASE_HEIGHT;
	for (int i = 0; BListItem *li = fInfoListView->ItemAt(i); i++) {
		height += li->Height();
	}
	ResizeTo(Bounds().Width(), height);
#endif
}


void
InfoWin::Update(uint32 which)
{
printf("InfoWin::Update(0x%08lx)\n", which);
	fLabelsView->SetText("");
	fContentsView->SetText("");
	fLabelsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &kBlue);
	fLabelsView->Insert(" ");
	fContentsView->SetFontAndColor(be_plain_font, B_FONT_ALL);
//	fContentsView->Insert("");

	fLabelsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &kRed);

	status_t err;
	// video track format information
	if ((which & INFO_VIDEO) && fController->VideoTrackCount() > 0) {
		fLabelsView->Insert("Video\n\n\n\n");
		BString s;
		media_format format;
		media_raw_video_format videoFormat;
		err = fController->GetEncodedVideoFormat(&format);
		if (err < B_OK) {
			s << "(" << strerror(err) << ")";
		} else if (format.type == B_MEDIA_ENCODED_VIDEO) {
			videoFormat = format.u.encoded_video.output;
			media_codec_info mci;
			err = fController->GetVideoCodecInfo(&mci);
			if (err < B_OK) {
				s << "Haiku Media Kit:\n(" << strerror(err) << ")";
				if (format.user_data_type == B_CODEC_TYPE_INFO) {
					s << (char *)format.user_data << " not supported";
				}
			} else
				s << mci.pretty_name; //<< "(" << mci.short_name << ")";
		} else if (format.type == B_MEDIA_RAW_VIDEO) {
			videoFormat = format.u.raw_video;
			s << "raw video";
		} else
			s << "unknown format";
		s << "\n";
		s << format.Width() << " x " << format.Height();
		// encoded has output as 1st field...
		s << ", " << videoFormat.field_rate << " fps";
		s << "\n\n";
		fContentsView->Insert(s.String());
	}

	// audio track format information
	if ((which & INFO_AUDIO) && fController->AudioTrackCount() > 0) {
		fLabelsView->Insert("Audio\n\n\n\n");
		BString s;
		media_format format;
		media_raw_audio_format audioFormat;
		err = fController->GetEncodedAudioFormat(&format);
		//string_for_format(format, buf, sizeof(buf));
		//printf("%s\n", buf);
		if (err < 0) {
			s << "(" << strerror(err) << ")";
		} else if (format.type == B_MEDIA_ENCODED_AUDIO) {
			audioFormat = format.u.encoded_audio.output;
			media_codec_info mci;
			err = fController->GetAudioCodecInfo(&mci);
			if (err < 0) {
				s << "Haiku Media Kit:\n(" << strerror(err) << ") ";
				if (format.user_data_type == B_CODEC_TYPE_INFO) {
					s << (char *)format.user_data << " not supported";
				}
			} else
				s << mci.pretty_name; //<< "(" << mci.short_name << ")";
		} else if (format.type == B_MEDIA_RAW_AUDIO) {
			audioFormat = format.u.raw_audio;
			s << "raw audio";
		} else
			s << "unknown format";
		s << "\n";
		uint32 bitsPerSample = 8 * (audioFormat.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK);
		uint32 channelCount = audioFormat.channel_count;
		float sr = audioFormat.frame_rate;

		if (bitsPerSample > 0)
			s << bitsPerSample << " Bit ";
		if (channelCount == 1)
			s << "Mono";
		else if (channelCount == 2)
			s << "Stereo";
		else
			s << channelCount << "Channels";
		s << ", ";
		if (sr > 0.0)
			s << sr / 1000;
		else
			s << "??";
		s<< " kHz";
		s << "\n\n";
		fContentsView->Insert(s.String());
	}

	// statistics
	if ((which & INFO_STATS) && fController->HasFile()) {
		fLabelsView->Insert("Duration\n");
		BString s;
		bigtime_t d = fController->Duration();
		bigtime_t v;

		//s << d << "µs; ";
		
		d /= 1000;
		
		v = d / (3600 * 1000);
		d = d % (3600 * 1000);
		bool hours = v > 0;
		if (hours)
			s << v << ":";
		v = d / (60 * 1000);
		d = d % (60 * 1000);
		s << v << ":";
		v = d / 1000;
		s << v;
		if (hours)
			s << " h";
		else
			s << " min";
		s << "\n";
		fContentsView->Insert(s.String());
		// TODO: demux/video/audio/... perfs (Kb/s)
		
		fLabelsView->Insert("Display Mode\n");
		if (fController->IsOverlayActive())
			fContentsView->Insert("Overlay\n");
		else
			fContentsView->Insert("DrawBitmap\n");
		
		fLabelsView->Insert("\n");
		fContentsView->Insert("\n\n");
	}

	if (which & INFO_TRANSPORT) {
		// Transport protocol info (file, http, rtsp, ...)
	}

	if (which & INFO_FILE) {
		if (fController->HasFile()) {
			media_file_format ff;
			BString s;
			if (fController->GetFileFormatInfo(&ff) == B_OK) {
				fLabelsView->Insert("Container\n");
				s << ff.pretty_name;
				s << "\n";
				fContentsView->Insert(s.String());
			} else
				fContentsView->Insert("\n");
			fLabelsView->Insert("Location\n");
			if (fController->GetLocation(&s) < B_OK)
				s = "<unknown>";
			s << "\n";
			fContentsView->Insert(s.String());
			if (fController->GetName(&s) < B_OK)
				s = "<unnamed media>";
			fFilenameView->SetText(s.String());
		} else {
			fFilenameView->SetText("<no media>");
		}
	}

	if ((which & INFO_COPYRIGHT) && fController->HasFile()) {
		
		BString s;
		if (fController->GetCopyright(&s) == B_OK && s.Length() > 0) {
			fLabelsView->Insert("Copyright\n\n");
			s << "\n\n";
			fContentsView->Insert(s.String());
		}
	}

	fController->Unlock();
	
	ResizeToPreferred();
}
