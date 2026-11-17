#include "platform/sim/script.h"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <iterator>
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
    // Not os_.frame4(): in the simulator that is the emulated panel, the same thing the screenshot shows.
    frames_[name] = os_.displayManager().shadowPacked();
    display_.takeCounters();  // expect-* count from here
  } else if (cmd == "tap" || cmd == "back") {
    int x = kBackX, y = kBackY;
    if (cmd == "tap") {
      if (!ints(2)) return false;
      x = n[0];
      y = n[1];
    }
    int hold = 40;  // "tap X Y MS": the finger stays down that long (a real finger: 150-250 ms, so the pressed look shows)
    if (cmd == "tap" && (in >> hold) && (hold < 1 || hold > 450)) {
      error = "'tap X Y MS': 1..450 ms (longer is a long press: use 'long')";
      return false;
    }
    touch(hal::TouchEvent::Down, x, y);
    step(hold);
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
  } else if (cmd == "wifi") {
    std::string state;
    in >> state;
    int crowd = 0;
    if (state == "crowd" && (in >> crowd) && crowd >= 0 && crowd <= 60) {
      net_.setCrowd(crowd);  // that many more networks in range (the scan page's paging and rescans)
    } else if (state == "on" || state == "off") {
      net_.setWifiAvailable(state == "on");
    } else {
      error = "usage: wifi on|off, wifi crowd N (0..60)";
      return false;
    }
  } else if (cmd == "type" || cmd == "type-max") {
    // Taps the keys of the on-screen keyboard that spells the text (switching layers as needed). type-max N:
    // every character key press (until just after it) may push at most N pixels; layer switches repaint all keys.
    long limit = -1;
    if (cmd == "type-max" && !(in >> limit)) {
      error = "'type-max' needs a pixel limit and a text";
      return false;
    }
    std::string text;
    std::getline(in >> std::ws, text);
    auto taps = os_.surface() ? os_.surface()->keyboardTaps(text) : std::vector<KeyTap>{};
    if (taps.empty() && !text.empty()) {
      error = "no on-screen keyboard is showing (or the text has no typeable characters)";
      return false;
    }
    for (size_t i = 0; i < taps.size(); ++i) {
      const uint64_t before = display_.counters().area;
      touch(hal::TouchEvent::Down, taps[i].x, taps[i].y);
      step(40);
      touch(hal::TouchEvent::Up, taps[i].x, taps[i].y);
      step(20);
      const uint64_t used = display_.counters().area - before;
      if (limit >= 0 && !taps[i].layerSwitch && used > static_cast<uint64_t>(limit)) {
        error = "type-max " + std::to_string(limit) + ": key " + std::to_string(i + 1) + " of '" + text + "' pushed " +
                std::to_string(used) + " pixels\n" + display_.logTail(3);
        return false;
      }
    }
  } else if (cmd == "power-off") {
    // What auto-off does before cutting the power: apps save their state, the sleep screen goes on the panel. The script goes on
    // (take a `shot`, then `restart` = the next boot).
    if (os_.surface()) os_.surface()->prepareForPowerOff();
    step(3000);
  } else if (cmd == "restart") {
    if (!restart_) {
      error = "'restart' is not available here";
      return false;
    }
    restart_();
    step(10);
  } else if (cmd == "timer-wake") {
    if (!timerWake_) {
      error = "'timer-wake' is not available here";
      return false;
    }
    error = timerWake_();
    if (!error.empty()) return false;  // no step here: the refresh starts with the next command (a `tap` right after is "touched during it")
  } else if (cmd == "expect-off") {
    int want = -1;
    in >> want;
    const int have = offState_ ? offState_() : -1;
    if (have != want) {
      error = "expect-off " + std::to_string(want) + ": " + (have < 0 ? std::string("the device is on") : "the device is off with a wake alarm in " + std::to_string(have) + " s");
      return false;
    }
  } else if (cmd == "expect-state") {
    std::string want;
    std::getline(in >> std::ws, want);
    step(0);
    const std::string have = os_.surface() ? os_.surface()->debugState() : std::string();
    if (have.find(want) == std::string::npos) {
      error = "expect-state '" + want + "': the screen says '" + have + "'";
      return false;
    }
  } else if (cmd == "expect-shot-equal") {
    std::string a, b;
    in >> a >> b;
    auto slurp = [&](const std::string& name) {
      std::ifstream f(out_ + "/" + name + ".png", std::ios::binary);
      return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    const std::string da = slurp(a), db = slurp(b);
    if (da.empty() || da != db) {
      error = "expect-shot-equal: " + a + " and " + b + (da.empty() || db.empty() ? " (a shot is missing)" : " differ");
      return false;
    }
  } else if (cmd == "expect-frame-equal") {
    std::string a, b;
    in >> a >> b;
    const auto fa = frames_.find(a), fb = frames_.find(b);
    if (fa == frames_.end() || fb == frames_.end()) {
      error = "expect-frame-equal: " + a + " and " + b + " (a shot is missing)";
      return false;
    }
    if (fa->second != fb->second) {
      size_t differ = 0;
      for (size_t i = 0; i < fa->second.size() && i < fb->second.size(); ++i) {
        const uint8_t x = fa->second[i] ^ fb->second[i];
        differ += ((x & 0xF0) ? 1 : 0) + ((x & 0x0F) ? 1 : 0);  // two 4-bit pixels per byte
      }
      error = "expect-frame-equal: " + a + " and " + b + " differ in " + std::to_string(differ) + " pixels";
      return false;
    }
  } else if (cmd == "page-until") {
    // page-until MAX TEXT: tap the right zone until the page's text contains TEXT (the reader's debugState).
    if (!ints(1)) return false;
    std::string want;
    std::getline(in >> std::ws, want);
    const int maxTaps = n[0];
    int idle = 0;
    for (int i = 0; i < maxTaps; ++i) {
      step(0);
      const std::string st = os_.surface() ? os_.surface()->debugState() : std::string();
      if (st.find("page=") == std::string::npos) {
        step(100);
        --i;
        if (++idle > 3000) {
          error = "page-until: the screen never showed pages";
          return false;
        }
        continue;
      }
      if (st.find(want) != std::string::npos) return true;
      touch(hal::TouchEvent::Down, 400, 500);
      step(40);
      touch(hal::TouchEvent::Up, 400, 500);
      step(150);
    }
    error = "page-until: '" + want + "' was not found in " + std::to_string(maxTaps) + " pages";
    return false;
  } else if (cmd == "page-through") {
    if (!ints(1)) return false;
    int x = 400, y = 500, settleMs = 700;
    in >> x >> y >> settleMs;
    const int maxTaps = n[0];
    int last = 0;
    for (int i = 0; i < maxTaps; ++i) {
      step(0);
      const std::string st = os_.surface() ? os_.surface()->debugState() : std::string();
      int cur = 0, total = 0;
      if (std::sscanf(st.c_str(), "page=%d/%d", &cur, &total) != 2) {
        step(100);  // still loading
        --i;
        if (++last > 3000) {
          error = "page-through: the screen never showed pages (state '" + st + "')";
          return false;
        }
        continue;
      }
      last = 0;
      if (cur >= total) {
        std::fprintf(stderr, "page-through: reached page %d/%d after %d turns\n", cur, total, i);
        return true;
      }
      touch(hal::TouchEvent::Down, x, y);
      step(40);
      touch(hal::TouchEvent::Up, x, y);
      step(static_cast<uint32_t>(settleMs));  // the turn (and by default the settle pass)
    }
    error = "page-through: the last page was not reached in " + std::to_string(maxTaps) + " taps (" +
            (os_.surface() ? os_.surface()->debugState() : std::string()) + ")";
    return false;
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
