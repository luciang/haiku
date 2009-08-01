// Copyright 1999, Be Incorporated. All Rights Reserved.
// Copyright 2000-2004, Jun Suzuki. All Rights Reserved.
// Copyright 2007, Stephan Aßmus. All Rights Reserved.
// This file may be used under the terms of the Be Sample Code License.
#include "MediaConverterApp.h"

#include <inttypes.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Mime.h>
#include <Path.h>
#include <String.h>
#include <View.h>

#include "MediaConverterWindow.h"
#include "MediaEncoderWindow.h"
#include "MessageConstants.h"
#include "Strings.h"


using std::nothrow;

const char APP_SIGNATURE[] = "application/x-vnd.Haiku-MediaConverter";


MediaConverterApp::MediaConverterApp()
	: BApplication(APP_SIGNATURE)
	, fWin(NULL)
	, fConvertThreadID(-1)
	, fConverting(false)
	, fCancel(false)
{
	// TODO: implement settings for window pos
	fWin = new MediaConverterWindow(BRect(50, 50, 520, 555));
}


MediaConverterApp::~MediaConverterApp()
{
	if (fConvertThreadID >= 0) {
		fCancel = true;
		status_t exitValue;
		wait_for_thread(fConvertThreadID, &exitValue);
	}
}


// #pragma mark -


void
MediaConverterApp::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case FILE_LIST_CHANGE_MESSAGE:
			if (fWin->Lock()) {
				bool enable = fWin->CountSourceFiles() > 0;
				fWin->SetEnabled(enable, enable);
				fWin->Unlock();
			}
			break;

		case START_CONVERSION_MESSAGE:
			if (!fConverting)
				StartConverting();
			break;

		case CANCEL_CONVERSION_MESSAGE:
			fCancel = true;
			break;

		case CONVERSION_DONE_MESSAGE:
			fCancel = false;
			fConverting = false;
			DetachCurrentMessage();
			BMessenger(fWin).SendMessage(msg);
			break;

		default:
			BApplication::MessageReceived(msg);
	}
}


void
MediaConverterApp::ReadyToRun()
{
	fWin->Show();
	fWin->PostMessage(INIT_FORMAT_MENUS);
}

