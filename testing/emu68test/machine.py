# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
machine.py - the Amiga under test, reached through amimcp's Python client (its
server/amiga.py): commands, program runs, uploads with read-back, background
jobs, detached scripts, reboots.

The agent serves nothing while a command is within its deadline, so anything
longer than a minute is started with Run >NIL: and polled (Job). What takes
the agent's own network down runs as a detached script (script()) that writes
a RAM: log ending in a marker, and the host polls that log until the agent
answers again with the marker in it.

Everything temporary lives in the suite's RAM: drawer (Device.ram), the
programs' outputs in its out drawer. A reboot empties RAM:, so the device
forgets what it uploaded (ram_known) and it goes up again on its next use.
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from .results import RC_CHECK_FAILED, RC_PASS, HarnessError, Result, parse

PIECE = 12 << 20            # one agent frame carries at most 16 MiB: a larger file goes in pieces, joined on the Amiga
READ_PIECE = 4 << 20        # a download's pieces
AGENT_STALL_S = 180         # how long a stalled agent is waited for before the run ends
BOOT_SETTLE_S = 10          # Workbench and its startup, after the agent answers again
TAIL = 1200                 # how much of a program's output a timeout's message carries
VERDICT_RE = re.compile(r"^(PASS|FAIL)\s*$", re.M)
PASS_RE = re.compile(r"^PASS\s*$", re.M)


def amiga_file_part(path: str) -> str:
    """The last component of an AmigaDOS path, after its volume and drawers (as dos FilePart() does)."""
    return re.split(r"[/:]", path)[-1]


class Unreachable(HarnessError):
    """The agent did not answer, even after waiting for it: the run cannot go on."""


class ToolTimeout(AssertionError):
    """A program ran past its deadline and was broken; the message carries what it had printed."""


def client(amimcp: str, server: str):
    """amimcp's module and a connected client for @server (address and token from AMIGA_*, else ~/.claude.json)."""
    sys.path.append(os.path.join(amimcp, "server"))  # last: its png.py must not hide the pypng package
    import amiga  # noqa: E402

    host = os.environ.get("AMIGA_HOST")
    token = os.environ.get("AMIGA_TOKEN", "")
    port = int(os.environ.get("AMIGA_PORT", "7846"))
    if not host:
        env = json.load(open(os.path.expanduser("~/.claude.json")))["mcpServers"][server]["env"]
        host, token, port = env["AMIGA_HOST"], env.get("AMIGA_TOKEN", ""), int(env.get("AMIGA_PORT", "7846"))
    return amiga, amiga.Amiga(host=host, port=port, token=token, timeout=120.0)


@dataclass
class Job:
    """A program started with Run >NIL:, its output in a RAM: file."""

    device: "Device"
    tool: str               # the program's name: its command's file part
    out: str                # its output file
    mark: int = 0           # where the debug log was when it started
    started: float = field(default_factory=time.time)

    def output(self) -> str:
        """What the program printed; '' while it runs (the shell's redirection holds the file)."""
        return self.device.download_text(self.out)

    def done(self) -> bool:
        """Whether the program has ended: its verdict is in the file (a kv program), or its process is gone."""
        if self.device.speaks_kv(self.tool):
            return bool(VERDICT_RE.search(self.output()))
        return not self.device.procs(self.tool)

    def wait(self, timeout: float) -> Result | str:
        """Poll until the program ends: its Result (a kv program) or its text. ToolTimeout after @timeout.

        Run >NIL: loses the return code, so a kv program's is read off its verdict: RC_PASS, or
        RC_CHECK_FAILED for FAIL whatever the cause.
        """
        t0 = time.time()
        while not self.done():
            if time.time() - t0 > timeout:
                self.stop()
                raise ToolTimeout(f"{self.tool} still running after {timeout} s:\n{self.output()[-TAIL:]}")
            time.sleep(1)
        text = self.output()
        self.device.log_settle(self.mark, self.tool)
        self.device.keep(self.out, text)
        if not self.device.speaks_kv(self.tool):
            return text
        return parse(text, RC_PASS if PASS_RE.search(text) else RC_CHECK_FAILED)

    def stop(self, timeout: float = 20) -> None:
        """Ctrl-C to the program (its process found by its command name) and wait for it to end."""
        self.device.break_all(self.tool, timeout)
        if self.device.procs(self.tool):
            raise AssertionError(f"{self.tool} did not stop on Ctrl-C within {timeout} s")


