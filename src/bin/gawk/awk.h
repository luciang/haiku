/*
 * awk.h -- Definitions for gawk. 
 */

/* 
 * Copyright (C) 1986, 1988, 1989, 1991-2003 the Free Software Foundation, Inc.
 * 
 * This file is part of GAWK, the GNU implementation of the
 * AWK Programming Language.
 * 
 * GAWK is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * GAWK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 */

/* ------------------------------ Includes ------------------------------ */

/*
 * config.h absolutely, positively, *M*U*S*T* be included before
 * any system headers.  Otherwise, extreme death, destruction
 * and loss of life results.
 *
 * Well, OK, gawk just won't work on systems using egcs and LFS.  But
 * that's almost as bad.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE	1	/* enable GNU extensions */
#endif /* _GNU_SOURCE */

#include <stdio.h>
#include <assert.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif /* HAVE_LIMITS_H */
#include <ctype.h>
#include <setjmp.h>

#include "gettext.h"
#define _(msgid)  gettext(msgid)
#define N_(msgid) msgid

#if ! (defined(HAVE_LIBINTL_H) && defined(ENABLE_NLS) && ENABLE_NLS > 0)
#ifndef LOCALEDIR
#define LOCALEDIR NULL
#endif /* LOCALEDIR */
#endif

#if defined(HAVE_STDARG_H) && defined(__STDC__) && __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include <signal.h>
#include <time.h>
#include <errno.h>
#if ! defined(errno) && ! defined(MSDOS) && ! defined(OS2)
extern int errno;
#endif
#ifdef HAVE_SIGNUM_H
#include <signum.h>
#endif
#if defined(HAVE_MBRLEN) && defined(HAVE_MBRTOWC) && defined(HAVE_WCHAR_H) && defined(HAVE_WCTYPE_H)
/* We can handle multibyte strings.  */
#define MBS_SUPPORT
#include <wchar.h>
#include <wctype.h>
#endif

/* ----------------- System dependencies (with more includes) -----------*/

/* This section is the messiest one in the file, not a lot that can be done */

/* First, get the ctype stuff right; from Jim Meyering */
#if defined(STDC_HEADERS) || (!defined(isascii) && !defined(HAVE_ISASCII))
#define IN_CTYPE_DOMAIN(c) 1
#else
#define IN_CTYPE_DOMAIN(c) isascii((unsigned char) c)
#endif

#ifdef isblank
#define ISBLANK(c) (IN_CTYPE_DOMAIN(c) && isblank((unsigned char) c))
#else
#define ISBLANK(c) ((c) == ' ' || (c) == '\t')
#endif
#ifdef isgraph
#define ISGRAPH(c) (IN_CTYPE_DOMAIN(c) && isgraph((unsigned char) c))
#else
#define ISGRAPH(c) (IN_CTYPE_DOMAIN(c) && isprint((unsigned char) c) && !isspace((unsigned char) c))
#endif

#define ISPRINT(c) (IN_CTYPE_DOMAIN (c) && isprint ((unsigned char) c))
#define ISDIGIT(c) (IN_CTYPE_DOMAIN (c) && isdigit ((unsigned char) c))
#define ISALNUM(c) (IN_CTYPE_DOMAIN (c) && isalnum ((unsigned char) c))
#define ISALPHA(c) (IN_CTYPE_DOMAIN (c) && isalpha ((unsigned char) c))
#define ISCNTRL(c) (IN_CTYPE_DOMAIN (c) && iscntrl ((unsigned char) c))
#define ISLOWER(c) (IN_CTYPE_DOMAIN (c) && islower ((unsigned char) c))
#define ISPUNCT(c) (IN_CTYPE_DOMAIN (c) && ispunct (unsigned char) (c))
#define ISSPACE(c) (IN_CTYPE_DOMAIN (c) && isspace ((unsigned char) c))
#define ISUPPER(c) (IN_CTYPE_DOMAIN (c) && isupper ((unsigned char) c))
#define ISXDIGIT(c) (IN_CTYPE_DOMAIN (c) && isxdigit ((unsigned char) c))

#ifndef TOUPPER
#define TOUPPER(c)	toupper((unsigned char) c)
#endif
#ifndef TOLOWER
#define TOLOWER(c)	tolower((unsigned char) c)
#endif

#ifdef __STDC__
#define	P(s)	s
#define MALLOC_ARG_T size_t
#else	/* not __STDC__ */
#define	P(s)	()
#define MALLOC_ARG_T unsigned
#define volatile
#endif	/* not __STDC__ */

#ifndef VMS
#include <sys/types.h>
#include <sys/stat.h>
#else	/* VMS */
#include <stddef.h>
#include <stat.h>
#include <file.h>	/* avoid <fcntl.h> in io.c */
#endif	/* VMS */

#if ! defined(S_ISREG) && defined(S_IFREG)
#define	S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#ifdef STDC_HEADERS
#include <stdlib.h>
#else	/* not STDC_HEADERS */
#include "protos.h"
#endif	/* not STDC_HEADERS */

#ifdef HAVE_STRING_H
#include <string.h>
#ifdef NEED_MEMORY_H
#include <memory.h>
#endif	/* NEED_MEMORY_H */
#else	/* not HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif	/* HAVE_STRINGS_H */
#endif	/* not HAVE_STRING_H */

