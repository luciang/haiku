/*
 * node.c -- routines for node management
 */

/* 
 * Copyright (C) 1986, 1988, 1989, 1991-2001, 2003 the Free Software Foundation, Inc.
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

#include "awk.h"

/* r_force_number --- force a value to be numeric */

AWKNUM
r_force_number(register NODE *n)
{
	register char *cp;
	register char *cpend;
	char save;
	char *ptr;
	unsigned int newflags;
	extern double strtod();

#ifdef GAWKDEBUG
	if (n == NULL)
		cant_happen();
	if (n->type != Node_val)
		cant_happen();
	if (n->flags == 0)
		cant_happen();
	if (n->flags & NUMCUR)
		return n->numbr;
#endif

	/* all the conditionals are an attempt to avoid the expensive strtod */

	n->numbr = 0.0;
	n->flags |= NUMCUR;

	if (n->stlen == 0) {
		if (0 && do_lint)
			lintwarn(_("can't convert string to float"));
		return 0.0;
	}

	cp = n->stptr;
	if (ISALPHA(*cp)) {
		if (0 && do_lint)
			lintwarn(_("can't convert string to float"));
		return 0.0;
	}

	cpend = cp + n->stlen;
	while (cp < cpend && ISSPACE(*cp))
		cp++;
	if (cp == cpend || ISALPHA(*cp)) {
		if (0 && do_lint)
			lintwarn(_("can't convert string to float"));
		return 0.0;
	}

	if (n->flags & MAYBE_NUM) {
		newflags = NUMBER;
		n->flags &= ~MAYBE_NUM;
	} else
		newflags = 0;
	if (cpend - cp == 1) {
		if (ISDIGIT(*cp)) {
			n->numbr = (AWKNUM)(*cp - '0');
			n->flags |= newflags;
		} else if (0 && do_lint)
			lintwarn(_("can't convert string to float"));
		return n->numbr;
	}

	if (do_non_decimal_data) {
		errno = 0;
		if (! do_traditional && isnondecimal(cp)) {
			n->numbr = nondec2awknum(cp, cpend - cp);
			goto finish;
		}
	}

	errno = 0;
	save = *cpend;
	*cpend = '\0';
	n->numbr = (AWKNUM) strtod((const char *) cp, &ptr);

	/* POSIX says trailing space is OK for NUMBER */
	while (ISSPACE(*ptr))
		ptr++;
	*cpend = save;
finish:
	/* the >= should be ==, but for SunOS 3.5 strtod() */
	if (errno == 0 && ptr >= cpend) {
		n->flags |= newflags;
	} else {
		if (0 && do_lint && ptr < cpend)
			lintwarn(_("can't convert string to float"));
		errno = 0;
	}

	return n->numbr;
}

/*
 * the following lookup table is used as an optimization in force_string
 * (more complicated) variations on this theme didn't seem to pay off, but 
 * systematic testing might be in order at some point
 */
static const char *const values[] = {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
};
#define	NVAL	(sizeof(values)/sizeof(values[0]))

/* format_val --- format a numeric value based on format */

NODE *
format_val(const char *format, int index, register NODE *s)
{
	char buf[BUFSIZ];
	register char *sp = buf;
	double val;
	char *orig, *trans, save;

	if (! do_traditional && (s->flags & INTLSTR) != 0) {
		save = s->stptr[s->stlen];
		s->stptr[s->stlen] = '\0';

		orig = s->stptr;
		trans = dgettext(TEXTDOMAIN, orig);

		s->stptr[s->stlen] = save;
		return tmp_string(trans, strlen(trans));
	}

	/* not an integral value, or out of range */
	if ((val = double_to_int(s->numbr)) != s->numbr
	    || val < LONG_MIN || val > LONG_MAX) {
		/*
		 * Once upon a time, if GFMT_WORKAROUND wasn't defined,
		 * we just blindly did this:
		 *	sprintf(sp, format, s->numbr);
		 *	s->stlen = strlen(sp);
		 *	s->stfmt = (char) index;
		 * but that's no good if, e.g., OFMT is %s. So we punt,
		 * and just always format the value ourselves.
		 */

		NODE *dummy, *r;
		unsigned short oflags;
		extern NODE **fmt_list;          /* declared in eval.c */

		/* create dummy node for a sole use of format_tree */
		getnode(dummy);
		dummy->type = Node_expression_list;
		dummy->lnode = s;
		dummy->rnode = NULL;
		oflags = s->flags;
		s->flags |= PERM; /* prevent from freeing by format_tree() */
		r = format_tree(format, fmt_list[index]->stlen, dummy, 2);
		s->flags = oflags;
		s->stfmt = (char) index;
		s->stlen = r->stlen;
		s->stptr = r->stptr;
		freenode(r);		/* Do not free_temp(r)!  We want */
		freenode(dummy);	/* to keep s->stptr == r->stpr.  */

		goto no_malloc;
	} else {
		/* integral value */
	        /* force conversion to long only once */
		register long num = (long) val;
		if (num < NVAL && num >= 0) {
			sp = (char *) values[num];
			s->stlen = 1;
		} else {
			(void) sprintf(sp, "%ld", num);
			s->stlen = strlen(sp);
		}
		s->stfmt = -1;
	}
	emalloc(s->stptr, char *, s->stlen + 2, "format_val");
	memcpy(s->stptr, sp, s->stlen+1);
no_malloc:
	s->stref = 1;
	s->flags |= STRCUR;
	return s;
}

