#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["cryptography"]
# ///
"""Firmware update over Wi-Fi for PaperOS (task P7-3, docs/DECISIONS.md D37).

    make ota-key            once: makes the signing key and writes its public half into src/core/update_key.h
    make flash              once after that, over USB: the firmware on the device must hold the public key
    make ota                builds, signs and sends the firmware to a device that shows
                            Settings > About > Update over Wi-Fi (HOST=192.168.1.23 to name it)

    uv run --script tools/ota.py keygen | pack | send [--host H] [--sim] [--force] | info

The private key stays on this Mac, outside the repository: ~/.config/paperos/ota-signing.key (PAPEROS_OTA_KEY names
another file). It is never printed. The device takes only a package signed with it, checks the image's SHA-256 before
making it the one that boots, and goes back to the firmware from before if the new one is reset before it has run 15 s.

Package: 148-byte header + firmware.bin (src/core/update.h):
  "POSOTA1\\n", build[32], size u32 LE, sha256[32], made u64 LE, Ed25519 signature[64] over the first 84 bytes.

Finding the device: --host, else $OTA_HOST, else the address of the last update (out/ota/last-host), else every address
of this Mac's /24 is asked "GET /" on port 80 (the update page answers {"paperos":"update",...}).
"""
import argparse, concurrent.futures, hashlib, http.client, json, os, re, socket, struct, subprocess, sys, time

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_FILE = os.environ.get("PAPEROS_OTA_KEY") or os.path.expanduser("~/.config/paperos/ota-signing.key")
KEY_HEADER = os.path.join(ROOT, "src", "core", "update_key.h")
BIN = os.path.join(ROOT, ".pio", "build", "papers3", "firmware.bin")
OUT = os.path.join(ROOT, "out", "ota")
MAGIC = b"POSOTA1\n"
HEADER_BYTES, SIGNED_BYTES = 148, 84
APP_DESC_OFFSET, APP_DESC_MAGIC, ELF_SHA_OFFSET = 0x20, 0xABCD5432, 0xB0


def say(*a):
    print(*a, flush=True)


def load_key():
    if not os.path.exists(KEY_FILE):
        sys.exit(f"no signing key at {KEY_FILE}: run `make ota-key` first")
    with open(KEY_FILE, "rb") as f:
        return serialization.load_pem_private_key(f.read(), password=None)


def public_bytes(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def header_key():
    """The public key compiled into the firmware (src/core/update_key.h)."""
    with open(KEY_HEADER) as f:
        return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", f.read()))


def cmd_keygen(a):
    if os.path.exists(KEY_FILE):
        key = load_key()
        say(f"signing key exists: {KEY_FILE} (kept)")
    else:
        os.makedirs(os.path.dirname(KEY_FILE), mode=0o700, exist_ok=True)
        key = Ed25519PrivateKey.generate()
        fd = os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as f:
            f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        say(f"made a signing key: {KEY_FILE} (owner only; keep a copy somewhere safe: without it no update is accepted)")
    pub = public_bytes(key)
    rows = [", ".join("0x%02x" % b for b in pub[i:i + 16]) for i in (0, 16)]
    text = (
        "// The public half of the key that signs firmware updates (DECISIONS.md D37). Written by `make ota-key` (tools/ota.py\n"
        "// keygen); the private half stays on the Mac, outside the repository. All zero: no key yet, updates are refused.\n"
        "#pragma once\n\n#include <array>\n#include <cstdint>\n\nnamespace pos::update {\n\n"
        "constexpr std::array<uint8_t, 32> kUpdateKey = {\n    " + ",\n    ".join(rows) + ",\n};\n\n}  // namespace pos::update\n")
    old = open(KEY_HEADER).read() if os.path.exists(KEY_HEADER) else ""
    if old != text:
        with open(KEY_HEADER, "w") as f:
            f.write(text)
        say(f"wrote the public key to {os.path.relpath(KEY_HEADER, ROOT)}: build and flash once over USB (make flash)")
    else:
        say("the public key in src/core/update_key.h is this key's")
    say("public key:", pub.hex())


def build_id(image):
    """What the firmware calls itself (hal::buildId): the first 12 hex digits of its ELF's SHA-256, kept in the image."""
    if len(image) < ELF_SHA_OFFSET + 32 or struct.unpack_from("<I", image, APP_DESC_OFFSET)[0] != APP_DESC_MAGIC:
        sys.exit("not an ESP32 application image (no app description at 0x20)")
    return image[ELF_SHA_OFFSET:ELF_SHA_OFFSET + 32].hex()[:12]


def pack(bin_path, key):
    if not os.path.exists(bin_path):
        sys.exit(f"no firmware at {os.path.relpath(bin_path, ROOT)}: run `make fw` (make ota does)")
    with open(bin_path, "rb") as f:
        image = f.read()
    build = build_id(image)
    made = int(time.time())
    head = MAGIC + build.encode().ljust(32, b"\0") + struct.pack("<I", len(image)) + hashlib.sha256(image).digest() + struct.pack("<Q", made)
    assert len(head) == SIGNED_BYTES
    head += key.sign(head)
    assert len(head) == HEADER_BYTES
    return build, head + image


def cmd_pack(a):
    key = load_key()
    build, package = pack(a.bin, key)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(package)
    say(f"build {build}: {len(package) - HEADER_BYTES} bytes, signed -> {os.path.relpath(a.out, ROOT)}")


def ask(host, port, timeout):
    """GET / of an update page: its JSON, or None."""
    try:
        c = http.client.HTTPConnection(host, port, timeout=timeout)
        c.request("GET", "/")
        r = c.getresponse()
        body = r.read(400)
        c.close()
        d = json.loads(body)
        return d if d.get("paperos") == "update" else None
    except (OSError, ValueError, http.client.HTTPException):
        return None


def my_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))  # no packet is sent: this only picks the interface of the default route
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def find(a):
    port = 8080 if a.sim else 80
    tried = []
    for host, why in ((a.host, "--host"), (os.environ.get("OTA_HOST"), "$OTA_HOST")):
        if host:
            d = ask(host, port, 4)
            if d:
                return host, port, d
            sys.exit(f"no update page at {host}:{port} ({why}): open Settings > About > Update over Wi-Fi on the device")
    if a.sim:
        d = ask("localhost", port, 2)
        if d:
            return "localhost", port, d
        sys.exit("no simulator with the update page open on localhost:8080")
    last = os.path.join(OUT, "last-host")
    if os.path.exists(last):
        host = open(last).read().strip()
        d = ask(host, port, 2)
        if d:
            return host, port, d
        tried.append(host)
    me = my_address()
    if not me:
        sys.exit("this Mac has no network address")
    net = me.rsplit(".", 1)[0]
    say(f"looking for the device on {net}.0/24 ...")
    hosts = [f"{net}.{i}" for i in range(1, 255) if f"{net}.{i}" != me]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
        for host, d in zip(hosts, pool.map(lambda h: ask(h, port, 1.5), hosts)):
            if d:
                return host, port, d
    sys.exit("no device found" + (f" (not at {tried[0]} either)" if tried else "") +
             ": open Settings > About > Update over Wi-Fi on it (it listens for 5 minutes), or name it with HOST=<address>")


