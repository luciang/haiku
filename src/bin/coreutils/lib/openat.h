/* provide a replacement openat function
   Copyright (C) 2004 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* written by Jim Meyering */

#if HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifndef AT_FDCWD
# define AT_FDCWD (-3041965) /* same value as Solaris 9 */

# ifdef __OPENAT_PREFIX
#  undef openat
#  define __OPENAT_CONCAT(x, y) x ## y
#  define __OPENAT_XCONCAT(x, y) __OPENAT_CONCAT (x, y)
#  define __OPENAT_ID(y) __OPENAT_XCONCAT (__OPENAT_PREFIX, y)
#  define openat __OPENAT_ID (openat)
int openat (int fd, char const *filename, int flags, /* mode_t mode */ ...);
# endif
#endif
