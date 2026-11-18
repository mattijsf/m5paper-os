#!/usr/bin/env python3
"""Writes the inbox HTTP fixtures of the UI tests: the companion relay (docs/COMPANION.md) with fake phone notifications,
encrypted with pyca/cryptography (not the Paper's libsodium code), times relative to the simulator's test clock
(2026-01-15T12:34:00Z = 13:34 in Amsterdam). Also writes the paired state the UI test presets use (companion.json).

Hosts: ha.example.test (a full queue, the pairing hello for the UI scripts' fixed random keys), ha-down.example.test (a short
queue plus the phone's answer to a reply), ha-empty.example.test (an empty queue; pushes answer 400), ha-gone.example.test (no
relay at all: 404 / 400), ha-clip.example.test (the phone's clipboard), ha-agenda.example.test (the phone's calendar, D28),
ha-link.example.test (a link shared for reading later, P7-18: the page of the Hacker News fixtures' first article).

  uv run -q --no-project --with cryptography python -I tests/fixtures/make-inbox-fixtures.py tests/fixtures/http tests/ui/data
"""
import base64, hashlib, hmac, struct
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
import calendar, json, os, sys

NOW = calendar.timegm((2026, 1, 15, 12, 34, 0))
MIN, HOUR, DAY = 60, 3600, 86400

def fnv(query):
    h = 2166136261
    for c in query.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return "%08x" % h

def name(host, method, path, query=""):
    n = f"{method}_{host}{path.replace('/', '_')}"
    return n + (("_" + fnv(query)) if query else "") + ".json"

LONG = ("Here is the plan for the weekend, in case you missed it: Saturday we start at the market, then lunch at the harbour, "
        "and in the afternoon the kids want the playground by the lake. Details and the route are at https://www.example.com/weekend/plan?utm_source=chat&ref=family "
        "and the photos from last time are here https://photos.example.org/albums/summer-2025/holiday-with-everyone-all-in-one-album-name-that-is-very-long. "
        "Supercalifragilisticexpialidocious" + "x" * 70 + " ok?")

# A group named mostly in emoji (like the user's family chat): grandpa, grandma, sunglasses, a yellow heart.
FAMILY = "Pap\U0001F474Mam\U0001F475Family \U0001F60E \U0001F49B"

WA = [  # newest first
    ("Anna", FAMILY, "120363000000000001@g.us", 13 * 60 + 31, "See you! \U0001F44D\U0001F3FD"),
    ("Anna", FAMILY, "120363000000000001@g.us", 13 * 60 + 25, "I can bring the cake \U0001F382\U0001F600 for \U0001F468\u200D\U0001F469\u200D\U0001F467"),
    ("Anna", FAMILY, "120363000000000001@g.us", 13 * 60 + 20, "Are you coming at six?"),
    ("Bram", "", "31600000002@s.whatsapp.net", 12 * 60 + 50, LONG),
    ("Carla", "Work", "120363000000000003@g.us", 11 * 60 + 5, "Meeting moved to Thursday"),
    ("Carla", "Work", "120363000000000003@g.us", 11 * 60 + 2, "Can someone send the agenda?"),
    ("Dirk", "", "31600000004@s.whatsapp.net", -(20 * HOUR), "Thanks for yesterday!"),   # yesterday evening
    ("Emma", "", "31600000005@s.whatsapp.net", -(3 * DAY), "Did you see the match?"),
    ("Fleur", "", "31600000006@s.whatsapp.net", -(4 * DAY), "Call me when you can"),
    ("Gijs", "", "31600000007@s.whatsapp.net", -(5 * DAY), "Happy birthday! \U0001F389"),
    ("Hanne", "", "31600000008@s.whatsapp.net", -(20 * DAY), "Photos are in the shared album"),
]

