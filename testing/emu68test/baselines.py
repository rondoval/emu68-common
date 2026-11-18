# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
baselines.py - what a component measured on each machine; a later build must
do no worse.

A suite's tests/baselines.json:

    {"format": 2,
     "amiga-pi4": {"netdev.tcp_tx.mbps": {"median": 940.1, "bound": 928.8, "boots": 9, "runs": 27},
                   "decode_clip42.decode.fps": {"median": 85.3}},
     "amiga-cm4": {}}

A figure is judged one of two ways, said where it is checked:

- check(): against its bound, the worst of the boots the baseline was seeded
  from (the lowest where higher is better). A run passes when it is as good as
  the component was on its worst seeding boot. For figures that vary from boot
  to boot; a suite seeds them from several boots.
- check_near(): near its median, within a tolerance the test gives. For
  figures measured once.

A figure without a baseline is recorded and warned about, never failed. The
key of a machine is its amiga MCP server name; a suite may add to it (a
second stack on the same machine).

A format 1 file ({"key": number}) still loads, each number as a median.
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path

FORMAT = 2
READABLE = (1, FORMAT)


@dataclass
class Baseline:
    """One figure as measured when the baselines were taken."""

    median: float
    bound: float | None = None      # the worst seeding boot's median; None: not judged by check()
    boots: int = 0
    runs: int = 0

    def as_json(self) -> dict:
        """The entry in baselines.json, without what it does not have."""
        return {k: v for k, v in asdict(self).items() if v not in (None, 0) or k == "median"}

    @classmethod
    def read(cls, entry) -> "Baseline":
        """A baseline from its entry: an object, or a format 1 number."""
        return cls(**entry) if isinstance(entry, dict) else cls(float(entry))


class Baselines:
    """The baselines of one machine, the values this run measured, and the warnings."""

    def __init__(self, machine: str, base: dict[str, Baseline] | None = None, seeding: bool = False):
        self.machine = machine
        self.base: dict[str, Baseline] = dict(base or {})
        self.seeding = seeding          # measuring to seed: nothing judged, nothing warned
        self.measured: dict[str, float] = {}
        self.warnings: list[str] = []

    @classmethod
    def load(cls, path: str | Path, machine: str) -> "Baselines":
        """The baselines of @machine from the JSON at @path; none when the file does not exist."""
        path = Path(path)
        if not path.exists():
            return cls(machine)
        data = json.loads(path.read_text())
        if data.get("format") not in READABLE:
            raise ValueError(f"{path}: format {data.get('format')!r}, this harness reads {FORMAT}")
        return cls(machine, {k: Baseline.read(v) for k, v in data.get(machine, {}).items()})

    @staticmethod
    def tolerance_of(spec: float | str, base: float) -> float:
        """An absolute tolerance from a number, or from an 'N%' string relative to @base."""
        if isinstance(spec, str):
            if not spec.endswith("%"):
                raise ValueError(f"tolerance {spec!r}: a number, or a percentage like '5%'")
            return abs(base) * float(spec[:-1]) / 100.0
        return float(spec)

    def _known(self, key: str, value: float) -> Baseline | None:
        """@value recorded under @key; its baseline, or None (seeding, or none yet: a warning)."""
        self.measured[key] = value
        if self.seeding:
            return None
        found = self.base.get(key)
        if found is None:
            self.warnings.append(f"{key}: no baseline for {self.machine}, measured {value} (recorded only)")
        return found

    def check(self, key: str, value: float, higher: bool) -> None:
        """Record @value under @key; it must reach the baseline's bound, the worst seeding boot (@higher
        says which way is better). Without a baseline it is only recorded."""
        base = self._known(key, value)
        if base is None:
            return
        if base.bound is None:
            raise ValueError(f"{key}: the baseline has no bound (check_near() judges a figure without one)")
        if higher and value < base.bound:
            raise AssertionError(f"{key}: {value:g} is below {base.bound:g}, the worst of {base.boots} seeding "
                                 f"boots (median {base.median:g}; higher is better)")
        if not higher and value > base.bound:
            raise AssertionError(f"{key}: {value:g} is above {base.bound:g}, the worst of {base.boots} seeding "
                                 f"boots (median {base.median:g}; lower is better)")

    def check_near(self, key: str, value: float, higher: bool, tolerance: float | str) -> None:
        """Record @value under @key; it may fall short of the baseline's median by @tolerance (a number, or
        'N%' of the median) when @higher is better, or exceed it by that much otherwise."""
        base = self._known(key, value)
        if base is None:
            return
        allowed = self.tolerance_of(tolerance, base.median)
        if higher and value < base.median - allowed:
            raise AssertionError(f"{key}: {value:g} is below the baseline {base.median:g} by more than "
                                 f"{allowed:g} (higher is better, tolerance {tolerance})")
        if not higher and value > base.median + allowed:
            raise AssertionError(f"{key}: {value:g} is above the baseline {base.median:g} by more than "
                                 f"{allowed:g} (lower is better, tolerance {tolerance})")

    def record(self, key: str, value: float) -> None:
        """Record @value under @key without judging it."""
        self.measured[key] = value

    def write_measured(self, path: str | Path) -> None:
        """The values this run measured, as JSON with sorted keys."""
        Path(path).write_text(json.dumps({"format": FORMAT, "machine": self.machine, "measured": self.measured},
                                         indent=2, sort_keys=True) + "\n")

    def update(self, path: str | Path) -> None:
        """This run's measured values as this machine's medians in the file at @path: the machine's other
        figures and the other machines kept. For figures judged by check_near(); a bound is seeded."""
        found = dict(self.base)
        found.update({key: Baseline(value) for key, value in self.measured.items()})
        write(path, self.machine, found)


def write(path: str | Path, machine: str, found: dict[str, Baseline]) -> None:
    """@found as @machine's baselines in the file at @path, the other machines' kept."""
    path = Path(path)
    data = json.loads(path.read_text()) if path.exists() else {}
    if data.get("format") not in (None,) + READABLE:
        raise ValueError(f"{path}: format {data.get('format')!r}, this harness writes {FORMAT}")
    others = {name: {k: Baseline.read(v).as_json() for k, v in entries.items()}
              for name, entries in data.items() if name not in ("format", machine)}
    others[machine] = {k: v.as_json() for k, v in sorted(found.items())}
    path.write_text(json.dumps({"format": FORMAT, **others}, indent=2, sort_keys=True) + "\n")
