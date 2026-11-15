// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * logicself.c - the harness's own logic program (testing/README.md): it runs
 * the kv grammar and the fake exec and utility bases under vamos, so the
 * suite in tests/ can judge them without a component.
 *
 *     LogicSelf <step>...
 *
 * Each step, then the state after it as step.N.*:
 *
 *     facts              one fact of every kind kv.h prints
 *     check=pass|fail    one check, passed or failed
 *     nomem=<n>          the next n allocations fail
 *     alloc=<bytes>      AllocVec() and FreeVec() through the fake exec
 *     signal=<mask>      Signal() through the fake exec
 *     post               PutMsg() of a message, taken back and replied
 *     forbid=on|off      Forbid() or Permit() through the fake exec
 *     tag=<tag>          GetTagData() of the tag from a list holding tags 1 and 2
 *     own=<value>        a call of the program's own kind, recorded
 *     stray              an exec function the fake base does not model (OpenLibrary)
 */

#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/utility.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <utility/tagitem.h>

#include <stdio.h>
#include <string.h>

#include <emu68test/kv.h>
#include <emu68test/logic.h>

#define TAG_FIRST (TAG_USER + 1u)  /* the first tag of the list tag= searches */
#define TAG_SECOND (TAG_USER + 2u) /* and the second */
#define TAG_FIRST_DATA 11u
#define TAG_SECOND_DATA 22u
#define TAG_MISSING_DATA 99u       /* GetTagData()'s default */
#define TASK_COOKIE 0x1000u        /* a task's address, for Signal() to record */
#define VERSION_SHOWN 47u
#define REVISION_SHOWN 115u
#define RATE_NUMERATOR 853u        /* 85.3 as kv_fixed1() prints it */
#define RATE_DENOMINATOR 10u

/* The program's own call kind. */
enum SelfCallKind : UBYTE
{
    SELF_OWN = LOGIC_CALL_PROGRAM, /* own=<value>: value the number given */
};

/* What the steps work on: the bases the code under test would be handed. */
struct Self
{
    struct ExecBase *SysBase;    /* the fake exec */
    struct Library *UtilityBase; /* the fake utility.library */
    ULONG allocated;             /* the last alloc= got memory */
};

/**
 * step_facts() - Print one fact of every kind.
 * @context: the program's state, unused.
 * @prefix: the step's prefix.
 * @operands: none.
 *
 * Return: FALSE for operands.
 */
static BOOL step_facts(void *context, const char *prefix, const char *operands)
{
    char key[LOGIC_KEY_TEXT];

    (void)context;
    logic_key(key, prefix, ".text");
    kv_str(key, "a name");
    logic_key(key, prefix, ".signed");
    kv_num(key, -3);
    logic_key(key, prefix, ".unsigned");
    kv_unum(key, 0xFFFFFFFFu);
    logic_key(key, prefix, ".hex");
    kv_hex(key, 0x29u);
    logic_key(key, prefix, ".flag");
    kv_bool(key, TRUE);
    logic_key(key, prefix, ".version");
    kv_version(key, VERSION_SHOWN, REVISION_SHOWN);
    logic_key(key, prefix, ".rate");
    kv_fixed1(key, RATE_NUMERATOR, RATE_DENOMINATOR);
    kv_str(kv_at("entry", 1, "name"), "second");
    kv_note("prose for %s", "people");
    return operands == NULL;
}

/**
 * step_check() - Make one check.
 * @context: the program's state, unused.
 * @prefix: the step's prefix, unused.
 * @operands: pass or fail.
 *
 * Return: FALSE for another word.
 */
static BOOL step_check(void *context, const char *prefix, const char *operands)
{
    (void)context;
    (void)prefix;
    if (operands == NULL)
        return FALSE;
    BOOL passed = strcmp(operands, "pass") == 0;
    if (!passed && strcmp(operands, "fail") != 0)
        return FALSE;
    kv_check(passed, "the check asked for", "asked to %s", operands);
    return TRUE;
}

/**
 * step_nomem() - Make the next allocations fail.
 * @context: the program's state, unused.
 * @prefix: the step's prefix, unused.
 * @operands: how many.
 *
 * Return: FALSE for operands that are not one number.
 */
static BOOL step_nomem(void *context, const char *prefix, const char *operands)
{
    ULONG count;

    (void)context;
    (void)prefix;
    if (!logic_numbers(operands, &count, 1))
        return FALSE;
    logic_fail_allocations(count);
    return TRUE;
}

/**
 * step_alloc() - AllocVec() and FreeVec() through the fake exec.
 * @context: the program's state.
 * @prefix: the step's prefix, unused.
 * @operands: the size.
 *
 * Return: FALSE for operands that are not one number.
 */
static BOOL step_alloc(void *context, const char *prefix, const char *operands)
{
    struct Self *self = context;
    struct ExecBase *SysBase = self->SysBase;
    ULONG size;

    (void)prefix;
    if (!logic_numbers(operands, &size, 1))
        return FALSE;
    APTR memory = AllocVec(size, MEMF_CLEAR);
    self->allocated = memory != NULL;
    if (memory != NULL)
        FreeVec(memory);
    return TRUE;
}

/**
 * step_signal() - Signal() through the fake exec.
 * @context: the program's state.
 * @prefix: the step's prefix, unused.
 * @operands: the signal mask.
 *
 * Return: FALSE for operands that are not one number.
 */