#ifdef NeXT
#if __GNUC__ < 2 || __GNUC_MINOR__ < 7
#include <libc.h>
#endif
#undef atof
#define getopt GNU_getopt
#define GFMT_WORKAROUND
#endif	/* NeXT */

#if defined(atarist) || defined(VMS)
#include <unixlib.h>
#endif	/* atarist || VMS */

#if ! defined(MSDOS) && ! defined(OS2) && ! defined(WIN32) && ! defined(__EMX__) && ! defined(__CYGWIN__) && ! (defined(__BEOS__) || defined(__HAIKU__)) && ! defined(O_BINARY) /*duh*/
#define O_BINARY	0
#endif

#if defined(TANDEM)
#define variable variabl
#define open(name, how, mode)	open(name, how)	/* !!! ANSI C !!! */
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#ifndef HAVE_VPRINTF
/* if you don't have vprintf, try this and cross your fingers. */
#ifdef	HAVE_DOPRNT
#define vfprintf(fp,fmt,arg)	_doprnt((fmt), (arg), (fp))
#else	/* not HAVE_DOPRNT */
you
lose
#endif	/* not HAVE_DOPRNT */
#endif	/* HAVE_VPRINTF */

#ifndef HAVE_SETLOCALE
#define setlocale(locale, val)	/* nothing */
#endif /* HAVE_SETLOCALE */

/* use this as lintwarn("...")
   this is a hack but it gives us the right semantics */
#define lintwarn (*(set_loc(__FILE__, __LINE__),lintfunc))

#ifdef VMS
#include "vms/redirect.h"
#endif  /*VMS*/

#ifdef atarist
#include "unsupported/atari/redirect.h"
#endif

#define RE_TRANSLATE_TYPE const char *
#define	GNU_REGEX
#ifdef GNU_REGEX
#include "regex.h"
typedef struct Regexp {
	struct re_pattern_buffer pat;
	struct re_registers regs;
} Regexp;
#define	RESTART(rp,s)	(rp)->regs.start[0]
#define	REEND(rp,s)	(rp)->regs.end[0]
#define	SUBPATSTART(rp,s,n)	(rp)->regs.start[n]
#define	SUBPATEND(rp,s,n)	(rp)->regs.end[n]
#define	NUMSUBPATS(rp,s)	(rp)->regs.num_regs
#endif	/* GNU_REGEX */

/* Stuff for losing systems. */
#ifdef STRTOD_NOT_C89
extern double gawk_strtod();
#define strtod gawk_strtod
#endif

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 7)
# define __attribute__(x)
#endif

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#endif /* ATTRIBUTE_UNUSED */

#ifndef ATTRIBUTE_NORETURN
#define ATTRIBUTE_NORETURN __attribute__ ((__noreturn__))
#endif /* ATTRIBUTE_NORETURN */

#ifndef ATTRIBUTE_PRINTF
#define ATTRIBUTE_PRINTF(m, n) __attribute__ ((__format__ (__printf__, m, n)))
#define ATTRIBUTE_PRINTF_1 ATTRIBUTE_PRINTF(1, 2)
#endif /* ATTRIBUTE_PRINTF */

/* We use __extension__ in some places to suppress -pedantic warnings
   about GCC extensions.  This feature didn't work properly before
   gcc 2.8.  */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8)
#define __extension__
#endif

/* this is defined by Windows32 extension libraries. It must be added to
 * every variable which is exported (including function pointers) */
#if defined(WIN32_EXTENSION) && !defined(ATTRIBUTE_EXPORTED)
# define ATTRIBUTE_EXPORTED __declspec(dllimport)
#else
# define ATTRIBUTE_EXPORTED
#endif


/* ------------------ Constants, Structures, Typedefs  ------------------ */

#ifndef AWKNUM
#define AWKNUM	double
#endif

#ifndef TRUE
/* a bit hackneyed, but what the heck */
#define TRUE	1
#define FALSE	0
#endif

#define LINT_INVALID	1	/* only warn about invalid */
#define LINT_ALL	2	/* warn about all things */

/* Figure out what '\a' really is. */
#ifdef __STDC__
#define BELL	'\a'		/* sure makes life easy, don't it? */
#else
#	if 'z' - 'a' == 25	/* ascii */
#		if 'a' != 97	/* machine is dumb enough to use mark parity */
#			define BELL	'\207'
#		else
#			define BELL	'\07'
#		endif
#	else
#		define BELL	'\057'
#	endif
#endif

