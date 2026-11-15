#include <ctime>

#include "platform/sim/sim.h"

namespace pos::sim {

uint32_t Clock::millis() {
  if (fake_) return fakeMs_.load();
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start_).count());
}

int64_t Clock::now() {
  if (rtcUnset_) return 0;
  if (fake_) return fakeStartUnix_ + fakeMs_.load() / 1000;  // fixed start so screenshots do not depend on the day
  return static_cast<int64_t>(std::time(nullptr));
}

}  // namespace pos::sim