def wa_items():
    out = []
    for i, (frm, chat, key, t, text) in enumerate(WA):
        # t > 0: minutes after local midnight today (CET = UTC+1): 13:31 -> 810 min after 00:00 local? use minutes from 00:00 UTC+1
        ts = (NOW - (NOW % DAY) - HOUR + t * MIN) if t > 0 else NOW + t
        out.append({"id": f"{ts}-3161234{i:04d}-{i:06X}"[:48], "from": frm, "chat": chat, "text": text, "ts": ts, "key": key})
    return out

MAIL = [
    ("Shop", "Your order #4821 is ready", "Hello Daan, your order #4821 & the invoice are ready. Pick it up before Friday.", 12 * 60 + 40),
    ("Anna Jansen", "Re: Saturday", "Sounds good! I will bring the chairs.\n\nAnna", 9 * 60 + 15),
    ("Bank", "Your statement", "Your monthly statement is available in the app.", -(3 * HOUR)),
    ("Newsletter", "Ten tips for \U0001F600 better sleep", "Tip one: go to bed at the same time.", -(DAY + 2 * HOUR)),
    ("Library", "Books due soon", "Two books are due on Monday.", -(3 * DAY)),
    ("Dirk", "Photos", "The photos from the trip are attached (not shown here).", -(9 * DAY)),
]

def b64(b): return base64.urlsafe_b64encode(b).rstrip(b"=").decode()
def pub(sk): return X25519PrivateKey.from_private_bytes(sk).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
def hk(prk, info, n): return hmac.new(prk, info + b"\x01", hashlib.sha256).digest()[:n]
def session(own_sk, peer_pk, pid, paper_pk, phone_pk):
    ss = X25519PrivateKey.from_private_bytes(own_sk).exchange(X25519PublicKey.from_public_bytes(peer_pk))
    prk = hmac.new(b"PaperOS companion v1", ss, hashlib.sha256).digest()
    t = pid + paper_pk + phone_pk
    return hk(prk, b"PaperOS p2d" + t, 32), hk(prk, b"PaperOS d2p" + t, 32)

# The fixed pairing of the UI test presets (the same keys as tests/fixtures/companion/vectors.json).
PAPER_SK, PHONE_SK, PID = bytes(range(0x00, 0x20)), bytes(range(0x40, 0x60)), bytes.fromhex("a1a2a3a4a5a6a7a8")
PAPER_PK, PHONE_PK = pub(PAPER_SK), pub(PHONE_SK)
P2D, D2P = session(PHONE_SK, PAPER_PK, PID, PAPER_PK, PHONE_PK)
_nonce = [0]
def nonce():
    _nonce[0] += 1
    return hashlib.sha256(b"fixture-nonce" + struct.pack(">I", _nonce[0])).digest()[:12]
def envelope(ctr, plain):
    head = bytes([1, ord("P")]) + PID + struct.pack(">Q", ctr)
    n = nonce()
    return b64(head + n + ChaCha20Poly1305(P2D).encrypt(n, json.dumps(plain, ensure_ascii=False, separators=(",", ":")).encode(), head))

def test_random(seed, i):  # core/companion_crypto.h setTestRandom: block i
    return hashlib.sha256(b"paperos-test-random" + struct.pack(">II", seed, i)).digest()

def hello_for_ui_scripts():
    """The phone's hello for the keys a UI script's Paper makes (setTestRandom(1): sk = block 0, pid = block 1)."""
    paper_sk, pid = test_random(1, 0), test_random(1, 1)[:8]
    paper_pk = pub(paper_sk)
    phone_sk = bytes(range(0x70, 0x90))
    phone_pk = pub(phone_sk)
    p2d, _ = session(phone_sk, paper_pk, pid, paper_pk, phone_pk)
    head = bytes([1, ord("H")]) + pid + phone_pk
    n = nonce()
    plain = b'{"t":"hello","name":"Pixel 9 Pro","app":"0.1"}'
    return b64(head + n + ChaCha20Poly1305(p2d).encrypt(n, plain, head))

