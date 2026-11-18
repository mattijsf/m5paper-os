// Somdoku: the user's own daily sum puzzle (somdoku.nl) on the e-ink panel, same puzzle, number and share text as the web
// game (game logic: somdoku.h). docs/apps/somdoku.md.
//
// One screen with three views: ready (name, day, streak, Start / Verder), play (the grid with its sums, a note, the keys)
// and done (time, streak, the timeline, Kopieer score). As on the web: no checking, the game ends by
// itself when the grid is right, the clock runs only while the puzzle is on screen and is shown only at the end.
#include <ctime>
#include <string>
#include <vector>

#include "apps/common/daily_game.h"
#include "apps/somdoku/somdoku.h"
#include "core/app.h"
#include "core/clipboard.h"
#include "core/strings.h"
#include "core/timefmt.h"
#include "ui/root.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace pos::apps::somdoku_app {

namespace game = ::pos::somdoku;
using game::kBlack;
using game::kEmpty;

namespace {

constexpr int kN = 5;

const char* const kDays[] = {"zondag", "maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag"};
const char* const kMonths[] = {"januari", "februari", "maart", "april", "mei", "juni",
                               "juli", "augustus", "september", "oktober", "november", "december"};

// Which puzzle a saved game belongs to: the sums. A changed generator gives other sums, and the saved game is dropped.
std::string sumsKey(const game::Puzzle& p) {
  std::string k;
  for (const auto& c : p.rows) k += joinInts(c) + "|";
  k += "/";
  for (const auto& c : p.cols) k += joinInts(c) + "|";
  return k;
}

// The game of today, as it is played and kept in the app's store.
struct State {
  std::vector<int> cells = std::vector<int>(kN * kN, kEmpty);
  game::Trail trail{kN * kN};
  bool started = false;
  bool finished = false;
  double activeMs = 0;  // the score: milliseconds played
};

// The grid with its sums: row sums on the left, column sums stacked above. Draws, and reports taps on cells.
class Board : public ui::Widget {
 public:
  static constexpr int kClueW = 96;
  static constexpr int kClueH = 96;
  static constexpr int kCell = 78;

  const game::Puzzle* puzzle = nullptr;
  const std::vector<int>* cells = nullptr;
  int selected = -1;
  bool live = false;  // taps select cells
  std::function<void(int)> onCell;

  ui::Size measure(int) override { return {kClueW + kN * kCell + 4, kClueH + kN * kCell + 4}; }  // + the 3 px frame

  int gridX() const { return frame.x + kClueW; }
  int gridY() const { return frame.y + kClueH; }
  Rect cellRect(int i) const {
    return Rect{static_cast<int16_t>(gridX() + (i % kN) * kCell), static_cast<int16_t>(gridY() + (i / kN) * kCell),
                static_cast<int16_t>(kCell), static_cast<int16_t>(kCell)};
  }

  void draw(Canvas& c) override {
    if (!puzzle || !cells) return;
    // Sums: one line per row, right-aligned against the grid; a column's stacked from the bottom up.
    for (int r = 0; r < kN; ++r) {
      std::string text;
      for (int v : puzzle->rows[static_cast<size_t>(r)]) text += (text.empty() ? "" : "  ") + std::to_string(v);
      ui::drawText(c, text, gridX() - 14, gridY() + r * kCell + kCell / 2, ui::Type::Title, ui::ink, ui::Anchor::MiddleRight, ui::paper);
    }
    for (int col = 0; col < kN; ++col) {
      const auto& clue = puzzle->cols[static_cast<size_t>(col)];
      const int lh = 30;
      for (size_t j = 0; j < clue.size(); ++j) {
        const int y = gridY() - 10 - static_cast<int>(clue.size() - 1 - j) * lh;
        ui::drawText(c, std::to_string(clue[j]), gridX() + col * kCell + kCell / 2, y - lh / 2, ui::Type::Title, ui::ink,
                     ui::Anchor::MiddleCenter, ui::paper);
      }
    }
    // Cells.
    for (int i = 0; i < kN * kN; ++i) {
      const Rect r = cellRect(i);
      const int v = (*cells)[static_cast<size_t>(i)];
      if (v == kBlack) {
        c.fillRect(r.x, r.y, r.w, r.h, ui::ink);
      } else if (v > 0) {
        ui::drawText(c, std::to_string(v), r.x + r.w / 2, r.y + r.h / 2, ui::Type::Headline, ui::ink, ui::Anchor::MiddleCenter,
                     ui::paper);
      }
    }
    // Lines: 1 px inside, 3 px around.
    for (int k = 1; k < kN; ++k) {
      c.drawFastVLine(gridX() + k * kCell, gridY(), kN * kCell, ui::ink);
      c.drawFastHLine(gridX(), gridY() + k * kCell, kN * kCell, ui::ink);
    }
    for (int t = 0; t < 3; ++t) c.drawRect(gridX() - t, gridY() - t, kN * kCell + 2 * t + 1, kN * kCell + 2 * t + 1, ui::ink);
    // The selected cell: a thick frame inside it (white on a black cell).
    if (selected >= 0) {
      const Rect r = cellRect(selected);
      const bool black = (*cells)[static_cast<size_t>(selected)] == kBlack;
      for (int t = 2; t < 8; ++t) c.drawRect(r.x + t, r.y + t, r.w - 2 * t + 1, r.h - 2 * t + 1, black ? ui::paper : ui::ink);
    }
  }

