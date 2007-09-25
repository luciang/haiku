/*
*   $Id: python.c 567 2007-06-24 01:20:20Z elliotth $
*
*   Copyright (c) 2000-2003, Darren Hiebert
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License.
*
*   This module contains functions for generating tags for Python language
*   files.
*/
/*
*   INCLUDE FILES
*/
#include "general.h"  /* must always come first */

#include <string.h>

#include "entry.h"
#include "options.h"
#include "read.h"
#include "routines.h"
#include "vstring.h"

/*
*   DATA DEFINITIONS
*/
typedef enum {
	K_CLASS, K_FUNCTION, K_MEMBER
} pythonKind;

static kindOption PythonKinds[] = {
	{TRUE, 'c', "class",    "classes"},
	{TRUE, 'f', "function", "functions"},
	{TRUE, 'm', "member",   "class members"}
};

typedef struct NestingLevel NestingLevel;
typedef struct NestingLevels NestingLevels;

struct NestingLevel
{
	int indentation;
	vString *name;
	boolean is_class;
};

struct NestingLevels
{
	NestingLevel *levels;
	int n;
	int allocated;
};

/*
*   FUNCTION DEFINITIONS
*/

#define vStringLast(vs) ((vs)->buffer[(vs)->length - 1])

static boolean isIdentifierFirstCharacter (int c)
{
	return (boolean) (isalpha (c) || c == '_');
}

static boolean isIdentifierCharacter (int c)
{
	return (boolean) (isalnum (c) || c == '_');
}

/* Given a string with the contents of a line directly after the "def" keyword,
 * extract all relevant information and create a tag.
 */
static void makeFunctionTag (vString *const function,
	vString *const parent, int is_class_parent)
{
	tagEntryInfo tag;
	initTagEntry (&tag, vStringValue (function));
	
	tag.kindName = "function";
	tag.kind = 'f';

	if (vStringLength (parent) > 0)
	{
		if (is_class_parent)
		{
			tag.kindName = "member";
			tag.kind = 'm';
			tag.extensionFields.scope [0] = "class";
			tag.extensionFields.scope [1] = vStringValue (parent);
		}
		else
		{
			tag.extensionFields.scope [0] = "function";
			tag.extensionFields.scope [1] = vStringValue (parent);
		}
	}

	/* If a function starts with __, we mark it as file scope.
	 * FIXME: What is the proper way to signal such attributes?
	 * TODO: What does functions/classes starting with _ and __ mean in python?
	 */
	if (strncmp (vStringValue (function), "__", 2) == 0 &&
		strcmp (vStringValue (function), "__init__") != 0)
	{
		tag.extensionFields.access = "private";
		tag.isFileScope = TRUE;
	}
	else
	{
		tag.extensionFields.access = "public";
	}
	makeTagEntry (&tag);
}

/* Given a string with the contents of the line directly after the "class"
 * keyword, extract all necessary information and create a tag.
 */
static void makeClassTag (vString *const class, vString *const inheritance,
	vString *const parent, int is_class_parent)
{
	tagEntryInfo tag;
	initTagEntry (&tag, vStringValue (class));
	tag.kindName = "class";
	tag.kind = 'c';
	if (vStringLength (parent) > 0)
	{
		if (is_class_parent)
		{
			tag.extensionFields.scope [0] = "class";
			tag.extensionFields.scope [1] = vStringValue (parent);
		}
		else
		{
			tag.extensionFields.scope [0] = "function";
			tag.extensionFields.scope [1] = vStringValue (parent);
		}
	}
	tag.extensionFields.inheritance = vStringValue (inheritance);
	makeTagEntry (&tag);
}

/* Skip a single or double quoted string. */
static const char *skipString (const char *cp)
{
	const char *start = cp;
	int escaped = 0;
	for (cp++; *cp; cp++)
	{
		if (escaped)
			escaped--;
		else if (*cp == '\\')
			escaped++;
		else if (*cp == *start)
			return cp + 1;
	}
	return cp;
}