typedef enum nodevals {
	/* illegal entry == 0 */
	Node_illegal,

	/* binary operators  lnode and rnode are the expressions to work on */
	Node_times,
	Node_quotient,
	Node_mod,
	Node_plus,
	Node_minus,
	Node_cond_pair,		/* conditional pair (see Node_line_range) */
	Node_subscript,
	Node_concat,
	Node_exp,

	/* unary operators   subnode is the expression to work on */
	Node_preincrement,
	Node_predecrement,
	Node_postincrement,
	Node_postdecrement,
	Node_unary_minus,
	Node_field_spec,

	/* assignments   lnode is the var to assign to, rnode is the exp */
	Node_assign,
	Node_assign_times,
	Node_assign_quotient,
	Node_assign_mod,
	Node_assign_plus,
	Node_assign_minus,
	Node_assign_exp,

	/* boolean binaries   lnode and rnode are expressions */
	Node_and,
	Node_or,

	/* binary relationals   compares lnode and rnode */
	Node_equal,
	Node_notequal,
	Node_less,
	Node_greater,
	Node_leq,
	Node_geq,
	Node_match,
	Node_nomatch,

	/* unary relationals   works on subnode */
	Node_not,

	/* program structures */
	Node_rule_list,		/* lnode is a rule, rnode is rest of list */
	Node_rule_node,		/* lnode is pattern, rnode is statement */
	Node_statement_list,	/* lnode is statement, rnode is more list */
	Node_switch_body,	/* lnode is the case list, rnode is default list */
	Node_case_list,	/* lnode is the case, rnode is a statement list */
	Node_if_branches,	/* lnode is to run on true, rnode on false */
	Node_expression_list,	/* lnode is an exp, rnode is more list */
	Node_param_list,	/* lnode is a variable, rnode is more list */

	/* keywords */
	Node_K_if,		/* lnode is conditonal, rnode is if_branches */
	Node_K_switch,		/* lnode is switch value, rnode is body of case statements */
	Node_K_case,		/* lnode is case value, rnode is stuff to run */
	Node_K_default,		/* lnode is empty, rnode is stuff to run */
	Node_K_while,		/* lnode is condtional, rnode is stuff to run */
	Node_K_for,		/* lnode is for_struct, rnode is stuff to run */
	Node_K_arrayfor,	/* lnode is for_struct, rnode is stuff to run */
	Node_K_break,		/* no subs */
	Node_K_continue,	/* no subs */
	Node_K_print,		/* lnode is exp_list, rnode is redirect */
	Node_K_print_rec,	/* lnode is NULL, rnode is redirect */
	Node_K_printf,		/* lnode is exp_list, rnode is redirect */
	Node_K_next,		/* no subs */
	Node_K_exit,		/* subnode is return value, or NULL */
	Node_K_do,		/* lnode is conditional, rnode stuff to run */
	Node_K_return,		/* lnode is return value */
	Node_K_delete,		/* lnode is array, rnode is subscript */
	Node_K_delete_loop,	/* lnode is array, rnode is subscript */
	Node_K_getline,		/* lnode is opt var, rnode is redirection */
	Node_K_function,	/* lnode is statement list, rnode is params */
	Node_K_nextfile,	/* no subs */

	/* I/O redirection for print statements */
	Node_redirect_output,	/* subnode is where to redirect */
	Node_redirect_append,	/* subnode is where to redirect */
	Node_redirect_pipe,	/* subnode is where to redirect */
	Node_redirect_pipein,	/* subnode is where to redirect */
	Node_redirect_input,	/* subnode is where to redirect */
	Node_redirect_twoway,	/* subnode is where to redirect */

	/* Variables */
	Node_var_new,		/* newly created variable, may become an array */
	Node_var,		/* scalar variable, lnode is value */
	Node_var_array,		/* array is ptr to elements, table_size num of eles */
	Node_val,		/* node is a value - type in flags */

	/* Builtins   subnode is explist to work on, builtin is func to call */
	Node_builtin,

	/*
	 * pattern: conditional ',' conditional ;  lnode of Node_line_range
	 * is the two conditionals (Node_cond_pair), other word (rnode place)
	 * is a flag indicating whether or not this range has been entered.
	 */
	Node_line_range,

	/*
	 * boolean test of membership in array
	 * lnode is string-valued, expression rnode is array name 
	 */
	Node_in_array,

	Node_func,		/* lnode is param. list, rnode is body */
	Node_func_call,		/* lnode is name, rnode is argument list */

	Node_cond_exp,		/* lnode is conditonal, rnode is if_branches */
	Node_regex,		/* a regexp, text, compiled, flags, etc */
	Node_dynregex,		/* a dynamic regexp */
	Node_hashnode,		/* an identifier in the symbol table */
	Node_ahash,		/* an array element */
	Node_array_ref,		/* array passed by ref as parameter */

	Node_BINMODE,		/* variables recognized in the grammar */
	Node_CONVFMT,
	Node_FIELDWIDTHS,
	Node_FNR,
	Node_FS,
	Node_IGNORECASE,
	Node_LINT,
	Node_NF,
	Node_NR,
	Node_OFMT,
	Node_OFS,
	Node_ORS,
	Node_RS,
	Node_TEXTDOMAIN,
	Node_final		/* sentry value, not legal */
} NODETYPE;

/*
 * NOTE - this struct is a rather kludgey -- it is packed to minimize
 * space usage, at the expense of cleanliness.  Alter at own risk.
 */
