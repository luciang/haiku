/*
 * Copyright 2007 Haiku, inc.
 * Copyright (c) 2003-4 Kian Duffy <myob@users.sourceforge.net>
 * Copyright (c) 2004 Daniel Furrer <assimil8or@users.sourceforge.net>
 * Parts Copyright (C) 1998,99 Kazuho Okui and Takashi Murai. 
 *
 * Distributed under the terms of the MIT license.
 *
 */

#include "Shell.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <new>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <OS.h>

#include "TermConst.h"
#include "TermParse.h"
#include "TerminalBuffer.h"
	

#ifndef CEOF
#define CEOF ('D'&037)
#endif
#ifndef CSUSP
#define CSUSP ('Z'&037)
#endif
#ifndef CQUIT
#define CQUIT ('\\'&037)
#endif
#ifndef CEOL
#define CEOL 0
#endif
#ifndef CSTOP
#define CSTOP ('Q'&037)
#endif
#ifndef CSTART
#define CSTART ('S'&037)
#endif
#ifndef CSWTCH
#define CSWTCH 0
#endif


/*
 * Set environment variable.
 */
#if defined(HAIKU_TARGET_PLATFORM_BEOS) || \
	defined(HAIKU_TARGET_PLATFORM_BONE) || \
	defined(HAIKU_TARGET_PLATFORM_LIBBE_TEST)

extern char **environ;

static int setenv(const char *var, const char *value, bool overwrite);

static int
setenv(const char *var, const char *value, bool overwrite)
{
	int envindex = 0;
	const int len = strlen(var);
	const int val_len = strlen (value);

	while (environ[envindex] != NULL) {
		if (!strncmp(environ[envindex], var, len)) {
			/* found it */
			if (overwrite) {
				environ[envindex] = (char *)malloc((unsigned)len + val_len + 2);
				sprintf(environ[envindex], "%s=%s", var, value);
			}
			return 0;
		}
		envindex++;
	}

	environ[envindex] = (char *)malloc((unsigned)len + val_len + 2);
	sprintf(environ[envindex], "%s=%s", var, value);
	environ[++envindex] = NULL;
	return 0;
}
#endif


/* handshake interface */
typedef struct 
{
	int status;		/* status of child */
	char msg[128];	/* error message */
	int row;		/* terminal rows */
	int col;		/* Terminal columns */
} handshake_t;

/* status of handshake */
#define PTY_OK	0	/* pty open and set termios OK */
#define PTY_NG	1	/* pty open or set termios NG */
#define PTY_WS	2	/* pty need WINSIZE (row and col ) */


Shell::Shell()
	:
	fFd(-1),
	fProcessID(-1),
	fTermParse(NULL),
	fAttached(false)
{
}


Shell::~Shell()
{
	Close();
}


status_t
Shell::Open(int row, int col, const char *encoding, int argc, const char **argv)
{
	if (fFd >= 0)
		return B_ERROR;

	status_t status = _Spawn(row, col, encoding, argc, argv);
	if (status < B_OK)
		return status;

	fTermParse = new (std::nothrow) TermParse(fFd);
	if (fTermParse == NULL) {
		Close();
		return B_NO_MEMORY;	
	}
	
	return B_OK;
}


void
Shell::Close()
{
	delete fTermParse;
	fTermParse = NULL;
	
	if (fFd >= 0) {
		close(fFd);
		kill(-fProcessID, SIGHUP);
		fProcessID = -1;
		int status;
		wait(&status);	
		fFd = -1;	
	}
}


const char *
Shell::TTYName() const
{
	return ttyname(fFd);
}


ssize_t
Shell::Read(void *buffer, size_t numBytes) const
{
	if (fFd < 0)
		return B_NO_INIT;
	
	return read(fFd, buffer, numBytes);
}


ssize_t
Shell::Write(const void *buffer, size_t numBytes)
{
	if (fFd < 0)
		return B_NO_INIT;
	
	return write(fFd, buffer, numBytes);
}


status_t
Shell::UpdateWindowSize(int rows, int columns)
{
	struct winsize winSize;
	winSize.ws_row = rows;
	winSize.ws_col = columns;
	if (ioctl(fFd, TIOCSWINSZ, &winSize) != 0)
		return errno;
	return B_OK;
}


status_t
Shell::GetAttr(struct termios &attr) const
{
	if (tcgetattr(fFd, &attr) < 0)
		return errno;
	return B_OK;
}


status_t
Shell::SetAttr(struct termios &attr)
{
	if (tcsetattr(fFd, TCSANOW, &attr) < 0)
		return errno;
	return B_OK;
}


int
Shell::FD() const
{
	return fFd;
}


status_t
Shell::AttachBuffer(TerminalBuffer *buffer)
{
	if (fAttached)
		return B_ERROR;

	fAttached = true;
	
	return fTermParse->StartThreads(buffer);
}


