// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * prefs.c — split one line of an ENV:*.prefs file into key/value, in place.
 * Pure string manipulation; no file-scope state (ROM-safe).
 */

#include <prefs.h>

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