  bool onTouch(const Gesture& g) override {
    if (!live || g.type != Gesture::Tap) return g.type == Gesture::Down && live;
    const int cx = g.x - gridX(), cy = g.y - gridY();
    if (cx < 0 || cy < 0 || cx >= kN * kCell || cy >= kN * kCell) {
      if (onCell) onCell(-1);  // beside the grid: let the cell go
      return true;
    }
    if (onCell) onCell((cy / kCell) * kN + cx / kCell);
    return true;
  }
};

// The keys: 1 2 3 / 4 Zwart Wis. Always drawn the same: no key shows the selected cell's value, so selecting a cell or
// filling it refreshes only the board, never the keys (e-ink: less to refresh).
class KeyPad : public ui::Widget {
 public:
  static constexpr int kKeyH = 76;
  static constexpr int kGap = 16;
  static constexpr int kValues[6] = {1, 2, 3, 4, kBlack, kEmpty};
  std::function<void(int)> onKey;

  ui::Size measure(int availW) override { return {availW, 2 * kKeyH + kGap}; }

  Rect keyRect(int k) const {
    const int w = (frame.w - 2 * kGap) / 3;
    return Rect{static_cast<int16_t>(frame.x + (k % 3) * (w + kGap)), static_cast<int16_t>(frame.y + (k / 3) * (kKeyH + kGap)),
                static_cast<int16_t>(w), static_cast<int16_t>(kKeyH)};
  }

  void draw(Canvas& c) override {
    static const char* const kLabels[6] = {"1", "2", "3", "4", "Zwart", "Wis"};
    for (int k = 0; k < 6; ++k) {
      const Rect r = keyRect(k);
      c.drawRoundRect(r.x, r.y, r.w, r.h, 12, ui::ink);
      c.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 11, ui::ink);
      const ui::Type type = k < 4 ? ui::Type::Headline : ui::Type::Title;
      ui::drawText(c, kLabels[k], r.x + r.w / 2, r.y + r.h / 2, type, ui::ink, ui::Anchor::MiddleCenter, ui::paper);
    }
  }

  bool onTouch(const Gesture& g) override {
    if (g.type == Gesture::Down) return true;
    if (g.type != Gesture::Tap) return false;
    for (int k = 0; k < 6; ++k) {
      const Rect r = keyRect(k);
      if (g.x >= r.x && g.x < r.x + r.w && g.y >= r.y && g.y < r.y + r.h) {
        if (onKey) onKey(kValues[k]);
        return true;
      }
    }
    return true;
  }
};

// The score so far in the title bar while playing, as the web game's header: 5 x 5 small tiles, one per cell in the order
// they were first filled; kept = solid ink, changed later = ink frame with a diagonal, not filled yet = a gray frame. It
// changes only when a cell is filled for the first time or a filled cell is changed, so most moves do not redraw it.
constexpr int kTallyTile = 11;
constexpr int kTallyGap = 2;
constexpr int kTallyW = kN * (kTallyTile + kTallyGap) - kTallyGap;

