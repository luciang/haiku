/*
 * eval.c - gawk parse tree interpreter 
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

extern double pow P((double x, double y));
extern double modf P((double x, double *yp));
extern double fmod P((double x, double y));

static int eval_condition P((NODE *tree));
static NODE *op_assign P((NODE *tree));
static NODE *func_call P((NODE *tree));
static NODE *match_op P((NODE *tree));
static void pop_forloop P((void));
static inline void pop_all_forloops P((void));
static void push_forloop P((const char *varname, NODE **elems, size_t nelems));
static void push_args P((int count, NODE *arglist, NODE **oldstack,
			const char *func_name, char **varnames));
static inline void pop_fcall_stack P((void));
static void pop_fcall P((void));
static int comp_func P((const void *p1, const void *p2));

#if __GNUC__ < 2
NODE *_t;		/* used as a temporary in macros */
#endif
#ifdef MSDOS
double _msc51bug;	/* to get around a bug in MSC 5.1 */
#endif
NODE *ret_node;
int OFSlen;
int ORSlen;
int OFMTidx;
int CONVFMTidx;

/* Profiling stuff */
#ifdef PROFILING
#define INCREMENT(n)	n++
#else
#define INCREMENT(n)	/* nothing */
#endif

/* Macros and variables to save and restore function and loop bindings */
/*
 * the val variable allows return/continue/break-out-of-context to be
 * caught and diagnosed
 */
#define PUSH_BINDING(stack, x, val) (memcpy((char *)(stack), (const char *)(x), sizeof(jmp_buf)), val++)
#define RESTORE_BINDING(stack, x, val) (memcpy((char *)(x), (const char *)(stack), sizeof(jmp_buf)), val--)

static jmp_buf loop_tag;		/* always the current binding */
static int loop_tag_valid = FALSE;	/* nonzero when loop_tag valid */
static int func_tag_valid = FALSE;
static jmp_buf func_tag;
extern int exiting, exit_val;

/* This rather ugly macro is for VMS C */
#ifdef C
#undef C
#endif
#define C(c) ((char)c)  
/*
 * This table is used by the regexp routines to do case independant
 * matching. Basically, every ascii character maps to itself, except
 * uppercase letters map to lower case ones. This table has 256
 * entries, for ISO 8859-1. Note also that if the system this
 * is compiled on doesn't use 7-bit ascii, casetable[] should not be
 * defined to the linker, so gawk should not load.
 *
 * Do NOT make this array static, it is used in several spots, not
 * just in this file.
 */
#if 'a' == 97	/* it's ascii */
const char casetable[] = {
	'\000', '\001', '\002', '\003', '\004', '\005', '\006', '\007',
	'\010', '\011', '\012', '\013', '\014', '\015', '\016', '\017',
	'\020', '\021', '\022', '\023', '\024', '\025', '\026', '\027',
	'\030', '\031', '\032', '\033', '\034', '\035', '\036', '\037',
	/* ' '     '!'     '"'     '#'     '$'     '%'     '&'     ''' */
	'\040', '\041', '\042', '\043', '\044', '\045', '\046', '\047',
	/* '('     ')'     '*'     '+'     ','     '-'     '.'     '/' */
	'\050', '\051', '\052', '\053', '\054', '\055', '\056', '\057',
	/* '0'     '1'     '2'     '3'     '4'     '5'     '6'     '7' */
	'\060', '\061', '\062', '\063', '\064', '\065', '\066', '\067',
	/* '8'     '9'     ':'     ';'     '<'     '='     '>'     '?' */
	'\070', '\071', '\072', '\073', '\074', '\075', '\076', '\077',
	/* '@'     'A'     'B'     'C'     'D'     'E'     'F'     'G' */
	'\100', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
	/* 'H'     'I'     'J'     'K'     'L'     'M'     'N'     'O' */
	'\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
	/* 'P'     'Q'     'R'     'S'     'T'     'U'     'V'     'W' */
	'\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
	/* 'X'     'Y'     'Z'     '['     '\'     ']'     '^'     '_' */
	'\170', '\171', '\172', '\133', '\134', '\135', '\136', '\137',
	/* '`'     'a'     'b'     'c'     'd'     'e'     'f'     'g' */
	'\140', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
	/* 'h'     'i'     'j'     'k'     'l'     'm'     'n'     'o' */
	'\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
	/* 'p'     'q'     'r'     's'     't'     'u'     'v'     'w' */
	'\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
	/* 'x'     'y'     'z'     '{'     '|'     '}'     '~' */
	'\170', '\171', '\172', '\173', '\174', '\175', '\176', '\177',

	/* Latin 1: */
	C('\200'), C('\201'), C('\202'), C('\203'), C('\204'), C('\205'), C('\206'), C('\207'),
	C('\210'), C('\211'), C('\212'), C('\213'), C('\214'), C('\215'), C('\216'), C('\217'),
	C('\220'), C('\221'), C('\222'), C('\223'), C('\224'), C('\225'), C('\226'), C('\227'),
	C('\230'), C('\231'), C('\232'), C('\233'), C('\234'), C('\235'), C('\236'), C('\237'),
	C('\240'), C('\241'), C('\242'), C('\243'), C('\244'), C('\245'), C('\246'), C('\247'),
	C('\250'), C('\251'), C('\252'), C('\253'), C('\254'), C('\255'), C('\256'), C('\257'),
	C('\260'), C('\261'), C('\262'), C('\263'), C('\264'), C('\265'), C('\266'), C('\267'),
	C('\270'), C('\271'), C('\272'), C('\273'), C('\274'), C('\275'), C('\276'), C('\277'),
	C('\340'), C('\341'), C('\342'), C('\343'), C('\344'), C('\345'), C('\346'), C('\347'),
	C('\350'), C('\351'), C('\352'), C('\353'), C('\354'), C('\355'), C('\356'), C('\357'),
	C('\360'), C('\361'), C('\362'), C('\363'), C('\364'), C('\365'), C('\366'), C('\327'),
	C('\370'), C('\371'), C('\372'), C('\373'), C('\374'), C('\375'), C('\376'), C('\337'),
	C('\340'), C('\341'), C('\342'), C('\343'), C('\344'), C('\345'), C('\346'), C('\347'),
	C('\350'), C('\351'), C('\352'), C('\353'), C('\354'), C('\355'), C('\356'), C('\357'),
	C('\360'), C('\361'), C('\362'), C('\363'), C('\364'), C('\365'), C('\366'), C('\367'),
	C('\370'), C('\371'), C('\372'), C('\373'), C('\374'), C('\375'), C('\376'), C('\377'),
};
#else
#include "You lose. You will need a translation table for your character set."
#endif

#undef C

/*
 * This table maps node types to strings for debugging.
 * KEEP IN SYNC WITH awk.h!!!!
 */
static const char *const nodetypes[] = {
	"Node_illegal",
	"Node_times",
	"Node_quotient",
	"Node_mod",
	"Node_plus",
	"Node_minus",
	"Node_cond_pair",
	"Node_subscript",
	"Node_concat",
	"Node_exp",
	"Node_preincrement",
	"Node_predecrement",
	"Node_postincrement",
	"Node_postdecrement",
	"Node_unary_minus",
	"Node_field_spec",
	"Node_assign",
	"Node_assign_times",
	"Node_assign_quotient",
	"Node_assign_mod",
	"Node_assign_plus",
	"Node_assign_minus",
	"Node_assign_exp",
	"Node_and",
	"Node_or",
	"Node_equal",
	"Node_notequal",
	"Node_less",
	"Node_greater",
	"Node_leq",
	"Node_geq",
	"Node_match",
	"Node_nomatch",
	"Node_not",
	"Node_rule_list",
	"Node_rule_node",
	"Node_statement_list",
	"Node_switch_body",
	"Node_case_list",
	"Node_if_branches",
	"Node_expression_list",
	"Node_param_list",
	"Node_K_if",
	"Node_K_switch",
	"Node_K_case",
	"Node_K_default",
	"Node_K_while",	
	"Node_K_for",
	"Node_K_arrayfor",
	"Node_K_break",
	"Node_K_continue",
	"Node_K_print",
	"Node_K_print_rec",
	"Node_K_printf",
	"Node_K_next",
	"Node_K_exit",
	"Node_K_do",
	"Node_K_return",
	"Node_K_delete",
	"Node_K_delete_loop",
	"Node_K_getline",
	"Node_K_function",
	"Node_K_nextfile",
	"Node_redirect_output",
	"Node_redirect_append",
	"Node_redirect_pipe",
	"Node_redirect_pipein",
	"Node_redirect_input",
	"Node_redirect_twoway",
	"Node_var_new",
	"Node_var",
	"Node_var_array",
	"Node_val",
	"Node_builtin",
	"Node_line_range",
	"Node_in_array",
	"Node_func",
	"Node_func_call",
	"Node_cond_exp",
	"Node_regex",
	"Node_dynregex",
	"Node_hashnode",
	"Node_ahash",
	"Node_array_ref",
	"Node_BINMODE",
	"Node_CONVFMT",
	"Node_FIELDWIDTHS",
	"Node_FNR",
	"Node_FS",
	"Node_IGNORECASE",
	"Node_LINT",
	"Node_NF",
	"Node_NR",
	"Node_OFMT",
	"Node_OFS",
	"Node_ORS",
	"Node_RS",
	"Node_TEXTDOMAIN",
	"Node_final --- this should never appear",
	NULL
};

