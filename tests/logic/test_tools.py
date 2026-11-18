# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""emu68-common's check tools under vamos: memcheck (memmove, byte for byte) and prefscheck (prefs.h and
strfit()). vamos proves the m68k code; the JIT's part of memmove needs an Amiga."""
from __future__ import annotations

import re

import pytest

pytestmark = pytest.mark.logic


def test_memmove_holds_for_every_move_memcheck_makes(logic_program):
    done = logic_program("tools/memcheck").run_plain()
    assert done.returncode == 0, done.stdout[-500:]
    assert re.search(r"^\d+ moves held$", done.stdout, re.M)


def test_the_prefs_reader_and_strfit_pass_every_check(logic_program):
    done = logic_program("tools/prefscheck").run_plain()
    assert done.returncode == 0, done.stdout[-500:]
    assert re.search(r"^\d+ checks, 0 failed$", done.stdout, re.M)
