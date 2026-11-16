# WhatsApp and Mail (inbox apps)

Two launcher tiles, one implementation (`src/apps/inbox/`): a list of recent messages. Home Assistant collects the messages
(WhatsApp add-on, Proton Mail through Bridge + IMAP) and keeps them in two sensors; the device reads those. WhatsApp can also
**send** (DECISIONS.md D21, "Sending" below): a reply in a conversation, or a new message to a favourite chat, through one guarded
Home Assistant script. Mail stays read-only. How the sensors are filled, the send path and the risks are in `BACKEND.md`
("Inbox feeds", "Sending a WhatsApp message").

| Tile | Feed entity | Icon |
|---|---|---|
| WhatsApp | `sensor.paperos_whatsapp_inbox` | message-circle |
| Mail | `sensor.paperos_mail_inbox` | mail |

Both are instances of `InboxApp` parameterised by `{id, name, icon, entity, kind}` (`kind` =
`Chat` or `Mail`), registered twice with `POS_REGISTER_APP`.

## Screens

### List

- Title: "WhatsApp" / "Mail". Action in the TitleBar: refresh icon.
- Rows (`ListItem`, 112 px):
  - Chat: **from** (bodyStrong; group messages "Anna - Family"), two lines of text (body),
    time trailing (caption: "14:02", "Yesterday", "Mon").
  - Mail: **from**, subject (body, one line), first line of text (caption, gray(5)), time.
- New since last visit: a filled dot before the name. 6 rows per page, `Pager`.
- Chat groups all messages of the same chat (`key`) into one row showing the latest text and a
  count badge; tapping opens the conversation. Mail does not group.
- Footer caption: "Updated 14:05". Empty: "No new messages" with the app icon.
- Bottom `Button` (secondary): "Mark all read" > confirmation sheet > calls
  `script.paperos_inbox_clear` with this feed, clears the list.
