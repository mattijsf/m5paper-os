#!/usr/bin/env python3
"""Reader acceptance on the device (P4-4 / P4-6): opens books from the library with injected taps, reports the cold index
time, the cached open time, page-turn times from the refresh log, and saves frame dumps (compare them with the simulator's
with `tools/device.py diff`).

  ~/.platformio/penv/bin/python tools/books-device.py OUTDIR 466 "Pride"  178 "Alice"

Each pair is the y of the book's row in the library (x = 270) and a short name for the files. The library must show no
"continue" card for those rows (the script wipes /sd/books/.paperos first, so every book is opened cold). Needs the books on the card
(make books). Writes OUTDIR/<name>-p1.png (first page), -p4.png (after 3 turns), -cached-p4.png (after reopening from the cache).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import device  # noqa: E402


class Dev:
    def __init__(self):
        self.link = device.Link()
        self.seen = []

    def cmd(self, c, timeout=20):
        return device.request(self.link, c, timeout=timeout)

    def log(self):
        _, payload, _ = self.cmd(">log")
        return (payload or b"").decode(errors="replace").splitlines()

    def wait_line(self, pattern, timeout):
        rx = re.compile(pattern)
        t0 = time.time()
        while time.time() - t0 < timeout:
            for line in self.log():
                if rx.search(line) and line not in self.seen:
                    self.seen.append(line)
                    return line
            time.sleep(1.5)
        raise TimeoutError(pattern)

    def shot(self, path):
        _, payload, header = self.cmd(">shot")
        pixels = device.unpack4(payload) if hasattr(device, "unpack4") else None
        return header, payload

    def tap(self, x, y):
        self.cmd(f">tap {x} {y}")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    books = list(zip([int(v) for v in sys.argv[2::2]], sys.argv[3::2]))
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "device.py")
    py = sys.executable

    def run(*args):
        import subprocess
        return subprocess.run([py, tool, *args], capture_output=True, text=True, timeout=120)

    run("set", "screen", "launcher")
    print("wipe cache:", run("rm", "/sd/books/.paperos").stdout.strip())
    rows = []
    for y, name in books:
        run("set", "screen", "read")
        time.sleep(8)
        d = Dev()
        d.seen = [l for l in d.log() if "[read] opened" in l]
        t0 = time.time()
        d.tap(270, y)
        line = d.wait_line(r"\[read\] opened|open failed", 240)
        cold = time.time() - t0
        detail = d.wait_line(r"\[read\]   zip", 5) if "indexed" in line else ""
        time.sleep(2)
        d.link.close()
        run("shot", f"{out}/{name}-p1.png")
        for _ in range(3):
            run("tap", "400", "500")
            time.sleep(3)
        run("shot", f"{out}/{name}-p4.png")
        refresh = [l for l in run("log").stdout.splitlines() if l.startswith("[disp]") or "render took" in l]
        # leave the book (saves the position), reopen from the cache: the library shows it as the "continue" card
        run("set", "screen", "launcher")
        time.sleep(1)
        run("set", "screen", "read")
        time.sleep(8)  # the library scans the card on the worker first
        d = Dev()
        d.seen = [l for l in d.log() if "[read] opened" in l]
        t1 = time.time()
        d.tap(400, 320)
        line2 = d.wait_line(r"\[read\] opened|open failed", 60)
        warm = time.time() - t1
        time.sleep(2)
        d.link.close()
        run("shot", f"{out}/{name}-cached-p4.png")
        rows.append((name, line, detail, cold, line2, warm, refresh[-8:]))
        run("set", "screen", "launcher")
        time.sleep(1)
    for name, line, detail, cold, line2, warm, refresh in rows:
        print(f"== {name}\n  cold:   {line.strip()}\n  {detail.strip()}\n  cached: {line2.strip()}")
        print("  last refresh lines:")
        for r in refresh:
            print("    " + r)


main()
