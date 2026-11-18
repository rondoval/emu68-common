# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
conftest.py - emu68-common's own suite: the emu68test package, taken from this
checkout (testing/), not from the install tree, so a change is tested before it
is installed.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

TESTS = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS.parent / "testing"))

pytest.register_assert_rewrite("emu68test")

from emu68test import plugin  # noqa: E402

plugin.configure(plugin.Suite(name="emu68-common", component="emu68-common", tests_dir=TESTS, ram="RAM:ec",
                              logic_dir=""))
pytest_plugins = ["emu68test.plugin"]
