# Backend: Home Assistant is the hub

The device talks to exactly one server: the user's Home Assistant (HA OS 2026.10, with add-ons
and HACS). Home Assistant does the heavy work; the device only reads small JSON documents.

1. **Home app**: read entity states, call services.
2. **WhatsApp inbox**: a Home Assistant add-on is linked to the user's WhatsApp account and
   fires an event per incoming message; a template sensor keeps the recent ones.
3. **Mail inbox**: Proton Mail Bridge exposes the mailbox over IMAP; Home Assistant's IMAP
   integration fires an event per new mail; a template sensor keeps the recent ones.

The device never speaks WhatsApp or IMAP. It reads two sensors, and sends a WhatsApp message only through one Home Assistant
script with guards (`script.paperos_whatsapp_send`, DECISIONS.md D21, "Sending a WhatsApp message" below). The reasons and the rejected
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

The package is `ha/paperos_inbox.yaml`, a copy of the live `packages/paperos_inbox.yaml` in the Home Assistant config repo (commit de1323d there; the
Home Assistant repo is the source, sync by copying). It defines the **WhatsApp half only** (sensor and `script.paperos_inbox_clear`); the mail sensor comes with P6-2.
**Exception (2026-11-16)**: this repo's copy is ahead of the live one by the send script `script.paperos_whatsapp_send` and its rate-limit sensor (DECISIONS.md D21, "Sending a WhatsApp message" below); they are not installed yet.
Tested on the live Home Assistant 2026.10.1 (2026-11-15): the template rendered 8 cases in Home Assistant's own engine (direct, group with a `{low, high}` timestamp,
media without text, poll ignored, status broadcast ignored, sent removes the chat, clear, first run without attributes), fake events fired through the API
produced the items, a fake sent event removed that chat, the script emptied the feed. **Not tested**: a real WhatsApp message (the integration is not set up
yet: it needs the add-on API token pasted by the user), the script with a non-admin token, the recorder exclusion (needs a Home Assistant restart), the 48 h expiry.
The package has no `connected` attribute, so the inbox's "link is down" notice never shows until one is added.

### WhatsApp: "WhatsApp for Home Assistant" (FaserF)

- Docs: https://faserf.github.io/ha-whatsapp/ . Two parts: an add-on ("App", from the add-on
  repository `https://github.com/FaserF/hassio-addons`) that holds the WhatsApp connection
  using the Baileys library, and the `whatsapp` integration that talks to it.
- Linking: the add-on shows a QR code; scan it in WhatsApp > Linked devices. It then behaves
  like WhatsApp Web: one more linked device on the user's own account.
- Incoming messages fire the event **`whatsapp_message_received`** with these fields (read from the integration and add-on source v2.1.10;
  an earlier version of this page had them wrong): `type` = `"message"` for text **and** media (not `"chat"`), `content`, `caption`,
  `media_type`, `sender_name`, `sender_number`, `from` (the **chat** JID: the person's `...@s.whatsapp.net`, or the group's `...@g.us` in a
  group), `sender` (the person's JID inside a group), `is_group`, `id`, and `raw` (the whole raw message; `raw.messageTimestamp` is a number
  or `{low, high}`). There is **no `timestamp` field and no group name** in the event. The integration drops the user's own messages from the
  received event.
- The user's own replies (from the phone too) fire **`whatsapp_message_sent`** with `to` / `recipient` (the chat JID).
- **Feed item mapping** (`packages/paperos_inbox.yaml`): `key` = `from` (chat JID); `chat` = `"Group"` for groups (the name is not in the event: improving
  this is open), `""` for direct chats; `text` = `content`, else `caption`, else `"[<media_type>]"`; `ts` = `raw.messageTimestamp` (`low` of a mapping),
  else now; `id` = `"<ts>-<sender_number>-<last 6 of the message id>"`; chats ending in `@broadcast` (status) or `@newsletter` are skipped. The device
  never parses `id` (it is only a key) and shows a group message as "Anna - Group".
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