typedef struct exp_node {
	union {
		struct {
			union {
				struct exp_node *lptr;
				char *param_name;
				long ll;
			} l;
			union {
				struct exp_node *rptr;
				struct exp_node *(*pptr) P((struct exp_node *));
				Regexp *preg;
				struct for_loop_header *hd;
				struct exp_node **av;
				int r_ent;	/* range entered */
			} r;
			union {
				struct exp_node *extra;
				long xl;
				char **param_list;
			} x;
			char *name;
			short number;
			unsigned long reflags;
#				define	CASE	1
#				define	CONST	2
#				define	FS_DFLT	4
		} nodep;
		struct {
			AWKNUM fltnum;	/* this is here for optimal packing of
					 * the structure on many machines
					 */
			char *sp;
			size_t slen;
			long sref;
			int idx;
		} val;
		struct {
			struct exp_node *next;
			char *name;
			size_t length;
			struct exp_node *value;
			long ref;
		} hash;
#define	hnext	sub.hash.next
#define	hname	sub.hash.name
#define	hlength	sub.hash.length
#define	hvalue	sub.hash.value

#define	ahnext		sub.hash.next
#define	ahname_str	sub.hash.name
#define ahname_len	sub.hash.length
#define	ahvalue	sub.hash.value
#define ahname_ref	sub.hash.ref
	} sub;
	NODETYPE type;
	unsigned short flags;
#		define	MALLOC	1	/* can be free'd */
#		define	TEMP	2	/* should be free'd */
#		define	PERM	4	/* can't be free'd */
#		define	STRING	8	/* assigned as string */
#		define	STRCUR	16	/* string value is current */
#		define	NUMCUR	32	/* numeric value is current */
#		define	NUMBER	64	/* assigned as number */
#		define	MAYBE_NUM 128	/* user input: if NUMERIC then
					 * a NUMBER */
#		define	ARRAYMAXED 256	/* array is at max size */
#		define	FUNC	512	/* this parameter is really a
					 * function name; see awkgram.y */
#		define	FIELD	1024	/* this is a field */
#		define	INTLSTR	2048	/* use localized version */
} NODE;

#define vname sub.nodep.name
#define exec_count sub.nodep.reflags

#define lnode	sub.nodep.l.lptr
#define nextp	sub.nodep.l.lptr
#define rnode	sub.nodep.r.rptr
#define source_file	sub.nodep.name
#define	source_line	sub.nodep.number
#define	param_cnt	sub.nodep.number
#define param	sub.nodep.l.param_name
#define parmlist	sub.nodep.x.param_list

#define subnode	lnode
#define builtin sub.nodep.r.pptr
#define callresult sub.nodep.x.extra
#define funcbody sub.nodep.x.extra

#define re_reg	sub.nodep.r.preg
#define re_flags sub.nodep.reflags
#define re_text lnode
#define re_exp	sub.nodep.x.extra

#define forloop	rnode->sub.nodep.r.hd

#define stptr	sub.val.sp
#define stlen	sub.val.slen
#define stref	sub.val.sref
#define	stfmt	sub.val.idx

#define numbr	sub.val.fltnum

/* Node_var: */
#define var_value lnode

/* Node_var_array: */
#define var_array sub.nodep.r.av
#define array_size sub.nodep.l.ll
#define table_size sub.nodep.x.xl

/* Node_array_ref: */
#define orig_array sub.nodep.x.extra
#define prev_array rnode

#define printf_count sub.nodep.x.xl

#define condpair lnode
#define triggered sub.nodep.r.r_ent

/* a regular for loop */
typedef struct for_loop_header {
	NODE *init;
	NODE *cond;
	NODE *incr;
} FOR_LOOP_HEADER;

typedef struct iobuf {
	const char *name;       /* filename */
	int fd;                 /* file descriptor */
	struct stat sbuf;       /* stat buf */
	char *buf;              /* start data buffer */
	char *off;              /* start of current record in buffer */
	char *dataend;          /* first byte in buffer to hold new data,
				   NULL if not read yet */
	char *end;              /* end of buffer */
	size_t readsize;        /* set from fstat call */
	size_t size;            /* buffer size */
	ssize_t count;          /* amount read last time */
	size_t scanoff;         /* where we were in the buffer when we had
				   to regrow/refill */
	int flag;
#		define	IOP_IS_TTY	1
#		define	IOP_IS_INTERNAL	2
#		define	IOP_NO_FREE	4
#		define	IOP_NOFREE_OBJ	8
#               define  IOP_AT_EOF      16
#               define  IOP_CLOSED      32
} IOBUF;

typedef void (*Func_ptr) P((void));

/* structure used to dynamically maintain a linked-list of open files/pipes */
struct redirect {
	unsigned int flag;
#		define	RED_FILE	1
#		define	RED_PIPE	2
#		define	RED_READ	4
#		define	RED_WRITE	8
#		define	RED_APPEND	16
#		define	RED_NOBUF	32
#		define	RED_USED	64	/* closed temporarily to reuse fd */
#		define	RED_EOF		128
#		define	RED_TWOWAY	256
#		define	RED_PTY		512
#		define	RED_SOCKET	1024
#		define	RED_TCP		2048
	char *value;
	FILE *fp;
	FILE *ifp;	/* input fp, needed for PIPES_SIMULATED */
	IOBUF *iop;
	int pid;
	int status;
	struct redirect *prev;
	struct redirect *next;
	const char *mode;
};

/*
 * structure for our source, either a command line string or a source file.
 * the same structure is used to remember variable pre-assignments.
 */
struct src {
       enum srctype { CMDLINE = 1, SOURCEFILE,
			PRE_ASSIGN, PRE_ASSIGN_FS } stype;
       char *val;
};

/* for debugging purposes */
struct flagtab {
	int val;
	const char *name;
};

/* longjmp return codes, must be nonzero */
/* Continue means either for loop/while continue, or next input record */
#define TAG_CONTINUE 1
/* Break means either for/while break, or stop reading input */
#define TAG_BREAK 2
/* Return means return from a function call; leave value in ret_node */
#define	TAG_RETURN 3

#ifndef LONG_MAX
#define LONG_MAX ((long)(~(1L << (sizeof (long) * 8 - 1))))
#endif
#ifndef ULONG_MAX
#define ULONG_MAX (~(unsigned long)0)
#endif
#ifndef LONG_MIN
#define LONG_MIN ((long)(-LONG_MAX - 1L))
#endif
#define HUGE    LONG_MAX 

