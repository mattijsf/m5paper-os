#!/usr/bin/env python3
"""Writes the inbox HTTP fixtures of the UI tests (feed contract, docs/BACKEND.md "Inbox feeds"): fake messages, times relative to
the simulator's test clock (2026-01-15T12:34:00Z = 13:34 in Amsterdam). Hosts: ha.example.test (normal), ha-down.example.test
(WhatsApp link down), ha-empty.example.test (empty feeds).

  python3 tests/fixtures/make-inbox-fixtures.py tests/fixtures/http
"""
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

WA = [  # newest first
    ("Anna", "Family", "g-family@g.us", 13 * 60 + 31, "See you!"),
    ("Anna", "Family", "g-family@g.us", 13 * 60 + 25, "I can bring the cake \U0001F382\U0001F600"),
    ("Anna", "Family", "g-family@g.us", 13 * 60 + 20, "Are you coming at six?"),
    ("Bram", "", "bram@s.whatsapp.net", 12 * 60 + 50, LONG),
    ("Carla", "Werk", "g-werk@g.us", 11 * 60 + 5, "Meeting moved to Thursday"),
    ("Carla", "Werk", "g-werk@g.us", 11 * 60 + 2, "Can someone send the agenda?"),
    ("Dirk", "", "dirk@s.whatsapp.net", -(20 * HOUR), "Thanks for yesterday!"),   # yesterday evening
    ("Emma", "", "emma@s.whatsapp.net", -(3 * DAY), "Did you see the match?"),
    ("Fleur", "", "fleur@s.whatsapp.net", -(4 * DAY), "Call me when you can"),
    ("Gijs", "", "gijs@s.whatsapp.net", -(5 * DAY), "Happy birthday! \U0001F389"),
    ("Hanne", "", "hanne@s.whatsapp.net", -(20 * DAY), "Photos are in the shared album"),
]

def wa_items():
    out = []
    for i, (frm, chat, key, t, text) in enumerate(WA):
        # t > 0: minutes after local midnight today (CET = UTC+1): 13:31 -> 810 min after 00:00 local? use minutes from 00:00 UTC+1
        ts = (NOW - (NOW % DAY) - HOUR + t * MIN) if t > 0 else NOW + t
        out.append({"id": f"{ts}-{i}", "from": frm, "chat": chat, "text": text, "ts": ts, "key": key})
    return out

HTML_MAIL = ("<html><head><style>p{color:#333}</style></head><body><p>Hello Daan,</p><p>Your order <b>#4821</b> &amp; the invoice are "
             "ready. Pick it up before Friday.</p><ul><li>1 x Paper notebook</li><li>2 x Pencil &mdash; soft</li></ul>"
             "<p>Track it at <a href=\"https://www.shop.example/orders/4821?ref=mail\">https://www.shop.example/orders/4821?ref=mail</a></p>"
             "<p>Thanks,<br>The shop</p></body></html>")
FULL_HTML = HTML_MAIL + "<p>" + " ".join(["This is a longer paragraph of the full mail text that the excerpt does not show."] * 18) + "</p>"

MAIL = [
    ("Shop <orders@shop.example>", "Your order #4821 is ready", HTML_MAIL, 12 * 60 + 40),
    ("Anna Jansen", "Re: Saturday", "Sounds good! I will bring the chairs.\n\nAnna", 9 * 60 + 15),
    ("Bank", "Your statement", "Your monthly statement is available in the app.", -(3 * HOUR)),
    ("Newsletter", "Ten tips for \U0001F600 better sleep", "<p>Tip one: go to bed at the same time.</p>", -(DAY + 2 * HOUR)),
    ("Library", "Books due soon", "Two books are due on Monday.", -(3 * DAY)),
    ("Dirk", "Photos", "The photos from the trip are attached (not shown here).", -(9 * DAY)),
]

def mail_items():
    out = []
    for i, (frm, subj, text, t) in enumerate(MAIL):
        ts = (NOW - (NOW % DAY) - HOUR + t * MIN) if t > 0 and t < 24 * 60 else NOW + t
        out.append({"id": 4800 + i, "from": frm, "subject": subj, "text": text[:1500], "ts": ts, "key": "entry-proton"})
    return out

def state(items, connected=None):
    attrs = {"updated": "2026-01-15T13:34:00+01:00", "items": items}
    if connected is not None:
        attrs["connected"] = connected
    return {"entity_id": "sensor.paperos_inbox", "state": str(items[0]["ts"] if items else 0), "attributes": attrs}

def write(out, host, method, path, body, query="", status=None):
    p = os.path.join(out, name(host, method, path, query))
    with open(p, "w") as f:
        f.write(body if isinstance(body, str) else json.dumps(body))
    if status:
        with open(p[:-5] + ".status", "w") as f:
            f.write(str(status))

def main(out):
    os.makedirs(out, exist_ok=True)
    h = "ha.example.test"
    write(out, h, "GET", "/api/states/sensor.paperos_whatsapp_inbox", state(wa_items(), True))
    write(out, h, "GET", "/api/states/sensor.paperos_mail_inbox", state(mail_items()))
    write(out, h, "POST", "/api/services/imap/fetch", {"changed_states": [], "service_response": {"text": FULL_HTML}}, "return_response")
    write(out, h, "POST", "/api/services/script/paperos_inbox_clear", "[]")
    d = "ha-down.example.test"
    write(out, d, "GET", "/api/states/sensor.paperos_whatsapp_inbox", state(wa_items()[:4], False))
    e = "ha-empty.example.test"
    for s in ("whatsapp", "mail"):
        write(out, e, "GET", f"/api/states/sensor.paperos_{s}_inbox", {"entity_id": "x", "state": "0", "attributes": {"updated": "2026-01-15T13:34:00+01:00", "items": []}})

main(sys.argv[1])
