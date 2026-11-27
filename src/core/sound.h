// The sound for new messages (Settings > Display and sound, config phone.sound; docs/apps/inbox.md "Sound"). The one
// sound is hal::chime(): two short notes on the device's buzzer. Whoever reads the phone's relay reports what is new
// (apps/inbox/relay_fetch); this decides whether it is heard. Process-wide and safe from any thread: the relay is read on
// the worker.
#pragma once

#include <cstdint>

namespace pos::sound {

// The setting, as Os::loadConfig() and Settings apply it. Off until then.
void setNewMessages(bool on);
bool newMessages();

// Night: 22:00 up to 07:00 local time. A scheduled refresh (POWER.md) also wakes the device then; nothing sounds.
// A clock that was never set counts as day.
constexpr int kQuietFromHour = 22, kQuietToHour = 7;
bool quiet(int64_t now);

// `fresh` messages arrived that nobody has seen or heard of: one chime if the setting is on and it is not night.
// True if it sounded.
bool arrived(int fresh, int64_t now);
// The chime, whatever the setting and the hour (Settings plays it when the switch goes on; the remote ">set chime 1").
void play();
// How many chimes were played since the start (tests, the simulator's `expect-chimes`).
int chimes();

}  // namespace pos::sound
