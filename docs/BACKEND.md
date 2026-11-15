# Backend: Home Assistant is the hub

The device talks to exactly one server: the user's Home Assistant (HA OS 2026.10, with add-ons
and HACS). Home Assistant does the heavy work; the device only reads small JSON documents.

1. **Home app**: read entity states, call services.
2. **WhatsApp inbox**: a Home Assistant add-on is linked to the user's WhatsApp account and
   fires an event per incoming message; a template sensor keeps the recent ones.
3. **Mail inbox**: Proton Mail Bridge exposes the mailbox over IMAP; Home Assistant's IMAP
   integration fires an event per new mail; a template sensor keeps the recent ones.

The device never speaks WhatsApp or IMAP. It reads two sensors. The reasons and the rejected
alternatives are in `DECISIONS.md` D16 and D17.

```
WhatsApp servers <--linked device--> [WhatsApp add-on] --event--> [template sensor] --+
Proton Mail <--> [Proton Bridge] <--IMAP--> [HA IMAP integration] --event--> [sensor] +--> REST --> PaperOS
lights, climate, switches ---------------------------------------------- states ------+
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

The package that defines both sensors is `ha/paperos_inbox.yaml` (**draft**, tested in task P6-3).

### WhatsApp: "WhatsApp for Home Assistant" (FaserF)

- Docs: https://faserf.github.io/ha-whatsapp/ . Two parts: an add-on ("App", from the add-on
  repository `https://github.com/FaserF/hassio-addons`) that holds the WhatsApp connection
  using the Baileys library, and the `whatsapp` integration that talks to it.
- Linking: the add-on shows a QR code; scan it in WhatsApp > Linked devices. It then behaves
  like WhatsApp Web: one more linked device on the user's own account.
- Incoming messages fire the event **`whatsapp_message_received`** with: `type` (`chat` for
  text), `sender` (JID: `...@s.whatsapp.net` person, `...@g.us` group), `sender_number`,
  `sender_name`, `content`, `timestamp`, `media_url`.
- The user's own replies (from the phone too) fire **`whatsapp_message_sent`** with `to` /
  `recipient` (JID), `is_group`, `group_id`, `content`, `id`.
- "Unread": WhatsApp gives a linked device no reliable "read on the phone" signal through this
  integration. The feed therefore holds **recent messages** and drops a chat's items when the
  user answers in that chat (`whatsapp_message_sent` to the same JID), when the device's "Mark
  all read" is used, or when they are older than 48 hours.

Installed on 2026-11-15 (task P6-1, done in Home Assistant): add-on
"WhatsApp Gateway" 2.1.10 from the repository above, slug `605cee21_whatsapp`. Facts about it: it runs on the **host
network**, its web UI / API listens on **port 8066**, it maps **`/config` read-write**, and its **API key is shown in its
web UI**. It wrote its integration 2.1.10 to `custom_components/whatsapp` itself (`auto_install_integration`). Options set:
`admin_notifications_enabled` false, `admin_numbers` empty, `mark_online` false (so the phone keeps its notifications),
`welcome_message_enabled` false, `sync_full_history` false, webhook off; start on boot on, auto-update off. Still open: restart
Home Assistant to load the integration, the web UI password (`ui_auth_enabled`: the user types it), add the integration, the QR
link by the user, one real captured event (below).

Risks and checks (task P6-1). **These need the owner's attention before installing:**

1. **Terms of service.** The add-on is an unofficial client. Its own docs warn that automated
   use "may violate their Terms of Service and lead to a permanent account ban" and recommend
   a second number. Reading the user's personal messages requires linking the *personal*
   account. PaperOS only reads and never sends through it, which is the lowest-risk use, but
   the risk is not zero and it is the user's account. The owner chose this route on 2026-11-15
   after the risk-free alternative (phone notification sensors, `DECISIONS.md` D8) was offered;
   repeat this warning once at install time.
2. **Phone notifications.** A linked device that reports itself "online" can stop WhatsApp
   from pushing notifications to the phone. After linking, send a test message from another
   account and confirm the phone still notifies. If not, set the add-on's presence to
   unavailable (its API has `POST /set_presence`) or find its "mark online" option.