/* nodetype2str --- convert a node type into a printable value */

const char *
nodetype2str(NODETYPE type)
{
	static char buf[40];

	if (type >= Node_illegal && type <= Node_final)
		return nodetypes[(int) type];

	sprintf(buf, _("unknown nodetype %d"), (int) type);
	return buf;
}

/* flags2str --- make a flags value readable */

const char *
flags2str(int flagval)
{
	static const struct flagtab values[] = {
		{ MALLOC, "MALLOC" },
		{ TEMP, "TEMP" },
		{ PERM, "PERM" },
		{ STRING, "STRING" },
		{ STRCUR, "STRCUR" },
		{ NUMCUR, "NUMCUR" },
		{ NUMBER, "NUMBER" },
		{ MAYBE_NUM, "MAYBE_NUM" },
		{ ARRAYMAXED, "ARRAYMAXED" },
		{ FUNC, "FUNC" },
		{ FIELD, "FIELD" },
		{ INTLSTR, "INTLSTR" },
		{ 0,	NULL },
	};

	return genflags2str(flagval, values);
}

/* genflags2str --- general routine to convert a flag value to a string */

const char *
genflags2str(int flagval, const struct flagtab *tab)
{
	static char buffer[BUFSIZ];
	char *sp;
	int i, space_left, space_needed;

	sp = buffer;
	space_left = BUFSIZ;
	for (i = 0; tab[i].name != NULL; i++) {
		/*
		 * note the trick, we want 1 or 0 for whether we need
		 * the '|' character.
		 */
		space_needed = (strlen(tab[i].name) + (sp != buffer));
		if (space_left < space_needed)
			fatal(_("buffer overflow in genflags2str"));

		if ((flagval & tab[i].val) != 0) {
			if (sp != buffer) {
				*sp++ = '|';
				space_left--;
			}
			strcpy(sp, tab[i].name);
			/* note ordering! */
			space_left -= strlen(sp);
			sp += strlen(sp);
		}
	}

	return buffer;
}

/*
 * interpret:
 * Tree is a bunch of rules to run. Returns zero if it hit an exit()
 * statement 
 */
int
interpret(register NODE *volatile tree)
{
	jmp_buf volatile loop_tag_stack; /* shallow binding stack for loop_tag */
	static jmp_buf rule_tag; /* tag the rule currently being run, for NEXT
				  * and EXIT statements.  It is static because
				  * there are no nested rules */
	register NODE *volatile t = NULL;	/* temporary */
	NODE **volatile lhs;	/* lhs == Left Hand Side for assigns, etc */
	NODE *volatile stable_tree;
	int volatile traverse = TRUE;	/* True => loop thru tree (Node_rule_list) */

	/* avoid false source indications */
	source = NULL;
	sourceline = 0;

	if (tree == NULL)
		return 1;
	sourceline = tree->source_line;
	source = tree->source_file;
	switch (tree->type) {
	case Node_rule_node:
		traverse = FALSE;  /* False => one for-loop iteration only */
		/* FALL THROUGH */
	case Node_rule_list:
		for (t = tree; t != NULL; t = t->rnode) {
			if (traverse)
				tree = t->lnode;
			sourceline = tree->source_line;
			source = tree->source_file;
			INCREMENT(tree->exec_count);
			switch (setjmp(rule_tag)) {
			case 0:	/* normal non-jump */
				/* test pattern, if any */
				if (tree->lnode == NULL ||
				    eval_condition(tree->lnode)) {
					/* using the lnode exec_count is kludgey */
					if (tree->lnode != NULL)
						INCREMENT(tree->lnode->exec_count);
					(void) interpret(tree->rnode);
				}
				break;
			case TAG_CONTINUE:	/* NEXT statement */
				pop_all_forloops();
				pop_fcall_stack();
				return 1;
			case TAG_BREAK:		/* EXIT statement */
				pop_all_forloops();
				pop_fcall_stack();
				return 0;
			default:
				cant_happen();
			}
			if (! traverse) 	/* case Node_rule_node */
				break;		/* don't loop */
		}
		break;

	case Node_statement_list:
		for (t = tree; t != NULL; t = t->rnode)
			(void) interpret(t->lnode);
		break;

	case Node_K_if:
		INCREMENT(tree->exec_count);
		if (eval_condition(tree->lnode)) {
			INCREMENT(tree->rnode->exec_count);
			(void) interpret(tree->rnode->lnode);
		} else {
			(void) interpret(tree->rnode->rnode);
		}
		break;

	case Node_K_switch:
		{
		NODE *switch_value;
		NODE *switch_body;
		NODE *case_list;
		NODE *default_list;
		NODE *case_stmt;

		int match_found = FALSE;

		PUSH_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		INCREMENT(tree->exec_count);
		stable_tree = tree;

		switch_value = tree_eval(stable_tree->lnode);
		switch_body = stable_tree->rnode;
		case_list = switch_body->lnode;
		default_list  = switch_body->rnode;

		for (; case_list != NULL; case_list = case_list->rnode) {
			case_stmt = case_list->lnode;

			/*
			 * Once a match is found, all cases will be processed as they fall through,
			 * so continue to execute statements until a break is reached.
			 */
			if (! match_found) {
				if (case_stmt->type == Node_K_default)
					;	/* do nothing */
				else if (case_stmt->lnode->type == Node_regex) {
					NODE *t1;
					Regexp *rp;

					t1 = force_string(switch_value);
					rp = re_update(case_stmt->lnode);

					match_found = (research(rp, t1->stptr, 0, t1->stlen, FALSE) >= 0);
					if (t1 != switch_value)
						free_temp(t1);
				} else {
					match_found = (cmp_nodes(switch_value, case_stmt->lnode) == 0);
				}
			}

			/* If a match was found, execute the statements associated with the case. */
			if (match_found) {
				INCREMENT(case_stmt->exec_count);
				switch (setjmp(loop_tag)) {
				case 0:                /* Normal non-jump    */
					(void) interpret(case_stmt->rnode);
					break;
				case TAG_CONTINUE:     /* continue statement */
					free_temp(switch_value);
					RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
					longjmp(loop_tag, TAG_CONTINUE);
					break;
				case TAG_BREAK:        /* break statement    */
					free_temp(switch_value);
					RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
					return 1;
				default:
					cant_happen();
				}
			}

		}

		free_temp(switch_value);

		/*
		 * If a default section was found, execute the statements associated with it
		 * and execute any trailing case statements if the default falls through.
		 */
		if (! match_found && default_list != NULL) {
			for (case_list = default_list;
					case_list != NULL; case_list = case_list->rnode) {
				case_stmt = case_list->lnode;

				INCREMENT(case_stmt->exec_count);
				switch (setjmp(loop_tag)) {
				case 0:                /* Normal non-jump    */
					(void) interpret(case_stmt->rnode);
					break;
				case TAG_CONTINUE:     /* continue statement */
					RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
					longjmp(loop_tag, TAG_CONTINUE);
					break;
				case TAG_BREAK:        /* break statement    */
					RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
					return 1;
				default:
					cant_happen();
				}
			}
		}

		RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		}
		break;

	case Node_K_while:
		PUSH_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);

		stable_tree = tree;
		while (eval_condition(stable_tree->lnode)) {
			INCREMENT(stable_tree->exec_count);
			switch (setjmp(loop_tag)) {
			case 0:	/* normal non-jump */
				(void) interpret(stable_tree->rnode);
				break;
			case TAG_CONTINUE:	/* continue statement */
				break;
			case TAG_BREAK:	/* break statement */
				RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
				return 1;
			default:
				cant_happen();
			}
		}
		RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		break;

	case Node_K_do:
		PUSH_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		stable_tree = tree;
		do {
			INCREMENT(stable_tree->exec_count);
			switch (setjmp(loop_tag)) {
			case 0:	/* normal non-jump */
				(void) interpret(stable_tree->rnode);
				break;
			case TAG_CONTINUE:	/* continue statement */
				break;
			case TAG_BREAK:	/* break statement */
				RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
				return 1;
			default:
				cant_happen();
			}
		} while (eval_condition(stable_tree->lnode));
		RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		break;

	case Node_K_for:
		PUSH_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		(void) interpret(tree->forloop->init);
		stable_tree = tree;
		while (eval_condition(stable_tree->forloop->cond)) {
			INCREMENT(stable_tree->exec_count);
			switch (setjmp(loop_tag)) {
			case 0:	/* normal non-jump */
				(void) interpret(stable_tree->lnode);
				/* fall through */
			case TAG_CONTINUE:	/* continue statement */
				(void) interpret(stable_tree->forloop->incr);
				break;
			case TAG_BREAK:	/* break statement */
				RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
				return 1;
			default:
				cant_happen();
			}
		}
		RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		break;

	case Node_K_arrayfor:
		{
		Func_ptr after_assign = NULL;
		NODE **list = NULL;
		NODE *volatile array;
		NODE *volatile save_array;
		volatile size_t i, num_elems;
		size_t j;
		volatile int retval = 0;
		int sort_indices = whiny_users;

#define hakvar forloop->init
#define arrvar forloop->incr
		/* get the array */
		save_array = tree->arrvar;
		array = get_array(save_array);

		/* sanity: do nothing if empty */
		if (array->var_array == NULL || array->table_size == 0)
			break;	/* from switch */

		/* allocate space for array */
		num_elems = array->table_size;
		emalloc(list, NODE **, num_elems * sizeof(NODE *), "for_loop");

		/* populate it */
		for (i = j = 0; i < array->array_size; i++) {
			NODE *t = array->var_array[i];

			if (t == NULL)
				continue;

			for (; t != NULL; t = t->ahnext) {
				list[j++] = dupnode(t);
				assert(list[j-1] == t);
			}
		}


		if (sort_indices)
			qsort(list, num_elems, sizeof(NODE *), comp_func); /* shazzam! */

		/* now we can run the loop */
		push_forloop(array->vname, list, num_elems);
		PUSH_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);

		lhs = get_lhs(tree->hakvar, &after_assign, FALSE);
		stable_tree = tree;
		for (i = 0; i < num_elems; i++) {
			INCREMENT(stable_tree->exec_count);
			unref(*((NODE **) lhs));
			*lhs = make_string(list[i]->ahname_str, list[i]->ahname_len);
			if (after_assign)
				(*after_assign)();
			switch (setjmp(loop_tag)) {
			case 0:
				(void) interpret(stable_tree->lnode);
			case TAG_CONTINUE:
				break;

			case TAG_BREAK:
				retval = 1;
				goto done;

			default:
				cant_happen();
			}
		}

	done:
		RESTORE_BINDING(loop_tag_stack, loop_tag, loop_tag_valid);
		pop_forloop();

		if (do_lint && num_elems != array->table_size)
			lintwarn(_("for loop: array `%s' changed size from %ld to %ld during loop execution"),
				array_vname(save_array), (long) num_elems, (long) array->table_size);
		
		if (retval == 1)
			return 1;
		break;
		}
