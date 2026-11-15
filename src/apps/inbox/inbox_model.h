// Inbox model (docs/apps/inbox.md, docs/BACKEND.md "Inbox feeds"): the feed contract, grouping of chat messages, relative times, the
// seen marker and badge count. Pure logic, unit-tested.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pos::inbox {

struct Msg {
  std::string id, from, chat, subject, text, key;
  int64_t ts = 0;  // unix seconds
};

struct Feed {
  std::vector<Msg> items;  // newest first, as the sensor has them
  bool linkDown = false;   // attributes.connected == false (optional: the package may add it)
  std::string updated;     // attributes.updated, as sent
};

// Parses the body of GET /api/states/<feed sensor>. Missing fields default to empty, unknown fields are ignored; items without
// text and subject are dropped. False if it is not a state object.
bool parseFeed(const std::string& json, Feed& out);

// Every text of the feed passed through core/text_clean (chat text is plain, mail text may be HTML).
void cleanFeed(Feed& feed, bool mail);

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
// Number of items newer than the seen marker (the launcher badge).
int newCount(const std::vector<Msg>& items, int64_t seen);
int64_t newestTs(const std::vector<Msg>& items);
// The first line of a text, for a list row ("" for an empty text).
std::string firstLine(const std::string& text);
// The body of the call that clears a feed: {"feed":"whatsapp","before":<newest ts the list showed>}. The feed then keeps messages that arrived
// after the list was fetched (script.paperos_inbox_clear `before`); without a time (<= 0) the field is left out and everything is cleared.
std::string clearBody(const std::string& feed, int64_t before);
// The text of a service_response of imap.fetch ("service_response":{"text":...}); "" if there is none.
std::string fetchedText(const std::string& json);

}  // namespace pos::inbox
