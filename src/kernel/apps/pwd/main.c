/*
 * Copyright (c) 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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

//#include <sys/cdefs.h>

//#include <sys/param.h>
#include <sys/stat.h>
//#include <sys/types.h>
#include <ktypes.h>

//#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char *getcwd_logical(void);
//void usage(void);
int usage(void);

int
main(int argc, char *argv[])
{
        int physical;
        int ch;
        char *p;

        physical = 1;
        while ((ch = getopt(argc, argv, "LP")) != -1)
                switch (ch) {
                case 'L':
                        physical = 0;
                        break;
                case 'P':
                        physical = 1;
                        break;
                case '?':
                default:
                        usage();
                }
        argc -= optind;
        argv += optind;

        if (argc != 0)
                usage();

        /*
         * If we're trying to find the logical current directory and that
         * fails, behave as if -P was specified.
         */
        if ((!physical && (p = getcwd_logical()) != NULL) ||
            (p = getcwd(NULL, 0)) != NULL)
                printf("%s\n", p);
        //else
        //	err(1, ".");	Taken a quick look at this
	//			perhaps it could be replaced with
	//			just an exit(1) ? Andrew McCall
		

        //exit(0);
	return B_NO_ERROR;
}


//void
int
usage(void)
{

        (void)fprintf(stderr, "usage: pwd [-LP]\n");
        //exit(1);
	return B_ERROR;
}

static char *
getcwd_logical(void)
{
        struct stat log, phy;
        char *pwd;

        /*
         * Check that $PWD is an absolute logical pathname referring to
         * the current working directory.
         */
        if ((pwd = getenv("PWD")) != NULL && *pwd == '/') {
                if (stat(pwd, &log) == -1 || stat(".", &phy) == -1)
                        return (NULL);
                if (log.st_dev == phy.st_dev && log.st_ino == phy.st_ino)
                        return (pwd);
        }

        errno = ENOENT;
        return (NULL);
}

