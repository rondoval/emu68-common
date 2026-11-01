// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * prefscheck — proves prefs.h and strfit() on m68k: what each value comes to,
 * the ends of a number's range, and a real file read a line at a time.  The
 * file is T:prefscheck.prefs, deleted again.
 *
 * Not installed.  Runs on an Amiga, or on the host:
 *     vamos -C 68040 -m 4096 prefscheck
 * RC 0 when everything held, else RC 10 and each failure on stdout.
 */
#ifdef __INTELLISENSE__
#include <clib/dos_protos.h>
#include <clib/exec_protos.h>
#else
#include <proto/dos.h>
#include <proto/exec.h>
#endif

#include <dos/dos.h>
#include <exec/tasks.h>
#include <exec/types.h>

#include <prefs.h>
#include <strutil.h>

static const char verstag[] __attribute__((used)) = VERSTAG;

#define FILE_NAME "T:prefscheck.prefs"
#define UNTOUCHED 12345

static ULONG checks, failed;

/**
 * check() - Count one check, and say so if it did not hold.
 * @held: whether it held
 * @what: what it was
 */
static void check(BOOL held, const char *what)
{
	checks++;
	if (!held)
	{
		failed++;
		Printf((CONST_STRPTR) "failed: %s\n", what);
	}
}

/**
 * check_split() - Check the lines that hold nothing, and one that does.
 */
static void check_split(void)
{
	static const char *const nothing[] = {"", "\n", "   \n", "# KEY = 1\n", "; KEY = 1\n", "no equals\n", "= value\n", "KEY =\n", "KEY =   \n"};
	char line[64];
	char *key, *val;

	for (ULONG i = 0; i < sizeof(nothing) / sizeof(nothing[0]); i++)
	{
		strlcpy(line, nothing[i], sizeof(line));
		check(!prefs_split(line, &key, &val), nothing[i]);
	}
	strlcpy(line, "  KEY  =  a value = b \r\n", sizeof(line));
	check(prefs_split(line, &key, &val) && strcmp(key, "KEY") == 0 && strcmp(val, "a value = b") == 0, "the first = splits, blanks go");
}

/**
 * check_bool() - Check every word in either case, and one that is none.
 */
static void check_bool(void)
{
	static const struct
	{
		const char *word;
		BOOL value;
	} words[] = {{"on", TRUE},  {"YES", TRUE}, {"True", TRUE},  {"1", TRUE},
	             {"off", FALSE}, {"NO", FALSE}, {"False", FALSE}, {"0", FALSE}};

	for (ULONG i = 0; i < sizeof(words) / sizeof(words[0]); i++)
	{
		BOOL out = (BOOL)!words[i].value;
		check(prefs_bool(words[i].word, &out) && out == words[i].value, words[i].word);
	}
	BOOL out = 7;
	check(!prefs_bool("maybe", &out) && out == 7, "maybe");
	check(!prefs_bool("", &out) && out == 7, "an empty value");
}

/**
 * check_number() - Check numbers, the ends of a LONG, and what is no number.
 */
static void check_number(void)
{
	static const struct
	{
		const char *text;
		LONG value;
	} numbers[] = {{"0", 0}, {"42", 42}, {"-7", -7}, {"+7", 7}, {"007", 7}, {"2147483647", 2147483647L}, {"-2147483648", PREFS_NUMBER_LEAST}};
	static const char *const none[] = {"", "-", "+", "abc", "12abc", "1 2", " 1", "--1", "2147483648", "-2147483649", "99999999999", "4294967296"};
	LONG out = UNTOUCHED;

	for (ULONG i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
		check(prefs_number(numbers[i].text, PREFS_NUMBER_LEAST, PREFS_NUMBER_MOST, &out) && out == numbers[i].value, numbers[i].text);
	for (ULONG i = 0; i < sizeof(none) / sizeof(none[0]); i++)
	{
		out = UNTOUCHED;
		check(!prefs_number(none[i], PREFS_NUMBER_LEAST, PREFS_NUMBER_MOST, &out) && out == UNTOUCHED, none[i]);
	}
	check(prefs_number("1", 1, 60, &out) && out == 1, "the least of a range");
	check(prefs_number("60", 1, 60, &out) && out == 60, "the most of a range");
	out = UNTOUCHED;
	check(!prefs_number("0", 1, 60, &out) && out == UNTOUCHED, "below a range");
	check(!prefs_number("61", 1, 60, &out) && out == UNTOUCHED, "above a range");
}

/**
 * check_fit() - Check strfit() on both sides of the limit.
 */
static void check_fit(void)
{
	char out[4];

	check(strfit(out, "abc", sizeof(out)) && strcmp(out, "abc") == 0, "three in four bytes");
	check(!strfit(out, "wxyz", sizeof(out)) && strcmp(out, "abc") == 0, "four in four bytes leaves what was there");
	check(strfit(out, "", sizeof(out)) && out[0] == '\0', "nothing fits");
}

/**
 * check_file() - Check a real file: what is skipped, the order, a line too long.
 */
static void check_file(void)
{
	static const char text[] = "# a comment\n\nFIRST = 1\nno equals\nSECOND = two words\r\n";
	struct prefs_file f;
	char *key, *val;

	check(!prefs_open(&f, SysBase, "T:prefscheck.nothing"), "a file that is not there");

	BPTR fh = Open((CONST_STRPTR)FILE_NAME, MODE_NEWFILE);
	if (fh == 0)
	{
		check(FALSE, "no " FILE_NAME " to write");
		return;
	}
	(void)Write(fh, (APTR)text, sizeof(text) - 1);
	(void)FPuts(fh, (CONST_STRPTR) "THIRD = ");
	for (ULONG i = 0; i < sizeof(f.line) * 2; i++)
		(void)FPutC(fh, 'a');
	(void)FPuts(fh, (CONST_STRPTR) "\nLAST = end");
	(void)Close(fh);

	if (prefs_open(&f, SysBase, FILE_NAME))
	{
		check(prefs_next(&f, &key, &val) && strcmp(key, "FIRST") == 0 && strcmp(val, "1") == 0, "the first setting");
		check(prefs_next(&f, &key, &val) && strcmp(key, "SECOND") == 0 && strcmp(val, "two words") == 0, "the second setting");
		check(prefs_next(&f, &key, &val) && strcmp(key, "THIRD") == 0, "the line too long, as far as it fits");
		check(prefs_next(&f, &key, &val) && strcmp(key, "LAST") == 0 && strcmp(val, "end") == 0, "the last setting, no newline after it");
		check(!prefs_next(&f, &key, &val), "the end of the file");
		prefs_close(&f);
	}
	else
		check(FALSE, "the file would not open");

	/* A bare Task: prefs_open() must not get as far as dos.library */
	struct Task *self = FindTask(NULL);
	UBYTE type = self->tc_Node.ln_Type;
	Forbid();
	self->tc_Node.ln_Type = NT_TASK;
	BOOL opened = prefs_open(&f, SysBase, FILE_NAME);
	self->tc_Node.ln_Type = type;
	Permit();
	if (opened)
		prefs_close(&f);
	check(!opened, "a bare Task");

	(void)DeleteFile((CONST_STRPTR)FILE_NAME);
}

int main(void)
{
	check_split();
	check_bool();
	check_number();
	check_fit();
	check_file();
	Printf((CONST_STRPTR) "%lu checks, %lu failed\n", checks, failed);
	return failed != 0 ? RETURN_ERROR : RETURN_OK;
}
