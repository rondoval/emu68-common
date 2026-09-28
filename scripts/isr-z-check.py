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

So: slice each server out of the LINKED module and walk back from every `rts`
to the last instruction that touches the CCR.  That instruction must be the one
that left the return value in D0.

Finding the server is the awkward part.  AmigaOS HUNK carries no symbol table,
the modules link -s, and most servers are `static`, so neither the binary nor
the map names them.  The anchor is a SECTION instead: EMU68_INTSERVER() (see
emu68-common/include/intserver.h) gives each server a section of its own named
after it, and the linker map lists an input section by name with its address and
its size whatever the symbol's linkage -- and a section attribute survives LTO,
so this works with LTO on or off, always against the bytes that ship.

Usage: isr-z-check.py --objdump <objdump> --binary <module> --map <module.map>
                      SYMBOL [SYMBOL ...]
"""

import argparse
import re
import subprocess
import sys

# Linker-map input-section lines.  GNU ld wraps the first column at 16
# characters, and ".text.isr." is already 10, so the wrapped two-line form is
# the normal case here rather than an edge case:
#
#      .text.isr.nvme_int_isr
#                     0x0000fbc0       0x8c .../irq.c.obj
SEC = r'\.text\.isr\.(\S+)'
SEC_INLINE = re.compile(r'^ ' + SEC + r'\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S')
SEC_WRAPPED = re.compile(r'^ ' + SEC + r'\s*$')
ADDR_SIZE = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S')
# A symbol inside the section; linker assignments ("_endOfCode = .") are not one.
SYMBOL = re.compile(r'^\s+0x[0-9a-f]+\s+([A-Za-z_.$][^\s=]*)\s*$')

INSN = re.compile(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\t(.*)$')

# Mnemonics come in Motorola ("movea.l", "bne.s") or MIT ("moveal", "bnes")
# spelling depending on the binutils build, so match on a prefix.
CC_TRANSPARENT = ('movem', 'movea', 'lea', 'pea', 'jmp', 'rts',
                  'rte', 'rtr', 'nop', 'link', 'unlk', 'exg', 'adda', 'suba',
                  'reset', 'stop', 'trap')
BRANCH = re.compile(r'^b(ra|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)')
DBCC = re.compile(r'^db')
# A compare sets Z from the comparison, not from the value left in the
# destination, so it never certifies a return value.
COMPARE = re.compile(r'^cmp')
# jsr/bsr are deliberately NOT cc-transparent: the callee's own epilogue is the
# last thing to touch the CCR, and it is not this function's to promise.
CALL = re.compile(r'^(jsr|bsr)')
# Control transfers that do not come back.  jmp/bra leaving the server's own
# section is a tail call, and then the CALLEE's epilogue is what reaches Exec.
TRANSFER = re.compile(r'^(bra|jra|jmp)')


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
    if CALL.match(m):
        return True
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
    if CALL.match(mnemonic(text)):
        return False
    return not COMPARE.match(mnemonic(text)) and is_dreg0(destination(text))


def transfer_target(text):
    """Absolute target of a non-returning control transfer, or None if computed."""
    hits = re.findall(r'0x([0-9a-f]+)', text)
    return int(hits[-1], 16) if hits else None


def sections(map_path):
    """{name: {'vma', 'size', 'syms'}} for every .text.isr.* input section."""
    found, cur, pending = {}, None, None

    def start(name, vma, size):
        found[name] = {'vma': int(vma, 16), 'size': int(size, 16), 'syms': []}
        return found[name]

    try:
        lines = open(map_path, encoding='utf-8', errors='replace').read().splitlines()
    except OSError as exc:
        sys.exit('isr-z-check: cannot read the linker map %s: %s\n'
                 '  emu68_module_layout() emits it with -Wl,-Map; the check needs it\n'
                 '  because HUNK has no symbol table.' % (map_path, exc))

    for line in lines:
        m = SEC_INLINE.match(line)
        if m:
            cur, pending = start(*m.groups()), None
            continue
        m = SEC_WRAPPED.match(line)
        if m:
            cur, pending = None, m.group(1)
            continue
        if pending:
            m = ADDR_SIZE.match(line)
            cur = start(pending, *m.groups()) if m else None
            pending = None
            continue
        if cur is not None:
            m = SYMBOL.match(line)
            if m:
                cur['syms'].append(m.group(1))
            else:
                cur = None
    return found


def disassemble(objdump, binary, vma, size):
    """[(addr, text), ...] for one slice of the linked module.

    -b amiga: objdump reads the hunk format natively, so the addresses line up
    with the map's without any header arithmetic.  -m m68k is the full
    instruction set; the format's own default mach is a bare 68000 and renders
    anything newer (extb.l, say) as an undecoded .short.
    """
    cmd = [objdump, '-D', '-b', 'amiga', '-m', 'm68k',
           '--start-address=0x%x' % vma, '--stop-address=0x%x' % (vma + size),
           binary]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, OSError) as exc:
        sys.exit('isr-z-check: %s failed: %s' % (' '.join(cmd), exc))
    return [(m.group(1), m.group(2).strip())
            for m in (INSN.match(l) for l in out.splitlines()) if m]


def check(body, lo, hi):
    """Complaints about this server's exit paths, if any."""
    bad = []
    for i, (addr, text) in enumerate(body):
        m = mnemonic(text)

        if TRANSFER.match(m) or BRANCH.match(m):
            target = transfer_target(text)
            if target is None:
                bad.append('  @%s: "%s" jumps somewhere this check cannot resolve, '
                           'so the exit path is unverifiable' % (addr, text))
            elif not lo <= target < hi:
                bad.append('  @%s: "%s" leaves the server (tail call) - the CALLEE\'s '
                           'epilogue is what reaches Exec, not this one' % (addr, text))

        if m != 'rts':
            continue
        last = next((body[j] for j in range(i - 1, -1, -1)
                     if touches_cc(body[j][1])), None)
        if last is None:
            bad.append('  rts@%s: nothing in the function sets the CCR' % addr)
        elif CALL.match(mnemonic(last[1])):
            bad.append('  rts@%s: the last thing to touch the CCR is the call "%s" @%s - '
                       'the flag Exec sees comes from the CALLEE\'s epilogue'
                       % (addr, last[1], last[0]))
        elif not certifies_d0(last[1]):
            bad.append('  rts@%s: last CCR write is "%s" @%s, which does not '
                       'leave Z from the return value in d0' % (addr, last[1], last[0]))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--objdump', required=True)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--map', required=True, dest='mapfile')
    ap.add_argument('symbols', nargs='+')
    args = ap.parse_args()

    found = sections(args.mapfile)

    missing = [s for s in args.symbols if s not in found]
    if missing:
        sys.exit('isr-z-check: no .text.isr.<name> section in %s for: %s\n'
                 '  An interrupt server declares itself with EMU68_INTSERVER(<name>)\n'
                 '  from <intserver.h>, and the name in it must match the function.\n'
                 '  Note LTO targets build with -Wno-attributes, so a misspelled\n'
                 '  section(...) is silently a no-op - this error is the only signal.'
                 % (args.mapfile, ', '.join(missing)))

    extra = [s for s in found if s not in args.symbols]
    if extra:
        sys.exit('isr-z-check: %s carries interrupt server(s) the build does not know '
                 'about: %s\n'
                 '  Every EMU68_INTSERVER() must be listed in emu68_isr_z_check(... '
                 'SERVERS ...),\n  otherwise it ships unchecked.'
                 % (args.binary, ', '.join(sorted(extra))))

    problems = []
    for name in args.symbols:
        sec = found[name]
        lo, hi = sec['vma'], sec['vma'] + sec['size']
        if len(sec['syms']) > 1:
            problems.append('%s: .text.isr.%s holds more than one function (%s).\n'
                            '  A clone landed in the server\'s section, so the range '
                            'checked below is not just the server.'
                            % (name, name, ', '.join(sec['syms'])))
            continue
        body = disassemble(args.objdump, args.binary, lo, sec['size'])
        if not body:
            problems.append('%s: nothing disassembled at 0x%x+0x%x' % (name, lo, sec['size']))
            continue
        bad = check(body, lo, hi)
        if bad:
            problems.append('%s (0x%x..0x%x):\n%s\n\n  the whole server:\n%s'
                            % (name, lo, hi, '\n'.join(bad),
                               '\n'.join('    %8s  %s' % (a, t) for a, t in body)))

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
