/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * kv.h - the output grammar of the stack's Amiga-side test programs, which
 * emu68test's results.py reads:
 *
 *   format=1
 *   command=<what ran>
 *   key=value ...                facts; groups indexed by number (check.0.name)
 *     prose ...                  for people, indented two spaces, never parsed
 *   checks=<n>
 *   checks_failed=<n>
 *   PASS | FAIL                  the last line
 *
 *   key   := [a-z][a-z0-9_]* ("." [a-z0-9_]+)*
 *   value := the rest of the line: decimal, 0x hex, yes/no, one-decimal rates,
 *            names verbatim
 *
 * A check is check.N.name (the same text every run), check.N.ok=yes|no and
 * check.N.detail (what was seen). The return codes are AmigaDOS levels. A
 * usage error is printed before the format line, so the suite reads it as no
 * output of the grammar at all.
 *
 * Every line goes to stdout, flushed: a program broken mid-run leaves what it
 * printed. Linking this library turns off libnix's own Ctrl-C check, which
 * would end the program inside a stdio call, without its verdict: a program
 * that can be stopped looks for SIGBREAKF_CTRL_C itself and winds down. A program on a real machine may also mark its run in Emu68's debug
 * log (kv_debug_log()), so the suite can cut the log by runs.
 */
#ifndef EMU68TEST_KV_H
#define EMU68TEST_KV_H

#include <exec/types.h>

#define KV_FORMAT 1

#define KV_RC_PASS 0          /* done, and every check passed */
#define KV_RC_CHECK_FAILED 5  /* a check failed */
#define KV_RC_ENVIRONMENT 10  /* could not test: a library, device or file is missing */
#define KV_RC_ERROR 20        /* the API under test refused, a timeout, or a usage error */

void kv_debug_log(const char *tool, const char *arguments, BOOL trace);
void kv_begin(const char *command);
void kv_str(const char *key, const char *value);
void kv_num(const char *key, LONG value);
void kv_unum(const char *key, ULONG value);
void kv_hex(const char *key, ULONG value);
void kv_bool(const char *key, BOOL value);
void kv_version(const char *key, ULONG version, ULONG revision);
void kv_fixed1(const char *key, ULONG numerator, ULONG denominator);
const char *kv_at(const char *group, ULONG index, const char *field);
void kv_note(const char *format, ...) __attribute__((format(printf, 1, 2)));
BOOL kv_check(BOOL passed, const char *name, const char *format, ...) __attribute__((format(printf, 3, 4)));
void kv_environment(const char *format, ...) __attribute__((format(printf, 1, 2)));
int kv_end(int rc);

#endif /* EMU68TEST_KV_H */
