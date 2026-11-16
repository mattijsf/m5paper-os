# Backend: Home Assistant is the hub

The device talks to exactly one server: the user's Home Assistant (HA OS 2026.10, with add-ons
and HACS). Home Assistant does the heavy work; the device only reads small JSON documents.

1. **Home app**: read entity states, call services.
2. **Inbox (WhatsApp, Mail)**: since 2026-11-16 (DECISIONS.md D22) Home Assistant is **not trusted with message text**. A
   companion Android app on the user's phone reads the WhatsApp and Proton Mail notifications, encrypts each one for the
   device, and posts only ciphertext; Home Assistant stores and forwards it ("Companion app" below). **Not built yet**: today
   Home Assistant has no feed sensors and the inbox apps say "Not set up yet".

The device never speaks WhatsApp or IMAP, and Home Assistant never sees a message in plaintext. The earlier route (a WhatsApp
add-on and Proton Mail Bridge inside Home Assistant, D16/D17) was removed on 2026-11-16; see "Removed: the add-on route" below.

```
phone: WhatsApp / Proton Mail notifications --> [companion app] --encrypted blob--> [HA webhook -> relay sensor] --+
                                    ^                                                                             |
                                    +---------- encrypted command (reply, mark read) <-- [HA command queue] <--+  +--> REST --> PaperOS
lights, climate, switches -------------------------------------------------------------- states -----------------+
```

## Connection

| Setting | Where | Notes |
|---|---|---|
| Token | `/sd/paperos_config`, line `HA_TOKEN=...` | Present on the device's SD card (183 characters, checked 2026-11-15). See `apps/settings.md` "Provisioning". |
| Base URL | `/sd/paperos_config` `HA_URL=...`, else `config.json` `ha.baseUrl` | **Decision the public HTTPS address**, set by the user as `HA_URL=` on the card, so the device also works away from home. It is **never hard-coded in firmware and never committed as a default** (the `config.json` default stays a placeholder). HTTPS with the CA bundle is the expected case; the plain `http://` LAN address remains an optional alternative at home: no TLS handshake, faster, less RAM. |
| User | a dedicated **non-admin** Home Assistant user | The home-assistant repo's standing rule: never put the Mac's admin token on other devices (the TV app has its own non-admin user). The design below needs no admin rights. Which user the current token belongs to is not known: confirm with the owner in task P5-1. |

**Cost of TLS** (HTTPS is the expected case): a handshake takes about 1.5 s and about 45 KB of RAM. So the net
service opens **one kept-alive connection per refresh and sends every request of that refresh over it** (a Home
tab reads many entities), and the Wi-Fi linger (`net.lingerSec`) must be long enough that a second refresh a few
seconds later does not pay a second handshake. Do not open a connection per request.

Every request carries `Authorization: Bearer <token>` and `Content-Type: application/json`.
The token sits in plain text on the SD card: whoever holds the device can read it. Revoke it in
Home Assistant (profile > Security) if the device is lost.

## API calls used

| Purpose | Request | Response |
|---|---|---|
| Test | `GET /api/` | `{"message":"API running."}` |
| One entity | `GET /api/states/<entity_id>` | `{"entity_id","state","attributes":{...},"last_changed"}` |
| Call a service | `POST /api/services/<domain>/<service>` body `{"entity_id":"light.x","brightness_pct":60}` | `200` + list of changed states |
| Service with a response | `POST /api/services/<domain>/<service>?return_response` | `{"changed_states":[...],"service_response":{...}}` |
| Fire an event | `POST /api/events/<event_type>` body `{...}` | `200` (needs admin: see "Mark all read" below) |

### Reading the Home app's states

`GET /api/states` returns every entity (hundreds of KB): not used. `POST /api/template` would
give everything in one call but needs an admin token: not used.

v1: one `GET /api/states/<id>` per entity in `home.json`, sequentially over one kept-alive HTTP
connection (about 30 entities; expected 1 to 2 s on the LAN). Only the visible tab's entities
are fetched first; the other tabs follow in the background.