- Chat only: if the WhatsApp link is down (the integration's connection sensor is off), a
  notice row at the top: "WhatsApp link is down. Re-link in Home Assistant."

### Detail

- Chat: that chat's messages oldest to newest as paragraphs: sender (group chats) and time in
  caption, text in body; paginated with `ui::TextBlock`.
- Mail: from, subject (title), time, then the text. Opens at once with the 1500-character
  excerpt from the feed; meanwhile fetches the full text
  (`POST /api/services/imap/fetch?return_response`, BACKEND.md) and replaces the excerpt when it
  arrives (re-paginate, keep page 1). If the fetch fails the excerpt stays, with a caption
  "Showing the beginning only".

## As built (simulator, 2026-11-15)

- `apps/inbox/inbox_app.cpp` (list, chat and mail detail, mark all read) and `inbox_model` (contract parser, grouping, relative times,
  seen marker; unit-tested), text through `core/text_clean` (P6-6). Registered twice (`whatsapp`, `mail`).
- Rows are one `ListItem` (96 px): name (with a bullet when new, and "(3)" for a chat of three messages), one line of text, time at the
  right; the spec's two text lines are not built. 6 rows per page. `Screen::setAction` puts the refresh icon in the title bar.
- Chat detail: the conversation oldest first as text pages (sender and time in bold, then the text), tap right = next page, left third =
  back; a "Reply" button below (see "Sending"). Mail detail: sender, subject, time, then the excerpt; the full text arrives from `imap.fetch` (`service_response.text`, or one level
  down per entry) and replaces it from page 1; no Wi-Fi or an error leaves the excerpt and says "Showing the beginning only".
- Seen marker: `ctx.store` key `seen` = newest `ts` shown; dots use the marker as it was when the app started, so a list opened a
  second time has none. Badge: `/data/cache/<id>/badge` (the launcher reads it) = items newer than the marker, written after each fetch; right after
  a look it is 0, so it only rises when a feed is fetched without being shown: the scheduled refresh (P7-5, POWER.md) does that while the device is off. Mark all read clears the badge.
- The link-down notice reads an optional `attributes.connected` (false = down) of the WhatsApp feed sensor: **an assumption**, the
  package has to provide it (P6-3).
- Fixtures: `tests/fixtures/make-inbox-fixtures.py` (hosts `ha`, `ha-down`, `ha-empty` .example.test; the three presets under
  `tests/ui/data/inbox*`). Scripts: `inbox`, `inbox-mail`, `inbox-link`, `inbox-empty`, `inbox-error`. The inbox uses the fixed UI fonts, not
  the reader's four text sizes; the chat and mail text pages use the reader's S face (24 px).

## Sending (WhatsApp, D21; simulator 2026-11-16)

- **Reply**: a secondary button at the bottom of a conversation, when its `key` is a chat JID the script accepts (`whatsapp::validChat`).
  The target is the sender (direct chat) or "Group with Anna" (a group: its name is not known, BACKEND.md).
- **New message**: a button next to "Mark all read" on the WhatsApp list when the config has favourites (`inbox.whatsapp.favorites`,
  apps/settings.md "Config schema", at most 8). One favourite opens compose at once, more show a "Send to" list (name, Group / Person).
  Without favourites the list looks as before.
- **Compose** (`apps/inbox/compose.cpp`, title "Message"): "To <name>", a six-line text field (1000 characters, line feeds kept, Enter types
  one), a counter, "Paste" (disabled while the clipboard is empty) and "Send" (disabled while the text is empty or a send runs); the keyboard
  is an overlay. A long press on the field gives Paste / Copy all / Clear (UI.md).
- **Send** always opens a confirmation sheet: "Send to <name>?", the text (six lines at most), "From your own WhatsApp account", Send /
  Cancel. Then `script.paperos_whatsapp_send` (`whatsapp_send.h`: body, answer). Sent: toast "Sent to <name>" and the compose screen closes.
  Refused or failed: a toast with the reason and the text stays. The log says "send: sent" / "not sent (reason)", never the text.
- **API for other apps** (`apps/inbox/compose.h`): `whatsapp::openCompose(ctx, {name, chat}, text)`, `whatsapp::openNewMessage(ctx, text)`,
  `whatsapp::sendToFavorite(ctx, "Family", "Somdoku 12:34")` (a game's "Send score": the favourite by name, else the list). The compose screen
  is pushed onto the calling app's stack; back returns there. Prefilled text is cleaned like the clipboard and cut to 1000 characters.
- Tests: `whatsapp_send_test.cpp` (JIDs, body, favourites, answers), `config_test.cpp` (favourites); UI scripts `whatsapp-send` (list with New
  message, Reply, compose, typing + Paste, confirmation, sent), `whatsapp-send-refused` (favourites list, refusal "too_soon"),
  `whatsapp-send-missing` (one favourite, the script not installed: 400). Fixtures: the send script answers ok on `ha`, `too_soon` on
  `ha-down`, 400 on `ha-empty` (`make-inbox-fixtures.py`; the fixture chat keys are now digit JIDs).

## Behaviour

- `onShow`: show `/data/cache/inbox/<feed>.json` at once, then
  `GET /api/states/<entity>`, parse `attributes.items`, update rows that changed.
- What "unread" means here: Home Assistant keeps recent items and drops them when the user
  deals with them elsewhere (answers the WhatsApp chat; reads all mail so the unread count is
  0) or after 48 h (WhatsApp). On top, the device keeps a "seen" marker per feed
  (`ctx.store`: highest `ts` shown in the list); newer items get the dot. The launcher badge =
  number of items newer than the marker, computed when the feed was last fetched (the launcher
  does no network I/O).
- No polling. Refresh on open, on the refresh button, and when returning from detail if the
  data is older than 60 s.
- Errors: first load failure shows `EmptyState` "Cannot reach Home Assistant" + Retry;
  otherwise a `Toast` and the cached list stays. No token: see `apps/settings.md`.
- Text clean-up (`core/text_clean`, task P6-6): strip HTML tags and decode entities (mail),
  collapse blank lines, replace characters outside the baked font ranges (emoji) with nothing
  or a short text form, shorten links to their host name.

## Feed contract (copy of BACKEND.md)

```json
{ "state": "1763200123",
  "attributes": {
    "updated": "2026-11-15T14:05:00+01:00",
    "items": [
      { "id": "1763200000-31612345678", "from": "Anna", "chat": "Family",
        "text": "Are you coming at six?", "ts": 1763200000, "key": "1203630xxxx@g.us" }
    ] } }
```

Mail items add `subject`; their `id` is the IMAP uid and `key` the IMAP config entry id.
Unknown fields are ignored; missing fields default to empty, so the Home Assistant side can
evolve without breaking the device.

## Tests

- Unit: parsing the state JSON (including missing fields); grouping by `key`; relative time
  formatting; seen marker / badge; text clean-up cases.
- UI: `tests/ui/inbox.script` with fixtures for both feeds (12 items, 2 groups, long text,
  an emoji, an HTML mail, an empty feed, an error, link down). Shots: list page 1 and 2, chat
  detail, mail detail before and after the full text arrives, empty, error.
- The app is built and tested from the contract with fixtures (task P6-4); it does not need the
  Home Assistant side to exist.
