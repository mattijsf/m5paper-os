// DisplayManager: the only code that pushes pixels to the panel (docs/DISPLAY.md).
// Widgets draw into the canvas and call invalidate(rect, hint); commit() diffs the canvas
// against the shadow (what the panel was last given), shrinks each rect to the pixels that
// changed, picks a refresh mode from the hint and the ghost budget, and pushes through
// hal::Display. Fast passes are settled to 16 gray levels later by tick().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/gfx.h"
#include "hal/clock.h"
#include "hal/display.h"

namespace pos {

// What the caller intends; the manager maps it to panel modes (docs/DISPLAY.md "Refresh classes").
enum class Hint : uint8_t {
  Feedback,  // immediate response to a touch
  Content,   // new content in place
  Screen,    // whole screen changed
  Image,     // grayscale picture
};

struct DisplayParams {
  float ghostLimit = 8.0f;       // score at which the next Screen commit becomes a Quality flash
  uint32_t settleMs = 600;       // quiet time before a Fast region is redrawn in 16 grays
  uint32_t idleCleanMs = 2000;   // idle time before a forced clean when the score reached 2 x limit
};

struct RefreshEntry {
  uint32_t t = 0;  // ms
  char mode = 'F';  // 'Q', 'T' or 'F'
  Rect rect;
  uint32_t changed = 0;  // pixels that differed from the shadow (settle and clean passes: region area)
  const char* why = "";   // "feedback", "content", "screen", "image", "settle", "clean", "boot"
};

constexpr uint32_t kNoDeadline = 0xFFFFFFFFu;

class DisplayManager {
 public:
  DisplayManager(hal::Display& display, Canvas& canvas, hal::Clock& clock, DisplayParams params = {});

  // Mark a region of the canvas as changed. Cheap; nothing reaches the panel until commit().
  void invalidate(Rect r, Hint hint);
  void invalidateAll(Hint hint) { invalidate(Rect{0, 0, kScreenW, kScreenH}, hint); }
  bool dirty() const { return !pending_.empty(); }

  // Push everything invalidated. While the panel is busy only Feedback is pushed; the rest stays
  // pending (coalesced) until a later commit.
  void commit();
  // Run settle and clean timers due at `nowMs`.
  void tick(uint32_t nowMs);
  // Earliest time tick() has work (settle, clean), kNoDeadline if none. Pending invalidations
  // that wait for the panel are due "now".
  uint32_t nextDeadlineMs() const;
  // Quality pass over the whole screen now: cleans ghosts, resets the score, drops settles.
  void forceClean();

  float ghostScore() const { return score_; }
  const DisplayParams& params() const { return params_; }
  const std::vector<RefreshEntry>& refreshLog() const { return log_; }
  // Last lines of the refresh log as text: "t=<ms> mode=<Q|T|F> rect=x,y,w,h changed=<n> why=<..>".
  std::string refreshLogTail(int maxLines = 20) const;
  // What the panel was last given, 4 bits per pixel (two per byte, high nibble first, 0 black .. 15
  // white): the same format as the >shot dump.
  const std::vector<uint8_t>& shadowPacked() const { return shadow_; }

 private:
  struct Pending {
    Rect rect;
    Hint hint;
  };
  struct Settle {
    Rect rect;
    uint32_t dueMs;
    float added;  // ghost score the Fast passes of this region added
  };

  void pushPass(Rect r, Hint hint);
  void push(Rect r, Refresh mode, uint32_t changed, const char* why);
  // Diff `r` against the shadow: returns the bounding box of changed pixels (empty if none).
  Rect diff(Rect r, uint32_t& changed, bool& onlyBlackWhite) const;
  void updateShadow(Rect r);
  void addSettle(Rect r, float added, uint32_t nowMs);
  uint8_t level(int x, int y) const;
  uint8_t shadowAt(size_t i) const;

  hal::Display& display_;
  Canvas& canvas_;
  hal::Clock& clock_;
  DisplayParams params_;
  std::vector<uint8_t> shadow_;
  bool shadowValid_ = false;
  std::vector<Pending> pending_;
  std::vector<Settle> settles_;
  std::vector<RefreshEntry> log_;
  float score_ = 0;
  uint32_t lastActivityMs_ = 0;
  bool wasBusy_ = false;
};

}  // namespace pos
