# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""Baselines (emu68test/baselines.py): a figure against its bound or near its median, recording, the file."""
import json

import pytest

from emu68test.baselines import Baseline, Baselines, write

pytestmark = pytest.mark.unit


def test_a_figure_must_reach_the_worst_seeding_boot():
    base = Baselines("amiga-pi4", {"netdev.tcp_tx.mbps": Baseline(940.1, 928.8, 9, 27)})
    base.check("netdev.tcp_tx.mbps", 928.8, higher=True)
    with pytest.raises(AssertionError, match="below 928.8, the worst of 9 seeding boots"):
        base.check("netdev.tcp_tx.mbps", 928.7, higher=True)


def test_lower_is_better_against_the_highest_seeding_boot():
    base = Baselines("amiga-pi4", {"sana2.rr_1.p50_us": Baseline(902, 902, 4, 12)})
    base.check("sana2.rr_1.p50_us", 902, higher=False)
    with pytest.raises(AssertionError, match="above 902"):
        base.check("sana2.rr_1.p50_us", 903, higher=False)


def test_a_figure_without_a_bound_cannot_be_checked_against_one():
    base = Baselines("amiga-pi4", {"netdev.tcp_tx.ctxsw_s": Baseline(4250.0, None, 6, 18)})
    with pytest.raises(ValueError, match="no bound"):
        base.check("netdev.tcp_tx.ctxsw_s", 5000.0, higher=False)


def test_near_the_median_within_a_tolerance_either_way():
    base = Baselines("amiga-pi4", {"tx_submit.avg_us": Baseline(0.9), "decode.fps": Baseline(30.0)})
    base.check_near("tx_submit.avg_us", 1.05, higher=False, tolerance="20%")
    base.check_near("tx_submit.avg_us", 0.1, higher=False, tolerance="20%")
    with pytest.raises(AssertionError, match="above the baseline 0.9"):
        base.check_near("tx_submit.avg_us", 1.1, higher=False, tolerance="20%")
    base.check_near("decode.fps", 28.5, higher=True, tolerance=2.0)
    with pytest.raises(AssertionError, match="decode.fps: 27.9 is below the baseline 30"):
        base.check_near("decode.fps", 27.9, higher=True, tolerance=2.0)
    assert base.measured["decode.fps"] == 27.9      # recorded before the verdict


def test_a_missing_baseline_only_warns_and_seeding_neither_judges_nor_warns():
    base = Baselines("amiga-cm4")
    base.check("netdev.tcp_rx.mbps", 940.0, higher=True)
    base.check_near("decode.fps", 12.5, higher=True, tolerance="10%")
    assert base.measured == {"netdev.tcp_rx.mbps": 940.0, "decode.fps": 12.5}
    assert base.warnings[1] == "decode.fps: no baseline for amiga-cm4, measured 12.5 (recorded only)"
    seeding = Baselines("amiga-pi4", {"netdev.tcp_rx.mbps": Baseline(945.0, 944.0, 4, 12)}, seeding=True)
    seeding.check("netdev.tcp_rx.mbps", 100.0, higher=True)
    assert seeding.measured == {"netdev.tcp_rx.mbps": 100.0} and not seeding.warnings


def test_a_figure_only_recorded_is_never_judged():
    base = Baselines("amiga-pi4", {"speedtest.mbps": Baseline(100.0, 90.0, 3, 3)})
    base.record("speedtest.mbps", 1.0)
    assert base.measured == {"speedtest.mbps": 1.0} and not base.warnings


def test_a_tolerance_is_a_number_or_a_percentage():
    assert Baselines.tolerance_of("10%", 200) == 20.0 and Baselines.tolerance_of(3, 200) == 3.0
    with pytest.raises(ValueError, match="percentage"):
        Baselines.tolerance_of("5 percent", 100)


def test_writing_keeps_the_other_machines_and_reads_back(tmp_path):
    path = tmp_path / "baselines.json"
    path.write_text(json.dumps({"format": 2, "amiga-cm4": {"x.y.mbps": {"median": 1.0, "boots": 1, "runs": 1}}}))
    write(path, "amiga-pi4", {"netdev.tcp_tx.mbps": Baseline(940.1, 928.8, 9, 27),
                              "netdev.tcp_tx.ctxsw_s": Baseline(4250.0, None, 6, 18)})
    data = json.loads(path.read_text())
    assert data["amiga-cm4"] == {"x.y.mbps": {"median": 1.0, "boots": 1, "runs": 1}}
    assert data["amiga-pi4"]["netdev.tcp_tx.ctxsw_s"] == {"median": 4250.0, "boots": 6, "runs": 18}
    assert Baselines.load(path, "amiga-pi4").base["netdev.tcp_tx.mbps"] == Baseline(940.1, 928.8, 9, 27)


def test_a_format_1_file_of_plain_numbers_loads_as_medians(tmp_path):
    path = tmp_path / "baselines.json"
    path.write_text(json.dumps({"format": 1, "amiga-pi4": {"decode.fps": 85.3}, "amiga-cm4": {"decode.fps": 90}}))
    base = Baselines.load(path, "amiga-pi4")
    assert base.base == {"decode.fps": Baseline(85.3)}
    base.check_near("decode.fps", 84.0, higher=True, tolerance=2.0)


def test_updating_writes_the_measured_medians_and_keeps_everything_else(tmp_path):
    path = tmp_path / "baselines.json"
    path.write_text(json.dumps({"format": 1, "amiga-pi4": {"decode.fps": 85.3, "encode.fps": 30.0},
                                "amiga-cm4": {"decode.fps": 90}}))
    base = Baselines.load(path, "amiga-pi4")
    base.check_near("decode.fps", 86.0, higher=True, tolerance=2.0)
    base.check_near("jpeg.psnr_db", 34.1, higher=True, tolerance=2.0)
    base.update(path)
    assert json.loads(path.read_text()) == {
        "format": 2,
        "amiga-pi4": {"decode.fps": {"median": 86.0}, "encode.fps": {"median": 30.0}, "jpeg.psnr_db": {"median": 34.1}},
        "amiga-cm4": {"decode.fps": {"median": 90.0}}}


def test_a_baselines_file_of_a_later_format_is_refused(tmp_path):
    path = tmp_path / "baselines.json"
    path.write_text(json.dumps({"format": 3, "amiga-pi4": {}}))
    with pytest.raises(ValueError, match="format 3"):
        Baselines.load(path, "amiga-pi4")
    with pytest.raises(ValueError, match="format 3"):
        write(path, "amiga-pi4", {})


def test_the_measured_values_go_out_sorted(tmp_path):
    base = Baselines("amiga-pi4")
    base.record("b", 2.0)
    base.record("a", 1.0)
    base.write_measured(tmp_path / "measured.json")
    assert json.loads((tmp_path / "measured.json").read_text()) == {"format": 2, "machine": "amiga-pi4",
                                                                    "measured": {"a": 1.0, "b": 2.0}}