/* -------------------------- External variables -------------------------- */
/* gawk builtin variables */
extern long NF;
extern long NR;
extern long FNR;
extern int BINMODE;
extern int IGNORECASE;
extern int RS_is_null;
extern char *OFS;
extern int OFSlen;
extern char *ORS;
extern int ORSlen;
extern char *OFMT;
extern char *CONVFMT;
ATTRIBUTE_EXPORTED extern int CONVFMTidx;
extern int OFMTidx;
extern char *TEXTDOMAIN;
extern NODE *BINMODE_node, *CONVFMT_node, *FIELDWIDTHS_node, *FILENAME_node;
extern NODE *FNR_node, *FS_node, *IGNORECASE_node, *NF_node;
extern NODE *NR_node, *OFMT_node, *OFS_node, *ORS_node, *RLENGTH_node;
extern NODE *RSTART_node, *RS_node, *RT_node, *SUBSEP_node, *PROCINFO_node;
extern NODE *LINT_node, *ERRNO_node, *TEXTDOMAIN_node;
ATTRIBUTE_EXPORTED extern NODE **stack_ptr;
extern NODE *Nnull_string;
extern NODE *Null_field;
extern NODE **fields_arr;
extern int sourceline;
extern char *source;
extern NODE *expression_value;

#if __GNUC__ < 2
# if defined(WIN32_EXTENSION)
static
# else
extern
#endif
NODE *_t;	/* used as temporary in tree_eval */
#endif

extern NODE *nextfree;
extern int field0_valid;
extern int do_traditional;
extern int do_posix;
extern int do_intervals;
extern int do_intl;
extern int do_non_decimal_data;
extern int do_dump_vars;
extern int do_tidy_mem;
extern int in_begin_rule;
extern int in_end_rule;
extern int whiny_users;
#ifdef NO_LINT
#define do_lint 0
#define do_lint_old 0
#else
ATTRIBUTE_EXPORTED extern int do_lint;
extern int do_lint_old;
#endif
#ifdef MBS_SUPPORT
extern int gawk_mb_cur_max;
#endif

#if defined (HAVE_GETGROUPS) && defined(NGROUPS_MAX) && NGROUPS_MAX > 0
extern GETGROUPS_T *groupset;
extern int ngroups;
#endif

extern const char *myname;

extern char quote;
extern char *defpath;
extern char envsep;

extern const char casetable[];	/* for case-independent regexp matching */

/* ------------------------- Pseudo-functions ------------------------- */

#define is_identchar(c)		(isalnum(c) || (c) == '_')
#define isnondecimal(str)	(((str)[0]) == '0' && (ISDIGIT((str)[1]) \
					|| (str)[1] == 'x' || (str)[1] == 'X'))

#define var_uninitialized(n)	((n)->var_value == Nnull_string)

#ifdef MPROF
#define	getnode(n)	emalloc((n), NODE *, sizeof(NODE), "getnode"), (n)->flags = 0, (n)-exec_count = 0;
#define	freenode(n)	free(n)
#else	/* not MPROF */
#define	getnode(n)	if (nextfree) n = nextfree, nextfree = nextfree->nextp;\
			else n = more_nodes()
#ifndef NO_PROFILING
#define	freenode(n)	((n)->flags = 0,\
		(n)->exec_count = 0, (n)->nextp = nextfree, nextfree = (n))
#else /* not PROFILING */
#define	freenode(n)	((n)->flags = 0,\
		(n)->nextp = nextfree, nextfree = (n))
#endif	/* not PROFILING */
#endif	/* not MPROF */

#ifndef GAWKDEBUG
#define DUPNODE_MACRO 1
/*
 * Speed up the path leading to r_dupnode, as well as duplicating TEMP nodes,
 * on expense of slowing down the access to PERM nodes (by two instructions).
 * This is right since PERM nodes are realtively rare.
 *
 * The code also sets MALLOC flag for PERM nodes, which should not matter.
 */
#define DUPNODE_COMMON	(_t->flags & (TEMP|PERM)) != 0 ? \
			  (_t->flags &= ~TEMP, _t->flags |= MALLOC, _t) : \
			  r_dupnode(_t)
#if __GNUC__ >= 2
#define dupnode(n)	({NODE * _t = (n); DUPNODE_COMMON;})
#else
#define dupnode(n)	(_t = (n), DUPNODE_COMMON)
#endif
#else	/* GAWKDEBUG */
#define dupnode(n)	r_dupnode(n)
#endif	/* GAWKDEBUG */

#define get_array(t)	get_actual(t, TRUE)	/* allowed to die fatally */
#define get_param(t)	get_actual(t, FALSE)	/* not allowed */

#ifdef MEMDEBUG
#undef freenode
#define	get_lhs(p, a, r)	r_get_lhs((p), (a), (r))
#define	m_tree_eval(t, iscond)	r_tree_eval(t, iscond)
#else
#define	get_lhs(p, a, r) ((p)->type == Node_var && \
			  ! var_uninitialized(p) ? \
			  (&(p)->var_value) : \
			 r_get_lhs((p), (a), (r)))