void drawTally(Canvas& c, const Rect& f, const std::string& marks) {
  const int x0 = f.x;
  const int y0 = f.y + (f.h - kTallyW) / 2;
  for (int i = 0; i < kN * kN; ++i) {
    const int x = x0 + (i % kN) * (kTallyTile + kTallyGap);
    const int y = y0 + (i / kN) * (kTallyTile + kTallyGap);
    const char m = static_cast<size_t>(i) < marks.size() ? marks[static_cast<size_t>(i)] : ' ';
    if (m == 'g') {
      c.fillRect(x, y, kTallyTile, kTallyTile, ui::ink);
    } else if (m == 'y') {
      c.drawRect(x, y, kTallyTile, kTallyTile, ui::ink);
      c.drawLine(x, y + kTallyTile - 1, x + kTallyTile - 1, y, ui::ink);
      c.drawLine(x + 1, y + kTallyTile - 1, x + kTallyTile - 1, y + 1, ui::ink);
    } else {
      c.drawRect(x, y, kTallyTile, kTallyTile, ui::divider);
    }
  }
}

// The timeline as tiles, five to a row: kept = solid ink, changed later = a hatched square (legible on the gray panel).
class TrailTiles : public ui::Widget {
 public:
  std::string marks;
  static constexpr int kTile = 40;
  static constexpr int kGap = 8;
  ui::Size measure(int availW) override {
    const int rows = std::max(1, static_cast<int>((marks.size() + kN - 1) / kN));
    return {availW, rows * (kTile + kGap) - kGap};
  }
  void draw(Canvas& c) override {
    const int w = kN * (kTile + kGap) - kGap;
    const int x0 = frame.x + (frame.w - w) / 2;
    for (size_t i = 0; i < marks.size(); ++i) {
      const int x = x0 + static_cast<int>(i % kN) * (kTile + kGap);
      const int y = frame.y + static_cast<int>(i / kN) * (kTile + kGap);
      if (marks[i] == 'g') {
        c.fillRect(x, y, kTile, kTile, ui::ink);
      } else {
        c.drawRect(x, y, kTile, kTile, ui::ink);
        c.drawRect(x + 1, y + 1, kTile - 2, kTile - 2, ui::ink);
        for (int d = 8; d < 2 * kTile; d += 8) {
          const int ax = x + std::max(0, d - kTile), ay = y + std::min(d, kTile - 1);
          const int bx = x + std::min(d, kTile - 1), by = y + std::max(0, d - kTile);
          c.drawLine(ax, ay, bx, by, ui::ink);
        }
      }
    }
  }
};

}  // namespace

class GameScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    root_ = &root;
    current_ = this;

    // Ready (also the pause screen and the message while the puzzle is being made).
    ready_ = root.add<ui::Column>(20);
    ready_->add<ui::Spacer>(24);
    readyName_ = ready_->add<ui::Label>("", ui::Type::Headline);
    readyName_->align = ui::TextAlign::Center;
    readyDay_ = ready_->add<ui::Label>("", ui::Type::Body);
    readyDay_->align = ui::TextAlign::Center;
    readyStreak_ = ready_->add<ui::Label>("", ui::Type::Body);
    readyStreak_->align = ui::TextAlign::Center;
    ready_->add<ui::Spacer>(16);
    start_ = ready_->add<ui::Button>("Start", [this] { start(); });
    // Without a clock (the battery ran flat): set it from the internet, then today's puzzle.
    clockButton_ = ready_->add<ui::Button>("Klok zetten", [this] { setClock(); });
    readyHint_ = ready_->add<ui::Label>("", ui::Type::Body);
    readyHint_->align = ui::TextAlign::Center;
    readyHint_->maxLines = 3;

    // Play.
    play_ = root.add<ui::Column>(16, ui::Align::Center);
    play_->add<ui::Spacer>(8);
    board_ = play_->add<Board>();
    note_ = play_->add<ui::Label>("", ui::Type::Body);
    note_->align = ui::TextAlign::Center;
    keys_ = play_->add<KeyPad>();
    board_->onCell = [this](int i) { select(i); };
    keys_->onKey = [this](int v) { put(v); };

    // Done.
    done_ = root.add<ui::Column>(18);
    done_->add<ui::Spacer>(8);
    doneTitle_ = done_->add<ui::Label>("", ui::Type::Title);
    doneTitle_->align = ui::TextAlign::Center;
    doneStreak_ = done_->add<ui::Label>("", ui::Type::Body);
    doneStreak_->align = ui::TextAlign::Center;
    tiles_ = done_->add<TrailTiles>();
    done_->add<ui::Spacer>(8);
    // No send button: WhatsApp cannot be sent to directly (companion relay, D22). The score goes to the clipboard and is pasted
    // into a reply in the inbox, or on the phone.
    done_->add<ui::Button>("Kopieer score", [this] { copyScore(); });

    openDay();
  }

  // Today from the clock, and its puzzle (generated on the worker). Without a clock: nothing, the ready view says so.
  void openDay() {
    Date date;
    struct tm t{};
    if (apps::today(ctx().clock, date, &t)) {
      day_ = game::dayString(date);
      longDay_ = std::string(kDays[t.tm_wday]) + " " + std::to_string(t.tm_mday) + " " + kMonths[t.tm_mon];
    }
    number_ = day_.empty() ? 0 : game::puzzleNumber(day_);
    name_ = day_.empty() ? "Somdoku" : "Somdoku " + std::to_string(number_);
    setTitle(name_);
    readyName_->setText(name_);
    readyDay_->setText(longDay_);
    readyDay_->visible = !longDay_.empty();
    show();
    if (!day_.empty()) {
      const std::string day = day_;
      ctx().async([day] { return game::dailyPuzzle(day); }, [this](game::Puzzle p) {
        puzzle_ = std::move(p);
        load();
        board_->puzzle = &puzzle_;
        board_->cells = &state_.cells;
        show();
      });
    }
  }

  void onShow() override {
    current_ = this;
    if (view_ == View::Play) resumeClock();
  }
  void onHide() override { stopClock(); }
  ~GameScreen() override {
    if (current_ == this) current_ = nullptr;
  }
  // The game screen of the running app (the app has one).
  static GameScreen* current() { return current_; }

  std::string debugState() const override {
    int filled = 0;
    for (int v : state_.cells) filled += v != kEmpty;
    const char* views[] = {"wait", "ready", "play", "done"};
    return std::string("view=") + views[static_cast<int>(view_)] + " day=" + day_ + " number=" + std::to_string(number_) +
           " filled=" + std::to_string(filled) + " selected=" + std::to_string(selected_) +
           " grid=" + std::to_string(board_ ? board_->gridX() : 0) + "," + std::to_string(board_ ? board_->gridY() : 0) +
           " ms=" + std::to_string(static_cast<long>(state_.activeMs)) + " marks=" + game::marks(state_.trail) + ";";
  }

  // For the sleep screen.
  bool sleepInfo(SleepInfo& info) const {
    if (day_.empty()) return false;
    info.title = name_;
    info.subtitle = state_.finished ? "Opgelost in " + game::clock(state_.activeMs / 1000) : longDay_;
    return true;
  }

  void stopClock() {
    if (runningSince_ != 0) {
      state_.activeMs += static_cast<double>(ctx().clock.millis() - runningSince_);
      runningSince_ = 0;
      save();
    }
  }

 private:
  enum class View : uint8_t { Wait, Ready, Play, Done };

  void resumeClock() {
    if (runningSince_ == 0 && view_ == View::Play) runningSince_ = ctx().clock.millis() | 1u;  // 0 means stopped
  }
  void tick() {
    if (runningSince_ != 0) {
      const uint32_t now = ctx().clock.millis();
      state_.activeMs += static_cast<double>(now - runningSince_);
      runningSince_ = now;
    }
  }

  void show() {
    View v = View::Wait;
    if (puzzle_.ok) v = state_.finished ? View::Done : (state_.started && playing_) ? View::Play : View::Ready;
    view_ = v;
    ready_->visible = v == View::Ready || v == View::Wait;
    play_->visible = v == View::Play;
    if (v == View::Play)
      setTitleAccessory(kTallyW, [this](Canvas& c, const Rect& f) { drawTally(c, f, game::marks(state_.trail)); });
    else
      setTitleAccessory(0, nullptr);
    done_->visible = v == View::Done;
    start_->visible = v == View::Ready;
    clockButton_->visible = day_.empty() && !clock_.busy();
    readyStreak_->visible = v == View::Ready;
    if (day_.empty()) {
      readyHint_->setText(clock_.hint("De klok is nog niet gezet. Zet hem via wifi, dan komt de puzzel van vandaag."));
    } else if (v == View::Wait) {
      readyHint_->setText("Puzzel maken...");
    } else if (v == View::Ready) {
      start_->setText(state_.started ? "Verder" : "Start");
      readyStreak_->setText(game::streakText(streakNow()));
      readyHint_->setText(state_.started ? "Gepauzeerd. De klok staat stil." : "De klok loopt alleen terwijl je speelt.");
    } else if (v == View::Done) {
      doneTitle_->setText("Opgelost in " + game::clock(state_.activeMs / 1000));
      doneStreak_->setText(game::streakText(streakNow()));
      tiles_->marks = game::marks(state_.trail);
    }
    if (v == View::Play)
      setAction(Icon::Pause, [this] { pause(); });
    else
      setAction(Icon::Pause, nullptr);  // pausing only while playing
    if (v == View::Play) {
      board_->live = true;
      board_->selected = selected_;
      updateNote();
    }
    if (root_->host()) root_->host()->relayout();
  }

  void updateNote() {
    int open = 0;
    for (int v : state_.cells) open += v == kEmpty;
    std::string text;
    if (selected_ < 0 && open == kN * kN)
      text = "Tik een vakje, dan een cijfer.";
    else if (open)
      text = "Nog " + std::to_string(open) + (open == 1 ? " vakje leeg." : " vakjes leeg.");
    else
      text = "Alles ingevuld, maar het klopt nog niet.";
    note_->setText(text);
  }

  void start() {
    if (!puzzle_.ok || state_.finished) return;
    state_.started = true;
    playing_ = true;
    save();
    show();
    resumeClock();
  }

  void pause() {
    if (view_ != View::Play) return;
    stopClock();
    playing_ = false;
    selected_ = -1;  // justFilled_ stays, as on the web: pausing touches no other cell
    show();
  }

  void select(int i) {
    if (view_ != View::Play) return;
    if (i == selected_) i = -1;  // the same cell again lets it go
    // Only touching another cell ends a slip; letting the cell go does not (the web game's select(null)).
    if (i >= 0 && i != justFilled_) justFilled_ = -1;
    selected_ = i;
    board_->selected = i;
    board_->invalidate(Hint::Feedback);
    updateNote();
  }

  void put(int value) {
    if (view_ != View::Play || selected_ < 0) return;
    const int from = state_.cells[static_cast<size_t>(selected_)];
    const int to = from == value ? kEmpty : value;
    const std::string before = game::marks(state_.trail);
    game::noteMove(state_.trail, selected_, from, to, selected_ == justFilled_);
    // The tally is redrawn a moment later, in its own push: together with the cell it would be one tall rectangle from the
    // title bar down to the cell, and the cell's gray settle pass would then cover all of it. Alone it is ink only (no settle).
    if (game::marks(state_.trail) != before) ctx().timer(30, [this] { refreshTitleAccessory(); });
    state_.cells[static_cast<size_t>(selected_)] = to;
    justFilled_ = selected_;
    tick();
    if (state_.cells == puzzle_.solution) {
      stopClock();
      state_.finished = true;
      playing_ = false;
      selected_ = -1;
      markSolved();
      save();
      show();
      return;
    }
    save();
    board_->invalidate(Hint::Feedback);
    updateNote();
  }

  // Without a clock (the battery ran flat): set it from the internet, then today's puzzle.
  void setClock() {
    if (clock_.busy()) return;
    clock_.start(ctx(), [this] { openDay(); });
    show();  // "Verbinden met wifi..."
  }

  std::string scoreText() const { return game::shareText(name_, state_.activeMs / 1000, state_.trail, kN); }

  void copyScore() { apps::copyScore(ctx(), scoreText()); }

  // --- the store ------------------------------------------------------------------------------------------------------

  std::vector<std::string> solvedDays() const { return split(ctx().store.get("solved", std::string()), ','); }

  int streakNow() const {
    const std::vector<std::string> days = solvedDays();
    return game::streak(day_, [&](const std::string& d) {
      for (const auto& x : days)
        if (x == d) return true;
      return false;
    });
  }

  void markSolved() {
    std::vector<std::string> days = solvedDays();
    bool known = false;
    for (const auto& d : days) known = known || d == day_;
    if (!known) days.push_back(day_);
    if (days.size() > 400) days.erase(days.begin(), days.begin() + static_cast<long>(days.size() - 400));
    std::string s;
    for (const auto& d : days) s += (s.empty() ? "" : ",") + d;
    ctx().store.set("solved", s);
  }

  void load() {
    Store& st = ctx().store;
    if (st.get("day", std::string()) != day_ || st.get("sums", std::string()) != sumsKey(puzzle_)) {
      state_ = State{};
      save();
      return;
    }
    const std::vector<int> cells = splitInts(st.get("cells", std::string()));
    if (cells.size() == static_cast<size_t>(kN * kN)) state_.cells = cells;
    state_.trail = game::Trail(kN * kN);
    state_.trail.order = splitInts(st.get("order", std::string()));
    const std::string changed = st.get("changed", std::string());
    for (size_t i = 0; i < changed.size() && i < state_.trail.changed.size(); ++i) state_.trail.changed[i] = changed[i] == '1';
    state_.trail.edits = st.get("edits", 0);
    state_.started = st.get("started", false);
    state_.finished = st.get("finished", false);
    state_.activeMs = st.get("activeMs", 0.0);
  }

  void save() {
    if (!puzzle_.ok) return;
    Store& st = ctx().store;
    st.set("day", day_);
    st.set("sums", sumsKey(puzzle_));
    st.set("cells", joinInts(state_.cells));
    st.set("order", joinInts(state_.trail.order));
    std::string changed;
    for (bool b : state_.trail.changed) changed += b ? '1' : '0';
    st.set("changed", changed);
    st.set("edits", state_.trail.edits);
    st.set("started", state_.started);
    st.set("finished", state_.finished);
    st.set("activeMs", state_.activeMs);
    st.save();
  }

  ui::Column* root_ = nullptr;
  ui::Column *ready_ = nullptr, *play_ = nullptr, *done_ = nullptr;
  ui::Label *readyName_ = nullptr, *readyDay_ = nullptr, *readyStreak_ = nullptr, *readyHint_ = nullptr;
  ui::Button* start_ = nullptr;
  Board* board_ = nullptr;
  ui::Label* note_ = nullptr;
  KeyPad* keys_ = nullptr;
  ui::Label *doneTitle_ = nullptr, *doneStreak_ = nullptr;
  TrailTiles* tiles_ = nullptr;

  ui::Button* clockButton_ = nullptr;
  apps::ClockSetter clock_;
  std::string day_, longDay_, name_;
  int number_ = 0;
  game::Puzzle puzzle_;
  State state_;
  View view_ = View::Wait;
  bool playing_ = false;  // the puzzle is in sight (after Start / Verder); false again after a pause
  int selected_ = -1;
  int justFilled_ = -1;   // the cell filled last, while no other cell was touched: changing it is a slip
  uint32_t runningSince_ = 0;
  static GameScreen* current_;
};
GameScreen* GameScreen::current_ = nullptr;

class SomdokuApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<GameScreen>(); }
  // Leaving the app (back, home, power-off): the clock stops and the game is saved.
  void onStop() override {
    if (GameScreen* s = GameScreen::current()) s->stopClock();
  }
  bool sleepInfo(SleepInfo& info) override {
    GameScreen* s = GameScreen::current();
    return s && s->sleepInfo(info);
  }
};

POS_REGISTER_APP(SomdokuApp, {
    .id = "somdoku",
    .name = "Somdoku",
    .icon = Icon::Grid3x3,
    .order = 95,  // after Settings: the tiles of the other apps keep their places
    .stayOpen = true,  // a game: thinking is not leaving (the idle return)
});

}  // namespace pos::apps::somdoku_app
