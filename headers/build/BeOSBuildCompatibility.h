#ifndef BEOS_BUILD_COMPATIBILITY_H
#define BEOS_BUILD_COMPATIBILITY_H

#if defined(HAIKU_HOST_PLATFORM_CYGWIN)
#	ifndef __addr_t_defined
#		define __addr_t_defined
#	endif
#endif

#if defined(HAIKU_HOST_PLATFORM_CYGWIN) || defined(HAIKU_HOST_PLATFORM_SUNOS)
#ifndef DEFFILEMODE
#define DEFFILEMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)
#endif

#ifndef S_IUMSK
#define	S_IUMSK 07777
#endif

#include <ctype.h>
#endif

#ifdef HAIKU_HOST_PLATFORM_SUNOS
#	include <limits.h>
#	ifndef NAME_MAX
#		define NAME_MAX	MAXNAMELEN
#	endif
#endif

typedef unsigned long	haiku_build_addr_t;
#define addr_t			haiku_build_addr_t

#include <Errors.h>

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Is kernel-only under Linux.
extern size_t   strlcpy(char* dest, const char* source, size_t length);
extern size_t	strlcat(char* dest, const char* source, size_t length);

#if defined(HAIKU_HOST_PLATFORM_FREEBSD) || defined(HAIKU_HOST_PLATFORM_DARWIN)
extern size_t	strnlen(const char* string, size_t length);
#endif

#if defined(HAIKU_HOST_PLATFORM_CYGWIN) || defined(HAIKU_HOST_PLATFORM_SUNOS)
extern char*	stpcpy(char* dest, const char* src);
extern char*	strcasestr(const char* s, const char* find);
#endif

// BeOS only
extern ssize_t  read_pos(int fd, off_t pos, void* buffer, size_t count);
extern ssize_t  write_pos(int fd, off_t pos, const void* buffer, size_t count);
extern ssize_t	readv_pos(int fd, off_t pos, const struct iovec* vec,
					size_t count);
extern ssize_t	writev_pos(int fd, off_t pos, const struct iovec* vec,
					size_t count);


// There's no O_NOTRAVERSE under Linux and FreeBSD -- we replace it with a flag
// that won't be used by our tools, preferrably a non-portable one; a fixed
// constant could always lead to trouble on the host.
// We can abuse this flag for our purposes as we filter it in libroot.
#ifndef O_NOTRAVERSE
#	ifdef O_NOCTTY
#		define O_NOTRAVERSE	O_NOCTTY
#	elif defined(O_RANDOM)
#		define O_NOTRAVERSE O_RANDOM
#	else
#		error "Search for a proper replacement value for O_NOTRAVERSE"
#	endif
#endif

#ifndef S_IUMSK
#	define S_IUMSK ALLPERMS
#endif


// remap strerror()
extern char* _haiku_build_strerror(int errnum);

#ifndef BUILDING_HAIKU_ERROR_MAPPER

#undef strerror
#define strerror(errnum)	_haiku_build_strerror(errnum)

#endif	// BUILDING_HAIKU_ERROR_MAPPER

#ifdef __cplusplus
} // extern "C"
#endif

#endif	// BEOS_BUILD_COMPATIBILITY_H

