#!/usr/bin/env python3
"""Pushes whose box is large for what changed in it: one event whose changes are far apart, sent to the panel as one box.

    tools/ui-test.sh; python3 tools/push-audit.py out/ui
    PAPEROS_TAP_MS=200 tools/ui-test.sh; python3 tools/push-audit.py out/ui   # real-finger taps: pressed looks show (screenshots then differ)

Reads the [disp] lines of the *.script.log files a UI test run leaves next to its screenshots. Lists Feedback and Content
pushes of 30 000 px or more of which under 12 % changed (DISPLAY.md "Rules for UI code" 7). Exit code 0 always: a list
whose rows change far apart is on it by nature; read the list."""
import re, sys, glob, os, collections
pat = re.compile(r"\[disp\] push (\w) rect=(\d+),(\d+),(\d+),(\d+) changed=(\d+) why=(\S+)")
rows = []
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.script.log"))):
    name = os.path.basename(f)[:-11]
    last_touch = ""
    for line in open(f, errors="replace"):
        if line.startswith("[touch]"):
            last_touch = line.split("]")[1].strip()[:24]
        m = pat.search(line)
        if not m: continue
        mode, x, y, w, h, ch, why = m.group(1), *map(int, m.group(2, 3, 4, 5, 6)), m.group(7)
        area = w * h
        if why in ("feedback", "content") and area >= 30000 and ch < area * 0.12:
            rows.append((area / max(ch, 1), name, why, f"{w}x{h}@{x},{y}", ch, area, last_touch))
rows.sort(reverse=True)
seen = collections.Counter()
for r in rows:
    seen[(r[1], r[2])] += 1
    if seen[(r[1], r[2])] > 2: continue
    print(f"{r[1]:28s} {r[2]:9s} box {r[3]:18s} changed {r[4]:6d} of {r[5]:6d} ({100*r[4]/r[5]:.0f}%)  after {r[6]}")
print(len(rows), "oversized pushes in", len(set(r[1] for r in rows)), "scripts")