Facts from the linking on 2026-11-15 (task P6-1, done with the owner): **the WhatsApp link is up since 19:51** (the QR code was scanned
in the add-on's own web UI; the session survived an add-on restart: `hasCreds` true, connection open). Things learned:

- The integration's **phone-number pairing rewrites a leading 0 to the country code 49**: enter numbers as `31...`.
- The Home Assistant setup dialog **recreates the socket on every submit**, which voids a QR code or pairing code that was just shown:
  **pair in the add-on's web UI**, not in the dialog.
- The bundled Baileys is **7.0.0-rc14**, with open upstream pairing bugs (WhiskeySockets/Baileys #2600, #2737, #2770).
- **Without `ui_auth_enabled` the add-on treats every private-network address as superadmin**: its web UI, chats API and send API are open
  on port 8066 without a key; and it **broadcasts its API key over mDNS while it is not set up**. Set the UI password.
- Add-on options now: `log_level` info, `mask_sensitive_data` true (besides those above).
- Still open: the UI password (the user types it), the Home Assistant integration is not set up yet, and the question where WhatsApp
  data may live (STATUS.md, open question 1).

Risks and checks (task P6-1). **These need the owner's attention before installing:**

1. **Terms of service.** The add-on is an unofficial client. Its own docs warn that automated
   use "may violate their Terms of Service and lead to a permanent account ban" and recommend
   a second number. Reading the user's personal messages requires linking the *personal*
   account. PaperOS mostly reads; since D21 (2026-11-15) it can also send a message the user typed and confirmed on the device, through
   a rate-limited script (below). The risk is not zero and it is the user's account. The owner chose this route on 2026-11-15
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
5. Group messages: the event has no group name, and **group names stay "Group": a decision, not an open task.** The only source without polling
   is the integration's Chats sensor (`sensor.whatsapp_<number>_chats`, disabled by default), whose `groups` attribute lists every group chat with its `jid`;
   enabling it would put the chat and group list into entity attributes that every Home Assistant login can read, and the owner wants this hardened (D20). The
   sensor stays disabled on purpose. The package template still looks the name up there (`integration_entities('whatsapp')`, `selectattr('jid','eq',chat)`) and falls
   back to "Group": tested in Home Assistant's engine without the sensor ("Group"), so it costs nothing and would work if the decision changes.
   (`whatsapp.search_groups` only posts a persistent notification; `get_contacts` returns a response but is one call per event.)
   `whatsapp_message_received`) for a direct and a group message and save redacted samples as fixtures; look for a way to get the group name.

### Integration set up (2026-11-15, about 20:12-20:14)

- The user pasted the add-on API token into the manual integration form (host `http://localhost:8066`): the `whatsapp` config entry is loaded and
  `binary_sensor.whatsapp_<number>` (connected) is on. It also creates a `notify`, `switch ..._auto_responder` (off), `switch ..._moderation_engine`
  (on by default: switched off, after which the entity disappears by design), `sensor ..._active_moderation_warnings` and an `update`. It offers **57
  services**, including `whatsapp.send_message`, callable by any Home Assistant login. Integration options are at their defaults: `mark_as_read` false,
  `self_messages` false.
- The user restarted Home Assistant at about 20:14: afterwards the integration is loaded and connected and `sensor.paperos_whatsapp_inbox` restored its
  state. **The recorder exclusion is verified**: a state change of the sensor (the clear script) produced no new history row (5 rows before, 5 after, all
  from before the restart).
- The connection `binary_sensor` now exists: the inbox's link-down notice could read it (today it reads an optional `connected` attribute that the package
  does not set).
- **Verified 20:24:31**: a real incoming direct message reached the feed 5 s after it was sent, with exactly the contract keys (`from` = display name, `chat` = "", `key` = `...@s.whatsapp.net`, numeric `ts` from `raw.messageTimestamp`, `id` = `<ts>-<number>-<6 chars>`).
- **Still not verified**: a real group message in the feed, the clear script with a non-admin token, the 48 h expiry, group names.

### Add-on security (accepted risk, 2026-11-15)

- After the user set the UI password (`ui_auth_enabled` true, 20:15), from another machine on the home network and **without login** port 8066
  answers 302/401 for the web UI, `/api/chats`, `/status`, `/stats`, `/logs` and `POST /send_message`.
- **Still open without any login in add-on 2.1.10**: `GET /api/dashboard` (connection state, own number, last received message and sender, recent
  lists), `GET /api/moderation/config`, and `GET` and `POST /api/autoresponder/config`. The POST handler has no auth middleware (source:
  `whatsapp/app/src/routes/api/autoresponder.js`) and sets `enabled` and `message_template` from the body: anyone on the home network could switch on
  auto-replies sent from the user's account. Verified only that an unauthenticated POST with an empty body returns 200 and changes nothing
  (`enabled` stayed false); enabling was not tried. This is a defect of the add-on, not a setting.
- **The owner chose to keep the add-on running** ("Keep running"). **Watch**: check each add-on update for auth on `/api/autoresponder/config` and
  `/api/dashboard`.
- The add-on's API token belongs only in the Home Assistant integration; it must **never** go on the device's SD card (the device needs only `HA_TOKEN` and `HA_URL`).

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