#undef hakvar
#undef arrvar

	case Node_K_break:
		INCREMENT(tree->exec_count);
		if (! loop_tag_valid) {
			/*
			 * Old AT&T nawk treats break outside of loops like
			 * next. New ones catch it at parse time. Allow it if
			 * do_traditional is on, and complain if lint.
			 */
			static int warned = FALSE;

			if (do_lint && ! warned) {
				lintwarn(_("`break' outside a loop is not portable"));
				warned = TRUE;
			}
			if (! do_traditional || do_posix)
				fatal(_("`break' outside a loop is not allowed"));
			longjmp(rule_tag, TAG_CONTINUE);
		} else
			longjmp(loop_tag, TAG_BREAK);
		break;

	case Node_K_continue:
		INCREMENT(tree->exec_count);
		if (! loop_tag_valid) {
			/*
			 * Old AT&T nawk treats continue outside of loops like
			 * next. New ones catch it at parse time. Allow it if
			 * do_traditional is on, and complain if lint.
			 */
			static int warned = FALSE;

			if (do_lint && ! warned) {
				lintwarn(_("`continue' outside a loop is not portable"));
				warned = TRUE;
			}
			if (! do_traditional || do_posix)
				fatal(_("`continue' outside a loop is not allowed"));
			longjmp(rule_tag, TAG_CONTINUE);
		} else
			longjmp(loop_tag, TAG_CONTINUE);
		break;

	case Node_K_print:
		INCREMENT(tree->exec_count);
		do_print(tree);
		break;

	case Node_K_print_rec:
		INCREMENT(tree->exec_count);
		do_print_rec(tree);
		break;

	case Node_K_printf:
		INCREMENT(tree->exec_count);
		do_printf(tree);
		break;

	case Node_K_delete:
		INCREMENT(tree->exec_count);
		do_delete(tree->lnode, tree->rnode);
		break;

	case Node_K_delete_loop:
		INCREMENT(tree->exec_count);
		do_delete_loop(tree->lnode, tree->rnode);
		break;

	case Node_K_next:
		INCREMENT(tree->exec_count);
		if (in_begin_rule)
			fatal(_("`next' cannot be called from a BEGIN rule"));
		else if (in_end_rule)
			fatal(_("`next' cannot be called from an END rule"));

		/* could add a lint check here for in a loop or function */
		longjmp(rule_tag, TAG_CONTINUE);
		break;

	case Node_K_nextfile:
		INCREMENT(tree->exec_count);
		if (in_begin_rule)
			fatal(_("`nextfile' cannot be called from a BEGIN rule"));
		else if (in_end_rule)
			fatal(_("`nextfile' cannot be called from an END rule"));

		/* could add a lint check here for in a loop or function */
		/*
		 * Have to do this cleanup here, since we don't longjump
		 * back to the main awk rule loop (rule_tag).
		 */
		pop_all_forloops();
		pop_fcall_stack();

		do_nextfile();
		break;

	case Node_K_exit:
		INCREMENT(tree->exec_count);
		/*
		 * In A,K,&W, p. 49, it says that an exit statement "...
		 * causes the program to behave as if the end of input had
		 * occurred; no more input is read, and the END actions, if
		 * any are executed." This implies that the rest of the rules
		 * are not done. So we immediately break out of the main loop.
		 */
		exiting = TRUE;
		if (tree->lnode != NULL) {
			t = tree_eval(tree->lnode);
			exit_val = (int) force_number(t);
			free_temp(t);
		}
		longjmp(rule_tag, TAG_BREAK);
		break;

	case Node_K_return:
		INCREMENT(tree->exec_count);
		t = tree_eval(tree->lnode);
		ret_node = dupnode(t);
		free_temp(t);
		longjmp(func_tag, TAG_RETURN);
		break;

	default:
		/*
		 * Appears to be an expression statement.  Throw away the
		 * value. 
		 */
		if (do_lint && (tree->type == Node_var || tree->type == Node_var_new))
			lintwarn(_("statement has no effect"));
		INCREMENT(tree->exec_count);
		t = tree_eval(tree);
		if (t)	/* stopme() returns NULL */
			free_temp(t);
		break;
	}
	return 1;
}

/* r_tree_eval --- evaluate a subtree */

