/*
 * field.c - routines for dealing with fields and record parsing
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

#include "awk.h"

typedef void (* Setfunc) P((long, char *, long, NODE *));

static long (*parse_field) P((long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *));
static void rebuild_record P((void));
static long re_parse_field P((long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *));
static long def_parse_field P((long, char **, int, NODE *,
			      Regexp *, Setfunc, NODE *));
static long posix_def_parse_field P((long, char **, int, NODE *,
			      Regexp *, Setfunc, NODE *));
static long null_parse_field P((long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *));
static long sc_parse_field P((long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *));
static long fw_parse_field P((long, char **, int, NODE *,
			     Regexp *, Setfunc, NODE *));
static void set_element P((long num, char * str, long len, NODE *arr));
static void grow_fields_arr P((long num));
static void set_field P((long num, char *str, long len, NODE *dummy));
static void update_PROCINFO P((char *subscript, char *str));


static char *parse_extent;	/* marks where to restart parse of record */
static long parse_high_water = 0; /* field number that we have parsed so far */
static long nf_high_water = 0;	/* size of fields_arr */
static int resave_fs;
static NODE *save_FS;		/* save current value of FS when line is read,
				 * to be used in deferred parsing
				 */
static int *FIELDWIDTHS = NULL;

NODE **fields_arr;		/* array of pointers to the field nodes */
int field0_valid;		/* $(>0) has not been changed yet */
int default_FS;			/* TRUE when FS == " " */
Regexp *FS_re_yes_case = NULL;
Regexp *FS_re_no_case = NULL;
Regexp *FS_regexp = NULL;
NODE *Null_field = NULL;

/* using_FIELDWIDTHS --- static function, macro to avoid overhead */
#define using_FIELDWIDTHS()	(parse_field == fw_parse_field)

/* init_fields --- set up the fields array to start with */

void
init_fields()
{
	emalloc(fields_arr, NODE **, sizeof(NODE *), "init_fields");
	fields_arr[0] = Nnull_string;
	parse_extent = fields_arr[0]->stptr;
	save_FS = dupnode(FS_node->var_value);
	getnode(Null_field);
	*Null_field = *Nnull_string;
	Null_field->flags |= FIELD;
	Null_field->flags &= ~(NUMCUR|NUMBER|MAYBE_NUM|PERM);
	field0_valid = TRUE;
}

/* grow_fields --- acquire new fields as needed */

static void
grow_fields_arr(long num)
{
	register int t;
	register NODE *n;

	erealloc(fields_arr, NODE **, (num + 1) * sizeof(NODE *), "grow_fields_arr");
	for (t = nf_high_water + 1; t <= num; t++) {
		getnode(n);
		*n = *Null_field;
		fields_arr[t] = n;
	}
	nf_high_water = num;
}

/* set_field --- set the value of a particular field */

/*ARGSUSED*/
static void
set_field(long num,
	char *str,
	long len,
	NODE *dummy ATTRIBUTE_UNUSED)	/* just to make interface same as set_element */
{
	register NODE *n;

	if (num > nf_high_water)
		grow_fields_arr(num);
	n = fields_arr[num];
	n->stptr = str;
	n->stlen = len;
	n->flags = (STRCUR|STRING|MAYBE_NUM|FIELD);
}

/* rebuild_record --- Someone assigned a value to $(something).
			Fix up $0 to be right */