class Device:
    """One Amiga, through the agent."""

    def __init__(self, mod, ami, rundir: Path, ram: str, kv_programs: tuple[str, ...] = (), capture=None,
                 end_marks: bool = False, boot_settle_s: float = BOOT_SETTLE_S):
        self.mod = mod                      # amimcp's amiga module: its exceptions
        self.ami = ami                      # the connected client
        self.rundir = Path(rundir)
        self.ram = ram                      # the suite's drawer in RAM:, e.g. RAM:gt
        self.out = f"{ram}/out"             # the programs' output files
        self.kv_programs = tuple(name.lower() for name in kv_programs)  # the programs that print the grammar
        self.capture = capture              # the session's DebugLog, or None
        self.end_marks = end_marks          # the kv programs mark their runs in the debug log (kv_debug_log())
        self.boot_settle_s = boot_settle_s
        self.ram_known: set[str] = set()    # what went up to RAM: since the last boot
        self.test_name = "session"          # the running test, for the output files' names
        self.artifacts: Path | None = None  # the running test's dir, where outputs are kept
        self.runs = 0

    # -- commands

    def exec(self, cmd: str, timeout: float = 60) -> tuple[int, str]:
        """An AmigaDOS command whose output the agent collects; once more after the agent comes back from a stall."""
        try:
            return self.ami.exec_command(cmd, timeout=timeout)
        except (self.mod.AmigaUnreachable, OSError):
            self.wait_agent(AGENT_STALL_S)
            try:
                return self.ami.exec_command(cmd, timeout=timeout)
            except (self.mod.AmigaUnreachable, OSError) as e:
                raise Unreachable(f"the agent is unreachable ({e})") from None

    def speaks_kv(self, tool: str) -> bool:
        """Whether @tool (a program's name) prints the grammar."""
        return tool.lower() in self.kv_programs

    def _out_name(self, command: str) -> str:
        """The next output file of this test: <ram>/out/<test>-<n>-<tool>.txt, <tool> being the command's
        AmigaDOS file part (C:genet-stats -> genet-stats: a second colon makes no file name)."""
        self.runs += 1
        safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", self.test_name)[:40]
        return f"{self.out}/{safe}-{self.runs}-{amiga_file_part(command)}.txt"

    def keep(self, remote: str, text: str) -> None:
        """A program's output into the test's artifact dir."""
        if self.artifacts is not None:
            (self.artifacts / remote.split("/")[-1]).write_text(text, encoding="latin-1")

    def log_mark(self) -> int:
        """Where the debug log is now; 0 without one."""
        return self.capture.mark() if self.capture is not None else 0

    def log_settle(self, mark: int, tool: str) -> None:
        """Wait (3 s at most) for a kv program's end mark in the debug log, so the test's slice is complete."""
        if self.end_marks and self.capture is not None and self.capture.ok and self.speaks_kv(tool):
            self.capture.wait_for(rf"^\[{re.escape(tool)}\] end rc=", mark, 3.0)

    def run_text(self, command: str, args: str = "", timeout: float = 120) -> tuple[int, str]:
        """@command run to its end, its output redirected to a RAM: file and read back: (rc, text)."""
        out = self._out_name(command)
        mark = self.log_mark()
        try:
            rc, _ = self.exec(f"{command} >{out} {args}", timeout=timeout)
        except self.mod.AmigaError as e:
            if "still running" not in str(e):
                raise
            try:
                self.ami.break_command()
            except self.mod.AmigaError:
                pass
            self.reap()
            text = self.download_text(out)
            self.keep(out, text)
            raise ToolTimeout(f"{command} still running after {timeout} s (broken):\n{text[-TAIL:]}") from None
        text = self.download_text(out)
        self.log_settle(mark, amiga_file_part(command))
        self.keep(out, text)
        return rc, text

    def run(self, command: str, args: str = "", timeout: float = 120) -> Result:
        """A kv program run to its end, its output parsed."""
        rc, text = self.run_text(command, args, timeout)
        return parse(text, rc)

    def start(self, command: str, args: str = "") -> Job:
        """@command started with Run >NIL:, its output in a RAM: file: a Job to wait for or stop.

        The shell's redirection holds the file until the program ends, so nothing can read a running
        job's output: done() reads it once the program is gone, and stop() finds the process by its
        command name.
        """
        out = self._out_name(command)
        mark = self.log_mark()
        self.exec(f"Run >NIL: {command} >{out} {args}", timeout=15)
        return Job(self, amiga_file_part(command), out, mark)

    def reap(self) -> None:
        """Wait for a broken command to be reaped: the next EXEC is refused while it lives."""
        for _ in range(30):
            time.sleep(1)
            try:
                self.ami.exec_command("Echo >NIL: reaped", timeout=5)
                return
            except self.mod.AmigaError:
                continue

    def procs(self, name: str) -> list[int]:
        """The CLI numbers of the processes whose command is @name, by any path (Status FULL)."""
        _, text = self.exec("Status FULL", timeout=15)
        found = []
        for num, command in re.findall(r"^Process\s+(\d+):.*Loaded as command: (.*)$", text, re.M):
            if amiga_file_part(command.strip()).lower() == name.lower():
                found.append(int(num))
        return found

    def break_all(self, name: str, timeout: float = 15) -> None:
        """Ctrl-C to every process of @name and wait for them to go."""
        for n in self.procs(name):
            self.exec(f"Break {n} C", timeout=10)
        t0 = time.time()
        while self.procs(name) and time.time() - t0 < timeout:
            time.sleep(1)

    def script(self, name: str, body: str, marker: str, limit: float) -> str:
        """@body as <ram>/<name>, run detached; its log <ram>/<name>.log polled until it holds @marker.

        For a script that takes the agent's network down and up again: the agent
        is unreachable part of the time, and polling waits it out. The script
        must append everything to the log and echo @marker last.
        """
        remote, log = f"{self.ram}/{name}", f"{self.ram}/{name}.log"
        self.upload_bytes(remote, body.encode("latin-1"))
        self.exec(f"Delete >NIL: {log} QUIET", timeout=20)
        self.exec(f"Run >NIL: Execute {remote}", timeout=20)
        t0, text = time.time(), ""
        while time.time() - t0 < limit:
            time.sleep(5)
            try:
                text = self.ami.read_file(log).decode("latin-1", "replace")
            except (self.mod.AmigaError, self.mod.AmigaUnreachable, OSError):
                continue
            if marker in text:
                break
        self.keep(log, text)
        if marker not in text:
            raise ToolTimeout(f"{name} did not reach {marker!r} within {limit} s:\n{text[-TAIL:]}")
        return text

    # -- files

    def retry(self, fn):
        """@fn (a call or two on the agent's client), once more after the agent answers again: an agent that
        stalls once costs a retry, not the test."""
        try:
            return fn()
        except (self.mod.AmigaUnreachable, OSError):
            self.wait_agent(AGENT_STALL_S)
            return fn()

    def download(self, remote: str) -> bytes:
        """A file from the Amiga, in pieces."""
        def once():
            data = bytearray()
            while True:
                chunk = self.ami.read_range(remote, len(data), READ_PIECE)
                data += chunk
                if len(chunk) < READ_PIECE:
                    return bytes(data)
        return self.retry(once)

    def download_text(self, remote: str) -> str:
        """A text file's content, '' when it does not exist or is held open."""
        try:
            return self.download(remote).decode("latin-1", "replace")
        except self.mod.AmigaError:
            return ""

    def read_range(self, remote: str, offset: int, length: int) -> bytes:
        """@length bytes of a file from @offset."""
        return self.retry(lambda: self.ami.read_range(remote, offset, length))

    def upload_bytes(self, remote: str, data: bytes) -> None:
        """@data to @remote, in pieces joined on the Amiga when large, read back and compared."""
        if len(data) <= PIECE:
            self.retry(lambda: self.ami.write_file(remote, data))
        else:
            parts = [f"{remote}.part{at}" for at in range(0, len(data), PIECE)]
            for part, at in zip(parts, range(0, len(data), PIECE)):
                self.retry(lambda part=part, at=at: self.ami.write_file(part, data[at:at + PIECE]))
            rc, text = self.exec(f"Join {' '.join(parts)} AS {remote}", timeout=60)
            self.exec(f"Delete >NIL: {' '.join(parts)} QUIET", timeout=30)
            if rc != 0:
                raise HarnessError(f"{remote}: Join failed ({rc}): {text.strip()}")
        if self.download(remote) != data:
            raise HarnessError(f"{remote}: read back differs")

    def upload(self, local: Path, remote: str) -> None:
        """A local file to the Amiga."""
        self.upload_bytes(remote, Path(local).read_bytes())
        self.ram_known.add(remote)

    def upload_once(self, local: Path, remote: str) -> None:
        """A local file to RAM:, unless it went up since the last boot."""
        if remote not in self.ram_known:
            self.upload(local, remote)

    def delete(self, *remotes: str) -> None:
        """Files on the Amiga, quietly."""
        if remotes:
            self.exec(f"Delete >NIL: {' '.join(remotes)} QUIET", timeout=30)
        for remote in remotes:
            self.ram_known.discard(remote)

    def version_of(self, remote: str) -> str:
        """The version Version FULL reports for a file, '' when none."""
        rc, text = self.exec(f"Version {remote} FULL", timeout=20)
        return text.strip() if rc == 0 else ""

    def size_of(self, remote: str) -> int:
        """A file's size (List), -1 when missing."""
        rc, text = self.exec(f"List {remote} LFORMAT=%L", timeout=20)
        found = re.search(r"(\d+)", text) if rc == 0 else None
        return int(found.group(1)) if found else -1

    # -- the machine

    def ram_dirs(self, *more: str) -> None:
        """The suite's RAM: drawer with its out drawer, and @more drawers inside it."""
        for drawer in (self.ram, self.out, *(f"{self.ram}/{name}" for name in more)):
            self.exec(f"MakeDir >NIL: {drawer}", timeout=15)

    def _after_boot(self) -> None:
        """What a boot leaves: the startup waited out, RAM: forgotten, the drawers made again."""
        time.sleep(self.boot_settle_s)
        self.wait_agent(AGENT_STALL_S)
        self.ram_known.clear()
        self.ram_dirs()

    def reboot(self) -> None:
        """C:Reboot (the 68k only), wait for the agent, forget RAM:."""
        mark = f"{self.ram}-reboot-mark"
        self.ami.write_file(mark, b"1")
        try:
            rc, text = self.ami.exec_command("C:Reboot", timeout=3)
            raise HarnessError(f"C:Reboot returned ({rc}): {text.strip()}")  # a reboot never returns
        except HarnessError:
            raise
        except Exception:
            pass  # the agent goes down with the reboot
        t0 = time.time()
        while time.time() - t0 < 60:  # a reboot clears RAM: and may be back within a ping's timeout: ask RAM:, not the agent
            try:
                self.ami.read_file(mark)
                time.sleep(1)
            except Exception:
                break
        else:
            raise HarnessError("C:Reboot: the Amiga never rebooted")
        self.wait_agent(300)
        self._after_boot()

    def wait_agent(self, timeout: float) -> None:
        """Until the agent answers a ping."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                self.ami.ping()
                return
            except Exception:
                time.sleep(3)
        raise Unreachable(f"the agent did not come back in {timeout} s")

    def wait_reset(self, timeout: float) -> bool:
        """Until someone resets the machine and the agent answers again, RAM: forgotten: TRUE when that happened.

        Only a reset empties RAM:; an agent that does not answer proves
        nothing, it may be stalled.
        """
        mark = f"{self.ram}-reset-mark"
        self.ami.write_file(mark, b"1")
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                self.ami.read_file(mark)
            except self.mod.AmigaError:  # the agent answered, and the mark is gone
                self._after_boot()
                return True
            except (self.mod.AmigaUnreachable, OSError):
                pass
            time.sleep(2)
        return False

    def flush(self) -> None:
        """Avail FLUSH twice: expunge what nobody holds (a library of another build still in memory)."""
        self.exec("Avail >NIL: FLUSH", timeout=30)
        time.sleep(1)
        self.exec("Avail >NIL: FLUSH", timeout=30)
