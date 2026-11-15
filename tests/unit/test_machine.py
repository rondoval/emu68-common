# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""The machine (emu68test/machine.py) against a scripted agent: output names, kv programs, background
jobs, large uploads, the retry after a stall."""
import pytest

from emu68test import machine
from emu68test.machine import Device, ToolTimeout, Unreachable, amiga_file_part
from emu68test.results import RC_CHECK_FAILED, HarnessError

pytestmark = pytest.mark.unit

PASSED = "format=1\ncommand=query\nchecks=0\nchecks_failed=0\nPASS\n"
FAILED = "format=1\ncommand=query\nfail=no device\nchecks=0\nchecks_failed=0\nFAIL\n"
STATUS_RUNNING = "Process  4: Loaded as command: RAM:gt/GenetTest\n"


class Mod:
    """amimcp's exceptions."""

    class AmigaError(Exception):
        pass

    class AmigaUnreachable(Exception):
        pass


class Agent:
    """An agent that keeps files and runs commands from a table: command prefix -> (rc, text)."""

    def __init__(self):
        self.files: dict[str, bytes] = {}
        self.commands: list[str] = []
        self.answers: dict[str, tuple[int, str]] = {}
        self.unreachable = 0        # how many of the next calls find the agent gone
        self.pings = 0

    def _gone(self):
        if self.unreachable:
            self.unreachable -= 1
            raise Mod.AmigaUnreachable("no route to host")

    def exec_command(self, command, timeout=60):
        self._gone()
        self.commands.append(command)
        joined = command.split(" AS ")
        if command.startswith("Join "):
            self.files[joined[1]] = b"".join(self.files[part] for part in joined[0].split()[1:])
        for prefix, answer in self.answers.items():
            if command.startswith(prefix):
                return answer
        return 0, ""

    def write_file(self, remote, data):
        self._gone()
        self.files[remote] = data

    def read_range(self, remote, offset, length):
        self._gone()
        if remote not in self.files:
            raise Mod.AmigaError("object not found")
        return self.files[remote][offset:offset + length]

    def ping(self):
        self.pings += 1


@pytest.fixture
def agent(monkeypatch):
    monkeypatch.setattr(machine.time, "sleep", lambda seconds: None)
    return Agent()


@pytest.fixture
def amiga(agent, tmp_path):
    """A Device on the scripted agent (not named device: the plugin gives a test that uses one a test dir)."""
    return Device(Mod, agent, tmp_path, "RAM:gt", kv_programs=("GenetTest",))


def test_output_files_are_named_by_the_command_s_file_part(amiga):
    assert [amiga_file_part(c) for c in ("C:genet-stats", "RAM:gt/GenetTest", "Execute")] == \
        ["genet-stats", "GenetTest", "Execute"]
    amiga.test_name = "test_genet_stats[netdev]"
    out = amiga._out_name("C:genet-stats")
    assert out == "RAM:gt/out/test_genet_stats_netdev_-1-genet-stats.txt" and out.count(":") == 1


def test_a_kv_program_s_run_is_parsed_and_kept_in_the_test_s_dir(amiga, agent, tmp_path):
    amiga.artifacts = tmp_path
    agent.files["RAM:gt/out/session-1-GenetTest.txt"] = PASSED.encode()
    result = amiga.run("RAM:gt/GenetTest", "QUERY")
    assert result.passed and result.kv["command"] == "query"
    assert agent.commands == ["RAM:gt/GenetTest >RAM:gt/out/session-1-GenetTest.txt QUERY"]
    assert (tmp_path / "session-1-GenetTest.txt").read_text() == PASSED


def test_a_program_past_its_deadline_is_broken_and_its_output_reported(amiga, agent):
    agent.files["RAM:gt/out/session-1-GenetTest.txt"] = b"format=1\nhalf way\n"
    agent.break_command = lambda: None

    def still_running(command, timeout=60):
        if command.startswith("RAM:gt/GenetTest"):
            raise Mod.AmigaError("command still running after 5 s")
        return 0, ""

    agent.exec_command = still_running
    with pytest.raises(ToolTimeout, match="still running after 5 s \\(broken\\):\nformat=1\nhalf way"):
        amiga.run("RAM:gt/GenetTest", "MCAST 60", timeout=5)


