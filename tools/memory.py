#!/usr/bin/env python3
"""Memory per app on the device (D27): internal heap and PSRAM, lowest point and largest free block, task stacks.

    ~/.platformio/penv/bin/python tools/memory.py [--only read,home,...] [--json out/memory.json]

For each app the device is rebooted (so "lowest since boot" belongs to that app), the app is opened with >set screen, used a
little (the reader turns 10 pages; Home and the inbox fetch over Wi-Fi/TLS, read-only), then ">set memreport" logs one line.
Then the app is left for the launcher and a second line shows what stayed allocated. Bounded: every request has a timeout,
the whole run a limit (--limit, default 900 s).
"""
import argparse
import importlib.util
import json
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("paperos_device", os.path.join(ROOT, "tools", "device.py"))
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)

MEM = re.compile(r"\[mem\] (\S+) internal free=(\d+) min=(\d+) largest=(\d+) psram free=(\d+) min=(\d+) largest=(\d+)")
STACK = re.compile(r"\[memstack\] (\S+)(.*)")

# app id -> (wait after opening in s, taps after that)
SCENARIOS = {
    "idle": (2, []),
    "read": (6, [(400, 500)] * 10),
    "home": (15, []),
    "whatsapp": (15, []),
    "mail": (15, []),
    "settings": (2, [(270, 280), (36, 92), (270, 760), (36, 92)]),
    "somdoku": (3, []),
    "wordgame": (3, []),
    "weather": (15, []),
}


class Dev:
    def __init__(self, port):
        self.port = port
        self.link = None

    def open(self):
        if self.link is None:
            self.link = dev.Link(self.port)

    def close(self):
        if self.link is not None:
            self.link.close()
            self.link = None

    def ok(self, cmd, timeout=10):
        self.open()
        st, payload, _ = dev.request(self.link, cmd, timeout=timeout)
        if not st.startswith("#ok"):
            raise SystemExit(f"{cmd}: {st}")
        return payload

    def reboot(self):
        self.open()
        try:
            dev.request(self.link, ">reboot", timeout=3, retries=0)
        except Exception:
            pass
        self.close()
        time.sleep(4)
        for _ in range(30):  # the port comes back after the boot
            try:
                self.open()
                self.ok(">log", timeout=3)
                return
            except Exception:
                self.close()
                time.sleep(1)
        raise SystemExit("device did not come back after >reboot")

    def report(self, tag):
        self.ok(f">set memreport {tag}")
        log = (self.ok(">log") or b"").decode(errors="replace")
        rows = [MEM.search(l) for l in log.splitlines()]
        rows = [m for m in rows if m and m.group(1) == tag]
        if not rows:
            return None
        m = rows[-1]
        stacks = {}
        for l in log.splitlines():
            st = STACK.search(l)
            if st and st.group(1) == tag:
                stacks = dict(kv.split("=", 1) for kv in st.group(2).split() if "=" in kv)
        return {"internal_free": int(m.group(2)), "internal_min": int(m.group(3)), "internal_largest": int(m.group(4)),
                "psram_free": int(m.group(5)), "psram_min": int(m.group(6)), "psram_largest": int(m.group(7)), "stacks": stacks}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", default=",".join(SCENARIOS))
    ap.add_argument("--json")
    ap.add_argument("--port")
    ap.add_argument("--limit", type=int, default=900)
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    dev.arm_limit(a.limit)
    d = Dev(a.port)
    out = {}
    try:
        for app in a.only.split(","):
            wait, taps = SCENARIOS[app]
            d.reboot()
            time.sleep(2)
            if app != "idle":
                d.ok(f">set screen {app}")
            time.sleep(wait)
            for x, y in taps:
                d.ok(f">tap {x} {y}")
                time.sleep(1.5)
            r_in = d.report(f"{app}-in")
            d.ok(">set screen launcher")
            time.sleep(2)
            r_out = d.report(f"{app}-out")
            out[app] = {"in": r_in, "out": r_out}
            if r_in:
                k = lambda v: f"{v / 1024:.0f}K"
                print(f"{app:9s} internal free {k(r_in['internal_free'])} min {k(r_in['internal_min'])} largest "
                      f"{k(r_in['internal_largest'])} | psram free {k(r_in['psram_free'])} | after leaving: internal free "
                      f"{k(r_out['internal_free']) if r_out else '?'} | stack left {r_in['stacks']}")
            else:
                print(f"{app:9s} no [mem] line (firmware without >set memreport?)")
    finally:
        d.close()
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1)
        print(f"wrote {a.json}")


if __name__ == "__main__":
    main()
