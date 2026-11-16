#!/usr/bin/env python3
"""Tap-to-panel latency on the device: plays typical taps, reads the firmware's "[lat]" trace and the refresh log.

    ~/.platformio/penv/bin/python tools/latency.py [--n 6] [--only page,nav,small] [--json out/latency.json]

Scenarios (the device is driven into them with injected taps; Down and Up of an injected tap are 60 ms apart):
  page   reader page turn (Read tile, then taps on the right side of the page)
  nav    Settings list -> Display (a new screen), and back
  small  Settings > Display, the ghost-limit stepper: + and - (a small area changes)
Per scenario it prints the median of: event->gesture, ->render (gesture dispatched to render start), render, commit
(diff + push call), event->pushed (finger up to the panel driver having the update), and the push call itself.
The waveform that follows is the panel's (measure it with `tools/cam.py around`). DISPLAY.md "Tap latency".
Bounded: every request has a timeout, the whole run a limit (--limit, default 240 s); the port is closed at the end.
"""
import argparse
import importlib.util
import json
import os
import re
import statistics
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("paperos_device", os.path.join(ROOT, "tools", "device.py"))
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)

LAT = re.compile(r"\[lat\] event->gesture (\d+), ->render (\d+), render (\d+), commit (\d+) \(diff (\d+), push call (\d+), "
                 r"mode (\w) (\d+)x(\d+)\), event->pushed (\d+) ms")
FIELDS = ["event->gesture", "->render", "render", "commit", "diff", "push call", "event->pushed"]


class Bench:
    def __init__(self, port=None):
        self.link = dev.Link(port)

    def ok(self, cmd, timeout=10):
        st, payload, _ = dev.request(self.link, cmd, timeout=timeout)
        if not st.startswith("#ok"):
            raise SystemExit(f"{cmd}: {st}")
        return payload

    def tap(self, x, y):
        self.ok(f">tap {x} {y}")

    def lat_lines(self):
        return [l for l in (self.ok(">log") or b"").decode(errors="replace").splitlines() if "[lat]" in l]

    def measure(self, x, y, settle=1.6):
        before = self.lat_lines()
        self.tap(x, y)
        time.sleep(settle)
        after = self.lat_lines()
        if not after or after == before:
            return None
        m = LAT.search(after[-1])
        if not m:
            return None
        g = [int(v) for v in m.groups()[:6]] + [int(m.group(10))]
        return {"values": dict(zip(FIELDS, g)), "mode": m.group(7), "rect": f"{m.group(8)}x{m.group(9)}"}


def run(b, name, n):
    rows = []
    if name == "page":
        b.ok(">set screen launcher")
        time.sleep(1.5)
        b.tap(140, 300)  # Read: the open book (not at its last page: a turn there changes nothing)
        time.sleep(3)
        for _ in range(n):
            rows.append(b.measure(400, 500))
    elif name == "nav":
        b.ok(">set screen settings")
        time.sleep(1.5)
        for _ in range(n):
            rows.append(b.measure(270, 280))  # Display row
            b.measure(36, 92)  # back (not counted: same kind of change)
    elif name == "small":
        b.ok(">set screen settings")
        time.sleep(1.5)
        b.tap(270, 280)
        time.sleep(1.5)
        for i in range(n):
            rows.append(b.measure(480 if i % 2 == 0 else 60, 316))  # + then -: ends where it started
        if n % 2:
            b.tap(60, 316)
            time.sleep(1)
    rows = [r for r in rows if r]
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=6)
    ap.add_argument("--only", default="page,nav,small")
    ap.add_argument("--json")
    ap.add_argument("--port")
    ap.add_argument("--limit", type=int, default=240)
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    dev.arm_limit(a.limit)
    b = Bench(a.port)
    out = {}
    try:
        for name in a.only.split(","):
            print(f"{name}: {a.n} taps ...")
            rows = run(b, name, a.n)
            if not rows:
                print(f"  {name}: no [lat] lines (firmware without the trace?)")
                continue
            med = {f: statistics.median(r["values"][f] for r in rows) for f in FIELDS}
            out[name] = {"n": len(rows), "median": med, "mode": rows[-1]["mode"], "rect": rows[-1]["rect"],
                         "event->pushed": [r["values"]["event->pushed"] for r in rows]}
            print(f"  {name} ({len(rows)} taps, {rows[-1]['mode']} {rows[-1]['rect']}): " +
                  ", ".join(f"{f} {med[f]:.0f}" for f in FIELDS) + " ms (medians)")
            print(f"    event->pushed per tap: {out[name]['event->pushed']}")
    finally:
        b.link.close()
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1)
        print(f"wrote {a.json}")


if __name__ == "__main__":
    main()
