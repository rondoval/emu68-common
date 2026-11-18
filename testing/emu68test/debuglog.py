# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
debuglog.py - the debug log: Emu68's serial output (the drivers' Kprintf
lines, a profile build's perf reports, the test programs' marks), captured
into <rundir>/serial.log for the whole session by one reader thread. Tests
take byte ranges of it (marks) and search them.

Each machine has its own FTDI adapter, and whichever is attached is
/dev/ttyUSB0: the PI4's (0403:6001) a plain UART at 921600 8N1, the CM4's
(0403:6014) Emu68's fast serial, where the baud rate makes no difference. An
adapter reaches WSL through usbipd, and a stale node (after a detach) reads
as "No such device", which one re-attach fixes. Lines end in CR, some with a
stray NUL: both are stripped. Emu68 writes its own lines to the same UART;
when it and a driver write at once the bytes interleave and both lines are
lost (garbled()).
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import threading
import time
from pathlib import Path

from .results import expect as _expect, forbid as _forbid

BAUD = 921600
ADAPTERS = {"amiga-pi4": "0403:6001", "amiga-cm4": "0403:6014"}  # each machine's adapter, by its amimcp server


class DebugLog:
    """The capture of one session; ok says whether the port opened."""

    def __init__(self, path: Path, device: str = "/dev/ttyUSB0", usb_id: str | None = None, baud: int = BAUD):
        self.path = Path(path)
        self.device = device        # the serial node, or "none" for no capture
        self.usb_id = usb_id        # the adapter to attach when the node is missing; None: never attach
        self.baud = baud
        self.ok = False
        self.why = "not started"
        self._stop = False
        self._thread: threading.Thread | None = None

    def attach(self) -> None:
        """Attach the adapter to WSL again and wait for its node."""
        if self.usb_id is None:
            return
        exe = shutil.which("usbipd.exe") or "/mnt/c/Program Files/usbipd-win/usbipd.exe"
        try:
            subprocess.run([exe, "attach", "--wsl", "--hardware-id", self.usb_id], capture_output=True, timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            return
        for _ in range(20):
            if os.path.exists(self.device):
                return
            time.sleep(0.5)

    def _open(self):
        """The port, or None with self.why set."""
        try:
            import serial  # pyserial
        except ImportError:
            self.why = "pyserial is not installed"
            return None
        if not os.path.exists(self.device):
            self.attach()
        try:
            return serial.Serial(self.device, self.baud, timeout=0.2)
        except Exception as first:  # busy, missing, or a stale node left by a detach
            if "No such device" not in str(first):
                self.why = f"{self.device} not usable ({first})"
                return None
            self.attach()
            try:
                return serial.Serial(self.device, self.baud, timeout=0.2)
            except Exception as e:
                self.why = f"{self.device} not usable ({e})"
                return None

    def start(self) -> None:
        """Open the port and read it into the file until stop()."""
        if self.device == "none":
            self.why = "no serial device given"
            return
        port = self._open()
        if port is None:
            return
        self.ok = True
        self.why = ""
        out = open(self.path, "ab")

        def loop():
            while not self._stop:
                data = port.read(65536)
                if data:
                    out.write(data)
                    out.flush()
            port.close()
            out.close()

        self._thread = threading.Thread(target=loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """End the capture."""
        self._stop = True
        if self._thread:
            self._thread.join(timeout=2)

    def mark(self) -> int:
        """Where the log is now: the start of a slice."""
        return os.path.getsize(self.path) if self.path.exists() else 0

    def text(self, start: int = 0, end: int | None = None) -> str:
        """The log from @start (to @end), CRs and NULs stripped."""
        if not self.path.exists():
            return ""
        with open(self.path, "rb") as f:
            f.seek(start)
            data = f.read() if end is None else f.read(max(0, end - start))
        return data.replace(b"\0", b"").replace(b"\r", b"").decode("latin-1", "replace")

    def wait_for(self, regex: str, start: int, timeout: float) -> bool:
        """Until @regex (multi-line) appears in the log after @start, or @timeout passes."""
        t0 = time.time()
        while True:
            if re.search(regex, self.text(start), re.M):
                return True
            if time.time() - t0 > timeout:
                return False
            time.sleep(0.2)

    def settle(self, quiet: float = 0.5, limit: float = 3.0) -> None:
        """Wait until the log stops growing (lines still in flight when a command returns)."""
        t0, size = time.time(), self.mark()
        while time.time() - t0 < limit:
            time.sleep(quiet)
            now = self.mark()
            if now == size:
                return
            size = now

    def since(self, start: int) -> "Slice":
        """The log since @start, as something to search."""
        self.settle()
        return Slice(self.text(start))


def garbled(text: str) -> tuple[int, int]:
    """(lines with bytes outside ASCII, lines): Emu68 writes its own lines to the same UART, and when both sides write at
    once their bytes interleave; such a line is unreadable, and a line a check expects may be the one hit."""
    lines = text.splitlines()
    return sum(1 for line in lines if re.search(r"[^\x00-\x7f]", line)), len(lines)


class Slice:
    """A piece of the debug log."""

    def __init__(self, text: str):
        self.text = text

    def expect(self, regex: str, what: str) -> re.Match[str]:
        """The match of @regex in the slice; AssertionError in plain words when none (noting garbled lines)."""
        try:
            return _expect(self.text, regex, what)
        except AssertionError as e:
            bad, total = garbled(self.text)
            if bad:
                raise AssertionError(f"{e}\n({bad} of {total} lines in this slice are garbled: Emu68's own output "
                                     f"interleaved with the drivers', so the line may have been there)") from None
            raise

    def forbid(self, regex: str, what: str) -> None:
        """AssertionError in plain words when @regex matches in the slice."""
        _forbid(self.text, regex, what)

    def find(self, regex: str) -> list:
        """Every match of @regex (its groups) in the slice."""
        return re.findall(regex, self.text, re.M)

    def lines(self, regex: str) -> list[str]:
        """The lines matching @regex."""
        return [line for line in self.text.splitlines() if re.search(regex, line)]