NODE *
r_tree_eval(register NODE *tree, int iscond)
{
	register NODE *r, *t1, *t2;	/* return value & temporary subtrees */
	register NODE **lhs;
	register int di;
	AWKNUM x, x1, x2;
	long lx;
#ifdef _CRAY
	long lx2;
#endif

#ifndef TREE_EVAL_MACRO
	if (tree == NULL)
		return Nnull_string;
	else if (tree->type == Node_val) {
		if (tree->stref <= 0)
			cant_happen();
		return ((tree->flags & INTLSTR) != 0
			? r_force_string(tree)
			: tree);
	} else if (tree->type == Node_var) {
		if (tree->var_value->stref <= 0)
			cant_happen();
		if (! var_uninitialized(tree))
			return tree->var_value;
	}
#endif

	if (tree->type == Node_param_list) {
		if ((tree->flags & FUNC) != 0)
			fatal(_("can't use function name `%s' as variable or array"),
					tree->vname);

		tree = stack_ptr[tree->param_cnt];

		if (tree == NULL) {
			if (do_lint)
				lintwarn(_("reference to uninitialized argument `%s'"),
						tree->vname);
			return Nnull_string;
		}

		if (do_lint && var_uninitialized(tree))
			lintwarn(_("reference to uninitialized argument `%s'"),
			      tree->vname);
	}

	switch (tree->type) {
	case Node_array_ref:
		if (tree->orig_array->type == Node_var_array)
			fatal(_("attempt to use array `%s' in a scalar context"),
				array_vname(tree));
		tree->orig_array->type = Node_var;
		/* fall through */
	case Node_var_new:
		tree->type = Node_var;
		tree->var_value = Nnull_string;
		/* fall through */
	case Node_var:
		if (do_lint && var_uninitialized(tree))
			lintwarn(_("reference to uninitialized variable `%s'"),
			      tree->vname);
		return tree->var_value;

	case Node_and:
		return tmp_number((AWKNUM) (eval_condition(tree->lnode)
					    && eval_condition(tree->rnode)));

	case Node_or:
		return tmp_number((AWKNUM) (eval_condition(tree->lnode)
					    || eval_condition(tree->rnode)));

	case Node_not:
		return tmp_number((AWKNUM) ! eval_condition(tree->lnode));

		/* Builtins */
	case Node_builtin:
		return (*tree->builtin)(tree->subnode);

	case Node_K_getline:
		return (do_getline(tree));

	case Node_in_array:
		return tmp_number((AWKNUM) (in_array(tree->lnode, tree->rnode) != NULL));

	case Node_func_call:
		return func_call(tree);

		/* unary operations */
	case Node_NR:
	case Node_FNR:
	case Node_NF:
	case Node_FIELDWIDTHS:
	case Node_FS:
	case Node_RS:
	case Node_field_spec:
	case Node_subscript:
	case Node_IGNORECASE:
	case Node_OFS:
	case Node_ORS:
	case Node_OFMT:
	case Node_CONVFMT:
	case Node_BINMODE:
	case Node_LINT:
	case Node_TEXTDOMAIN:
		lhs = get_lhs(tree, (Func_ptr *) NULL, TRUE);
		return *lhs;

	case Node_var_array:
		fatal(_("attempt to use array `%s' in a scalar context"),
			array_vname(tree));

	case Node_unary_minus:
		t1 = tree_eval(tree->subnode);
		x = -force_number(t1);
		free_temp(t1);
		return tmp_number(x);

	case Node_cond_exp:
		if (eval_condition(tree->lnode))
			return tree_eval(tree->rnode->lnode);
		return tree_eval(tree->rnode->rnode);

	case Node_match:
	case Node_nomatch:
	case Node_regex:
	case Node_dynregex:
		return match_op(tree);

	case Node_concat:
		{
		NODE **treelist;
		NODE **strlist;
		NODE *save_tree;
		register NODE **treep;
		register NODE **strp;
		register size_t len;
		register size_t supposed_len;
		char *str;
		register char *dest;
		int alloc_count, str_count;
		int i;

		/*
		 * This is an efficiency hack for multiple adjacent string
		 * concatenations, to avoid recursion and string copies.
		 *
		 * Node_concat trees grow downward to the left, so
		 * descend to lowest (first) node, accumulating nodes
		 * to evaluate to strings as we go.
		 */

		/*
		 * But first, no arbitrary limits. Count the number of
		 * nodes and malloc the treelist and strlist arrays.
		 * There will be alloc_count + 1 items to concatenate. We
		 * also leave room for an extra pointer at the end to
		 * use as a sentinel.  Thus, start alloc_count at 2.
		 */
		save_tree = tree;
		for (alloc_count = 2; tree != NULL && tree->type == Node_concat;
				tree = tree->lnode)
			alloc_count++;
		tree = save_tree;
		emalloc(treelist, NODE **, sizeof(NODE *) * alloc_count, "tree_eval");
		emalloc(strlist, NODE **, sizeof(NODE *) * alloc_count, "tree_eval");

		/* Now, here we go. */
		treep = treelist;
		while (tree != NULL && tree->type == Node_concat) {
			*treep++ = tree->rnode;
			tree = tree->lnode;
		}
		*treep = tree;
		/*
		 * Now, evaluate to strings in LIFO order, accumulating
		 * the string length, so we can do a single malloc at the
		 * end.
		 *
		 * Evaluate the expressions first, then get their
		 * lengthes, in case one of the expressions has a
		 * side effect that changes one of the others.
		 * See test/nasty.awk.
		 *
		 * dupnode the results a la do_print, to give us
		 * more predicable behavior; compare gawk 3.0.6 to
		 * nawk/mawk on test/nasty.awk.
		 */
		strp = strlist;
		supposed_len = len = 0;
		while (treep >= treelist) {
			NODE *n;

			/* Here lies the wumpus's brother. R.I.P. */
			n = force_string(tree_eval(*treep--));
			*strp = dupnode(n);
			free_temp(n);
			supposed_len += (*strp)->stlen;
			strp++;
		}
		*strp = NULL;

		str_count = strp - strlist;
		strp = strlist;
		for (i = 0; i < str_count; i++) {
			len += (*strp)->stlen;
			strp++;
		}
		if (do_lint && supposed_len != len)
			lintwarn(_("concatenation: side effects in one expression have changed the length of another!"));
		emalloc(str, char *, len+2, "tree_eval");
		str[len] = str[len+1] = '\0';	/* for good measure */
		dest = str;
		strp = strlist;
		while (*strp != NULL) {
			memcpy(dest, (*strp)->stptr, (*strp)->stlen);
			dest += (*strp)->stlen;
			unref(*strp);
			strp++;
		}
		r = make_str_node(str, len, ALREADY_MALLOCED);
		r->flags |= TEMP;

		free(strlist);
		free(treelist);
		}
		return r;

	/* assignments */
	case Node_assign:
		{
		Func_ptr after_assign = NULL;

		if (do_lint && iscond)
			lintwarn(_("assignment used in conditional context"));
		r = tree_eval(tree->rnode);
		lhs = get_lhs(tree->lnode, &after_assign, FALSE);

		assign_val(lhs, r);
		if (after_assign)
			(*after_assign)();
		return *lhs;
		}

	/* other assignment types are easier because they are numeric */
	case Node_preincrement:
	case Node_predecrement:
	case Node_postincrement:
	case Node_postdecrement:
	case Node_assign_exp:
	case Node_assign_times:
	case Node_assign_quotient:
	case Node_assign_mod:
	case Node_assign_plus:
	case Node_assign_minus:
		return op_assign(tree);
	default:
		break;	/* handled below */
	}

	/*
	 * Evaluate subtrees in order to do binary operation, then keep going.
	 * Use dupnode to make sure that these values don't disappear out
	 * from under us during recursive subexpression evaluation.
	 */
	t1 = dupnode(tree_eval(tree->lnode));
	t2 = dupnode(tree_eval(tree->rnode));

	switch (tree->type) {
	case Node_geq:
	case Node_leq:
	case Node_greater:
	case Node_less:
	case Node_notequal:
	case Node_equal:
		di = cmp_nodes(t1, t2);
		unref(t1);
		unref(t2);
		switch (tree->type) {
		case Node_equal:
			return tmp_number((AWKNUM) (di == 0));
		case Node_notequal:
			return tmp_number((AWKNUM) (di != 0));
		case Node_less:
			return tmp_number((AWKNUM) (di < 0));
		case Node_greater:
			return tmp_number((AWKNUM) (di > 0));
		case Node_leq:
			return tmp_number((AWKNUM) (di <= 0));
		case Node_geq:
			return tmp_number((AWKNUM) (di >= 0));
		default:
			cant_happen();
		}
		break;
	default:
		break;	/* handled below */
	}

	x1 = force_number(t1);
	x2 = force_number(t2);
	unref(t1);
	unref(t2);
	switch (tree->type) {
	case Node_exp:
		if ((lx = x2) == x2 && lx >= 0) {	/* integer exponent */
			if (lx == 0)
				x = 1;
			else if (lx == 1)
				x = x1;
			else {
				/* doing it this way should be more precise */
				for (x = x1; --lx; )
					x *= x1;
			}
		} else
			x = pow((double) x1, (double) x2);
		return tmp_number(x);

	case Node_times:
		return tmp_number(x1 * x2);

	case Node_quotient:
		if (x2 == 0)
			fatal(_("division by zero attempted"));
#ifdef _CRAY
		/* special case for integer division, put in for Cray */
		lx2 = x2;
		if (lx2 == 0)
			return tmp_number(x1 / x2);
		lx = (long) x1 / lx2;
		if (lx * x2 == x1)
			return tmp_number((AWKNUM) lx);
		else
#endif
			return tmp_number(x1 / x2);

	case Node_mod:
		if (x2 == 0)
			fatal(_("division by zero attempted in `%%'"));
#ifdef HAVE_FMOD
		return tmp_number(fmod(x1, x2));
#else	/* ! HAVE_FMOD */
		(void) modf(x1 / x2, &x);
		return tmp_number(x1 - x * x2);
#endif	/* ! HAVE_FMOD */

	case Node_plus:
		return tmp_number(x1 + x2);

	case Node_minus:
		return tmp_number(x1 - x2);

	default:
		fatal(_("illegal type (%s) in tree_eval"), nodetype2str(tree->type));
	}
	return 0;
}

