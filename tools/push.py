#!/usr/bin/env python3
"""Copies files to the device's SD card over USB serial (task P4-0).

    ~/.platformio/penv/bin/python tools/push.py book.epub other.txt            # to /sd/books/
    ~/.platformio/penv/bin/python tools/push.py --to /sd/paperos/ file.json
    ~/.platformio/penv/bin/python tools/push.py --sim --data out/x sim-data/sd/books/*   # round trip against the simulator

Each file is sent with `>put <path> <size> <crc32>` in 4 KB chunks, one acknowledgement per chunk; the device checks the
CRC and renames <path>.part. Only /sd/books/ and /sd/paperos/ are writable, never paperos_config. Files that are already
on the card with the same size are skipped unless --force.
"""
import argparse
import os
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import device  # noqa: E402

CHUNK = 4096


def listing(link, folder):
    status, payload, _ = device.request(link, ">ls " + folder.rstrip("/"))
    out = {}
    if status.startswith("#ok") and payload:
        for line in payload.decode(errors="replace").splitlines():
            name, _, size = line.partition("\t")
            if size.isdigit():
                out[name] = int(size)
    return out


def push_one(link, src, dest_folder, force):
    data = open(src, "rb").read()
    name = os.path.basename(src)
    path = dest_folder.rstrip("/") + "/" + name
    if not force and listing(link, dest_folder).get(name) == len(data):
        print(f"have   {name} ({len(data)} bytes)")
        return True
    t0 = time.time()
    link.send(f">put {path} {len(data)} {zlib.crc32(data) & 0xFFFFFFFF:08x}")
    line = link.readline()
    while line and not line.startswith(("#put go", "#err")):
        line = link.readline()
    if not line.startswith("#put go"):
        print(f"FAILED {name}: {line or 'no reply'}")
        return False
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent + CHUNK]
        link.w.write(chunk)
        link.w.flush()
        sent += len(chunk)
        if sent < len(data) and sent % CHUNK == 0:  # the device acknowledges every full 4 KB (except the last)
            while True:
                r = link.readline()
                if r.startswith("#ack"):
                    break
                if r.startswith("#err"):
                    print(f"FAILED {name}: {r}")
                    return False
                if not r:
                    print(f"FAILED {name}: no acknowledgement at {sent} bytes")
                    return False
    while True:
        r = link.readline()
        if r.startswith("#ok"):
            break
        if r.startswith("#err") or not r:
            print(f"FAILED {name}: {r or 'no reply'}")
            return False
    dt = max(time.time() - t0, 1e-6)
    print(f"sent   {name}: {len(data)} bytes in {dt:.1f} s ({len(data) / dt / 1024:.0f} KB/s)")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--to", default="/sd/books/")
    ap.add_argument("--port")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("--data", help="with --sim: the simulator's data root")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    sim_args = ("--data", a.data) if a.sim and a.data else ()
    link = device.Link(a.port, a.sim, sim_args)
    ok = True
    t0 = time.time()
    total = 0
    try:
        for f in a.files:
            if not os.path.isfile(f):
                print(f"skip   {f}: not a file")
                ok = False  # asked for and not pushed: the run did not do what it was told
                continue
            total += os.path.getsize(f)
            ok = push_one(link, f, a.to, a.force) and ok
    finally:
        link.close()
    print(f"done: {len(a.files)} file(s), {total} bytes, {time.time() - t0:.1f} s")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
