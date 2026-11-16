# WhatsApp and Mail (inbox apps)

Two launcher tiles, one implementation (`src/apps/inbox/`): the WhatsApp and Proton Mail notifications of the user's phone.
**Since D22 (2026-11-16)** they come from the companion Android app, which encrypts each notification for the Paper; Home
Assistant only relays ciphertext (`sensor.paperos_relay_to_paper`), the Paper decrypts it. The protocol, the relay and the
limits are in `docs/COMPANION.md`; the decision and its costs in `DECISIONS.md` D22. WhatsApp can **reply** to a chat whose
notification is still on the phone; there are no new chats. Mail is read-only and shows what the notification showed
(sender, subject, maybe a preview), never the full mail.

| Tile | Source | Icon |
|---|---|---|
| WhatsApp | the relay's `post` envelopes with `app` = `whatsapp` | message-circle |
| Mail | the same queue, `app` = `mail` | mail |

Both are instances of `InboxApp` parameterised by `{id, name, icon, entity, kind}` (`kind` = `Chat` or `Mail`), registered twice
with `POS_REGISTER_APP`.

## Screens

### List

- Title: "WhatsApp" / "Mail". Action in the title bar: refresh.
- One row per phone notification (`ListItem`, 96 px), 6 per page with a `Pager`:
  - WhatsApp: **name** (the chat title for a group, "Anna - Family"; the sender for a direct chat), with "(3)" for a chat of three
    messages and a bullet when new; the latest message; the time at the right.
  - Mail: **sender**, the subject, the time.
- Footer caption: "Updated 14:05". Empty: "No new messages" with the app icon.
- Bottom button "Mark all read" > confirmation sheet ("The list is cleared here. The notifications stay on your phone.") > clears
  this app's notifications on the Paper and sends `dismiss` with their keys to the phone (best effort).
- **No phone paired** (no `/data/companion.json`): `EmptyState` "No phone paired" / "Messages come from the PaperOS app on your
  phone. Pair it in Settings > Home Assistant > Phone." Nothing is requested.
- **Not set up yet** (Home Assistant answers 404 for the relay): `EmptyState` "Not set up yet" / "The relay for your phone's
  messages is missing in Home Assistant."; the badge goes to 0.
- **Cannot reach Home Assistant** (first load fails, nothing saved): `EmptyState` with Retry. With saved data: a toast and the
  saved list stays ("Saved data").
- The phone's answer to a reply (`result`) shows as a toast when the list fetches it: "Reply to Family sent" / "Reply to Family not
  sent: wait 10 seconds".

### Detail

- Chat: the notification's messages (at most 6, what MessagingStyle gave), oldest first, as paragraphs: sender (groups) and time,
  then the text; paginated with `ui::TextBlock` (tap right = next page, left third = back). A "Reply" button when the notification
  has a reply action.
- Mail: sender, subject (title), time, the caption "The preview your phone showed", then the preview ("(no preview)" if the
  notification had none).

## Replying (WhatsApp)

- **Reply** opens the reply screen (`apps/inbox/compose.cpp`, title "Reply"): "To <name>", a six-line text field (900 characters,
  line feeds kept), a counter, "Paste" and "Send"; the keyboard is an overlay; a long press on the field gives Paste / Copy all /
  Clear (UI.md).
- **Send** always opens a confirmation sheet: "Send to <name>?", the text (seven lines at most), "Your phone sends it from your
  WhatsApp account", Send / Cancel. Then the Paper seals a `reply` envelope (counter stored first, COMPANION.md 5) and pushes it to
  `script.paperos_relay_push`. Pushed: toast "Sent to your phone" and the screen closes; the phone's `result` arrives later. Not
  pushed: "Not sent: no relay in Home Assistant" (400/404), "No answer: it may still reach your phone" (timeout), or the network
  error; the text stays. The log says "reply: pushed" / "not pushed", never the text.
- **The guards are on the phone** (length, 10 s, 20 per hour, the notification must still exist): COMPANION.md 7.2.
- **API for other apps** (`apps/inbox/compose.h`): `whatsapp::openCompose(ctx, {name, key}, text)` and
  `whatsapp::sendToFavorite(ctx, "Woordle challenge", "Somdoku 12:34")`: a reply to the newest live WhatsApp notification whose chat
  name is that name; none: the toast "No WhatsApp notification from <name> on your phone" (or "No phone paired: see Settings > Home
  Assistant"). The config's `inbox.whatsapp.favorites` is no longer used.

## Behaviour

- On open (and on refresh, and back from a detail older than 60 s): the saved notifications (`/data/cache/inbox/notifs.json`) show
  at once; then the worker reads `sensor.paperos_relay_to_paper`, applies the envelopes in counter order (`post` replaces a key,
  `remove` and `sync` drop keys, `result` becomes a toast, `unpair` forgets the pairing), saves, and acks up to the last counter
  (`apps/inbox/relay_fetch.cpp`, `core/companion.cpp`). Rejected envelopes are counted in the log, never shown.
- Notifications older than 7 days are dropped on the Paper.
- Seen marker: `ctx.store` key `seen` = newest `ts` shown; dots use the marker as it was when the app started. Badge:
  `/data/cache/<id>/badge` = items newer than the marker; the scheduled refresh (P7-5, POWER.md) reads the relay too and writes both
  badges.
- No polling. Text through `core/text_clean` (emoji, links, stray HTML).

## Feed contract

The decrypted notifications are turned into the feed items the list, grouping and badge code always used (`inbox::feedFromNotifs`,
BACKEND.md "Inbox feeds"): `id` = key + "#" + index, `from`, `chat`, `subject` (mail), `text`, `ts`, `key` = the notification key,
`canReply`.

## Tests

- Unit: `companion_crypto_test.cpp` (RFC 7748 and the shared vectors of `tests/fixtures/companion/vectors.json`, rejections),
  `companion_test.cpp` (queue order, replay, forgeries, results, unpair, expiry, envelopes to the phone, pairing, the store),
  `inbox_test.cpp` (grouping, times, `feedFromNotifs`), `whatsapp_send_test.cpp` (targets, names, push outcome).
- UI: `inbox`, `inbox-mail`, `inbox-empty`, `inbox-error`, `inbox-wifi`, `inbox-unpaired`, `inbox-gone`, `inbox-gone-mail`,
  `whatsapp-send`, `whatsapp-send-missing`, `whatsapp-reply-result`, `phone-pair` (Settings), `somdoku` (the score as a reply).
  Fixtures: `tests/fixtures/make-inbox-fixtures.py` encrypts fake notifications with pyca/cryptography for hosts `ha`, `ha-down`,
  `ha-empty`, `ha-gone` (.example.test) and writes the presets' paired state (`companion.json`). UI scripts run with
  `setTestRandom(1)`, so the pairing QR code and its hello are fixed.