def test_a_background_kv_job_ends_with_its_verdict_and_fails_as_a_failed_check(amiga, agent):
    job = amiga.start("RAM:gt/GenetTest", "EVENTS 30")
    assert agent.commands[-1] == "Run >NIL: RAM:gt/GenetTest >RAM:gt/out/session-1-GenetTest.txt EVENTS 30"
    assert not job.done()
    agent.files[job.out] = FAILED.encode()
    result = job.wait(10)
    assert job.done() and not result.passed and result.rc == RC_CHECK_FAILED and result.kv["fail"] == "no device"


def test_a_background_job_of_another_program_ends_when_its_process_is_gone(amiga, agent):
    agent.answers["Status FULL"] = (0, "Process  4: Loaded as command: C:genet-stats\n")
    job = amiga.start("C:genet-stats", "WATCH")
    assert not job.done() and amiga.procs("genet-stats") == [4]
    agent.answers["Status FULL"] = (0, "Process  1: Loaded as command: Shell\n")
    agent.files[job.out] = b"plain text\n"
    assert job.wait(10) == "plain text\n"


def test_a_job_that_ignores_ctrl_c_is_reported(amiga, agent):
    agent.answers["Status FULL"] = (0, STATUS_RUNNING)
    job = amiga.start("RAM:gt/GenetTest", "MCAST 600")
    with pytest.raises(AssertionError, match="GenetTest did not stop on Ctrl-C within 0 s"):
        job.stop(timeout=0)
    assert "Break 4 C" in agent.commands


def test_a_large_upload_goes_in_pieces_joined_on_the_amiga_and_is_read_back(amiga, agent, monkeypatch):
    monkeypatch.setattr(machine, "PIECE", 4)
    amiga.upload_bytes("RAM:gt/clip", b"0123456789")
    assert agent.files["RAM:gt/clip"] == b"0123456789"
    assert "Join RAM:gt/clip.part0 RAM:gt/clip.part4 RAM:gt/clip.part8 AS RAM:gt/clip" in agent.commands


def test_an_upload_that_reads_back_differently_is_a_harness_error(amiga, agent):
    agent.write_file = lambda remote, data: agent.files.__setitem__(remote, data[:-1])
    with pytest.raises(HarnessError, match="RAM:gt/tool: read back differs"):
        amiga.upload_bytes("RAM:gt/tool", b"abc")


def test_a_file_goes_up_once_per_boot(amiga, agent, tmp_path):
    local = tmp_path / "tool"
    local.write_bytes(b"abc")
    amiga.upload_once(local, "RAM:gt/tool")
    del agent.files["RAM:gt/tool"]
    amiga.upload_once(local, "RAM:gt/tool")
    assert "RAM:gt/tool" not in agent.files
    amiga.delete("RAM:gt/tool")
    amiga.upload_once(local, "RAM:gt/tool")
    assert agent.files["RAM:gt/tool"] == b"abc"


def test_a_command_is_tried_once_more_after_the_agent_comes_back(amiga, agent):
    agent.unreachable = 1
    assert amiga.exec("Version") == (0, "") and agent.pings == 1


def test_an_agent_that_stays_gone_ends_the_run(amiga, agent):
    agent.unreachable = 2
    with pytest.raises(Unreachable, match="the agent is unreachable"):
        amiga.exec("Version")


def test_the_ram_drawers_are_the_suite_s_own(amiga, agent):
    amiga.ram_dirs("bin")
    assert agent.commands == ["MakeDir >NIL: RAM:gt", "MakeDir >NIL: RAM:gt/out", "MakeDir >NIL: RAM:gt/bin"]