static void
rebuild_record()
{
	/*
	 * use explicit unsigned longs for lengths, in case
	 * a size_t isn't big enough.
	 */
	register unsigned long tlen;
	register unsigned long ofslen;
	register NODE *tmp;
	NODE *ofs;
	char *ops;
	register char *cops;
	long i;

	assert(NF != -1);

	tlen = 0;
	ofs = force_string(OFS_node->var_value);
	ofslen = ofs->stlen;
	for (i = NF; i > 0; i--) {
		tmp = fields_arr[i];
		tmp = force_string(tmp);
		tlen += tmp->stlen;
	}
	tlen += (NF - 1) * ofslen;
	if ((long) tlen < 0)
		tlen = 0;
	emalloc(ops, char *, tlen + 2, "rebuild_record");
	cops = ops;
	ops[0] = '\0';
	for (i = 1;  i <= NF; i++) {
		tmp = fields_arr[i];
		/* copy field */
		if (tmp->stlen == 1)
			*cops++ = tmp->stptr[0];
		else if (tmp->stlen != 0) {
			memcpy(cops, tmp->stptr, tmp->stlen);
			cops += tmp->stlen;
		}
		/* copy OFS */
		if (i != NF) {
			if (ofslen == 1)
				*cops++ = ofs->stptr[0];
			else if (ofslen != 0) {
				memcpy(cops, ofs->stptr, ofslen);
				cops += ofslen;
			}
		}
	}
	tmp = make_str_node(ops, tlen, ALREADY_MALLOCED);

	/*
	 * Since we are about to unref fields_arr[0], we want to find
	 * any fields that still point into it, and have them point
	 * into the new field zero.  This has to be done intelligently,
	 * so that unrefing a field doesn't try to unref into the old $0.
	 */
	for (cops = ops, i = 1; i <= NF; i++) {
		if (fields_arr[i]->stlen > 0) {
			NODE *n;
			getnode(n);

			if ((fields_arr[i]->flags & FIELD) == 0) {
				*n = *Null_field;
				n->stlen = fields_arr[i]->stlen;
				if ((fields_arr[i]->flags & (NUMCUR|NUMBER)) != 0) {
					n->flags |= (fields_arr[i]->flags & (NUMCUR|NUMBER));
					n->numbr = fields_arr[i]->numbr;
				}
			} else {
				*n = *(fields_arr[i]);
				n->flags &= ~(MALLOC|TEMP|PERM|STRING);
			}

			n->stptr = cops;
			unref(fields_arr[i]);
			fields_arr[i] = n;
		}
		cops += fields_arr[i]->stlen + ofslen;
	}

	unref(fields_arr[0]);

	fields_arr[0] = tmp;
	field0_valid = TRUE;
}

/*
 * set_record:
 * setup $0, but defer parsing rest of line until reference is made to $(>0)
 * or to NF.  At that point, parse only as much as necessary.
 *
 * Manage a private buffer for the contents of $0.  Doing so keeps us safe
 * if `getline var' decides to rearrange the contents of the IOBUF that
 * $0 might have been pointing into.  The cost is the copying of the buffer;
 * but better correct than fast.
 */
void
set_record(const char *buf, int cnt)
{
	NODE *n;
	static char *databuf;
	static unsigned long databuf_size;
#define INITIAL_SIZE	512
#define MAX_SIZE	((unsigned long) ~0)	/* maximally portable ... */

	reset_record();

	/* buffer management: */
	if (databuf_size == 0) {	/* first time */
		emalloc(databuf, char *, INITIAL_SIZE, "set_record");
		databuf_size = INITIAL_SIZE;
		memset(databuf, '\0', INITIAL_SIZE);

	}
	/*
	 * Make sure there's enough room. Since we sometimes need
	 * to place a sentinel at the end, we make sure
	 * databuf_size is > cnt after allocation.
	 */
	if (cnt >= databuf_size) {
		while (cnt >= databuf_size && databuf_size <= MAX_SIZE)
			databuf_size *= 2;
		erealloc(databuf, char *, databuf_size, "set_record");
		memset(databuf, '\0', databuf_size);
	}
	/* copy the data */
	memcpy(databuf, buf, cnt);

	/* manage field 0: */
	unref(fields_arr[0]);
	getnode(n);
	n->stptr = databuf;
	n->stlen = cnt;
	n->stref = 1;
	n->type = Node_val;
	n->stfmt = -1;
	n->flags = (STRING|STRCUR|MAYBE_NUM|FIELD);
	fields_arr[0] = n;

#undef INITIAL_SIZE
#undef MAX_SIZE
}

/* reset_record --- start over again with current $0 */

void
reset_record()
{
	register int i;
	NODE *n;

	(void) force_string(fields_arr[0]);

	NF = -1;
	for (i = 1; i <= parse_high_water; i++) {
		unref(fields_arr[i]);
		getnode(n);
		*n = *Null_field;
		fields_arr[i] = n;
	}

	parse_high_water = 0;
	/*
	 * $0 = $0 should resplit using the current value of FS.
	 */
	if (resave_fs) {
		resave_fs = FALSE;
		unref(save_FS);
		save_FS = dupnode(FS_node->var_value);
	}

	field0_valid = TRUE;
}

