# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""
screen.py - the Amiga's screen, for a suite that looks at it: screenshots as
arrays, a window's place, clicks. It needs numpy, and amimcp's png module
(importable once machine.client() has run).
"""
from __future__ import annotations

import re
import time

import numpy as np

WAKE_SETTLE_S = 0.3
WINDOW_RE = re.compile(r'W\s+\d+\s+(-?\d+)\s+(-?\d+)\s+(\d+)x(\d+)\s+\S+\s+"(.*)"')


def wake(device) -> None:
    """A nudge of the mouse: a screen blanker closes its screen on input."""
    def nudge():
        device.ami.input_move_rel(1, 1)
        device.ami.input_move_rel(-1, -1)

    device.retry(nudge)
    time.sleep(WAKE_SETTLE_S)


def screenshot(device, rect: tuple[int, int, int, int] | None = None, screen: int = 0, name: str = "shot") -> np.ndarray:
    """A region (x, y, w, h) of a screen as an RGB array, saved as a PNG in the test's artifact dir."""
    import png  # amimcp's

    if rect:
        x, y, w, h = rect
        shot = device.ami.screenshot(timeout=60, x=x, y=y, w=w, h=h, screen=screen)
    else:
        shot = device.ami.screenshot(timeout=60, screen=screen)
    if device.artifacts is not None:
        (device.artifacts / f"{name}.png").write_bytes(png.screenshot_png(shot))
    if shot.palette is not None:
        palette = np.array(shot.palette, np.uint8)
        return palette[np.frombuffer(shot.pixels, np.uint8)].reshape(shot.height, shot.width, 3)
    return np.frombuffer(shot.pixels, np.uint8).reshape(shot.height, shot.width, 3)


def window(device, title: str) -> tuple[int, int, int, int]:
    """The frontmost screen's window titled @title: (x, y, w, h); the blanker woken first."""
    wake(device)
    for line in device.ami.ui_tree().splitlines():
        found = WINDOW_RE.match(line)
        if found and found.group(5) == title:
            return tuple(int(found.group(i)) for i in range(1, 5))  # type: ignore[return-value]
    raise AssertionError(f"no window \"{title}\" on the frontmost screen")


def click(device, x: int, y: int) -> None:
    """A left click at screen coordinates."""
    device.ami.input_click(x, y)