/* eval_condition --- is TREE true or false? Returns 0==false, non-zero==true */

static int
eval_condition(register NODE *tree)
{
	register NODE *t1;
	register int ret;

	if (tree == NULL)	/* Null trees are the easiest kinds */
		return TRUE;
	if (tree->type == Node_line_range) {
		/*
		 * Node_line_range is kind of like Node_match, EXCEPT: the
		 * lnode field (more properly, the condpair field) is a node
		 * of a Node_cond_pair; whether we evaluate the lnode of that
		 * node or the rnode depends on the triggered word.  More
		 * precisely:  if we are not yet triggered, we tree_eval the
		 * lnode; if that returns true, we set the triggered word. 
		 * If we are triggered (not ELSE IF, note), we tree_eval the
		 * rnode, clear triggered if it succeeds, and perform our
		 * action (regardless of success or failure).  We want to be
		 * able to begin and end on a single input record, so this
		 * isn't an ELSE IF, as noted above.
		 */
		if (! tree->triggered) {
			if (! eval_condition(tree->condpair->lnode))
				return FALSE;
			else
				tree->triggered = TRUE;
		}
		/* Else we are triggered */
		if (eval_condition(tree->condpair->rnode))
			tree->triggered = FALSE;
		return TRUE;
	}

	/*
	 * Could just be J.random expression. in which case, null and 0 are
	 * false, anything else is true 
	 */

	t1 = m_tree_eval(tree, TRUE);
	if (t1->flags & MAYBE_NUM)
		(void) force_number(t1);
	if (t1->flags & NUMBER)
		ret = (t1->numbr != 0.0);
	else
		ret = (t1->stlen != 0);
	free_temp(t1);
	return ret;
}

/* cmp_nodes --- compare two nodes, returning negative, 0, positive */

int
cmp_nodes(register NODE *t1, register NODE *t2)
{
	register int ret;
	register size_t len1, len2;
	register int l;
	int ldiff;

	if (t1 == t2)
		return 0;
	if (t1->flags & MAYBE_NUM)
		(void) force_number(t1);
	if (t2->flags & MAYBE_NUM)
		(void) force_number(t2);
	if ((t1->flags & NUMBER) && (t2->flags & NUMBER)) {
		if (t1->numbr == t2->numbr)
			return 0;
		/* don't subtract, in case one or both are infinite */
		else if (t1->numbr < t2->numbr)
			return -1;
		else
			return 1;
	}
	(void) force_string(t1);
	(void) force_string(t2);
	len1 = t1->stlen;
	len2 = t2->stlen;
	ldiff = len1 - len2;
	if (len1 == 0 || len2 == 0)
		return ldiff;
	l = (ldiff <= 0 ? len1 : len2);
	if (IGNORECASE) {
		const unsigned char *cp1 = (const unsigned char *) t1->stptr;
		const unsigned char *cp2 = (const unsigned char *) t2->stptr;

#ifdef MBS_SUPPORT
		if (gawk_mb_cur_max > 1) {
			mbstate_t mbs;
			memset(&mbs, 0, sizeof(mbstate_t));
			ret = strncasecmpmbs((const char *) cp1, mbs,
					     (const char *) cp2, mbs, l);
		} else
#endif
		for (ret = 0; l-- > 0 && ret == 0; cp1++, cp2++)
			ret = casetable[*cp1] - casetable[*cp2];
	} else
		ret = memcmp(t1->stptr, t2->stptr, l);
	return (ret == 0 ? ldiff : ret);
}

/* op_assign --- do +=, -=, etc. */

static NODE *
op_assign(register NODE *tree)
{
	AWKNUM rval, lval;
	NODE **lhs;
	AWKNUM t1, t2;
	long ltemp;
	NODE *tmp;
	Func_ptr after_assign = NULL;
	int post = FALSE;

	/*
	 * For += etc, do the rhs first, since it can rearrange things,
	 * and *then* get the lhs.
	 */
	if (tree->rnode != NULL) {
		tmp = tree_eval(tree->rnode);
		rval = force_number(tmp);
		free_temp(tmp);
	} else
		rval = (AWKNUM) 1.0;

	lhs = get_lhs(tree->lnode, &after_assign, TRUE);
	lval = force_number(*lhs);
	unref(*lhs);

	switch(tree->type) {
	case Node_postincrement:
		post = TRUE;
		/* fall through */
	case Node_preincrement:
	case Node_assign_plus:
		*lhs = make_number(lval + rval);
		break;

	case Node_postdecrement:
		post = TRUE;
		/* fall through */
	case Node_predecrement:
	case Node_assign_minus:
		*lhs = make_number(lval - rval);
		break;

	case Node_assign_exp:
		if ((ltemp = rval) == rval) {	/* integer exponent */
			if (ltemp == 0)
				*lhs = make_number((AWKNUM) 1);
			else if (ltemp == 1)
				*lhs = make_number(lval);
			else {
				/* doing it this way should be more precise */
				for (t1 = t2 = lval; --ltemp; )
					t1 *= t2;
				*lhs = make_number(t1);
			}
		} else
			*lhs = make_number((AWKNUM) pow((double) lval, (double) rval));
		break;

	case Node_assign_times:
		*lhs = make_number(lval * rval);
		break;

	case Node_assign_quotient:
		if (rval == (AWKNUM) 0)
			fatal(_("division by zero attempted in `/='"));
#ifdef _CRAY
		/* special case for integer division, put in for Cray */
		ltemp = rval;
		if (ltemp == 0) {
			*lhs = make_number(lval / rval);
			break;
		}
		ltemp = (long) lval / ltemp;
		if (ltemp * lval == rval)
			*lhs = make_number((AWKNUM) ltemp);
		else
#endif	/* _CRAY */
			*lhs = make_number(lval / rval);
		break;

	case Node_assign_mod:
		if (rval == (AWKNUM) 0)
			fatal(_("division by zero attempted in `%%='"));
#ifdef HAVE_FMOD
		*lhs = make_number(fmod(lval, rval));
#else	/* ! HAVE_FMOD */
		(void) modf(lval / rval, &t1);
		t2 = lval - rval * t1;
		*lhs = make_number(t2);
#endif	/* ! HAVE_FMOD */
		break;

	default:
		cant_happen();
	}

	if (after_assign)
		(*after_assign)();

	/* for postincrement or postdecrement, return the old value */
	return (post ? tmp_number(lval) : *lhs);
}

/*
 * Avoiding memory leaks is difficult.  In paticular, any of `next',
 * `nextfile', `break' or `continue' (when not in a loop), can longjmp
 * out to the outermost level.  This leaks memory if it happens in a
 * called function. It also leaks memory if it happens in a
 * `for (iggy in foo)' loop, since such loops malloc an array of the
 * current array indices to loop over, which provides stability.
 *
 * The following code takes care of these problems.  First comes the
 * array-loop management code.  This can be a stack of arrays being looped
 * on at any one time.  This stack serves for both mainline code and
 * function body code. As each loop starts and finishes, it pushes its
 * info onto this stack and off of it; whether the loop is in a function
 * body or not isn't relevant.
 *
 * Since the list of indices is created using dupnode(), when popping
 * this stack it should be safe to unref() things, and then memory
 * will get finally released when the function call stack is popped.
 * This means that the loop_stack should be popped first upon a `next'.
 */

