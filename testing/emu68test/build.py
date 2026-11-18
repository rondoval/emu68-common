# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
build.py - the flavour of the stack build a suite tests, from the build dir's
CMakeCache.txt: the debug backend and tier, the components pinned to debug or
profile, the cache-operation flavour, the build stamp.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

ON = ("ON", "1", "TRUE", "YES")
LOGGING_TIERS = ("debug", "trace")
PROFILING_TIERS = ("profile",) + LOGGING_TIERS


@dataclass
class BuildInfo:
    """One component's flavour in the stack build."""

    component: str                  # its name in the stack: emu68-genet-driver-netdev
    backend: str = "?"              # EMU68_DEBUG_BACKEND
    tier: str = "?"                 # EMU68_TIER
    debug_pins: str = ""            # EMU68_DEBUG: components at the debug tier whatever the stack's is
    profile_pins: str = ""          # EMU68_PROFILE: likewise for profile
    stamp: str = ""                 # EMU68_BUILD_STAMP: empty in a release
    values: dict[str, str] = field(default_factory=dict, repr=False)  # every EMU68_* cache entry

    @classmethod
    def read(cls, build_dir: Path, component: str) -> "BuildInfo":
        """@component's flavour as recorded in @build_dir; '?' throughout when there is no build there."""
        cache = Path(build_dir) / "CMakeCache.txt"
        info = cls(component)
        if not cache.exists():
            return info
        info.values = dict(re.findall(r"^(EMU68_\w+):\w+=(.*)$", cache.read_text(), re.M))
        info.backend = info.values.get("EMU68_DEBUG_BACKEND", "?")
        info.tier = info.values.get("EMU68_TIER", "?")
        info.debug_pins = info.values.get("EMU68_DEBUG", "")
        info.profile_pins = info.values.get("EMU68_PROFILE", "")
        info.stamp = info.values.get("EMU68_BUILD_STAMP", "")
        return info

    def option(self, name: str) -> bool:
        """Whether the cache's boolean @name (EMU68_...) is on."""
        return self.values.get(name, "OFF").upper() in ON

    def _pinned(self, pins: str) -> bool:
        names = pins.split(";")
        return self.component in names or "ALL" in names

    @property
    def debug(self) -> bool:
        """Whether the component logs to the debug backend."""
        return self.backend != "off" and (self.tier in LOGGING_TIERS or self._pinned(self.debug_pins))

    @property
    def profile(self) -> bool:
        """Whether the component's perf probes are compiled in."""
        return self.backend != "off" and (self.tier in PROFILING_TIERS or self._pinned(self.profile_pins) or
                                          self._pinned(self.debug_pins))

    @property
    def flavour(self) -> str:
        """The archive this build matches: 'standard' (exec cache LVOs) or 'rangeops'."""
        return "standard" if self.option("EMU68_FORCE_LVO_CACHE_OPS") else "rangeops"

    def summary(self) -> dict[str, str]:
        """What a run records of the build (run.json)."""
        return {"component": self.component, "backend": self.backend, "tier": self.tier,
                "debug_pins": self.debug_pins, "profile_pins": self.profile_pins, "flavour": self.flavour,
                "stamp": self.stamp}
