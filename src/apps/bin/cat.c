// ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
//
//  Copyright (c) 2001-2003, OpenBeOS
//
//  This software is part of the OpenBeOS distribution. 
//
//
//
//  File:        cat.c
//  Description: Concatenate file(s), or standard input, to standard output.
//
// ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char const copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)cat.c	8.2 (Berkeley) 4/27/95";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <ctype.h>
//#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

int bflag, eflag, nflag, sflag, tflag, vflag;
int rval;
const char *filename;

static void usage(void);
static void scanfiles(char *argv[], int cooked);
static void cook_cat(FILE *);
static void raw_cat(int);


int
main(int argc, char *argv[])
{
	int ch;
	setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "AbeEnstTuv")) != -1)
		switch (ch) {
		case 'A':
			vflag = eflag = tflag = 1;	/* -A implies -vET */
			break;
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
			break;
		case 'E':
			eflag = 1;
			break;						
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = vflag = 1;	/* -t implies -v */
			break;
		case 'T':
			tflag = 1;
			break;			
		case 'u':
			setbuf(stdout, NULL);
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	argv += optind;

	if (bflag || eflag || nflag || sflag || tflag || vflag)
		scanfiles(argv, 1);
	else
		scanfiles(argv, 0);
	if (fclose(stdout)) {
		fprintf(stderr, "Error: stdout\n");
		exit(1);
	}
	exit(rval);
	/* NOTREACHED */
}

static void
usage(void)
{
	fprintf(stderr, "Usage: /bin/cat [OPTION] [FILE]...\n"
		"Concatenate FILE(s), or standard input, to standard output.\n"
  		"  -A \t equivalent to -vET\n"
		"  -b \t number nonblank output lines\n"
		"  -e \t equivalent to -vE\n"
		"  -E \t display $ at end of each line\n"
		"  -n \t number all output lines\n"
		"  -s \t never more than one single blank line\n"
		"  -t \t equivalent to -vT\n"
		"  -T \t display TAB characters as ^I\n"
		"  -u \t unbuffered output\n"
		"  -v \t use ^ and M- notation, except for LFD and TAB\n\n"  
		"With no FILE, or when FILE is -, read standard input.\n");
	exit(1);
	/* NOTREACHED */
}

static void
scanfiles(char *argv[], int cooked)
{
	int i = 0;
	char *path;
	FILE *fp;

	while ((path = argv[i]) != NULL || i == 0) {
		int fd;

		if (path == NULL || strcmp(path, "-") == 0) {
			filename = "stdin";
			fd = STDIN_FILENO;
		} else {
			filename = path;
			fd = open(path, O_RDONLY);
		}
		if (fd < 0) {
			fprintf(stderr, "/bin/cat: %s: No such file or directory\n", path);
			rval = 1;
		} else if (cooked) {
			if (fd == STDIN_FILENO)
				cook_cat(stdin);
			else {
				fp = fdopen(fd, "r");
				cook_cat(fp);
				fclose(fp);
			}
		} else {
			raw_cat(fd);
			if (fd != STDIN_FILENO)
				close(fd);
		}
		if (path == NULL)
			break;
		++i;
	}
}

static void
cook_cat(FILE *fp)
{
	static int nflagfirstrun = 1, line = 0;
	int ch, gobble, prev;

	/* Reset EOF condition on stdin. */
	if (fp == stdin && feof(stdin))
		clearerr(stdin);

	gobble = 0;
	for (prev = '\n'; (ch = getc(fp)) != EOF; prev = ch) {
		if (prev == '\n') {
			if (sflag) {
				if (ch == '\n') {
					if (gobble)
						continue;
					gobble = 1;
				} else
					gobble = 0;
			}
			if (nflag && (!bflag || ch != '\n')) {
				if (nflagfirstrun) {
					fprintf(stdout, "%6d\t", ++line);				
					if (ferror(stdout))
						break;
				} else {
					nflagfirstrun = 1;
				}
			}
		}
		if (ch == '\n') {
			if (eflag && putchar('$') == EOF)
				break;
		} else if (ch == '\t') {
			if (tflag) {
				if (putchar('^') == EOF || putchar('I') == EOF)
					break;
				continue;
			}
		} else if (vflag) {
			if (!isascii(ch) && !isprint(ch)) {
				if (putchar('M') == EOF || putchar('-') == EOF)
					break;
				ch = toascii(ch);
			}
			if (iscntrl(ch)) {
				if (putchar('^') == EOF ||
				    putchar(ch == '\177' ? '?' :
				    ch | 0100) == EOF)
					break;
				continue;
			}
		}
		if (putchar(ch) == EOF)
			break;
	}
	if (prev != '\n') {
		nflagfirstrun = 0;
	}
	if (ferror(fp)) {
		fprintf(stderr, "Error in file: %s\n", filename);
		rval = 1;
		clearerr(fp);
	}
	if (ferror(stdout)) {
		fprintf(stderr, "Error: stdout\n");
		exit(1);
	}
}

static void
raw_cat(int rfd)
{
	int off, wfd;
	ssize_t nr, nw;
	static size_t bsize;
	static char *buf = NULL;
	struct stat sbuf;

	wfd = fileno(stdout);
	if (buf == NULL) {
		if (fstat(wfd, &sbuf)) {
			fprintf(stderr, "Error in file: %s\n", filename);
			exit(1);
		}
		bsize = MAX(sbuf.st_blksize, 1024);
		if ((buf = malloc(bsize)) == NULL) {
			fprintf(stderr, "Could not allocate enough memory\n");
			exit(1);
		}
	}
	while ((nr = read(rfd, buf, bsize)) > 0)
		for (off = 0; nr; nr -= nw, off += nw)
			if ((nw = write(wfd, buf + off, (size_t)nr)) < 0) {
				fprintf(stderr, "Error: stdout\n");
				exit(1);
			}
	if (nr < 0) {
		fprintf(stderr, "Error in file: %s\n", filename);
		rval = 1;
	}
}
