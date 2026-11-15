#include "platform/sim/script.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

namespace pos::sim {

namespace {
// Centre of the back arrow's 72x72 target (docs/UI.md), below the status bar.
constexpr int kBackX = 36, kBackY = 92;
}  // namespace

void ScriptRunner::step(uint32_t ms) {
  for (uint32_t done = 0; done < ms;) {
    uint32_t slice = std::min<uint32_t>(10, ms - done);
    if (clock_.fake()) clock_.advance(slice);
    else SDL_Delay(slice);
    input_.pump();
    // The worker thread runs in real time while the fake clock jumps: give a busy worker a moment
    // to see the new time, so results arrive at a repeatable simulated time.
    if (os_.worker().pending() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(3));
    os_.loopOnce();
    if (idle_) idle_();
    done += slice;
  }
}

void ScriptRunner::touch(hal::TouchEvent::Type t, int x, int y) {
  hal::TouchEvent ev;
  ev.type = t;
  ev.x = static_cast<int16_t>(x);
  ev.y = static_cast<int16_t>(y);
  ev.ms = clock_.millis();
  input_.inject(ev);
}

bool ScriptRunner::command(const std::string& cmd, const std::string& args, std::string& error) {
  std::istringstream in(args);
  int n[4];
  auto ints = [&](int count) {
    for (int i = 0; i < count; ++i)
      if (!(in >> n[i])) {
        error = "'" + cmd + "' needs " + std::to_string(count) + " numbers";
        return false;
      }
    return true;
  };
  if (cmd == "wait") {
    if (!ints(1)) return false;
    step(static_cast<uint32_t>(n[0]));
  } else if (cmd == "shot") {
    std::string name;
    in >> name;
    if (name.empty()) {
      error = "'shot' needs a name";
      return false;
    }
    step(0);
    if (!display_.saveShot(out_ + "/" + name + ".png")) {
      error = "cannot write " + out_ + "/" + name + ".png";
      return false;
    }
    display_.takeCounters();  // expect-* count from here
  } else if (cmd == "tap" || cmd == "back") {
    int x = kBackX, y = kBackY;
    if (cmd == "tap") {
      if (!ints(2)) return false;
      x = n[0];
      y = n[1];
    }
    touch(hal::TouchEvent::Down, x, y);
    step(40);
    touch(hal::TouchEvent::Up, x, y);
    step(10);
  } else if (cmd == "long") {
    if (!ints(2)) return false;
    touch(hal::TouchEvent::Down, n[0], n[1]);
    step(800);
    touch(hal::TouchEvent::Up, n[0], n[1]);
    step(10);
  } else if (cmd == "swipe") {
    if (!ints(4)) return false;
    touch(hal::TouchEvent::Down, n[0], n[1]);
    step(20);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
      touch(hal::TouchEvent::Move, n[0] + (n[2] - n[0]) * i / steps, n[1] + (n[3] - n[1]) * i / steps);
      step(20);
    }
    touch(hal::TouchEvent::Up, n[2], n[3]);
    step(10);
  } else if (cmd == "expect-no-quality") {
    step(0);
    if (display_.counters().quality > 0) {
      error = "expect-no-quality: " + std::to_string(display_.counters().quality) + " Quality push(es) since the last shot\n" +
              display_.logTail(5);
      return false;
    }
  } else if (cmd == "expect-max-pushed") {
    if (!ints(1)) return false;
    step(0);
    if (display_.counters().area > static_cast<uint64_t>(n[0])) {
      error = "expect-max-pushed " + std::to_string(n[0]) + ": " + std::to_string(display_.counters().area) +
              " pixels pushed since the last shot";
      return false;
    }
  } else {
    error = "unknown command '" + cmd + "'";
    return false;
  }
  return true;
}

int ScriptRunner::run(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "cannot open script %s\n", path.c_str());
    return 2;
  }
  std::string line;
  int lineNo = 0;
  while (std::getline(f, line)) {
    ++lineNo;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream in(line);
    std::string cmd;
    if (!(in >> cmd)) continue;
    std::string rest;
    std::getline(in, rest);
    std::string error;
    if (!command(cmd, rest, error)) {
      std::fprintf(stderr, "%s:%d: %s\n", path.c_str(), lineNo, error.c_str());
      return 1;
    }
  }
  return 0;
}

}  // namespace pos::sim
