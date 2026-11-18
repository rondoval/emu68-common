/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * logic.h - support for a logic test program: a component's own sources built
 * as an m68k CLI program and run on the host under vamos, where emu68test's
 * vamos.py judges what it prints (kv.h).
 *
 * Code under test that reaches exec and utility.library through a base
 * pointer of its own is handed fake bases (fakelib.c, fakeexec.c): each
 * function modelled goes to a stub, the ones that only keep memory or lists
 * on to the real exec, and any other call ends the run with fail=<the library
 * and LVO>. Signal(), PutMsg() and ReplyMsg() are recorded, and the program
 * records what its own stand-ins see (logic_record()).
 *
 * A program takes its steps from the command line (steps.c): each argument
 * is name[=operands], made by the program's function for the name, and
 * followed by the state as step.N.*.
 */
#ifndef EMU68TEST_LOGIC_H
#define EMU68TEST_LOGIC_H

#include <exec/types.h>

struct Library;
struct ExecBase;
struct Message;

/* What a recorded call was. A program's own kinds start at LOGIC_CALL_PROGRAM. */
enum LogicCallKind : UBYTE
{
    LOGIC_SIGNAL = 1,        /* exec Signal(): target the task, value the signal set */
    LOGIC_PUTMSG,            /* exec PutMsg(): target the message, value the port */
    LOGIC_REPLYMSG,          /* exec ReplyMsg(): target the message */
    LOGIC_CALL_PROGRAM = 16, /* the first kind a program may define */
};

/* One call the code under test made. */
struct LogicCall
{
    UBYTE kind;  /* a LogicCallKind, or the program's own */
    APTR target; /* what it was called on */
    ULONG value; /* its number: signals, a mask */
};

#define LOGIC_CALLS_MAX 256u

void logic_record(UBYTE kind, APTR target, ULONG value);
ULONG logic_calls(const struct LogicCall **calls);
void logic_calls_clear(void);

/* A fake library's function: its LVO (negative) and the stub it jumps to. */
struct FakeFunction
{
    LONG lvo;  /* the offset from the base, as the NDK's lvo/<library>_lib.i gives it */
    APTR code; /* the stub, which takes the LVO's registers */
};

struct Library *fake_library_create(const char *name, ULONG slots, ULONG baseSize, const void *image,
                                    const struct FakeFunction *functions, ULONG count);

/* The fake bases, built at the first call; logic_forbids() is the Forbid()
 * and Disable() nesting now, 0 when the code under test left them balanced. */
struct ExecBase *logic_exec(void);
struct Library *logic_utility(void);
LONG logic_forbids(void);
/* The next @count allocations the code under test asks the fake exec for fail. */
void logic_fail_allocations(ULONG count);
/* The oldest message PutMsg() posted and nobody took yet, or NULL. */
struct Message *logic_port_take(void);

#define LOGIC_KEY_TEXT 64u   /* room for a key such as step.12.call.3.value */
#define LOGIC_VALUE_TEXT 200u /* room for a call's value as text */

/* One step a program knows. */
struct LogicStep
{
    const char *name;                                                       /* the text before '=' */
    BOOL (*run)(void *context, const char *prefix, const char *operands);   /* operands NULL without '='; FALSE refuses them */
};

/* A program: its steps and how it prints its state. */
struct LogicProgram
{
    const char *command;                                     /* the command= fact */
    const struct LogicStep *steps;                           /* the steps it knows */
    ULONG count;                                             /* how many */
    void (*print_state)(const void *context, const char *prefix); /* after each step */
};

/* How a program names one of its own calls: the kind's name, and in @value
 * its number as text, or "" to have it printed as hex. NULL for a kind it
 * does not know. */
typedef const char *(*LogicCallNamer)(const struct LogicCall *call, char *value);

#define LOGIC_COUNT_OF(table) (sizeof(table) / sizeof((table)[0]))

int logic_main(const struct LogicProgram *program, void *context, int argc, char **argv);
BOOL logic_switch(const char *text, BOOL *on);
BOOL logic_numbers(const char *text, ULONG *numbers, ULONG count);
void logic_key(char *key, const char *prefix, const char *format, ...) __attribute__((format(printf, 3, 4)));
void logic_print_flag(const char *prefix, const char *name, BOOL value);
void logic_print_number(const char *prefix, const char *name, ULONG value);
void logic_print_text(const char *prefix, const char *name, const char *value);
void logic_print_calls(const char *prefix, LogicCallNamer namer);

#endif /* EMU68TEST_LOGIC_H */
