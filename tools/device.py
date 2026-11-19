#!/usr/bin/env python3
"""Remote control for PaperOS over USB serial (or the simulator): frame dump, touch, log.

Run with PlatformIO's Python (it has pyserial):
    ~/.platformio/penv/bin/python tools/device.py shot out/device.png
    ~/.platformio/penv/bin/python tools/device.py tap 140 300
Commands: ls [DIR] | cat /data/power.csv | rm PATH | status (key names + lengths, Wi-Fi, heap) | diff A.png B.png [FIRST_ROW] | shot OUT.png | tap X Y | long X Y | swipe X1 Y1 X2 Y2 | log | refresh | set KEY VALUE | poweroff SECONDS [THEN_SECONDS] | wait-boot | diff A.png B.png
Options:  --port /dev/cu.usbmodemXXXX (default: the first /dev/cu.usbmodem*)
          --limit N   the whole run ends after N seconds with exit code 3 (it never hangs): default 90, script: from its length
          --sim   talk to .pio/build/sim/program --headless --remote instead of a device
          --wifi [HOST]  over Wi-Fi instead of USB (Settings > Developer > Wireless development on the device; D39):
                  HOST, else $PAPEROS_HOST, else the saved address (tools/ota.py host), else the last update's. The
                  device signs nobody in but the Mac with the update key (~/.config/paperos/ota-signing.key).
          -v      also print device log lines seen while waiting for a reply
The port is opened exclusively: a second tool on the same port ends at once with "in use by another program".
Live view in a browser (click = tap): tools/view.py.
Protocol: docs/ROADMAP.md P1-8, src/core/remote.h. The dump is the frame the firmware drew, not
what the panel physically shows (no ghosting, no half-played waveforms).
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys
import time
import zlib

W, H = 540, 960


WIFI_PORT = 8266  # update::kPort: wireless development
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_FILE = os.environ.get("PAPEROS_OTA_KEY") or os.path.expanduser("~/.config/paperos/ota-signing.key")


def wifi_host(host=None):
    """The device's address for --wifi: given, $PAPEROS_HOST, the saved one (tools/ota.py host), the last update's."""
    if host and host != "auto":
        return host
    if os.environ.get("PAPEROS_HOST"):
        return os.environ["PAPEROS_HOST"]
    for path in (os.path.join(os.path.dirname(KEY_FILE), "ota-host"), os.path.join(ROOT, "out", "ota", "last-host")):
        if os.path.exists(path):
            text = open(path).read().strip()
            if text:
                return text
    sys.exit("no address for --wifi: give it (--wifi 192.168.1.60), or save it once: uv run --script tools/ota.py host ADDRESS")


def sign(message):
    """Ed25519 signature with the update key (cryptography: in PlatformIO's Python and in the uv scripts)."""
    from cryptography.hazmat.primitives import serialization
    if not os.path.exists(KEY_FILE):
        sys.exit(f"no signing key at {KEY_FILE}: wireless development signs in with it (make ota-key made it)")
    with open(KEY_FILE, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    return key.sign(message)


class _Sock:
    """A socket that reads and writes like the serial port (readline, read, write, flush) with a timeout."""

    def __init__(self, sock):
        self.s = sock
        self.buf = b""

    def _more(self):
        chunk = self.s.recv(65536)
        if not chunk:
            raise TimeoutError("the device closed the connection")
        self.buf += chunk

    def readline(self):
        import socket
        try:
            while b"\n" not in self.buf:
                self._more()
        except socket.timeout:
            out, self.buf = self.buf, b""
            return out
        i = self.buf.index(b"\n") + 1
        out, self.buf = self.buf[:i], self.buf[i:]
        return out

    def read(self, n):
        import socket
        try:
            while len(self.buf) < n:
                self._more()
        except socket.timeout:
            pass
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def write(self, data):
        self.s.sendall(data)

    def flush(self):
        pass


class Link:
    """Line/byte transport to the firmware: a serial port, the simulator's stdio, or Wi-Fi (wireless development)."""

    def __init__(self, port=None, sim=False, sim_args=(), wifi=None):
        self.sim = sim
        self.wifi = wifi
        self.answered = False  # once the device answered, it is awake: no more resends (they only hide a hang)
        if wifi:
            import socket
            host = wifi_host(wifi)
            try:
                sock = socket.create_connection((host, WIFI_PORT), timeout=6)
            except OSError as e:
                sys.exit(f"no connection to {host}:{WIFI_PORT} ({e}): is Settings > Developer > Wireless development on?")
            sock.settimeout(10)
            self.r = self.w = _Sock(sock)
            hello = self.r.readline().decode(errors="replace").strip()  # #hello paperos 1 <build> <nonce hex>
            parts = hello.split()
            if len(parts) != 5 or parts[:3] != ["#hello", "paperos", "1"]:
                sys.exit(f"{host}:{WIFI_PORT} is no PaperOS device ({hello!r})")
            self.build = parts[3]
            self.w.write(b">auth " + sign(("paperos-remote:" + parts[4]).encode()).hex().encode() + b"\n")
            answer = self.r.readline().decode(errors="replace").strip()
            if answer != "#ok":
                sys.exit(f"the device did not sign this Mac in: {answer or 'no answer'} (a firmware with another update key?)")
            self.answered = True
            return
        if sim:
            root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            self.proc = subprocess.Popen(
                [os.path.join(root, ".pio/build/sim/program"), "--headless", "--remote", *sim_args],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=root)
            self.r, self.w = self.proc.stdout, self.proc.stdin
        else:
            import serial  # pyserial: use ~/.platformio/penv/bin/python

            port = port or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
            if not port:
                sys.exit("no device: /dev/cu.usbmodem* not found")
            self.ser = serial.Serial()
            self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 5
            self.ser.write_timeout = 5  # a device that does not read (asleep, busy) must not block the tool for ever
            # Do NOT touch dtr/rts: setting them while opening the USB-Serial/JTAG port walks
            # through the (DTR=0, RTS=1) reset state and reboots the board on every command.
            # One program per port: two readers each get half of the replies. The lock goes when the holder closes or ends.
            self.ser.exclusive = True
            try:
                self.ser.open()
            except serial.SerialException as e:
                if "lock" in str(e).lower() or "busy" in str(e).lower():
                    sys.exit(f"{port} is in use by another program (tools/view.py or another device tool): stop it first")
                raise
            self.r = self.w = self.ser

    def send(self, line):
        if not self.sim and not self.wifi:
            self.ser.reset_input_buffer()
        global LAST_COMMAND
        LAST_COMMAND = line
        try:
            self.w.write(line.encode() + b"\n")
            self.w.flush()
        except Exception as e:  # serial.SerialTimeoutException, a port that went away
            if type(e).__name__ in ("SerialTimeoutException", "SerialException", "OSError", "BrokenPipeError"):
                raise TimeoutError(f"cannot send {line!r}: {e}")
            raise

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
        """Close the port. Do not keep an open port unread (for example while a camera works): close it and open a new
        Link later. 2026-11-16: with the port held but unread for ~15 s, a >shot went unanswered for 60 s."""
        if self.wifi:
            self.w.s.close()
        elif self.sim:
            self.w.close()
            self.proc.wait(timeout=10)
        else:
            self.ser.close()


LAST_COMMAND = ""


def arm_limit(seconds):
    """The whole run ends after `seconds`, whatever blocks (a write, a read, the port): exit code 3 and a line that says where."""
    import signal

    def give_up(*_):
        print(f"device.py: gave up after {seconds} s (limit; last command sent: {LAST_COMMAND!r}). The device may be asleep, "
              f"busy or off: try `device.py status`.", file=sys.stderr, flush=True)
        os._exit(3)

    signal.signal(signal.SIGALRM, give_up)
    signal.alarm(int(seconds))


def request(link, cmd, verbose=False, timeout=10, retries=2):
    """Send one command; return (status_line, payload_bytes or None, header or None).

    A device that light-sleeps (no host seen on the USB bus: the port is closed between two tool runs) does not hear a
    command sent meanwhile (HARDWARE.md): it wakes by its own timer within a minute, so the FIRST command of a Link is sent
    again, `retries` more times, before giving up. Once the device has answered on this Link it is awake: a later command
    gets one try of `timeout` seconds and then fails at once (resends there only turned a hang into minutes of waiting)."""
    if link.answered:
        retries = 0
    for attempt in range(retries + 1):
        try:
            r = _request_once(link, cmd, verbose, timeout)
            link.answered = True
            return r
        except TimeoutError:
            if attempt == retries or link.sim:
                if link.answered:
                    raise TimeoutError(f"no reply to {cmd!r} in {timeout} s although the device answered before on this "
                                       f"link: busy or stalled; check `device.py log` / `status` now")
                raise
            print(f"| no reply to {cmd!r} in {timeout} s (asleep or busy?), sending it again ({attempt + 2}/{retries + 1})", file=sys.stderr, flush=True)


def _request_once(link, cmd, verbose, timeout):
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
        elif line.startswith("#log ") or line.startswith("#refresh ") or line.startswith("#status ") or line.startswith("#ls ") or line.startswith("#cat ") or line.startswith("#state "):
            header = line.split()
            payload = link.read(int(header[1]))
        elif line.startswith("#ok") or line.startswith("#err"):
            return line, payload, header
        elif verbose:
            print("| " + line, file=sys.stderr)
    raise TimeoutError(f"no reply to {cmd!r}")


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def png_bytes(pixels, w=W, h=H):
    """Gray 8-bit pixels as a PNG file in memory."""
    raw = b"".join(b"\x00" + pixels[y * w:(y + 1) * w] for y in range(h))

    def chunk(t, d):
        body = t + d
        return struct.pack(">I", len(d)) + body + struct.pack(">I", crc32(body))

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    return data + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")


def write_png(path, pixels, w=W, h=H):
    with open(path, "wb") as f:
        f.write(png_bytes(pixels, w, h))


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


_HIGH = bytes((b >> 4) * 17 for b in range(256))
_LOW = bytes((b & 15) * 17 for b in range(256))


def unpack4(packed):
    """Two 4-bit pixels per byte -> one gray byte per pixel (0, 17, .. 255)."""
    out = bytearray(len(packed) * 2)
    out[0::2] = packed.translate(_HIGH)
    out[1::2] = packed.translate(_LOW)
    return bytes(out)


# ---- UI scripts on the device (task P7-2): the simulator's script commands through the remote commands.

SCRIPT_UNSUPPORTED = {
    "type": "needs the key positions of the on-screen keyboard (not exposed over the link)",
    "type-max": "needs the key positions of the on-screen keyboard (not exposed over the link)",
    "wifi": "the fake Wi-Fi world exists only in the simulator",
    "restart": "a restart on the device is a real power cycle (use >poweroff N by hand)",
    "power-off": "never powers the device off from a script",
    "timer-wake": "an RTC wake on the device is a real power cycle (use >poweroff N by hand)",
    "expect-off": "the device cannot answer while it is off",
}


def script_plan(path):
    """Parses a script: (app, [(lineno, cmd, args)], [reasons it cannot run on the device])."""
    app, steps, blocked = "", [], []
    for no, raw in enumerate(open(path, encoding="utf-8").read().splitlines(), 1):
        line = raw.strip()
        if line.startswith("# app:"):
            app = line[len("# app:"):].strip()
            continue
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cmd, _, args = line.partition(" ")
        if app == "fonts" and not blocked:
            blocked.append("the font sheet is drawn by the Os, not a screen `>set screen` can open")
        if cmd in SCRIPT_UNSUPPORTED:
            blocked.append(f"line {no}: '{cmd}': {SCRIPT_UNSUPPORTED[cmd]}")
        steps.append((no, cmd, args.strip()))
    return app, steps, blocked


def run_script(link, path, out_dir, golden_dir, verbose=False):
    """Runs a UI script against the device (or the simulator with --sim). Returns the exit code.

    Every `shot` is saved as OUT/<name>.png and compared with the golden: identical, identical below the status bar (the top 56 rows
    hold the live clock and battery), or different (data on the device differs from the fixtures: expected for many screens)."""
    app, steps, blocked = script_plan(path)
    if blocked:
        print(f"cannot run {path} on the device:")
        for b in blocked:
            print("  " + b)
        return 2
    os.makedirs(out_dir, exist_ok=True)

    def ok(cmd, timeout=10):
        status, payload, hdr = request(link, cmd, verbose, timeout=timeout)
        if not status.startswith("#ok"):
            raise RuntimeError(f"{cmd}: {status}")
        return payload, hdr

    def refresh_entries():
        payload, _ = ok(">refresh")
        entries = []
        for l in (payload or b"").decode(errors="replace").splitlines():
            f = dict(p.split("=", 1) for p in l.split() if "=" in p)
            if "t" in f and "mode" in f:
                entries.append((int(f["t"]), f["mode"], f.get("rect", "0,0,0,0")))
        return entries

    def state():
        payload, _ = ok(">state")
        return (payload or b"").decode(errors="replace")

    def tap(x, y):
        ok(f">tap {x} {y}")

    ok(">set screen " + (app or "launcher"))
    time.sleep(1.0)
    last_t = max([e[0] for e in refresh_entries()] or [0])
    results, failed = [], 0
    for no, cmd, args in steps:
        try:
            if verbose:
                print(f"  {os.path.basename(path)}:{no}: {cmd} {args}", flush=True)
            if cmd == "wait":
                time.sleep(int(args) / 1000.0)
            elif cmd == "shot":
                name = args.split()[0]
                payload, hdr = ok(">shot", timeout=10)
                w, h = int(hdr[1]), int(hdr[2])
                pixels = unpack4(payload)
                write_png(os.path.join(out_dir, name + ".png"), pixels, w, h)
                gold = os.path.join(golden_dir, name + ".png")
                if not os.path.exists(gold):
                    verdict = "no golden"
                else:
                    gw, gh, gp = read_png(gold)
                    bad = sum(1 for a, b in zip(pixels, gp) if a != b)
                    below = sum(1 for a, b in zip(pixels[56 * w:], gp[56 * w:]) if a != b)
                    verdict = "IDENTICAL" if bad == 0 else "identical below the status bar" if below == 0 else f"DIFFERENT ({below} pixels below the status bar)"
                results.append((name, verdict))
                print(f"  shot {name}: {verdict}")
                last_t = max([e[0] for e in refresh_entries()] or [last_t])
            elif cmd in ("tap", "long"):
                x, y = args.split()[:2]
                ok(f">{cmd} {x} {y}")
                time.sleep(0.15)  # the touch is played out by the firmware (down, 40 ms, up): let it finish before the next command
            elif cmd == "back":
                tap(36, 92)
            elif cmd == "swipe":
                ok(">swipe " + " ".join(args.split()[:4]))
            elif cmd == "expect-no-quality":
                q = [e for e in refresh_entries() if e[0] > last_t and e[1] == "Q"]
                if q:
                    raise RuntimeError(f"expect-no-quality: {len(q)} Quality push(es) since the last shot")
            elif cmd == "expect-max-pushed":
                area = 0
                for e in refresh_entries():
                    if e[0] > last_t:
                        r = [int(v) for v in e[2].split(",")]
                        area += r[2] * r[3]
                if area > int(args.split()[0]):
                    raise RuntimeError(f"expect-max-pushed {args}: {area} pixels pushed since the last shot")
            elif cmd == "expect-state":
                s = state()
                if args not in s:
                    raise RuntimeError(f"expect-state '{args}': the screen says '{s[:80]}'")
            elif cmd in ("expect-shot-equal", "expect-frame-equal"):  # on the device a shot IS the drawn frame
                a_name, b_name = args.split()[:2]
                pa = read_png(os.path.join(out_dir, a_name + ".png"))[2]
                pb = read_png(os.path.join(out_dir, b_name + ".png"))[2]
                if pa != pb:
                    raise RuntimeError(f"{cmd}: {a_name} and {b_name} differ")
            elif cmd in ("page-through", "page-until"):
                parts = args.split()
                max_taps = int(parts[0])
                want = " ".join(parts[1:]) if cmd == "page-until" else None
                done = False
                for _ in range(max_taps):
                    s = state()
                    m = re.search(r"page=(\d+)/(\d+)", s)
                    if not m:
                        time.sleep(0.3)
                        continue
                    if (want is not None and want in s) or (want is None and int(m.group(1)) >= int(m.group(2))):
                        done = True
                        break
                    tap(400, 500)
                    time.sleep(0.9)
                if not done:
                    raise RuntimeError(f"{cmd}: not reached in {max_taps} taps")
            else:
                raise RuntimeError(f"unknown command '{cmd}'")
        except (RuntimeError, TimeoutError) as e:
            print(f"{path}:{no}: {e}")
            failed += 1
            break
    same = sum(1 for _, v in results if v.startswith("IDENTICAL") or v.startswith("identical"))
    print(f"{os.path.basename(path)}: {len(results)} shot(s), {same} equal to the golden" + (", FAILED" if failed else ""))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("--wifi", nargs="?", const="auto", metavar="HOST", help="over Wi-Fi (Settings > Developer > Wireless development)")
    ap.add_argument("--sim-data", help="--sim: the simulator's data root (default sim-data/), e.g. a test card with a Wi-Fi network")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("cmd", choices=["shot", "tap", "long", "swipe", "log", "refresh", "status", "ls", "cat", "rm", "set", "poweroff", "wait-boot", "diff", "script", "scripts"])
    ap.add_argument("--out", default="out/device-ui", help="script: where the frame dumps go")
    ap.add_argument("--golden", default="tests/ui/golden", help="script: goldens to compare with")
    ap.add_argument("--limit", type=int, default=0, help="give up after this many seconds (default: 90, wait-boot and poweroff 120, script: its waits + 20 s per step + 60)")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # progress shows at once, also through a pipe

    limit = a.limit
    if not limit:
        limit = 90
        if a.cmd in ("wait-boot", "poweroff"):
            limit = 120
        elif a.cmd == "script" and a.args and os.path.exists(a.args[0]):
            _, steps, _ = script_plan(a.args[0])
            waits = sum(int(s[2].split()[0]) for s in steps if s[1] == "wait" and s[2].split()[:1] and s[2].split()[0].isdigit()) / 1000.0
            turns = sum(int(s[2].split()[0]) for s in steps if s[1] in ("page-through", "page-until") and s[2].split()[0].isdigit())
            limit = int(60 + waits + 20 * len(steps) + 2 * turns)
    if a.cmd not in ("scripts", "diff"):
        arm_limit(limit)

    if a.cmd == "scripts":
        # Which UI scripts can run on the device, and which of those can be compared with the goldens.
        for path in sorted(glob.glob("tests/ui/*.script")):
            name = os.path.basename(path)[:-len(".script")]
            _, _, blocked = script_plan(path)
            preset = os.path.isdir(os.path.join("tests/ui/data", name))
            if blocked:
                print(f"cannot   {name}: {blocked[0].split(': ', 1)[-1]}")
            elif preset:
                print(f"data     {name}: runs, but its goldens come from preset card data and HTTP fixtures (tests/ui/data/{name}): the device has its own")
            else:
                print(f"compare  {name}")
        return

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

    link = Link(a.port, a.sim, ("--data", a.sim_data) if a.sim_data else (), wifi=a.wifi)
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
        elif a.cmd == "status":
            status, payload, _ = request(link, ">status", a.verbose)
            sys.stdout.write((payload or b"").decode(errors="replace"))
            if not status.startswith("#ok"):
                sys.exit(status)
        elif a.cmd == "ls":
            status, payload, _ = request(link, ">ls " + (a.args[0] if a.args else "/sd/books"), a.verbose)
            if not status.startswith("#ok"):
                sys.exit(status)
            sys.stdout.write((payload or b"").decode(errors="replace"))
        elif a.cmd == "script":
            if len(a.args) != 1:
                sys.exit("usage: script tests/ui/NAME.script [--out DIR] [--golden DIR]")
            sys.exit(run_script(link, a.args[0], a.out, a.golden, a.verbose))
        elif a.cmd == "cat":
            if len(a.args) != 1:
                sys.exit("usage: cat /data/power.csv")
            status, payload, _ = request(link, ">cat " + a.args[0], a.verbose)
            if not status.startswith("#ok"):
                sys.exit(status)
            sys.stdout.write((payload or b"").decode(errors="replace"))
        elif a.cmd == "rm":
            if len(a.args) != 1:
                sys.exit("usage: rm PATH")
            status, _, _ = request(link, ">rm " + a.args[0], a.verbose)
            print(status)
            if not status.startswith("#ok"):
                sys.exit(1)
        elif a.cmd == "refresh":
            status, payload, _ = request(link, ">refresh", a.verbose)
            sys.stdout.write((payload or b"").decode(errors="replace"))
            if not status.startswith("#ok"):
                sys.exit(status)
        elif a.cmd == "set":
            if len(a.args) != 2:
                sys.exit("usage: set KEY VALUE")
            status, _, _ = request(link, ">set " + " ".join(a.args), a.verbose)
            print(status)
            if not status.startswith("#ok"):
                sys.exit(1)
        elif a.cmd == "poweroff":
            # Cuts the board's power (and the SD card's: the way to unwedge a card) and boots again after SECONDS.
            # With a clock the wake runs the scheduled refresh (P7-5) and stays on; THEN_SECONDS: that refresh switches off once more.
            if len(a.args) not in (1, 2) or not all(x.isdigit() and 1 <= int(x) <= 3600 for x in a.args):
                sys.exit("usage: poweroff SECONDS [THEN_SECONDS] (1..3600)")
            try:
                status, _, _ = request(link, ">poweroff " + " ".join(a.args), a.verbose, timeout=3, retries=0)
                print(status)
            except TimeoutError:
                print("no reply (the board lost power first)")
            print(f"the device boots again in about {a.args[0]} s: use wait-boot")
        elif a.cmd == "log":
            status, payload, _ = request(link, ">log", a.verbose)
            sys.stdout.write((payload or b"").decode(errors="replace"))
            if not status.startswith("#ok"):
                sys.exit(status)
        elif a.cmd == "wait-boot":
            limit = float(a.args[0]) if a.args else 30
            t0 = time.time()
            while time.time() - t0 < limit:
                try:
                    _, payload, _ = request(link, ">log", a.verbose, timeout=3, retries=0)
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