/* r_force_string --- force a value to be a string */

NODE *
r_force_string(register NODE *s)
{
	NODE *ret;
#ifdef GAWKDEBUG
	if (s == NULL)
		cant_happen();
	if (s->type != Node_val)
		cant_happen();
	if (s->stref <= 0)
		cant_happen();
	if ((s->flags & STRCUR) != 0
	    && (s->stfmt == -1 || s->stfmt == CONVFMTidx))
		return s;
#endif

	ret = format_val(CONVFMT, CONVFMTidx, s);
	return ret;
}

/*
 * dupnode:
 * Duplicate a node.  (For strings, "duplicate" means crank up the
 * reference count.)
 */

NODE *
r_dupnode(NODE *n)
{
	register NODE *r;

#ifndef DUPNODE_MACRO
	if ((n->flags & TEMP) != 0) {
		n->flags &= ~TEMP;
		n->flags |= MALLOC;
		return n;
	}
	if ((n->flags & PERM) != 0)
		return n;
#endif
	if ((n->flags & (MALLOC|STRCUR)) == (MALLOC|STRCUR)) {
		if (n->stref < LONG_MAX)
			n->stref++;
		else
			n->flags |= PERM;
		return n;
	} else if ((n->flags & MALLOC) != 0 && n->type == Node_ahash) {
		if (n->ahname_ref < LONG_MAX)
			n->ahname_ref++;
		else
			n->flags |= PERM;
		return n;
	}
	getnode(r);
	*r = *n;
	r->flags &= ~(PERM|TEMP|FIELD);
	r->flags |= MALLOC;
	if (n->type == Node_val && (n->flags & STRCUR) != 0) {
		r->stref = 1;
		emalloc(r->stptr, char *, r->stlen + 2, "dupnode");
		memcpy(r->stptr, n->stptr, r->stlen);
		r->stptr[r->stlen] = '\0';
	} else if (n->type == Node_ahash && (n->flags & MALLOC) != 0) {
		r->ahname_ref = 1;
		emalloc(r->ahname_str, char *, r->ahname_len + 2, "dupnode");
		memcpy(r->ahname_str, n->ahname_str, r->ahname_len);
		r->ahname_str[r->ahname_len] = '\0';
	}
	return r;
}

/* copy_node --- force a brand new copy of a node to be allocated */

NODE *
copynode(NODE *old)
{
	NODE *new;
	int saveflags;

	assert(old != NULL);
	saveflags = old->flags;
	old->flags &= ~(MALLOC|PERM);
	new = dupnode(old);
	old->flags = saveflags;
	return new;
}

/* mk_number --- allocate a node with defined number */

NODE *
mk_number(AWKNUM x, unsigned int flags)
{
	register NODE *r;

	getnode(r);
	r->type = Node_val;
	r->numbr = x;
	r->flags = flags;
#ifdef GAWKDEBUG
	r->stref = 1;
	r->stptr = NULL;
	r->stlen = 0;
#endif
	return r;
}

/* make_str_node --- make a string node */

NODE *
make_str_node(char *s, unsigned long len, int flags)
{
	register NODE *r;

	getnode(r);
	r->type = Node_val;
	r->flags = (STRING|STRCUR|MALLOC);
	if (flags & ALREADY_MALLOCED)
		r->stptr = s;
	else {
		emalloc(r->stptr, char *, len + 2, s);
		memcpy(r->stptr, s, len);
	}
	r->stptr[len] = '\0';
	       
	if ((flags & SCAN) != 0) {	/* scan for escape sequences */
		const char *pf;
		register char *ptm;
		register int c;
		register const char *end;

		end = &(r->stptr[len]);
		for (pf = ptm = r->stptr; pf < end;) {
			c = *pf++;
			if (c == '\\') {
				c = parse_escape(&pf);
				if (c < 0) {
					if (do_lint)
						lintwarn(_("backslash at end of string"));
					c = '\\';
				}
				*ptm++ = c;
			} else
				*ptm++ = c;
		}
		len = ptm - r->stptr;
		erealloc(r->stptr, char *, len + 1, "make_str_node");
		r->stptr[len] = '\0';
		r->flags |= PERM;
	}
	r->stlen = len;
	r->stref = 1;
	r->stfmt = -1;

	return r;
}