static BOOL step_signal(void *context, const char *prefix, const char *operands)
{
    struct Self *self = context;
    struct ExecBase *SysBase = self->SysBase;
    ULONG mask;

    (void)prefix;
    if (!logic_numbers(operands, &mask, 1))
        return FALSE;
    Signal((struct Task *)TASK_COOKIE, mask);
    return TRUE;
}

/**
 * step_post() - PutMsg() a message, take it back as the port's task would, and reply.
 * @context: the program's state.
 * @prefix: the step's prefix.
 * @operands: none.
 *
 * Return: FALSE for operands.
 */
static BOOL step_post(void *context, const char *prefix, const char *operands)
{
    static struct MsgPort port;
    static struct Message message;
    struct Self *self = context;
    struct ExecBase *SysBase = self->SysBase;

    PutMsg(&port, &message);
    struct Message *taken = logic_port_take();
    logic_print_flag(prefix, ".taken", taken == &message);
    logic_print_flag(prefix, ".empty", logic_port_take() == NULL);
    if (taken != NULL)
        ReplyMsg(taken);
    logic_print_flag(prefix, ".replied", message.mn_Node.ln_Type == NT_REPLYMSG);
    return operands == NULL;
}

/**
 * step_forbid() - Forbid() or Permit() through the fake exec.
 * @context: the program's state.
 * @prefix: the step's prefix, unused.
 * @operands: on or off.
 *
 * Return: FALSE for another word.
 */
static BOOL step_forbid(void *context, const char *prefix, const char *operands)
{
    struct Self *self = context;
    struct ExecBase *SysBase = self->SysBase;
    BOOL on;

    (void)prefix;
    if (!logic_switch(operands, &on))
        return FALSE;
    if (on)
        Forbid();
    else
        Permit();
    return TRUE;
}

/**
 * step_tag() - GetTagData() through the fake utility.library.
 * @context: the program's state.
 * @prefix: the step's prefix.
 * @operands: the tag to look for, from TAG_USER.
 *
 * Return: FALSE for operands that are not one number.
 */
static BOOL step_tag(void *context, const char *prefix, const char *operands)
{
    struct TagItem tags[] = {{TAG_FIRST, TAG_FIRST_DATA}, {TAG_SECOND, TAG_SECOND_DATA}, {TAG_DONE, 0}};
    struct Self *self = context;
    struct Library *UtilityBase = self->UtilityBase;
    ULONG tag;

    if (!logic_numbers(operands, &tag, 1))
        return FALSE;
    logic_print_number(prefix, ".data", GetTagData(TAG_USER + tag, TAG_MISSING_DATA, tags));
    return TRUE;
}

/**
 * step_own() - Record a call of the program's own kind.
 * @context: the program's state, unused.
 * @prefix: the step's prefix, unused.
 * @operands: its value.
 *
 * Return: FALSE for operands that are not one number.
 */
static BOOL step_own(void *context, const char *prefix, const char *operands)
{
    ULONG value;

    (void)context;
    (void)prefix;
    if (!logic_numbers(operands, &value, 1))
        return FALSE;
    logic_record(SELF_OWN, NULL, value);
    return TRUE;
}

/**
 * step_stray() - Call an exec function the fake base does not model, which ends the run.
 * @context: the program's state.
 * @prefix: the step's prefix, unused.
 * @operands: none.
 *
 * Return: FALSE for operands; never reached without them.
 */
static BOOL step_stray(void *context, const char *prefix, const char *operands)
{
    struct Self *self = context;
    struct ExecBase *SysBase = self->SysBase;

    (void)prefix;
    if (operands != NULL)
        return FALSE;
    OpenLibrary((CONST_STRPTR) "dos.library", 0);
    return TRUE;
}

/**
 * name_call() - Name the program's own call.
 * @call: the call.
 * @value: room for its value as text.
 *
 * Return: its name, with the value as "own <n>".
 */
static const char *name_call(const struct LogicCall *call, char *value)
{
    snprintf(value, LOGIC_VALUE_TEXT, "own %lu", (unsigned long)call->value);
    return "own";
}

/**
 * print_state() - The state after a step: the nesting, the last allocation, the calls.
 * @context: the program's state.
 * @prefix: the step's prefix.
 */
static void print_state(const void *context, const char *prefix)
{
    const struct Self *self = context;
    char key[LOGIC_KEY_TEXT];

    logic_key(key, prefix, ".forbids");
    kv_num(key, logic_forbids());
    logic_print_flag(prefix, ".allocated", self->allocated != 0);
    logic_print_calls(prefix, name_call);
}

static const struct LogicStep steps[] = {
    {"facts", step_facts},   {"check", step_check}, {"nomem", step_nomem},   {"alloc", step_alloc},
    {"signal", step_signal}, {"post", step_post},   {"forbid", step_forbid}, {"tag", step_tag},
    {"own", step_own},       {"stray", step_stray},
};

static const struct LogicProgram program = {"self", steps, LOGIC_COUNT_OF(steps), print_state};

/**
 * main() - Make each step given on the fake bases.
 * @argc: the argument count.
 * @argv: the steps.
 *
 * Return: logic_main()'s.
 */
int main(int argc, char **argv)
{
    struct Self self = {logic_exec(), logic_utility(), 0};
    return logic_main(&program, &self, argc, argv);
}
