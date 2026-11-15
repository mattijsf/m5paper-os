// The main loop's idle step. Os::loopOnce() does steps 1-4 of the loop (input, worker callbacks,
// timers, draw + commit); Loop::iterate() adds step 5: ask the PowerPolicy to idle until the next
// deadline (docs/ARCHITECTURE.md "Main loop").
#pragma once

#include <cstdint>

#include "core/os.h"
#include "core/power_policy.h"

namespace pos {

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
