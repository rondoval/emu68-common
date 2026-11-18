# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""The kv and logic libraries (testing/src/) as LogicSelf runs them under vamos: every kind of fact, the
checks and their return codes, the fake exec and utility bases, the recorded calls."""
from __future__ import annotations

import pytest

from emu68test.results import RC_CHECK_FAILED, RC_ENVIRONMENT, RC_PASS, HarnessError
from emu68test.vamos import grouped

pytestmark = pytest.mark.logic

PROGRAM = "testing/LogicSelf"
TASK = 0x1000       # the task LogicSelf signals


@pytest.fixture
def run(logic_program):
    """LogicSelf run with the steps given."""
    return logic_program(PROGRAM).run


def calls(step: dict[str, str]) -> list[tuple[str, str]]:
    """The calls a step recorded, as (kind, value)."""
    return [(call["kind"], call["value"]) for call in grouped(step, "call")]


def test_every_kind_of_fact_prints_as_the_grammar_says(run):
    result = run("facts").result
    step = result.group("step")[0]
    assert result.kv["command"] == "self" and result.passed
    assert (step["text"], step["signed"], step["unsigned"]) == ("a name", "-3", "4294967295")
    assert (step["hex"], step["flag"], step["version"], step["rate"]) == ("0x00000029", "yes", "47.115", "85.3")
    assert result.kv["entry.1.name"] == "second" and result.prose == ["prose for people"]


def test_a_passed_check_passes_the_run(run):
    result = run("check=pass").result
    assert result.rc == RC_PASS and result.verdict == "PASS"
    assert result.check("the check asked for").detail == "asked to pass"
    assert (result.num("checks"), result.num("checks_failed")) == (1, 0)


def test_a_failed_check_fails_the_run_with_its_own_return_code(run):
    result = run("check=pass", "check=fail").result
    assert result.rc == RC_CHECK_FAILED and result.verdict == "FAIL"
    assert result.failures() == ["the check asked for: asked to fail"]
    assert (result.num("checks"), result.num("checks_failed")) == (2, 1)


def test_an_unknown_step_is_an_environment_failure_the_harness_names(run):
    done = run("frob")
    assert done.result.rc == RC_ENVIRONMENT
    with pytest.raises(HarnessError, match="frob is not a step"):
        done.steps()


def test_an_allocation_asked_to_fail_fails_once(run):
    refused, granted = run("nomem=1", "alloc=64", "alloc=64").steps()[1:]
    assert (refused["allocated"], granted["allocated"]) == ("no", "yes")


def test_signal_putmsg_and_replymsg_are_recorded_per_step(run):
    quiet, signalled, posted = run("alloc=8", "signal=0x100", "post").steps()
    assert calls(quiet) == []
    assert calls(signalled) == [("signal", "0x00000100")]
    assert [kind for kind, _ in calls(posted)] == ["putmsg", "reply"]
    assert (posted["taken"], posted["empty"], posted["replied"]) == ("yes", "yes", "yes")


def test_forbid_and_permit_count_their_nesting(run):
    assert [step["forbids"] for step in run("forbid=on", "forbid=on", "forbid=off", "forbid=off").steps()] == \
        ["1", "2", "1", "0"]


def test_the_fake_utility_library_searches_a_tag_list(run):
    found, missing = run("tag=2", "tag=7").steps()
    assert (found["data"], missing["data"]) == ("22", "99")


def test_a_program_names_its_own_calls(run):
    (step,) = run("own=5").steps()
    assert calls(step) == [("own", "own 5")]


def test_a_library_call_nobody_modelled_ends_the_run_naming_it(run):
    result = run("stray").result
    assert result.rc == RC_ENVIRONMENT and result.verdict == "FAIL"
    assert result.kv["fail"] == "exec.library LVO -552 called, which this program does not model"
