#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["cryptography"]
# ///
"""Firmware update over Wi-Fi for PaperOS (task P7-3, docs/DECISIONS.md D37).

    make ota-key            once: makes the signing key and writes its public half into src/core/update_key.h
    make flash              once after that, over USB: the firmware on the device must hold the public key
    make ota                builds, signs and sends the firmware over Wi-Fi to a device with
                            Settings > Developer > Wireless development on (HOST=192.168.1.23 to name it)

    uv run --script tools/ota.py keygen | pack | send [--host H] [--sim] [--force] | info | host [ADDRESS]

The private key stays on this Mac, outside the repository: ~/.config/paperos/ota-signing.key (PAPEROS_OTA_KEY names
another file). It is never printed. The device takes only a package signed with it, checks the image's SHA-256 before
making it the one that boots, and goes back to the firmware from before if the new one is reset before it has run 15 s.

Package: 148-byte header + firmware.bin (src/core/update.h):
  "POSOTA1\\n", build[32], size u32 LE, sha256[32], made u64 LE, Ed25519 signature[64] over the first 84 bytes.

Finding the device: --host, else $OTA_HOST, else the address a device on USB reports, else the address of the last
update (out/ota/last-host), else its usual address (`ota.py host ADDRESS` saves it next to the key: a reservation in
the router keeps it the same). The network is never searched.
"""
import argparse, hashlib, os, re, struct, subprocess, sys, time

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_FILE = os.environ.get("PAPEROS_OTA_KEY") or os.path.expanduser("~/.config/paperos/ota-signing.key")
HOST_FILE = os.path.join(os.path.dirname(KEY_FILE), "ota-host")  # the device's usual address (`ota.py host ADDRESS`)
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


def find(a):
    """The device to send to: --host, $OTA_HOST, the address a device on USB reports, the last update's, the saved one.
    No search of the network: asking every address of the home network looks like an attack to a router (2026-11-18:
    a mesh router then dropped everything from this Mac to the device's port 80)."""
    if a.sim:
        return "localhost"
    for host in (a.host, os.environ.get("OTA_HOST"), usb_address(), last_host(), usual_host()):
        if host:
            return host
    sys.exit("no address for the device: make ota HOST=<address> (Settings > Developer shows it), or save it once: "
             "uv run --script tools/ota.py host ADDRESS")


def usual_host():
    """The address the device usually has (a reservation in the router): `ota.py host ADDRESS` saved it."""
    return open(HOST_FILE).read().strip() if os.path.exists(HOST_FILE) else None


def cmd_host(a):
    if a.address:
        if not re.fullmatch(r"[A-Za-z0-9.-]+", a.address):
            sys.exit("an address or a host name, nothing else")
        os.makedirs(os.path.dirname(HOST_FILE), mode=0o700, exist_ok=True)
        with open(HOST_FILE, "w") as f:
            f.write(a.address + "\n")
    say("the device's usual address:", usual_host() or "(none saved)")


def last_host():
    path = os.path.join(OUT, "last-host")
    return open(path).read().strip() if os.path.exists(path) else None


def usb_address():
    """The Wi-Fi address of a device on USB (`tools/device.py status`), or None. Never waits long."""
    import glob
    if not glob.glob("/dev/cu.usbmodem*"):
        return None
    py = os.path.expanduser("~/.platformio/penv/bin/python")
    try:
        out = subprocess.run([py, os.path.join(ROOT, "tools", "device.py"), "--limit", "12", "status"], capture_output=True, text=True,
                             timeout=15).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"^wifi: connected .* ip=(\d+\.\d+\.\d+\.\d+)", out, re.M)
    return m.group(1) if m else None


def cmd_send(a):
    """Over wireless development (D39): the signed sign-in, then `>ota <size>` and the package in 4 KB pieces, each
    acknowledged; `#ok` when the image is in place, then the device restarts into it."""
    key = load_key()
    if header_key() != public_bytes(key):
        sys.exit("src/core/update_key.h does not hold this Mac's key: run `make ota-key`, then flash once over USB")
    build, package = pack(a.bin, key)
    host = find(a)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import device
    link = device.Link(wifi=host)
    say(f"device at {host}: runs build {link.build}")
    if link.build == build and not a.force:
        say(f"it already runs build {build}: nothing sent (--force sends it anyway)")
        link.close()
        return
    say(f"sending build {build}, {len(package) - HEADER_BYTES} bytes ...")
    t0 = time.time()
    link.send(f">ota {len(package)}")
    line = link.readline()
    while line and not line.startswith(("#ota go", "#err")):
        line = link.readline()
    if not line.startswith("#ota go"):
        sys.exit(f"the device refused it: {line or 'no answer'}")
    chunk, sent, shown = 4096, 0, -1
    while sent < len(package):
        piece = package[sent:sent + chunk]
        link.w.write(piece)
        sent += len(piece)
        if sent < len(package) and sent % chunk == 0:  # the device acknowledges every full 4 KB (except the last)
            r = link.readline()
            while r and not r.startswith(("#ack", "#err")):
                r = link.readline()
            if not r.startswith("#ack"):
                sys.exit(f"the device refused it after {sent} bytes: {r or 'no answer'}")
        pct = sent * 100 // len(package)
        if pct // 10 != shown:
            shown = pct // 10
            say(f"  {pct:3d} %  {sent // 1024} KB  {time.time() - t0:.1f} s")
    r = link.readline()
    while r and not r.startswith(("#ok", "#err")):
        r = link.readline()
    link.close()
    if not r.startswith("#ok"):
        sys.exit(f"the device refused it: {r or 'no answer'}")
    if not a.sim:
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "last-host"), "w") as f:
            f.write(host + "\n")
    say(f"build {build} is in place, the device restarts ({time.time() - t0:.1f} s); it keeps it once it has run 15 s.")


def cmd_info(a):
    key = load_key()
    say("key file:", KEY_FILE)
    say("public key:", public_bytes(key).hex())
    say("in src/core/update_key.h:", "yes" if header_key() == public_bytes(key) else "NO (run make ota-key)")
    say("the device's usual address:", usual_host() or "(none saved)", "| last update:", last_host() or "(none)")
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
    s = sub.add_parser("send", help="sign firmware.bin and send it over wireless development (Settings > Developer)")
    s.add_argument("--bin", default=BIN)
    s.add_argument("--host")
    s.add_argument("--sim", action="store_true", help="the simulator on localhost")
    s.add_argument("--force", action="store_true", help="send even if the device runs this build")
    s = sub.add_parser("host", help="save (or show) the address the device usually has, for `make ota` without HOST=")
    s.add_argument("address", nargs="?")
    s = sub.add_parser("info", help="which key, which build")
    s.add_argument("--bin", default=BIN)
    a = p.parse_args()
    {"keygen": cmd_keygen, "pack": cmd_pack, "send": cmd_send, "info": cmd_info, "host": cmd_host}[a.cmd](a)


if __name__ == "__main__":
    main()
