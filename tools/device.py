#!/usr/bin/env python3
"""Remote control for PaperOS over USB serial (or the simulator): frame dump, touch, log.

Run with PlatformIO's Python (it has pyserial):
    ~/.platformio/penv/bin/python tools/device.py shot out/device.png
    ~/.platformio/penv/bin/python tools/device.py tap 140 300
Commands: diff A.png B.png [FIRST_ROW] | shot OUT.png | tap X Y | long X Y | swipe X1 Y1 X2 Y2 | log | refresh | set KEY VALUE | wait-boot | diff A.png B.png
Options:  --port /dev/cu.usbmodemXXXX (default: the first /dev/cu.usbmodem*)
          --sim   talk to .pio/build/sim/program --headless --remote instead of a device
          -v      also print device log lines seen while waiting for a reply
Protocol: docs/ROADMAP.md P1-8, src/core/remote.h. The dump is the frame the firmware drew, not
what the panel physically shows (no ghosting, no half-played waveforms).
"""
import argparse
import glob
import os
import struct
import subprocess
import sys
import time
import zlib

W, H = 540, 960


class Link:
    """Line/byte transport to the firmware: a serial port or the simulator's stdio."""

    def __init__(self, port=None, sim=False):
        self.sim = sim
        if sim:
            root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            self.proc = subprocess.Popen(
                [os.path.join(root, ".pio/build/sim/program"), "--headless", "--remote"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=root)
            self.r, self.w = self.proc.stdout, self.proc.stdin
        else:
            import serial  # pyserial: use ~/.platformio/penv/bin/python

            port = port or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
            if not port:
                sys.exit("no device: /dev/cu.usbmodem* not found")
            self.ser = serial.Serial()
            self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 5
            # Do NOT touch dtr/rts: setting them while opening the USB-Serial/JTAG port walks
            # through the (DTR=0, RTS=1) reset state and reboots the board on every command.
            self.ser.open()
            self.r = self.w = self.ser

    def send(self, line):
        if not self.sim:
            self.ser.reset_input_buffer()
        self.w.write(line.encode() + b"\n")
        self.w.flush()

    def readline(self):
        return self.r.readline().decode(errors="replace").rstrip("\r\n")

    def read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.r.read(n - len(buf))
            if not chunk:
                raise TimeoutError(f"short read: {len(buf)} of {n} bytes")
            buf += chunk
        return buf

    def close(self):
        if self.sim:
            self.w.close()
            self.proc.wait(timeout=10)
        else:
            self.ser.close()


def request(link, cmd, verbose=False, timeout=20):
    """Send one command; return (status_line, payload_bytes or None, header or None)."""
    link.send(cmd)
    deadline = time.time() + timeout
    payload, header = None, None
    while time.time() < deadline:
        line = link.readline()
        if not line:
            continue
        if line.startswith("#shot "):
            header = line.split()
            payload = link.read(int(header[3]))
        elif line.startswith("#log ") or line.startswith("#refresh "):
            header = line.split()
            payload = link.read(int(header[1]))
        elif line.startswith("#ok") or line.startswith("#err"):
            return line, payload, header
        elif verbose:
            print("| " + line, file=sys.stderr)
    raise TimeoutError(f"no reply to {cmd!r}")


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def write_png(path, pixels, w=W, h=H):
    raw = b"".join(b"\x00" + pixels[y * w:(y + 1) * w] for y in range(h))

    def chunk(t, d):
        body = t + d
        return struct.pack(">I", len(d)) + body + struct.pack(">I", crc32(body))

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def read_png(path):
    """Gray 8-bit, non-interlaced, filter 0 only (what PaperOS writes). Returns (w, h, pixels)."""
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, il = struct.unpack(">IIBBBBB", body)
            assert (depth, ctype, il) == (8, 0, 0), "only gray8 non-interlaced PNGs"
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    rows = []
    for y in range(h):
        row = raw[y * (w + 1):(y + 1) * (w + 1)]
        assert row[0] == 0, "only filter 0 supported"
        rows.append(row[1:])
    return w, h, b"".join(rows)


def unpack4(packed):
    out = bytearray(len(packed) * 2)
    for i, b in enumerate(packed):
        out[2 * i] = (b >> 4) * 17
        out[2 * i + 1] = (b & 15) * 17
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("cmd", choices=["shot", "tap", "long", "swipe", "log", "refresh", "set", "wait-boot", "diff"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()

    if a.cmd == "diff":
        if len(a.args) not in (2, 3):
            sys.exit("usage: diff A.png B.png [FIRST_ROW]   (compare only rows from FIRST_ROW: skips the status bar, which shows live battery and time)")
        wa, ha, pa = read_png(a.args[0])
        wb, hb, pb = read_png(a.args[1])
        if (wa, ha) != (wb, hb):
            sys.exit(f"DIFFERENT size {wa}x{ha} vs {wb}x{hb}")
        first = int(a.args[2]) if len(a.args) == 3 else 0
        bad = sum(1 for x, y in zip(pa[first * wa:], pb[first * wa:]) if x != y)
        print("IDENTICAL" if bad == 0 else f"DIFFERENT: {bad} of {len(pa)} pixels")
        sys.exit(0 if bad == 0 else 1)

    link = Link(a.port, a.sim)
    try:
        if a.cmd == "shot":
            if len(a.args) != 1:
                sys.exit("usage: shot OUT.png")
            status, payload, hdr = request(link, ">shot", a.verbose)
            if not status.startswith("#ok") or payload is None:
                sys.exit(f"shot failed: {status}")
            w, h, n, crc = int(hdr[1]), int(hdr[2]), int(hdr[3]), int(hdr[4], 16)
            if crc32(payload) != crc:
                sys.exit(f"CRC mismatch: device {crc:08x}, received {crc32(payload):08x}")
            os.makedirs(os.path.dirname(os.path.abspath(a.args[0])), exist_ok=True)
            write_png(a.args[0], unpack4(payload), w, h)
            print(f"wrote {a.args[0]} ({w}x{h}, {n} bytes, crc {crc:08x} ok)")
        elif a.cmd in ("tap", "long", "swipe"):
            want = 4 if a.cmd == "swipe" else 2
            if len(a.args) != want or not all(x.lstrip("-").isdigit() for x in a.args):
                sys.exit(f"usage: {a.cmd} " + ("X1 Y1 X2 Y2" if want == 4 else "X Y"))
            status, _, _ = request(link, f">{a.cmd} " + " ".join(a.args), a.verbose)
            print(status)
            if not status.startswith("#ok"):
                sys.exit(1)
            if a.sim:
                time.sleep(0.3)  # the simulator exits when we close stdin: let timed events play out
        elif a.cmd == "refresh":
            status, payload, _ = request(link, ">refresh", a.verbose)
            sys.stdout.write((payload or b"").decode(errors="replace"))
        elif a.cmd == "set":
            if len(a.args) != 2:
                sys.exit("usage: set KEY VALUE")
            status, _, _ = request(link, ">set " + " ".join(a.args), a.verbose)
            print(status)
            if not status.startswith("#ok"):
                sys.exit(1)
        elif a.cmd == "log":
            status, payload, _ = request(link, ">log", a.verbose)
            sys.stdout.write((payload or b"").decode(errors="replace"))
        elif a.cmd == "wait-boot":
            limit = float(a.args[0]) if a.args else 30
            t0 = time.time()
            while time.time() - t0 < limit:
                try:
                    _, payload, _ = request(link, ">log", a.verbose, timeout=3)
                    if payload and b"full quality refresh" in payload:
                        print(f"booted ({time.time() - t0:.1f} s)")
                        return
                except TimeoutError:
                    pass
                time.sleep(0.5)
            sys.exit("timeout waiting for boot")
    finally:
        link.close()


if __name__ == "__main__":
    main()
