// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * kv.c - the output grammar of the stack's Amiga-side test programs (kv.h).
 */

#include <emu68test/kv.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_PORT 0xdeadbeefUL /* Emu68 logs every byte written here */
#define LINE_BYTES 512u         /* the longest line echoed to the debug log, or a check's detail */
#define KEY_BYTES 96u           /* kv_at()'s key */
#define TENTHS 10u              /* kv_fixed1() prints one decimal */

static const char *toolName; /* the debug log's prefix; NULL without kv_debug_log() */
static BOOL traceOn;         /* every line goes to the debug log too */
static ULONG checksMade;
static ULONG checksFailed;
static BOOL environmentFailed;

void __chkabort(void);

/**
 * __chkabort() - Replace libnix's Ctrl-C check, which its stdio calls and
 * which ends the program on the spot.
 *
 * A program of the grammar ends with its verdict: it looks for Ctrl-C itself
 * and winds down, or the suite cannot stop it.
 */
void __chkabort(void)
{
}

/**
 * debug_text() - Write a text to Emu68's debug log, as it is.
 * @text: the text.
 */
static void debug_text(const char *text)
{
    while (*text != 0)
        *(volatile UBYTE *)DEBUG_PORT = (UBYTE)*text++;
}

/**
 * emit_arguments() - Print one line, flushed, and echo it to the debug log when tracing.
 * @format: its printf() format, the newline included.
 * @arguments: the format's arguments.
 */
static void emit_arguments(const char *format, va_list arguments)
{
    char line[LINE_BYTES];

    vsnprintf(line, sizeof(line), format, arguments);
    fputs(line, stdout);
    fflush(stdout);
    if (traceOn)
    {
        debug_text("[");
        debug_text(toolName);
        debug_text("] ");
        debug_text(line);
    }
}

/**
 * emit() - Print one line, flushed, and echo it to the debug log when tracing.
 * @format: its printf() format, the newline included.
 * @...: the format's arguments.
 */
static __attribute__((format(printf, 1, 2))) void emit(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    emit_arguments(format, arguments);
    va_end(arguments);
}

/**
 * kv_debug_log() - Mark this run in Emu68's debug log: "[tool] begin
 * <arguments>" now, "[tool] end rc=<n>" from kv_end().
 * @tool: the program's name, the prefix of its lines in the log.
 * @arguments: its command line, with its newline or without.
 * @trace: TRUE to echo every line printed to the log as well.
 *
 * Only for a program on a real machine: nothing answers at the debug port
 * under vamos.
 */
void kv_debug_log(const char *tool, const char *arguments, BOOL trace)
{
    toolName = tool;
    traceOn = trace;
    debug_text("[");
    debug_text(tool);
    debug_text("] begin ");
    debug_text(arguments);
    if (arguments[0] == 0 || arguments[strlen(arguments) - 1u] != '\n')
        debug_text("\n");
}

/**
 * kv_begin() - Start the output: the format line and the command.
 * @command: what this run does, as its first argument names it.
 */
void kv_begin(const char *command)
{
    emit("format=%d\n", KV_FORMAT);
    kv_str("command", command);
}

/**
 * kv_str() - Print one fact as text.
 * @key: its key.
 * @value: its value, one line.
 */
void kv_str(const char *key, const char *value)
{
    emit("%s=%s\n", key, value);
}

/**
 * kv_num() - Print one fact as a signed decimal.
 * @key: its key.
 * @value: its value.
 */
void kv_num(const char *key, LONG value)
{
    emit("%s=%ld\n", key, (long)value);
}

/**
 * kv_unum() - Print one fact as an unsigned decimal.
 * @key: its key.
 * @value: its value.
 */
void kv_unum(const char *key, ULONG value)
{
    emit("%s=%lu\n", key, (unsigned long)value);
}

/**
 * kv_hex() - Print one fact as hexadecimal, eight digits.
 * @key: its key.
 * @value: its value.
 */
void kv_hex(const char *key, ULONG value)
{
    emit("%s=0x%08lx\n", key, (unsigned long)value);
}

