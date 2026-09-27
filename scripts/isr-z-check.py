#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""Check that an Exec interrupt server leaves the Z flag its caller expects.

exec.library/AddIntServer walks a server chain until a server returns with the
Z condition code CLEAR ("this interrupt was mine, stop here"); Z SET lets the
walk go on.  The flag, not D0, is what the caller tests -- and the autodoc
warns that a compiler may leave it set from something other than the return
value:

    Watch out for a "MOVEM" instruction (which does not affect the condition
    codes) turning into "MOVE" (which does).

That is exactly what m68k GCC does when a function saves a single data
register: the epilogue restores it with `move.l (sp)+,dN`, which sets Z from
the restored register.  `return 1` then reaches the caller looking like "not
handled".  It is silent, it depends on register allocation, and it changes
under an innocent edit.

So: disassemble the objects, slice out each named server, and walk back from
every `rts` to the last instruction that touches the CCR.  That instruction
must be the one that left the return value in D0.

Usage: isr-z-check.py --objdump <objdump> --objdir <dir> SYMBOL [SYMBOL ...]
"""

import argparse
import os
import re
import subprocess
import sys

# binutils prints either "00000000 <_sym>:" or, for the amiga hunk target on
# some builds, "00000000 00000000 _sym:".
SYM_ANGLE = re.compile(r'^[0-9a-f]+\s+<([^>]+)>:\s*$')
SYM_PLAIN = re.compile(r'^[0-9a-f]{8} [0-9a-f]{8} (\S+):\s*$')
INSN = re.compile(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\t(.*)$')

# Mnemonics come in Motorola ("movea.l", "bne.s") or MIT ("moveal", "bnes")
# spelling depending on the binutils build, so match on a prefix.
CC_TRANSPARENT = ('movem', 'movea', 'lea', 'pea', 'jmp', 'jsr', 'bsr', 'rts',
                  'rte', 'rtr', 'nop', 'link', 'unlk', 'exg', 'adda', 'suba',
                  'reset', 'stop', 'trap')
BRANCH = re.compile(r'^b(ra|sr|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)')
DBCC = re.compile(r'^db')
# A compare sets Z from the comparison, not from the value left in the
# destination, so it never certifies a return value.
COMPARE = re.compile(r'^cmp')


def mnemonic(text):
    return text.split()[0].lower() if text else ''


def destination(text):
    """Last operand, in either operand syntax ("104(a1)" / "a1@(104)")."""
    parts = text.split(None, 1)
    if len(parts) < 2:
        return ''
    return parts[1].split(',')[-1].strip().lstrip('%')


def is_dreg0(operand):
    return operand in ('d0', 'd0:d1')


def touches_cc(text):
    m = mnemonic(text)
    if m.startswith(CC_TRANSPARENT) or BRANCH.match(m) or DBCC.match(m):
        return False
    # ADDQ/SUBQ with an address-register destination leaves the CCR alone
    # (68000 PRM: "if the destination is an address register, the condition
    # codes are not affected").
    if m.startswith(('addq', 'subq')):
        d = destination(text)
        if d.startswith('a') or d.startswith('sp'):
            return False
    return True


def certifies_d0(text):
    """Does this instruction set Z from the value it leaves in D0?"""
    return not COMPARE.match(mnemonic(text)) and is_dreg0(destination(text))


def functions(objdump, obj):
    """{symbol: [(addr, text), ...]} for every function in one object file."""
    try:
        out = subprocess.run([objdump, '-d', obj], capture_output=True,
                             text=True, check=True).stdout
    except (subprocess.CalledProcessError, OSError) as exc:
        sys.exit('isr-z-check: %s -d %s failed: %s' % (objdump, obj, exc))
    found, name, body = {}, None, []
    for line in out.splitlines():
        m = SYM_ANGLE.match(line) or SYM_PLAIN.match(line)
        if m:
            if name:
                found[name] = body
            name, body = m.group(1), []
            continue
        m = INSN.match(line)
        if m and name:
            body.append((m.group(1), m.group(2).strip()))
    if name:
        found[name] = body
    return found


def check(body):
    """Complaints about this function's exit paths, if any."""
    bad = []
    for i, (addr, text) in enumerate(body):
        if mnemonic(text) != 'rts':
            continue
        last = next((body[j] for j in range(i - 1, -1, -1)
                     if touches_cc(body[j][1])), None)
        if last is None:
            bad.append('  rts@%s: nothing in the function sets the CCR' % addr)
        elif not certifies_d0(last[1]):
            bad.append('  rts@%s: last CCR write is "%s" @%s, which does not '
                       'leave Z from the return value in d0' % (addr, last[1], last[0]))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--objdump', required=True)
    ap.add_argument('--objdir', required=True)
    ap.add_argument('symbols', nargs='+')
    args = ap.parse_args()

    objs = [os.path.join(root, f)
            for root, _, files in os.walk(args.objdir)
            for f in files if f.endswith(('.obj', '.o'))]
    if not objs:
        sys.exit('isr-z-check: no object files under %s' % args.objdir)

    seen, problems = {}, []
    for obj in objs:
        for name, body in functions(args.objdump, obj).items():
            bare = name.lstrip('_')
            if bare in args.symbols and bare not in seen:
                seen[bare] = obj
                bad = check(body)
                if bad:
                    problems.append('%s (%s):\n%s'
                                    % (bare, os.path.basename(obj), '\n'.join(bad)))

    missing = [s for s in args.symbols if s not in seen]
    if missing:
        sys.exit('isr-z-check: symbol(s) not found under %s: %s\n'
                 '  (an interrupt server must keep its symbol; check the name)'
                 % (args.objdir, ', '.join(missing)))

    if problems:
        sys.exit(
            'isr-z-check: interrupt server(s) may return the wrong Z flag.\n\n'
            + '\n\n'.join(problems) +
            '\n\nAn Exec interrupt server signals "handled, stop the chain" with Z\n'
            'clear and "not mine, keep walking" with Z set; the caller tests the\n'
            'flag, not D0.  A compiler epilogue ending in MOVE (rather than the\n'
            'CCR-transparent MOVEM) overwrites it with the restored register.\n'
            'Write the server in assembly -- see interrupt-chaining.md in\n'
            'emu68-gic400-library.\n')


if __name__ == '__main__':
    main()
