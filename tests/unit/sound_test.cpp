// core/sound: the setting, the night, and the count the simulator's scripts check.
#include <doctest.h>

#include "core/sound.h"
#include "core/timefmt.h"

using namespace pos;

TEST_CASE("sound: new messages chime once per read, only with the setting on and not at night") {
  applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  const int64_t noon = 1768474800;      // 2026-01-15 12:00 in Amsterdam
  const int64_t evening = noon + 9 * 3600 + 59 * 60;  // 21:59
  const int64_t night = noon + 10 * 3600;             // 22:00
  const int64_t early = noon + 18 * 3600 + 59 * 60;   // 06:59 the next morning
  const int64_t morning = noon + 19 * 3600;           // 07:00
  CHECK_FALSE(sound::quiet(noon));
  CHECK_FALSE(sound::quiet(evening));
  CHECK(sound::quiet(night));
  CHECK(sound::quiet(early));
  CHECK_FALSE(sound::quiet(morning));
  CHECK_FALSE(sound::quiet(0));  // a clock that was never set: day

  const int before = sound::chimes();
  sound::setNewMessages(false);
  CHECK_FALSE(sound::arrived(3, noon));  // off (the default)
  CHECK(sound::chimes() == before);
  sound::setNewMessages(true);
  CHECK(sound::newMessages());
  CHECK_FALSE(sound::arrived(0, noon));  // nothing new
  CHECK(sound::arrived(3, noon));        // three messages, one chime
  CHECK(sound::chimes() == before + 1);
  CHECK_FALSE(sound::arrived(1, night));
  CHECK(sound::chimes() == before + 1);
  sound::play();  // Settings, when the switch goes on: also at night
  CHECK(sound::chimes() == before + 2);
  sound::setNewMessages(false);
}
