// Inbox model (docs/apps/inbox.md, docs/BACKEND.md "Inbox feeds"): the feed contract (built from the phone's decrypted
// notifications, docs/COMPANION.md), grouping of chat messages, relative times, the seen marker and badge count. Pure logic,
// unit-tested.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/companion.h"

namespace pos::inbox {

struct Msg {
  std::string id, from, chat, subject, text, key;
  int64_t ts = 0;  // unix seconds
  bool canReply = false;  // a WhatsApp notification with a reply action on the phone
};

struct Feed {
  std::vector<Msg> items;  // newest first, as the sensor has them
  std::string updated;     // attributes.updated, as sent
};

// The messages of one app ("mail" or "whatsapp") from the phone's notifications, newest first; `key` = the notification key.
// The texts are ready for the panel: the store cleans a notification when it arrives (companion::cleanNotif).
Feed feedFromNotifs(const companion::Notifs& notifs, bool mail);

// Parses a state object in the feed contract (the shape of the Home Assistant sensors before D22; kept for fixtures and tests). Missing fields default to empty, unknown fields are ignored; items without
// text and subject are dropped. False if it is not a state object.
bool parseFeed(const std::string& json, Feed& out);

// One list row of a chat feed: all messages of a chat (`key`), newest first inside.
struct Conversation {
  std::string key, title;  // title: "Anna" or "Anna - Family" for the latest sender in a group
  std::string lastText;
  int64_t ts = 0;          // time of the latest message
  int count = 0;
  bool isNew = false;      // a message newer than the seen marker
  std::vector<int> items;  // indexes into Feed::items, newest first
};
// Groups by `key` (a message without a key is its own conversation), conversations ordered by their latest message.
std::vector<Conversation> groupChats(const std::vector<Msg>& items, int64_t seen);

// "14:02" for today, "Yesterday", the weekday name within the last six days, else "12 Nov"; "" if ts <= 0 or in the far future.
std::string relativeTime(int64_t ts, int64_t now);
// "Mark all read" in the store: drops the notifications of one app whose messages are all at or before `upTo` (the
// newest message the list showed) and returns their keys (for `dismiss` to the phone). A notification with a message
// after `upTo` stays: that message was never on the screen.
std::vector<std::string> clearUpTo(companion::Notifs& notifs, bool mail, int64_t upTo);
// Number of items newer than the seen marker (the launcher badge).
int newCount(const std::vector<Msg>& items, int64_t seen);
int64_t newestTs(const std::vector<Msg>& items);
// The first line of a text, for a list row ("" for an empty text).
std::string firstLine(const std::string& text);
}  // namespace pos::inbox
