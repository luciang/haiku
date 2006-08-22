/*
 * Copyright 2003-2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jonas Sundström, jonas.sundstrom@kirilla.com
 */


#include "ZipOMaticZipper.h"
#include "ZipOMaticWindow.h"
#include "ZipOMaticMisc.h"

#include <Debug.h>
#include <FindDirectory.h> 
#include <Message.h>
#include <Path.h>
#include <Volume.h>

#include <signal.h>
#include <unistd.h>
#include <errno.h>


const char* kZipperThreadName = "ZipperThread";


ZipperThread::ZipperThread (BMessage* refsMessage, BWindow* window)
	: GenericThread(kZipperThreadName, B_NORMAL_PRIORITY, refsMessage),
	fWindowMessenger(window),
	fZipProcess(-1),
	m_std_in(-1),
	m_std_out(-1),
	m_std_err(-1),
	fOutputFile(NULL)
{
	PRINT(("ZipperThread()\n"));

	m_thread_data_store = new BMessage(*refsMessage);
		// leak?
		// prevents bug with B_SIMPLE_DATA
		// (drag&drop messages)
}


ZipperThread::~ZipperThread()	
{
}


status_t
ZipperThread::ThreadStartup()
{
	PRINT(("ZipperThread::ThreadStartup()\n"));

	BString archiveName = "Archive.zip";

	// do all refs have the same parent dir?
	type_code type = B_REF_TYPE;
	int32 refCount = 0;
	entry_ref ref;
	entry_ref lastRef;
	bool sameFolder = true;

	status_t status = m_thread_data_store->GetInfo("refs", &type, &refCount);
	if (status != B_OK)
		return status;

	for (int index = 0;	index < refCount; index++) {
		m_thread_data_store->FindRef("refs", index, &ref);

		if (index > 0) {
			BEntry entry(&ref);
			if (entry.IsSymLink()) {
				entry.SetTo(&ref, true);
				entry_ref target;
				entry.GetRef(&target);
				if (lastRef.directory != target.directory) {
					sameFolder = false;
					break;
				}
			} else if (lastRef.directory != ref.directory) {
				sameFolder = false;
				break;
			}
		}
		lastRef = ref;
	}

	// change active dir
	if (sameFolder) {
		BEntry entry(&lastRef);
		BPath path;
		entry.GetParent(&entry);
		entry.GetPath(&path);
		chdir(path.Path());
	} else {
		BPath path;
		if (find_directory(B_DESKTOP_DIRECTORY, &path) == B_OK)
			chdir(path.Path());
	}

	// archive filename
	if (refCount == 1) {
		archiveName = lastRef.name;
		archiveName += ".zip";			
	}

	int32 argc = refCount + 3;
	const char** argv = new const char* [argc + 1];

	argv[0] = strdup("/bin/zip");
	argv[1] = strdup("-ry");
	argv[2] = strdup(archiveName.String());

	// files to zip
	for (int index = 0;  index < refCount ;  index++) {
		m_thread_data_store->FindRef("refs", index, &ref);

		if (sameFolder) {
			// just the file name
			argv[3 + index]	= strdup(ref.name);
		} else {
			// full path
			BPath path(&ref);
			BString file = path.Path();
			argv[3 + index]	= strdup(path.Path());
		}
	}

	argv[argc] = NULL;

	fZipProcess = _PipeCommand(argc, argv, m_std_in, m_std_out, m_std_err); 

	delete [] argv;

	if (fZipProcess < 0)
		return fZipProcess;

	resume_thread(fZipProcess);

	fOutputFile = fdopen(m_std_out, "r");
	if (fOutputFile == NULL)
		return errno;

	archiveName.Prepend("Creating archive: ");

	_SendMessageToWindow('strt', "archive_filename", archiveName.String());
	_SendMessageToWindow('outp', "zip_output", "Preparing to archive"); 

	PRINT(("\n"));

	return B_OK;
}


status_t
ZipperThread::ExecuteUnit()
{
	//PRINT(("ZipperThread::ExecuteUnit()\n"));

	// read output from /bin/zip
	// send it to window
	char buffer[4096];

	char* output = fgets(buffer, sizeof(buffer) - 1, fOutputFile);
	if (output == NULL)
		return EOF;

	char* newLine = strrchr(output, '\n');
	if (newLine != NULL)
		*newLine = '\0';

	if (!strncmp("  a", output, 3)) {
		output[2] = 'A';
		_SendMessageToWindow('outp', "zip_output", output + 2);
	} else if (!strncmp("up", output, 2)) {
		output[0] = 'U';
		_SendMessageToWindow('outp', "zip_output", output);
	} else {
		_SendMessageToWindow('outp', "zip_output", output);
	}

	return B_OK;
}


