/* Kernel only exports for kernel add-ons
** 
** Distributed under the terms of the OpenBeOS License.
*/
#ifndef _KERNEL_EXPORT_H
#define _KERNEL_EXPORT_H


#include <SupportDefs.h>
#include <OS.h>


#ifdef __cplusplus
extern "C" {
#endif

/*-------------------------------------------------------------*/

/* disable/restore interrupts on the current CPU */

typedef ulong cpu_status;

extern cpu_status	disable_interrupts(void);
extern void			restore_interrupts(cpu_status status);


/* spinlocks.  Note that acquire/release should be called with
 * interrupts disabled.
 */

typedef vint32 spinlock;

extern void			acquire_spinlock(spinlock *lock);
extern void			release_spinlock(spinlock *lock);


/* interrupt handling support for device drivers */

/* Values returned by interrupt handlers */
#define B_UNHANDLED_INTERRUPT	0	/* pass to next handler */
#define B_HANDLED_INTERRUPT		1	/* don't pass on */
#define B_INVOKE_SCHEDULER		2	/* don't pass on; invoke the scheduler */

typedef int32 (*interrupt_handler)(void *data);

extern long 		install_io_interrupt_handler(long interrupt_number,
						interrupt_handler handler, void *data, ulong flags);
extern long 		remove_io_interrupt_handler(long interrupt_number,
						interrupt_handler handler, void	*data);


/*-------------------------------------------------------------*/
/* timer interrupts services */

/* The BeOS qent structure is probably part of a general double linked list
 * interface used all over the kernel; a struct is required to have a qent
 * entry struct as first element, so it can be linked to other elements
 * easily. The key field is probably just an id, eventually used to order
 * the list.
 * Since we don't use this kind of interface, but we have to provide it
 * to keep compatibility, we can use the qent struct for other purposes...
 *
 * ToDo: don't do this! Drop source compatibility, but don't overdefine those values!
 */
typedef struct qent {
	int64		key;			/* We use this as the sched time */
	struct qent	*next;			/* This is used as a pointer to next timer */
	struct qent	*prev;			/* This can be used for callback args */
} qent;

typedef struct timer timer;
typedef	int32 (*timer_hook)(timer *);

struct timer {
	qent		entry;
	uint16		flags;
	uint16		cpu;
	timer_hook	hook;
	bigtime_t	period;
};

#define B_ONE_SHOT_ABSOLUTE_TIMER	1
#define	B_ONE_SHOT_RELATIVE_TIMER	2
#define	B_PERIODIC_TIMER			3

extern status_t		add_timer(timer *t, timer_hook hook, bigtime_t period, int32 flags);
extern bool			cancel_timer(timer *t);


/*-------------------------------------------------------------*/
/* kernel threads */

extern thread_id	spawn_kernel_thread(thread_func function, const char *threadName, 
						long priority, void *arg);


/*-------------------------------------------------------------*/
/* signal functions */

extern int			send_signal_etc(pid_t thread, uint sig, uint32 flags);
extern int			has_signals_pending(void *_thread);


/*-------------------------------------------------------------*/
/* snooze functions */

extern status_t		snooze_etc(bigtime_t usecs, int timebase, uint32 flags);


/*-------------------------------------------------------------*/
/* virtual memory buffer functions */

#define B_DMA_IO		0x00000001
#define B_READ_DEVICE	0x00000002

typedef struct {
	void	*address;	/* address in physical memory */
	ulong	size;		/* size of block */
} physical_entry;

extern long			lock_memory(void *buffer, ulong numBytes, ulong flags);
extern long			unlock_memory(void *buffer, ulong numBytes, ulong flags);
extern long			get_memory_map(const void *buffer, ulong size,
						physical_entry *table, long numEntries);

/* address specifications for mapping physical memory */
#define	B_ANY_KERNEL_BLOCK_ADDRESS	(B_ANY_KERNEL_ADDRESS + 1)

/* call to map physical memory - typically used for memory-mapped i/o */

extern area_id		map_physical_memory(const char *areaName, void *physicalAddress,
						size_t size, uint32 flags, uint32 protection, void **mappedAddress);


/* MTR attributes for mapping physical memory (Intel Architecture only) */
// ToDo: what have those to do here?
#define	B_MTR_UC	0x10000000
#define	B_MTR_WC	0x20000000
#define	B_MTR_WT	0x30000000
#define	B_MTR_WP	0x40000000
#define	B_MTR_WB	0x50000000
#define	B_MTR_MASK	0xf0000000


/*-------------------------------------------------------------*/
/* hardware inquiry */

/* platform_type return value is defined in OS.h */

extern platform_type platform();

#if __POWERPC__
extern long			motherboard_version(void);
extern long			io_card_version(void);
#endif

/*-------------------------------------------------------------*/
/* primitive kernel debugging facilities */

/* Standard debug output is on...
 *	mac: modem port
 *	pc: com1
 *	...at 19.2 kbaud, no parity, 8 bit, 1 stop bit.
 *
 * Note: the kernel settings file can override these defaults
 */

#if __GNUC__
extern void			dprintf(const char *format, ...)		/* just like printf */
                                  __attribute__ ((format (__printf__, 1, 2)));
extern void			kprintf(const char *fmt, ...)			/* only for debugger cmds */
                                  __attribute__ ((format (__printf__, 1, 2)));
#else
extern void			dprintf(const char *format, ...);		/* just like printf */
extern void			kprintf(const char *fmt, ...);			/* only for debugger cmds */
#endif

extern bool			set_dprintf_enabled(bool new_state);	/* returns old state */

extern void			panic(const char *format, ...);

extern void			kernel_debugger(const char *message);	/* enter kernel debugger */
extern ulong		parse_expression(char *str);			/* utility for debugger cmds */

/* special return codes for kernel debugger */
#define  B_KDEBUG_CONT   2
#define  B_KDEBUG_QUIT   3

typedef int (*debugger_command_hook)(int argc, char **argv);

extern int			add_debugger_command(char *name, debugger_command_hook hook, char *help);
extern int			remove_debugger_command(char *name, debugger_command_hook hook); 

extern status_t 	load_driver_symbols(char *driver_name);


/*-------------------------------------------------------------*/
/* misc */

extern void			spin(bigtime_t microseconds);
	/* does a busy delay loop for at least "microseconds" */

typedef int (*daemon_hook)(void *arg, int iteration);

extern status_t		register_kernel_daemon(daemon_hook hook, void *arg, int frequency);
extern status_t		unregister_kernel_daemon(daemon_hook hook, void *arg);

extern void			call_all_cpus(void (*f)(void *, int), void *cookie);

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_EXPORT_H */
