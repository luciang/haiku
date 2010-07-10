/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */


/*! Some of the file open()/fcntl() flags in Haiku's headers have
 *	different values from Linux's header value. When an open flag is
 *	passed from Haiku to Linux it must:
 *	- be converted from Haiku to the LH_ equivalent
 *	- from the LH_ equivalent to the proper Linux value.
 *
 * Because we cannot include both Linux and Haiku headers in the same
 * compilation unit, we must provide new definitions for the LH_* values.
 */



// Define LH_$NAME to have the value it has in Haiku.
// When this header is included from a compilation unit that includes
// Haiku headers, the macro will also assert that the values are
// the same as the ones in Haiku's headers. This makes conversion from
// Haiku values to LH_ values trivial: the values are identical.

#if defined(BRIDGE_HAIKU)

# include <fcntl.h>
# define DEF_ASSERT(NAME, HAIKU_VALUE)									\
	static const int LH_##NAME = HAIKU_VALUE;							\
	const char assert_LH_and_HAIKU_values_for_##NAME##_are_equal		\
		[ (NAME == HAIKU_VALUE) ? 0 : -1 ];

#elif defined(BRIDGE_LKL)

# define DEF_ASSERT(NAME, HAIKU_VALUE)									\
	static const int LH_##NAME = HAIKU_VALUE;

#else
# error You must define either BRIDGE_HAIKU or BRIDGE_LKL before including this.
#endif

/* commands that can be passed to fcntl() */
DEF_ASSERT(F_DUPFD,		0x0001)		/* duplicate fd */
DEF_ASSERT(F_GETFD,		0x0002)		/* get fd flags */
DEF_ASSERT(F_SETFD,		0x0004)		/* set fd flags */
DEF_ASSERT(F_GETFL,		0x0008)		/* get file status flags and access mode */
DEF_ASSERT(F_SETFL,		0x0010)		/* set file status flags */
DEF_ASSERT(F_GETLK,		0x0020)		/* get locking information */
DEF_ASSERT(F_SETLK,		0x0080)		/* set locking information */
DEF_ASSERT(F_SETLKW,	0x0100)		/* as above, but waits if blocked */

/* advisory locking types */
DEF_ASSERT(F_RDLCK,		0x0040)		/* read or shared lock */
DEF_ASSERT(F_UNLCK,		0x0200)		/* unlock */
DEF_ASSERT(F_WRLCK,		0x0400)		/* write or exclusive lock */

/* file descriptor flags for fcntl() */
DEF_ASSERT(FD_CLOEXEC,	1)			/* close on exec */

/* file access modes for open() */
DEF_ASSERT(O_RDONLY,	0x0000)		/* read only */
DEF_ASSERT(O_WRONLY,	0x0001)		/* write only */
DEF_ASSERT(O_RDWR,		0x0002)		/* read and write */
DEF_ASSERT(O_ACCMODE,	0x0003)		/* mask to get the access modes above */
//DEF_ASSERT(O_RWMASK,	0x0003)
	// NOTE: same value as ACCMODE, not defined by LKL

/* flags for open() */
DEF_ASSERT(O_EXCL,		0x0100)		/* exclusive creat */
DEF_ASSERT(O_CREAT,		0x0200)		/* create and open file */
DEF_ASSERT(O_TRUNC,		0x0400)		/* open with truncation */
DEF_ASSERT(O_NOCTTY,	0x1000)		/* don't make tty the controlling tty */
//DEF_ASSERT(O_NOTRAVERSE,0x2000)		/* do not traverse leaf link */
	// NOTE: not defined by LKL, must be handled separately.

/* flags for open() and fcntl() */
DEF_ASSERT(O_CLOEXEC,	0x00000040)	/* close on exec */
DEF_ASSERT(O_NONBLOCK,	0x00000080)	/* non blocking io */
DEF_ASSERT(O_NDELAY,	0x00000080)
DEF_ASSERT(O_APPEND,	0x00000800)	/* to end of file */
// DEF_ASSERT(O_TEXT,		0x00004000)	/* CR-LF translation */
// DEF_ASSERT(O_BINARY,	0x00008000)	/* no translation */
	// Haiku's kernel does not seem to make use of O_TEXT/O_BINARY.