static struct loop_info {
	const char *varname;	/* variable name, for debugging */
	NODE **elems;		/* list of indices */
	size_t nelems;		/* how many there are */
} *loop_stack = NULL;
size_t nloops = 0;		/* how many slots there are in the stack */
size_t nloops_active = 0;	/* how many loops are actively stacked */

/* pop_forloop --- pop one for loop off the stack */

static void
pop_forloop()
{
	int i, curloop;
	struct loop_info *loop;

	assert(nloops_active > 0);

	curloop = --nloops_active;	/* 0-based indexing */
	loop = & loop_stack[curloop];

	for (i = 0; i < loop->nelems; i++)
		unref(loop->elems[i]);

	free(loop->elems);

	loop->elems = NULL;
	loop->varname = NULL;
	loop->nelems = 0;
}

/* pop_forloops --- pop the for loops stack all the way */

static inline void
pop_all_forloops()
{
	while (nloops_active > 0)
		pop_forloop();	/* decrements nloops_active for us */
}

/* push_forloop --- add a single for loop to the stack */

static void
push_forloop(const char *varname, NODE **elems, size_t nelems)
{
#define NLOOPS	4	/* seems like a good guess */
	if (loop_stack == NULL) {
		/* allocate stack, set vars */
		nloops = NLOOPS;
		emalloc(loop_stack, struct loop_info *, nloops * sizeof(struct loop_info),
				"push_forloop");
	} else if (nloops_active == nloops) {
		/* grow stack, set vars */
		nloops *= 2;
		erealloc(loop_stack, struct loop_info *, nloops * sizeof(struct loop_info),
				"push_forloop");
	}

	loop_stack[nloops_active].varname = varname;
	loop_stack[nloops_active].elems = elems;
	loop_stack[nloops_active].nelems = nelems;
	nloops_active++;
}

static struct fcall {
	const char *fname;	/* function name */
	unsigned long count;	/* how many args */
	NODE *arglist;		/* list thereof */
	NODE **prevstack;	/* function stack frame of previous function */
	NODE **stack;		/* function stack frame of current function */
} *fcall_list = NULL;

static long fcall_list_size = 0;
static long curfcall = -1;

/* pop_fcall --- pop off a single function call */

static void
pop_fcall()
{
	NODE *n, **sp;
	int count;
	struct fcall *f;

	assert(curfcall >= 0);
	f = & fcall_list[curfcall];
	stack_ptr = f->prevstack;

	sp = f->stack;

	for (count = f->count; count > 0; count--) {
		n = *sp++;
		if (n->type == Node_var)		/* local variable */
			unref(n->var_value);
		else if (n->type == Node_var_array)	/* local array */
			assoc_clear(n);
		freenode(n);
	}
	if (f->stack)
		free((char *) f->stack);
	/* memset(f, '\0', sizeof(struct fcall)); */
	curfcall--;
}

/* pop_fcall_stack --- pop off all function args, don't leak memory */

static inline void
pop_fcall_stack()
{
	while (curfcall >= 0)
		pop_fcall();
}

/* push_args --- push function arguments onto the stack */

static void
push_args(int count,
	NODE *argp,
	NODE **oldstack,
	const char *func_name,
	char **varnames)
{
	struct fcall *f;
	NODE *arg, *r, **sp;
	int i;

	if (fcall_list_size == 0) {	/* first time */
		emalloc(fcall_list, struct fcall *, 10 * sizeof(struct fcall),
			"push_args");
		fcall_list_size = 10;
	}

	if (++curfcall >= fcall_list_size) {
		fcall_list_size *= 2;
		erealloc(fcall_list, struct fcall *,
			fcall_list_size * sizeof(struct fcall), "push_args");
	}
	f = & fcall_list[curfcall];

	if (count > 0)
		emalloc(f->stack, NODE **, count*sizeof(NODE *), "push_args");
	else
		f->stack = NULL;
	f->count = count;
	f->fname = func_name;	/* not used, for debugging, just in case */
	f->arglist = argp;
	f->prevstack = oldstack;

	sp = f->stack;

	/* for each calling arg. add NODE * on stack */
	for (i = 0; i < count; i++) {
		getnode(r);
		*sp++ = r;
		if (argp == NULL) {
			/* local variable */
			r->type = Node_var_new;
			r->vname = varnames[i];
			continue;
		}
		arg = argp->lnode;
		/* call by reference for arrays; see below also */
		if (arg->type == Node_param_list) {
			/* we must also reassign f here; see below */
			f = & fcall_list[curfcall];
			arg = f->prevstack[arg->param_cnt];
		}
		if (arg->type == Node_var_array || arg->type == Node_var_new) {
			r->type = Node_array_ref;
			r->orig_array = arg;
			r->prev_array = arg;
		} else if (arg->type == Node_array_ref) {
  			*r = *arg;
			r->prev_array = arg;
		} else {
			NODE *n = tree_eval(arg);

			r->type = Node_var;
			r->lnode = dupnode(n);
			r->rnode = (NODE *) NULL;
			free_temp(n);
  		}
		r->vname = varnames[i];
		argp = argp->rnode;
	}

	/*
	 * We have to reassign f.  Why, you may ask?  It is possible that
	 * other functions were called during the course of tree_eval()-ing
	 * the arguments to this function.  As a result of that, fcall_list
	 * may have been realloc()'ed, with the result that f is now
	 * pointing into free()'d space.  This was a nasty one to track down.
	 */
	f = & fcall_list[curfcall];

	if (argp != NULL) {
		/* Left over calling args. */
		warning(
		    _("function `%s' called with more arguments than declared"),
		    func_name);
		/* Evaluate them, they may have side effects: */
		do {
			arg = argp->lnode;
			if (arg->type == Node_param_list)
				arg = f->prevstack[arg->param_cnt];
			if (arg->type != Node_var_array &&
			    arg->type != Node_array_ref &&
			    arg->type != Node_var_new)
				free_temp(tree_eval(arg));

			/* reassign f, tree_eval could have moved it */
			f = & fcall_list[curfcall];
		} while ((argp = argp->rnode) != NULL);
	}

	stack_ptr = f->stack;
}

/* func_call --- call a function, call by reference for arrays */

NODE **stack_ptr;

static NODE *
func_call(NODE *tree)
{
	register NODE *r;
	NODE *name, *arg_list;
	NODE *f;
	jmp_buf volatile func_tag_stack;
	jmp_buf volatile loop_tag_stack;
	int volatile save_loop_tag_valid = FALSE;
	NODE *save_ret_node;
	extern NODE *ret_node;

	/* tree->rnode is a Node_val giving function name */
	/* tree->lnode is Node_expression_list of calling args. */
	name = tree->rnode;
	arg_list = tree->lnode;

	/* retrieve function definition node */
	if (tree->funcbody != NULL)
		f = tree->funcbody;
	else {
		f = lookup(name->stptr);
		if (f == NULL || f->type != Node_func)
			fatal(_("function `%s' not defined"), name->stptr);

		tree->funcbody = f;	/* save for next call */
	}

#ifdef FUNC_TRACE
	fprintf(stderr, _("function %s called\n"), name->stptr);
#endif
	push_args(f->lnode->param_cnt, arg_list, stack_ptr, name->stptr,
			f->parmlist);

	/*
	 * Execute function body, saving context, as a return statement
	 * will longjmp back here.
	 *
	 * Have to save and restore the loop_tag stuff so that a return
	 * inside a loop in a function body doesn't scrog any loops going
	 * on in the main program.  We save the necessary info in variables
	 * local to this function so that function nesting works OK.
	 * We also only bother to save the loop stuff if we're in a loop
	 * when the function is called.
	 */
	if (loop_tag_valid) {
		int junk = 0;

		save_loop_tag_valid = (volatile int) loop_tag_valid;
		PUSH_BINDING(loop_tag_stack, loop_tag, junk);
		loop_tag_valid = FALSE;
	}
	PUSH_BINDING(func_tag_stack, func_tag, func_tag_valid);
	save_ret_node = ret_node;
	ret_node = Nnull_string;	/* default return value */
	INCREMENT(f->exec_count);	/* count function calls */
	if (setjmp(func_tag) == 0)
		(void) interpret(f->rnode);

	r = ret_node;
	ret_node = (NODE *) save_ret_node;
	RESTORE_BINDING(func_tag_stack, func_tag, func_tag_valid);
	pop_fcall();

	/* Restore the loop_tag stuff if necessary. */
	if (save_loop_tag_valid) {
		int junk = 0;

		loop_tag_valid = (int) save_loop_tag_valid;
		RESTORE_BINDING(loop_tag_stack, loop_tag, junk);
	}

	if ((r->flags & PERM) == 0)
		r->flags |= TEMP;
	return r;
}

