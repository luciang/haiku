/*
 * Copyright 2004-2006, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 * Original code from ZipOMatic by jonas.sundstrom@kirilla.com
 */
#include <Messenger.h>
#include <Path.h>
#include "ExpanderThread.h"
#include <image.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

const char * ExpanderThreadName	=	"ExpanderThread";

ExpanderThread::ExpanderThread(BMessage * refs_message, BMessenger * messenger)
	:	GenericThread(ExpanderThreadName, B_NORMAL_PRIORITY, refs_message),
	fWindowMessenger(messenger),
	fThreadId(-1),
	fStdIn(-1),
	fStdOut(-1),
	fStdErr(-1),
	fExpanderOutput(NULL),
	fExpanderOutputString(),
	fExpanderOutputBuffer(new char [4096])
{
	SetDataStore(new BMessage(* refs_message));  // leak?
	// prevents bug with B_SIMPLE_DATA
	// (drag&drop messages)
}

ExpanderThread::~ExpanderThread()
{
	delete fWindowMessenger;
	delete [] fExpanderOutputBuffer;
}

status_t
ExpanderThread::ThreadStartup()
{
	status_t	status	=	B_OK;
	entry_ref	srcRef, destRef;
	BString 	cmd;

	if ((status = GetDataStore()->FindRef("srcRef", &srcRef)) != B_OK)
		return status;
	if ((status = GetDataStore()->FindRef("destRef", &destRef)) == B_OK) {
		BPath path(&destRef);
		chdir(path.Path());
	}
	if ((status = GetDataStore()->FindString("cmd", &cmd)) != B_OK)
		return status;

	BPath path(&srcRef);
	BString pathString(path.Path());
	pathString.CharacterEscape("\"$`", '\\');
	pathString.Prepend("\"");
	pathString.Append("\"");
	cmd.ReplaceAll("%s", pathString.String());

	int32 argc = 3;
	const char ** argv = new const char * [argc + 1];

	argv[0]	=	strdup("/bin/sh");
	argv[1]	=	strdup("-c");
	argv[2]	=	strdup(cmd.String());
	argv[argc] = NULL;

	fThreadId = PipeCommand(argc, argv, fStdIn, fStdOut, fStdErr);

	delete [] argv;

	if (fThreadId < 0)
		return fThreadId;

	// lower the command priority since it is a background task.
	set_thread_priority(fThreadId, B_LOW_PRIORITY);

	resume_thread(fThreadId);

	fExpanderOutput = fdopen(fStdOut, "r");

	return B_OK;
}

status_t
ExpanderThread::ExecuteUnit(void)
{
	// read output from command
	// send it to window

	char *output_string = fgets(fExpanderOutputBuffer , 4096 - 1, fExpanderOutput);

	if (output_string == NULL)
		return EOF;

	BMessage message('outp');
	message.AddString("output", output_string);
	for (int32 i = 0; i < 5; i++) {
		output_string = fgets(fExpanderOutputBuffer , 4096 - 1, fExpanderOutput);
		if (!output_string)
			break;
		message.AddString("output", output_string);
	}
	fWindowMessenger->SendMessage(&message);

	return B_OK;
}

status_t
ExpanderThread::ThreadShutdown(void)
{
	close(fStdIn);
	close(fStdOut);
	close(fStdErr);

	return B_OK;
}

void
ExpanderThread::ThreadStartupFailed(status_t status)
{
	fprintf(stderr, "ExpanderThread::ThreadStartupFailed() : %s\n", strerror(status));

	Quit();
}

void
ExpanderThread::ExecuteUnitFailed(status_t status)
{
	if (status == EOF) {
		// thread has finished, been quit or killed, we don't know
		fWindowMessenger->SendMessage(new BMessage('exit'));
	} else {
		// explicit error - communicate error to Window
		fWindowMessenger->SendMessage(new BMessage('exrr'));
	}

	Quit();
}

void
ExpanderThread::ThreadShutdownFailed(status_t status)
{
	fprintf(stderr, "ExpanderThread::ThreadShutdownFailed() %s\n", strerror(status));
}


status_t
ExpanderThread::ProcessRefs(BMessage *msg)
{
	return B_OK;
}

thread_id
ExpanderThread::PipeCommand(int argc, const char **argv, int &in, int &out, int &err, const char **envp)
{
	// This function written by Peter Folk <pfolk@uni.uiuc.edu>
	// and published in the BeDevTalk FAQ
	// http://www.abisoft.com/faq/BeDevTalk_FAQ.html#FAQ-209

	// Save current FDs
	int old_in  =  dup(0);
	int old_out  =  dup(1);
	int old_err  =  dup(2);

	int filedes[2];

	/* Create new pipe FDs as stdin, stdout, stderr */
	pipe(filedes);  dup2(filedes[0], 0); close(filedes[0]);
	in = filedes[1];  // Write to in, appears on cmd's stdin
	pipe(filedes);  dup2(filedes[1], 1); close(filedes[1]);
	out = filedes[0]; // Read from out, taken from cmd's stdout
	pipe(filedes);  dup2(filedes[1], 2); close(filedes[1]);
	err = filedes[0]; // Read from err, taken from cmd's stderr

	// "load" command.
	thread_id ret  =  load_image(argc, argv, envp);

	if (ret < B_OK)
		return ret;

	// thread ret is now suspended.

	setpgid(ret, ret);

	// Restore old FDs
	close(0); dup(old_in); close(old_in);
	close(1); dup(old_out); close(old_out);
	close(2); dup(old_err); close(old_err);

	/* Theoretically I should do loads of error checking, but
	   the calls aren't very likely to fail, and that would 
	   muddy up the example quite a bit.  YMMV. */

	return ret;
}


status_t
ExpanderThread::SuspendExternalExpander()
{
	thread_info thread_info;
	status_t status = get_thread_info(fThreadId, &thread_info);

	if (status == B_OK)
		return send_signal(-fThreadId, SIGSTOP);
	else
		return status;
}

status_t
ExpanderThread::ResumeExternalExpander()
{
	thread_info thread_info;
	status_t status = get_thread_info(fThreadId, &thread_info);

	if (status == B_OK)
		return send_signal(-fThreadId, SIGCONT);
	else
		return status;
}

status_t
ExpanderThread::InterruptExternalExpander()
{
	thread_info thread_info;
	status_t status = get_thread_info(fThreadId, &thread_info);

	if (status == B_OK) {
		status = send_signal(-fThreadId, SIGINT);
		WaitOnExternalExpander();
	}
	return status;
}

status_t
ExpanderThread::WaitOnExternalExpander()
{
	thread_info thread_info;
	status_t status = get_thread_info(fThreadId, &thread_info);

	if (status == B_OK)
		return wait_for_thread(fThreadId, &status);
	else
		return status;
}