If task P5-2 measures more than 3 s for a tab, add an aggregate sensor to the Home Assistant
package: a template sensor `sensor.paperos_home` whose attribute `states` holds
`{entity_id: {s, b, ...}}` for the listed entities, so the device needs one request. Do not
build this before measuring.

## Inbox feeds

Both feeds have the same shape so the device has one implementation (`apps/inbox.md`).
Until 2026-11-16 Home Assistant filled these two sensors in plaintext (D16/D17); both are gone now (404).

| Entity | state | attributes |
|---|---|---|
| `sensor.paperos_whatsapp_inbox` | unix time of last change | `items`: list, newest first, max 30; `updated` |
| `sensor.paperos_mail_inbox` | unix time of last change | `items`: list, newest first, max 20; `updated` |

Item (the **feed contract**):

```json
{ "id": "1763200000-31612345678", "from": "Anna", "chat": "Family", "text": "Are you coming at six?",
  "ts": 1763200000, "key": "31612345678@s.whatsapp.net" }
```

| Field | WhatsApp | Mail |
|---|---|---|
| `id` | timestamp + sender number | IMAP uid |
| `from` | sender display name | sender address/name |
| `chat` | group name, or empty for a direct chat | empty |
| `subject` | absent | mail subject |
| `text` | message text, max 500 chars | start of the body as plain text, max 1500 chars |
| `ts` | unix seconds | unix seconds |
| `key` | chat id (JID): used to drop a chat's items | IMAP config entry id: used to fetch the full text |

Since D22 this is the shape of a feed **after the device has decrypted it**: the relay holds ciphertext, the device turns each
decrypted item into this contract and the list, detail and badge code stay as they are. The relay's own layout is fixed in task
P6-8/P6-9 (ROADMAP.md). What a notification gives: WhatsApp (MessagingStyle) sender, group name and the message texts; Proton Mail
sender and subject, maybe a short preview, never the full body (so mail `text` is short and `key` no longer names an IMAP entry).

The package is `ha/paperos_inbox.yaml`, a copy of the live `packages/paperos_inbox.yaml` in the Home Assistant config repo
(the Home Assistant repo is the source, sync by copying). Since 2026-11-16 (home-assistant commit `ebc133d`) it holds only
`script.paperos_inbox_clear` and a recorder exclusion of `sensor.paperos_whatsapp_inbox` / `sensor.paperos_mail_inbox`, kept for the
relay. `GET /api/states/sensor.paperos_*_inbox` answers **404** (checked live 2026-11-16 after the orphaned entities were removed):
the inbox shows "Not set up yet" and drops its saved list (apps/inbox.md).

## Companion app (D22, planned: tasks P6-8 to P6-11)

Design sketch from 2026-11-16; P6-8 turns it into `docs/COMPANION.md`. Nothing of it is built.

- **Threat model**: Home Assistant and every integration in it may be hostile. It may read, drop, delay and replay what passes
  through; it must not be able to read or forge an item, or make the phone send. The device parses everything from Home Assistant as
  untrusted input (bounded JSON, sizes checked before decrypting).
- **Phone side**: a sideloaded Kotlin app, NotificationListenerService for WhatsApp and Proton Mail, foreground service and a
  battery-optimisation exemption. `onNotificationRemoved` (read or dismissed on the phone) can drop the item on the device.
- **Pairing**: the device makes an X25519 key pair (private key in `/sd/paperos_config`, secret rules apply) and shows its public key
  as a QR code; the app scans it and posts its own public key through Home Assistant; both show a short fingerprint and the user
  confirms they match.
- **Items**: `crypto_box`-style authenticated encryption (X25519 + XChaCha20-Poly1305) with both keys, plus a counter or time window
  against replay. Device: Monocypher (one C file, platform-free, `core`). Phone: libsodium (Lazysodium).
- **Relay in Home Assistant**: a webhook trigger (`POST /api/webhook/<id>`; the id is a secret) into a trigger-based template sensor
  holding the last N blobs per feed, recorder-excluded. Home Assistant still sees timing, sizes and count.
- **Commands back** (reply, mark read): encrypted device to phone, delivered through a sensor or event the app reads (it keeps a
  websocket to Home Assistant; no Google push). **Send guards live in the phone app** (length, 10 s gap, 20 per hour, one chat), not
  in Home Assistant. A reply works only while that chat's notification is still on the phone (its RemoteInput action).