#ifdef PROFILING
/* dump_fcall_stack --- print a backtrace of the awk function calls */

void
dump_fcall_stack(FILE *fp)
{
	int i;

	if (curfcall < 0)
		return;

	fprintf(fp, _("\n\t# Function Call Stack:\n\n"));
	for (i = curfcall; i >= 0; i--)
		fprintf(fp, "\t# %3d. %s\n", i+1, fcall_list[i].fname);
	fprintf(fp, _("\t# -- main --\n"));
}
#endif /* PROFILING */

/*
 * r_get_lhs:
 * This returns a POINTER to a node pointer. get_lhs(ptr) is the current
 * value of the var, or where to store the var's new value 
 *
 * For the special variables, don't unref their current value if it's
 * the same as the internal copy; perhaps the current one is used in
 * a concatenation or some other expression somewhere higher up in the
 * call chain.  Ouch.
 */

NODE **
r_get_lhs(register NODE *ptr, Func_ptr *assign, int reference)
{
	register NODE **aptr = NULL;
	register NODE *n;

	if (assign)
		*assign = NULL;	/* for safety */
	if (ptr->type == Node_param_list) {
		if ((ptr->flags & FUNC) != 0)
			fatal(_("can't use function name `%s' as variable or array"), ptr->vname);
		ptr = stack_ptr[ptr->param_cnt];
	}

	switch (ptr->type) {
	case Node_var_array:
		fatal(_("attempt to use array `%s' in a scalar context"),
			array_vname(ptr));

	/*
	 * The following goop ensures that uninitialized variables
	 * used as parameters eventually get their type set correctly
	 * to scalar (i.e., Node_var).
	 */
	case Node_array_ref:
		if (ptr->orig_array->type == Node_var_array)
			fatal(_("attempt to use array `%s' in a scalar context"),
				array_vname(ptr));
		ptr->orig_array->type = Node_var;
		/* fall through */
	case Node_var_new:
		ptr->type = Node_var;
		ptr->var_value = Nnull_string;
		/* fall through */
	case Node_var:
		if (do_lint && reference && var_uninitialized(ptr))
			lintwarn(_("reference to uninitialized variable `%s'"),
					      ptr->vname);

		aptr = &(ptr->var_value);
#ifdef GAWKDEBUG
		if (ptr->var_value->stref <= 0)
			cant_happen();
#endif
		break;

	case Node_FIELDWIDTHS:
		aptr = &(FIELDWIDTHS_node->var_value);
		if (assign != NULL)
			*assign = set_FIELDWIDTHS;
		break;

	case Node_RS:
		aptr = &(RS_node->var_value);
		if (assign != NULL)
			*assign = set_RS;
		break;

	case Node_FS:
		aptr = &(FS_node->var_value);
		if (assign != NULL)
			*assign = set_FS;
		break;

	case Node_FNR:
		if (FNR_node->var_value->numbr != FNR) {
			unref(FNR_node->var_value);
			FNR_node->var_value = make_number((AWKNUM) FNR);
		}
		aptr = &(FNR_node->var_value);
		if (assign != NULL)
			*assign = set_FNR;
		break;

	case Node_NR:
		if (NR_node->var_value->numbr != NR) {
			unref(NR_node->var_value);
			NR_node->var_value = make_number((AWKNUM) NR);
		}
		aptr = &(NR_node->var_value);
		if (assign != NULL)
			*assign = set_NR;
		break;

	case Node_NF:
		if (NF == -1 || NF_node->var_value->numbr != NF) {
			if (NF == -1)
				(void) get_field(HUGE-1, assign); /* parse record */
			unref(NF_node->var_value);
			NF_node->var_value = make_number((AWKNUM) NF);
		}
		aptr = &(NF_node->var_value);
		if (assign != NULL)
			*assign = set_NF;
		break;

	case Node_IGNORECASE:
		aptr = &(IGNORECASE_node->var_value);
		if (assign != NULL)
			*assign = set_IGNORECASE;
		break;

	case Node_BINMODE:
		aptr = &(BINMODE_node->var_value);
		if (assign != NULL)
			*assign = set_BINMODE;
		break;

	case Node_LINT:
		aptr = &(LINT_node->var_value);
		if (assign != NULL)
			*assign = set_LINT;
		break;

	case Node_OFMT:
		aptr = &(OFMT_node->var_value);
		if (assign != NULL)
			*assign = set_OFMT;
		break;

	case Node_CONVFMT:
		aptr = &(CONVFMT_node->var_value);
		if (assign != NULL)
			*assign = set_CONVFMT;
		break;

	case Node_ORS:
		aptr = &(ORS_node->var_value);
		if (assign != NULL)
			*assign = set_ORS;
		break;

	case Node_OFS:
		aptr = &(OFS_node->var_value);
		if (assign != NULL)
			*assign = set_OFS;
		break;

	case Node_TEXTDOMAIN:
		aptr = &(TEXTDOMAIN_node->var_value);
		if (assign != NULL)
			*assign = set_TEXTDOMAIN;
		break;

	case Node_field_spec:
		{
		int field_num;

		n = tree_eval(ptr->lnode);
		if (do_lint) {
			if ((n->flags & NUMBER) == 0) {
				lintwarn(_("attempt to field reference from non-numeric value"));
				if (n->stlen == 0)
					lintwarn(_("attempt to reference from null string"));
			}
		}
		field_num = (int) force_number(n);
		free_temp(n);
		if (field_num < 0)
			fatal(_("attempt to access field %d"), field_num);
		if (field_num == 0 && field0_valid) {	/* short circuit */
			aptr = &fields_arr[0];
			if (assign != NULL)
				*assign = reset_record;
		} else
			aptr = get_field(field_num, assign);
		if (do_lint && reference && (*aptr == Null_field || *aptr == Nnull_string))
			lintwarn(_("reference to uninitialized field `$%d'"),
					      field_num);
		break;
		}

	case Node_subscript:
		n = get_array(ptr->lnode);
		aptr = assoc_lookup(n, concat_exp(ptr->rnode), reference);
		break;

	case Node_builtin:
#if 1
		/* in gawk for a while */
		fatal(_("assignment is not allowed to result of builtin function"));
#else
		/*
		 * This is how Christos at Deshaw did it.
		 * Does this buy us anything?
		 */
		if (ptr->builtin == NULL)
			fatal(_("assignment is not allowed to result of builtin function"));
		ptr->callresult = (*ptr->builtin)(ptr->subnode);
		aptr = &ptr->callresult;
		break;
#endif

	default:
		fprintf(stderr, "type = %s\n", nodetype2str(ptr->type));
		fflush(stderr);
		cant_happen();
	}
	return aptr;
}

/* match_op --- do ~ and !~ */

static NODE *
match_op(register NODE *tree)
{
	register NODE *t1;
	register Regexp *rp;
	int i;
	int match = TRUE;

	if (tree->type == Node_nomatch)
		match = FALSE;
	if (tree->type == Node_regex)
		t1 = *get_field(0, (Func_ptr *) 0);
	else {
		t1 = force_string(tree_eval(tree->lnode));
		tree = tree->rnode;
	}
	rp = re_update(tree);
	i = research(rp, t1->stptr, 0, t1->stlen, FALSE);
	i = (i == -1) ^ (match == TRUE);
	free_temp(t1);
	return tmp_number((AWKNUM) i);
}

/* set_IGNORECASE --- update IGNORECASE as appropriate */

void
set_IGNORECASE()
{
	static int warned = FALSE;

	if ((do_lint || do_traditional) && ! warned) {
		warned = TRUE;
		lintwarn(_("`IGNORECASE' is a gawk extension"));
	}
	if (do_traditional)
		IGNORECASE = FALSE;
	else if ((IGNORECASE_node->var_value->flags & (STRING|STRCUR)) != 0) {
		if ((IGNORECASE_node->var_value->flags & MAYBE_NUM) == 0)
			IGNORECASE = (force_string(IGNORECASE_node->var_value)->stlen > 0);
		else
			IGNORECASE = (force_number(IGNORECASE_node->var_value) != 0.0);
	} else if ((IGNORECASE_node->var_value->flags & (NUMCUR|NUMBER)) != 0)
		IGNORECASE = (force_number(IGNORECASE_node->var_value) != 0.0);
	else
		IGNORECASE = FALSE;		/* shouldn't happen */
                  
	set_RS();	/* set_RS() calls set_FS() if need be, for us */
}

