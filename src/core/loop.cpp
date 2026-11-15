#include "core/loop.h"

namespace pos {

void Loop::iterate() {
  os_.loopOnce();
  policy_.idle(os_.idleWaitMs());
}

}  // namespace pos
