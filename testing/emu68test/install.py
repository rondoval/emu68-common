# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
install.py - the build under test into its real places on the Amiga, the
machine's own files kept on the host first.

The first run against a machine copies each file it would replace into the
suite's tests/.machine/<server>/ and never overwrites those copies; restore()
puts them back. A passed build stays installed, so the next run replaces the
previous build, not the machine's original.

PFS3 delays its writes: a file that read back correctly was lost by a reboot a
second later. So an install waits, reads back, reboots and reads back again;
what the reboot lost goes up once more with a longer wait before it is an
error.
"""
from __future__ import annotations

import hashlib
import re
import time
from dataclasses import dataclass
from pathlib import Path

from .results import HarnessError

PFS3_SETTLE_S = (20, 60)        # the wait before the reboot: the first attempt's, then the second's


def digest(data: bytes) -> str:
    """A short fingerprint of @data, for the report."""
    return hashlib.sha256(data).hexdigest()[:16]


def backup_name(remote: str) -> str:
    """The host-side file name of @remote's original."""
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", remote)


@dataclass
class Placed:
    """One file the install wrote, or found already in place."""

    remote: str
    changed: bool
    digest: str
    version: str = ""


class Installer:
    """The machine's originals and the install of the build under test."""

    def __init__(self, device, backup_dir: Path):
        self.device = device
        self.backup_dir = Path(backup_dir)

    def original(self, remote: str) -> bytes | None:
        """The machine's own @remote as first seen; None when the machine had none."""
        path = self.backup_dir / backup_name(remote)
        return path.read_bytes() if path.exists() else None

    def keep_originals(self, remotes: list[str]) -> None:
        """Copy each of @remotes to the host once, before anything replaces it."""
        self.backup_dir.mkdir(parents=True, exist_ok=True)
        for remote in remotes:
            path = self.backup_dir / backup_name(remote)
            absent = self.backup_dir / (backup_name(remote) + ".absent")
            if path.exists() or absent.exists():
                continue
            try:
                path.write_bytes(self.device.download(remote))
            except self.device.mod.AmigaError:
                absent.write_text("the machine had no such file\n")

    def differing(self, files: dict[str, bytes]) -> list[str]:
        """The remote paths of @files whose content on the Amiga is not the one given."""
        return [remote for remote, data in files.items() if self._current(remote) != data]

    def install(self, files: dict[str, bytes], reboot: bool = True) -> list[Placed]:
        """@files (remote path -> content) into place; a reboot when anything changed."""
        self.keep_originals(list(files))
        changed = self.differing(files)
        todo = changed
        for settle in PFS3_SETTLE_S:
            if not todo:
                break
            for remote in todo:
                self.device.upload_bytes(remote, files[remote])
            time.sleep(settle)
            lost = self.differing(files)
            if lost:
                raise HarnessError("did not read back before the reboot: " + ", ".join(lost))
            if not reboot:
                break
            self.device.reboot()
            todo = self.differing(files)
        else:
            if todo:
                raise HarnessError("lost in the reboot twice: " + ", ".join(todo))
        return [Placed(remote, remote in changed, digest(data), self.device.version_of(remote))
                for remote, data in files.items()]

    def restore(self, remotes: list[str]) -> list[Placed]:
        """The machine as it was for @remotes: its originals back, what it never had deleted; a reboot."""
        files, absent = {}, []
        for remote in remotes:
            path = self.backup_dir / backup_name(remote)
            if path.exists():
                files[remote] = path.read_bytes()
            elif (self.backup_dir / (backup_name(remote) + ".absent")).exists():
                absent.append(remote)
        if absent:
            self.device.delete(*absent)
        return self.install(files)

    def _current(self, remote: str) -> bytes | None:
        try:
            return self.device.download(remote)
        except self.device.mod.AmigaError:
            return None