/* set_BINMODE --- set translation mode (OS/2, DOS, others) */

void
set_BINMODE()
{
	static int warned = FALSE;
	char *p, *cp, save;
	NODE *v;
	int digits = FALSE;

	if ((do_lint || do_traditional) && ! warned) {
		warned = TRUE;
		lintwarn(_("`BINMODE' is a gawk extension"));
	}
	if (do_traditional)
		BINMODE = 0;
	else if ((BINMODE_node->var_value->flags & STRING) != 0) {
		v = BINMODE_node->var_value;
		p = v->stptr;
		save = p[v->stlen];
		p[v->stlen] = '\0';

		for (cp = p; *cp != '\0'; cp++) {
			if (ISDIGIT(*cp)) {
				digits = TRUE;
				break;
			}
		}

		if (! digits || (BINMODE_node->var_value->flags & MAYBE_NUM) == 0) {
			BINMODE = 0;
			if (strcmp(p, "r") == 0)
				BINMODE = 1;
			else if (strcmp(p, "w") == 0)
				BINMODE = 2;
			else if (strcmp(p, "rw") == 0 || strcmp(p, "wr") == 0)
				BINMODE = 3;

			if (BINMODE == 0 && v->stlen != 0) {
				/* arbitrary string, assume both */
				BINMODE = 3;
				warning("BINMODE: arbitary string value treated as \"rw\"");
			}
		} else
			BINMODE = (int) force_number(BINMODE_node->var_value);

		p[v->stlen] = save;
	} else if ((BINMODE_node->var_value->flags & NUMBER) != 0)
		BINMODE = (int) force_number(BINMODE_node->var_value);
	else
		BINMODE = 0;		/* shouldn't happen */
}

/* set_OFS --- update OFS related variables when OFS assigned to */

void
set_OFS()
{
	OFS = force_string(OFS_node->var_value)->stptr;
	OFSlen = OFS_node->var_value->stlen;
	OFS[OFSlen] = '\0';
}

/* set_ORS --- update ORS related variables when ORS assigned to */

void
set_ORS()
{
	ORS = force_string(ORS_node->var_value)->stptr;
	ORSlen = ORS_node->var_value->stlen;
	ORS[ORSlen] = '\0';
}

/* fmt_ok --- is the conversion format a valid one? */

NODE **fmt_list = NULL;
static int fmt_ok P((NODE *n));
static int fmt_index P((NODE *n));

static int
fmt_ok(NODE *n)
{
	NODE *tmp = force_string(n);
	const char *p = tmp->stptr;

	if (*p++ != '%')
		return 0;
	while (*p && strchr(" +-#", *p) != NULL)	/* flags */
		p++;
	while (*p && ISDIGIT(*p))	/* width - %*.*g is NOT allowed */
		p++;
	if (*p == '\0' || (*p != '.' && ! ISDIGIT(*p)))
		return 0;
	if (*p == '.')
		p++;
	while (*p && ISDIGIT(*p))	/* precision */
		p++;
	if (*p == '\0' || strchr("efgEG", *p) == NULL)
		return 0;
	if (*++p != '\0')
		return 0;
	return 1;
}

/* fmt_index --- track values of OFMT and CONVFMT to keep semantics correct */

static int
fmt_index(NODE *n)
{
	register int ix = 0;
	static int fmt_num = 4;
	static int fmt_hiwater = 0;

	if (fmt_list == NULL)
		emalloc(fmt_list, NODE **, fmt_num*sizeof(*fmt_list), "fmt_index");
	(void) force_string(n);
	while (ix < fmt_hiwater) {
		if (cmp_nodes(fmt_list[ix], n) == 0)
			return ix;
		ix++;
	}
	/* not found */
	n->stptr[n->stlen] = '\0';
	if (do_lint && ! fmt_ok(n))
		lintwarn(_("bad `%sFMT' specification `%s'"),
			    n == CONVFMT_node->var_value ? "CONV"
			  : n == OFMT_node->var_value ? "O"
			  : "", n->stptr);

	if (fmt_hiwater >= fmt_num) {
		fmt_num *= 2;
		erealloc(fmt_list, NODE **, fmt_num * sizeof(*fmt_list), "fmt_index");
	}
	fmt_list[fmt_hiwater] = dupnode(n);
	return fmt_hiwater++;
}

/* set_OFMT --- track OFMT correctly */

void
set_OFMT()
{
	OFMTidx = fmt_index(OFMT_node->var_value);
	OFMT = fmt_list[OFMTidx]->stptr;
}

/* set_CONVFMT --- track CONVFMT correctly */

void
set_CONVFMT()
{
	CONVFMTidx = fmt_index(CONVFMT_node->var_value);
	CONVFMT = fmt_list[CONVFMTidx]->stptr;
}

/* set_LINT --- update LINT as appropriate */

void
set_LINT()
{
#ifndef NO_LINT
	int old_lint = do_lint;

	if ((LINT_node->var_value->flags & (STRING|STRCUR)) != 0) {
		if ((LINT_node->var_value->flags & MAYBE_NUM) == 0) {
			const char *lintval;
			size_t lintlen;

			do_lint = (force_string(LINT_node->var_value)->stlen > 0);
			lintval = LINT_node->var_value->stptr;
			lintlen = LINT_node->var_value->stlen;
			if (do_lint) {
				do_lint = LINT_ALL;
				if (lintlen == 5 && strncmp(lintval, "fatal", 5) == 0)
					lintfunc = r_fatal;
				else if (lintlen == 7 && strncmp(lintval, "invalid", 7) == 0)
					do_lint = LINT_INVALID;
				else
					lintfunc = warning;
			} else
				lintfunc = warning;
		} else {
			if (force_number(LINT_node->var_value) != 0.0)
				do_lint = LINT_ALL;
			else
				do_lint = FALSE;
			lintfunc = warning;
		}
	} else if ((LINT_node->var_value->flags & (NUMCUR|NUMBER)) != 0) {
		if (force_number(LINT_node->var_value) != 0.0)
			do_lint = LINT_ALL;
		else
			do_lint = FALSE;
		lintfunc = warning;
	} else
		do_lint = FALSE;		/* shouldn't happen */

	if (! do_lint)
		lintfunc = warning;

	/* explicitly use warning() here, in case lintfunc == r_fatal */
	if (old_lint != do_lint && old_lint && do_lint == FALSE)
		warning(_("turning off `--lint' due to assignment to `LINT'"));
#endif /* ! NO_LINT */
}

/* set_TEXTDOMAIN --- update TEXTDOMAIN variable when TEXTDOMAIN assigned to */

void
set_TEXTDOMAIN()
{
	int len;

	TEXTDOMAIN = force_string(TEXTDOMAIN_node->var_value)->stptr;
	len = TEXTDOMAIN_node->var_value->stlen;
	TEXTDOMAIN[len] = '\0';
	/*
	 * Note: don't call textdomain(); this value is for
	 * the awk program, not for gawk itself.
	 */
}

/*
 * assign_val --- do mechanics of assignment, for calling from multiple
 *		  places.
 */

NODE *
assign_val(NODE **lhs_p, NODE *rhs)
{
	if (rhs != *lhs_p) {
		/*
		 * Since we know that the nodes are different,
		 * we can do the unref() before the dupnode().
		 */
		unref(*lhs_p);
		*lhs_p = dupnode(rhs);
	}
	return *lhs_p;
}

/* update_ERRNO --- update the value of ERRNO */

void
update_ERRNO()
{
	char *cp;

	cp = strerror(errno);
	cp = gettext(cp);
	unref(ERRNO_node->var_value);
	ERRNO_node->var_value = make_string(cp, strlen(cp));
}

/* comp_func --- array index comparison function for qsort */

static int
comp_func(const void *p1, const void *p2)
{
	size_t len1, len2;
	const char *str1, *str2;
	const NODE *t1, *t2;
	int cmp1;

	t1 = *((const NODE *const *) p1);
	t2 = *((const NODE *const *) p2);

/*
	t1 = force_string(t1);
	t2 = force_string(t2);
*/
	len1 = t1->ahname_len;
	str1 = t1->ahname_str;

	len2 = t2->ahname_len;
	str2 = t2->ahname_str;

	/* Array indexes are strings, compare as such, always! */
	cmp1 = memcmp(str1, str2, len1 < len2 ? len1 : len2);
	/* if prefixes are equal, size matters */
	return (cmp1 != 0 ? cmp1 :
		len1 < len2 ? -1 : (len1 > len2));
}
