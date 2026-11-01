// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _PREFS_H
#define _PREFS_H

#include <dos/dos.h>
#include <types.h>

struct ExecBase;
struct DosLibrary;

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

/*
 * A value as a switch: on, yes, true or 1, and off, no, false or 0, in any
 * case. Anything else returns FALSE and leaves *out untouched.
 */
BOOL prefs_bool(const char *value, BOOL *out);

/*
 * A value as a decimal number from @least to @most, the whole of it: "12abc",
 * a number out of that range and one no LONG can hold return FALSE and leave
 * *out untouched. To clamp instead, ask for PREFS_NUMBER_LEAST to
 * PREFS_NUMBER_MOST and clamp the result.
 */
#define PREFS_NUMBER_LEAST (-0x7FFFFFFFL - 1L)
#define PREFS_NUMBER_MOST 0x7FFFFFFFL

BOOL prefs_number(const char *value, LONG least, LONG most, LONG *out);

/*
 * A prefs file, read a line at a time:
 *
 *	struct prefs_file f;
 *	if (prefs_open(&f, SysBase, "ENV:name.prefs"))
 *	{
 *		char *key, *val;
 *		while (prefs_next(&f, &key, &val))
 *			...
 *		prefs_close(&f);
 *	}
 *
 * prefs_next() goes past what prefs_split() refuses. @key and @val point into
 * the struct and last until the next call, and may be cut up further.
 *
 * prefs_open() is FALSE, with nothing to close, for a file that is not there,
 * and for a caller that is a bare Task, which cannot use dos.library: a device
 * can be opened from one. It may run from any Process, a driver's OpenDevice()
 * included, so the struct goes on the caller's stack.
 */
struct prefs_file
{
	struct ExecBase *SysBase;
	struct DosLibrary *DOSBase;
	BPTR fh;
	char line[300]; /* a key and a 255-character path; a longer line reads as two */
};

BOOL prefs_open(struct prefs_file *f, struct ExecBase *SysBase, const char *path);
BOOL prefs_next(struct prefs_file *f, char **key, char **val);
void prefs_close(struct prefs_file *f);

#endif
