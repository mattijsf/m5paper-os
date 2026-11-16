#include "core/remote.h"

#include <algorithm>

#include <cstdio>
#include <cstdlib>
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
  if (word == "state") {
    std::string extra;
    if (in >> extra) return bad("no arguments expected");
    c.kind = RemoteCommand::State;
  } else if (word == "shot" || word == "log" || word == "refresh" || word == "reboot" || word == "status") {
    std::string extra;
    if (in >> extra) return bad("no arguments expected");
    c.kind = word == "shot" ? RemoteCommand::Shot
             : word == "log" ? RemoteCommand::Log
             : word == "refresh" ? RemoteCommand::Refresh
                                 : word == "status" ? RemoteCommand::Status : RemoteCommand::Reboot;
  } else if (word == "poweroff") {
    // "poweroff N" or "poweroff N M": M = after the scheduled refresh of that wake, off once more for M seconds (never until the button)
    if (!(in >> n[0]) || n[0] < 1 || n[0] > 3600) return bad("usage: poweroff SECONDS [THEN_SECONDS] (1..3600)");
    n[1] = 0;
    if (in >> n[1]) {
      std::string extra;
      if (n[1] < 1 || n[1] > 3600 || (in >> extra)) return bad("usage: poweroff SECONDS [THEN_SECONDS] (1..3600)");
    } else if (!in.eof()) {
      return bad("usage: poweroff SECONDS [THEN_SECONDS] (1..3600)");
    }
    c.kind = RemoteCommand::PowerOff;
    c.x1 = n[0];
    c.y1 = n[1];
  } else if (word == "tap" || word == "long") {
    if (!ints(2)) return bad("usage: tap|long x y");
    c.kind = word == "tap" ? RemoteCommand::Tap : RemoteCommand::Long;
    c.x1 = n[0];
    c.y1 = n[1];
  } else if (word == "put") {
    // path may contain spaces: everything before the last two tokens
    std::string rest;
    std::getline(in >> std::ws, rest);
    size_t sp2 = rest.rfind(' ');
    size_t sp1 = sp2 == std::string::npos ? std::string::npos : rest.rfind(' ', sp2 - 1);
    if (sp1 == std::string::npos || sp1 == 0) return bad("usage: put <path> <size> <crc32 hex>");
    c.path = rest.substr(0, sp1);
    char* e1 = nullptr;
    char* e2 = nullptr;
    c.size = std::strtoull(rest.c_str() + sp1 + 1, &e1, 10);
    c.crc = static_cast<uint32_t>(std::strtoul(rest.c_str() + sp2 + 1, &e2, 16));
    if (*e1 != ' ' || *e2 != '\0') return bad("usage: put <path> <size> <crc32 hex>");
    c.kind = RemoteCommand::Put;
  } else if (word == "cat") {
    std::string rest;
    std::getline(in >> std::ws, rest);
    if (rest.empty()) return bad("usage: cat <path>");
    c.kind = RemoteCommand::Cat;
    c.path = rest;
  } else if (word == "ls" || word == "rm") {
    std::string rest;
    std::getline(in >> std::ws, rest);
    if (rest.empty()) return bad("usage: ls|rm <path>");
    c.path = rest;
    c.kind = word == "ls" ? RemoteCommand::Ls : RemoteCommand::Rm;
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

uint32_t crc32Update(uint32_t prev, const uint8_t* data, size_t len) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    init = true;
  }
  uint32_t crc = ~prev;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

uint32_t crc32(const uint8_t* data, size_t len) { return crc32Update(0, data, len); }

bool remotePathAllowed(const std::string& p) {
  if (p.find("..") != std::string::npos || p.find('\\') != std::string::npos) return false;
  const bool ok = p.rfind("/sd/books/", 0) == 0 || p == "/sd/books" || p.rfind("/sd/paperos/", 0) == 0 || p == "/sd/paperos";
  if (!ok) return false;
  const size_t slash = p.rfind('/');
  return p.substr(slash + 1) != "paperos_config";
}

void Remote::say(const std::string& s) {
  std::string line = s + "\n";
  send(line.data(), line.size());
}

void Remote::send(const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  out_.insert(out_.end(), p, p + n);
  pump();
}

void Remote::pump() {
  while (outPos_ < out_.size()) {
    const size_t room = host_.writable();
    if (room == 0) return;
    const size_t n = std::min(room, out_.size() - outPos_);
    host_.reply(out_.data() + outPos_, n);
    outPos_ += n;
  }
  out_.clear();
  out_.shrink_to_fit();  // the 259 KB of a frame dump do not stay allocated
  outPos_ = 0;
}

