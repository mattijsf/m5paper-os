// UI script runner (docs/SIMULATOR.md "UI scripts"). Plain text, one command per line:
//   wait MS | shot NAME | tap X Y | long X Y | swipe X1 Y1 X2 Y2 | back
//   expect-no-quality | expect-max-pushed N        (checked against pushes since the last shot)
//   restart                                         save and close the app, build the surface again (same data)
//   expect-state TEXT                               the top screen's debugState() contains TEXT (reader: "page=3/33")
//   expect-shot-equal A B                           the two screenshots (shot A, shot B) are byte-identical
//   page-through MAX [TAP_X TAP_Y]                   taps (default 400 500) until debugState says "page=N/N" (the last page),
//                                                    at most MAX taps; fails if the end is not reached; prints the page count
// `#` starts a comment. Time only moves through `wait` (and the short steps inside gestures)
// when the clock is fake.
#pragma once

#include <functional>
#include <string>

#include "core/os.h"
#include "platform/sim/sim.h"

namespace pos::sim {

class ScriptRunner {
 public:
  // `idle` is called after every simulated step (the window presents a frame; headless: nothing).
  ScriptRunner(Os& os, Display& display, Input& input, Clock& clock, Net& net, std::string outDir, std::function<void()> idle)
      : os_(os), display_(display), input_(input), clock_(clock), net_(net), out_(std::move(outDir)), idle_(std::move(idle)) {}

  // Runs the file. Returns 0 on success; prints "<file>:<line>: <reason>" to stderr and returns
  // non-zero on the first failing command.
  int run(const std::string& path);
  // What `restart` does: save and close what is open, then build the Surface again (a boot with the same data).
  void setRestart(std::function<void()> fn) { restart_ = std::move(fn); }
  // What `page-through` and `expect-state` read: the Surface's debugState().

 private:
  void step(uint32_t ms);  // advance time in 10 ms slices, running the main loop each slice
  void touch(hal::TouchEvent::Type t, int x, int y);
  bool command(const std::string& cmd, const std::string& args, std::string& error);

  Os& os_;
  Display& display_;
  Input& input_;
  Clock& clock_;
  Net& net_;
  std::string out_;
  std::function<void()> idle_;
  std::function<void()> restart_;
};

}  // namespace pos::sim
