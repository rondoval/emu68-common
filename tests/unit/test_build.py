# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""The build's flavour (emu68test/build.py), the perf slots (perf.py), the report (report.py) and the
debug log (debuglog.py)."""
from pathlib import Path

import pytest

from emu68test import perf
from emu68test.baselines import Baselines
from emu68test.build import BuildInfo
from emu68test.debuglog import DebugLog, Slice, garbled
from emu68test.report import Report, Row

pytestmark = pytest.mark.unit

SAMPLES = Path(__file__).parent / "samples"
CACHE = ("EMU68_DEBUG_BACKEND:STRING=pistorm\nEMU68_TIER:STRING=off\n"
         "EMU68_PROFILE:STRING=emu68-genet-driver-netdev;lwip-amiga\nEMU68_DEBUG:STRING=\n"
         "EMU68_FORCE_LVO_CACHE_OPS:BOOL=OFF\nEMU68_VCHIQ_TEST_HOOKS:BOOL=ON\nEMU68_BUILD_STAMP:STRING=dev-1\n")


def test_a_component_pinned_to_profile_profiles_and_another_does_not(tmp_path):
    (tmp_path / "CMakeCache.txt").write_text(CACHE)
    genet = BuildInfo.read(tmp_path, "emu68-genet-driver-netdev")
    assert genet.profile and not genet.debug and genet.flavour == "rangeops" and genet.stamp == "dev-1"
    assert genet.option("EMU68_VCHIQ_TEST_HOOKS") and not genet.option("EMU68_NO_SUCH")
    assert not BuildInfo.read(tmp_path, "emu68-vchiq-library").profile
    assert genet.summary()["flavour"] == "rangeops"


def test_the_stack_s_tier_and_backend_decide_without_a_pin(tmp_path):
    (tmp_path / "CMakeCache.txt").write_text("EMU68_DEBUG_BACKEND:STRING=pistorm\nEMU68_TIER:STRING=debug\n"
                                             "EMU68_FORCE_LVO_CACHE_OPS:BOOL=ON\n")
    info = BuildInfo.read(tmp_path, "emu68-nvme-driver")
    assert info.debug and info.profile and info.flavour == "standard"
    (tmp_path / "CMakeCache.txt").write_text("EMU68_DEBUG_BACKEND:STRING=off\nEMU68_TIER:STRING=debug\n")
    assert not BuildInfo.read(tmp_path, "emu68-nvme-driver").debug


def test_no_build_is_unknown_throughout(tmp_path):
    info = BuildInfo.read(tmp_path / "none", "emu68-nvme-driver")
    assert (info.backend, info.tier) == ("?", "?") and not info.profile


def test_perf_totals_add_one_component_s_reports_up():
    text = (SAMPLES / "perf_serial.txt").read_text()
    slots = perf.totals(text, "genet")
    assert slots["rx_wake"].calls == 2124 + 489 and slots["rx_wake"].max_us == 7555
    assert slots["rx_reply"].avg_us == pytest.approx(49689 / 4448)
    assert "hist" not in slots and perf.totals(text, "videocore") == {}


def test_the_report_has_a_row_per_test_and_what_was_measured(tmp_path):
    report = Report(tmp_path, "thing.device tests")
    report.header.append("Replaced on the Amiga: DEVS:thing.device")
    report.add(Row("hw/test_a.py::test_one", "passed", 2.4, notes=["mbps: 940"]))
    report.add(Row("hw/test_a.py::test_two", "failed", 1.0, reason="assert 1 == 2\nmore", notes=["a | b"]))
    base = Baselines("amiga-pi4")
    base.check_near("decode.fps", 30.0, higher=True, tolerance=1)
    text = report.write(base).read_text()
    assert "2 tests in 0 s: 1 failed, 1 passed" in text and "Replaced on the Amiga" in text
    assert "| hw/test_a.py::test_one | passed | 2 | mbps: 940 |" in text
    assert "| hw/test_a.py::test_two | failed | 1 | assert 1 == 2; a / b |" in text
    assert "- decode.fps: no baseline for amiga-pi4" in text and "| decode.fps | 30.0 |" in text


def test_the_report_says_whether_the_debug_log_was_captured(tmp_path):
    log = DebugLog(tmp_path / "serial.log", "none")
    log.start()
    assert "Debug log: NOT captured (no serial device given)" in Report(tmp_path, "t").write(None, log).read_text()


def test_the_debug_log_is_read_by_marks_with_crs_and_nuls_stripped(tmp_path):
    log = DebugLog(tmp_path / "serial.log", "none")
    log.path.write_bytes(b"[vchiq] up\r\x00[VCTest] begin MARK\r")
    mark = log.mark()
    with open(log.path, "ab") as out:
        out.write(b"[VCTest] end rc=0\r\n")
    assert log.text(0, mark) == "[vchiq] up[VCTest] begin MARK" and log.text(mark) == "[VCTest] end rc=0\n"
    assert log.wait_for(r"end rc=(\d+)", mark, 0) and not log.wait_for(r"begin", mark, 0)


def test_a_slice_notes_garbled_lines_when_a_line_is_missing():
    text = "[vchiq] up\n[vc\xfe\xffq] sess\n"
    assert garbled(text) == (1, 2)
    piece = Slice(text)
    assert piece.lines(r"up$") == ["[vchiq] up"] and piece.find(r"^\[(\w+)\] up") == ["vchiq"]
    piece.forbid(r"error", "an error line")
    with pytest.raises(AssertionError, match="1 of 2 lines in this slice are garbled"):
        piece.expect(r"session \d+", "the session line")