/**
 * kv_bool() - Print one fact as yes or no.
 * @key: its key.
 * @value: the flag.
 */
void kv_bool(const char *key, BOOL value)
{
    kv_str(key, value ? "yes" : "no");
}

/**
 * kv_version() - Print one fact as version.revision.
 * @key: its key.
 * @version: the version.
 * @revision: the revision.
 */
void kv_version(const char *key, ULONG version, ULONG revision)
{
    emit("%s=%lu.%lu\n", key, (unsigned long)version, (unsigned long)revision);
}

/**
 * kv_fixed1() - Print one fact as a quotient with one decimal (a rate).
 * @key: its key.
 * @numerator: the numerator.
 * @denominator: the denominator; 0 prints 0.0.
 */
void kv_fixed1(const char *key, ULONG numerator, ULONG denominator)
{
    ULONG tenths = denominator != 0 ? (ULONG)((unsigned long long)numerator * TENTHS / denominator) : 0;
    emit("%s=%lu.%lu\n", key, (unsigned long)(tenths / TENTHS), (unsigned long)(tenths % TENTHS));
}

/**
 * kv_at() - Make the key of an indexed group's field: group.index.field.
 * @group: the group.
 * @index: the entry's number, from 0.
 * @field: the field.
 *
 * Return: the key in a static buffer, good until the next call.
 */
const char *kv_at(const char *group, ULONG index, const char *field)
{
    static char key[KEY_BYTES];

    snprintf(key, sizeof(key), "%s.%lu.%s", group, (unsigned long)index, field);
    return key;
}

/**
 * kv_note() - Print a line of prose, for people.
 * @format: its printf() format, without a newline.
 * @...: the format's arguments.
 */
void kv_note(const char *format, ...)
{
    char text[LINE_BYTES];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    emit("  %s\n", text);
}

/**
 * kv_check() - Print one check as check.N.name, check.N.ok and check.N.detail.
 * @passed: its outcome.
 * @name: what it checks, the same text every run.
 * @format: the printf() format of its detail: what was seen.
 * @...: the format's arguments.
 *
 * Return: @passed.
 */
BOOL kv_check(BOOL passed, const char *name, const char *format, ...)
{
    char detail[LINE_BYTES];
    va_list arguments;
    ULONG index = checksMade++;

    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);
    kv_str(kv_at("check", index, "name"), name);
    kv_bool(kv_at("check", index, "ok"), passed);
    kv_str(kv_at("check", index, "detail"), detail);
    if (!passed)
        checksFailed++;
    return passed;
}

/**
 * kv_environment() - Report that the run could not test: a library, device,
 * port or file is missing.
 * @format: the printf() format of what failed.
 * @...: the format's arguments.
 */
void kv_environment(const char *format, ...)
{
    char text[LINE_BYTES];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    kv_str("fail", text);
    environmentFailed = TRUE;
}

/**
 * kv_end() - End the output: the checks' tally, the verdict, and the end mark
 * in the debug log after kv_debug_log().
 * @rc: the program's own return code, KV_RC_PASS when it has none.
 *
 * Return: the code to exit with: @rc, raised to KV_RC_CHECK_FAILED by a
 * failed check and to KV_RC_ENVIRONMENT by kv_environment().
 */
int kv_end(int rc)
{
    if (checksFailed != 0 && rc < KV_RC_CHECK_FAILED)
        rc = KV_RC_CHECK_FAILED;
    if (environmentFailed && rc < KV_RC_ENVIRONMENT)
        rc = KV_RC_ENVIRONMENT;
    kv_unum("checks", checksMade);
    kv_unum("checks_failed", checksFailed);
    emit("%s\n", rc == KV_RC_PASS ? "PASS" : "FAIL");
    if (toolName != NULL)
    {
        char mark[LINE_BYTES];
        snprintf(mark, sizeof(mark), "[%s] end rc=%d\n", toolName, rc);
        debug_text(mark);
    }
    return rc;
}