#define TREE_EVAL_MACRO 1
#if __GNUC__ >= 2
#define	m_tree_eval(t, iscond) __extension__ \
                        ({NODE * _t = (t);                 \
			   if (_t == NULL)                 \
			       _t = Nnull_string;          \
			   else {                          \
			       switch(_t->type) {          \
			       case Node_val:              \
				   if (_t->flags&INTLSTR)  \
					_t = r_force_string(_t); \
				   break;                  \
			       case Node_var:              \
				   if (! var_uninitialized(_t)) { \
				       _t = _t->var_value;     		   \
				       break;                  		   \
				   }					   \
				   /*FALLTHROUGH*/			   \
			       default:                    \
				   _t = r_tree_eval(_t, iscond);\
				   break;                  \
			       }                           \
			   }                               \
			   _t;})
#else
#define	m_tree_eval(t, iscond)	(_t = (t), _t == NULL ? Nnull_string : \
			(_t->type == Node_param_list ? \
			  r_tree_eval(_t, iscond) : \
			((_t->type == Node_val && (_t->flags&INTLSTR)) ? \
				r_force_string(_t) : \
			(_t->type == Node_val ? _t : \
			(_t->type == Node_var && \
			 ! var_uninitialized(_t) ? _t->var_value : \
			 r_tree_eval(_t, iscond))))))
#endif /* __GNUC__ */
#endif /* not MEMDEBUG */
#define tree_eval(t)	m_tree_eval(t, FALSE)

#define	make_number(x)	mk_number((x), (unsigned int)(MALLOC|NUMCUR|NUMBER))
#define	tmp_number(x)	mk_number((x), (unsigned int)(MALLOC|TEMP|NUMCUR|NUMBER))

#define	free_temp(n)	do { NODE *_n = (n); if (_n->flags&TEMP) unref(_n);} \
				while (FALSE)

#define	make_string(s, l)	make_str_node((s), (size_t) (l), 0)
#define		SCAN			1
#define		ALREADY_MALLOCED	2

#define	cant_happen()	r_fatal("internal error line %d, file: %s", \
				__LINE__, __FILE__)

#ifdef HAVE_STRINGIZE
#define	emalloc(var,ty,x,str)	(void)((var=(ty)malloc((MALLOC_ARG_T)(x))) ||\
				 (fatal(_("%s: %s: can't allocate %ld bytes of memory (%s)"),\
					(str), #var, (long) (x), strerror(errno)),0))
#define	erealloc(var,ty,x,str)	(void)((var=(ty)realloc((char *)var,\
						  (MALLOC_ARG_T)(x))) ||\
				 (fatal(_("%s: %s: can't allocate %ld bytes of memory (%s)"),\
					(str), #var, (long) (x), strerror(errno)),0))
#else /* HAVE_STRINGIZE */
#define	emalloc(var,ty,x,str)	(void)((var=(ty)malloc((MALLOC_ARG_T)(x))) ||\
				 (fatal(_("%s: %s: can't allocate %ld bytes of memory (%s)"),\
					(str), "var", (long) (x), strerror(errno)),0))
#define	erealloc(var,ty,x,str)	(void)((var=(ty)realloc((char *)var,\
						  (MALLOC_ARG_T)(x))) ||\
				 (fatal(_("%s: %s: can't allocate %ld bytes of memory (%s)"),\
					(str), "var", (long) (x), strerror(errno)),0))
#endif /* HAVE_STRINGIZE */

#ifdef GAWKDEBUG
#define	force_number	r_force_number
#define	force_string	r_force_string
#else /* not GAWKDEBUG */
#ifdef lint
extern AWKNUM force_number();
#endif
#if __GNUC__ >= 2
#define	force_number(n)	__extension__ ({NODE *_tn = (n);\
			(_tn->flags & NUMCUR) ?_tn->numbr : r_force_number(_tn);})
#define	force_string(s)	__extension__ ({NODE *_ts = (s);\
			  ((_ts->flags & INTLSTR) ? \
				r_force_string(_ts) : \
			  ((_ts->flags & STRCUR) && \
			   (_ts->stfmt == -1 || _ts->stfmt == CONVFMTidx)) ?\
			  _ts : r_force_string(_ts));})
#else
#ifdef MSDOS
extern double _msc51bug;
#define	force_number(n)	(_msc51bug=(_t = (n),\
			  (_t->flags & NUMCUR) ? _t->numbr : r_force_number(_t)))
#else /* not MSDOS */
#define	force_number(n)	(_t = (n),\
			 (_t->flags & NUMCUR) ? _t->numbr : r_force_number(_t))
#endif /* not MSDOS */
#define	force_string(s)	(_t = (s),(_t->flags & INTLSTR) ? \
					r_force_string(_t) :\
				((_t->flags & STRCUR) && \
				 (_t->stfmt == -1 || \
				 _t->stfmt == CONVFMTidx))? \
			 _t : r_force_string(_t))

#endif /* not __GNUC__ */
#endif /* not GAWKDEBUG */

#define	STREQ(a,b)	(*(a) == *(b) && strcmp((a), (b)) == 0)
#define	STREQN(a,b,n)	((n) && *(a)== *(b) && \
			 strncmp((a), (b), (size_t) (n)) == 0)

#define fatal		set_loc(__FILE__, __LINE__), r_fatal

/* ------------- Function prototypes or defs (as appropriate) ------------- */

