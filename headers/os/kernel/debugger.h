/*
 * Copyright 2005, Ingo Weinhold, bonefish@users.sf.net.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEBUGGER_H
#define _DEBUGGER_H

#include <signal.h>

#include <image.h>
#include <OS.h>

// include architecture specific definitions
#ifdef __INTEL__
	#include <arch/x86/arch_debugger.h>
#elif __POWERPC__
	#include <arch/ppc/arch_debugger.h>
#endif

typedef struct debug_cpu_state debug_cpu_state;

#ifdef __cplusplus
extern "C" {
#endif

extern status_t	install_default_debugger(port_id debuggerPort);
extern port_id	install_team_debugger(team_id team, port_id debuggerPort);
extern status_t	remove_team_debugger(team_id team);
extern status_t	debug_thread(thread_id thread);
extern void		wait_for_debugger(void);

// team debugging flags
enum {
	// event mask: If a flag is set, any of the team's threads will stop when
	// the respective event occurs. All flags are enabled by default. Always
	// enabled are debugger() calls and hardware exceptions, as well as the
	// deletion of the debugged team.
	B_TEAM_DEBUG_SIGNALS						= 0x00010000,
	B_TEAM_DEBUG_PRE_SYSCALL					= 0x00020000,
	B_TEAM_DEBUG_POST_SYSCALL					= 0x00040000,
	B_TEAM_DEBUG_TEAM_CREATION					= 0x00080000,
	B_TEAM_DEBUG_THREADS						= 0x00100000,
	B_TEAM_DEBUG_IMAGES							= 0x00200000,

	// new thread handling
	B_TEAM_DEBUG_STOP_NEW_THREADS				= 0x01000000,

	B_TEAM_DEBUG_USER_FLAG_MASK					= 0xffff0000,
};

// per-thread debugging flags
enum {
	// event mask: If a flag is set, the thread will stop when the respective
	// event occurs. If there is a corresponding team flag, it is sufficient,
	// if either is set. Per default none of the flags is set.
	B_THREAD_DEBUG_PRE_SYSCALL					= 0x00010000,
	B_THREAD_DEBUG_POST_SYSCALL					= 0x00020000,

	// child thread handling
	B_THREAD_DEBUG_STOP_CHILD_THREADS			= 0x00100000,
	B_THREAD_DEBUG_SYSCALL_TRACE_CHILD_THREADS	= 0x00200000,

	B_THREAD_DEBUG_USER_FLAG_MASK				= 0xffff0000,
};

// reasons for why a thread is invoking the debugger
typedef enum {
	B_THREAD_NOT_RUNNING = 0,	// thread not running (e.g. when starting to
								// debug)
	B_SIGNAL_RECEIVED,			// thread received a signal
	B_TEAM_CREATED,				// thread created a new team
	B_THREAD_CREATED,			// thread spawned a new thread
	B_IMAGE_CREATED,			// thread created a new image
	B_IMAGE_DELETED,			// thread deleted an image
	B_DEBUGGER_CALL,			// thread called debugger()
	B_BREAKPOINT_HIT,			// thread hit a breakpoint
	B_WATCHPOINT_HIT,			// thread hit a memory watchpoint
	B_PRE_SYSCALL_HIT,			// thread starts a syscall
	B_POST_SYSCALL_HIT,			// thread finished a syscall
	B_SINGLE_STEP,				// thread is being single stepped
	B_EXCEPTION_OCCURRED,		// an exception (fault, trap,...) occurred
} debug_why_stopped;

// in case of a B_EXCEPTION_OCCURRED event: the type of the exception
typedef enum {
	B_NMI						= 0,	// non-maskable interrupt
	B_MACHINE_CHECK_EXCEPTION,
	B_SEGMENT_VIOLATION,
	B_ALIGNMENT_EXCEPTION,
	B_DIVIDE_ERROR,
	B_OVERFLOW_EXCEPTION,
	B_BOUNDS_CHECK_EXCEPTION,
	B_INVALID_OPCODE_EXCEPTION,
	B_SEGMENT_NOT_PRESENT,
	B_STACK_FAULT,
	B_GENERAL_PROTECTION_FAULT,
	B_FLOATING_POINT_EXCEPTION,
} debug_exception_type;

extern void get_debug_why_stopped_string(debug_why_stopped whyStopped,
		char *buffer, int32 bufferSize);
extern void get_debug_exception_string(debug_exception_type exception,
		char *buffer, int32 bufferSize);

// Value indicating how a stopped thread shall continue.
enum {
	B_THREAD_DEBUG_HANDLE_EVENT = 0,	// handle the event normally
										// (e.g. a signal is delivered, a
										// CPU fault kills the team,...)
	B_THREAD_DEBUG_IGNORE_EVENT,		// ignore the event and continue as if
										// it didn't occur (e.g. a signal or
										// a CPU fault will be ignored)
};

// watchpoint types (ToDo: Check PPC support.)
enum {
	B_DATA_READ_WATCHPOINT = 0,			// !x86
	B_DATA_WRITE_WATCHPOINT,
	B_DATA_READ_WRITE_WATCHPOINT,
};

// how to apply signal ignore masks
typedef enum {
	B_DEBUG_SIGNAL_MASK_AND	= 0,
	B_DEBUG_SIGNAL_MASK_OR,
	B_DEBUG_SIGNAL_MASK_SET,
} debug_signal_mask_op;

#define B_DEBUG_SIGNAL_TO_MASK(signal) (1ULL << ((signal) - 1))


// #pragma mark -

// messages to the debug nub thread
typedef enum {
	B_DEBUG_MESSAGE_READ_MEMORY	= 0,	// read from the team's memory
	B_DEBUG_MESSAGE_WRITE_MEMORY,		// write to the team's memory
	B_DEBUG_MESSAGE_SET_TEAM_FLAGS,		// set the team's debugging flags
	B_DEBUG_MESSAGE_SET_THREAD_FLAGS,	// set a thread's debugging flags
	B_DEBUG_MESSAGE_RUN_THREAD,			// run a thread full speed
	B_DEBUG_MESSAGE_STEP_THREAD,		// let a thread execute the next
										// instruction
	B_DEBUG_MESSAGE_GET_WHY_STOPPED,	// ask why the thread stopped
	B_DEBUG_MESSAGE_SET_CPU_STATE,		// change a stopped thread's CPU state
	B_DEBUG_MESSAGE_SET_BREAKPOINT,		// set a breakpoint
	B_DEBUG_MESSAGE_CLEAR_BREAKPOINT,	// clear a breakpoint
	B_DEBUG_MESSAGE_SET_WATCHPOINT,		// set a watchpoint
	B_DEBUG_MESSAGE_CLEAR_WATCHPOINT,	// clear a watchpoint
	B_DEBUG_MESSAGE_SET_SIGNAL_MASKS,	// set/get a thread's masks of signals
	B_DEBUG_MESSAGE_GET_SIGNAL_MASKS,	//  the debugger is interested in
	B_DEBUG_MESSAGE_SET_SIGNAL_HANDLER,	// set/get a thread's signal handler for
	B_DEBUG_MESSAGE_GET_SIGNAL_HANDLER,	//  a signal

	B_DEBUG_MESSAGE_PREPARE_HANDOVER,	// prepares the debugged team for being
										// handed over to another debugger;
										// the new debugger can just invoke
										// install_team_debugger()
} debug_nub_message;

// maximal number of bytes to read/write via B_{READ,WRITE]_MEMORY
enum {
	B_MAX_READ_WRITE_MEMORY_SIZE	= 1024,
};

// B_DEBUG_MESSAGE_READ_MEMORY

typedef struct {
	port_id		reply_port;		// port to send the reply to
	void		*address;		// address from which to read
	int32		size;			// number of bytes to read
} debug_nub_read_memory;

typedef struct {
	status_t	error;			// B_OK, if reading went fine
	int32		size;			// the number of bytes actually read
								// > 0, iff error == B_OK
	char		data[B_MAX_READ_WRITE_MEMORY_SIZE];
								// the read data
} debug_nub_read_memory_reply;

// B_DEBUG_MESSAGE_WRITE_MEMORY

typedef struct {
	port_id		reply_port;		// port to send the reply to
	void		*address;		// address to which to write
	int32		size;			// number of bytes to write
	char		data[B_MAX_READ_WRITE_MEMORY_SIZE];
								// data to write
} debug_nub_write_memory;

typedef struct {
	status_t	error;			// B_OK, if writing went fine
} debug_nub_write_memory_reply;

// B_DEBUG_MESSAGE_SET_TEAM_FLAGS

typedef struct {
	int32		flags;			// the new team debugging flags
} debug_nub_set_team_flags;

// B_DEBUG_MESSAGE_SET_THREAD_FLAGS

typedef struct {
	thread_id	thread;			// the thread
	int32		flags;			// the new thread debugging flags
} debug_nub_set_thread_flags;

// B_DEBUG_MESSAGE_RUN_THREAD

typedef struct {
	thread_id	thread;			// the thread to be run
	uint32		handle_event;	// how to handle the occurred event
} debug_nub_run_thread;

// B_DEBUG_MESSAGE_STEP_THREAD

typedef struct {
	thread_id	thread;			// the thread
	uint32		handle_event;	// how to handle the occurred event
} debug_nub_step_thread;

// B_DEBUG_MESSAGE_GET_WHY_STOPPED

typedef struct {
	port_id		reply_port;		// port to send the reply to
	thread_id	thread;			// the thread
} debug_nub_get_why_stopped;

// B_DEBUG_MESSAGE_SET_CPU_STATE

typedef struct {
	thread_id			thread;				// the thread
	debug_cpu_state		cpu_state;			// the new CPU state
} debug_nub_set_cpu_state;

// B_DEBUG_MESSAGE_SET_BREAKPOINT

typedef struct {
	port_id		reply_port;		// port to send the reply to
	void		*address;		// breakpoint address
} debug_nub_set_breakpoint;

typedef struct {
	status_t	error;			// B_OK, if the breakpoint has been set
								// successfully
} debug_nub_set_breakpoint_reply;

// B_DEBUG_MESSAGE_CLEAR_BREAKPOINT

typedef struct {
	void		*address;		// breakpoint address
} debug_nub_clear_breakpoint;

// B_DEBUG_MESSAGE_SET_WATCHPOINT

typedef struct {
	port_id		reply_port;		// port to send the reply to
	void		*address;		// watchpoint address
	uint32		type;			// watchpoint type (see type constants above)
	int32		length;			// number of bytes to watch (typically 1, 2,
								// 4); architecture specific alignment
								// restrictions apply.
} debug_nub_set_watchpoint;

typedef struct {
	status_t	error;			// B_OK, if the watchpoint has been set
								// successfully
} debug_nub_set_watchpoint_reply;

// B_DEBUG_MESSAGE_CLEAR_WATCHPOINT

typedef struct {
	void		*address;		// watchpoint address
} debug_nub_clear_watchpoint;

// B_DEBUG_MESSAGE_SET_SIGNAL_MASKS

typedef struct {
	thread_id				thread;				// the thread
	uint64					ignore_mask;		// the mask for signals the
												// debugger wishes not to be
												// notified of
	uint64					ignore_once_mask;	// the mask for signals the
												// debugger wishes not to be
												// notified of when they next
												// occur
	debug_signal_mask_op	ignore_op;			// what to do with ignore_mask
	debug_signal_mask_op	ignore_once_op;		// what to do with
												// ignore_once_mask
} debug_nub_set_signal_masks;

// B_DEBUG_MESSAGE_GET_SIGNAL_MASKS

typedef struct {
	port_id		reply_port;			// port to send the reply to
	thread_id	thread;				// the thread
} debug_nub_get_signal_masks;

typedef struct {
	status_t	error;				// B_OK, if the thread exists
	uint64		ignore_mask;		// the mask for signals the debugger wishes
									// not to be notified of
	uint64		ignore_once_mask;	// the mask for signals the debugger wishes
									// not to be notified of when they next
									// occur
} debug_nub_get_signal_masks_reply;

// B_DEBUG_MESSAGE_SET_SIGNAL_HANDLER

typedef struct {
	thread_id			thread;		// the thread
	int					signal;		// the signal
	struct sigaction	handler;	// the new signal handler
} debug_nub_set_signal_handler;

// B_DEBUG_MESSAGE_GET_SIGNAL_HANDLER

typedef struct {
	port_id				reply_port;	// port to send the reply to
	thread_id			thread;		// the thread
	int					signal;		// the signal
} debug_nub_get_signal_handler;

typedef struct {
	status_t			error;		// B_OK, if the thread exists
	struct sigaction	handler;	// the signal handler
} debug_nub_get_signal_handler_reply;

// B_DEBUG_MESSAGE_PREPARE_HANDOVER

typedef struct {
	status_t	error;			// B_OK, if the everything went fine
} debug_nub_prepare_handover_reply;

// union of all messages structures sent to the debug nub thread
typedef union {
	debug_nub_read_memory			read_memory;
	debug_nub_write_memory			write_memory;
	debug_nub_set_team_flags		set_team_flags;
	debug_nub_set_thread_flags		set_thread_flags;
	debug_nub_run_thread			run_thread;
	debug_nub_step_thread			step_thread;
	debug_nub_get_why_stopped		get_why_stopped;
	debug_nub_set_cpu_state			set_cpu_state;
	debug_nub_set_breakpoint		set_breakpoint;
	debug_nub_clear_breakpoint		clear_breakpoint;
	debug_nub_set_watchpoint		set_watchpoint;
	debug_nub_clear_watchpoint		clear_watchpoint;
	debug_nub_set_signal_masks		set_signal_masks;
	debug_nub_get_signal_masks		get_signal_masks;
	debug_nub_set_signal_handler	set_signal_handler;
	debug_nub_get_signal_handler	get_signal_handler;
} debug_nub_message_data;


// #pragma mark -

// messages sent to the debugger
typedef enum {
	B_DEBUGGER_MESSAGE_THREAD_STOPPED = 0,	// thread stopped (e.g. when
											// starting to debug or when an
											// error occurs)
	B_DEBUGGER_MESSAGE_PRE_SYSCALL,			// begin of a syscall
	B_DEBUGGER_MESSAGE_POST_SYSCALL,		// end of a syscall
	B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED,		// thread received a signal
	B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED,	// an exception occurred
	B_DEBUGGER_MESSAGE_TEAM_CREATED,		// the debugged team created a new
											// one
	B_DEBUGGER_MESSAGE_TEAM_DELETED,		// the debugged team is gone
	B_DEBUGGER_MESSAGE_THREAD_CREATED,		// a thread has been created
	B_DEBUGGER_MESSAGE_THREAD_DELETED,		// a thread has been deleted
	B_DEBUGGER_MESSAGE_IMAGE_CREATED,		// an image has been created
	B_DEBUGGER_MESSAGE_IMAGE_DELETED,		// an image has been deleted
} debug_debugger_message;

// first member of all debugger messages -- not a message by itself
typedef struct {
	thread_id	thread;			// the thread being the event origin
	team_id		team;			// the thread's team
	port_id		nub_port;		// port to debug nub for this team (only set
								// for synchronous messages)
} debug_origin;

// B_DEBUGGER_MESSAGE_THREAD_STOPPED

typedef struct {
	debug_origin		origin;
	debug_why_stopped	why;		// reason for contacting debugger
	debug_cpu_state		cpu_state;	// cpu state
	void				*data;		// additional data
} debug_thread_stopped;

// B_DEBUGGER_MESSAGE_PRE_SYSCALL

typedef struct {
	debug_origin	origin;
	uint32			syscall;		// the syscall number
	uint32			args[16];		// syscall arguments
} debug_pre_syscall;

// B_DEBUGGER_MESSAGE_POST_SYSCALL

typedef struct {
	debug_origin	origin;
	bigtime_t		start_time;		// time of syscall start
	bigtime_t		end_time;		// time of syscall completion
	uint64			return_value;	// the syscall's return value
	uint32			syscall;		// the syscall number
	uint32			args[16];		// syscall arguments
} debug_post_syscall;

// B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED

typedef struct {
	debug_origin		origin;
	int					signal;		// the signal
	struct sigaction	handler;	// the signal handler
	bool				deadly;		// true, if handling the signal will kill
									// the team
} debug_signal_received;

// B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED

typedef struct {
	debug_origin			origin;
	debug_exception_type	exception;		// the exception
	int						signal;			// the signal that will be sent,
											// when the thread continues
											// normally
} debug_exception_occurred;

// B_DEBUGGER_MESSAGE_TEAM_CREATED

typedef struct {
	debug_origin	origin;
	team_id			new_team;		// the newly created team
} debug_team_created;

// B_DEBUGGER_MESSAGE_TEAM_DELETED

typedef struct {
	debug_origin	origin;			// thread is < 0, team is the deleted team
									// (asynchronous message)
} debug_team_deleted;

// B_DEBUGGER_MESSAGE_THREAD_CREATED

typedef struct {
	debug_origin	origin;			// the thread that created the new thread
	team_id			new_thread;		// the newly created thread
} debug_thread_created;

// B_DEBUGGER_MESSAGE_THREAD_DELETED

typedef struct {
	debug_origin	origin;			// the deleted thread (asynchronous message)
} debug_thread_deleted;

// B_DEBUGGER_MESSAGE_IMAGE_CREATED

typedef struct {
	debug_origin	origin;
	image_info		info;			// info for the image
} debug_image_created;

// B_DEBUGGER_MESSAGE_IMAGE_DELETED

typedef struct {
	debug_origin	origin;
	image_info		info;			// info for the image
} debug_image_deleted;

// union of all messages structures sent to the debugger
typedef union {
	debug_thread_stopped			thread_stopped;
	debug_pre_syscall				pre_syscall;
	debug_post_syscall				post_syscall;
	debug_signal_received			signal_received;
	debug_exception_occurred		exception_occurred;
	debug_team_created				team_created;
	debug_team_deleted				team_deleted;
	debug_thread_created			thread_created;
	debug_thread_deleted			thread_deleted;
	debug_image_created				image_created;
	debug_image_deleted				image_deleted;

	debug_origin					origin;	// for convenience (no real message)
} debug_debugger_message_data;

#ifdef __cplusplus
}	// extern "C"
#endif

#endif	// _DEBUGGER_H
