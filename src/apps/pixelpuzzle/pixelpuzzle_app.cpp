// Pixelpuzzel: a daily picture puzzle (nonogram) on the e-ink panel. Game logic: pixelpuzzle.h. docs/apps/pixelpuzzle.md.
//
// One screen with three views, as Somdoku: ready (name, day, streak, Start / Verder), play (the grid with its clues, a note,
// Vullen | Kruisje) and done (the picture in grays, its name, the time, the streak, Kopieer score). A tap fills or crosses one
// cell, a swipe along a row or column a whole stretch (one refresh for many cells), a long press the other mark. The game
// ends by itself when every row and column has its clue; a clue that is met turns gray.
#include <ctime>
#include <string>
#include <vector>

#include "apps/common/daily_game.h"
#include "apps/pixelpuzzle/pixelpuzzle.h"
#include "apps/somdoku/somdoku.h"  // dates, the streak and the clock text, shared by the daily games
#include "core/app.h"
#include "core/strings.h"
#include "core/timefmt.h"
#include "ui/inputs.h"
#include "ui/root.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace pos::apps::pixelpuzzle_app {

namespace game = ::pos::pixelpuzzle;
using game::kN;

namespace {

const char* const kDays[] = {"zondag", "maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag"};
const char* const kMonths[] = {"januari", "februari", "maart", "april", "mei", "juni",
                               "juli", "augustus", "september", "oktober", "november", "december"};

// The grid with its clues: the rows' left of it, the columns' stacked above it. Draws, and reports which cells a finger
// touched: one for a tap or a long press, a straight stretch for a swipe.
class Board : public ui::Widget {
 public:
  static constexpr int kCell = 40;      // 4.3 mm on the panel: a finger's tap lands in its cell
  static constexpr int kClueLine = 24;  // a column clue's line height

  const game::Puzzle* puzzle = nullptr;
  const std::vector<uint8_t>* cells = nullptr;
  bool live = false;
  std::function<void(const std::vector<int>&, bool alt)> onCells;  // alt: a long press (the other mark)

  // Room above the grid for the longest column clue of the day (one to five numbers).
  int clueH() const {
    size_t most = 1;
    if (puzzle)
      for (const auto& c : puzzle->cols) most = std::max(most, c.size());
    return static_cast<int>(most) * kClueLine + 10;
  }
  ui::Size measure(int availW) override { return {availW, clueH() + kN * kCell + 4}; }

  int gridX() const { return frame.x + frame.w - kN * kCell - 3; }
  int gridY() const { return frame.y + clueH(); }

  void draw(Canvas& c) override {
    if (!puzzle || !cells) return;
    // Clues: a met one in gray (the player can leave that line alone).
    for (int r = 0; r < kN; ++r) {
      std::string text;
      for (int v : puzzle->rows[static_cast<size_t>(r)]) text += (text.empty() ? "" : " ") + std::to_string(v);
      if (text.empty()) text = "0";
      const Color col = game::lineDone(*puzzle, *cells, r) ? ui::disabled : ui::ink;
      ui::drawText(c, text, gridX() - 10, gridY() + r * kCell + kCell / 2, ui::Type::Body, col, ui::Anchor::MiddleRight, ui::paper);
    }
    for (int k = 0; k < kN; ++k) {
      game::Clue clue = puzzle->cols[static_cast<size_t>(k)];
      if (clue.empty()) clue.push_back(0);
      const Color col = game::lineDone(*puzzle, *cells, kN + k) ? ui::disabled : ui::ink;
      for (size_t j = 0; j < clue.size(); ++j) {
        const int y = gridY() - 8 - static_cast<int>(clue.size() - 1 - j) * kClueLine - kClueLine / 2;
        ui::drawText(c, std::to_string(clue[j]), gridX() + k * kCell + kCell / 2, y, ui::Type::Body, col, ui::Anchor::MiddleCenter,
                     ui::paper);
      }
    }
    // Cells: filled = ink, crossed = a gray cross.
    for (int i = 0; i < kN * kN; ++i) {
      const int x = gridX() + (i % kN) * kCell, y = gridY() + (i / kN) * kCell;
      const uint8_t v = (*cells)[static_cast<size_t>(i)];
      if (v == game::Filled) {
        c.fillRect(x + 1, y + 1, kCell - 1, kCell - 1, ui::ink);
      } else if (v == game::Crossed) {
        const int m = 10;
        for (int d = 0; d < 2; ++d) {
          c.drawLine(x + m + d, y + m, x + kCell - m + d, y + kCell - m, ui::secondary);
          c.drawLine(x + kCell - m + d, y + m, x + m + d, y + kCell - m, ui::secondary);
        }
      }
    }
    // Lines: 1 px between cells, 2 px every five, 3 px around.
    for (int k = 1; k < kN; ++k) {
      const int w = k % 5 == 0 ? 2 : 1;
      for (int t = 0; t < w; ++t) {
        c.drawFastVLine(gridX() + k * kCell + t, gridY(), kN * kCell, ui::ink);
        c.drawFastHLine(gridX(), gridY() + k * kCell + t, kN * kCell, ui::ink);
      }
    }
    for (int t = 0; t < 3; ++t) c.drawRect(gridX() - t, gridY() - t, kN * kCell + 2 * t + 1, kN * kCell + 2 * t + 1, ui::ink);
  }