/* array.c */
extern NODE *get_actual P((NODE *symbol, int canfatal));
extern char *array_vname P((const NODE *symbol));
extern void array_init P((void));
extern NODE *concat_exp P((NODE *tree));
extern void assoc_clear P((NODE *symbol));
extern NODE *in_array P((NODE *symbol, NODE *subs));
extern NODE **assoc_lookup P((NODE *symbol, NODE *subs, int reference));
extern void do_delete P((NODE *symbol, NODE *tree));
extern void do_delete_loop P((NODE *symbol, NODE *tree));
extern NODE *assoc_dump P((NODE *symbol));
extern NODE *do_adump P((NODE *tree));
extern NODE *do_asort P((NODE *tree));
extern NODE *do_asorti P((NODE *tree));
extern unsigned long (*hash)P((const char *s, size_t len, unsigned long hsize));
/* awkgram.c */
extern char *tokexpand P((void));
extern NODE *node P((NODE *left, NODETYPE op, NODE *right));
extern NODE *install P((char *name, NODE *value));
extern NODE *lookup P((const char *name));
extern NODE *variable P((char *name, int can_free, NODETYPE type));
extern int yyparse P((void));
extern void dump_funcs P((void));
extern void dump_vars P((const char *fname));
extern void release_all_vars P((void));
extern const char *getfname P((NODE *(*)(NODE *)));
extern NODE *stopme P((NODE *tree));
extern void shadow_funcs P((void));
/* builtin.c */
extern double double_to_int P((double d));
extern NODE *do_exp P((NODE *tree));
extern NODE *do_fflush P((NODE *tree));
extern NODE *do_index P((NODE *tree));
extern NODE *do_int P((NODE *tree));
extern NODE *do_length P((NODE *tree));
extern NODE *do_log P((NODE *tree));
extern NODE *do_mktime P((NODE *tree));
extern NODE *do_sprintf P((NODE *tree));
extern void do_printf P((NODE *tree));
extern void print_simple P((NODE *tree, FILE *fp));
extern NODE *do_sqrt P((NODE *tree));
extern NODE *do_substr P((NODE *tree));
extern NODE *do_strftime P((NODE *tree));
extern NODE *do_systime P((NODE *tree));
extern NODE *do_system P((NODE *tree));
extern void do_print P((NODE *tree));
extern void do_print_rec P((NODE *tree));
extern NODE *do_tolower P((NODE *tree));
extern NODE *do_toupper P((NODE *tree));
extern NODE *do_atan2 P((NODE *tree));
extern NODE *do_sin P((NODE *tree));
extern NODE *do_cos P((NODE *tree));
extern NODE *do_rand P((NODE *tree));
extern NODE *do_srand P((NODE *tree));
extern NODE *do_match P((NODE *tree));
extern NODE *do_gsub P((NODE *tree));
extern NODE *do_sub P((NODE *tree));
extern NODE *do_gensub P((NODE *tree));
extern NODE *format_tree P((const char *, size_t, NODE *, long));
extern NODE *do_lshift P((NODE *tree));
extern NODE *do_rshift P((NODE *tree));
extern NODE *do_and P((NODE *tree));
extern NODE *do_or P((NODE *tree));
extern NODE *do_xor P((NODE *tree));
extern NODE *do_compl P((NODE *tree));
extern NODE *do_strtonum P((NODE *tree));
extern AWKNUM nondec2awknum P((char *str, size_t len));
extern NODE *do_dcgettext P((NODE *tree));
extern NODE *do_dcngettext P((NODE *tree));
extern NODE *do_bindtextdomain P((NODE *tree));
#ifdef MBS_SUPPORT
extern int strncasecmpmbs P((const char *, mbstate_t, const char *,
			     mbstate_t, size_t));
#endif
/* eval.c */
extern int interpret P((NODE *volatile tree));
extern NODE *r_tree_eval P((NODE *tree, int iscond));
extern int cmp_nodes P((NODE *t1, NODE *t2));
extern NODE **r_get_lhs P((NODE *ptr, Func_ptr *assign, int reference));
extern void set_IGNORECASE P((void));
extern void set_OFS P((void));
extern void set_ORS P((void));
extern void set_OFMT P((void));
extern void set_CONVFMT P((void));
extern void set_BINMODE P((void));
extern void set_LINT P((void));
extern void set_TEXTDOMAIN P((void));
extern void update_ERRNO P((void));
extern const char *redflags2str P((int));
extern const char *flags2str P((int));
extern const char *genflags2str P((int flagval, const struct flagtab *tab));
extern const char *nodetype2str P((NODETYPE type));
extern NODE *assign_val P((NODE **lhs_p, NODE *rhs));
#ifdef PROFILING
extern void dump_fcall_stack P((FILE *fp));
#endif
/* ext.c */
NODE *do_ext P((NODE *));
#ifdef DYNAMIC
void make_builtin P((char *, NODE *(*)(NODE *), int));
NODE *get_argument P((NODE *, int));
void set_value P((NODE *));
#endif
/* field.c */
extern void init_fields P((void));
extern void set_record P((const char *buf, int cnt));
extern void reset_record P((void));
extern void set_NF P((void));
extern NODE **get_field P((long num, Func_ptr *assign));
extern NODE *do_split P((NODE *tree));
extern void set_FS P((void));
extern void set_RS P((void));
extern void set_FIELDWIDTHS P((void));
extern int using_fieldwidths P((void));
/* gawkmisc.c */
extern char *gawk_name P((const char *filespec));
extern void os_arg_fixup P((int *argcp, char ***argvp));
extern int os_devopen P((const char *name, int flag));
extern void os_close_on_exec P((int fd, const char *name, const char *what,
				const char *dir));
