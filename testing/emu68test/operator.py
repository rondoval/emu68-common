# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
operator.py - someone at the machine, for the tests that need hands or ears: a
cable pulled, a sound judged. Asked through the terminal, past pytest's
capture; a test that needs the operator is skipped without one.
"""
from __future__ import annotations

import sys

import pytest


class Operator:
    """The person at the machine."""

    def __init__(self, enabled: bool, capmanager, why_not: str = "needs someone at the machine"):
        self.enabled = enabled          # the run was started with the option that says someone is there
        self.capmanager = capmanager    # pytest's capture manager, suspended while asking
        self.why_not = why_not          # the skip reason without one: names the suite's option

    def say(self, text: str) -> None:
        """A line for the operator, past pytest's capture."""
        with self.capmanager.global_and_fixture_disabled():
            print(f"\n>>> {text}", flush=True)

    def ask(self, question: str) -> None:
        """A yes/no question; n fails the test, s skips it."""
        if not self.enabled:
            pytest.skip(self.why_not)
        if not sys.stdin.isatty():
            pytest.skip("no terminal to ask the operator")
        with self.capmanager.global_and_fixture_disabled():
            answer = input(f"\n>>> {question} [y/n/s] ").strip().lower()
        if answer == "s":
            pytest.skip("the operator skipped it")
        if answer != "y":
            pytest.fail(f"the operator said no: {question}")
