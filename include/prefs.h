// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _PREFS_H
#define _PREFS_H

#include <types.h>

/*
 * Split one line of an ENV:*.prefs file into a key and a value, in place.
 * @line is the raw FGets() result, trailing CR/LF included.
 *
 * On a "key = value" line the newline is trimmed, whitespace around both key
 * and value is stripped, *key and *val are pointed at the two NUL-terminated
 * halves, and TRUE is returned. A blank line, a comment (first non-blank is '#'
 * or ';'), a line with no '=', or one whose key or value is empty returns FALSE
 * and leaves *key and *val untouched.
 */
BOOL prefs_split(char *line, char **key, char **val);

#endif