extern int os_isdir P((int fd));
extern int os_is_setuid P((void));
extern int os_setbinmode P((int fd, int mode));
extern void os_restore_mode P((int fd));
extern size_t optimal_bufsize P((int fd, struct stat *sbuf));
extern int ispath P((const char *file));
extern int isdirpunct P((int c));
#if defined(_MSC_VER) && !defined(_WIN32)
extern char *memcpy_ulong P((char *dest, const char *src, unsigned long l));
extern void *memset_ulong P((void *dest, int val, unsigned long l));
#define memcpy memcpy_ulong
#define memset memset_ulong
#endif
/* io.c */
extern void set_FNR P((void));
extern void set_NR P((void));
extern void do_input P((void));
extern struct redirect *redirect P((NODE *tree, int *errflg));
extern NODE *do_close P((NODE *tree));
extern int flush_io P((void));
extern int close_io P((void));
extern int devopen P((const char *name, const char *mode));
extern int pathopen P((const char *file));
extern NODE *do_getline P((NODE *tree));
extern void do_nextfile P((void)) ATTRIBUTE_NORETURN;
extern struct redirect *getredirect P((const char *str, int len));
/* main.c */
extern int main P((int argc, char **argv));
extern NODE *load_environ P((void));
extern NODE *load_procinfo P((void));
extern int arg_assign P((char *arg, int initing));
/* msg.c */
extern void err P((const char *s, const char *emsg, va_list argp)) ATTRIBUTE_PRINTF(2, 0);
#if _MSC_VER == 510
extern void msg P((va_list va_alist, ...));
extern void error P((va_list va_alist, ...));
extern void warning P((va_list va_alist, ...));
extern void set_loc P((const char *file, int line));
extern void r_fatal P((va_list va_alist, ...));
extern void (*lintfunc) P((va_list va_alist, ...));
#else
#if defined(HAVE_STDARG_H) && defined(__STDC__) && __STDC__
extern void msg (const char *mesg, ...) ATTRIBUTE_PRINTF_1;
extern void error (const char *mesg, ...) ATTRIBUTE_PRINTF_1;
extern void warning (const char *mesg, ...) ATTRIBUTE_PRINTF_1;
extern void set_loc (const char *file, int line);
extern void r_fatal (const char *mesg, ...) ATTRIBUTE_PRINTF_1 ATTRIBUTE_NORETURN;
ATTRIBUTE_EXPORTED 
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 2)
extern void (*lintfunc) (const char *mesg, ...) ATTRIBUTE_PRINTF_1;
#else
extern void (*lintfunc) (const char *mesg, ...);
#endif
#else
extern void msg ();
extern void error ();
extern void warning ();
extern void set_loc ();
extern void r_fatal ();
extern void (*lintfunc) ();
#endif
#endif
/* profile.c */
extern void init_profiling P((int *flag, const char *def_file));
extern void init_profiling_signals P((void));
extern void set_prof_file P((const char *filename));
extern void dump_prog P((NODE *begin, NODE *prog, NODE *end));
extern void pp_func P((const char *name, size_t namelen, NODE *f));
extern void pp_string_fp P((FILE *fp, const char *str, size_t namelen,
			int delim, int breaklines));
/* node.c */
extern AWKNUM r_force_number P((NODE *n));
extern NODE *format_val P((const char *format, int index, NODE *s));
extern NODE *r_force_string P((NODE *s));
extern NODE *r_dupnode P((NODE *n));
extern NODE *copynode P((NODE *n));
extern NODE *mk_number P((AWKNUM x, unsigned int flags));
extern NODE *make_str_node P((char *s, unsigned long len, int scan ));
extern NODE *tmp_string P((char *s, size_t len ));
extern NODE *more_nodes P((void));
#ifdef MEMDEBUG
extern void freenode P((NODE *it));
#endif
extern void unref P((NODE *tmp));
extern int parse_escape P((const char **string_ptr));
/* re.c */
extern Regexp *make_regexp P((const char *s, size_t len, int ignorecase));
extern int research P((Regexp *rp, const char *str, int start,
		       size_t len, int need_start));
extern void refree P((Regexp *rp));
extern void reg_error P((const char *s));
extern Regexp *re_update P((NODE *t));
extern void resyntax P((int syntax));
extern void resetup P((void));
extern int reisstring P((const char *text, size_t len, Regexp *re, const char *buf));
extern int remaybelong P((const char *text, size_t len));

/* strncasecmp.c */
#ifndef BROKEN_STRNCASECMP
extern int strcasecmp P((const char *s1, const char *s2));
extern int strncasecmp P((const char *s1, const char *s2, register size_t n));
#endif

#if defined(atarist)
#if defined(PIPES_SIMULATED)
/* unsupported/atari/tmpnam.c */
extern char *tmpnam P((char *buf));
extern char *tempnam P((const char *path, const char *base));
#else
#include <wait.h>
#endif
#include <fcntl.h>
#define INVALID_HANDLE  (__SMALLEST_VALID_HANDLE - 1)
#else
#define INVALID_HANDLE (-1)
#endif /* atarist */

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((((unsigned) (stat_val)) >> 8) & 0xFF)
#endif

#ifndef STATIC
#define STATIC static
#endif
