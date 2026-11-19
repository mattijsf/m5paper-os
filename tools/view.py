#!/usr/bin/env python3
"""Live view of PaperOS in a browser: the frame the firmware drew, and the mouse (or a finger) as touch.

Run with PlatformIO's Python (it has pyserial):
    ~/.platformio/penv/bin/python tools/view.py            # the device on USB, opens http://127.0.0.1:8137/
    ~/.platformio/penv/bin/python tools/view.py --sim      # the simulator (.pio/build/sim/program --headless --remote)
    ~/.platformio/penv/bin/python tools/view.py --lan      # also reachable from a phone on the same network
In the page: click = tap, hold 600 ms = long press, drag = swipe (the gesture limits of docs/UI.md).

Options:  --port /dev/cu.usbmodemXXXX   (default: the first /dev/cu.usbmodem*)
          --http 8137    the port of the page
          --lan          listen on every interface; the address then carries a random key, because whoever opens the page
                         sees the screen (inbox, mail) and can tap. Without --lan only this computer can open it.
          --idle 300     stop after this many seconds without an open page (0 = never): the viewer holds the serial port,
                         and the device does not sleep while a host holds it
          --limit N      stop after N seconds whatever happens (tests)
          --no-open      do not open the browser
          -v             one line per frame

How it works: one thread owns the link. It asks `>refresh` (small) a few times per second; when the refresh log changed,
after every input, and every 5 s anyway, it takes a `>shot` (~0.5 s on the device) and hands the PNG to the page. So the
page follows the device within about a second, at most ~2 frames per second: enough for e-ink.

Limits: it shows the frame buffer, not the panel (no ghosting, no flash: that is tools/cam.py). An injected touch cannot
wake a sleeping chip, but the device does not sleep while this tool holds the port. Stop the viewer before running
tools/device.py, push.py, latency.py or memory.py: two programs cannot share the serial port.
Protocol: src/core/remote.h.
"""
import argparse
import glob
import json
import os
import queue
import re
import secrets
import signal
import socket
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from device import H, W, Link, crc32, png_bytes, request, unpack4  # noqa: E402

POLL = 0.15       # seconds between two `>refresh` polls while nothing happens
FULL_EVERY = 5.0  # a frame dump at least this often, in case a change left no line in the refresh log
INPUT = re.compile(r"^(?:(tap|long) (\d{1,3}) (\d{1,3})|(swipe) (\d{1,3}) (\d{1,3}) (\d{1,3}) (\d{1,3}))$")


def valid_input(text):
    """'tap X Y', 'long X Y' or 'swipe X1 Y1 X2 Y2' with every point on the panel."""
    m = INPUT.match(text)
    if not m:
        return False
    nums = [int(n) for n in text.split()[1:]]
    return all(0 <= x < W for x in nums[0::2]) and all(0 <= y < H for y in nums[1::2])