def cmd_send(a):
    key = load_key()
    if header_key() != public_bytes(key):
        sys.exit("src/core/update_key.h does not hold this Mac's key: run `make ota-key`, then flash once over USB")
    build, package = pack(a.bin, key)
    host, port, info = find(a)
    say(f"device at {host}: runs build {info.get('build')} from {info.get('slot')}")
    if info.get("build") == build and not a.force:
        say(f"it already runs build {build}: nothing sent (--force sends it anyway)")
        return
    size = len(package) - HEADER_BYTES
    if info.get("max") and size > info["max"]:
        sys.exit(f"the image ({size} bytes) is larger than the device's slot ({info['max']})")
    say(f"sending build {build}, {size} bytes ...")
    t0 = time.time()
    c = http.client.HTTPConnection(host, port, timeout=60)
    c.putrequest("POST", "/update")
    c.putheader("Content-Type", "application/octet-stream")
    c.putheader("Content-Length", str(len(package)))
    c.endheaders()
    sent, step, shown = 0, 32 * 1024, -1
    try:
        while sent < len(package):
            c.send(package[sent:sent + step])
            sent += min(step, len(package) - sent)
            pct = sent * 100 // len(package)
            if pct // 10 != shown:
                shown = pct // 10
                say(f"  {pct:3d} %  {sent // 1024} KB  {time.time() - t0:.1f} s")
        r = c.getresponse()
        answer = r.read(600).decode(errors="replace").strip()
    except OSError as e:
        # The device answers as soon as it refuses a package and closes: the rest of the upload then fails to send.
        try:
            r = c.getresponse()
            answer = r.read(600).decode(errors="replace").strip()
        except (OSError, http.client.HTTPException):
            sys.exit(f"the upload failed after {sent} bytes: {e}")
    if r.status != 200:
        sys.exit(f"the device refused it ({r.status}): {answer}")
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "last-host"), "w") as f:
        f.write(host + "\n")
    say(f"{answer}  ({time.time() - t0:.1f} s)")
    say("it restarts into the new firmware and keeps it once it has run 15 s; Settings > About shows the build.")


def cmd_info(a):
    key = load_key()
    say("key file:", KEY_FILE)
    say("public key:", public_bytes(key).hex())
    say("in src/core/update_key.h:", "yes" if header_key() == public_bytes(key) else "NO (run make ota-key)")
    if os.path.exists(a.bin):
        with open(a.bin, "rb") as f:
            image = f.read()
        say(f"firmware.bin: build {build_id(image)}, {len(image)} bytes")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("keygen", help="make the signing key (kept if it exists) and write its public half into the firmware source")
    s = sub.add_parser("pack", help="sign firmware.bin into a package file")
    s.add_argument("--bin", default=BIN)
    s.add_argument("--out", default=os.path.join(OUT, "paperos.pos"))
    s = sub.add_parser("send", help="sign firmware.bin and send it to a device that shows the update page")
    s.add_argument("--bin", default=BIN)
    s.add_argument("--host")
    s.add_argument("--sim", action="store_true", help="the simulator on localhost:8080")
    s.add_argument("--force", action="store_true", help="send even if the device runs this build")
    s = sub.add_parser("info", help="which key, which build")
    s.add_argument("--bin", default=BIN)
    a = p.parse_args()
    {"keygen": cmd_keygen, "pack": cmd_pack, "send": cmd_send, "info": cmd_info}[a.cmd](a)


if __name__ == "__main__":
    main()
