#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["opencv-python-headless>=4.8", "numpy>=1.24", "pyserial>=3.5"]
# ///
"""Webcam on the M5PaperS3 panel: what the panel physically shows (ghosting, flash, refresh timing, gray levels).

    uv run --script tools/cam.py devices                 cameras macOS offers (avfoundation) and their modes
    uv run --script tools/cam.py shot [--out out/cam.png]   one still (after a warm-up for exposure)
    uv run --script tools/cam.py video [--seconds 5] [--fps 60] [--out out/cam.mp4]
    uv run --script tools/cam.py calibrate [--image out/cam.png] [--frame out/device.png]
    uv run --script tools/cam.py compare [--image out/cam.png] [--frame out/device.png]
    uv run --script tools/cam.py around tap X Y | long X Y | swipe X1 Y1 X2 Y2 [--before 1] [--after 4]
    uv run --script tools/cam.py lock [--focus auto|N]   exposure, white balance, focus fixed for this session (uvc-util)
    uv run --script tools/cam.py unlock
    uv run --script tools/cam.py selftest              synthetic camera images: registration, ghost number, timing

The camera set-up is NOT stable over days (it can be moved, daylight changes): nothing here is a golden.
Every run registers the panel afresh (SIFT match of the camera image against the frame dump of the same moment, or
ArUco markers 0-3 learnt by the same calibration), and every number is normalised by the white and black seen in the
same image. Compare numbers only within one session, before against after. The camera is a helper, never a pass/fail gate.

Options:  --camera NAME|INDEX   default: $PAPEROS_CAM, else the first camera that is not built in. Screen capture
                                devices are never used (they would record whatever is on the Mac's screen).
          --source FILE        replay a video file in real time instead of a camera (tests without a camera)
          --limit N            the whole run ends after N seconds with exit code 3 (default: from the command)
          -v                   print ffmpeg's command line and the device log lines
Output goes to out/ (gitignored). The panel can show private messages: never commit, upload or paste these images.
"""
import argparse
import glob
import importlib.util
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 540, 960  # panel, portrait, as the frame dump
CORNERS = os.path.join(ROOT, "out", "cam-corners.json")
VERBOSE = False
CHILDREN = []  # ffmpeg processes to kill when the run limit fires


def say(msg):
    print(msg, flush=True)


def arm_limit(seconds):
    def give_up(*_):
        for p in CHILDREN:
            try:
                p.kill()
            except Exception:
                pass
        print(f"cam.py: gave up after {seconds} s (limit). Camera unplugged or busy? Try `cam.py devices`.", file=sys.stderr, flush=True)
        os._exit(3)

    signal.signal(signal.SIGALRM, give_up)
    signal.alarm(int(seconds))


# ---------------------------------------------------------------- cameras (ffmpeg avfoundation)

def ffmpeg():
    exe = shutil.which("ffmpeg")
    if not exe:
        sys.exit("ffmpeg not found (brew install ffmpeg)")
    return exe


def list_cameras():
    """[(index, name)] of avfoundation video devices."""
    r = subprocess.run([ffmpeg(), "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""],
                       capture_output=True, text=True, timeout=20)
    cams, video = [], False
    for line in r.stderr.splitlines():
        if "video devices" in line:
            video = True
        elif "audio devices" in line:
            video = False
        else:
            m = re.search(r"\]\s\[(\d+)\]\s(.+)$", line)
            if m and video:
                cams.append((int(m.group(1)), m.group(2).strip()))
    return cams


def is_screen(name):
    return "capture screen" in name.lower()


def is_builtin(name):
    n = name.lower()
    return "facetime" in n or "built-in" in n or "ingebouwd" in n


def pick_camera(want):
    cams = list_cameras()
    want = want or os.environ.get("PAPEROS_CAM")
    if want:
        for i, n in cams:
            if (want.isdigit() and int(want) == i) or want.lower() == n.lower():
                if is_screen(n):
                    sys.exit(f"refused: '{n}' records the Mac's screen, not the panel")
                return n
        sys.exit(f"camera '{want}' not found; cameras: " + ", ".join(f"[{i}] {n}" for i, n in cams))
    for _, n in cams:
        if not is_screen(n) and not is_builtin(n):
            return n
    sys.exit("no external camera found (only: " + ", ".join(n for _, n in cams) + "). Plug it in, or pass --camera "
             "'FaceTime HD-camera' to use the built-in one, or --source FILE to replay a video.")


def camera_modes(name):
    """Supported 'WxH@fps' modes, from ffmpeg's error message for an impossible frame rate."""
    r = subprocess.run([ffmpeg(), "-hide_banner", "-f", "avfoundation", "-framerate", "1234", "-i", f"{name}:none", "-frames:v", "1",
                        "-f", "null", "-"], capture_output=True, text=True, timeout=20)
    modes = []
    for line in r.stderr.splitlines():
        m = re.search(r"(\d+)x(\d+)@\[([\d.]+)\s+([\d.]+)\]fps", line)
        if m:
            modes.append((int(m.group(1)), int(m.group(2)), float(m.group(4))))
    if "not authorized" in r.stderr.lower() or "permission" in r.stderr.lower():
        sys.exit("macOS has not given this terminal Camera permission: System Settings > Privacy & Security > Camera")
    return sorted(set(modes), key=lambda m: (-m[0] * m[1], -m[2]))


USB_BYTES_PER_S = 20e6  # what avfoundation gets through for uncompressed frames (C922: 640x480 @ 30 = 18 MB/s ran at 31 fps,
                        # 864x480 @ 30 = 25 MB/s at 15 fps, 1280x720 and 1920x1080 at 10 fps); MJPEG cannot be chosen


def best_mode(name, fps):
    """fps 0: the largest mode (stills). Otherwise the largest mode that reaches `fps` within the USB budget, trying lower
    rates down to 15 if none fits; else the largest mode at its best rate. None if ffmpeg lists none."""
    modes = camera_modes(name)
    if not modes:
        return None
    if fps <= 0:
        m = modes[0]
        return m[0], m[1], int(round(min(m[2], 30)))
    for want in sorted({fps, 30, 25, 20, 15}, reverse=True):
        if want > fps:
            continue
        fast = [m for m in modes if m[2] >= want - 0.5 and m[0] * m[1] * 2 * want <= USB_BYTES_PER_S]
        if fast:
            return fast[0][0], fast[0][1], want
    top = max(m[2] for m in modes)
    m = [m for m in modes if m[2] == top][0]
    return m[0], m[1], min(fps, int(round(top)))


def input_args(a, fps):
    """ffmpeg input arguments for the camera (or --source), and a short description."""
    if a.source:
        if not os.path.exists(a.source):
            sys.exit(f"--source {a.source}: no such file")
        return ["-re", "-i", a.source], f"file {a.source}", "direct"
    name = pick_camera(a.camera)
    mode = best_mode(name, fps)
    if getattr(a, "size", None):
        w_, h_ = (int(v) for v in a.size.lower().split("x"))
        mode = (w_, h_, mode[2] if mode else fps)
    args = ["-f", "avfoundation", "-use_wallclock_as_timestamps", "1"]
    desc = f"camera '{name}'"
    if mode:
        args += ["-framerate", str(mode[2]), "-video_size", f"{mode[0]}x{mode[1]}"]
        desc += f" {mode[0]}x{mode[1]} @ {mode[2]} fps"
    args += ["-i", f"{name}:none"]
    return args, desc, VIA


VIA = "direct"  # how ffmpeg runs for a camera: "direct" (child process) or "terminal" (Terminal.app, see FFmpeg)


class FFmpeg:
    """One ffmpeg run. "direct": a child process. "terminal": started in Terminal.app through a .command file.

    Why Terminal: macOS gives camera access per app, and the app that launched the shell is responsible. An editor that
    never asks for the camera gets no Camera prompt (it is not in System Settings > Privacy & Security > Camera), so ffmpeg started from its
    shell opens the camera and receives no frames, silently. Terminal can be allowed once. stdout then goes through a FIFO,
    stderr through a file, and stop() sends SIGTERM (ffmpeg finishes the mp4 cleanly)."""

    def __init__(self, cmd, via, want_stdout=False):
        self.via = via
        self.returncode = None
        if VERBOSE:
            say(f"  $ ({via}) " + " ".join(cmd))
        if via == "direct":
            self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE if want_stdout else subprocess.DEVNULL,
                                      stderr=subprocess.PIPE)
            CHILDREN.append(self.p)
            self.stdout, self.stderr = self.p.stdout, self.p.stderr
            return
        import uuid
        base = os.path.join(ROOT, "out", "cam-run")
        for d in glob.glob(os.path.join(base, "[0-9a-f]" * 8)):  # runs older than an hour
            if time.time() - os.path.getmtime(d) > 3600:
                shutil.rmtree(d, ignore_errors=True)
        self.dir = os.path.join(ROOT, "out", "cam-run", uuid.uuid4().hex[:8])
        os.makedirs(self.dir)
        out = "/dev/null"
        if want_stdout:
            out = os.path.join(self.dir, "out.fifo")
            os.mkfifo(out)
        self.err, self.done, self.pidf = (os.path.join(self.dir, n) for n in ("err.log", "done", "pid"))
        open(self.err, "w").close()
        if want_stdout:  # ffmpeg writes the FIFO itself: it opens it only once it runs, so nothing waits on the reader
            cmd = [out if c == "pipe:1" else c for c in cmd]
        server_request({"cmd": cmd, "out": "/dev/null", "err": self.err, "done": self.done, "pid": self.pidf})
        self._fifo = out if want_stdout else None
        self._stdout = None
        self.stderr = self._tail()
        CHILDREN.append(self)

    @property
    def stdout(self):
        if self.via == "terminal" and self._stdout is None and self._fifo:
            self._stdout = open(self._fifo, "rb")  # blocks until ffmpeg opens it for writing
        return self._stdout

    @stdout.setter
    def stdout(self, v):
        self._stdout = v

    def _tail(self):
        with open(self.err, "rb") as f:
            while True:
                line = f.readline()
                if line:
                    yield line
                elif self.poll() is not None:
                    rest = f.read()
                    if rest:
                        yield from rest.splitlines(True)
                    return
                else:
                    time.sleep(0.02)

    def poll(self):
        if self.via == "direct":
            self.returncode = self.p.poll()
        elif self.returncode is None and os.path.exists(self.done):
            try:
                self.returncode = int(open(self.done).read().strip() or 1)
            except ValueError:
                pass
        return self.returncode

    def pid(self):
        if self.via == "direct":
            return self.p.pid
        for _ in range(100):
            if os.path.exists(self.pidf) and open(self.pidf).read().strip():
                return int(open(self.pidf).read())
            time.sleep(0.05)
        return None

    def wait(self, timeout):
        t0 = time.time()
        while self.poll() is None:
            if time.time() - t0 > timeout:
                raise subprocess.TimeoutExpired("ffmpeg", timeout)
            time.sleep(0.05)
        return self.returncode

    def stop(self, timeout=10):
        """Ask ffmpeg to finish (the mp4 gets its index), then kill it if it does not."""
        if self.poll() is not None:
            return
        pid = self.pid()
        if pid:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        try:
            self.wait(timeout)
        except subprocess.TimeoutExpired:
            self.kill()

    def kill(self):
        pid = self.pid() if self.poll() is None else None
        if pid:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def stderr_text(self):
        if self.via == "direct":
            return self.p.stderr.read().decode(errors="replace")
        return open(self.err, errors="replace").read()