/* Skip everything up to an identifier start. */
static const char *skipEverything (const char *cp)
{
	for (; *cp; cp++)
	{
		if (isIdentifierFirstCharacter ((int) *cp))
			return cp;
		if (*cp == '"' || *cp == '\'')
		{
			cp = skipString(cp);
		}
    }
    return cp;
}

/* Skip an identifier. */
static const char *skipIdentifier (const char *cp)
{
	while (isIdentifierCharacter ((int) *cp))
		cp++;
    return cp;
}

static const char *findDefinitionOrClass (const char *cp)
{
	while (*cp)
	{
		cp = skipEverything (cp);
		if (!strncmp(cp, "def", 3) || !strncmp(cp, "class", 5))
		{
			return cp;
		}
		cp = skipIdentifier (cp);
	}
	return NULL;
}

static const char *skipSpace (const char *cp)
{
	while (isspace ((int) *cp))
		++cp;
	return cp;
}

/* Starting at ''cp'', parse an identifier into ''identifier''. */
static const char *parseIdentifier (const char *cp, vString *const identifier)
{
	vStringClear (identifier);
	while (isIdentifierCharacter ((int) *cp))
	{
		vStringPut (identifier, (int) *cp);
		++cp;
	}
	vStringTerminate (identifier);
	return cp;
}

static void parseClass (const char *cp, vString *const class,
	vString *const parent, int is_class_parent)
{
	vString *const inheritance = vStringNew ();
	vStringClear (inheritance);
	cp = parseIdentifier (cp, class);
	cp = skipSpace (cp);
	if (*cp == '(')
	{
		++cp;
		while (*cp != ')')
		{
			if (*cp == '\0')
			{
				/* Closing parenthesis can be in follow up line. */
				cp = (const char *) fileReadLine ();
				if (!cp) break;
				vStringPut (inheritance, ' ');
				continue;
			}
			vStringPut (inheritance, *cp);
			++cp;
		}
		vStringTerminate (inheritance);
	}
	makeClassTag (class, inheritance, parent, is_class_parent);
	vStringDelete (inheritance);
}

static void parseFunction (const char *cp, vString *const def,
	vString *const parent, int is_class_parent)
{
	cp = parseIdentifier (cp, def);
	makeFunctionTag (def, parent, is_class_parent);
}

/* Get the combined name of a nested symbol. Classes are separated with ".",
 * functions with "/". For example this code:
 * class MyClass:
 *     def myFunction:
 *         def SubFunction:
 *             class SubClass:
 *                 def Method:
 *                     pass
 * Would produce this string:
 * MyClass.MyFunction/SubFunction/SubClass.Method
 */
static boolean constructParentString(NestingLevels *nls, int indent,
	vString *result)
{
	int i;
	NestingLevel *prev = NULL;
	int is_class = FALSE;
	vStringClear (result);
	for (i = 0; i < nls->n; i++)
	{
		NestingLevel *nl = nls->levels + i;
		if (indent <= nl->indentation)
			break;
		if (prev)
		{
			if (prev->is_class)
				vStringCatS(result, ".");
			else
				vStringCatS(result, "/");
		}
		vStringCat(result, nl->name);
		is_class = nl->is_class;
		prev = nl;
	}
	return is_class;
}

static NestingLevels *newNestingLevels(void)
{
	NestingLevels *nls = xCalloc (1, NestingLevels);
	return nls;
}

static void freeNestingLevels(NestingLevels *nls)
{
	int i;
	for (i = 0; i < nls->allocated; i++)
		vStringDelete(nls->levels[i].name);
	if (nls->levels) eFree(nls->levels);
	eFree(nls);
}

/* TODO: This is totally out of place in python.c, but strlist.h is not usable.
 * Maybe should just move these three functions to a separate file, even if no
 * other parser uses them.
 */