def wa_posts():
    """WA grouped into one notification per chat, oldest message first (MessagingStyle)."""
    chats = {}
    for item in reversed(wa_items()):
        chats.setdefault(item["key"], []).append(item)
    posts = []
    for i, (key, items) in enumerate(chats.items()):
        group = items[0]["chat"] != ""
        posts.append({"t": "post", "k": "0|com.whatsapp|1|" + key.split("@")[0][-6:] + "|" + str(1000 + i), "app": "whatsapp",
                      "chat": items[0]["chat"] if group else "", "reply": items[0]["from"] != "Gijs", "ts": items[-1]["ts"],
                      "msgs": [{"from": x["from"], "text": x["text"][:500], "ts": x["ts"]} for x in items[-6:]]})
    return sorted(posts, key=lambda p: p["ts"])

def mail_posts():
    out = []
    for i, m in enumerate(reversed(mail_items())):
        out.append({"t": "post", "k": "0|ch.protonmail.android|" + str(4800 + i) + "|null|10" + str(i), "app": "mail", "chat": "", "reply": False,
                    "ts": m["ts"], "msgs": [{"from": m["from"], "subj": m["subject"], "text": m["text"], "ts": m["ts"]}]})
    return out

def queue(plains):
    return [{"id": i + 1, "ts": NOW, "e": envelope(i + 1, p)} for i, p in enumerate(plains)]

def relay_state(entity, q):
    return {"entity_id": entity, "state": str(NOW), "attributes": {"updated": "2026-01-15T13:34:00+01:00", "queue": q}}

def mail_items():
    out = []
    for i, (frm, subj, text, t) in enumerate(MAIL):
        ts = (NOW - (NOW % DAY) - HOUR + t * MIN) if t > 0 and t < 24 * 60 else NOW + t
        out.append({"id": 4800 + i, "from": frm, "subject": subj, "text": text[:1500], "ts": ts, "key": "entry-proton"})
    return out

def agenda_msgs():
    """The phone's calendar as two runs (COMPANION.md 7.1 `agenda`): today (Thursday 2026-01-15) to Sunday, then Monday to
    Thursday 22. Minutes after midnight on the phone. Fourteen rows on the Paper: three pages."""
    def ev(n, s=None, e=None, l=None):
        o = {}
        if s is not None: o["s"] = s
        if e is not None: o["e"] = e
        o["n"] = n
        if l: o["l"] = l
        return o
    return [
        {"t": "agenda", "from": "2026-01-15", "to": "2026-01-18", "days": [
            {"d": 0, "ev": [ev("Anna's birthday"), ev("Dentist", 570, 615, "Main St 4"), ev("Lunch with Carla", 780, 840, "Harbour caf\u00e9"),
                            ev("Project review", 960, 1020), ev("Choir rehearsal", 1170, 1290, "Old church")]},
            {"d": 1, "ev": [ev("School run", 525, 555), ev("Sprint demo", 720, 780, "Room 4")]},
            {"d": 2, "ev": [ev("Weekend on Texel", None, None, "Den Burg"), ev("Ferry", 605, 625, "Den Helder")]},
            {"d": 3, "ev": [ev("Weekend on Texel", None, 1080, "Den Burg")]}]},
        {"t": "agenda", "from": "2026-01-19", "to": "2026-01-22", "days": [
            {"d": 1, "ev": [ev("Quarterly planning with the whole department and the guests from the Utrecht office", 1200, None,
                               "Conference centre De Nieuwe Wereld, room Zuiderzee")]},
            {"d": 3, "ev": [ev("Car to the garage", 480, 510), ev("", 540, 540)], "more": 3}]},
    ]

def write(out, host, method, path, body, query="", status=None):
    p = os.path.join(out, name(host, method, path, query))
    with open(p, "w") as f:
        f.write(body if isinstance(body, str) else json.dumps(body))
    if status:
        with open(p[:-5] + ".status", "w") as f:
            f.write(str(status))