- **Limits**: muted chats and media are missing; mail has no body; the phone must be on and online.

### "Mark all read" from the device

The package defines `script.paperos_inbox_clear` (field `feed`: `whatsapp` or `mail`; optional **`before`**, a unix time) which fires
the `paperos_inbox_clear` event. **Kept for the relay** (P6-9 decides what it clears); since 2026-11-16 nothing listens to the event.
`before`: the device sends the newest `ts` its list showed, so a message that arrived after the list was fetched is not lost.
`event_data.before` is rendered with `| int(0)`. The device calls `POST /api/services/script/paperos_inbox_clear` with
`{"feed":"whatsapp","before":<ts>}` (a service call works for a non-admin user; firing events directly does not). Installed and tested
in Home Assistant's own engine on 2026-11-15 (home-assistant commit 80e19b0).

### Sending a WhatsApp message (D21): removed by D22

`script.paperos_whatsapp_send` (guards: 1..1000 characters, one chat JID, 10 s gap, 20 per hour) was installed on 2026-11-16 and
sent one message to the user's own number; it was removed the same day with the add-on. The device code (compose, confirmation,
`whatsapp::sendToFavorite`) stays; `POST /api/services/script/paperos_whatsapp_send?return_response` now answers **400** (checked live
2026-11-16), which the device shows as the toast "Not sent: no send script in Home Assistant", keeping the text. The guards move to
the phone app (P6-11); the script's design is in git (`ha/paperos_inbox.yaml` before D22, `tests/ha/check-send-script.py`).

### Removed: the add-on route (D16, D17; 2026-11-15 to 2026-11-16)

Kept as a record; details in git history (this file before D22, home-assistant `addons/protonmail_bridge/` up to d97a365, and the
Proton Mail notes).

- **WhatsApp Gateway** (FaserF, Baileys 7.0.0-rc14, add-on 2.1.10 + `whatsapp` integration): linked on 2026-11-15, a real message
  reached the feed in 5 s. Learned: without its UI password every private-network address was superadmin and its API key went out
  over mDNS; `/api/dashboard` and `POST /api/autoresponder/config` had no auth even with it; the integration offered 57 services
  (including `whatsapp.send_message`) to every Home Assistant login. Removal on 2026-11-16: "Hard Reset & Logout" in its web UI
  (Baileys `logout()`: the linked device is dropped at WhatsApp, credentials deleted), then the integration, the add-on with its data,
  and the add-on repository. Left on disk: `/config/custom_components/whatsapp` (unused code the add-on wrote; delete by hand).
- **Proton Mail Bridge** (local add-on, Bridge 3.27.2 built on the Green; IMAP over SSL on port 143 for Core only, because the IMAP
  integration speaks implicit TLS only): built and running, never logged in to Proton. Removed with its data on 2026-11-16; left on
  disk: `/local_apps/protonmail_bridge` (the add-on source copy; delete by hand).

### Installing the package

The Home Assistant config is a repo of its own: **live on push to main**,
with its own rules (its docs: check YAML, push to main, wait for
the sync, answer the user in Dutch there). The Home Assistant repo's `packages/paperos_inbox.yaml` is the source: change it
there, validate, push, then copy it byte for byte to `ha/paperos_inbox.yaml` here.
Add-ons and integrations set up in the UI live in `.storage/` and are not in git: record what
was installed in that repo's `docs/HANDOVER.md`.

## Privacy

Home Assistant holds no message or mail text (D22); with the companion app it will hold ciphertext and metadata (timing, sizes,
count) only. Decrypted text lives on the device: in memory and in its cache on the device's internal flash (`/data/cache/inbox/`).
The device's sleep screen, which anyone near it can read, shows the unread count only ("3 unread"), never a sender or text. Someone else
also uses this Home Assistant: nothing of the inbox may appear on a dashboard or under `www/`.

## No fallback through Home Assistant

The Home Assistant Android app's notification sensors (D8) would put message text into Home Assistant in plaintext, so they are no
longer a fallback (D22). Without the companion app the inbox simply stays empty.