static void addNestingLevel(NestingLevels *nls, int indentation,
	vString *name, boolean is_class)
{
	int i;
	NestingLevel *nl = NULL;

	for (i = 0; i < nls->n; i++)
	{
		nl = nls->levels + i;
		if (indentation <= nl->indentation) break;
	}
	if (i == nls->n)
	{
		if (i >= nls->allocated)
		{
			nls->allocated++;
			nls->levels = xRealloc(nls->levels,
				nls->allocated, NestingLevel);
			nls->levels[i].name = vStringNew();
		}
		nl = nls->levels + i;
	}
	nls->n = i + 1;

	vStringCopy(nl->name, name);
	nl->indentation = indentation;
	nl->is_class = is_class;
}

static void findPythonTags (void)
{
	vString *const continuation = vStringNew ();
	vString *const name = vStringNew ();
	vString *const parent = vStringNew();

	NestingLevels *const nesting_levels = newNestingLevels();

	const char *line;
	int line_skip = 0;
	boolean longStringLiteral = FALSE;

	while ((line = (const char *) fileReadLine ()) != NULL)
	{
		const char *cp = line;
		char *longstring;
		const char *keyword;
		int indent;

		cp = skipSpace (cp);

		if (*cp == '#' || *cp == '\0')  /* skip comment or blank line */
			continue;

		/* Deal with line continuation. */
		if (!line_skip) vStringClear(continuation);
		vStringCatS(continuation, line);
		vStringStripTrailing(continuation);
		if (vStringLast(continuation) == '\\')
		{
			vStringChop(continuation);
			vStringCatS(continuation, " ");
			line_skip = 1;
			continue;
		}
		cp = line = vStringValue(continuation);
		cp = skipSpace (cp);
		indent = cp - line;
		line_skip = 0;

		/* Deal with multiline string ending. */
		if (longStringLiteral)
		{
			/* Note: We do ignore anything in the same line after a multiline
			 * string for now.
			 */
			if (strstr (cp, "\"\"\""))
				longStringLiteral = FALSE;
			continue;
		}
		
		/* Deal with multiline string start. */
		if ((longstring = strstr (cp, "\"\"\"")) != NULL)
		{
			/* Note: For our purposes, the line just ends at the first long
			 * string.
			 */
			*longstring = '\0';
			longstring += 3;
			longStringLiteral = TRUE;
			while ((longstring = strstr (longstring, "\"\"\"")) != NULL)
			{
				longstring += 3;
				longStringLiteral = !longStringLiteral;
			}
		}

		/* Deal with def and class keywords. */
		keyword = findDefinitionOrClass (cp);
		if (keyword)
		{
			boolean found = FALSE;
			boolean is_class = FALSE;
			if (!strncmp (keyword, "def", 3))
			{
				cp = skipSpace (keyword + 3);
				found = TRUE;
			}
			else if (!strncmp (keyword, "class", 5))
			{
				cp = skipSpace (keyword + 5);
				found = TRUE;
				is_class = TRUE;
			}

			if (found)
			{
				boolean is_parent_class;

				is_parent_class =
					constructParentString(nesting_levels, indent, parent);

				if (is_class)
					parseClass (cp, name, parent, is_parent_class);
				else
					parseFunction(cp, name, parent, is_parent_class);

				addNestingLevel(nesting_levels, indent, name, is_class);
			}
		}
	}
	/* Clean up all memory we allocated. */
	vStringDelete (parent);
	vStringDelete (name);
	vStringDelete (continuation);
	freeNestingLevels (nesting_levels);
}

extern parserDefinition *PythonParser (void)
{
	static const char *const extensions[] = { "py", "pyx", "pxd", "scons", NULL };
	parserDefinition *def = parserNew ("Python");
	def->kinds = PythonKinds;
	def->kindCount = KIND_COUNT (PythonKinds);
	def->extensions = extensions;
	def->parser = findPythonTags;
	return def;
}

/* vi:set tabstop=4 shiftwidth=4: */