/* set_NF --- handle what happens to $0 and fields when NF is changed */

void
set_NF()
{
	register int i;
	NODE *n;

	assert(NF != -1);

	NF = (long) force_number(NF_node->var_value);

	if (NF < 0)
		fatal(_("NF set to negative value"));

	if (NF > nf_high_water)
		grow_fields_arr(NF);
	if (parse_high_water < NF) {
		for (i = parse_high_water + 1; i >= 0 && i <= NF; i++) {
			unref(fields_arr[i]);
			getnode(n);
			*n = *Null_field;
			fields_arr[i] = n;
		}
	} else if (parse_high_water > 0) {
		for (i = NF + 1; i >= 0 && i <= parse_high_water; i++) {
			unref(fields_arr[i]);
			getnode(n);
			*n = *Null_field;
			fields_arr[i] = n;
		}
		parse_high_water = NF;
	}
	field0_valid = FALSE;
}

/*
 * re_parse_field --- parse fields using a regexp.
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a regular
 * expression -- either user-defined or because RS=="" and FS==" "
 */
static long
re_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n)
{
	register char *scan = *buf;
	register long nf = parse_high_water;
	register char *field;
	register char *end = scan + len;
#ifdef MBS_SUPPORT
	size_t mbclen = 0;
	mbstate_t mbs;
	if (gawk_mb_cur_max > 1)
		memset(&mbs, 0, sizeof(mbstate_t));
#endif

	if (up_to == HUGE)
		nf = 0;
	if (len == 0)
		return nf;

	if (RS_is_null && default_FS)
		while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\n'))
			scan++;
	field = scan;
	while (scan < end
	       && research(rp, scan, 0, (end - scan), TRUE) != -1
	       && nf < up_to) {
		if (REEND(rp, scan) == RESTART(rp, scan)) {   /* null match */
#ifdef MBS_SUPPORT
			if (gawk_mb_cur_max > 1)	{
				mbclen = mbrlen(scan, end-scan, &mbs);
				if ((mbclen == 1) || (mbclen == (size_t) -1)
					|| (mbclen == (size_t) -2) || (mbclen == 0)) {
					/* We treat it as a singlebyte character.  */
					mbclen = 1;
				}
				scan += mbclen;
			} else
#endif
			scan++;
			if (scan == end) {
				(*set)(++nf, field, (long)(scan - field), n);
				up_to = nf;
				break;
			}
			continue;
		}
		(*set)(++nf, field,
		       (long)(scan + RESTART(rp, scan) - field), n);
		scan += REEND(rp, scan);
		field = scan;
		if (scan == end)	/* FS at end of record */
			(*set)(++nf, field, 0L, n);
	}
	if (nf != up_to && scan < end) {
		(*set)(++nf, scan, (long)(end - scan), n);
		scan = end;
	}
	*buf = scan;
	return (nf);
}

/*
 * def_parse_field --- default field parsing.
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a single space
 * character.
 */

static long
def_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n)
{
	register char *scan = *buf;
	register long nf = parse_high_water;
	register char *field;
	register char *end = scan + len;
	char sav;

	if (up_to == HUGE)
		nf = 0;
	if (len == 0)
		return nf;

	/*
	 * Nasty special case. If FS set to "", return whole record
	 * as first field. This is not worth a separate function.
	 */
	if (fs->stlen == 0) {
		(*set)(++nf, *buf, len, n);
		*buf += len;
		return nf;
	}

	/* before doing anything save the char at *end */
	sav = *end;
	/* because it will be destroyed now: */

	*end = ' ';	/* sentinel character */
	for (; nf < up_to; scan++) {
		/*
		 * special case:  fs is single space, strip leading whitespace 
		 */
		while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\n'))
			scan++;
		if (scan >= end)
			break;
		field = scan;
		while (*scan != ' ' && *scan != '\t' && *scan != '\n')
			scan++;
		(*set)(++nf, field, (long)(scan - field), n);
		if (scan == end)
			break;
	}

	/* everything done, restore original char at *end */
	*end = sav;

	*buf = scan;
	return nf;
}

/*
 * posix_def_parse_field --- default field parsing.
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a single space
 * character.  The only difference between this and def_parse_field()
 * is that this one does not allow newlines to separate fields.
 */

