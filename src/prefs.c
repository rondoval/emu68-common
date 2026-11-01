// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * prefs.c — one line and one value of an ENV:*.prefs file, as strings. Pure
 * string manipulation; no file-scope state (ROM-safe). prefs_file.c reads the
 * file.
 */

#include <prefs.h>
#include <strutil.h>

BOOL prefs_split(char *line, char **key, char **val)
{
	/* Trim the trailing CR/LF FGets leaves on. */
	char *eol = line;
	while (*eol != '\0' && *eol != '\n' && *eol != '\r')
		eol++;
	*eol = '\0';

	/* Skip leading blanks; blank and comment lines carry no setting. */
	while (*line == ' ' || *line == '\t')
		line++;
	if (*line == '\0' || *line == '#' || *line == ';')
		return FALSE;

	/* Split at the first '='. */
	char *eq = line;
	while (*eq != '\0' && *eq != '=')
		eq++;
	if (*eq != '=')
		return FALSE;
	*eq = '\0';

	char *k = line;
	char *v = eq + 1;

	/* Strip blanks: the value's leading, then both halves' trailing. */
	while (*v == ' ' || *v == '\t')
		v++;

	char *end = v;
	while (*end != '\0')
		end++;
	while (end > v && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';

	end = k;
	while (*end != '\0')
		end++;
	while (end > k && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';

	if (*k == '\0' || *v == '\0')
		return FALSE;

	*key = k;
	*val = v;
	return TRUE;
}

BOOL prefs_bool(const char *value, BOOL *out)
{
	static const char *const on[] = {"on", "yes", "true", "1"};
	static const char *const off[] = {"off", "no", "false", "0"};

	for (ULONG i = 0; i < sizeof(on) / sizeof(on[0]); i++)
	{
		if (_Stricmp((CONST_STRPTR)value, (CONST_STRPTR)on[i]) == 0)
		{
			*out = TRUE;
			return TRUE;
		}
		if (_Stricmp((CONST_STRPTR)value, (CONST_STRPTR)off[i]) == 0)
		{
			*out = FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

BOOL prefs_number(const char *value, LONG least, LONG most, LONG *out)
{
	BOOL negative = *value == '-';
	if (negative || *value == '+')
		value++;
	if (*value == '\0')
		return FALSE;

	/* Counted downwards: a LONG reaches one further below zero than above it */
	LONG number = 0;
	for (; *value != '\0'; value++)
	{
		LONG digit = *value - '0';
		if (digit < 0 || digit > 9 || number < (PREFS_NUMBER_LEAST + digit) / 10)
			return FALSE;
		number = number * 10 - digit;
	}
	if (!negative)
	{
		if (number == PREFS_NUMBER_LEAST)
			return FALSE;
		number = -number;
	}
	if (number < least || number > most)
		return FALSE;
	*out = number;
	return TRUE;
}