SOCK = os.path.join(ROOT, "out", "cam-run", "serve.sock")
SERVE_IDLE = 30 * 60


def server_request(req):
    """Hand one ffmpeg run to the capture server in Terminal.app; start the server (one window) if it is not running."""
    import socket
    for attempt in range(2):
        try:
            c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            c.settimeout(5)
            c.connect(SOCK)
            c.sendall((json.dumps(req) + "\n").encode())
            reply = c.makefile().readline().strip()
            c.close()
            if reply != "ok":
                sys.exit(f"capture server: {reply or 'no reply'}")
            return
        except (FileNotFoundError, ConnectionRefusedError):
            if attempt:
                break
            say("  starting the capture server in one Terminal window (it keeps Terminal's camera permission; quits after "
                f"{SERVE_IDLE // 60} min idle) ...")
            script = os.path.join(ROOT, "out", "cam-run", "serve.command")
            os.makedirs(os.path.dirname(script), exist_ok=True)
            with open(script, "w") as f:
                f.write(f"#!/bin/zsh\n# PaperOS camera capture server (tools/cam.py serve). Close this window to stop it.\n"
                        f"cd '{ROOT}' && exec uv run --script tools/cam.py serve\n")
            os.chmod(script, 0o755)
            subprocess.run(["open", "-g", "-a", "Terminal", script], check=True, timeout=20)
            t0 = time.time()
            while not os.path.exists(SOCK) and time.time() - t0 < 30:
                time.sleep(0.2)
    sys.exit("capture server did not start (see its Terminal window)")


def cmd_serve(a):
    """Runs in Terminal.app: accepts ffmpeg runs over a Unix socket (owner only) and starts them as its children, so they
    use Terminal's camera permission. Only the ffmpeg binary is run, never a shell. Quits after 30 min without a request."""
    import socket
    signal.alarm(0)
    os.makedirs(os.path.dirname(SOCK), exist_ok=True)
    if os.path.exists(SOCK):
        try:
            t = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            t.connect(SOCK)
            t.close()
            sys.exit("a capture server is already running")
        except ConnectionRefusedError:
            os.remove(SOCK)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    old = os.umask(0o077)
    srv.bind(SOCK)
    os.umask(old)
    srv.listen(4)
    srv.settimeout(SERVE_IDLE)
    exe = ffmpeg()
    say(f"PaperOS camera capture server: {SOCK}. Quits after {SERVE_IDLE // 60} min idle; Ctrl-C stops it.")

    def run(req):
        try:
            with open(req["err"], "ab") as err:
                # Opening the FIFO blocks until the client reads it: hence this thread.
                with open(req["out"], "wb") as out:
                    p = subprocess.Popen(req["cmd"], stdin=subprocess.DEVNULL, stdout=out, stderr=err)
                    with open(req["pid"], "w") as f:
                        f.write(str(p.pid))
                    rc = p.wait()
        except Exception as e:  # noqa: BLE001
            rc = 1
            say(f"  failed: {e}")
        with open(req["done"], "w") as f:
            f.write(str(rc))
        say(f"{time.strftime('%H:%M:%S')}  done (exit {rc})")

    try:
        while True:
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                say("idle: quitting")
                break
            with conn:
                try:
                    req = json.loads(conn.makefile().readline())
                    cmd = req["cmd"]
                    if not cmd or cmd[0] != exe or any(not isinstance(x, str) for x in cmd):
                        raise ValueError("only ffmpeg runs are accepted")
                    base = os.path.join(ROOT, "out", "cam-run")
                    for k in ("err", "done", "pid"):
                        if not os.path.realpath(req[k]).startswith(base + os.sep):
                            raise ValueError(f"{k} outside out/cam-run")
                    if req["out"] != "/dev/null" and not os.path.realpath(req["out"]).startswith(base + os.sep):
                        raise ValueError("out outside out/cam-run")
                    say(f"{time.strftime('%H:%M:%S')}  ffmpeg run -> {os.path.basename(os.path.dirname(req['err']))}")
                    threading.Thread(target=run, args=(req,), daemon=True).start()
                    conn.sendall(b"ok\n")
                except Exception as e:  # noqa: BLE001
                    conn.sendall(f"refused: {e}\n".encode())
    finally:
        srv.close()
        if os.path.exists(SOCK):
            os.remove(SOCK)