  // The cell under a point, or -1 outside the grid.
  int cellAt(int x, int y) const {
    const int cx = x - gridX(), cy = y - gridY();
    if (cx < 0 || cy < 0 || cx >= kN * kCell || cy >= kN * kCell) return -1;
    return (cy / kCell) * kN + cx / kCell;
  }

  bool onTouch(const Gesture& g) override {
    if (!live) return false;
    if (g.type == Gesture::Down) return cellAt(g.x, g.y) >= 0;
    const int from = cellAt(g.type == Gesture::Tap || g.type == Gesture::LongPress ? g.x : g.startX,
                            g.type == Gesture::Tap || g.type == Gesture::LongPress ? g.y : g.startY);
    if (from < 0 || !onCells) return false;
    if (g.type == Gesture::Tap || g.type == Gesture::LongPress) {
      onCells({from}, g.type == Gesture::LongPress);
      return true;
    }
    if (g.type < Gesture::SwipeLeft || g.type > Gesture::SwipeDown) return false;
    // A swipe: the stretch from the first cell along its row or column to where the finger is now (kept inside the grid).
    const int r0 = from / kN, c0 = from % kN;
    const int cx = std::max(0, std::min(kN - 1, (g.x - gridX()) / kCell));
    const int cy = std::max(0, std::min(kN - 1, (g.y - gridY()) / kCell));
    std::vector<int> idx;
    if (g.type == Gesture::SwipeLeft || g.type == Gesture::SwipeRight) {
      for (int c = std::min(c0, cx); c <= std::max(c0, cx); ++c) idx.push_back(r0 * kN + c);
      if (cx < c0) std::reverse(idx.begin(), idx.end());
    } else {
      for (int r = std::min(r0, cy); r <= std::max(r0, cy); ++r) idx.push_back(r * kN + c0);
      if (cy < r0) std::reverse(idx.begin(), idx.end());
    }
    onCells(idx, false);
    return true;
  }
};

// The solved picture, larger, with the grays the puzzle did not show.
class Reveal : public ui::Widget {
 public:
  static constexpr int kCell = 28;
  const game::Puzzle* puzzle = nullptr;
  ui::Size measure(int availW) override { return {availW, kN * kCell + 4}; }
  void draw(Canvas& c) override {
    if (!puzzle) return;
    const int x0 = frame.x + (frame.w - kN * kCell) / 2, y0 = frame.y + 2;
    for (int i = 0; i < kN * kN; ++i) {
      const int x = x0 + (i % kN) * kCell, y = y0 + (i / kN) * kCell;
      if (puzzle->solution[static_cast<size_t>(i)])
        c.fillRect(x, y, kCell, kCell, ui::ink);
      else if (puzzle->shade[static_cast<size_t>(i)])
        c.fillRect(x, y, kCell, kCell, gray(puzzle->shade[static_cast<size_t>(i)]));
    }
    c.drawRect(x0 - 2, y0 - 2, kN * kCell + 4, kN * kCell + 4, ui::divider);
  }
};

}  // namespace

class GameScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    root_ = &root;
    current_ = this;

    // Ready and done keep the usual 24 px margins (10 + 14); only the grid uses the width.
    readyBox_ = root.add<ui::Padding>(14, 0, 14, 0);
    ready_ = readyBox_->add<ui::Column>(20);
    ready_->add<ui::Spacer>(24);
    readyName_ = ready_->add<ui::Label>("", ui::Type::Headline);
    readyName_->align = ui::TextAlign::Center;
    readyDay_ = ready_->add<ui::Label>("", ui::Type::Body);
    readyDay_->align = ui::TextAlign::Center;
    readyStreak_ = ready_->add<ui::Label>("", ui::Type::Body);
    readyStreak_->align = ui::TextAlign::Center;
    ready_->add<ui::Spacer>(16);
    start_ = ready_->add<ui::Button>("Start", [this] { start(); });
    clockButton_ = ready_->add<ui::Button>("Klok zetten", [this] { setClock(); });
    readyHint_ = ready_->add<ui::Label>("", ui::Type::Body);
    readyHint_->align = ui::TextAlign::Center;
    readyHint_->maxLines = 4;

    play_ = root.add<ui::Column>(16);
    play_->add<ui::Spacer>(4);
    board_ = play_->add<Board>();
    note_ = play_->add<ui::Label>("", ui::Type::Body);
    note_->align = ui::TextAlign::Center;
    mode_ = play_->add<ui::Padding>(14, 0, 14, 0)->add<ui::Segmented>(std::vector<std::string>{"Vullen", "Kruisje"}, 0);
    board_->onCells = [this](const std::vector<int>& idx, bool alt) { mark(idx, alt); };

    doneBox_ = root.add<ui::Padding>(14, 0, 14, 0);
    done_ = doneBox_->add<ui::Column>(16);
    done_->add<ui::Spacer>(8);
    reveal_ = done_->add<Reveal>();
    doneName_ = done_->add<ui::Label>("", ui::Type::Title);
    doneName_->align = ui::TextAlign::Center;
    doneTime_ = done_->add<ui::Label>("", ui::Type::Body);
    doneTime_->align = ui::TextAlign::Center;
    doneStreak_ = done_->add<ui::Label>("", ui::Type::Body);
    doneStreak_->align = ui::TextAlign::Center;
    done_->add<ui::Spacer>(4);
    done_->add<ui::Button>("Kopieer score", [this] { apps::copyScore(ctx(), scoreText()); });

    openDay();
  }

  int sideMargin() const override { return 10; }  // the grid and its row clues need the width (as Teletekst)

  void openDay() {
    Date date;
    struct tm t{};
    if (apps::today(ctx().clock, date, &t)) {
      day_ = somdoku::dayString(date);
      longDay_ = std::string(kDays[t.tm_wday]) + " " + std::to_string(t.tm_mday) + " " + kMonths[t.tm_mon];
    }
    number_ = day_.empty() ? 0 : game::puzzleNumber(day_);
    puzzle_ = day_.empty() ? game::Puzzle{} : game::dailyPuzzle(day_);
    name_ = number_ > 0 ? "Pixelpuzzel " + std::to_string(number_) : "Pixelpuzzel";
    setTitle(name_);
    readyName_->setText(name_);
    readyDay_->setText(longDay_);
    readyDay_->visible = !longDay_.empty();
    load();
    board_->puzzle = &puzzle_;
    board_->cells = &cells_;
    reveal_->puzzle = &puzzle_;
    show();
  }

  void onShow() override {
    current_ = this;
    if (view_ == View::Play) resumeClock();
  }
  void onHide() override { stopClock(); }
  ~GameScreen() override {
    if (current_ == this) current_ = nullptr;
  }
  static GameScreen* current() { return current_; }

  std::string debugState() const override {
    int filled = 0, crossed = 0;
    for (uint8_t v : cells_) {
      filled += v == game::Filled;
      crossed += v == game::Crossed;
    }
    const char* views[] = {"ready", "play", "done"};
    return std::string("view=") + views[static_cast<int>(view_)] + " day=" + day_ + " number=" + std::to_string(number_) +
           " name=" + nameToken() + " filled=" + std::to_string(filled) + " crossed=" + std::to_string(crossed) +
           " grid=" + std::to_string(board_ ? board_->gridX() : 0) + "," + std::to_string(board_ ? board_->gridY() : 0) +
           " mode=" + std::to_string(mode_ ? mode_->selected() : 0) + " ms=" + std::to_string(static_cast<long>(activeMs_)) + ";";
  }

  std::string nameToken() const {  // "Een_hart": one token for expect-state
    std::string n = puzzle_.name;
    for (char& ch : n)
      if (ch == ' ') ch = '_';
    return n;
  }

  bool sleepInfo(SleepInfo& info) const {
    if (day_.empty()) return false;
    info.title = name_;
    info.subtitle = finished_ ? puzzle_.name + ", opgelost in " + somdoku::clock(activeMs_ / 1000) : longDay_;
    return true;
  }

  void stopClock() {
    if (runningSince_ != 0) {
      activeMs_ += static_cast<double>(ctx().clock.millis() - runningSince_);
      runningSince_ = 0;
      save();
    }
  }

 private:
  enum class View : uint8_t { Ready, Play, Done };

  void resumeClock() {
    if (runningSince_ == 0 && view_ == View::Play) runningSince_ = ctx().clock.millis() | 1u;
  }
  void tick() {
    if (runningSince_ != 0) {
      const uint32_t now = ctx().clock.millis();
      activeMs_ += static_cast<double>(now - runningSince_);
      runningSince_ = now;
    }
  }

  void show() {
    const View v = !puzzle_.ok ? View::Ready : finished_ ? View::Done : (started_ && playing_) ? View::Play : View::Ready;
    view_ = v;
    readyBox_->visible = v == View::Ready;
    play_->visible = v == View::Play;
    doneBox_->visible = v == View::Done;
    start_->visible = puzzle_.ok && v == View::Ready;
    clockButton_->visible = day_.empty() && !clock_.busy();
    readyStreak_->visible = puzzle_.ok && v == View::Ready;
    if (day_.empty()) {
      readyHint_->setText(clock_.hint("De klok is nog niet gezet. Zet hem via wifi, dan komt de puzzel van vandaag."));
    } else if (!puzzle_.ok) {
      readyHint_->setText("De eerste puzzel is van 20 november 2026.");
    } else if (v == View::Ready) {
      start_->setText(started_ ? "Verder" : "Start");
      readyStreak_->setText(somdoku::streakText(streakNow()));
      readyHint_->setText(started_ ? "Gepauzeerd. De klok staat stil."
                                   : "Kleur de vakjes zo dat elke rij en kolom de getallen heeft: de lengtes van de zwarte stukken, "
                                     "op volgorde. Er komt een plaatje uit.");
    } else if (v == View::Done) {
      doneName_->setText(puzzle_.name);
      doneTime_->setText("Opgelost in " + somdoku::clock(activeMs_ / 1000));
      doneStreak_->setText(somdoku::streakText(streakNow()));
    }
    setAction(Icon::Pause, v == View::Play ? std::function<void()>([this] { pause(); }) : std::function<void()>());
    board_->live = v == View::Play;
    if (v == View::Play) updateNote();
    if (root_->host()) root_->host()->relayout();
  }

  void updateNote() {
    int open = 0;
    for (int i = 0; i < 2 * kN; ++i) open += !game::lineDone(puzzle_, cells_, i);
    note_->setText(open == 2 * kN ? "Tik een vakje, of veeg over een rij." : "Nog " + std::to_string(open) + (open == 1 ? " regel." : " regels."));
  }

  void start() {
    if (!puzzle_.ok || finished_) return;
    started_ = true;
    playing_ = true;
    save();
    show();
    resumeClock();
  }

  void pause() {
    if (view_ != View::Play) return;
    stopClock();
    playing_ = false;
    show();
  }

  // A tap, a long press or a swipe: the first cell decides. Put the mark (Vullen: filled, Kruisje: a cross; a long press the
  // other one) on every cell of the stretch, or take it away if the first cell already had it.
  void mark(const std::vector<int>& idx, bool alt) {
    if (view_ != View::Play || idx.empty()) return;
    const bool fill = (mode_->selected() == 0) != alt;
    const uint8_t want = fill ? game::Filled : game::Crossed;
    const uint8_t to = cells_[static_cast<size_t>(idx.front())] == want ? game::Empty : want;
    bool changed = false;
    for (int i : idx) {
      uint8_t& c = cells_[static_cast<size_t>(i)];
      if (c != to) {
        c = to;
        changed = true;
      }
    }
    if (!changed) return;
    tick();
    if (game::solved(puzzle_, cells_)) {
      stopClock();
      finished_ = true;
      playing_ = false;
      markSolved();
      save();
      show();
      return;
    }
    save();
    board_->invalidate(Hint::Feedback);
    updateNote();
  }

  void setClock() {
    if (clock_.busy()) return;
    clock_.start(ctx(), [this] { openDay(); });
    show();
  }

  std::string scoreText() const { return game::shareText(number_, puzzle_.name, activeMs_ / 1000); }

  // --- the store ------------------------------------------------------------------------------------------------------

  std::vector<std::string> solvedDays() const { return split(ctx().store.get("solved", std::string()), ','); }

  int streakNow() const {
    const std::vector<std::string> days = solvedDays();
    return somdoku::streak(day_, [&](const std::string& d) {
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

  // A saved game counts for the same day and the same picture (a changed list of pictures drops it).
  void load() {
    cells_.assign(kN * kN, game::Empty);
    started_ = finished_ = false;
    activeMs_ = 0;
    Store& st = ctx().store;
    if (!puzzle_.ok || st.get("day", std::string()) != day_ || st.get("name", std::string()) != puzzle_.name) return;
    const std::string cells = st.get("cells", std::string());
    if (cells.size() == cells_.size())
      for (size_t i = 0; i < cells.size(); ++i) cells_[i] = cells[i] == '1' ? game::Filled : cells[i] == '2' ? game::Crossed : game::Empty;
    started_ = st.get("started", false);
    finished_ = st.get("finished", false);
    activeMs_ = st.get("activeMs", 0.0);
  }

  void save() {
    if (!puzzle_.ok) return;
    Store& st = ctx().store;
    st.set("day", day_);
    st.set("name", puzzle_.name);
    std::string cells;
    for (uint8_t v : cells_) cells += static_cast<char>('0' + v);
    st.set("cells", cells);
    st.set("started", started_);
    st.set("finished", finished_);
    st.set("activeMs", activeMs_);
    st.save();
  }

  ui::Column* root_ = nullptr;
  ui::Padding *readyBox_ = nullptr, *doneBox_ = nullptr;
  ui::Column *ready_ = nullptr, *play_ = nullptr, *done_ = nullptr;
  ui::Label *readyName_ = nullptr, *readyDay_ = nullptr, *readyStreak_ = nullptr, *readyHint_ = nullptr;
  ui::Button *start_ = nullptr, *clockButton_ = nullptr;
  Board* board_ = nullptr;
  ui::Label* note_ = nullptr;
  ui::Segmented* mode_ = nullptr;
  Reveal* reveal_ = nullptr;
  ui::Label *doneName_ = nullptr, *doneTime_ = nullptr, *doneStreak_ = nullptr;

  apps::ClockSetter clock_;
  std::string day_, longDay_, name_;
  int number_ = 0;
  game::Puzzle puzzle_;
  std::vector<uint8_t> cells_ = std::vector<uint8_t>(kN * kN, game::Empty);
  bool started_ = false, finished_ = false, playing_ = false;
  double activeMs_ = 0;
  View view_ = View::Ready;
  uint32_t runningSince_ = 0;
  static GameScreen* current_;
};
GameScreen* GameScreen::current_ = nullptr;

class PixelpuzzleApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<GameScreen>(); }
  void onStop() override {
    if (GameScreen* s = GameScreen::current()) s->stopClock();
  }
  bool sleepInfo(SleepInfo& info) override {
    GameScreen* s = GameScreen::current();
    return s && s->sleepInfo(info);
  }
};

POS_REGISTER_APP(PixelpuzzleApp, {
    .id = "pixelpuzzle",
    .name = "Pixelpuzzel",
    .icon = Icon::Blocks,
    .order = 101,  // after Hacker News: the tiles of the other apps keep their places
    .stayOpen = true,
});

}  // namespace pos::apps::pixelpuzzle_app
