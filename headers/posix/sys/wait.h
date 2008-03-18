#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
/* 
** Distributed under the terms of the OpenBeOS License.
*/


#include <sys/types.h>
#include <signal.h>


/* waitpid()/waitid() options */
#define WNOHANG		0x01
#define WUNTRACED	0x02
#define WCONTINUED	0x04
#define WEXITED		0x08
#define WSTOPPED	0x10
#define WNOWAIT		0x20

/* macros to interprete wait()/waitpid() status */
#define WIFEXITED(value)	(((value) & ~0xff) == 0)
#define WEXITSTATUS(value)	((value) & 0xff)
#define WIFSIGNALED(value)	((((value) >> 8) & 0xff) != 0)
#define WTERMSIG(value)		(((value) >> 8) & 0xff)
#define WIFSTOPPED(value)	((((value) >> 16) & 0xff) != 0)
#define WSTOPSIG(value)		(((value) >> 16) & 0xff)
#define WIFCORED(value)		((value) & 0x10000)
#define WIFCONTINUED(value)	((value) & 0x20000)

// TODO: waitid() is part of the real-time signal extension. Uncomment when
// implemented!
#if 0
/* ID types for waitid() */
typedef enum {
	P_ALL,		/* wait for any children, ignore ID */
	P_PID,		/* wait for the child whose process ID matches */
	P_PGID		/* wait for any child whose process group ID matches */
} idtype_t;
#endif	// 0


#ifdef __cplusplus
extern "C" {
#endif

extern pid_t wait(int *_status);
extern pid_t waitpid(pid_t pid, int *_status, int options);
//extern int waitid(idtype_t idType, id_t id, siginfo_t *info, int options);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_WAIT_H */