def run_ffmpeg(args, timeout, what, via="direct", out=None, seconds=None):
    """Run ffmpeg to the end, or, with `seconds`, stop it that long after `out` first appears. Camera timestamps from
    avfoundation do not advance reliably (ffmpeg then never reaches a -t), so the duration is measured on the wall clock."""
    cmd = [ffmpeg(), "-hide_banner", "-loglevel", "error", "-y", *args]
    if out and os.path.exists(out):
        os.remove(out)
    p = FFmpeg(cmd, via)
    if via == "terminal":
        say(f"  (capturing through Terminal.app: macOS gives the camera to Terminal, not to this app)")
    try:
        if seconds is not None:
            t0 = time.time()
            while not (out and os.path.exists(out)) and p.poll() is None:
                if time.time() - t0 > 20:
                    raise subprocess.TimeoutExpired("ffmpeg", 20)
                time.sleep(0.05)
            say(f"  first frame after {time.time() - t0:.1f} s; recording {seconds:g} s ...")
            end = time.time() + seconds
            while time.time() < end and p.poll() is None:
                time.sleep(0.05)
            p.stop()
            p.returncode = 0 if p.returncode in (None, 255, -15, 143) else p.returncode  # stopped on purpose
        p.wait(timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        sys.exit(f"{what}: ffmpeg did not finish in {timeout} s. Camera unplugged or busy? If Terminal is not allowed to use the "
                 "camera: System Settings > Privacy & Security > Camera > Terminal on.")
    err = p.stderr_text().strip()
    if p.returncode != 0:
        sys.exit(f"{what} failed: {err.splitlines()[-1] if err else 'ffmpeg exit ' + str(p.returncode)}")


def cmd_shot(a):
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    inp, desc, via = input_args(a, 0)
    say(f"shot: {desc}, {a.warmup:.1f} s warm-up for exposure and focus ...")
    # Keep overwriting one image while the camera settles: the file holds the last frame.
    out = os.path.abspath(a.out)
    if a.source:
        run_ffmpeg([*inp, "-t", f"{a.warmup}", "-update", "1", out], a.warmup + 25, "shot", via)
    else:
        run_ffmpeg([*inp, "-fps_mode", "passthrough", "-update", "1", out], a.warmup + 30, "shot", via, out=out, seconds=a.warmup)
    img = cv2.imread(a.out)
    if img is None:
        sys.exit("shot: no image written")
    say(f"wrote {a.out} ({img.shape[1]}x{img.shape[0]})")


def cmd_video(a):
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    inp, desc, via = input_args(a, a.fps)
    say(f"video: {desc}, {a.seconds} s ...")
    t0 = time.time()
    out = os.path.abspath(a.out)
    enc = ["-c:v", "h264_videotoolbox", "-b:v", "12M", "-pix_fmt", "yuv420p", out]
    if a.source:
        run_ffmpeg([*inp, "-t", str(a.seconds), *enc], a.seconds + 30, "video", via)
    else:
        run_ffmpeg([*inp, "-fps_mode", "passthrough", *enc], a.seconds + 40, "video", via, out=out, seconds=a.seconds)
    n, fps = probe_video(a.out)
    say(f"wrote {a.out} ({n} frames, {fps:.1f} fps, {time.time() - t0:.1f} s)")


def probe_video(path):
    cap = cv2.VideoCapture(path)
    n, fps = int(cap.get(cv2.CAP_PROP_FRAME_COUNT)), cap.get(cv2.CAP_PROP_FPS)
    cap.release()
    return n, fps


def cmd_devices(a):
    cams = list_cameras()
    if not cams:
        say("no video devices")
    for i, n in cams:
        tag = " (screen capture: never used)" if is_screen(n) else " (built in)" if is_builtin(n) else ""
        say(f"[{i}] {n}{tag}")
        if not is_screen(n):
            modes = camera_modes(n)
            say("     modes: " + (", ".join(f"{w}x{h}@{f:g}" for w, h, f in modes[:12]) or "(none listed)"))
    have = shutil.which("uvc-util") or os.path.exists(os.path.expanduser("~/.local/bin/uvc-util"))
    say("exposure/focus/white-balance lock: " + ("uvc-util found: `cam.py lock`" if have else
        "uvc-util not installed: auto exposure stays on (numbers are normalised per image anyway)"))


# ---------------------------------------------------------------- camera controls (uvc-util)

LOCK = os.path.join(ROOT, "out", "cam-lock.json")


def uvc_util():
    exe = shutil.which("uvc-util") or os.path.expanduser("~/.local/bin/uvc-util")
    if not os.path.exists(exe):
        sys.exit("uvc-util not found: build it (DISPLAY.md \"Camera on the panel\") into ~/.local/bin")
    return exe


def uvc_index(name):
    r = subprocess.run([uvc_util(), "-d"], capture_output=True, text=True, timeout=15)
    for line in r.stdout.splitlines():
        m = re.match(r"\s*(\d+)\s+0x[0-9a-f]+:0x[0-9a-f]+\s+\S+\s+\S+\s+(.+?)\s*$", line)
        if m and m.group(2).lower() == name.lower():
            return m.group(1)
    sys.exit(f"uvc-util does not list '{name}' (not a UVC camera?)")


class Uvc:
    def __init__(self, name):
        self.idx = uvc_index(name)

    def get(self, ctl):
        r = subprocess.run([uvc_util(), "-I", self.idx, "-g", ctl], capture_output=True, text=True, timeout=15)
        m = re.search(r"=\s*(\S+)", r.stdout)
        if not m:
            raise RuntimeError(f"uvc-util -g {ctl}: {r.stdout.strip() or r.stderr.strip()}")
        v = m.group(1)
        return {"true": 1, "false": 0}.get(v, int(v) if v.lstrip("-").isdigit() else v)

    def set(self, **kv):
        args = [uvc_util(), "-I", self.idx]
        for k, v in kv.items():
            args += ["-s", f"{k.replace('_', '-')}={v}"]
        r = subprocess.run(args, capture_output=True, text=True, timeout=15)
        if r.returncode != 0:
            raise RuntimeError(f"uvc-util set {kv}: {r.stdout.strip()} {r.stderr.strip()}")


def sharpness(img, Hm):
    v = warp(img, Hm) if Hm is not None else img[img.shape[0] // 4: 3 * img.shape[0] // 4, img.shape[1] // 4: 3 * img.shape[1] // 4]
    return float(cv2.Laplacian(v, cv2.CV_32F).var())


def cmd_lock(a):
    """Exposure, white balance and focus to fixed values for this session (they change with the light and the camera's place,
    so lock again at the start of a session). Exposure and white balance: what the camera's automatics chose after a few
    seconds, the exposure capped at 33 ms (30 fps). Focus: a sweep over the range on whatever the panel shows (a page with
    text works best), sharpest = highest Laplacian variance inside the panel."""
    if a.source:
        sys.exit("lock needs a camera")
    name = pick_camera(a.camera)
    u = Uvc(name)
    say(f"lock: '{name}' (uvc-util index {u.idx}); automatics on for a few seconds ...")
    u.set(auto_exposure_mode=8, auto_white_balance_temp="true", auto_focus="true")
    rec = Recorder(a, os.path.join(ROOT, "out", "cam-lock.mp4"), width=1920, fps=0)
    rec.wait_first(20)
    time.sleep(3.0)
    expo, wb, gain = u.get("exposure-time-abs"), u.get("white-balance-temp"), u.get("gain")
    expo = min(int(a.exposure or expo), 330)
    wb = int(a.wb or wb)
    u.set(auto_exposure_mode=1, exposure_time_abs=expo, auto_white_balance_temp="false", white_balance_temp=wb, auto_focus="false")
    say(f"  exposure {expo / 10:.1f} ms (manual), white balance {wb} K, gain {gain}")
    frame = load_frame(a.frame) if os.path.exists(a.frame) else None
    Hm, how = find_panel(rec.frames[-1], frame, a.max_age * 60, quiet=True)
    say(f"  sharpness measured {'inside the panel (' + how + ')' if Hm is not None else 'in the middle of the image (panel not found)'}")

    def sweep(values):
        marks = []
        for v in values:
            u.set(focus_abs=v)
            marks.append((time.time(), v))
            time.sleep(0.9)  # the lens moves, then two or three frames at 10 fps
        marks.append((time.time(), None))
        n = len(rec.frames)
        res = []
        for (t0, v), (t1, _) in zip(marks, marks[1:]):
            idx = [i for i in range(n) if t0 + 0.45 <= rec.arrival[i] < t1]
            if idx:
                res.append((max(sharpness(rec.frames[i], Hm) for i in idx), v))
        return res

    if a.focus == "auto":
        coarse = sweep(range(0, 251, 25))
        say("  focus sweep: " + "  ".join(f"{v}:{sh:.0f}" for sh, v in coarse))
        best = max(coarse)[1]
        fine = sweep(range(max(0, best - 20), min(250, best + 20) + 1, 5))
        say("  fine:        " + "  ".join(f"{v}:{sh:.0f}" for sh, v in fine))
        focus = max(fine)[1]
    else:
        focus = int(a.focus)
    u.set(focus_abs=focus)
    time.sleep(0.8)
    rec.stop()
    settings = {"when": time.strftime("%Y-%m-%d %H:%M:%S"), "camera": name, "exposure_time_abs": expo, "white_balance_temp": wb,
                "gain": gain, "focus_abs": focus}
    with open(LOCK, "w") as f:
        json.dump(settings, f, indent=1)
    say(f"locked: exposure {expo / 10:.1f} ms, white balance {wb} K, focus {focus}; written to {LOCK}. `cam.py unlock` gives the automatics back.")


def cmd_unlock(a):
    name = pick_camera(a.camera)
    Uvc(name).set(auto_exposure_mode=8, auto_white_balance_temp="true", auto_focus="true")
    say(f"'{name}': automatic exposure, white balance and focus again")


# ---------------------------------------------------------------- registration

def gray(img):
    return img if img.ndim == 2 else cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)


def load_frame(path):
    f = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if f is None:
        sys.exit(f"{path}: cannot read (make device-shot writes out/device.png)")
    if f.shape != (H, W):
        sys.exit(f"{path}: {f.shape[1]}x{f.shape[0]}, expected {W}x{H}")
    return f


def register_sift(cam_gray, frame):
    """Homography panel(540x960) -> camera from SIFT matches; (H, inliers) or (None, why)."""
    sift = cv2.SIFT_create(nfeatures=6000)
    # The frame dump is crisp and high contrast; the camera image is soft: blur the dump a little to match.
    k1, d1 = sift.detectAndCompute(cv2.GaussianBlur(frame, (0, 0), 0.8), None)
    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
    k2, d2 = sift.detectAndCompute(clahe.apply(cam_gray), None)
    if d1 is None or d2 is None or len(k1) < 20 or len(k2) < 20:
        return None, f"too few features (frame {len(k1)}, camera {len(k2)}): the page needs text or shapes"
    good = []
    for m in cv2.BFMatcher(cv2.NORM_L2).knnMatch(d1, d2, k=2):
        if len(m) == 2 and m[0].distance < 0.75 * m[1].distance:
            good.append(m[0])
    if len(good) < 20:
        return None, f"only {len(good)} matches"
    src = np.float32([k1[m.queryIdx].pt for m in good])
    dst = np.float32([k2[m.trainIdx].pt for m in good])
    Hm, mask = cv2.findHomography(src, dst, cv2.RANSAC, 3.0)
    inl = int(mask.sum()) if mask is not None else 0
    if Hm is None or inl < 20:
        return None, f"no consistent homography ({inl} inliers of {len(good)})"
    q = panel_quad(Hm)
    if not cv2.isContourConvex(q.astype(np.float32)) or cv2.contourArea(q.astype(np.float32)) < 0.01 * cam_gray.size:
        return None, "homography gives a degenerate panel outline"
    return Hm, inl


def panel_quad(Hm):
    pts = np.float32([[0, 0], [W, 0], [W, H], [0, H]]).reshape(-1, 1, 2)
    return cv2.perspectiveTransform(pts, Hm).reshape(4, 2)


def detect_aruco(cam_gray):
    """{id: centre (x, y)} of ArUco 4x4_50 markers 0-3 seen in the camera image."""
    try:
        det = cv2.aruco.ArucoDetector(cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50), cv2.aruco.DetectorParameters())
        corners, ids, _ = det.detectMarkers(cam_gray)
    except AttributeError:
        return {}
    out = {}
    if ids is not None:
        for c, i in zip(corners, ids.flatten()):
            if 0 <= i <= 3:
                out[int(i)] = c.reshape(4, 2).mean(axis=0)
    return out


