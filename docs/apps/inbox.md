# WhatsApp and Mail (inbox apps)

Two launcher tiles, one implementation (`src/apps/inbox/`): a read-only list of recent, still
unread messages. No replying. Data comes from Home Assistant feeds; how they are filled is in
`BACKEND.md` ("Inbox feeds").

| Tile | Feed entity | Icon |
|---|---|---|
| WhatsApp | `sensor.paperos_whatsapp_inbox` | message-circle |
| Mail | `sensor.paperos_mail_inbox` | mail |

Both are instances of `InboxApp` parameterised by `{id, name, icon, entity, groupByChat}`,
registered twice with `POS_REGISTER_APP`.

## Screens

### List

- Title: "WhatsApp" / "Mail". Action in the TitleBar: refresh icon.
- Rows (`ListItem`, 112 px): **from** (bodyStrong; for group messages "Anna - Family"),
  first two lines of text (body, gray(5) once seen), time trailing (caption: "14:02",
  "Yesterday", "Mon").
- New since last visit: a filled dot before the name. 6 rows per page, `Pager`.
- WhatsApp groups consecutive messages of the same chat into one row with a count badge
  (`groupByChat`); tapping opens the conversation detail. Mail does not group.
- Footer caption: "Updated 14:05". Empty: "No unread messages" with the app icon.
- Bottom `Button` (secondary): "Mark all read" > confirmation sheet > fires the
  `paperos_inbox_clear` event for this feed, clears the list.

### Detail

- WhatsApp: the chat's messages oldest to newest as paragraphs: time (caption) + text (body),
  paginated with `ui::TextBlock`.
- Mail: from, time, then the text (subject and snippet; full body only with the Phase 8 gateway).

## Behaviour

- `onShow`: show `/data/cache/inbox/<feed>.json` at once, then
  `GET /api/states/<entity>`, parse `attributes.items`, update rows that changed.
- "Seen" marker: `ctx.store` keeps the highest item id shown in the list; items above it get the
  dot. The launcher badge = number of items with id greater than the marker, computed when the
  feed was last fetched (no network from the launcher).
- No polling. Refresh on open, on the refresh button, and when returning from detail if the
  data is older than 60 s.
- Errors: first load failure shows `EmptyState` "Cannot reach Home Assistant" + Retry;
  otherwise a `Toast` and the cached list stays.
- Text: characters outside the baked font ranges (emoji) draw as `?` in v1. Task P6-4 replaces
  the most common emoji with short text (":)" etc.) or drops them.

## Feed contract (copy of BACKEND.md)

```json
{ "state": "1763200123",
  "attributes": {
    "updated": "2026-11-15T14:05:00+01:00",
    "items": [ { "id": "1763200000123", "from": "Anna", "chat": "Family", "text": "Are you coming at six?", "ts": 1763200000 } ]
  } }
```

`items` newest first, at most 25, `text` at most 500 characters.

## Tests

- Unit: parsing the state JSON; grouping by chat; relative time formatting; seen marker / badge.
- UI: `tests/ui/inbox.script` with fixtures for both feeds (12 items, 2 groups, long text,
  an emoji, an empty feed, an error). Shots: list page 1 and 2, detail, empty, error.
