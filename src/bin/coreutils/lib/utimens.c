/* Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Paul Eggert.  */

/* derived from a function in touch.c */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "utimens.h"

#include <errno.h>

#if HAVE_UTIME_H
# include <utime.h>
#endif

/* Some systems (even some that do have <utime.h>) don't declare this
   structure anywhere.  */
#ifndef HAVE_STRUCT_UTIMBUF
struct utimbuf
{
  long actime;
  long modtime;
};
#endif

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
# define __attribute__(x)
#endif

#ifndef ATTRIBUTE_UNUSED
# define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#endif

/* Set the access and modification time stamps of FD (a.k.a. FILE) to be
   TIMESPEC[0] and TIMESPEC[1], respectively.
   FD must be either negative -- in which case it is ignored --
   or a file descriptor that is open on FILE.
   If TIMESPEC is null, set the time stamps to the current time.  */

int
futimens (int fd ATTRIBUTE_UNUSED,
	  char const *file, struct timespec const timespec[2])
{
  /* There's currently no interface to set file timestamps with
     nanosecond resolution, so do the best we can, discarding any
     fractional part of the timestamp.  */
#if HAVE_WORKING_UTIMES
  struct timeval timeval[2];
  struct timeval const *t;
  if (timespec)
    {
      timeval[0].tv_sec = timespec[0].tv_sec;
      timeval[0].tv_usec = timespec[0].tv_nsec / 1000;
      timeval[1].tv_sec = timespec[1].tv_sec;
      timeval[1].tv_usec = timespec[1].tv_nsec / 1000;
      t = timeval;
    }
  else
    t = NULL;
# if HAVE_FUTIMES
  if (0 <= fd)
    {
      if (futimes (fd, t) == 0)
	return 0;

      /* On GNU/Linux without the futimes syscall and without /proc
	 mounted, glibc futimes fails with errno == ENOENT.  Fall back
	 on utimes if we get a weird error number like that.  */
      switch (errno)
	{
	case EACCES:
	case EIO:
	case EPERM:
	case EROFS:
	  return -1;
	}
    }
# endif
  return utimes (file, t);

#else

  struct utimbuf utimbuf;
  struct utimbuf const *t;
  if (timespec)
    {
      utimbuf.actime = timespec[0].tv_sec;
      utimbuf.modtime = timespec[1].tv_sec;
      t = &utimbuf;
    }
  else
    t = NULL;
  return utime (file, t);

#endif
}

/* Set the access and modification time stamps of FILE to be
   TIMESPEC[0] and TIMESPEC[1], respectively.  */
int
utimens (char const *file, struct timespec const timespec[2])
{
  return futimens (-1, file, timespec);
}
