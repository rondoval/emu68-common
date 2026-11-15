# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""The install (emu68test/install.py): the machine's originals kept once, read-backs around the reboot, a
second attempt for what a reboot lost, the restore."""
import pytest

from emu68test import install
from emu68test.results import HarnessError

pytestmark = pytest.mark.unit

DRIVER = "DEVS:Networks/thing.device"
TOOL = "C:thing-stats"


class FakeDevice:
    """Files in a dict; a reboot that counts, and may lose what was written since the last one."""

    class mod:
        class AmigaError(Exception):
            pass

    def __init__(self, files, lose_writes=False, reboots_losing=0):
        self.files = dict(files)
        self.reboots = 0
        self.lose_writes = lose_writes          # an upload never lands
        self.reboots_losing = reboots_losing    # this many reboots lose the files written before them
        self.written: dict[str, bytes | None] = {}  # since the last reboot: path -> what it replaced

    def download(self, remote):
        if remote not in self.files:
            raise self.mod.AmigaError("object not found")
        return self.files[remote]

    def upload_bytes(self, remote, data):
        if self.lose_writes:
            return
        self.written.setdefault(remote, self.files.get(remote))
        self.files[remote] = data

    def delete(self, *remotes):
        for remote in remotes:
            self.files.pop(remote, None)

    def reboot(self):
        self.reboots += 1
        if self.reboots <= self.reboots_losing:
            for remote, before in self.written.items():
                if before is None:
                    self.files.pop(remote, None)
                else:
                    self.files[remote] = before
        self.written = {}

    def version_of(self, remote):
        return f"{remote} 5.0"


@pytest.fixture(autouse=True)
def quick(monkeypatch):
    monkeypatch.setattr(install, "PFS3_SETTLE_S", (0, 0))


def test_install_keeps_the_originals_once(tmp_path):
    device = FakeDevice({DRIVER: b"4.4"})
    installer = install.Installer(device, tmp_path)
    placed = installer.install({DRIVER: b"5.0", TOOL: b"stats"})
    assert [p.changed for p in placed] == [True, True] and device.reboots == 1
    assert placed[0].version == f"{DRIVER} 5.0" and placed[0].digest == install.digest(b"5.0")
    assert installer.original(DRIVER) == b"4.4" and installer.original(TOOL) is None
    installer.install({DRIVER: b"5.1"})
    assert installer.original(DRIVER) == b"4.4"


def test_install_of_what_is_there_does_not_reboot(tmp_path):
    device = FakeDevice({DRIVER: b"5.0"})
    placed = install.Installer(device, tmp_path).install({DRIVER: b"5.0"})
    assert not placed[0].changed and device.reboots == 0


def test_install_without_a_reboot_only_places_the_files(tmp_path):
    device = FakeDevice({})
    install.Installer(device, tmp_path).install({TOOL: b"stats"}, reboot=False)
    assert device.files == {TOOL: b"stats"} and device.reboots == 0


def test_install_refuses_to_reboot_over_a_write_that_did_not_read_back(tmp_path):
    device = FakeDevice({DRIVER: b"4.4"}, lose_writes=True)
    with pytest.raises(HarnessError, match="did not read back before the reboot"):
        install.Installer(device, tmp_path).install({DRIVER: b"5.0"})
    assert device.reboots == 0


def test_what_one_reboot_lost_goes_up_again(tmp_path):
    device = FakeDevice({DRIVER: b"4.4"}, reboots_losing=1)
    placed = install.Installer(device, tmp_path).install({DRIVER: b"5.0", TOOL: b"stats"})
    assert device.files == {DRIVER: b"5.0", TOOL: b"stats"} and device.reboots == 2
    assert [p.changed for p in placed] == [True, True]


def test_what_two_reboots_lost_is_an_error(tmp_path):
    device = FakeDevice({DRIVER: b"4.4"}, reboots_losing=2)
    with pytest.raises(HarnessError, match="lost in the reboot twice: DEVS:Networks/thing.device"):
        install.Installer(device, tmp_path).install({DRIVER: b"5.0"})


def test_differing_names_what_is_not_the_build_s(tmp_path):
    installer = install.Installer(FakeDevice({DRIVER: b"5.0"}), tmp_path)
    assert installer.differing({DRIVER: b"5.0", TOOL: b"stats"}) == [TOOL]


def test_restore_puts_back_what_the_machine_had(tmp_path):
    device = FakeDevice({DRIVER: b"4.4"})
    installer = install.Installer(device, tmp_path)
    installer.install({DRIVER: b"5.0", TOOL: b"stats"})
    installer.restore([DRIVER, TOOL])
    assert device.files == {DRIVER: b"4.4"} and device.reboots == 2