status_t
ZipperThread::ThreadShutdown()
{
	PRINT(("ZipperThread::ThreadShutdown()\n"));

	close(m_std_in); 
    close(m_std_out); 
   	close(m_std_err);

	return B_OK;
}


void
ZipperThread::ThreadStartupFailed(status_t status)
{
	error_message("ZipperThread::ThreadStartupFailed() \n", status);
	Quit();
}


void
ZipperThread::ExecuteUnitFailed(status_t status)
{
	error_message("ZipperThread::ExecuteUnitFailed() \n", status);

	if (status == EOF) {
		// thread has finished, been quit or killed, we don't know
		_SendMessageToWindow('exit');
	} else {
		// explicit error - communicate error to Window
		_SendMessageToWindow('exrr');
	}

	Quit();
}


void
ZipperThread::ThreadShutdownFailed(status_t status)
{
	error_message("ZipperThread::ThreadShutdownFailed() \n", status);
}


void
ZipperThread::_MakeShellSafe(BString* string)
{
	string->CharacterEscape("\"$`", '\\');
	string->Prepend("\""); 
	string->Append("\""); 
}


status_t
ZipperThread::ProcessRefs(BMessage* msg)
{
	return B_OK;
}


thread_id
ZipperThread::_PipeCommand(int argc, const char** argv, int& in, int& out,
	int& err, const char** envp)
{
	// This function written by Peter Folk <pfolk@uni.uiuc.edu>
	// and published in the BeDevTalk FAQ 
	// http://www.abisoft.com/faq/BeDevTalk_FAQ.html#FAQ-209

	// Save current FDs 
	int old_in = dup(0); 
	int old_out = dup(1); 
	int old_err = dup(2); 

	int filedes[2]; 

	/* Create new pipe FDs as stdin, stdout, stderr */ 
	pipe(filedes);
	dup2(filedes[0], 0);
	close(filedes[0]); 
	in = filedes[1];  // Write to in, appears on cmd's stdin 
	pipe(filedes);
	dup2(filedes[1], 1);
	close(filedes[1]); 
	out = filedes[0]; // Read from out, taken from cmd's stdout 
	pipe(filedes);
	dup2(filedes[1], 2);
	close(filedes[1]); 
	err = filedes[0]; // Read from err, taken from cmd's stderr 

	// "load" command. 
	thread_id ret = load_image(argc, argv, envp); 
	// thread ret is now suspended. 

	PRINT(("load_image() thread_id: %ld\n", ret));

	// Restore old FDs 
	close(0); dup(old_in); close(old_in); 
	close(1); dup(old_out); close(old_out); 
	close(2); dup(old_err); close(old_err); 

	// TODO:
	/*
	Theoretically I should do loads of error checking, but 
	the calls aren't very likely to fail, and that would 
	muddy up the example quite a bit.  YMMV.
	*/ 

    return ret;
}


void
ZipperThread::_SendMessageToWindow(uint32 what, const char* name, const char* value)
{
	BMessage msg(what);
	if (name != NULL && value != NULL)
		msg.AddString(name, value);

	fWindowMessenger.SendMessage(&msg);
}


status_t
ZipperThread::SuspendExternalZip()
{
	PRINT(("ZipperThread::SuspendExternalZip()\n"));

	thread_info info;
	status_t status = get_thread_info(fZipProcess, &info);

	if (status == B_OK && !strcmp(info.name, "zip"))
		return suspend_thread(fZipProcess);

	return status;
}


status_t
ZipperThread::ResumeExternalZip()
{
	PRINT(("ZipperThread::ResumeExternalZip()\n"));

	thread_info info;
	status_t status = get_thread_info(fZipProcess, &info);

	if (status == B_OK && !strcmp(info.name, "zip"))
		return resume_thread(fZipProcess);

	return status;
}


status_t
ZipperThread::InterruptExternalZip()
{
	PRINT(("ZipperThread::InterruptExternalZip()\n"));
	
	thread_info info;
	status_t status = get_thread_info(fZipProcess, &info);

	if (status == B_OK && !strcmp(info.name, "zip")) {
		status = B_OK;
		status = send_signal(fZipProcess, SIGINT);
		WaitOnExternalZip();
		return status;
	}

	return status;
}


status_t
ZipperThread::WaitOnExternalZip()
{
	PRINT(("ZipperThread::WaitOnExternalZip()\n"));

	thread_info info;
	status_t status = get_thread_info(fZipProcess, &info);

	if (status == B_OK && !strcmp(info.name, "zip"))
		return wait_for_thread(fZipProcess, &status);

	return status;
}