def preset_pairing():
    return json.dumps({"v": 1, "paired": True, "pid": b64(PID), "sk": b64(PAPER_SK), "phone_pk": b64(PHONE_PK), "phone": "Pixel 9 Pro",
                       "tx": 0, "rx": 0})

def main(out, presets):
    os.makedirs(out, exist_ok=True)
    to_paper, pairing = "/api/states/sensor.paperos_relay_to_paper", "/api/states/sensor.paperos_relay_pairing"
    push, ack = "/api/services/script/paperos_relay_push", "/api/services/script/paperos_relay_ack"
    h = "ha.example.test"
    write(out, h, "GET", to_paper, relay_state("sensor.paperos_relay_to_paper", queue(wa_posts() + mail_posts())))
    write(out, h, "GET", pairing, relay_state("sensor.paperos_relay_pairing", [{"id": 0, "ts": NOW, "e": hello_for_ui_scripts()}]))
    write(out, h, "POST", push, "[]")
    write(out, h, "POST", ack, "[]")
    d = "ha-down.example.test"  # a short queue and the phone's answer to reply 1 (the preset has sent [{ref 1, Family}])
    write(out, d, "GET", to_paper, relay_state("sensor.paperos_relay_to_paper",
                                               queue(wa_posts()[-4:] + [{"t": "result", "ref": 1, "ok": False, "why": "too_soon"}])))
    write(out, d, "POST", push, "[]")
    write(out, d, "POST", ack, "[]")
    e = "ha-empty.example.test"
    write(out, e, "GET", to_paper, relay_state("sensor.paperos_relay_to_paper", []))
    write(out, e, "POST", push, {"message": "Service script.paperos_relay_push not found."}, status=400)
    write(out, e, "POST", ack, "[]")
    g = "ha-gone.example.test"  # no relay: 404 for the queues, 400 for the scripts
    for path in (to_paper, pairing):
        write(out, g, "GET", path, {"message": "Entity not found."}, status=404)
    write(out, g, "POST", push, {"message": "Service script.paperos_relay_push not found."}, status=400)
    c = "ha-clip.example.test"  # the phone shared its clipboard (D23, COMPANION.md 7 `clip`)
    write(out, c, "GET", to_paper, relay_state("sensor.paperos_relay_to_paper",
                                               queue([{"t": "clip", "text": "Pick up bread\nand milk", "ts": NOW - 60}])))
    write(out, c, "POST", push, "[]")
    write(out, c, "POST", ack, "[]")
    a = "ha-agenda.example.test"  # the phone's calendar (D28, COMPANION.md 7.1 `agenda`) after two chats
    write(out, a, "GET", to_paper, relay_state("sensor.paperos_relay_to_paper", queue(wa_posts()[-2:] + agenda_msgs())))
    write(out, a, "POST", push, "[]")
    write(out, a, "POST", ack, "[]")
    l = "ha-link.example.test"  # a link shared with the Paper to read later (P7-18): a `clip` that is a title and an address
    write(out, l, "GET", to_paper, relay_state("sensor.paperos_relay_to_paper", queue(
        wa_posts()[-1:] + [{"t": "clip", "text": "A file system that fits on a postcard https://www.example.com/postcard-fs", "ts": NOW - 300}])))
    write(out, l, "POST", push, "[]")
    write(out, l, "POST", ack, "[]")
    # The paired state of the UI test presets that read a relay.
    for name in ("inbox", "inbox-mail", "inbox-error", "inbox-wifi", "inbox-empty", "inbox-gone", "inbox-gone-mail", "whatsapp-send",
                 "whatsapp-reply-result", "whatsapp-send-missing", "sched-refresh", "somdoku", "textfield-phone", "agenda", "agenda-none", "today-board", "read-later"):
        os.makedirs(os.path.join(presets, name, "data"), exist_ok=True)
        with open(os.path.join(presets, name, "data", "companion.json"), "w") as f:
            f.write(preset_pairing())

main(sys.argv[1], sys.argv[2])