void
MediaConverterApp::RefsReceived(BMessage *msg)
{
	entry_ref ref;
	int32 i = 0;
	BMediaFile *f;
	BString errorFiles;
	int32 errors = 0;

	// from Open dialog or drag & drop

	while (msg->FindRef("refs", i++, &ref) == B_OK) {
		f = new BMediaFile(&ref/*, B_MEDIA_FILE_NO_READ_AHEAD*/);
		if (f->InitCheck() != B_OK) {
			errorFiles << ref.name << "\n";
			errors++;
			delete f;
			continue;
		}
		if (fWin->Lock()) {
			fWin->AddSourceFile(f, ref);
			fWin->Unlock();
		}
	}

	if (errors) {
		BString alertText;
		alertText << errors << ((errors > 1) ? FILES : FILE)
				  << NOTRECOGNIZE << "\n";
		alertText << errorFiles;
		BAlert *alert = new BAlert(ERROR_LOAD_STRING, alertText.String(),
						CONTINUE_STRING	, NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
	}
}


// #pragma mark -


bool
MediaConverterApp::IsConverting() const
{
	return fConverting;
}


void
MediaConverterApp::StartConverting()
{
	bool locked = fWin->Lock();

	if (locked && (fWin->CountSourceFiles() > 0)) {
		fConvertThreadID = spawn_thread(MediaConverterApp::_RunConvertEntry,
			"converter thread", B_LOW_PRIORITY, (void *)this);
		if (fConvertThreadID >= 0) {
			fConverting = true;
			fCancel = false;
			resume_thread(fConvertThreadID);
		}
	}

	if (locked) {
		fWin->Unlock();
	}
}


void
MediaConverterApp::SetStatusMessage(const char *message)
{
	if (fWin != NULL && fWin->Lock()) {
		fWin->SetStatusMessage(message);
		fWin->Unlock();
	}
}


// #pragma mark -


BEntry
MediaConverterApp::_CreateOutputFile(BDirectory directory,
	entry_ref* ref, media_file_format* outputFormat)
{
	BString name(ref->name);
	// create output file name
	int32 extIndex = name.FindLast('.');
	if (extIndex != B_ERROR)
		name.Truncate(extIndex + 1);
	else
		name.Append(".");

	name.Append(outputFormat->file_extension);

	BEntry inEntry(ref);
	BEntry outEntry;

	if (inEntry.InitCheck() == B_OK) {
		// ensure that output name is unique
		int32 len = name.Length();
		int32 i = 1;
		while (directory.Contains(name.String())) {
			name.Truncate(len);
			name << " " << i;
			i++;
		}
		outEntry.SetTo(&directory, name.String());
	}

	return outEntry;
}


int32
MediaConverterApp::_RunConvertEntry(void* castToMediaConverterApp)
{
	MediaConverterApp* app = (MediaConverterApp*)castToMediaConverterApp;
	app->_RunConvert();
	return 0;
}


void
MediaConverterApp::_RunConvert()
{
	bigtime_t start = 0;
	bigtime_t end = 0;
	int32 audioQuality = 75;
	int32 videoQuality = 75;

	if (fWin->Lock()) {
		char *a;
		start = strtoimax(fWin->StartDuration(), &a, 0) * 1000;
		end = strtoimax(fWin->EndDuration(), &a, 0) * 1000;
		audioQuality = fWin->AudioQuality();
		videoQuality = fWin->VideoQuality();
		fWin->Unlock();
	}

	int32 srcIndex = 0;

	BMediaFile *inFile(NULL), *outFile(NULL);
	BEntry outEntry;
	entry_ref inRef;
	entry_ref outRef;
	BPath path;
	BString name;

	while (!fCancel) {
		if (fWin->Lock()) {
			status_t r = fWin->GetSourceFileAt(srcIndex, &inFile, &inRef);
			if (r == B_OK) {
				media_codec_info* audioCodec;
				media_codec_info* videoCodec;
				media_file_format* fileFormat;
				fWin->GetSelectedFormatInfo(&fileFormat, &audioCodec, &videoCodec);
				BDirectory directory = fWin->OutputDirectory();
				fWin->Unlock();
				outEntry = _CreateOutputFile(directory, &inRef, fileFormat);

				// display file name

				outEntry.GetPath(&path);
				name.SetTo(path.Leaf());

				if (outEntry.InitCheck() == B_OK) {
					entry_ref outRef;
					outEntry.GetRef(&outRef);
					outFile = new BMediaFile(&outRef, fileFormat);

					name.Prepend(" '");
					name.Prepend(OUTPUT_FILE_STRING1);
					name.Append("' ");
					name.Append(OUTPUT_FILE_STRING2);
				} else {
					name.Prepend(" '");
					name.Prepend(OUTPUT_FILE_STRING3);
					name.Append("'");
				}

				if (fWin->Lock()) {
					fWin->SetFileMessage(name.String());
					fWin->Unlock();
				}

				if (outFile != NULL) {
					r = _ConvertFile(inFile, outFile, audioCodec, videoCodec,
						audioQuality, videoQuality, start, end);

					// set mime
					update_mime_info(path.Path(), false, false, false);

					fWin->Lock();
					if (r == B_OK) {
						fWin->RemoveSourceFile(srcIndex);
					} else {
						srcIndex++;
						BString error(CONVERT_ERROR_STRING);
  						error << " '" << inRef.name << "'";
						fWin->SetStatusMessage(error.String());
					}
					fWin->Unlock();
				}


			} else {
				fWin->Unlock();
				break;
			}
		} else {
			break;
		}
	}

	BMessenger(this).SendMessage(CONVERSION_DONE_MESSAGE);
}


// #pragma mark -


status_t
MediaConverterApp::_ConvertFile(BMediaFile* inFile, BMediaFile* outFile,
	media_codec_info* audioCodec, media_codec_info* videoCodec,
	int32 audioQuality, int32 videoQuality,
	bigtime_t startDuration, bigtime_t endDuration)
{
	BMediaTrack* inVidTrack = NULL;
	BMediaTrack* inAudTrack = NULL;
	BMediaTrack* outVidTrack = NULL;
	BMediaTrack* outAudTrack = NULL;

	media_format inFormat;
	media_format outAudFormat;
	media_format outVidFormat;

	media_raw_audio_format* raf = NULL;
	media_raw_video_format* rvf = NULL;

	int32 width = -1;
	int32 height = -1;

	short audioFrameSize = 1;

	uint8* videoBuffer = NULL;
	uint8* audioBuffer = NULL;

	// gather the necessary format information and construct output tracks
	int64 videoFrameCount = 0;
	int64 audioFrameCount = 0;

	status_t ret = B_OK;

	int32 tracks = inFile->CountTracks();
	for (int32 i = 0; i < tracks && (!outAudTrack || !outVidTrack); i++) {
		BMediaTrack* inTrack = inFile->TrackAt(i);
		memset(&inFormat, 0, sizeof(media_format));
		inTrack->EncodedFormat(&inFormat);
		if (inFormat.IsAudio() && (audioCodec != NULL)) {
			inAudTrack = inTrack;
			memset(&outAudFormat, 0, sizeof(media_format));
			outAudFormat.type = B_MEDIA_RAW_AUDIO;
			raf = &(outAudFormat.u.raw_audio);
			inTrack->DecodedFormat(&outAudFormat);

			audioBuffer = new uint8[raf->buffer_size];
//			audioFrameSize = (raf->format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
			audioFrameSize = (raf->format & 0xf) * raf->channel_count;
			outAudTrack = outFile->CreateTrack(&outAudFormat, audioCodec);

			if (outAudTrack != NULL) {
				if (outAudTrack->SetQuality(audioQuality / 100.0f) != B_OK
					&& fWin->Lock()) {
					fWin->SetAudioQualityLabel(AUDIO_SUPPORT_STRING);
					fWin->Unlock();
				}
			}

		} else if (inFormat.IsVideo() && (videoCodec != NULL)) {
			inVidTrack = inTrack;
			width = (int32)inFormat.Width();
			height = (int32)inFormat.Height();

			// construct desired decoded video format
			memset(&outVidFormat, 0, sizeof(outVidFormat));
			outVidFormat.type = B_MEDIA_RAW_VIDEO;
			rvf = &(outVidFormat.u.raw_video);
			rvf->last_active = (uint32)(height - 1);
			rvf->orientation = B_VIDEO_TOP_LEFT_RIGHT;
			rvf->display.format = B_RGB32;
			rvf->display.bytes_per_row = 4 * width;
			rvf->display.line_width = width;
			rvf->display.line_count = height;

			inVidTrack->DecodedFormat(&outVidFormat);

			if (rvf->display.format == B_RGBA32) {
				printf("fixing color space (B_RGBA32 -> B_RGB32)");
				rvf->display.format = B_RGB32;
			}
			// Transfer the display aspect ratio.
			if (inFormat.type == B_MEDIA_ENCODED_VIDEO) {
				rvf->pixel_width_aspect
					= inFormat.u.encoded_video.output.pixel_width_aspect;
				rvf->pixel_height_aspect
					= inFormat.u.encoded_video.output.pixel_height_aspect;
			} else {
				rvf->pixel_width_aspect
					= inFormat.u.raw_video.pixel_width_aspect;
				rvf->pixel_height_aspect
					= inFormat.u.raw_video.pixel_height_aspect;
			}

			videoBuffer = new (nothrow) uint8[height * rvf->display.bytes_per_row];
			outVidTrack = outFile->CreateTrack(&outVidFormat, videoCodec);

			if (outVidTrack != NULL) {
				// DLM Added to use 3ivx Parameter View
				const char* videoQualitySupport = NULL;
				BView* encoderView = outVidTrack->GetParameterView();
				if (encoderView) {
					MediaEncoderWindow* encoderWin
						= new MediaEncoderWindow(BRect(50, 50, 520, 555), encoderView);
					encoderWin->Go();
						// blocks until the window is quit

					// The quality setting is ignored by the 3ivx encoder if the
					// view was displayed, but this method is the trigger to read
					// all the parameter settings
					outVidTrack->SetQuality(videoQuality / 100.0f);

					// We can now delete the encoderView created for us by the encoder
					delete encoderView;
					encoderView = NULL;

					videoQualitySupport = VIDEO_PARAMFORM_STRING;
				} else {
					if (outVidTrack->SetQuality(videoQuality / 100.0f) >= B_OK)
						videoQualitySupport = VIDEO_SUPPORT_STRING;
				}
				if (videoQualitySupport && fWin->Lock()) {
					fWin->SetVideoQualityLabel(videoQualitySupport);
					fWin->Unlock();
				}
			}
		} else {
			//  didn't do anything with the track
			inFile->ReleaseTrack(inTrack);
		}
	}

	if (!outVidTrack && !outAudTrack) {
		printf("MediaConverterApp::_ConvertFile() - no tracks found!\n");
		ret = B_ERROR;
	}

	if (fCancel) {
		// don't have any video or audio tracks here, or cancelled
		printf("MediaConverterApp::_ConvertFile() - user canceld before transcoding\n");
		ret = B_CANCELED;
	}

	if (ret < B_OK) {
		delete[] audioBuffer;
		delete[] videoBuffer;
		delete outFile;
		return ret;
	}

	outFile->CommitHeader();
	// this is where you would call outFile->AddCopyright(...)

	int64 framesRead;
	media_header mh;
	int32 lastPercent, currPercent;
	float completePercent;
	BString status;

	int64 start;
	int64 end;
	int32 stat = 0;

	// read video from source and write to destination, if necessary
	if (outVidTrack != NULL) {
		lastPercent = -1;
		videoFrameCount = inVidTrack->CountFrames();
		if (endDuration == 0 || endDuration < startDuration) {
			start = 0;
			end = videoFrameCount;
		} else {
			inVidTrack->SeekToTime(&endDuration, stat);
			end = inVidTrack->CurrentFrame();
			inVidTrack->SeekToTime(&startDuration, stat);
			start = inVidTrack->CurrentFrame();
			if (end > videoFrameCount)
				end =  videoFrameCount;
			if (start > end)
				start = 0;
		}

		framesRead = 0;
		for (int64 i = start; (i <= end) && !fCancel; i += framesRead) {
			if ((ret = inVidTrack->ReadFrames(videoBuffer, &framesRead,
					&mh)) != B_OK) {
				fprintf(stderr, "Error reading video frame %Ld: %s\n", i,
						strerror(ret));
				status.SetTo(ERROR_READ_VIDEO_STRING);
				status << i;
				SetStatusMessage(status.String());

				break;
			}
//printf("writing frame %lld\n", i);
			if ((ret = outVidTrack->WriteFrames(videoBuffer, framesRead,
					mh.u.encoded_video.field_flags)) != B_OK) {
				fprintf(stderr, "Error writing video frame %Ld: %s\n", i,
						strerror(ret));
				status.SetTo(ERROR_WRITE_VIDEO_STRING);
				status << i;
				SetStatusMessage(status.String());
				break;
			}
			completePercent = (float)(i - start) / (float)(end - start) * 100;
			currPercent = (int16)floor(completePercent);
			if (currPercent > lastPercent) {
				lastPercent = currPercent;
				status.SetTo(WRITE_VIDEO_STRING);
				status.Append(" ");
				status << currPercent << "% " << COMPLETE_STRING;
				SetStatusMessage(status.String());

			}
		}
		outVidTrack->Flush();
		inFile->ReleaseTrack(inVidTrack);
	}

	// read audio from source and write to destination, if necessary
	if (outAudTrack != NULL) {
		lastPercent = -1;

		audioFrameCount =  inAudTrack->CountFrames();

		if (endDuration == 0 || endDuration < startDuration) {
			start = 0;
			end = audioFrameCount;
		} else {
			inAudTrack->SeekToTime(&endDuration, stat);
			end = inAudTrack->CurrentFrame();
			inAudTrack->SeekToTime(&startDuration, stat);
			start = inAudTrack->CurrentFrame();
			if (end > audioFrameCount)
				end = audioFrameCount;
			if (start > end)
				start = 0;
		}

		for (int64 i = start; (i <= end) && !fCancel; i += framesRead) {
			if ((ret = inAudTrack->ReadFrames(audioBuffer, &framesRead,
				&mh)) != B_OK) {
				fprintf(stderr, "Error reading audio frames: %s\n", strerror(ret));
				status.SetTo(ERROR_READ_AUDIO_STRING);
				status << i;
				SetStatusMessage(status.String());
				break;
			}
//printf("writing audio frames %lld\n", i);
			if ((ret = outAudTrack->WriteFrames(audioBuffer,
				framesRead)) != B_OK) {
				fprintf(stderr, "Error writing audio frames: %s\n",
					strerror(ret));
				status.SetTo(ERROR_WRITE_AUDIO_STRING);
				status << i;
				SetStatusMessage(status.String());
				break;
			}
			completePercent = (float)(i - start) / (float)(end - start) * 100;
			currPercent = (int16)floor(completePercent);
			if (currPercent > lastPercent) {
				lastPercent = currPercent;
				status.SetTo(WRITE_AUDIO_STRING);
				status.Append(" ");
				status << currPercent << "% " << COMPLETE_STRING;
				SetStatusMessage(status.String());
			}
		}
		outAudTrack->Flush();
		inFile->ReleaseTrack(inAudTrack);

	}

	outFile->CloseFile();
	delete outFile;

	delete[] videoBuffer;
	delete[] audioBuffer;

	return ret;
}


// #pragma mark -


int
main(int, char **)
{
	MediaConverterApp app;
	app.Run();

	return 0;
}