DEF_ASSERT(O_SYNC,		0x00010000)	/* write synchronized I/O file integrity */
DEF_ASSERT(O_RSYNC,		0x00020000)	/* read synchronized I/O file integrity */
DEF_ASSERT(O_DSYNC,		0x00040000)	/* write synchronized I/O data integrity */
DEF_ASSERT(O_NOFOLLOW,	0x00080000)	/* fail on symlinks */
// DEF_ASSERT(O_NOCACHE,	0x00100000)
	// NOTE: same as O_DIRECT, not defined by LKL
DEF_ASSERT(O_DIRECT,	0x00100000)
DEF_ASSERT(O_DIRECTORY,	0x00200000)	/* fail if not a directory */

//DEF_ASSERT(O_TEMPORARY,	0x00400000)	/* used to avoid writing temporary */
										/* files to disk */
	// NOTE: does not seem to be used in Haiku at this moment.

DEF_ASSERT(AT_FDCWD,	(-1))		/* CWD FD for the *at() functions */

DEF_ASSERT(AT_SYMLINK_NOFOLLOW,	0x01)	/* fstatat(), fchmodat(), fchownat(),
										utimensat() */
DEF_ASSERT(AT_SYMLINK_FOLLOW,	0x02)	/* linkat() */
DEF_ASSERT(AT_REMOVEDIR,		0x04)	/* unlinkat() */
DEF_ASSERT(AT_EACCESS,			0x08)	/* faccessat() */





#if defined(BRIDGE_HAIKU)


// Convert from Haiku open() mode to the LH_ conversion equivalent
static int
haiku_to_lh_openMode(int haikuOpenMode)
{
	// because they have the same values, conversion is trivial.
	return haikuOpenMode;
}

#elif defined(BRIDGE_LKL)

#define SET_LKL_MODE_BITS(NAME, src, dst)						\
	do { dst |= ((src & LH_##NAME) == LH_##NAME) ? NAME : 0; } while (0)


// Convert the LH_ conversion mode to the LKL equivalent
static int
lh_to_lkl_openMode(int lhOpenMode)
{
	int lklOpenMode = 0;

	/* file access modes for open() */
	SET_LKL_MODE_BITS(O_RDONLY,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_WRONLY,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_RDWR,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_ACCMODE,		lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_RWMASK,		lhOpenMode, lklOpenMode);
		// NOTE: same value as ACCMODE, not defined by LKL

	/* flags for open() */
	SET_LKL_MODE_BITS(O_EXCL,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_CREAT,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_TRUNC,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_NOCTTY,			lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_NOTRAVERSE,	lhOpenMode, lklOpenMode);
		// NOTE: not defined by LKL, must be handled separately.

	/* flags for open() and fcntl() */
	SET_LKL_MODE_BITS(O_CLOEXEC,		lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_NONBLOCK,		lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_NDELAY,			lhOpenMode, lklOpenMode);
	SET_LKL_MODE_BITS(O_APPEND,			lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_TEXT,		lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_BINARY,		lhOpenMode, lklOpenMode);
		// Haiku's kernel does not seem to make use of O_TEXT/O_BINARY.


	// LKL does not support O_RSYNC/O_DSYNC.
	// TODO: decide if the conversion to O_SYNC is appropriate.
	if ((lhOpenMode & LH_O_RSYNC) != 0 || (lhOpenMode & LH_O_DSYNC) != 0) {
		lhOpenMode &= ~ LH_O_RSYNC;
		lhOpenMode &= ~ LH_O_DSYNC;
		lhOpenMode |= LH_O_SYNC;
	}
	SET_LKL_MODE_BITS(O_SYNC,			lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_RSYNC,		lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_DSYNC,		lhOpenMode, lklOpenMode);

	SET_LKL_MODE_BITS(O_NOFOLLOW,		lhOpenMode, lklOpenMode);


	// SET_LKL_MODE_BITS(O_NOCACHE,		lhOpenMode, lklOpenMode);
		// NOTE: same as O_DIRECT, not defined by LKL
	SET_LKL_MODE_BITS(O_DIRECT,			lhOpenMode, lklOpenMode);

	SET_LKL_MODE_BITS(O_DIRECTORY,		lhOpenMode, lklOpenMode);
	// SET_LKL_MODE_BITS(O_TEMPORARY,	lhOpenMode, lklOpenMode);
		// NOTE: does not seem to be used in Haiku at this moment.

	return lklOpenMode;
}

#else
# error You must define either BRIDGE_HAIKU or BRIDGE_LKL before including this.
#endif