static long
posix_def_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n)
{
	register char *scan = *buf;
	register long nf = parse_high_water;
	register char *field;
	register char *end = scan + len;
	char sav;

	if (up_to == HUGE)
		nf = 0;
	if (len == 0)
		return nf;

	/*
	 * Nasty special case. If FS set to "", return whole record
	 * as first field. This is not worth a separate function.
	 */
	if (fs->stlen == 0) {
		(*set)(++nf, *buf, len, n);
		*buf += len;
		return nf;
	}

	/* before doing anything save the char at *end */
	sav = *end;
	/* because it will be destroyed now: */

	*end = ' ';	/* sentinel character */
	for (; nf < up_to; scan++) {
		/*
		 * special case:  fs is single space, strip leading whitespace 
		 */
		while (scan < end && (*scan == ' ' || *scan == '\t'))
			scan++;
		if (scan >= end)
			break;
		field = scan;
		while (*scan != ' ' && *scan != '\t')
			scan++;
		(*set)(++nf, field, (long)(scan - field), n);
		if (scan == end)
			break;
	}

	/* everything done, restore original char at *end */
	*end = sav;

	*buf = scan;
	return nf;
}

/*
 * null_parse_field --- each character is a separate field
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is the null string.
 */
static long
null_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n)
{
	register char *scan = *buf;
	register long nf = parse_high_water;
	register char *end = scan + len;

	if (up_to == HUGE)
		nf = 0;
	if (len == 0)
		return nf;

#ifdef MBS_SUPPORT
	if (gawk_mb_cur_max > 1) {
		mbstate_t mbs;
		memset(&mbs, 0, sizeof(mbstate_t));
		for (; nf < up_to && scan < end;) {
			size_t mbclen = mbrlen(scan, end-scan, &mbs);
			if ((mbclen == 1) || (mbclen == (size_t) -1)
				|| (mbclen == (size_t) -2) || (mbclen == 0)) {
				/* We treat it as a singlebyte character.  */
				mbclen = 1;
			}
			(*set)(++nf, scan, mbclen, n);
			scan += mbclen;
		}
	} else
#endif
	for (; nf < up_to && scan < end; scan++)
		(*set)(++nf, scan, 1L, n);

	*buf = scan;
	return nf;
}

/*
 * sc_parse_field --- single character field separator
 *
 * This is called both from get_field() and from do_split()
 * via (*parse_field)().  This variation is for when FS is a single character
 * other than space.
 */
static long
sc_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n)
{
	register char *scan = *buf;
	register char fschar;
	register long nf = parse_high_water;
	register char *field;
	register char *end = scan + len;
	char sav;
#ifdef MBS_SUPPORT
	size_t mbclen = 0;
	mbstate_t mbs;
	if (gawk_mb_cur_max > 1)
		memset(&mbs, 0, sizeof(mbstate_t));
#endif

	if (up_to == HUGE)
		nf = 0;
	if (len == 0)
		return nf;

	if (RS_is_null && fs->stlen == 0)
		fschar = '\n';
	else
		fschar = fs->stptr[0];

	/* before doing anything save the char at *end */
	sav = *end;
	/* because it will be destroyed now: */
	*end = fschar;	/* sentinel character */

	for (; nf < up_to;) {
		field = scan;
#ifdef MBS_SUPPORT
		if (gawk_mb_cur_max > 1) {
			while (*scan != fschar) {
				mbclen = mbrlen(scan, end-scan, &mbs);
				if ((mbclen == 1) || (mbclen == (size_t) -1)
					|| (mbclen == (size_t) -2) || (mbclen == 0)) {
					/* We treat it as a singlebyte character.  */
					mbclen = 1;
				}
				scan += mbclen;
			}
		} else
#endif
		while (*scan != fschar)
			scan++;
		(*set)(++nf, field, (long)(scan - field), n);
		if (scan == end)
			break;
		scan++;
		if (scan == end) {	/* FS at end of record */
			(*set)(++nf, field, 0L, n);
			break;
		}
	}

	/* everything done, restore original char at *end */
	*end = sav;

	*buf = scan;
	return nf;
}

/*
 * fw_parse_field --- field parsing using FIELDWIDTHS spec
 *
 * This is called from get_field() via (*parse_field)().
 * This variation is for fields are fixed widths.
 */
