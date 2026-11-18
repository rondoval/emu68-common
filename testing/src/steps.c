// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * steps.c - the step loop of a logic test program (logic.h): each argument is
 * a step, name[=operands], made by the program's function for the name, and
 * followed by the state as step.N.*; and what the programs print with.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emu68test/kv.h>
#include <emu68test/logic.h>

#define NUMBER_BASE_ANY 0 /* strtoul: decimal, 0x hex or 0 octal */

/**
 * run_step() - Make one step with the program's function for its name.
 * @program: the program's steps and printer.
 * @context: what the program's steps work on.
 * @prefix: the step's prefix.
 * @step: the step as given.
 *
 * Return: FALSE for a name the program does not know, or operands its function refused.
 */
static BOOL run_step(const struct LogicProgram *program, void *context, const char *prefix, const char *step)
{
    const char *equals = strchr(step, '=');
    size_t length = equals != NULL ? (size_t)(equals - step) : strlen(step);

    for (ULONG index = 0; index < program->count; index++)
    {
        const struct LogicStep *kind = &program->steps[index];
        if (strlen(kind->name) == length && strncmp(kind->name, step, length) == 0)
            return kind->run(context, prefix, equals != NULL ? equals + 1 : NULL);
    }
    return FALSE;
}

/**
 * logic_main() - Make each step given and print the state after it.
 * @program: the program's steps and printer.
 * @context: what the program's steps work on.
 * @argc: the argument count.
 * @argv: the steps.
 *
 * Return: KV_RC_PASS (the test judges what was printed); KV_RC_ENVIRONMENT
 * and fail= for a step the program does not know.
 */
int logic_main(const struct LogicProgram *program, void *context, int argc, char **argv)
{
    char prefix[LOGIC_KEY_TEXT];

    kv_begin(program->command);
    for (int index = 1; index < argc; index++)
    {
        snprintf(prefix, sizeof(prefix), "step.%d", index - 1);
        if (!run_step(program, context, prefix, argv[index]))
        {
            kv_environment("step %d: %s is not a step", index - 1, argv[index]);
            return kv_end(KV_RC_PASS);
        }
        program->print_state(context, prefix);
    }
    kv_unum("steps", (ULONG)(argc - 1));
    return kv_end(KV_RC_PASS);
}

/**
 * logic_switch() - Read a switch: on or up, off or down.
 * @text: the text, or NULL.
 * @on: set to the switch.
 *
 * Return: FALSE for neither.
 */
BOOL logic_switch(const char *text, BOOL *on)
{
    if (text == NULL)
        return FALSE;
    *on = strcmp(text, "on") == 0 || strcmp(text, "up") == 0;
    return *on || strcmp(text, "off") == 0 || strcmp(text, "down") == 0;
}

/**
 * logic_numbers() - Read @count numbers separated by commas, decimal or 0x hex.
 * @text: the text, or NULL.
 * @numbers: where they go.
 * @count: how many.
 *
 * Return: TRUE when exactly @count were there.
 */
BOOL logic_numbers(const char *text, ULONG *numbers, ULONG count)
{
    if (text == NULL)
        return FALSE;
    for (ULONG index = 0; index < count; index++)
    {
        char *end;
        numbers[index] = strtoul(text, &end, NUMBER_BASE_ANY);
        if (end == text || *end != (index + 1 < count ? ',' : '\0'))
            return FALSE;
        text = end + 1;
    }
    return TRUE;
}

/**
 * logic_print_flag() - Print a yes/no fact under a step's prefix.
 * @prefix: the step's prefix.
 * @name: the fact's name, from its dot.
 * @value: the flag.
 */
void logic_print_flag(const char *prefix, const char *name, BOOL value)
{
    char key[LOGIC_KEY_TEXT];
    logic_key(key, prefix, "%s", name);
    kv_bool(key, value);
}

/**
 * logic_print_number() - Print a number under a step's prefix.
 * @prefix: the step's prefix.
 * @name: the fact's name, from its dot.
 * @value: the number.
 */
void logic_print_number(const char *prefix, const char *name, ULONG value)
{
    char key[LOGIC_KEY_TEXT];
    logic_key(key, prefix, "%s", name);
    kv_unum(key, value);
}

/**
 * logic_print_text() - Print a text under a step's prefix.
 * @prefix: the step's prefix.
 * @name: the fact's name, from its dot.
 * @value: the text, one line.
 */
void logic_print_text(const char *prefix, const char *name, const char *value)
{
    char key[LOGIC_KEY_TEXT];
    logic_key(key, prefix, "%s", name);
    kv_str(key, value);
}

/**
 * logic_key() - A key under a prefix: <prefix><format>, cut at LOGIC_KEY_TEXT.
 * @key: room for LOGIC_KEY_TEXT characters.
 * @prefix: the prefix, such as step.3.
 * @format: the printf() format of the rest, from its dot.
 * @...: the format's arguments.
 */
void logic_key(char *key, const char *prefix, const char *format, ...)
{
    va_list arguments;
    int used = snprintf(key, LOGIC_KEY_TEXT, "%s", prefix);
    va_start(arguments, format);
    if (used >= 0 && used < (int)LOGIC_KEY_TEXT)
        vsnprintf(key + used, LOGIC_KEY_TEXT - (size_t)used, format, arguments);
    va_end(arguments);
}

/**
 * exec_call_name() - The name of a call the fake exec recorded.
 * @kind: the call's kind.
 *
 * Return: its name, or "?" for a kind that is not exec's.
 */
static const char *exec_call_name(UBYTE kind)
{
    switch (kind)
    {
    case LOGIC_SIGNAL:
        return "signal";
    case LOGIC_PUTMSG:
        return "putmsg";
    case LOGIC_REPLYMSG:
        return "reply";
    default:
        return "?";
    }
}

/**
 * logic_print_calls() - Print the calls recorded as <prefix>.call.N.kind and
 * .value and <prefix>.calls, and forget them.
 * @prefix: the keys' prefix, such as step.3.
 * @namer: how the program names its own calls, or NULL when it records none.
 */
void logic_print_calls(const char *prefix, LogicCallNamer namer)
{
    const struct LogicCall *calls;
    ULONG count = logic_calls(&calls);
    char key[LOGIC_KEY_TEXT];

    for (ULONG index = 0; index < count; index++)
    {
        char value[LOGIC_VALUE_TEXT] = "";
        const char *name = calls[index].kind >= LOGIC_CALL_PROGRAM && namer != NULL ? namer(&calls[index], value) : NULL;

        logic_key(key, prefix, ".call.%lu.kind", (unsigned long)index);
        kv_str(key, name != NULL ? name : exec_call_name(calls[index].kind));
        logic_key(key, prefix, ".call.%lu.value", (unsigned long)index);
        if (value[0] != 0)
            kv_str(key, value);
        else
            kv_hex(key, calls[index].value);
    }
    logic_key(key, prefix, ".calls");
    kv_unum(key, count);
    logic_calls_clear();
}