3. **Never** enable the add-on's auto-responder, bot commands, or "mark as read" features.
4. The link drops if the phone is offline for 14 days or WhatsApp is reinstalled: the feed
   then silently stops. The integration has a connection `binary_sensor`; the inbox app shows
   "WhatsApp link is down" when it is off (fetch it along with the feed).
5. Group messages: the documented event fields do not include the group name. Capture real
   events (HA > Developer tools > Events > listen to `whatsapp_message_received`) for a direct
   and a group message and adjust the template; save redacted samples as fixtures.

### Mail: Proton Mail Bridge + the built-in IMAP integration

Proton has no IMAP except through **Proton Mail Bridge** (needs a paid Proton plan), which
decrypts mail locally and serves IMAP on port 1143 with its own generated password.

1. **Run Bridge where Home Assistant can reach it** (task P6-2). Options, in order:
   a. A Home Assistant add-on that runs Bridge and exposes IMAP. The known community add-on
      (`fboulay/ha-addons-repository`, "ProtonMail Bridge") documents itself as send-only
      (SMTP); check whether its current version or a fork exposes port 1143.
   b. A small **local add-on** in the home-assistant repo wrapping the
      `ghcr.io/belphemur/protonmail-bridge` image (ports 1143/1025, data in `/data`), logged in
      once interactively (`init`, `login`, 2FA, then `info` prints the IMAP password).
   c. Bridge on another always-on machine in the house.
   Do not start b or c without telling the user what will be installed where.
2. **IMAP integration** (core, https://www.home-assistant.io/integrations/imap): server = the
   Bridge host, port 1143, STARTTLS, "verify SSL" off (Bridge uses a self-signed certificate),
   username/password from Bridge, folder INBOX, search `UnSeen UnDeleted`. In its options
   enable "message data: text" and keep push (IDLE) on.
3. Each new mail fires **`imap_content`** with `sender`, `subject`, `date`, `text` (first
   2048 bytes by default), `uid`, `entry_id`, `initial`. The package stores the newest 20.
   When the integration's unread-count sensor drops to 0 (everything read elsewhere) the list
   is emptied.
4. **Full text on demand**: the mail detail screen calls
   `POST /api/services/imap/fetch?return_response` with `{"entry": "<key>", "uid": "<id>"}` and
   shows `service_response.text`. Home Assistant does the MIME decoding. HTML-only mails may
   come back as HTML: the device strips tags (`core/text_clean`, task P6-6). Verify the exact
   request/response shape in task P6-2 and record a fixture.

### "Mark all read" from the device

The package defines `script.paperos_inbox_clear` (field `feed`: `whatsapp` or `mail`) which
fires the `paperos_inbox_clear` event the sensors listen to. The device calls
`POST /api/services/script/paperos_inbox_clear` (a service call works for a non-admin user;
firing events directly does not).

### Installing the package

The Home Assistant config is a repo of its own: **live on push to main**,
with its own rules (its docs: check YAML, push to main, wait for
the sync, answer the user in Dutch there). Install as its own step in that repo: copy
`ha/paperos_inbox.yaml` to `packages/paperos_inbox.yaml`, adjust entity ids, validate, push.
Add-ons and integrations set up in the UI live in `.storage/` and are not in git: record what
was installed in that repo's `docs/HANDOVER.md`.

## Privacy

Message and mail text is held in Home Assistant state (memory and
`.storage/core.restore_state`) and cached on the device's internal flash. The package excludes
both sensors from the recorder so text is not written to the history database. Nothing goes to
a third party beyond what WhatsApp and Proton already hold. Never expose the two sensors under
`www/` (public) or on a dashboard shared with guests. Someone else also uses this Home Assistant:
the feeds contain the user's private messages, so they must not appear on shared dashboards.

## Fallback kept in reserve

Phone notification sensors of the Home Assistant Android app (`DECISIONS.md` D8) can fill the
same two sensors with no add-on and no terms risk (sender + text for WhatsApp, sender + subject
for mail). If the WhatsApp add-on proves unreliable or the account gets a warning, switch the
package's triggers; the device does not change.
