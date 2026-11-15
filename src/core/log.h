// Logging. POS_LOG("tag", "fmt", args...) prints "[tag] message\n" through the platform hook
// pos::hal::logWrite (stderr in the simulator, Serial on the device).
// Never log secrets (tokens, passwords): key names and lengths only.
#pragma once

#include <cstdarg>
#include <cstdio>

#include "hal/platform.h"

namespace pos {

inline void logf(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

inline void logf(const char* tag, const char* fmt, ...) {
  char buf[256];
  int n = std::snprintf(buf, sizeof buf, "[%s] ", tag);
  if (n < 0 || n >= static_cast<int>(sizeof buf) - 2) n = 0;
  va_list ap;
  va_start(ap, fmt);
  int m = std::vsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
  va_end(ap);
  size_t len = n + (m < 0 ? 0 : m);
  if (len > sizeof buf - 2) len = sizeof buf - 2;  // truncated: keep room for '\n'
  buf[len] = '\n';
  buf[len + 1] = '\0';
  hal::logWrite(buf);
}

}  // namespace pos

#define POS_LOG(tag, ...) ::pos::logf((tag), __VA_ARGS__)