void
Shell::DetachBuffer()
{
	if (fAttached)
		fTermParse->StopThreads();
}


// private
static status_t
send_handshake_message(thread_id target, const handshake_t& handshake)
{
	return send_data(target, 0, &handshake, sizeof(handshake_t));
}


static void
receive_handshake_message(handshake_t& handshake)
{
	thread_id sender;
	receive_data(&sender, &handshake, sizeof(handshake_t));
}


static void
initialize_termios(struct termios &tio)
{
	/*
	 * Set Terminal interface.
	 */

	tio.c_line = 0;
	tio.c_lflag |= ECHOE; 

	/* input: nl->nl, cr->nl */
	tio.c_iflag &= ~(INLCR|IGNCR);
	tio.c_iflag |= ICRNL;
	tio.c_iflag &= ~ISTRIP;

	/* output: cr->cr, nl in not retrun, no delays, ln->cr/ln */
	tio.c_oflag &= ~(OCRNL|ONLRET|NLDLY|CRDLY|TABDLY|BSDLY|VTDLY|FFDLY);
	tio.c_oflag |= ONLCR;
	tio.c_oflag |= OPOST;

	/* baud rate is 19200 (equal beterm) */
	tio.c_cflag &= ~(CBAUD);
	tio.c_cflag |= B19200;

	tio.c_cflag &= ~CSIZE;
	tio.c_cflag |= CS8;
	tio.c_cflag |= CREAD;

	tio.c_cflag |= HUPCL;
	tio.c_iflag &= ~(IGNBRK|BRKINT);

	/*
	 * enable signals, canonical processing (erase, kill, etc), echo.
	*/
	tio.c_lflag |= ISIG|ICANON|ECHO|ECHOE|ECHONL;
	tio.c_lflag &= ~(ECHOK | IEXTEN);

	/* set control characters. */
	tio.c_cc[VINTR]  = 'C' & 0x1f;	/* '^C'	*/
	tio.c_cc[VQUIT]  = CQUIT;		/* '^\'	*/
	tio.c_cc[VERASE] = 0x7f;		/* '^?'	*/
	tio.c_cc[VKILL]  = 'U' & 0x1f;	/* '^U'	*/
	tio.c_cc[VEOF]   = CEOF;		/* '^D' */
	tio.c_cc[VEOL]   = CEOL;		/* '^@' */
	tio.c_cc[VMIN]   = 4;
	tio.c_cc[VTIME]  = 0;
	tio.c_cc[VEOL2]  = CEOL;		/* '^@' */
	tio.c_cc[VSWTCH] = CSWTCH;		/* '^@' */
	tio.c_cc[VSTART] = CSTART;		/* '^S' */
	tio.c_cc[VSTOP]  = CSTOP;		/* '^Q' */
	tio.c_cc[VSUSP]  = CSUSP;		/* '^Z' */
}


