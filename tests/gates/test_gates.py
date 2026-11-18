# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""The code gates (emu68test/gates.py) on the harness's own C: the kv and logic libraries and LogicSelf."""
from pathlib import Path

import pytest

from emu68test.gates import Gates

pytestmark = pytest.mark.gates

COMPONENT = Path(__file__).resolve().parents[2]
TESTING = COMPONENT / "testing"
GATES = Gates(root=COMPONENT,
              sources=sorted([*TESTING.glob("src/*.c"), *TESTING.glob("selftest/*.c"),
                              *TESTING.glob("include/emu68test/*.h")]),
              allow_list=COMPONENT / "tests" / "complexity-allow.txt")


def test_every_function_of_the_harness_has_its_kernel_doc():
    assert GATES.kerneldoc_findings() == []


def test_every_function_of_the_harness_stays_within_the_complexity_limit():
    assert GATES.complexity_findings() == []