class Viewer:
    """Owns the link to the device (one thread) and the newest frame (read by the HTTP threads)."""

    def __init__(self, port, sim, verbose, wifi=None):
        self.wifi = wifi
        self.port, self.sim, self.verbose = port, sim, verbose
        self.cond = threading.Condition()
        self.png, self.frame = b"", 0
        self.status, self.status_n = "connecting", 0
        self.inputs = queue.Queue(maxsize=8)
        self.last_seen = time.time()  # the last request of a page
        self.stop = False

    def say(self, text):
        with self.cond:
            if text == self.status:
                return
            self.status = text
            self.status_n += 1
            self.cond.notify_all()
        print(time.strftime("%H:%M:%S ") + text, flush=True)

    def run(self):
        while not self.stop:
            link = None
            try:
                if not self.sim and not self.port and not glob.glob("/dev/cu.usbmodem*"):
                    self.say("no device on USB (/dev/cu.usbmodem*): waiting for it")
                else:
                    link = Link(self.port, self.sim, wifi=self.wifi)
                    self.session(link)
            except TimeoutError:
                self.say("the device does not answer (asleep, busy or off): trying again")
            except SystemExit as e:  # Link: no device, or the port is held by another program
                self.say(f"{e}: trying again")
            except Exception as e:  # serial.SerialException, OSError: the port went away (unplugged, reboot)
                self.say(f"link lost ({type(e).__name__}): trying again")
            finally:
                if link:
                    try:
                        link.close()
                    except Exception:
                        pass
            for _ in range(10):  # 1 s, but stop at once
                if self.stop:
                    break
                time.sleep(0.1)

    def session(self, link):
        mark, shot_at, shown = None, 0.0, None
        name = "simulator" if self.sim else "device"
        while not self.stop:
            sent = False
            try:
                cmd = self.inputs.get(timeout=POLL)
            except queue.Empty:
                cmd = None
            while cmd:
                status, _, _ = request(link, ">" + cmd, timeout=5)
                if not status.startswith("#ok"):
                    print(f"{time.strftime('%H:%M:%S')} {cmd}: {status}", flush=True)
                sent = True
                try:
                    cmd = self.inputs.get_nowait()
                except queue.Empty:
                    cmd = None
            if sent:
                time.sleep(0.12)  # the firmware plays the touch (down, 40 ms, up): let the screen react before asking
            # The refresh log is read BEFORE the dump: a change after this point changes the log again, so it is never missed.
            _, payload, _ = request(link, ">refresh", timeout=5)
            now_mark = crc32(payload or b"")
            if sent or now_mark != mark or time.time() - shot_at > FULL_EVERY:
                mark = now_mark
                t0 = time.time()
                status, payload, hdr = request(link, ">shot", timeout=10)
                shot_at = time.time()
                if not status.startswith("#ok") or payload is None:
                    self.say(f"frame dump failed: {status}")
                    continue
                w, h, crc = int(hdr[1]), int(hdr[2]), int(hdr[4], 16)
                if crc32(payload) != crc:
                    print(f"{time.strftime('%H:%M:%S')} frame dropped: CRC mismatch", flush=True)
                    mark = None  # take it again
                    continue
                if crc != shown:
                    shown = crc
                    png = png_bytes(unpack4(payload), w, h)
                    with self.cond:
                        self.png = png
                        self.frame += 1
                        self.cond.notify_all()
                    if self.verbose:
                        print(f"{time.strftime('%H:%M:%S')} frame {self.frame}: dump {1000 * (shot_at - t0):.0f} ms, "
                              f"{len(png) // 1024} KB", flush=True)
            self.say(f"live: {name}")


PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>PaperOS live view</title>
<style>
  :root { color-scheme: light dark; --bg: #e8e6e1; --fg: #2b2b2b; --muted: #6f6c66; --bezel: #d3d0c8; --touch: #d2401e; }
  @media (prefers-color-scheme: dark) { :root { --bg: #1b1b1b; --fg: #e6e6e6; --muted: #98958f; --bezel: #303030; } }
  html, body { height: 100%; margin: 0; }
  body { background: var(--bg); color: var(--fg); font: 13px/1.3 system-ui, sans-serif; display: flex;
         flex-direction: column; align-items: center; justify-content: center; gap: 18px; overflow: hidden; }
  #panel { position: relative; aspect-ratio: __W__ / __H__;
           height: min(calc(100dvh - 84px), calc((100vw - 52px) * __H__ / __W__));
           background: #fff; border-radius: 4px; box-shadow: 0 0 0 10px var(--bezel), 0 10px 30px rgba(0, 0, 0, .25);
           touch-action: none; user-select: none; -webkit-user-select: none; -webkit-touch-callout: none; cursor: pointer; }
  #frame { width: 100%; height: 100%; display: block; pointer-events: none; border-radius: 4px; }
  #frame:not([src]) { visibility: hidden; }
  #mark { position: absolute; width: 28px; height: 28px; margin: -14px 0 0 -14px; border: 3px solid var(--touch);
          border-radius: 50%; box-sizing: border-box; opacity: 0; pointer-events: none; transition: opacity .3s; }
  #mark.on { opacity: .9; transition: none; }
  #bar { color: var(--muted); display: flex; gap: 14px; white-space: nowrap; }
  #status { color: var(--fg); }
  @media (max-width: 520px) { #hint { display: none; } }
</style>
</head>
<body>
<div id="panel"><img id="frame" alt="PaperOS screen" draggable="false"><div id="mark"></div></div>
<div id="bar"><span id="status">connecting</span><span id="hint">click: tap &middot; hold: long press &middot; drag: swipe</span></div>
<script>
const W = __W__, H = __H__, MOVE = 24, HOLD = 600;  // gesture limits of docs/UI.md
const panel = document.getElementById('panel'), img = document.getElementById('frame');
const mark = document.getElementById('mark'), statusEl = document.getElementById('status');
let frame = -1, statusN = -1, down = null, markTimer = 0;

function point(e) {
  const r = panel.getBoundingClientRect();
  const clamp = (v, max) => Math.max(0, Math.min(max, Math.round(v)));
  return [clamp((e.clientX - r.left) * W / r.width, W - 1), clamp((e.clientY - r.top) * H / r.height, H - 1)];
}
function send(cmd, p) {
  fetch('input', {method: 'POST', body: cmd}).catch(() => {});
  mark.style.left = (100 * p[0] / W) + '%';
  mark.style.top = (100 * p[1] / H) + '%';
  mark.classList.add('on');
  clearTimeout(markTimer);
  markTimer = setTimeout(() => mark.classList.remove('on'), 1500);
}
function far(a, b) { return Math.abs(b[0] - a[0]) >= MOVE || Math.abs(b[1] - a[1]) >= MOVE; }

panel.addEventListener('pointerdown', e => {
  if (e.button || down) return;
  e.preventDefault();
  try { panel.setPointerCapture(e.pointerId); } catch (err) {}  // keeps the drag when it leaves the panel
  const d = down = {id: e.pointerId, from: point(e), last: point(e), timer: 0};
  d.timer = setTimeout(() => {  // a long press fires while still down, as on the device
    if (down === d && !far(d.from, d.last)) { down = null; send(`long ${d.from[0]} ${d.from[1]}`, d.from); }
  }, HOLD);
});
panel.addEventListener('pointermove', e => { if (down && e.pointerId === down.id) down.last = point(e); });
panel.addEventListener('pointerup', e => {
  if (!down || e.pointerId !== down.id) return;
  const a = down.from, b = point(e);
  clearTimeout(down.timer);
  down = null;
  if (far(a, b)) send(`swipe ${a[0]} ${a[1]} ${b[0]} ${b[1]}`, b);
  else send(`tap ${a[0]} ${a[1]}`, a);
});
panel.addEventListener('pointercancel', () => { if (down) clearTimeout(down.timer); down = null; });
panel.addEventListener('contextmenu', e => e.preventDefault());

async function follow() {
  for (;;) {
    try {
      const reply = await fetch(`state?v=${frame}&s=${statusN}`);
      if (!reply.ok) throw new Error(reply.status);
      const state = await reply.json();
      if (state.v !== frame) {
        frame = state.v;
        if (frame > 0) { img.src = `frame.png?v=${frame}`; mark.classList.remove('on'); }
      }
      statusN = state.s;
      statusEl.textContent = state.status;
    } catch (e) {
      statusEl.textContent = 'the viewer stopped (tools/view.py)';
      await new Promise(done => setTimeout(done, 1000));
    }
  }
}
follow();
</script>
</body>
</html>
""".replace("__W__", str(W)).replace("__H__", str(H))


def make_handler(viewer, prefix, hosts):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def send(self, code, body=b"", ctype="text/plain; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def route(self):
            """The path below the prefix, or None after answering 403/404. Without --lan only this computer's own names are
            accepted as Host (another site cannot reach the page through a DNS name that points here)."""
            if hosts and self.headers.get("Host", "") not in hosts:
                self.send(403, b"wrong host")
                return None, None
            path, _, query = self.path.partition("?")
            if not path.startswith(prefix):
                self.send(404, b"not found")
                return None, None
            viewer.last_seen = time.time()
            return path[len(prefix):], parse_qs(query)

        def do_GET(self):
            path, query = self.route()
            if path is None:
                return
            if path == "":
                self.send(200, PAGE.encode(), "text/html; charset=utf-8")
            elif path == "state":
                def number(key):
                    try:
                        return int(query.get(key, ["-1"])[0])
                    except ValueError:
                        return -1
                v, s, deadline = number("v"), number("s"), time.time() + 20
                with viewer.cond:
                    while viewer.frame == v and viewer.status_n == s and not viewer.stop and time.time() < deadline:
                        viewer.cond.wait(timeout=max(0.05, deadline - time.time()))
                    body = json.dumps({"v": viewer.frame, "s": viewer.status_n, "status": viewer.status})
                viewer.last_seen = time.time()
                self.send(200, body.encode(), "application/json")
            elif path == "frame.png":
                with viewer.cond:
                    png = viewer.png
                self.send(200 if png else 404, png, "image/png")
            else:
                self.send(404, b"not found")

        def do_POST(self):
            path, _ = self.route()
            if path is None:
                return
            # A page of another site must not tap the device: the browser names the calling site in Origin.
            origin = self.headers.get("Origin")
            if origin and urlsplit(origin).netloc != self.headers.get("Host"):
                return self.send(403, b"wrong origin")
            try:
                size = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                size = -1
            if path != "input" or not 0 < size <= 64:
                return self.send(400, b"bad request")
            text = self.rfile.read(size).decode(errors="replace").strip()
            if not valid_input(text):
                return self.send(400, b"bad input")
            try:
                viewer.inputs.put_nowait(text)
            except queue.Full:
                return self.send(429, b"too many inputs waiting")
            self.send(204)

    return Handler


def lan_address():
    """This computer's address on the local network (no packet is sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("--wifi", action="store_true", help="over Wi-Fi (Settings > Developer > Wireless development)")
    ap.add_argument("--host", help="--wifi: the device's address (default: $PAPEROS_HOST, the saved one, the last update's)")
    ap.add_argument("--http", type=int, default=8137)
    ap.add_argument("--lan", action="store_true")
    ap.add_argument("--idle", type=int, default=300)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--no-open", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    viewer = Viewer(a.port, a.sim, a.verbose, (a.host or "auto") if a.wifi else None)
    if a.lan:
        prefix, hosts, bind = f"/{secrets.token_urlsafe(9)}/", None, "0.0.0.0"
        url = f"http://{lan_address()}:{a.http}{prefix}"
    else:
        prefix, bind = "/", "127.0.0.1"
        hosts = {f"127.0.0.1:{a.http}", f"localhost:{a.http}"}
        url = f"http://127.0.0.1:{a.http}/"
    try:
        server = ThreadingHTTPServer((bind, a.http), make_handler(viewer, prefix, hosts))
    except OSError as e:
        sys.exit(f"cannot listen on port {a.http} ({e.strerror}): is a viewer already running? Try --http {a.http + 1}")
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    worker = threading.Thread(target=viewer.run, daemon=True)
    worker.start()
    print(f"PaperOS live view: {url}   (Ctrl-C stops it)", flush=True)
    if a.lan:
        print("whoever has this address sees the screen and can tap: it works until the viewer stops", flush=True)
    if not a.no_open:
        webbrowser.open(url)

    def ask_stop(*_):
        raise KeyboardInterrupt

    # Also when started in the background, where the shell leaves SIGINT ignored: stop cleanly and release the port.
    signal.signal(signal.SIGINT, ask_stop)
    signal.signal(signal.SIGTERM, ask_stop)
    started, why = time.time(), "stopped"
    try:
        while True:
            time.sleep(0.5)
            if a.limit and time.time() - started > a.limit:
                why = f"stopped after {a.limit} s (--limit)"
                break
            if a.idle and time.time() - viewer.last_seen > a.idle:
                why = f"stopped: no open page for {a.idle} s (--idle)"
                break
    except KeyboardInterrupt:
        pass
    viewer.stop = True
    with viewer.cond:
        viewer.cond.notify_all()
    worker.join(timeout=12)  # lets a running command finish and closes the port
    server.shutdown()
    print(why, flush=True)


if __name__ == "__main__":
    main()