/* tmp_string --- allocate a temporary string */

NODE *
tmp_string(char *s, size_t len)
{
	register NODE *r;

	r = make_string(s, len);
	r->flags |= TEMP;
	return r;
}

/* more_nodes --- allocate more nodes */

#define NODECHUNK	100

NODE *nextfree = NULL;

NODE *
more_nodes()
{
	register NODE *np;

	/* get more nodes and initialize list */
	emalloc(nextfree, NODE *, NODECHUNK * sizeof(NODE), "more_nodes");
	for (np = nextfree; np <= &nextfree[NODECHUNK - 1]; np++) {
		np->flags = 0;
#ifndef NO_PROFILING
		np->exec_count = 0;
#endif
		np->nextp = np + 1;
	}
	--np;
	np->nextp = NULL;
	np = nextfree;
	nextfree = nextfree->nextp;
	return np;
}

#ifdef MEMDEBUG
#undef freenode
/* freenode --- release a node back to the pool */

void
freenode(NODE *it)
{
#ifdef MPROF
	it->stref = 0;
	free((char *) it);
#else	/* not MPROF */
#ifndef NO_PROFILING
	it->exec_count = 0;
#endif
	/* add it to head of freelist */
	it->nextp = nextfree;
	nextfree = it;
#endif	/* not MPROF */
}
#endif	/* GAWKDEBUG */

/* unref --- remove reference to a particular node */

void
unref(register NODE *tmp)
{
	if (tmp == NULL)
		return;
	if ((tmp->flags & PERM) != 0)
		return;
	tmp->flags &= ~TEMP;
	if ((tmp->flags & MALLOC) != 0) {
		if (tmp->type == Node_ahash) {
			if (tmp->ahname_ref > 1) {
				tmp->ahname_ref--;
				return;
			}
			free(tmp->ahname_str);
		} else if ((tmp->flags & STRCUR) != 0) {
			if (tmp->stref > 1) {
				tmp->stref--;
				return;
			}
			free(tmp->stptr);
		}
		freenode(tmp);
		return;
	}
	if ((tmp->flags & FIELD) != 0) {
		freenode(tmp);
		return;
	}
}

/*
 * parse_escape:
 *
 * Parse a C escape sequence.  STRING_PTR points to a variable containing a
 * pointer to the string to parse.  That pointer is updated past the
 * characters we use.  The value of the escape sequence is returned. 
 *
 * A negative value means the sequence \ newline was seen, which is supposed to
 * be equivalent to nothing at all. 
 *
 * If \ is followed by a null character, we return a negative value and leave
 * the string pointer pointing at the null character. 
 *
 * If \ is followed by 000, we return 0 and leave the string pointer after the
 * zeros.  A value of 0 does not mean end of string.  
 *
 * Posix doesn't allow \x.
 */

int
parse_escape(const char **string_ptr)
{
	register int c = *(*string_ptr)++;
	register int i;
	register int count;

	switch (c) {
	case 'a':
		return BELL;
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'n':
		return '\n';
	case 'r':
		return '\r';
	case 't':
		return '\t';
	case 'v':
		return '\v';
	case '\n':
		return -2;
	case 0:
		(*string_ptr)--;
		return -1;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		i = c - '0';
		count = 0;
		while (++count < 3) {
			if ((c = *(*string_ptr)++) >= '0' && c <= '7') {
				i *= 8;
				i += c - '0';
			} else {
				(*string_ptr)--;
				break;
			}
		}
		return i;
	case 'x':
		if (do_lint) {
			static int didwarn = FALSE;

			if (! didwarn) {
				didwarn = TRUE;
				lintwarn(_("POSIX does not allow `\\x' escapes"));
			}
		}
		if (do_posix)
			return ('x');
		if (! ISXDIGIT((*string_ptr)[0])) {
			warning(_("no hex digits in `\\x' escape sequence"));
			return ('x');
		}
		i = 0;
		for (;;) {
			/* do outside test to avoid multiple side effects */
			c = *(*string_ptr)++;
			if (ISXDIGIT(c)) {
				i *= 16;
				if (ISDIGIT(c))
					i += c - '0';
				else if (ISUPPER(c))
					i += c - 'A' + 10;
				else
					i += c - 'a' + 10;
			} else {
				(*string_ptr)--;
				break;
			}
		}
		return i;
	case '\\':
	case '"':
		return c;
	default:
	{
		static short warned[256];
		unsigned char uc = (unsigned char) c;

		/* N.B.: use unsigned char here to avoid Latin-1 problems */

		if (! warned[uc]) {
			warned[uc] = TRUE;

			warning(_("escape sequence `\\%c' treated as plain `%c'"), uc, uc);
		}
	}
		return c;
	}
}
