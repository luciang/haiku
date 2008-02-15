/*
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2005-2007, François Revol, revol@free.fr.
 * Distributed under the terms of the MIT License.
 */

/*! Launches an application/document from the shell */


#include <Entry.h>
#include <List.h>
#include <MimeType.h>
#include <Roster.h>
#include <String.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


const char *kTrackerSignature = "application/x-vnd.Be-TRAK";


status_t
open_file(const char* openWith, BEntry &entry, int32 line = -1, int32 col = -1)
{
	entry_ref ref;
	status_t status = entry.GetRef(&ref);
	if (status < B_OK)
		return status;

	BMessenger target(openWith ? openWith : kTrackerSignature);
	if (!target.IsValid())
		return be_roster->Launch(&ref);

	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &ref);
	if (line > -1)
		message.AddInt32("be:line", line);
	if (col > -1)
		message.AddInt32("be:column", col);

	// tell the app to open the file
	return target.SendMessage(&message);
}


int
main(int argc, char **argv)
{
	int exitcode = EXIT_SUCCESS;
	const char *openWith = NULL;

	char *progName = argv[0];
	if (strrchr(progName, '/'))
		progName = strrchr(progName, '/') + 1;

	if (argc < 2)
		fprintf(stderr,"usage: %s <file[:line[:column]] or url or application signature> ...\n", progName);

	while (*++argv) {
		status_t status = B_OK;
		argc--;

		BEntry entry(*argv);
		if ((status = entry.InitCheck()) == B_OK && entry.Exists()) {
			status = open_file(openWith, entry);
		} else if (!strncasecmp("application/", *argv, 12)) {
			// maybe it's an application-mimetype?

			// subsequent files are open with that app
			openWith = *argv;

			// in the case the app is already started, 
			// don't start it twice if we have other args
			BList teams;
			if (argc > 1)
				be_roster->GetAppList(*argv, &teams);

			if (teams.IsEmpty())
				status = be_roster->Launch(*argv);
			else
				status = B_OK;
		} else if (strchr(*argv, ':')) {
			// try to open it as an URI
			BString mimeType = "application/x-vnd.Be.URL.";
			BString arg(*argv);
			mimeType.Append(arg, arg.FindFirst(":"));

			// the protocol should be alphanum
			// we just check if it's registered
			// if not there is likely no supporting app anyway
			if (BMimeType::IsValid(mimeType.String())) {
				char *args[2] = { *argv, NULL };
				status = be_roster->Launch(openWith ? openWith : mimeType.String(), 1, args);
				if (status == B_OK)
					continue;
			}

			// maybe it's "file:line" or "file:line:col"
			int line = 0, col = 0, i;
			status = B_ENTRY_NOT_FOUND;
			// remove gcc error's last :
			if (arg[arg.Length() - 1] == ':')
				arg.Truncate(arg.Length() - 1);

			i = arg.FindLast(':');
			if (i > 0) {
				line = atoi(arg.String() + i + 1);
				arg.Truncate(i);

				status = entry.SetTo(arg.String());
				if (status == B_OK && entry.Exists())
					status = open_file(openWith, entry, line);
				if (status == B_OK)
					continue;

				// get the column
				col = line;
				i = arg.FindLast(':');
				line = atoi(arg.String() + i + 1);
				arg.Truncate(i);

				status = entry.SetTo(arg.String());
				if (status == B_OK && entry.Exists())
					status = open_file(openWith, entry, line, col);
			}
		} else
			status = B_ENTRY_NOT_FOUND;

		if (status != B_OK && status != B_ALREADY_RUNNING) {
			fprintf(stderr, "%s: \"%s\": %s\n", progName, *argv, strerror(status));
			// make sure the shell knows this
			exitcode = EXIT_FAILURE;
		}
	}

	return exitcode;
}
