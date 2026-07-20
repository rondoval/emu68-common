#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
"""perf-report — reduce a serial capture of perf.h timing samples to
steady-state per-stage tables.

Parses every ``[<prefix>] <name>: n=.. sum=..us avg=..us max=..us`` line the
stack's perf instances emit (any number of instances per capture, one report
section each), groups the ~2 s report blocks into windows, keeps the
steady-state windows (key slot count >= 50 % of its peak), and prints per-slot
rates, per-event averages and share of wall time.

When the capture also carries the stack's standard telemetry, it is folded in:
the ``[netstack] lock:`` line (core-lock held/waited shares) and genet's
``rxprof``/``mib pkt``/``txh pkt`` lines (driver frames/s, used to normalize
slot costs per RX/TX frame).

Usage:
  perf-report.py <capture.log> [--key SLOT] [--label TEXT]

The capture may carry a ``HH:MM:SS:ms -> `` serial-tool prefix and stray
binary bytes; both are tolerated. Without timestamps, windows are split when
a slot name repeats, and rate/%-of-wall columns assume 2.02 s per window.
"""
import argparse
import re
import sys

TS = re.compile(r"^(\d+):(\d+):(\d+):(\d+) -> (.*)$")
PERF = re.compile(r"\[(\w+)\] (\w+): n=(\d+) sum=(\d+)us avg=[\d.]+us max=(\d+)us")
LOCK = re.compile(r"\[netstack\] lock: (\d+) holds, wait (\d+) us, hold (\d+) us, maxhold (\d+) us")
RXPROF = re.compile(r"\[genet\] rxprof: fr\+(\d+) drain=(\d+)us flush=(\d+)us")
TXH = re.compile(r"\[genet\] txh: pkt\+(\d+)")

WINDOW_FALLBACK_MS = 2020  # perf_report cadence when the log has no timestamps
WINDOW_GAP_MS = 300        # a larger gap between perf lines starts a new window


def parse(path):
    """Return (windows-per-prefix, locks, rx200, tx200).

    windows[prefix] = [ {t, slots: {name: (n, sum, max)}} ... ]
    locks = [(t, holds, wait, hold, maxhold)], rx200 = [(t, fr, drain, flush)],
    tx200 = [(t, pkt)] — t is milliseconds, or a synthetic counter when the
    capture has no timestamps.
    """
    windows = {}
    current = {}
    locks, rx200, tx200 = [], [], []
    synth_t = [0]

    def now(line):
        m = TS.match(line)
        if m:
            h, mi, s, ms = (int(x) for x in m.groups()[:4])
            return ((h * 60 + mi) * 60 + s) * 1000 + ms, m.group(5), True
        return synth_t[0], line, False

    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("ascii", errors="replace").rstrip()
            t, rest, stamped = now(line)

            pm = PERF.search(rest)
            if pm:
                prefix, name, n, su, mx = pm.groups()
                cur = current.get(prefix)
                fresh = (cur is None or name in cur["slots"] or
                         (stamped and t - cur["t"] > WINDOW_GAP_MS))
                if fresh:
                    if not stamped:
                        synth_t[0] += WINDOW_FALLBACK_MS
                        t = synth_t[0]
                    cur = {"t": t, "slots": {}}
                    windows.setdefault(prefix, []).append(cur)
                    current[prefix] = cur
                cur["slots"][name] = (int(n), int(su), int(mx))
                continue

            lm = LOCK.search(rest)
            if lm:
                locks.append((t,) + tuple(int(x) for x in lm.groups()))
                continue
            rm = RXPROF.search(rest)
            if rm:
                rx200.append((t,) + tuple(int(x) for x in rm.groups()))
                continue
            tm = TXH.search(rest)
            if tm:
                tx200.append((t, int(tm.group(1))))
    return windows, locks, rx200, tx200


def in_span(rows, t0, t1):
    return [r for r in rows if t0 < r[0] <= t1]


def report(prefix, wins, locks, rx200, tx200, key):
    peak_of = lambda name: max((w["slots"].get(name, (0, 0, 0))[0] for w in wins), default=0)
    if key is None:
        totals = {}
        for w in wins:
            for name, (n, _su, _mx) in w["slots"].items():
                totals[name] = totals.get(name, 0) + n
        if not totals:
            return
        key = max(totals, key=totals.get)
    peak = peak_of(key)
    sel = [w for w in wins if w["slots"].get(key, (0, 0, 0))[0] >= peak * 0.5]
    if len(sel) < 2:
        print(f"[{prefix}] fewer than two steady windows (key slot '{key}') — nothing to reduce")
        return

    ts = [w["t"] for w in sel]
    spans = [b - a for a, b in zip(ts, ts[1:]) if b - a < 3000]
    wall_ms = sum(spans) + (spans[0] if spans else WINDOW_FALLBACK_MS)
    wall_us = wall_ms * 1000.0
    secs = wall_us / 1e6

    slots = {}
    for w in sel:
        for name, (n, su, mx) in w["slots"].items():
            N, S, M = slots.get(name, (0, 0, 0))
            slots[name] = (N + n, S + su, max(M, mx))

    t0, t1 = ts[0] - WINDOW_FALLBACK_MS, ts[-1]
    lk = in_span(locks, t0, t1)
    rx = in_span(rx200, t0, t1)
    tx = in_span(tx200, t0, t1)
    rxfr = sum(r[1] for r in rx)
    txfr = sum(r[1] for r in tx)

    print(f"=== [{prefix}] key={key} windows={len(sel)} wall={wall_ms / 1000.0:.2f}s ===")
    if lk:
        holdu = sum(l[3] for l in lk)
        waitu = sum(l[2] for l in lk)
        maxh = max(l[4] for l in lk)
        print(f"lock: held {holdu / wall_us * 100:.0f}%  waited {waitu / wall_us * 100:.0f}%"
              f"  maxhold {maxh / 1000.0:.1f}ms  holds/s {sum(l[1] for l in lk) / secs:.0f}")
    if rx:
        drain = sum(r[2] for r in rx)
        flush = sum(r[3] for r in rx)
        print(f"driver: rx {rxfr / secs:.0f} fr/s (drain {drain / max(rxfr, 1):.1f}"
              f" flush {flush / max(rxfr, 1):.1f} us/fr)   tx {txfr / secs:.0f} fr/s")

    hdr = f"{'slot':<16}{'n/s':>9}{'us/event':>10}{'%wall':>8}"
    if rxfr or txfr:
        hdr += "   per-frame us (rx/tx)"
    print(hdr)
    for name, (n, su, mx) in sorted(slots.items(), key=lambda kv: -kv[1][1]):
        if n == 0:
            continue
        row = f"{name:<16}{n / secs:>9.0f}{su / n:>10.1f}{su / wall_us * 100:>7.1f}%"
        if rxfr or txfr:
            per_rx = su / rxfr if rxfr else 0
            per_tx = su / txfr if txfr else 0
            row += f"   rx {per_rx:6.1f} / tx {per_tx:6.1f}"
        row += f"   (max {mx}us)"
        print(row)
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log", help="serial capture file")
    ap.add_argument("--key", help="steady-state key slot (default: highest-count slot)")
    ap.add_argument("--label", help="free-text label printed above the report")
    args = ap.parse_args()

    windows, locks, rx200, tx200 = parse(args.log)
    if not windows:
        print("no perf report lines found", file=sys.stderr)
        return 1
    if args.label:
        print(f"## {args.label}")
    for prefix in sorted(windows):
        report(prefix, windows[prefix], locks, rx200, tx200, args.key)
    return 0


if __name__ == "__main__":
    sys.exit(main())