static long
fw_parse_field(long up_to,	/* parse only up to this field number */
	char **buf,	/* on input: string to parse; on output: point to start next */
	int len,
	NODE *fs ATTRIBUTE_UNUSED,
	Regexp *rp ATTRIBUTE_UNUSED,
	Setfunc set,	/* routine to set the value of the parsed field */
	NODE *n)
{
	register char *scan = *buf;
	register long nf = parse_high_water;
	register char *end = scan + len;

	if (up_to == HUGE)
		nf = 0;
	if (len == 0)
		return nf;
	for (; nf < up_to && (len = FIELDWIDTHS[nf+1]) != -1; ) {
		if (len > end - scan)
			len = end - scan;
		(*set)(++nf, scan, (long) len, n);
		scan += len;
	}
	if (len == -1)
		*buf = end;
	else
		*buf = scan;
	return nf;
}

/* get_field --- return a particular $n */

/* assign is not NULL if this field is on the LHS of an assign */

NODE **
get_field(register long requested, Func_ptr *assign)
{
	/*
	 * if requesting whole line but some other field has been altered,
	 * then the whole line must be rebuilt
	 */
	if (requested == 0) {
		if (! field0_valid) {
			/* first, parse remainder of input record */
			if (NF == -1) {
				NF = (*parse_field)(HUGE-1, &parse_extent,
		    			fields_arr[0]->stlen -
					(parse_extent - fields_arr[0]->stptr),
		    			save_FS, FS_regexp, set_field,
					(NODE *) NULL);
				parse_high_water = NF;
			}
			rebuild_record();
		}
		if (assign != NULL)
			*assign = reset_record;
		return &fields_arr[0];
	}

	/* assert(requested > 0); */

	if (assign != NULL)
		field0_valid = FALSE;		/* $0 needs reconstruction */

	if (requested <= parse_high_water)	/* already parsed this field */
		return &fields_arr[requested];

	if (NF == -1) {	/* have not yet parsed to end of record */
		/*
		 * parse up to requested fields, calling set_field() for each,
		 * saving in parse_extent the point where the parse left off
		 */
		if (parse_high_water == 0)	/* starting at the beginning */
			parse_extent = fields_arr[0]->stptr;
		parse_high_water = (*parse_field)(requested, &parse_extent,
		     fields_arr[0]->stlen - (parse_extent - fields_arr[0]->stptr),
		     save_FS, FS_regexp, set_field, (NODE *) NULL);

		/*
		 * if we reached the end of the record, set NF to the number of
		 * fields so far.  Note that requested might actually refer to
		 * a field that is beyond the end of the record, but we won't
		 * set NF to that value at this point, since this is only a
		 * reference to the field and NF only gets set if the field
		 * is assigned to -- this case is handled below
		 */
		if (parse_extent == fields_arr[0]->stptr + fields_arr[0]->stlen)
			NF = parse_high_water;
		if (requested == HUGE-1)	/* HUGE-1 means set NF */
			requested = parse_high_water;
	}
	if (parse_high_water < requested) { /* requested beyond end of record */
		if (assign != NULL) {	/* expand record */
			if (requested > nf_high_water)
				grow_fields_arr(requested);

			NF = requested;
			parse_high_water = requested;
		} else
			return &Null_field;
	}

	return &fields_arr[requested];
}

/* set_element --- set an array element, used by do_split() */

static void
set_element(long num, char *s, long len, NODE *n)
{
	register NODE *it;

	it = make_string(s, len);
	it->flags |= MAYBE_NUM;
	*assoc_lookup(n, tmp_number((AWKNUM) (num)), FALSE) = it;
}

/* do_split --- implement split(), semantics are same as for field splitting */