def save_corners(Hm, cam_gray, source):
    quad = panel_quad(Hm)
    markers = {}
    Hinv = np.linalg.inv(Hm)
    for i, c in detect_aruco(cam_gray).items():  # remember where the markers sit in panel coordinates
        p = cv2.perspectiveTransform(np.float32([[c]]), Hinv)[0, 0]
        markers[str(i)] = [float(p[0]), float(p[1])]
    data = {"time": time.time(), "when": time.strftime("%Y-%m-%d %H:%M:%S"), "source": source,
            "camera_size": [cam_gray.shape[1], cam_gray.shape[0]], "quad": quad.tolist(), "markers_panel": markers}
    os.makedirs(os.path.dirname(CORNERS), exist_ok=True)
    with open(CORNERS, "w") as f:
        json.dump(data, f, indent=1)
    return data


def find_panel(cam_gray, frame, max_age, quiet=False):
    """Homography panel -> camera for this image. Order: SIFT against the frame dump (saved for later images of the
    session), ArUco markers learnt by that calibration, the saved outline if it is younger than max_age seconds."""
    log = (lambda m: None) if quiet else say
    if frame is not None:
        Hm, info = register_sift(cam_gray, frame)
        if Hm is not None:
            log(f"  panel found by matching the frame dump ({info} inliers)")
            save_corners(Hm, cam_gray, "sift")
            return Hm, "sift"
        log(f"  frame-dump match failed: {info}")
    saved = None
    if os.path.exists(CORNERS):
        with open(CORNERS) as f:
            saved = json.load(f)
        age = time.time() - saved["time"]
        if age > max_age:
            log(f"  saved outline is {age / 60:.0f} min old (limit {max_age / 60:.0f} min): the camera may have moved, not used")
            saved = None
        elif tuple(saved["camera_size"]) != (cam_gray.shape[1], cam_gray.shape[0]):
            log("  saved outline is for another image size, not used")
            saved = None
    if saved and len(saved.get("markers_panel", {})) == 4:
        seen = detect_aruco(cam_gray)
        if len(seen) == 4:
            src = np.float32([saved["markers_panel"][str(i)] for i in range(4)])
            dst = np.float32([seen[i] for i in range(4)])
            log("  panel found by the four ArUco markers")
            return cv2.getPerspectiveTransform(src, dst), "aruco"
    if saved:
        src = np.float32([[0, 0], [W, 0], [W, H], [0, H]])
        log(f"  using the outline saved at {saved['when']} ({saved['source']})")
        return cv2.getPerspectiveTransform(src, np.float32(saved["quad"])), "saved"
    return None, "none"


def warp(cam_gray, Hm, w=W, h=H):
    S = np.diag([W / w, H / h, 1.0])  # output pixel -> panel pixel
    return cv2.warpPerspective(cam_gray, Hm @ S, (w, h), flags=cv2.INTER_AREA | cv2.WARP_INVERSE_MAP)


def cmd_calibrate(a):
    cam = cv2.imread(a.image)
    if cam is None:
        sys.exit(f"{a.image}: cannot read (make cam-shot)")
    frame = load_frame(a.frame)
    say(f"calibrate: {a.image} against {a.frame}")
    Hm, info = register_sift(gray(cam), frame)
    if Hm is None:
        sys.exit(f"calibration failed: {info}. Show a page with text (a book page, Settings) and take both again.")
    d = save_corners(Hm, gray(cam), "sift")
    say(f"wrote {CORNERS}: panel outline {np.round(d['quad']).astype(int).tolist()}, {info} inliers, "
        f"{len(d['markers_panel'])} ArUco marker(s)")
    out = cam.copy()
    cv2.polylines(out, [np.int32(d["quad"])], True, (0, 0, 255), 3)
    path = os.path.join(ROOT, "out", "cam-calibrated.png")
    cv2.imwrite(path, out)
    say(f"wrote {path} (outline drawn in red: look at it)")


# ---------------------------------------------------------------- compare: residual = ghosting

def compare_images(cam_gray, frame, Hm):
    """Normalised camera view of the panel and the residual against the frame dump.

    Lighting: divide by the local white (smoothed over pixels the frame says are white). Tone: a robust linear fit of the
    normalised camera value against the (blurred) frame level. Residual: (camera - fit) / contrast, so 0.05 means 5 % of
    the white-to-black difference. Ghost: how much darker than expected the camera sees pixels the frame says are white,
    away from any ink (6 px), which is where the previous page shows through."""
    view = warp(cam_gray, Hm).astype(np.float32)
    E = frame.astype(np.float32) / 255.0  # 1 = white
    white = (frame >= 250).astype(np.uint8)
    far = cv2.erode(white, np.ones((7, 7), np.uint8))  # >= 3 px from ink: the previous page's text sits in the same lines
    far[:8, :] = far[-8:, :] = 0  # the panel's edge: a pixel of misregistration shows the bezel
    far[:, :8] = far[:, -8:] = 0
    # Local white field, from cells of 30x30 px.
    cw = 60  # light changes slowly; big cells keep a dense ghost from pulling the white down
    gh, gw = H // cw, W // cw
    field = np.full((gh, gw), np.nan, np.float32)
    for gy in range(gh):
        for gx in range(gw):
            sl = (slice(gy * cw, (gy + 1) * cw), slice(gx * cw, (gx + 1) * cw))
            m = far[sl] > 0
            if m.sum() > 80:
                field[gy, gx] = np.percentile(view[sl][m], 85)
    known = ~np.isnan(field)
    if known.sum() < 6:
        raise ValueError("the frame has too little white to measure the light")
    # Light varies smoothly: a robust quadratic surface through the cells (no blur at the edges, no holes).
    ky, kx = np.nonzero(known)
    cx, cy = (kx + 0.5) * cw / W, (ky + 0.5) * cw / H
    vals = field[ky, kx]
    basis = lambda x, y: np.stack([np.ones_like(x), x, y, x * x, x * y, y * y], -1)
    use = np.ones(vals.size, bool)
    for _ in range(3):
        coef = np.linalg.lstsq(basis(cx[use], cy[use]), vals[use], rcond=None)[0]
        r = vals - basis(cx, cy) @ coef
        s_ = 1.4826 * np.median(np.abs(r[use])) + 1e-6
        use = np.abs(r) < 3 * s_
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    field = (basis((xx + 0.5) / W, (yy + 0.5) / H) @ coef).astype(np.float32)
    N = view / np.maximum(field, 1.0)
    Eb = cv2.GaussianBlur(E, (0, 0), 1.2)
    Nb = cv2.GaussianBlur(N, (0, 0), 1.2)
    # Robust fit Nb = a + b * Eb.
    sel = np.ones_like(Eb, bool)
    a = b = 0.0
    for _ in range(4):
        A = np.stack([np.ones(sel.sum()), Eb[sel]], 1)
        a, b = np.linalg.lstsq(A, Nb[sel], rcond=None)[0]
        r = Nb - (a + b * Eb)
        s = 1.4826 * np.median(np.abs(r[sel])) + 1e-6
        sel = np.abs(r) < 3 * s
    if b < 0.05:
        raise ValueError(f"no contrast between ink and white (fit slope {b:.3f}): wrong page, or the panel is not in view")
    R = (Nb - (a + b * Eb)) / b
    ghost = np.where(far > 0, np.clip(-R, 0, None), np.nan)
    lightness = np.clip((N - a) / b, -0.2, 1.2)  # 0 = black ink, 1 = white, in this image's own scale
    return {"view": lightness, "R": R, "ghost": ghost, "far": far, "a": float(a), "b": float(b)}


