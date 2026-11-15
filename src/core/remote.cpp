#include "core/remote.h"

#include <cstdio>
#include <cstring>
#include <sstream>

#include "core/log.h"

namespace pos {

RemoteCommand parseRemoteCommand(const std::string& raw) {
  RemoteCommand c;
  std::string line = raw;
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
  if (!line.empty() && line[0] == '>') line.erase(0, 1);
  std::istringstream in(line);
  std::string word;
  in >> word;
  int n[4];
  auto ints = [&](int count) {
    for (int i = 0; i < count; ++i) {
      if (!(in >> n[i])) return false;
    }
    std::string extra;
    return !(in >> extra);
  };
  auto bad = [&](const char* why) {
    c.kind = RemoteCommand::None;
    c.error = why;
    return c;
  };
  if (word == "shot" || word == "log" || word == "refresh" || word == "reboot") {
    std::string extra;
    if (in >> extra) return bad("no arguments expected");
    c.kind = word == "shot" ? RemoteCommand::Shot
             : word == "log" ? RemoteCommand::Log
             : word == "refresh" ? RemoteCommand::Refresh
                                 : RemoteCommand::Reboot;
  } else if (word == "tap" || word == "long") {
    if (!ints(2)) return bad("usage: tap|long x y");
    c.kind = word == "tap" ? RemoteCommand::Tap : RemoteCommand::Long;
    c.x1 = n[0];
    c.y1 = n[1];
  } else if (word == "set") {
    if (!(in >> c.key >> c.value)) return bad("usage: set key value");
    c.kind = RemoteCommand::Set;
  } else if (word == "swipe") {
    if (!ints(4)) return bad("usage: swipe x1 y1 x2 y2");
    c.kind = RemoteCommand::Swipe;
    c.x1 = n[0];
    c.y1 = n[1];
    c.x2 = n[2];
    c.y2 = n[3];
  } else {
    return bad("unknown command");
  }
  return c;
}

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

void Remote::say(const std::string& s) {
  std::string line = s + "\n";
  host_.reply(line.data(), line.size());
}

void Remote::queue(uint32_t at, hal::TouchEvent::Type t, int x, int y) {
  Timed e;
  e.at = at;
  e.ev.type = t;
  e.ev.x = static_cast<int16_t>(x);
  e.ev.y = static_cast<int16_t>(y);
  timed_.push_back(e);
}

void Remote::handleLine(const std::string& line) {
  if (line.empty() || line[0] != '>') return;
  RemoteCommand c = parseRemoteCommand(line);
  const uint32_t now = clock_.millis();
  switch (c.kind) {
    case RemoteCommand::None:
      say("#err " + c.error);
      return;
    case RemoteCommand::Tap:
      queue(now, hal::TouchEvent::Down, c.x1, c.y1);
      queue(now + 60, hal::TouchEvent::Up, c.x1, c.y1);
      break;
    case RemoteCommand::Long:
      queue(now, hal::TouchEvent::Down, c.x1, c.y1);
      queue(now + 800, hal::TouchEvent::Up, c.x1, c.y1);
      break;
    case RemoteCommand::Swipe: {
      const int steps = 8;
      queue(now, hal::TouchEvent::Down, c.x1, c.y1);
      for (int i = 1; i <= steps; ++i)
        queue(now + 20 * i, hal::TouchEvent::Move, c.x1 + (c.x2 - c.x1) * i / steps, c.y1 + (c.y2 - c.y1) * i / steps);
      queue(now + 20 * (steps + 1), hal::TouchEvent::Up, c.x2, c.y2);
      break;
    }
    case RemoteCommand::Log: {
      std::string text = logTail(50);
      say("#log " + std::to_string(text.size()));
      host_.reply(text.data(), text.size());
      break;
    }
    case RemoteCommand::Refresh:
    {
      std::string text = os_.displayManager().refreshLogTail(50);
      say("#refresh " + std::to_string(text.size()));
      host_.reply(text.data(), text.size());
      break;
    }
    case RemoteCommand::Shot:
      shotPending_ = true;  // runs in tick(), after earlier touches were handled; replies then
      settle_ = 0;
      return;
    case RemoteCommand::Set:
      if (!host_.option(c.key, c.value)) {
        say("#err unknown option");
        return;
      }
      break;
    case RemoteCommand::Reboot:
      say("#ok");
      host_.reboot();
      return;
  }
  say("#ok");
}

void Remote::tick() {
  const uint32_t now = clock_.millis();
  for (size_t i = 0; i < timed_.size();) {
    if (static_cast<int32_t>(now - timed_[i].at) >= 0) {
      timed_[i].ev.ms = now;
      host_.inject(timed_[i].ev);
      timed_.erase(timed_.begin() + i);
    } else {
      ++i;
    }
  }
  if (shotPending_) {
    // Wait until all timed touches were injected, then one more pass so Os has drawn them.
    if (!timed_.empty()) settle_ = 0;
    else if (++settle_ >= 2) {
      shotPending_ = false;
      doShot();
    }
  }
}

void Remote::doShot() {
  std::vector<uint8_t> px = os_.frame4();
  char head[96];
  std::snprintf(head, sizeof head, "#shot %d %d %u %08x\n", kScreenW, kScreenH, static_cast<unsigned>(px.size()),
                static_cast<unsigned>(crc32(px.data(), px.size())));
  host_.reply(head, std::strlen(head));
  host_.reply(px.data(), px.size());
  say("#ok");
}

}  // namespace pos