NODE *
do_split(NODE *tree)
{
	NODE *src, *arr, *sep, *fs, *src2, *fs2, *tmp;
	char *s;
	long (*parseit) P((long, char **, int, NODE *,
			 Regexp *, Setfunc, NODE *));
	Regexp *rp = NULL;

	src = force_string(tree_eval(tree->lnode));

	arr = get_param(tree->rnode->lnode);
	if (arr->type != Node_var_array)
		fatal(_("split: second argument is not an array"));

	sep = tree->rnode->rnode->lnode;

	if (src->stlen == 0) {
		/*
		 * Skip the work if first arg is the null string.
		 */
		free_temp(src);
		/*
		 * Evaluate sep if it may have side effects.
		 */
		if ((sep->re_flags & (FS_DFLT|CONST)) == 0)
			free_temp(tree_eval(sep->re_exp));
		/*
		 * And now we can safely turn off the array.
		 */
		assoc_clear(arr);
		return tmp_number((AWKNUM) 0);
	}

	if ((sep->re_flags & FS_DFLT) != 0 && ! using_FIELDWIDTHS() && ! RS_is_null) {
		parseit = parse_field;
		fs = force_string(FS_node->var_value);
		rp = FS_regexp;
	} else {
		fs = force_string(tree_eval(sep->re_exp));
		if (fs->stlen == 0) {
			static short warned = FALSE;

			parseit = null_parse_field;

			if (do_lint && ! warned) {
				warned = TRUE;
				lintwarn(_("split: null string for third arg is a gawk extension"));
			}
		} else if (fs->stlen == 1 && (sep->re_flags & CONST) == 0) {
			if (fs->stptr[0] == ' ') {
				if (do_posix)
					parseit = posix_def_parse_field;
				else
					parseit = def_parse_field;
			} else
				parseit = sc_parse_field;
		} else {
			parseit = re_parse_field;
			rp = re_update(sep);
		}
	}

	/*
	 * do dupnode(), to avoid problems like
	 *	x = split(a["LINE"], a, a["FS"])
	 * since we assoc_clear the array. gack.
	 * this also gives us complete call by value semantics.
	 */
	src2 = dupnode(src);
	free_temp(src);

	fs2 = dupnode(fs);
	free_temp(fs);

	assoc_clear(arr);

	s = src2->stptr;
	tmp = tmp_number((AWKNUM) (*parseit)(HUGE, &s, (int) src2->stlen,
					     fs2, rp, set_element, arr));
	unref(src2);
	unref(fs2);
	return tmp;
}

/* set_FIELDWIDTHS --- handle an assignment to FIELDWIDTHS */

void
set_FIELDWIDTHS()
{
	register char *scan;
	char *end;
	register int i;
	static int fw_alloc = 4;
	static int warned = FALSE;
	extern double strtod();

	if (do_lint && ! warned) {
		warned = TRUE;
		lintwarn(_("`FIELDWIDTHS' is a gawk extension"));
	}
	if (do_traditional)	/* quick and dirty, does the trick */
		return;

	/*
	 * If changing the way fields are split, obey least-suprise
	 * semantics, and force $0 to be split totally.
	 */
	if (fields_arr != NULL)
		(void) get_field(HUGE - 1, 0);

	parse_field = fw_parse_field;
	scan = force_string(FIELDWIDTHS_node->var_value)->stptr;
	end = scan + 1;
	if (FIELDWIDTHS == NULL)
		emalloc(FIELDWIDTHS, int *, fw_alloc * sizeof(int), "set_FIELDWIDTHS");
	FIELDWIDTHS[0] = 0;
	for (i = 1; ; i++) {
		if (i >= fw_alloc) {
			fw_alloc *= 2;
			erealloc(FIELDWIDTHS, int *, fw_alloc * sizeof(int), "set_FIELDWIDTHS");
		}
		FIELDWIDTHS[i] = (int) strtod(scan, &end);
		if (end == scan)
			break;
		if (FIELDWIDTHS[i] <= 0)
			fatal(_("field %d in FIELDWIDTHS, must be > 0"), i);
		scan = end;
	}
	FIELDWIDTHS[i] = -1;

	update_PROCINFO("FS", "FIELDWIDTHS");
}

/* set_FS --- handle things when FS is assigned to */