def region_table(ghost, cols=3, rows=4):
    cells = []
    for r in range(rows):
        row = []
        for c in range(cols):
            g = ghost[r * H // rows:(r + 1) * H // rows, c * W // cols:(c + 1) * W // cols]
            v = g[~np.isnan(g)]
            row.append((float(v.mean() * 100), float(np.percentile(v, 95) * 100), int(v.size)) if v.size > 200 else None)
        cells.append(row)
    return cells


def gray_levels(view, frame):
    """Mean measured lightness per frame gray level (pixels inside large flat areas only), e.g. on the Display lab ramp."""
    out = {}
    for lvl in range(16):
        v8 = lvl * 17
        m = cv2.erode((frame == v8).astype(np.uint8), np.ones((9, 9), np.uint8)) > 0
        if m.sum() > 300:
            out[lvl] = float(np.median(view[m]))
    return out


def draw_diff(res, frame, cells, path, title):
    view = np.clip(res["view"] * 255, 0, 255).astype(np.uint8)
    heat = np.nan_to_num(res["ghost"], nan=0.0)
    hm = cv2.applyColorMap(np.clip(heat * 255 * 5, 0, 255).astype(np.uint8), cv2.COLORMAP_INFERNO)  # 20 % of contrast = full scale
    hm[res["far"] == 0] = (90, 90, 90)
    rows, cols = len(cells), len(cells[0])
    for r in range(rows):
        for c in range(cols):
            x0, y0 = c * W // cols, r * H // rows
            cv2.rectangle(hm, (x0, y0), ((c + 1) * W // cols - 1, (r + 1) * H // rows - 1), (255, 255, 255), 1)
            txt = "-" if cells[r][c] is None else f"{cells[r][c][0]:.1f}"
            cv2.putText(hm, txt, (x0 + 8, y0 + 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 0), 4)
            cv2.putText(hm, txt, (x0 + 8, y0 + 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    panels = [cv2.cvtColor(view, cv2.COLOR_GRAY2BGR), cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR), hm]
    gap = np.full((H, 8, 3), 255, np.uint8)
    img = np.hstack([panels[0], gap, panels[1], gap, panels[2]])
    bar = np.full((40, img.shape[1], 3), 255, np.uint8)
    cv2.putText(bar, title, (8, 27), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 2)
    labels = np.full((30, img.shape[1], 3), 255, np.uint8)
    for i, lab in enumerate(["camera (normalised, warped)", "frame dump (firmware)", "ghost: % of white-black contrast"]):
        cv2.putText(labels, lab, (8 + i * (W + 8), 21), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 0), 1)
    cv2.imwrite(path, np.vstack([bar, labels, img]))


def report_compare(res, frame, out_path, label=""):
    cells = region_table(res["ghost"])
    g = res["ghost"][~np.isnan(res["ghost"])]
    ghost = float(g.mean() * 100)
    p95 = float(np.percentile(g, 95) * 100)
    say(f"ghost {ghost:.2f} % (mean over {g.size} white pixels away from ink; p95 {p95:.1f} %; "
        f"fit: black {res['a']:.2f}, contrast {res['b']:.2f} of local white)")
    say("per region (mean % / p95 %), 3 columns x 4 rows, top first:")
    for row in cells:
        say("  " + "  ".join("     -      " if c is None else f"{c[0]:5.2f}/{c[1]:5.1f}" for c in row))
    lv = gray_levels(res["view"], frame)
    if len(lv) >= 3:
        say("measured lightness per gray level (0 = ink, 1 = white): " + ", ".join(f"g{k}={v:.2f}" for k, v in sorted(lv.items())))
    draw_diff(res, frame, cells, out_path, f"{label}ghost {ghost:.2f} %  (p95 {p95:.1f} %)")
    say(f"wrote {out_path}")
    return ghost


def cmd_compare(a):
    cam = cv2.imread(a.image)
    if cam is None:
        sys.exit(f"{a.image}: cannot read (make cam-shot)")
    frame = load_frame(a.frame)
    say(f"compare: {a.image} against {a.frame}")
    Hm, how = find_panel(gray(cam), frame, a.max_age * 60)
    if Hm is None:
        sys.exit("panel not found in the camera image: show a page with text, then cam-shot + device-shot again")
    try:
        res = compare_images(gray(cam), frame, Hm)
        if a.ref:
            ref = cv2.imread(a.ref)
            if ref is None:
                sys.exit(f"{a.ref}: cannot read")
            Hr, _ = find_panel(gray(ref), frame, a.max_age * 60, quiet=True)
            if Hr is None:
                sys.exit(f"panel not found in {a.ref}")
            say(f"  reference: {a.ref} (same page after a Clean): the light's own unevenness cancels out")
            res = against_reference(res, compare_images(gray(ref), frame, Hr))
    except ValueError as e:
        sys.exit(f"compare: {e}")
    report_compare(res, frame, a.diff, "against the reference: " if a.ref else "")


def against_reference(res, ref):
    """Ghost relative to a camera image of the same page in the same session after a Quality clean: what remains is
    what the image has and the clean panel has not (the uneven light and the panel's own mottle cancel out)."""
    out = dict(res)
    R = res["R"] - ref["R"]
    out["R"] = R
    far = res["far"] & ref["far"]
    out["far"] = far
    out["ghost"] = np.where(far > 0, np.clip(-R, 0, None), np.nan)
    return out


# ---------------------------------------------------------------- around: input + video + refresh log

def load_device_module():
    spec = importlib.util.spec_from_file_location("paperos_device", os.path.join(ROOT, "tools", "device.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def port_in_use(port):
    r = subprocess.run(["lsof", "-t", port], capture_output=True, text=True, timeout=10)
    return [p for p in r.stdout.split() if p.strip() and int(p) != os.getpid()]


class Recorder:
    """ffmpeg: the camera to an mp4 file, and the same frames small and gray to a pipe, each with its capture time."""

    def __init__(self, a, out_mp4, width=960, fps=None):
        inp, self.desc, via = input_args(a, a.fps if fps is None else fps)
        self.width = width
        # showinfo prints pts_time per frame on stderr. Camera: wallclock timestamps (-copyts), the host's clock.
        cmd = [ffmpeg(), "-hide_banner", "-loglevel", "info", "-y", *inp, "-copyts",
               "-fps_mode", "passthrough", "-map", "0:v", "-c:v", "h264_videotoolbox", "-b:v", "12M", "-pix_fmt", "yuv420p", out_mp4,
               "-map", "0:v", "-fps_mode", "passthrough", "-vf", f"scale={width}:-2,format=gray,showinfo", "-f", "rawvideo", "-pix_fmt", "gray", "pipe:1"]
        self.p = FFmpeg(cmd, via, want_stdout=True)
        self.size = None
        self.frames, self.arrival, self.pts = [], [], []
        self.err_tail = []
        self.started = threading.Event()
        self.t_err = threading.Thread(target=self._stderr, daemon=True)
        self.t_err.start()
        self.t_out = threading.Thread(target=self._stdout, daemon=True)
        self.t_out.start()

    def _stderr(self):
        for raw in self.p.stderr:
            line = raw.decode(errors="replace").rstrip()
            self.err_tail = (self.err_tail + [line])[-8:]
            if self.size is None:
                m = re.search(r"Stream #1:0.*gray.*?(\d{2,5})x(\d{2,5})", line)
                if m:
                    self.size = (int(m.group(1)), int(m.group(2)))
                    self.started.set()
            m = re.search(r"pts_time:\s*([\d.]+)", line)
            if m and "showinfo" in line:
                self.pts.append(float(m.group(1)))

    def _stdout(self):
        out = self.p.stdout  # Terminal mode: opens the FIFO now, so ffmpeg can open its end and print the stream sizes
        self.started.wait()
        w, h = self.size
        n = w * h
        while True:
            buf = out.read(n)
            if not buf or len(buf) < n:
                break
            self.arrival.append(time.time())
            self.frames.append(np.frombuffer(buf, np.uint8).reshape(h, w))

    def wait_first(self, timeout=15):
        t0 = time.time()
        while not self.frames and time.time() - t0 < timeout and self.p.poll() is None:
            time.sleep(0.05)
        if not self.frames:
            self.stop()
            sys.exit("camera gave no frames: " + " | ".join(self.err_tail[-3:]))

    def stop(self):
        self.p.stop()
        self.t_out.join(timeout=5)

    def times(self, file_source):
        """Capture time of each frame on the host clock. Camera: ffmpeg's wallclock pts; file: arrival time."""
        n = len(self.frames)
        if not file_source and len(self.pts) >= n and n:
            pts = np.array(self.pts[:n])
            if abs(pts[-1] - self.arrival[n - 1]) < 5:  # wallclock seconds: usable
                return pts, "camera timestamps"
        return np.array(self.arrival[:n]), "pipe arrival (includes a frame or two of latency)"


def warped_stack(frames, Hm_small, scale_w=270):
    sh = int(round(scale_w * H / W))
    if Hm_small is None:
        return [cv2.resize(f, (scale_w, sh), interpolation=cv2.INTER_AREA).astype(np.float32) for f in frames]
    return [warp(f, Hm_small, scale_w, sh).astype(np.float32) for f in frames]


def rect_slices(rect, scale_w=270):
    k = scale_w / W
    x, y, w, h = rect
    x0, y0 = max(0, int(x * k)), max(0, int(y * k))
    return slice(y0, max(y0 + 1, int((y + h) * k))), slice(x0, max(x0 + 1, int((x + w) * k)))


def activity(views, rects):
    """Per frame: mean |change| against the previous frame, for the whole panel and for each rect (for the plot)."""
    out = {"all": [0.0]}
    for i in range(len(rects)):
        out[i] = [0.0]
    for prev, v in zip(views, views[1:]):
        d = np.abs(v - prev)
        out["all"].append(float(d.mean()))
        for i, r in enumerate(rects):
            out[i].append(float(d[rect_slices(r)].mean()))
    return {key: np.array(v) for key, v in out.items()}


def visible_span(views, times, sl, t_start, t_end, frac=0.05):
    """When the region visibly changes between t_start and t_end: (first, last) capture time, or None.

    Against the frames before t_start (median) for the start, against the frames at t_end (median) for the end, so a
    slow waveform (a gradual settle) and a flat black phase inside a flash count as one change."""
    pre = [i for i in range(len(times)) if times[i] < t_start]
    post = [i for i in range(len(times)) if t_end - 0.1 <= times[i] <= t_end]
    win = [i for i in range(len(times)) if t_start <= times[i] <= t_end]
    if len(pre) < 2 or not post or not win:
        return None
    base = np.median(np.stack([views[i][sl] for i in pre[-10:]]), axis=0)
    final = np.median(np.stack([views[i][sl] for i in post[-6:]]), axis=0)
    d0 = np.array([np.abs(views[i][sl] - base).mean() for i in range(len(times))])
    d1 = np.array([np.abs(views[i][sl] - final).mean() for i in range(len(times))])
    noise0 = np.median(d0[pre[-10:]]) + 3 * np.std(d0[pre[-10:]]) + 0.5
    span = float(np.abs(final - base).mean())
    on = [i for i in win if d0[i] > max(noise0, frac * span)]
    if not on:
        return None
    noise1 = np.median(d1[post[-6:]]) + 3 * np.std(d1[post[-6:]]) + 0.5
    off = [i for i in win if d1[i] > max(noise1, frac * span)]
    dt = float(np.median(np.diff(times))) if len(times) > 1 else 0.0
    first = times[on[0]] - dt / 2  # the change began within the frame interval before it was seen: take the middle
    last = max(times[off[-1]] + dt / 2, first) if off else first
    return first, last


def analyse_timing(views, times, pushes, t_input):
    """pushes: [(host_start, mode, rect(x,y,w,h), call_ms)] -> lines of the report and the activity curves.
    A push's window ends where the next push that overlaps it starts (or at the end of the video)."""
    act = activity(views, [p[2] for p in pushes])
    lines = []
    # Where the firmware pushed (the union of the push rects), else the whole panel.
    if pushes:
        x0 = min(p[2][0] for p in pushes); y0 = min(p[2][1] for p in pushes)
        x1 = max(p[2][0] + p[2][2] for p in pushes); y1 = max(p[2][1] + p[2][3] for p in pushes)
        where, area = rect_slices((x0, y0, x1 - x0, y1 - y0)), "pushed area"
    else:
        where, area = (slice(None), slice(None)), "panel"
    b = visible_span(views, times, where, t_input, times[-1])
    if b:
        lines.append(f"{area}: first visible change {1000 * (b[0] - t_input):.0f} ms after the input, "
                     f"last change ends {1000 * (b[1] - t_input):.0f} ms after it")
    else:
        lines.append(f"{area}: no visible change after the input")
    for i, (ts, mode, rect, call) in enumerate(pushes):
        t_end = times[-1]
        for tn, _, rn, _ in pushes[i + 1:]:
            if tn > ts and not (rn[0] >= rect[0] + rect[2] or rect[0] >= rn[0] + rn[2] or rn[1] >= rect[1] + rect[3] or rect[1] >= rn[1] + rn[3]):
                # A waveform runs on after the next push starts: at least the measured full-screen times (STATUS.md).
                t_end = min(t_end, max(tn, ts + (1.6 if mode == "Q" else 0.6)))
                break
        bb = visible_span(views, times, rect_slices(rect), ts - 0.03, t_end)
        head = f"push {mode} rect={','.join(map(str, rect))} at +{1000 * (ts - t_input):.0f} ms (firmware call {call} ms)"
        if bb:
            lines.append(f"{head}: visible from +{1000 * (bb[0] - ts):.0f} to +{1000 * (bb[1] - ts):.0f} ms after the push "
                         f"({1000 * (bb[1] - bb[0]):.0f} ms on the panel)")
        else:
            lines.append(f"{head}: no visible change in its rect")
    return lines, act


def draw_timing(times, act, pushes, t_input, path, title):
    Wd, Hd, m = 1200, 420, 50
    img = np.full((Hd, Wd, 3), 255, np.uint8)
    t0, t1 = times[0], times[-1]
    sx = lambda t: int(m + (t - t0) / max(t1 - t0, 1e-3) * (Wd - 2 * m))
    top = max(float(act["all"].max()), 1.0)
    for i in range(len(pushes)):
        top = max(top, float(act[i].max()))
    sy = lambda v: int(Hd - m - v / top * (Hd - 2 * m))
    cv2.line(img, (m, Hd - m), (Wd - m, Hd - m), (0, 0, 0), 1)
    for s in np.arange(np.ceil(t0 * 10) / 10, t1, 0.5):
        cv2.line(img, (sx(s), Hd - m), (sx(s), Hd - m + 5), (0, 0, 0), 1)
        cv2.putText(img, f"{1000 * (s - t_input):.0f}", (sx(s) - 18, Hd - m + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 0), 1)
    cv2.line(img, (sx(t_input), m), (sx(t_input), Hd - m), (0, 160, 0), 2)
    cv2.putText(img, "input", (sx(t_input) + 3, m + 12), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 160, 0), 1)
    colours = [(200, 60, 0), (0, 120, 220), (160, 0, 160), (0, 150, 150)]
    for i, (ts, mode, rect, call) in enumerate(pushes):
        c = colours[i % len(colours)]
        cv2.line(img, (sx(ts), m), (sx(ts), Hd - m), c, 1)
        cv2.putText(img, f"{mode}", (sx(ts) + 3, m + 28 + 14 * (i % 4)), cv2.FONT_HERSHEY_SIMPLEX, 0.45, c, 1)
        pts = np.int32([[sx(t), sy(v)] for t, v in zip(times, act[i])])
        cv2.polylines(img, [pts], False, c, 1)
    pts = np.int32([[sx(t), sy(v)] for t, v in zip(times, act["all"])])
    cv2.polylines(img, [pts], False, (0, 0, 0), 2)
    cv2.putText(img, title, (m, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 1)
    cv2.putText(img, "ms after input; black: whole panel change per frame, colour: per push rect", (m, Hd - 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (80, 80, 80), 1)
    cv2.imwrite(path, img)


def contact_sheet(frames, views, times, Hm_small, t_input, path, n=12):
    act = activity(views, [])["all"]
    order = [i for i in range(len(frames)) if times[i] >= t_input - 0.1]
    if not order:
        return
    # Frames with the most change, plus the first after the input and the last, in time order.
    pick = sorted(set([order[0], order[-1]] + sorted(order, key=lambda i: -act[i])[:n - 2]))
    tiles = []
    for i in pick:
        v = warp(frames[i], Hm_small, 180, 320) if Hm_small is not None else cv2.resize(frames[i], (180, int(180 * frames[i].shape[0] / frames[i].shape[1])))
        v = cv2.cvtColor(v, cv2.COLOR_GRAY2BGR)
        cv2.putText(v, f"+{1000 * (times[i] - t_input):.0f} ms", (4, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 1)
        tiles.append(v)
    h = max(t.shape[0] for t in tiles)
    tiles = [cv2.copyMakeBorder(t, 0, h - t.shape[0], 0, 4, cv2.BORDER_CONSTANT, value=(255, 255, 255)) for t in tiles]
    rows = [np.hstack(tiles[i:i + 6] + [np.full_like(tiles[0], 255)] * (6 - len(tiles[i:i + 6]))) for i in range(0, len(tiles), 6)]
    cv2.imwrite(path, np.vstack(rows))


PUSH_RE = re.compile(r"\[disp\] push ([QTF]) rect=(-?\d+),(-?\d+),(\d+),(\d+) .*call=(\d+)ms")


def parse_refresh(payload):
    out = []
    for l in (payload or b"").decode(errors="replace").splitlines():
        f = dict(p.split("=", 1) for p in l.split() if "=" in p)
        if "t" in f and "mode" in f:
            out.append((int(f["t"]), f["mode"], tuple(int(v) for v in f.get("rect", "0,0,0,0").split(",")), int(f.get("call", 0))))
    return out


def cmd_around(a):
    want = {"tap": 2, "long": 2, "swipe": 4}[a.gesture]
    if len(a.coords) != want:
        sys.exit(f"usage: around {a.gesture} " + ("X1 Y1 X2 Y2" if want == 4 else "X Y"))
    dev = load_device_module()
    port = a.port or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
    if not port:
        sys.exit("no device: /dev/cu.usbmodem* not found")
    busy = port_in_use(port)
    if busy:
        sys.exit(f"{port} is open by process {', '.join(busy)} (another tool or `make monitor`?): not touching it")
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    before_png, after_png = os.path.join(ROOT, "out", "cam-before.png"), os.path.join(ROOT, "out", "cam-after.png")
    link = dev.Link(port)
    rec = None
    try:
        say(f"around: {port}; frame dump before ...")
        dump(dev, link, before_png)
        last_t = max([e[0] for e in parse_refresh(dev.request(link, ">refresh")[1])] or [0])
        mp4 = os.path.join(ROOT, "out", "cam-around.mp4")
        rec = Recorder(a, mp4)
        say(f"recording {rec.desc} ...")
        rec.wait_first()
        time.sleep(a.before)
        # Inject, then read the serial port line by line: each '[disp] push' line gets the host time it arrived.
        link.ser.timeout = 0.05
        cmd = f">{a.gesture} " + " ".join(map(str, a.coords))
        link.send(cmd)
        t_input = time.time()
        seen, ok = [], None
        end = t_input + a.after
        while time.time() < end:
            line = link.readline()
            if not line:
                continue
            now = time.time()
            if VERBOSE:
                say(f"| {1000 * (now - t_input):6.0f} ms  {line}")
            if line.startswith("#ok") and ok is None:
                ok = now
            m = PUSH_RE.search(line)
            if m:
                seen.append((now, m.group(1), tuple(int(m.group(i)) for i in range(2, 6)), int(m.group(6))))
        link.ser.timeout = 5
        rec.stop()
        say(f"input '{cmd}' answered in {1000 * ((ok or t_input) - t_input):.0f} ms; {len(rec.frames)} frames recorded, "
            f"{len(seen)} push line(s) on the serial log")
        entries = [e for e in parse_refresh(dev.request(link, ">refresh")[1]) if e[0] > last_t]
        dump(dev, link, after_png)
    finally:
        if rec:
            rec.stop()
        link.close()
    if not rec.frames:
        sys.exit("no frames")
    times, how_t = rec.times(bool(a.source))
    # Device clock -> host clock: a push's log line arrives when the push call returns (t + call).
    pushes = []
    offs = [s[0] - (e[0] + e[3]) / 1000.0 for s, e in zip(seen, entries) if s[1] == e[1] and s[2] == e[2]]
    if entries and offs:
        off = float(np.median(offs))
        pushes = [((e[0]) / 1000.0 + off, e[1], e[2], e[3]) for e in entries]
    elif seen:
        pushes = [(s[0] - s[3] / 1000.0, s[1], s[2], s[3]) for s in seen]
    fps_real = (len(times) - 1) / max(times[-1] - times[0], 1e-3)
    say(f"frame rate on the wire: {fps_real:.1f} fps (asked {a.fps}; uncompressed frames over USB 2 limit the size: see USB_BYTES_PER_S)")
    rec.desc += f", {fps_real:.0f} fps real"
    say(f"frame times: {how_t}; {len(entries)} refresh-log entr{'y' if len(entries) == 1 else 'ies'} after the input")
    # Register the panel on a quiet frame before the input, against the frame dump taken just before.
    frame_b = load_frame(before_png)
    i_ref = max([i for i in range(len(times)) if times[i] < t_input] or [0])
    Hm_small, how = find_panel(rec.frames[i_ref], frame_b, a.max_age * 60, quiet=True)
    if Hm_small is None:
        # Full camera resolution from the mp4: the small frames may be too soft for the match.
        cap = cv2.VideoCapture(mp4)
        cap.set(cv2.CAP_PROP_POS_FRAMES, i_ref)
        okf, full = cap.read()
        cap.release()
        if okf:
            Hf, how = find_panel(gray(full), frame_b, a.max_age * 60, quiet=True)
            if Hf is not None:
                k = rec.size[0] / full.shape[1]
                Hm_small = np.diag([k, k, 1.0]) @ Hf
    if Hm_small is None:
        say("  panel not found (frame-dump match failed, no fresh saved outline): whole-image activity only, no per-rect numbers")
    else:
        say(f"  panel registered ({how})")
    views = warped_stack(rec.frames, Hm_small)
    lines, act = analyse_timing(views, times, pushes, t_input)
    for l in lines:
        say("  " + l)
    tim = os.path.join(ROOT, "out", "cam-timing.png")
    draw_timing(times, act, pushes, t_input, tim, f"{cmd} ({rec.desc})")
    sheet = os.path.join(ROOT, "out", "cam-frames.png")
    contact_sheet(rec.frames, views, times, Hm_small, t_input, sheet)
    say(f"wrote {tim}, {sheet}, out/cam-around.mp4, {before_png}, {after_png}")
    # Ghost after the change: the last (settled) frame against the frame dump taken after it.
    if Hm_small is not None:
        frame_a = load_frame(after_png)
        last = rec.frames[-1]
        try:
            res = compare_images(last, frame_a, Hm_small)
            report_compare(res, frame_a, os.path.join(ROOT, "out", "cam-diff.png"), "after the input, last video frame: ")
            if np.mean(frame_a[56:] != frame_b[56:]) < 0.02:  # the page stayed (a counter or the clock may change)
                # Same page before and after (a Clean, a redraw): the ghost the input removed, light cancelled out.
                before = compare_images(rec.frames[i_ref], frame_b, Hm_small)
                before["far"] = before["far"] & res["far"]
                say("same page before and after: ghost of the frame before the input, against the last frame as reference")
                report_compare(against_reference(before, res), frame_b, os.path.join(ROOT, "out", "cam-diff-removed.png"),
                               "removed by the input: ")
        except ValueError as e:
            say(f"  compare skipped: {e}")


def dump(dev, link, path):
    status, payload, hdr = dev.request(link, ">shot", timeout=30)
    if not status.startswith("#ok") or payload is None:
        sys.exit(f"frame dump failed: {status}")
    w, h = int(hdr[1]), int(hdr[2])
    dev.write_png(path, dev.unpack4(payload), w, h)


# ---------------------------------------------------------------- selftest (no camera, no device)

def synth_camera(frame, ghost_frame, ghost_box, ghost_amount, seed=1, size=(1920, 1080)):
    """A camera-like image of the panel: rotated and in perspective, uneven light, gamma, blur, noise, dark bezel."""
    rng = np.random.default_rng(seed)
    E = frame.astype(np.float32) / 255.0
    v = E ** 0.8
    if ghost_frame is not None:
        x, y, w, h = ghost_box
        G = 1.0 - ghost_frame.astype(np.float32) / 255.0  # old ink
        v[y:y + h, x:x + w] -= ghost_amount * G[y:y + h, x:x + w] * (E[y:y + h, x:x + w] > 0.9)
    panel = 0.22 + 0.62 * np.clip(v, 0, 1)
    yy, xx = np.mgrid[0:H, 0:W]
    panel *= 0.75 + 0.25 * (xx / W) * (1 - 0.3 * yy / H)  # light from one side
    pad = 40
    canvas = np.full((H + 2 * pad, W + 2 * pad), 0.12, np.float32)
    canvas[pad:pad + H, pad:pad + W] = panel
    src = np.float32([[pad, pad], [W + pad, pad], [W + pad, H + pad], [pad, H + pad]])
    cx, cy = size[0] / 2, size[1] / 2
    dst = np.float32([[cx - 300, cy - 480], [cx + 270, cy - 500], [cx + 300, cy + 470], [cx - 290, cy + 500]])
    M = cv2.getPerspectiveTransform(src - pad + pad, dst)
    img = cv2.warpPerspective(canvas, M, size, flags=cv2.INTER_AREA, borderValue=0.3)
    img = cv2.GaussianBlur(img, (0, 0), 1.0) + rng.normal(0, 0.008, img.shape).astype(np.float32)
    return np.clip(img * 255, 0, 255).astype(np.uint8), dst


def cmd_selftest(a):
    gold = os.path.join(ROOT, "tests", "ui", "golden")
    # The known ghost page: Display lab Checker, then White (one Fast pass each): the checker shows through the white.
    page, prev = load_frame(os.path.join(gold, "lab-white.png")), load_frame(os.path.join(gold, "lab-checker.png"))
    ok = True
    say("selftest 1: registration and ghost number on synthetic camera images (Display lab White, ghost of Checker)")
    box = (0, H // 4, W // 3, H // 4)  # left column, second row of the 3x4 grid
    results = {}
    for amount in (0.0, 0.03, 0.08):
        img, true_quad = synth_camera(page, prev, box, amount)
        Hm, info = register_sift(img, page)
        if Hm is None:
            say(f"  FAIL: no registration ({info})")
            return 1
        err = float(np.abs(panel_quad(Hm) - true_quad).max())
        res = compare_images(img, page, Hm)
        cells = region_table(res["ghost"])
        tl = cells[1][0][0]
        others = [c[0] for r_, row in enumerate(cells) for c_, c in enumerate(row) if c is not None and (r_, c_) != (1, 0)]
        results[amount] = (tl, max(others))
        say(f"  ghost {amount * 100:.0f} % in one region: corner error {err:.1f} px, that region {tl:.2f} %, other regions max {max(others):.2f} %")
        if err > 4:
            ok = False
            say("  FAIL: corners off by more than 4 px")
        if amount and tl < max(others) + 0.3:
            ok = False
            say("  FAIL: the ghost region does not stand out")
    if results[0.08][0] <= results[0.03][0]:
        ok = False
        say("  FAIL: more ghost does not give a higher number")
    draw_and = synth_camera(page, prev, box, 0.08)[0]
    Hm, _ = register_sift(draw_and, page)
    report_compare(compare_images(draw_and, page, Hm), page, os.path.join(ROOT, "out", "cam-selftest-diff.png"), "selftest, 8 % ghost, left column second row: ")
    cv2.imwrite(os.path.join(ROOT, "out", "cam-selftest-camera.png"), draw_and)

    say("selftest 2: refresh timing from a synthetic 60 fps video (Fast push of a 270x240 rect: flash 120 ms, settle 300 ms)")
    fps, t_input = 60.0, 1.0
    t_push, flash, settle = 1.10, 0.12, 0.30
    page, prev = load_frame(os.path.join(gold, "reader-p4.png")), load_frame(os.path.join(gold, "reader-p2.png"))
    rect = (0, 0, 270, 240)
    frames, times = [], []
    small_w = 480
    for i in range(int(2.0 * fps)):
        t = i / fps
        shown = prev.copy()
        x, y, w, h = rect
        if t >= t_push:
            if t < t_push + flash:
                shown[y:y + h, x:x + w] = 0  # black flash
            elif t < t_push + flash + settle:
                f = (t - t_push - flash) / settle
                shown[y:y + h, x:x + w] = (prev[y:y + h, x:x + w] * (1 - f) + page[y:y + h, x:x + w] * f).astype(np.uint8)
            else:
                shown[y:y + h, x:x + w] = page[y:y + h, x:x + w]
        img, _ = synth_camera(shown, None, None, 0, seed=i)
        small = cv2.resize(img, (small_w, int(small_w * img.shape[0] / img.shape[1])), interpolation=cv2.INTER_AREA)
        frames.append(small)
        times.append(t)
    times = np.array(times)
    Hfull, _ = register_sift(synth_camera(prev, None, None, 0)[0], prev)
    k = small_w / 1920
    Hm_small = np.diag([k, k, 1.0]) @ Hfull
    views = warped_stack(frames, Hm_small)
    lines, act = analyse_timing(views, times, [(t_push, "F", rect, 87)], t_input)
    for l in lines:
        say("  " + l)
    b = visible_span(views, times, rect_slices(rect), t_push - 0.03, times[-1])
    want0, want1 = 0.0, flash + settle
    if not b or abs((b[0] - t_push) - want0) > 0.04 or abs((b[1] - t_push) - want1) > 0.05:
        ok = False
        say(f"  FAIL: expected visible +0 .. +{1000 * want1:.0f} ms")
    draw_timing(times, act, [(t_push, "F", rect, 87)], t_input, os.path.join(ROOT, "out", "cam-selftest-timing.png"), "selftest: synthetic Fast push")
    say("selftest " + ("passed" if ok else "FAILED") + " (images: out/cam-selftest-*.png)")
    return 0 if ok else 1


# ---------------------------------------------------------------- main

def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--camera")
    ap.add_argument("--source")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--size", default=os.environ.get("PAPEROS_CAM_SIZE"), help="camera mode WxH (default: the largest that reaches the fps)")
    ap.add_argument("--via", choices=["auto", "direct", "terminal"], default=os.environ.get("PAPEROS_CAM_VIA", "auto"),
                    help="how ffmpeg reaches the camera: terminal = through Terminal.app (default when not run from a terminal app)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("devices")
    p = sub.add_parser("shot")
    p.add_argument("--out", default="out/cam.png")
    p.add_argument("--warmup", type=float, default=2.0)
    p = sub.add_parser("video")
    p.add_argument("--seconds", type=float, default=5)
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--out", default="out/cam.mp4")
    p = sub.add_parser("calibrate")
    p.add_argument("--image", default="out/cam.png")
    p.add_argument("--frame", default="out/device.png")
    p = sub.add_parser("compare")
    p.add_argument("--image", default="out/cam.png")
    p.add_argument("--frame", default="out/device.png")
    p.add_argument("--diff", default="out/cam-diff.png")
    p.add_argument("--ref", help="camera image of the same page after Clean (Quality), same session: ghost relative to it")
    p.add_argument("--max-age", type=float, default=60, help="minutes a saved panel outline stays usable (default 60)")
    p = sub.add_parser("around")
    p.add_argument("gesture", choices=["tap", "long", "swipe"])
    p.add_argument("coords", nargs="+", type=int)
    p.add_argument("--before", type=float, default=1.0)
    p.add_argument("--after", type=float, default=4.0)
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--port")
    p.add_argument("--max-age", type=float, default=60)
    p = sub.add_parser("lock", help="exposure, white balance, focus to fixed values (uvc-util)")
    p.add_argument("--exposure", type=int, help="exposure-time-abs in 100 us units (default: what auto exposure chose, max 330)")
    p.add_argument("--wb", type=int, help="white balance in K (default: what auto white balance chose)")
    p.add_argument("--focus", default="auto", help="focus-abs 0..250, or auto (sweep)")
    p.add_argument("--frame", default="out/device.png")
    p.add_argument("--max-age", type=float, default=60)
    sub.add_parser("unlock", help="automatic exposure, white balance and focus again")
    sub.add_parser("selftest")
    sub.add_parser("serve", help="capture server; started by itself in Terminal.app when needed")
    a = ap.parse_args()
    VERBOSE = a.verbose
    global VIA
    # A shell of a terminal app (TERM_PROGRAM set) has that app's camera permission; an editor's shell may have none.
    VIA = a.via if a.via != "auto" else ("direct" if os.environ.get("TERM_PROGRAM") else "terminal")
    sys.stdout.reconfigure(line_buffering=True)
    os.chdir(ROOT)
    limit = a.limit or {"devices": 60, "shot": 60, "video": int(getattr(a, "seconds", 0)) + 60, "calibrate": 60, "compare": 60,
                        "around": int(getattr(a, "before", 0) + getattr(a, "after", 0)) + 150, "selftest": 300, "serve": 0, "lock": 120, "unlock": 30}[a.cmd]
    if limit:
        arm_limit(limit)
    fn = {"serve": cmd_serve, "lock": cmd_lock, "unlock": cmd_unlock, "devices": cmd_devices, "shot": cmd_shot, "video": cmd_video, "calibrate": cmd_calibrate, "compare": cmd_compare,
          "around": cmd_around, "selftest": cmd_selftest}[a.cmd]
    sys.exit(fn(a) or 0)


if __name__ == "__main__":
    main()
