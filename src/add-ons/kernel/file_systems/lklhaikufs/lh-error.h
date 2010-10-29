/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

// Define LH_$NAME to have the value it has in Linux.
// When this header is included from a compilation unit that includes
// Linux headers, the macro will also assert that the values are
// the same as the ones in Linux's headers. This makes conversion from
// Linux error codes to LH_ values trivial: the values are identical.


#if defined(BRIDGE_LKL)
# include <asm/errno.h>
# define DEF_ASSERT_ERROR(NAME)											\
	const char assert_LH_and_LINUX_values_for_##NAME##_are_equal		\
		[ (NAME == LH_##NAME) ? 0 : -1 ];
#else
# define DEF_ASSERT_ERROR(NAME)
#endif

#define LH_ENOMEM			12
#define LH_EACCES			13
#define LH_EINTR			 4
#define LH_EIO				 5
#define LH_EBUSY			16
#define LH_EFAULT			14
#define LH_ETIMEDOUT		110
#define LH_EAGAIN			11
#define LH_EBADF			 9
#define LH_EEXIST			17
#define LH_EINVAL			22
#define LH_ENAMETOOLONG		36
#define LH_ENOENT			 2
#define LH_EPERM			 1
#define LH_ENOTDIR			20
#define LH_EISDIR			21
#define LH_ENOTEMPTY		39
#define LH_ENOSPC			28
#define LH_EROFS			30
#define LH_EMFILE			24
#define LH_EXDEV			18
#define LH_ELOOP			40
#define LH_ENOEXEC			 8
#define LH_EPIPE			32



DEF_ASSERT_ERROR(ENOMEM);
DEF_ASSERT_ERROR(EACCES);
DEF_ASSERT_ERROR(EINTR);
DEF_ASSERT_ERROR(EIO);
DEF_ASSERT_ERROR(EBUSY);
DEF_ASSERT_ERROR(EFAULT);
DEF_ASSERT_ERROR(ETIMEDOUT);
DEF_ASSERT_ERROR(EAGAIN);
DEF_ASSERT_ERROR(EBADF);
DEF_ASSERT_ERROR(EEXIST);
DEF_ASSERT_ERROR(EINVAL);
DEF_ASSERT_ERROR(ENAMETOOLONG);
DEF_ASSERT_ERROR(ENOENT);
DEF_ASSERT_ERROR(EPERM);
DEF_ASSERT_ERROR(ENOTDIR);
DEF_ASSERT_ERROR(EISDIR);
DEF_ASSERT_ERROR(ENOTEMPTY);
DEF_ASSERT_ERROR(ENOSPC);
DEF_ASSERT_ERROR(EROFS);
DEF_ASSERT_ERROR(EMFILE);
DEF_ASSERT_ERROR(EXDEV);
DEF_ASSERT_ERROR(ELOOP);
DEF_ASSERT_ERROR(ENOEXEC);
DEF_ASSERT_ERROR(EPIPE);


#ifdef BRIDGE_HAIKU

static status_t
lh_to_haiku_error_x(int err)
{
	switch(err)
	{
	case 0:					return B_OK;
	case LH_ENOMEM:			return B_NO_MEMORY;
	case LH_EACCES:			return B_PERMISSION_DENIED;
	case LH_EINTR: 			return B_INTERRUPTED;
	case LH_EIO: 			return B_IO_ERROR;
	case LH_EBUSY: 			return B_BUSY;
	case LH_EFAULT: 		return B_BAD_ADDRESS;
	case LH_ETIMEDOUT: 		return B_TIMED_OUT;
	case LH_EAGAIN: 		return B_WOULD_BLOCK;
	case LH_EBADF: 			return B_FILE_ERROR;
	case LH_EEXIST: 		return B_FILE_EXISTS;
	case LH_EINVAL: 		return B_BAD_VALUE;
	case LH_ENAMETOOLONG: 	return B_NAME_TOO_LONG;
	case LH_ENOENT: 		return B_ENTRY_NOT_FOUND;
	case LH_EPERM: 			return B_NOT_ALLOWED;
	case LH_ENOTDIR: 		return B_NOT_A_DIRECTORY;
	case LH_EISDIR: 		return B_IS_A_DIRECTORY;
	case LH_ENOTEMPTY: 		return B_DIRECTORY_NOT_EMPTY;
	case LH_ENOSPC: 		return B_DEVICE_FULL;
	case LH_EROFS: 			return B_READ_ONLY_DEVICE;
	case LH_EMFILE: 		return B_NO_MORE_FDS;
	case LH_EXDEV: 			return B_CROSS_DEVICE_LINK;
	case LH_ELOOP: 			return B_LINK_LIMIT;
	case LH_ENOEXEC: 		return B_NOT_AN_EXECUTABLE;
	case LH_EPIPE: 			return B_BUSTED_PIPE;
	default:				return B_ERROR;
	}
}

static status_t
lh_to_haiku_error_str(int err, const char* func)
{
	status_t herr = lh_to_haiku_error_x(err);
	if (herr != B_OK) {
		dprintf("lklfs:: %s error rc=%d err=%s\n", func, (int) herr, strerror(herr));
	}
	return herr;
}


#define lh_to_haiku_error(err)		   lh_to_haiku_error_str(err, __func__)

#endif // BRIDGE_HAIKU