void
set_FS()
{
	char buf[10];
	NODE *fs;
	static NODE *save_fs = NULL;
	static NODE *save_rs = NULL;
	int remake_re = TRUE;

	/*
	 * If changing the way fields are split, obey least-suprise
	 * semantics, and force $0 to be split totally.
	 */
	if (fields_arr != NULL)
		(void) get_field(HUGE - 1, 0);

	/* It's possible that only IGNORECASE changed, or FS = FS */
	/*
	 * This comparison can't use cmp_nodes(), which pays attention
	 * to IGNORECASE, and that's not what we want.
	 */
	if (save_fs
		&& FS_node->var_value->stlen == save_fs->stlen
		&& STREQN(FS_node->var_value->stptr, save_fs->stptr, save_fs->stlen)
		&& save_rs
		&& RS_node->var_value->stlen == save_rs->stlen
		&& STREQN(RS_node->var_value->stptr, save_rs->stptr, save_rs->stlen)) {
		if (FS_regexp != NULL)
			FS_regexp = (IGNORECASE ? FS_re_no_case : FS_re_yes_case);

		/* FS = FS */
		if (! using_FIELDWIDTHS())
			return;
		else {
			remake_re = FALSE;
			goto choose_fs_function;
		}
	}

	unref(save_fs);
	save_fs = dupnode(FS_node->var_value);
	unref(save_rs);
	save_rs = dupnode(RS_node->var_value);
	resave_fs = TRUE;
	if (FS_regexp != NULL) {
		refree(FS_re_yes_case);
		refree(FS_re_no_case);
		FS_re_yes_case = FS_re_no_case = FS_regexp = NULL;
	}


choose_fs_function:
	buf[0] = '\0';
	default_FS = FALSE;
	fs = force_string(FS_node->var_value);

	if (! do_traditional && fs->stlen == 0) {
		static short warned = FALSE;

		parse_field = null_parse_field;

		if (do_lint && ! warned) {
			warned = TRUE;
			lintwarn(_("null string for `FS' is a gawk extension"));
		}
	} else if (fs->stlen > 1) {
		parse_field = re_parse_field;
	} else if (RS_is_null) {
		/* we know that fs->stlen <= 1 */
		parse_field = sc_parse_field;
		if (fs->stlen == 1) {
			if (fs->stptr[0] == ' ') {
				default_FS = TRUE;
				strcpy(buf, "[ \t\n]+");
			} else if (fs->stptr[0] == '\\') {
				/* yet another special case */
				strcpy(buf, "[\\\\\n]");
			} else if (fs->stptr[0] != '\n')
				sprintf(buf, "[%c\n]", fs->stptr[0]);
		}
	} else {
		if (do_posix)
			parse_field = posix_def_parse_field;
		else
			parse_field = def_parse_field;

		if (fs->stlen == 1) {
			if (fs->stptr[0] == ' ')
				default_FS = TRUE;
			else if (fs->stptr[0] == '\\')
				/* same special case */
				strcpy(buf, "[\\\\]");
			else
				parse_field = sc_parse_field;
		}
	}
	if (remake_re) {
		if (FS_regexp != NULL) {
			refree(FS_re_yes_case);
			refree(FS_re_no_case);
			FS_re_yes_case = FS_re_no_case = FS_regexp = NULL;
		}

		if (buf[0] != '\0') {
			FS_re_yes_case = make_regexp(buf, strlen(buf), FALSE);
			FS_re_no_case = make_regexp(buf, strlen(buf), TRUE);
			FS_regexp = (IGNORECASE ? FS_re_no_case : FS_re_yes_case);
			parse_field = re_parse_field;
		} else if (parse_field == re_parse_field) {
			FS_re_yes_case = make_regexp(fs->stptr, fs->stlen, FALSE);
			FS_re_no_case = make_regexp(fs->stptr, fs->stlen, TRUE);
			FS_regexp = (IGNORECASE ? FS_re_no_case : FS_re_yes_case);
		} else
			FS_re_yes_case = FS_re_no_case = FS_regexp = NULL;
	}

	/*
	 * For FS = "c", we don't use IGNORECASE. But we must use
	 * re_parse_field to get the character and the newline as
	 * field separators.
	 */
	if (fs->stlen == 1 && parse_field == re_parse_field)
		FS_regexp = FS_re_yes_case;

	update_PROCINFO("FS", "FS");
}

/* using_fieldwidths --- is FS or FIELDWIDTHS in use? */

int
using_fieldwidths()
{
	return using_FIELDWIDTHS();
}

/* update_PROCINFO --- update PROCINFO[sub] when FS or FIELDWIDTHS set */

static void
update_PROCINFO(char *subscript, char *str)
{
	NODE **aptr;

	if (PROCINFO_node == NULL)
		return;

	aptr = assoc_lookup(PROCINFO_node, tmp_string(subscript, strlen(subscript)), FALSE);
	assign_val(aptr, tmp_string(str, strlen(str)));
}