void Remote::flushAll() {
  if (outPos_ < out_.size()) host_.reply(out_.data() + outPos_, out_.size() - outPos_);
  out_.clear();
  out_.shrink_to_fit();
  outPos_ = 0;
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
      send(text.data(), text.size());
      break;
    }
    case RemoteCommand::State: {
      // What the top screen says about itself (Screen::debugState): UI scripts on the device use it like the simulator's do.
      const std::string text = os_.surface() ? os_.surface()->debugState() : std::string();
      say("#state " + std::to_string(text.size()));
      send(text.data(), text.size());
      break;
    }
    case RemoteCommand::Refresh:
    {
      std::string text = os_.displayManager().refreshLogTail(50);
      say("#refresh " + std::to_string(text.size()));
      send(text.data(), text.size());
      break;
    }
    case RemoteCommand::Put: {
      if (put_.active) abortPut("new upload started");
      if (!remotePathAllowed(c.path) || c.path.back() == '/') {
        say("#err path not allowed");
        return;
      }
      if (c.size > 64ull * 1024 * 1024) {
        say("#err too large");
        return;
      }
      put_.path = c.path;
      put_.partPath = c.path + ".part";
      if (!put_.file.open(put_.partPath)) {
        say("#err cannot open file");
        return;
      }
      put_.size = c.size;
      put_.got = 0;
      put_.crc = 0;
      put_.expect = c.crc;
      put_.active = true;
      put_.lastByteMs = now;
      say("#put go");
      if (c.size == 0) feedRaw(nullptr, 0);
      return;
    }
    case RemoteCommand::Ls: {
      if (!remotePathAllowed(c.path)) {
        say("#err path not allowed");
        return;
      }
      std::vector<fs::DirEntry> list;
      if (!fs::listDir(c.path, list)) {
        say("#err no such folder");
        return;
      }
      std::string text;
      for (const auto& e : list) text += e.name + "\t" + (e.isDir ? std::string("-") : std::to_string(e.size)) + "\n";
      say("#ls " + std::to_string(text.size()));
      send(text.data(), text.size());
      break;
    }
    case RemoteCommand::Cat: {
      // Only the power log (docs/POWER.md "Measuring"): the card's secrets are not here, but nothing else needs reading either.
      if (c.path != "/data/power.csv" && c.path != "/data/power.old.csv" && c.path != "/data/refresh.log") {
        say("#err path not allowed");
        return;
      }
      std::string text;
      if (!fs::readFile(c.path, text)) {
        say("#err no such file");
        return;
      }
      say("#cat " + std::to_string(text.size()));
      send(text.data(), text.size());
      break;
    }
    case RemoteCommand::Rm:
      if (!remotePathAllowed(c.path) || c.path == "/sd/books" || c.path == "/sd/paperos") {
        say("#err path not allowed");
        return;
      }
      if (!fs::removeAll(c.path)) {
        say("#err delete failed");
        return;
      }
      break;
    case RemoteCommand::Status: {
      std::string text = os_.statusText();
      say("#status " + std::to_string(text.size()));
      send(text.data(), text.size());
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
      flushAll();
      host_.reboot();
      return;
    case RemoteCommand::PowerOff:
      // Main power off with an RTC alarm: the board (and the SD card) lose power, then boot again after the delay.
      say("#ok");
      flushAll();
      if (Surface* surf = os_.surface()) surf->prepareForPowerOff();  // as auto-off does: state saved, sleep screen on the panel
      // The RTC wake then runs the scheduled refresh (if there is a clock) and, being a test, stays on afterwards.
      os_.powerOffForSleep(static_cast<uint32_t>(c.x1), true, static_cast<uint32_t>(c.y1));
      return;
  }
  say("#ok");
}

void Remote::abortPut(const std::string& why) {
  put_.file.close();
  fs::removeAll(put_.partPath);
  put_.active = false;
  say("#err " + why);
}

void Remote::feedRaw(const uint8_t* data, size_t n) {
  if (!put_.active) return;
  put_.lastByteMs = clock_.millis();
  while (n > 0 && put_.got < put_.size) {
    const size_t take = static_cast<size_t>(std::min<uint64_t>(n, put_.size - put_.got));
    const uint64_t before = put_.got;
    if (!put_.file.write(data, take)) {
      abortPut("write failed");
      return;
    }
    put_.crc = crc32Update(put_.crc, data, take);
    put_.got += take;
    data += take;
    n -= take;
    if (put_.got / 4096 != before / 4096 && put_.got < put_.size) say("#ack " + std::to_string(put_.got));
  }
  if (put_.got < put_.size) return;
  const bool wrote = put_.file.close();
  put_.active = false;
  if (!wrote) {
    fs::removeAll(put_.partPath);
    say("#err write failed");
    return;
  }
  if (put_.crc != put_.expect) {
    fs::removeAll(put_.partPath);
    say("#err crc mismatch");
    return;
  }
  if (!fs::rename(put_.partPath, put_.path)) {
    fs::removeAll(put_.partPath);
    say("#err rename failed");
    return;
  }
  say("#ack " + std::to_string(put_.got));
  say("#ok");
}

void Remote::tick() {
  pump();  // reply bytes still waiting for room in the USB buffer
  const uint32_t now = clock_.millis();
  if (put_.active && now - put_.lastByteMs > 8000) abortPut("upload timed out");
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
  send(head, std::strlen(head));
  send(px.data(), px.size());
  say("#ok");
}

}  // namespace pos
