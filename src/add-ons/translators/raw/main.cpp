/*
 * Copyright 2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "RAWTranslator.h"
#include "RAW.h"
#include "TranslatorWindow.h"

#include <Application.h>

#define TEST_MODE 0
#define SHOW_MODE 1
#if SHOW_MODE && TEST_MODE
#	include <Bitmap.h>
#	include <BitmapStream.h>
#	include <View.h>
#	include <Window.h>
#endif

#include <stdio.h>
#include <string.h>


int
main(int argc, char **argv)
{
	BApplication app("application/x-vnd.haiku-raw-translator");

#if TEST_MODE
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			BFile file;
			status_t status = file.SetTo(argv[i], B_READ_ONLY);
			if (status != B_OK) {
				fprintf(stderr, "Cannot read file %s: %s\n", argv[i],
					strerror(status));
				continue;
			}

			DCRaw raw(file);

			try {
				status = raw.Identify();
			} catch (status_t error) {
				status = error;
			}

			if (status < B_OK) {
				fprintf(stderr, "Could not identify file %s: %s\n",
					argv[i], strerror(status));
				continue;
			}

			image_meta_info meta;
			raw.GetMetaInfo(meta);

			printf("manufacturer: %s\n", meta.manufacturer);
			printf("model: %s\n", meta.model);
			printf("software: %s\n", meta.software);
			printf("flash used: %g\n", meta.flash_used);
			printf("ISO speed: %g\n", meta.iso_speed);
			printf("shutter: ");
			if (meta.shutter >= 1)
				printf("%g sec\n", meta.shutter);
			else
				printf("1/%g sec\n", 1 / meta.shutter);
			printf("aperture: %g\n", meta.aperture);
			printf("focal length: %g mm\n", meta.focal_length);
			printf("pixel aspect: %g\n", meta.pixel_aspect);
			printf("flip: %d\n", meta.flip);
			printf("shot order: %ld\n", meta.shot_order);
			printf("DNG version: %ld\n", meta.dng_version);
			printf("Camera White Balance:");
			for (int32 i = 0; i < 4; i++) {
				printf(" %g", meta.camera_multipliers[i]);
			}
			putchar('\n');

			for (int32 i = 0; i < (int32)raw.CountImages(); i++) {
				image_data_info data;
				raw.ImageAt(i, data);

				printf("  [%ld] %s %lu x %lu (%ld bits per sample, compression %ld)\n",
					i, data.is_raw ? "RAW " : "JPEG",
					data.width, data.height, data.bits_per_sample, data.compression);

#	if SHOW_MODE
				if (!data.is_raw) {
					// write data to file
					uint8* buffer;
					size_t bufferSize;
					try {
						status = raw.ReadImageAt(i, buffer, bufferSize);
					} catch (status_t error) {
						status = error;
					}
					if (status == B_OK) {
						BString name = "/tmp/output";
						name << i << ".jpg";
						BFile output(name.String(),
							B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
						output.Write(buffer, bufferSize);
					}
				} else {
					RAWTranslator translator;
					BBitmapStream output;
					BBitmap* bitmap;

					status_t status = translator.DerivedTranslate(&file, NULL,
						NULL, B_TRANSLATOR_BITMAP, &output, 0);
					if (status == B_OK)
						status = output.DetachBitmap(&bitmap);
					if (status == B_OK) {
						BWindow* window = new BWindow(BRect(50, 50,
								bitmap->Bounds().Width() + 50,
								bitmap->Bounds().Height() + 50),
							"RAW", B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS
							| B_NOT_RESIZABLE);
						BView* view = new BView(window->Bounds(), NULL,
							B_WILL_DRAW, B_FOLLOW_NONE);
						window->AddChild(view);
						window->Show();
						snooze(300000);
						window->Lock();
						view->DrawBitmap(bitmap, window->Bounds());
						view->Sync();
						window->Unlock();
						delete bitmap;

						wait_for_thread(window->Thread(), &status);
					}
				}
#	endif
			}
		}
		return 0;
	}
#endif

	status_t status = LaunchTranslatorWindow(new RAWTranslator, "RAW Settings",
		BRect(0, 0, 225, 175));
	if (status != B_OK)
		return 1;

	app.Run();
	return 0;
}