status_t
Shell::_Spawn(int row, int col, const char *encoding, int argc, const char **argv)
{
	const char *kDefaultShellCommand[] = { "/bin/sh", "--login", NULL };
	
	if (argv == NULL || argc == 0) {
		argv = kDefaultShellCommand;
		argc = 2;	
	}

	signal(SIGTTOU, SIG_IGN);
	
#ifdef __HAIKU__
	// get a pseudo-tty
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	const char *ttyName;
#else /* __HAIKU__ */
	/*
	 * Get a pseudo-tty. We do this by cycling through files in the
	 * directory. The operating system will not allow us to open a master
	 * which is already in use, so we simply go until the open succeeds.
	 */
	char ttyName[B_PATH_NAME_LENGTH];
	int master = -1;	
	DIR *dir = opendir("/dev/pt/");
	if (dir != NULL) {
		struct dirent *dirEntry;
		while ((dirEntry = readdir(dir)) != NULL) { 
			// skip '.' and '..'
			if (dirEntry->d_name[0] == '.')
				continue;

			char ptyName[B_PATH_NAME_LENGTH];
			snprintf(ptyName, sizeof(ptyName), "/dev/pt/%s", dirEntry->d_name);

			master = open(ptyName, O_RDWR);
			if (master >= 0) {
				// Set the tty that corresponds to the pty we found
				snprintf(ttyName, sizeof(ttyName), "/dev/tt/%s", dirEntry->d_name);
				break;
			} else {
				// B_BUSY is a normal case
				if (errno != B_BUSY) 
					fprintf(stderr, "could not open %s: %s\n", ptyName, strerror(errno));
			}
		}
		closedir(dir);
	}
#endif /* __HAIKU__ */

	if (master < 0) {
    	fprintf(stderr, "Didn't find any available pseudo ttys.");
    	return errno;
	}

#ifdef __HAIKU__
	if (grantpt(master) != 0 || unlockpt(master) != 0
		|| (ttyName = ptsname(master)) == NULL) {
		close(master);
    	fprintf(stderr, "Failed to init pseudo tty.");
		return errno;
	}
#endif /* __HAIKU__ */

	/*
	 * Get the modes of the current terminal. We will duplicates these
	 * on the pseudo terminal.
	 */

	thread_id terminalThread = find_thread(NULL);

	/* Fork a child process. */
	if ((fProcessID = fork()) < 0) {
		close(master);
		return B_ERROR;
	}

	handshake_t handshake;

	if (fProcessID == 0) {
		// Now in child process.

		/*
		 * Make our controlling tty the pseudo tty. This hapens because
		 * we cleared our original controlling terminal above.
		 */

		/* Set process session leader */
		if (setsid() < 0) {
			handshake.status = PTY_NG;
			snprintf(handshake.msg, sizeof(handshake.msg),
				"could not set session leader.");
			send_handshake_message(terminalThread, handshake);
			exit(1);
		}

		/* open slave pty */
		int slave = -1;
		if ((slave = open(ttyName, O_RDWR)) < 0) {
			handshake.status = PTY_NG;
			snprintf(handshake.msg, sizeof(handshake.msg),
				"can't open tty (%s).", ttyName);
			send_handshake_message(terminalThread, handshake);
			exit(1);
		}

		/* set signal default */
		signal(SIGCHLD, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);

		struct termios tio;
		/* get tty termios (not necessary).
		 * TODO: so why are we doing it ?
		 */
		tcgetattr(slave, &tio);
		
		initialize_termios(tio);

		/*
		 * change control tty. 
		 */

		dup2(slave, 0);
		dup2(slave, 1);
		dup2(slave, 2);

		/* close old slave fd. */
		if (slave > 2)
			close(slave);

		/*
		 * set terminal interface.
		 */
		if (tcsetattr(0, TCSANOW, &tio) == -1) {
			handshake.status = PTY_NG;
			snprintf(handshake.msg, sizeof(handshake.msg),
				"failed set terminal interface (TERMIOS).");
			send_handshake_message(terminalThread, handshake);
			exit(1);
		}

		/*
		 * set window size.
		 */

		handshake.status = PTY_WS;
		send_handshake_message(terminalThread, handshake);
		receive_handshake_message(handshake);

		if (handshake.status != PTY_WS) {
			handshake.status = PTY_NG;
			snprintf(handshake.msg, sizeof(handshake.msg),
				"mismatch handshake.");
			send_handshake_message(terminalThread, handshake);
			exit(1);
		}

		struct winsize ws = { handshake.row, handshake.col };

		ioctl(0, TIOCSWINSZ, &ws);

		tcsetpgrp(0, getpgrp());
			// set this process group ID as the controlling terminal
#ifndef __HAIKU__
		ioctl(0, 'pgid', getpid());
#endif
		set_thread_priority(find_thread(NULL), B_NORMAL_PRIORITY);

		/* pty open and set termios successful. */
		handshake.status = PTY_OK;
		send_handshake_message(terminalThread, handshake);

		/*
		 * setenv TERM and TTY.
		 */
		setenv("TERM", "xterm", true);
		setenv("TTY", ttyName, true);
		setenv("TTYPE", encoding, true);
		setenv("SHELL", argv[0], true);

		execve(argv[0], (char * const *)argv, environ);

		/*
		 * Exec failed.
		 * TODO: This doesn't belong here.
		 */
		
		sleep(1);
		const char *spawnAlertMessage = "alert --stop "
						"'Cannot execute \"%s\":\n"
						"\t%s' "
						"'Use Default Shell' 'Abort'";
		char errorMessage[256];
		snprintf(errorMessage, sizeof(errorMessage), spawnAlertMessage, argv[0], strerror(errno));

		int returnValue = system(errorMessage);
		if (returnValue == 0)
			execl(kDefaultShellCommand[0], kDefaultShellCommand[0],
				kDefaultShellCommand[1], NULL);

		exit(1);
	}

	/*
	 * In parent Process, Set up the input and output file pointers so 
	 * that they can write and read the pseudo terminal.
	 */

	/*
	 * close parent control tty.
	 */

	int done = 0;
	while (!done) {
		receive_handshake_message(handshake);

		switch (handshake.status) {
			case PTY_OK:
				done = 1;
				break;

			case PTY_NG:
				fprintf(stderr, "%s\n", handshake.msg);
				done = -1;
				break;

			case PTY_WS:
				handshake.row = row;
				handshake.col = col;
				handshake.status = PTY_WS;
				send_handshake_message(fProcessID, handshake);
				break;
		}
	}
  
	if (done <= 0)
		return B_ERROR;

	fFd = master;

	return B_OK;
}

