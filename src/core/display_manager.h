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
  Page,      // a new page of text (reader): Fast without a settle pass; every Nth is a Quality clean
  Screen,    // whole screen changed
  Image,     // grayscale picture
};

struct DisplayParams {
  float ghostLimit = 8.0f;       // score at which the next Screen commit becomes a Quality flash
  uint32_t settleMs = 600;       // quiet time before a Fast region is redrawn in 16 grays
  uint32_t idleCleanMs = 2000;   // idle time before a forced clean when the score reached 2 x limit
  // Every this many Page commits (page turns) the new page goes out as one full-screen Quality pass instead of Fast.
  // 0 = off. The ghost score alone never got there while reading (a turn added about 0.13 net; camera, 2026-11-16,
  // DISPLAY.md "Ghost budget").
  int pageCleanEvery = 10;
  // A Screen change whose new pixels hold at least this many pixels of flat gray (levels 1..14 with the same level 2 px
  // away on all four sides: fills such as the launcher's icon wells, not anti-aliased text edges) goes out as one Text
  // pass instead of Fast then settle: Fast thresholds those areas to black or white, so they appeared only with the
  // settle pass 0.6 s later, a second refresh. 0 = always Fast then settle.
  uint32_t grayAreaMin = 2000;
};

struct RefreshEntry {
  uint32_t t = 0;  // ms
  char mode = 'F';  // 'Q', 'T' or 'F'
  Rect rect;
  uint32_t changed = 0;  // pixels that differed from the shadow (settle and clean passes: region area)
  const char* why = "";   // "feedback", "content", "screen", "screen-gray", "image", "settle", "clean", "boot"
  uint32_t diffMs = 0;    // time spent comparing the canvas with the shadow before the push
  uint32_t pushMs = 0;    // time the hal::Display::push call itself took (it may block)
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
  int pagesSinceClean() const { return pages_; }
  const DisplayParams& params() const { return params_; }
  void setParams(const DisplayParams& p) { params_ = p; }
  // Pushes since boot per mode (Settings > Developer).
  struct Counts {
    uint32_t quality = 0, text = 0, fast = 0;
  };
  const Counts& counts() const { return counts_; }
  const std::vector<RefreshEntry>& refreshLog() const { return log_; }
  // Last lines of the refresh log as text: "t=<ms> mode=<Q|T|F> rect=x,y,w,h changed=<n> why=<..>".
  std::string refreshLogTail(int maxLines = 20) const;
  // What the panel was last given, 4 bits per pixel (two per byte, high nibble first, 0 black .. 15
  // white): the same format as the >shot dump. Built on demand from the shadow copy.
  std::vector<uint8_t> shadowPacked() const;

 private:
  static constexpr int kBandGap = 48;  // unchanged rows that end a band of a Feedback / Content push (pushPass)
  static constexpr int kMaxBands = 4;  // the last band takes what is left
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
  void pushBand(Rect r, Hint hint, int* restY);
  void push(Rect r, Refresh mode, uint32_t changed, const char* why, uint32_t diffMs = 0);
  // Diff `r` against the shadow: returns the bounding box of changed pixels (empty if none). With `grayArea` it also
  // counts changed pixels of flat gray (see DisplayParams::grayAreaMin), stopping at grayAreaMin.
  // Also brings the shadow up to date for `r` (each row's raw changed span is copied as soon as it is measured): every push
  // path used to copy the box in a second pass over the same PSRAM (DISPLAY.md "Tap latency").
  // With `restY` the scan stops after kBandGap unchanged rows below a changed one and says where the next band starts
  // (the end of `r` when nothing is left).
  Rect diff(Rect r, uint32_t& changed, bool& onlyBlackWhite, bool wantBw, uint32_t* grayArea = nullptr, int* restY = nullptr);
  bool flatGray(int x, int y, uint8_t q) const;
  void updateShadow(Rect r);
  void addSettle(Rect r, float added, uint32_t nowMs);

  hal::Display& display_;
  Canvas& canvas_;
  hal::Clock& clock_;
  DisplayParams params_;
  std::vector<uint8_t> shadow_;  // the canvas as last pushed (8-bit, exact copy): unchanged rows are found with memcmp
  bool shadowValid_ = false;
  std::vector<Pending> pending_;
  std::vector<Settle> settles_;
  std::vector<RefreshEntry> log_;
  Counts counts_;
  float score_ = 0;
  int pages_ = 0;  // Page commits since the last full-screen Quality pass
  uint32_t lastActivityMs_ = 0;
  bool wasBusy_ = false;
};

}  // namespace pos
