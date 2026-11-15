# Backend: Home Assistant is the hub

The device talks to exactly one server: the user's Home Assistant. It is used for

1. controlling the house (Home app), and
2. the **inbox feeds**: recent WhatsApp messages and mails, which Home Assistant collects from
   the notifications of the user's Android phone.

One URL, one token, one TLS setup, nothing new to host. Why not talk to WhatsApp or the mail
server directly: see `DECISIONS.md` D8 and D9.

## Connection

| Setting | Where | Notes |
|---|---|---|
| Base URL | `config.json` `ha.baseUrl` | The public HTTPS address is set by the user. On the home network a plain `http://<ip>:8123` URL is faster and needs no TLS; then the token crosses the LAN unencrypted. Default: HTTPS. |
| Token | `secrets.json` `ha.token` | Long-lived access token: HA > profile > Security > Long-lived access tokens. Name it "PaperOS". |
| TLS | device: Arduino `NetworkClientSecure` with the built-in CA bundle (`setCACertBundle`); sim: libcurl | no certificate pinning |

Every request carries `Authorization: Bearer <token>` and `Content-Type: application/json`.
The token is stored in plain text in the device flash (`/data/secrets.json`): anyone holding the
device and a USB cable can read it. If that matters, create a separate Home Assistant user for
the device and make the token there; revoke it in HA if the device is lost.

## API calls used

| Purpose | Request | Response |
|---|---|---|
| Test | `GET /api/` | `{"message":"API running."}` |
| One entity | `GET /api/states/<entity_id>` | `{"entity_id","state","attributes":{...},"last_changed"}` |
| Call a service | `POST /api/services/<domain>/<service>` body `{"entity_id":"light.x","brightness_pct":60}` | `200` + list of changed states |
| Many states in one request | `POST /api/template` body `{"template":"..."}` | rendered text (we make it JSON) |
| Fire an event | `POST /api/events/<event_type>` body `{...}` | `200` |

### Reading many states

`GET /api/states` returns every entity (hundreds of KB): too slow and too big. The Home app
instead renders a template that returns only what it needs, as compact JSON:

```jinja
{% set ids = ['light.lounge','light.lounge_relative','sensor.outdoor_temperature'] %}
{% set ns = namespace(o={}) %}
{% for id in ids %}{% set s = states[id] %}{% if s %}
{% set ns.o = dict(ns.o, **{id: {'s': s.state,
   'b': s.attributes.brightness, 'm': s.attributes.entity_id,
   't': s.attributes.temperature, 'c': s.attributes.current_temperature,
   'u': s.attributes.unit_of_measurement}}) %}
{% endif %}{% endfor %}{{ ns.o | tojson }}
```

Result: `{"light.lounge":{"s":"on","b":153,"m":["light.a","light.b"],...}, ...}`. `ids` is built
from `home.json` (entities, groups and members). Null attributes are simply absent or null.

Caveats to verify in task P5-2 (HA > Developer tools > Template first, then over the API):

- `POST /api/template` needs an **administrator** token. With a non-admin token it returns 401:
  then fall back to one `GET /api/states/<id>` per entity over a kept-alive connection (slower,
  ~50 ms each on the LAN). Implement the fallback only if the user chooses a non-admin user.
- The `dict(a, **b)` idiom must work in the installed HA version; if not, build the JSON by
  string concatenation with `tojson` per value.

## Inbox feeds

### How messages get into Home Assistant

The Home Assistant Android app has a **Last Notification** sensor and a **Last Removed
Notification** sensor (App > Settings > Companion app > Manage sensors > Notification sensors).
With an allow list they report notifications from chosen apps only:

- state = notification text; attributes include `android.title` (sender, or "Group: Sender"),
  `android.text`, `package`, `post_time`, `category`, `group_id`, `is_clearable`.
- They need the "notification access" permission on the phone.
- Source: https://companion.home-assistant.io/docs/core/sensors/ (Notification sensors).

Phone setup (once, by the user, on the Pixel that receives the messages):

1. Enable both sensors.
2. Allow list for both: `com.whatsapp` and the mail app (`ch.protonmail.android` for Proton
   Mail, `com.google.android.gm` for Gmail).
3. Note the entity ids (expected `sensor.pixel_9_pro_last_notification` and
   `sensor.pixel_9_pro_last_removed_notification`; check in HA > Settings > Entities).

### How Home Assistant stores them

A package (`ha/paperos_inbox.yaml` in this repo, a **draft** to be tested in task P6-1) defines
two trigger-based template sensors that keep the newest 25 items in an attribute:

| Entity | state | attributes |
|---|---|---|
| `sensor.paperos_whatsapp_inbox` | number of items | `items`: list, newest first; `updated`: ISO time |
| `sensor.paperos_mail_inbox` | number of items | same |

Item shape (the **feed contract** between Home Assistant and the device):

```json
{ "id": "1763200000123", "from": "Anna", "chat": "Family", "text": "Are you coming at six?", "ts": 1763200000 }
```

- `id`: `post_time` of the notification (unique enough, sortable).
- `from`: sender. `chat`: group name or empty. WhatsApp titles look like `Sender` or
  `Group: Sender`; the template splits on the first `": "`.
- `text`: notification text, cut to 500 characters. For mail: the subject (and the snippet when
  the mail app includes one).
- An item is **removed** when the phone removes the notification (the user read the chat on the
  phone, or dismissed it): the removed-notification trigger drops all items of the same chat.
  So the list is "recent and still unread on the phone", which is what was asked for.
- A `paperos_inbox_clear` event with `{"feed":"whatsapp"}` empties a feed (the device's
  "Mark all read" button fires it).

Summary notifications ("5 messages from 2 chats", title `WhatsApp`) are ignored by a condition
in the template. Messages in muted chats never produce a notification and never appear: accepted.

### Installing the package

The Home Assistant config lives in a repo of its own and goes **live on push to
main**. That repo has its own rules (there: check YAML, push to main, wait for the
sync, tell the user). Do the install as its own step, in that repo, following those rules:
copy `ha/paperos_inbox.yaml` to `home-assistant/packages/paperos_inbox.yaml`, adjust the two
phone sensor ids, validate, push. Also exclude the two inbox sensors from the recorder (the
package does this) so message text is not written to the history database.

### Device side

`GET /api/states/sensor.paperos_whatsapp_inbox` and read `attributes.items`. That is all. The
device keeps the last response in `/data/cache/inbox/<feed>.json` and a "seen up to id" marker
per feed for the unread badge on the launcher.

## Privacy

Message text passes through and is stored in Home Assistant (in memory and in
`.storage/core.restore_state`), and is cached on the device. Home Assistant is the user's own
server. Nothing is sent to any third party. The feeds must never be exposed under `www/`
(public) or in a dashboard shared with guests.

## Optional later: gateway for full mail bodies (Phase 8)

Notifications carry sender and subject only. Reading full mail text needs a small service on
the home network (`gateway/`, Docker) that logs in to the mailbox over IMAP (for Proton: via
Proton Mail Bridge), converts mails to plain text, and serves the same feed contract with an
extra `body` field. The device would then read `GET <gateway>/v1/mail`. Not designed in detail
yet; do not start it without asking the user which mail provider and where to host it.