The package defines `script.paperos_inbox_clear` (field `feed`: `whatsapp` or `mail`; optional **`before`**, a unix time) which
fires the `paperos_inbox_clear` event the sensors listen to. **`before`** (2026-11-15): the device sends the newest `ts` its list showed, and the
template drops only items with `ts <= before` (`old | selectattr('ts', 'gt', before)`); without it, or 0, everything is cleared as before. So a message that
arrived after the list was fetched is not lost by "Mark all read" (it was, with a real one). **Installed 2026-11-15 20:43** (Home Assistant repo commit 80e19b0, byte-identical to this repo's copy) and tested in Home Assistant's own template engine:
clear with `before` as a number and as a string keeps the newer item, `before` 0 or missing clears all. After the reload `script.paperos_inbox_clear` has
the fields `feed` and `before`, the feed kept its one real item, and a live call with `before=1` changed the sensor state and dropped nothing. **Not tested live**:
a device "Mark all read" with the new firmware against a feed that holds a newer item (only with fake events when the feed has no real messages), a real group message.
`event_data.before` is rendered with `| int(0)`. The device calls
`POST /api/services/script/paperos_inbox_clear` with `{"feed":"whatsapp","before":<ts>}` (a service call works for a non-admin user;
firing events directly does not).

### Sending a WhatsApp message (DECISIONS.md D21)

The device calls `POST /api/services/script/paperos_whatsapp_send?return_response` with `{"chat":"<JID>","text":"<message>"}` (a service
call: works for a non-admin user like `paperos_inbox_clear`). It never calls `whatsapp.send_message` or the add-on itself. The script
(`ha/paperos_inbox.yaml`) answers `service_response` = `{"ok": true|false, "reason": "...", "message_id": "..."}`.

| Guard (inside the script) | Refusal `reason` |
|---|---|
| text trimmed, 1..1000 characters | `empty`, `too_long` |
| chat is `<digits>@s.whatsapp.net` or `<digits>@g.us` (`<digits>-<digits>@g.us` for old groups): one target, no lists (the integration splits `target` on commas) | `bad_chat` |
| at least 10 s since the last attempt | `too_soon` |
| at most 20 attempts in the last hour | `hourly_limit` |
| `mode: single` (`max_exceeded: silent`): one run at a time | (a second call while one runs gets no response) |
| the integration did not answer `success: true` | `send_failed` |

- **Rate limit, how**: before it sends, the script fires `paperos_whatsapp_send_log`; the trigger-based template sensor
  `sensor.paperos_whatsapp_sends` keeps the times of the last hour in its attribute `times` (no text, no chat; restored after a restart;
  excluded from the recorder). `mode: single` makes "read the list, then add to it" race-free. Every attempt that passes the guards counts,
  a failed one too, so a fault cannot turn into a burst. Chosen because it needs no helper entities and survives restarts; the script's own
  `last_triggered` cannot work (it is set when the current run starts), and a non-admin token cannot fire the event to fake or flush it.
- **Integration fields** (FaserF/ha-whatsapp v2.1.10, `services.yaml` and `__init__.py`): `whatsapp.send_message` takes `target` (a JID or
  number, or a comma list) and `message`, optional `account`, `quote`, `expiration`; it supports a response, `{"success": true,
  "message_id": ...}`, and raises on failure (the script's `continue_on_error` turns that into `send_failed`). The integration also has a
  recipient whitelist option (not set): it could be a second fence.
- **Home Assistant pitfall**: a rendered template that reads as a Python literal becomes that type. The script therefore never keeps the
  text in a variable; the checks re-read the field in one template. A message that is only a number in another spelling ("4.50", "+5")
  goes out as Home Assistant renders it ("4.5", "5"); one that reads as a list or `None` is refused by the integration (`send_failed`).
- **The device** (apps/inbox.md "Sending"): confirmation sheet first, always; it maps the answer to a toast: `Sent to <name>`, the
  refusal reason ("Not sent: wait 10 seconds"), `400`/`404` = "Not sent: no send script in Home Assistant", a timeout = "No answer: check
  WhatsApp first" (it may have gone out). The text stays in the field unless it was sent. The text is never logged on either side.
- Whether the integration fires `whatsapp_message_sent` for a message sent through it (which would drop that chat from the feed, like a
  reply from the phone) is not known.
- **Checked**: the templates against 24 cases in a Jinja harness with stand-ins for Home Assistant's extensions
  (`tests/ha/check-send-script.py`: guards, rate limit, hourly window, native types). **Not checked**: the script in Home Assistant itself
  (not installed: it is installed together with the owner, in the Home Assistant repo, by its rules), the response over REST with a
  non-admin token, what `mode: single` answers to a second call, a real send.

### Installing the package

The Home Assistant config is a repo of its own: **live on push to main**,
with its own rules (its docs: check YAML, push to main, wait for
the sync, answer the user in Dutch there). Install as its own step in that repo: copy
`ha/paperos_inbox.yaml` to `packages/paperos_inbox.yaml`, adjust entity ids, validate, push. (Done for the WhatsApp half on 2026-11-15; from now on the Home Assistant repo's file is the source and this copy is synced from it.)
Add-ons and integrations set up in the UI live in `.storage/` and are not in git: record what
was installed in that repo's `docs/HANDOVER.md`.

## Privacy

Message and mail text is held in Home Assistant state (memory and
`.storage/core.restore_state`) and cached on the device's internal flash. The package excludes
both sensors from the recorder so text is not written to the history database. Nothing goes to
a third party beyond what WhatsApp and Proton already hold. Never expose the two sensors under
`www/` (public) or on a dashboard shared with guests. Someone else also uses this Home Assistant:
the feeds contain the user's private messages, so they must not appear on shared dashboards.
The device's sleep screen, which anyone near it can read, shows the unread count only ("3 unread"), never a sender or text.

## Fallback kept in reserve

Phone notification sensors of the Home Assistant Android app (`DECISIONS.md` D8) can fill the
same two sensors with no add-on and no terms risk (sender + text for WhatsApp, sender + subject
for mail). If the WhatsApp add-on proves unreliable or the account gets a warning, switch the
package's triggers; the device does not change.
