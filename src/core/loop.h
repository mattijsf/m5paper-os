// The main loop's idle step. Os::loopOnce() does steps 1-4 of the loop (input, worker callbacks,
// timers, draw + commit); Loop::iterate() adds step 5: ask the PowerPolicy to idle until the next
// deadline (docs/ARCHITECTURE.md "Main loop").
#pragma once

#include <cstdint>

#include "core/os.h"

namespace pos {

// Step 5. The platform decides how to wait: device = a short delay for now (light sleep: P3),
// simulator = wait for an SDL event. May return early (input, worker result).
class PowerPolicy {
 public:
  virtual ~PowerPolicy() = default;
  virtual void idle(uint32_t maxWaitMs) = 0;
};

class Loop {
 public:
  Loop(Os& os, PowerPolicy& policy) : os_(os), policy_(policy) {}
  // One full pass: steps 1-4, then idle.
  void iterate();

 private:
  Os& os_;
  PowerPolicy& policy_;
};

}  // namespace pos
