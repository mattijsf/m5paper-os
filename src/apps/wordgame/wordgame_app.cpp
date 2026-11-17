// Word game: a daily five-letter word game (game logic: wordgame.h). docs/apps/wordgame.md.
//
// One screen: the board of six rows, a legend, the letter keys; when the game is over the keys make way for the result
// and "Kopieer score". A second screen (the title bar's info button) has the statistics and the rules.
// More accepted words come from a list on the web, fetched about once a week and kept in the app's cache, so the game
// works offline (wordgame.h "More accepted words"). The words of the day are always the built-in ones.
// No colours on this panel: right place = solid ink, another place = a hatched ring, not in the word = a plain gray frame.
// All three are ink and paper only, so the Fast pass already shows the final look.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/common/daily_game.h"
#include "apps/wordgame/wordgame.h"
#include "core/app.h"
#include "core/clipboard.h"
#include "core/fs.h"
#include "core/strings.h"
#include "core/timefmt.h"
#include "ui/root.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace pos::apps::wordgame_app {

namespace game = ::pos::wordgame;
using game::kLen;
using game::kTries;

namespace {

std::string upper(char c) { return std::string(1, c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c); }

std::string upper(const std::string& s) {
  std::string out;
  for (char c : s) out += upper(c);
  return out;
}

constexpr const char* kListFile = "guesses";  // in the app's cache: the fetched words, packed (game::packGuessList)

constexpr const char* kNoClockHint = "Zet hem via wifi, dan komt het woord van vandaag.";

std::string triesText(int n) { return std::to_string(n) + (n == 1 ? " beurt" : " beurten"); }
using apps::daysText;

// Today's day number (game::dayOf), false while the clock was never set.
bool today(hal::Clock& clock, int* day) {
  Date date;
  if (!apps::today(clock, date)) return false;
  *day = game::dayOf(date);
  return true;
}

game::Stats loadStats(const Store& st) {
  game::Stats s;
  s.played = st.get("played", 0);
  s.won = st.get("won", 0);
  s.streak = st.get("streak", 0);
  s.maxStreak = st.get("maxStreak", 0);
  s.lastWonDay = st.get("lastWon", game::kNoDay);
  const std::vector<std::string> turns = split(st.get("turns", std::string()), ',');
  for (size_t i = 0; i < turns.size() && i < s.turns.size(); ++i) s.turns[i] = std::atoi(turns[i].c_str());
  return s;
}

void saveStats(Store& st, const game::Stats& s) {
  st.set("played", s.played);
  st.set("won", s.won);
  st.set("streak", s.streak);
  st.set("maxStreak", s.maxStreak);
  st.set("lastWon", s.lastWonDay);
  std::vector<std::string> turns;
  for (int n : s.turns) turns.push_back(std::to_string(n));
  st.set("turns", join(turns, ","));
}

// Fetches the word list from the web and puts its accepted guesses in the app's cache (wordgame.h "More accepted words").
// `done(packed)` runs on the UI thread with the packed words, or "" if it failed (the words that were there stay); it is
// dropped if the screen that asked is gone by then. Wi-Fi comes up for it by itself.
void fetchList(AppContext& ctx, std::function<void(std::string)> done) {
  ctx.store.set("listTried", static_cast<double>(ctx.clock.now()));
  ctx.store.save();
  hal::HttpRequest req;
  req.url = game::kListUrl;
  req.timeoutMs = 15000;
  AppContext* c = &ctx;  // outlives the callbacks: they are dropped with the screen, and the screens go before the app
  ctx.http(req, [c, done](hal::HttpResponse r) {
    if (r.status != 200 || r.body.empty() || r.body.size() > game::kListMaxBytes) {
      c->log("word list not fetched: status %d, %u bytes", r.status, static_cast<unsigned>(r.body.size()));
      done("");
      return;
    }
    // Packing and the cache file on the worker.
    const Cache cache = c->cache;  // a copy: the job may run after the app is gone
    auto body = std::make_shared<std::string>(std::move(r.body));
    c->async(
        [cache, body] {
          std::string packed = game::packGuessList(*body);
          if (!packed.empty() && !cache.write(kListFile, packed)) packed.clear();
          return packed;
        },
        [c, done](std::string packed) {
          if (packed.empty()) {
            c->log("word list: no words in it, or not written");
          } else {
            c->store.set("listFetched", static_cast<double>(c->clock.now()));
            c->store.set("listWords", static_cast<int>(packed.size() / kLen));
            c->store.save();
            c->log("word list: %u words", static_cast<unsigned>(packed.size() / kLen));
          }
          done(std::move(packed));
        });
  });
}

// Diagonal lines over a rectangle, `step` px apart.
void hatch(Canvas& c, int x, int y, int w, int h, int step) {
  for (int d = step / 2; d <= w + h - 2; d += step) {
    const int a = std::max(0, d - (h - 1)), b = std::min(d, w - 1);
    c.drawLine(x + a, y + d - a, x + b, y + d - b, ui::ink);
  }
}

void frameRect(Canvas& c, int x, int y, int w, int h, int stroke, Color color) {
  for (int t = 0; t < stroke; ++t) c.drawRect(x + t, y + t, w - 2 * t, h - 2 * t, color);
}

// A square in the look of a mark: the tiles of the board, the legend and the statistics.
//   right place: solid ink; another place: a hatched ring `ring` px wide around a paper middle; absent: a gray frame;
//   empty: a 1 px ink frame; typed and not played yet: a 3 px ink frame.
enum class Look : uint8_t { Empty, Typed, Right, Other, Absent };

Look lookOf(char mark) { return mark == game::kRight ? Look::Right : mark == game::kOther ? Look::Other : Look::Absent; }

// Returns the colour behind a letter drawn in the middle.
Color drawSquare(Canvas& c, int x, int y, int w, int h, Look look, int ring) {
  switch (look) {
    case Look::Right:
      c.fillRect(x, y, w, h, ui::ink);
      return ui::ink;
    case Look::Other:
      hatch(c, x, y, w, h, 4);
      frameRect(c, x, y, w, h, 2, ui::ink);
      c.fillRect(x + ring, y + ring, w - 2 * ring, h - 2 * ring, ui::paper);
      c.drawRect(x + ring - 1, y + ring - 1, w - 2 * ring + 2, h - 2 * ring + 2, ui::ink);
      return ui::paper;
    case Look::Typed:
      frameRect(c, x, y, w, h, 3, ui::ink);
      return ui::paper;
    case Look::Empty:
      // Ink, not gray: taking a letter back then changes only black and white pixels, one Fast push. With a gray frame
      // the Fast pass showed no tile at all and the settle pass drew it again 0.6 s later.
      frameRect(c, x, y, w, h, 1, ui::ink);
      return ui::paper;
    case Look::Absent:
      frameRect(c, x, y, w, h, 2, ui::divider);
      return ui::paper;
  }
  return ui::paper;
}

// The six rows of five tiles. Draws the played guesses with their marks and the letters typed so far.
class Board : public ui::Widget {
 public:
  static constexpr int kTile = 72;
  static constexpr int kGap = 8;
  static constexpr int kW = kLen * kTile + (kLen - 1) * kGap;
  static constexpr int kH = kTries * kTile + (kTries - 1) * kGap;

  const std::string* answer = nullptr;
  const std::vector<std::string>* guesses = nullptr;
  const std::string* typed = nullptr;

  ui::Size measure(int) override { return {kW, kH}; }

  Rect tileRect(int row, int col) const {
    return Rect{static_cast<int16_t>(frame.x + col * (kTile + kGap)), static_cast<int16_t>(frame.y + row * (kTile + kGap)),
                static_cast<int16_t>(kTile), static_cast<int16_t>(kTile)};
  }
  Rect rowRect(int row) const {
    return Rect{frame.x, static_cast<int16_t>(frame.y + row * (kTile + kGap)), static_cast<int16_t>(kW), static_cast<int16_t>(kTile)};
  }

  void draw(Canvas& c) override {
    if (!answer || !guesses || !typed) return;
    const int played = static_cast<int>(guesses->size());
    for (int row = 0; row < kTries; ++row) {
      const std::string marks = row < played ? game::marksOf(*answer, (*guesses)[static_cast<size_t>(row)]) : std::string();
      for (int col = 0; col < kLen; ++col) {
        const Rect r = tileRect(row, col);
        char letter = 0;
        Look look = Look::Empty;
        if (row < played) {
          letter = (*guesses)[static_cast<size_t>(row)][static_cast<size_t>(col)];
          look = lookOf(marks[static_cast<size_t>(col)]);
        } else if (row == played && static_cast<size_t>(col) < typed->size()) {
          letter = (*typed)[static_cast<size_t>(col)];
          look = Look::Typed;
        }
        const Color back = drawSquare(c, r.x, r.y, r.w, r.h, look, 10);
        if (letter) {
          const Color fg = look == Look::Right ? ui::paper : look == Look::Absent ? ui::disabled : ui::ink;
          ui::drawText(c, upper(letter), r.x + r.w / 2, r.y + r.h / 2, ui::Type::Headline, fg, ui::Anchor::MiddleCenter, back);
        }
      }
    }
  }
};

// What the three looks mean, on one line. Never changes, so it is never refreshed.
class Legend : public ui::Widget {
 public:
  static constexpr int kBox = 22;
  ui::Size measure(int availW) override { return {availW, 28}; }
  void draw(Canvas& c) override {
    struct Item {
      Look look;
      const char* text;
    };
    static const Item kItems[3] = {{Look::Right, "goede plek"}, {Look::Other, "andere plek"}, {Look::Absent, "zit er niet in"}};
    const int gap = 22, pad = 8;
    int total = 0;
    for (const Item& it : kItems) total += kBox + pad + ui::measureText(c, it.text, ui::Type::Caption);
    total += 2 * gap;
    int x = frame.x + (frame.w - total) / 2;
    const int cy = frame.y + frame.h / 2;
    for (const Item& it : kItems) {
      drawSquare(c, x, cy - kBox / 2, kBox, kBox, it.look, 5);
      x += kBox + pad;
      ui::drawText(c, it.text, x, cy, ui::Type::Caption, ui::ink, ui::Anchor::MiddleLeft, ui::paper);
      x += ui::measureText(c, it.text, ui::Type::Caption) + gap;
    }
  }
};

// The letter keys in three rows, with Enter and Backspace in the last. A key shows the best mark its letter got.
constexpr Color kSpentKey = gray(10);  // outline and letter of a key whose letter is not in the word
class KeyPad : public ui::Widget {
 public:
  static constexpr int kKeyW = 45;
  static constexpr int kKeyH = 72;
  static constexpr int kGapX = 4;
  static constexpr int kGapY = 8;
  static constexpr int kWideW = 69;
  static constexpr char kEnter = '\n';
  static constexpr char kBack = '\b';

  std::array<char, 26> marks{};
  std::function<void(char)> onKey;

  KeyPad() { marks.fill(game::kUnused); }

  ui::Size measure(int availW) override { return {availW, 3 * kKeyH + 2 * kGapY}; }

  struct Key {
    char ch;
    Rect r;
  };

  std::vector<Key> keys() const {
    static const char* const kRows[3] = {"qwertyuiop", "asdfghjkl", "\nzxcvbnm\b"};
    std::vector<Key> out;
    for (int row = 0; row < 3; ++row) {
      const std::string letters = kRows[row];
      int width = -kGapX;
      for (char ch : letters) width += (ch == kEnter || ch == kBack ? kWideW : kKeyW) + kGapX;
      int x = frame.x + (frame.w - width) / 2;
      for (char ch : letters) {
        const int w = ch == kEnter || ch == kBack ? kWideW : kKeyW;
        out.push_back({ch, Rect{static_cast<int16_t>(x), static_cast<int16_t>(frame.y + row * (kKeyH + kGapY)),
                                static_cast<int16_t>(w), static_cast<int16_t>(kKeyH)}});
        x += w + kGapX;
      }
    }
    return out;
  }

  // The middle of a key, for UI scripts.
  bool centerOf(char ch, int* x, int* y) const {
    for (const Key& k : keys()) {
      if (k.ch != ch) continue;
      *x = k.r.x + k.r.w / 2;
      *y = k.r.y + k.r.h / 2;
      return true;
    }
    return false;
  }

  void draw(Canvas& c) override {
    for (const Key& k : keys()) {
      const Rect& r = k.r;
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      if (k.ch == kEnter || k.ch == kBack) {
        ui::strokeRound(c, r, ui::kRadiusSmall, 2, ui::ink);
        ui::drawIcon(c, k.ch == kEnter ? Icon::CornerDownLeft : Icon::Delete, cx, cy, 32, ui::ink, ui::paper);
        continue;
      }
      const char mark = marks[static_cast<size_t>(k.ch - 'a')];
      Color fg = ui::ink, back = ui::paper;
      if (mark == game::kRight) {
        c.fillRoundRect(r.x, r.y, r.w, r.h, ui::kRadiusSmall, ui::ink);
        fg = ui::paper;
        back = ui::ink;
      } else if (mark == game::kOther) {
        // The hatched ring inside the outline; it stays clear of the rounded corners.
        ui::strokeRound(c, r, ui::kRadiusSmall, 2, ui::ink);
        hatch(c, r.x + 3, r.y + 3, r.w - 6, r.h - 6, 4);
        c.fillRect(r.x + 7, r.y + 10, r.w - 14, r.h - 20, ui::paper);
        c.drawRect(r.x + 6, r.y + 9, r.w - 12, r.h - 18, ui::ink);
      } else if (mark == game::kAbsent) {
        // Used up: faded well below the other keys.
        ui::strokeRound(c, r, ui::kRadiusSmall, 1, kSpentKey);
        fg = kSpentKey;
      } else {
        ui::strokeRound(c, r, ui::kRadiusSmall, 2, ui::ink);
      }
      ui::drawText(c, upper(k.ch), cx, cy, ui::Type::Title, fg, ui::Anchor::MiddleCenter, back);
    }
  }

  bool onTouch(const Gesture& g) override {
    if (g.type == Gesture::Down) return true;
    if (g.type != Gesture::Tap) return false;
    // A tap in the gap between two keys goes to the nearer one.
    for (const Key& k : keys()) {
      if (g.x >= k.r.x - kGapX / 2 && g.x < k.r.x + k.r.w + kGapX / 2 && g.y >= k.r.y - kGapY / 2 && g.y < k.r.y + k.r.h + kGapY / 2) {
        if (onKey) onKey(k.ch);
        return true;
      }
    }
    return true;
  }
};

// Four numbers with a caption under each.
class Numbers : public ui::Widget {
 public:
  struct Item {
    std::string value, caption;
  };
  std::vector<Item> items;
  ui::Size measure(int availW) override { return {availW, 92}; }
  void draw(Canvas& c) override {
    if (items.empty()) return;
    const int w = frame.w / static_cast<int>(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
      const int cx = frame.x + static_cast<int>(i) * w + w / 2;
      ui::drawText(c, items[i].value, cx, frame.y + 28, ui::Type::Headline, ui::ink, ui::Anchor::MiddleCenter, ui::paper);
      ui::drawText(c, items[i].caption, cx, frame.y + 72, ui::Type::Caption, ui::ink, ui::Anchor::MiddleCenter, ui::paper);
    }
  }
};

// Games per number of tries as bars: 1..6 and X for the games that were not guessed. `mark` (0..6, -1 none) is today's.
class Bars : public ui::Widget {
 public:
  std::array<int, kTries + 1> counts{};
  int mark = -1;
  static constexpr int kRow = 34;
  static constexpr int kGap = 8;
  ui::Size measure(int availW) override { return {availW, (kTries + 1) * (kRow + kGap) - kGap}; }
  void draw(Canvas& c) override {
    int most = 1;
    for (int n : counts) most = std::max(most, n);
    const int left = frame.x + 36;
    const int room = frame.w - 36;
    const int least = 44;  // room for the count inside the bar
    for (int i = 0; i <= kTries; ++i) {
      const int y = frame.y + i * (kRow + kGap);
      const int n = counts[static_cast<size_t>(i)];
      ui::drawText(c, i < kTries ? std::to_string(i + 1) : std::string("X"), frame.x + 12, y + kRow / 2, ui::Type::BodyStrong, ui::ink,
                   ui::Anchor::MiddleCenter, ui::paper);
      const int w = least + (room - least) * n / most;
      const bool solid = i == mark;
      if (solid)
        c.fillRect(left, y, w, kRow, ui::ink);
      else
        frameRect(c, left, y, w, kRow, 2, ui::ink);
      ui::drawText(c, std::to_string(n), left + w - 12, y + kRow / 2, ui::Type::BodyStrong, solid ? ui::paper : ui::ink,
                   ui::Anchor::MiddleRight, solid ? ui::ink : ui::paper);
    }
  }
};

}  // namespace

// Statistics and the rules.
class StatsScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Statistieken");
    const game::Stats s = loadStats(ctx().store);
    int day = 0;
    const bool clock = today(ctx().clock, &day);

    auto* col = root.add<ui::Column>(16);
    col->add<ui::Spacer>(4);
    auto* numbers = col->add<Numbers>();
    numbers->items = {{std::to_string(s.played), "gespeeld"},
                      {std::to_string(game::winPercent(s)), "% geraden"},
                      {std::to_string(clock ? game::streakOn(s, day) : 0), "reeks"},
                      {std::to_string(s.maxStreak), "langste reeks"}};
    col->add<ui::Label>("Aantal beurten", ui::Type::BodyStrong);
    auto* bars = col->add<Bars>();
    for (int i = 0; i < kTries; ++i) bars->counts[static_cast<size_t>(i)] = s.turns[static_cast<size_t>(i)];
    bars->counts[kTries] = std::max(0, s.played - s.won);
    // Today's finished game is the solid bar.
    if (clock && ctx().store.get("recorded", false) && ctx().store.get("day", game::kNoDay) == day) {
      const std::string answer = ctx().store.get("answer", std::string());
      const std::vector<std::string> guesses = split(ctx().store.get("guesses", std::string()), ',');
      bars->mark = game::solved(answer, guesses) ? static_cast<int>(guesses.size()) - 1 : kTries;
    }
    col->add<ui::Spacer>(4);
    auto* rules = col->add<ui::Label>(
        "Raad het woord van vijf letters in zes beurten. Elke gok is een bestaand woord. Om middernacht komt er een nieuw woord.",
        ui::Type::Body);
    rules->maxLines = 4;
    col->add<Legend>();
    // Metadata: how many words are accepted and when more were fetched; a button fetches them now (the game does it by
    // itself about once a week).
    col->add<ui::Spacer>(4);
    listLine_ = col->add<ui::Label>(listText(), ui::Type::Caption, ui::secondary);
    listLine_->align = ui::TextAlign::Center;
    listLine_->maxLines = 2;
    update_ = col->add<ui::Button>("Woordenlijst bijwerken", [this] { updateList(); }, ui::Button::Kind::Secondary);
  }

  std::string debugState() const override {
    return "view=stats list=" + std::to_string(ctx().store.get("listWords", 0)) + "; busy=" + (busy_ ? "1" : "0");
  }

 private:
  std::string listText() const {
    std::string list = "Woorden: " + std::to_string(game::wordCount()) + " ingebouwd, ";
    const int fetched = ctx().store.get("listWords", 0);
    struct tm t{};
    if (fetched > 0 && localTime(static_cast<int64_t>(ctx().store.get("listFetched", 0.0)), t)) {
      // Day and month only: with a four-digit count and the year the line did not fit (seen on the device, 2026-11-16).
      char date[16];
      std::snprintf(date, sizeof date, "%d-%d", t.tm_mday, t.tm_mon + 1);
      return list + std::to_string(fetched) + " opgehaald op " + date;
    }
    return list + "nog niets opgehaald";
  }

  void updateList() {
    if (busy_) return;
    busy_ = true;
    update_->setText("Ophalen...");
    fetchList(ctx(), [this](std::string packed) {
      busy_ = false;
      update_->setText("Woordenlijst bijwerken");
      if (packed.empty()) {
        ctx().toast("Ophalen lukte niet");
      } else {
        listLine_->setText(listText());
        ctx().toast("Woordenlijst bijgewerkt");
      }
    });
  }

  ui::Label* listLine_ = nullptr;
  ui::Button* update_ = nullptr;
  bool busy_ = false;
};

class GameScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    root_ = &root;
    current_ = this;

    // Without a clock there is no word of the day.
    noClock_ = root.add<ui::EmptyState>(Icon::Clock, "De klok is nog niet gezet", kNoClockHint, "Klok zetten",
                                        [this] { setClock(); });

    game_ = root.add<ui::Column>(14, ui::Align::Center);
    game_->add<ui::Spacer>(2);
    board_ = game_->add<Board>();
    board_->answer = &answer_;
    board_->guesses = &guesses_;
    board_->typed = &typed_;
    game_->add<Legend>();
    keys_ = game_->add<KeyPad>();
    keys_->onKey = [this](char ch) { key(ch); };

    // The result, in the place of the keys.
    done_ = root.add<ui::Column>(12);
    done_->add<ui::Spacer>(6);
    doneTitle_ = done_->add<ui::Label>("", ui::Type::Title);
    doneTitle_->align = ui::TextAlign::Center;
    doneLine_ = done_->add<ui::Label>("", ui::Type::Body);
    doneLine_->align = ui::TextAlign::Center;
    done_->add<ui::Spacer>(4);
    done_->add<ui::Button>("Kopieer score", [this] { copyScore(); });
    done_->add<ui::Button>("Statistieken", [this] { ctx().nav.push<StatsScreen>(); }, ui::Button::Kind::Secondary);

    setAction(Icon::Info, [this] { ctx().nav.push<StatsScreen>(); });
    openDay();
    show();
    loadList();
    fetchListIfDue();
  }

  // Back from the statistics, or still open after midnight: the word may be another one by now.
  void onShow() override {
    current_ = this;
    int day = 0;
    const bool clock = today(ctx().clock, &day);
    if (clock != hasDay_ || (clock && day != day_)) {
      openDay();
      show();
    }
    // The statistics screen may have fetched a new list meanwhile.
    if (ctx().store.get("listFetched", 0.0) != listStamp_) loadList();
  }
  ~GameScreen() override {
    if (current_ == this) current_ = nullptr;
  }
  static GameScreen* current() { return current_; }

  std::string debugState() const override {
    std::string marks;
    for (const std::string& g : guesses_) marks += (marks.empty() ? "" : ",") + game::marksOf(answer_, g);
    std::string s = std::string("view=") + (!hasDay_ ? "noclock" : over() ? "done" : "play") + " day=" + std::to_string(day_) +
                    " number=" + std::to_string(game::puzzleNumber(day_)) + " tries=" + std::to_string(guesses_.size()) +
                    " guesses=" + join(guesses_, ",") + "; typed=" + typed_ + "; marks=" + marks + "; list=" + std::to_string(extra_.size() / kLen) + ";";
    // Where the keys are: UI scripts tap them.
    if (keys_ && keys_->visible) {
      s += " keys=";
      for (char ch : std::string("qwertyuiopasdfghjklzxcvbnm\n\b")) {
        int x = 0, y = 0;
        if (keys_->centerOf(ch, &x, &y))
          s += std::string(1, ch == '\n' ? '+' : ch == '\b' ? '-' : ch) + std::to_string(x) + "," + std::to_string(y) + " ";
      }
    }
    return s;
  }

  bool sleepInfo(SleepInfo& info) const {
    if (!hasDay_) return false;
    info.title = name();
    if (!over())
      info.subtitle = "Beurt " + std::to_string(guesses_.size() + 1) + " van " + std::to_string(kTries);
    else
      info.subtitle = won() ? "Geraden in " + triesText(static_cast<int>(guesses_.size())) : "Niet geraden";
    return true;
  }

 private:
  bool won() const { return game::solved(answer_, guesses_); }
  bool over() const { return hasDay_ && game::finished(answer_, guesses_); }
  std::string name() const { return std::string(game::kName) + " " + std::to_string(game::puzzleNumber(day_)); }

  // Takes today's word and the saved game of today, if there is one.
  void openDay() {
    typed_.clear();
    guesses_.clear();
    answer_.clear();
    hasDay_ = today(ctx().clock, &day_);
    if (!hasDay_) {
      setTitle(game::kName);
      return;
    }
    setTitle(name());
    answer_ = game::answerOf(day_);
    Store& st = ctx().store;
    if (st.get("day", game::kNoDay) == day_ && st.get("answer", std::string()) == answer_) {
      for (const std::string& g : split(st.get("guesses", std::string()), ',')) {
        // Checked against the lists when it was played; the fetched list may not be loaded yet, so only the shape here.
        if (game::finished(answer_, guesses_) || g.size() != static_cast<size_t>(kLen)) break;
        guesses_.push_back(g);
      }
    } else {
      st.set("recorded", false);
    }
    save();
  }

  void save() {
    Store& st = ctx().store;
    st.set("day", day_);
    st.set("answer", answer_);
    st.set("guesses", join(guesses_, ","));
    st.save();
  }

  // Without a clock (the battery ran flat): set it from the internet, then today's word.
  void setClock() {
    if (clock_.busy()) return;
    clock_.start(ctx(), [this] {
      openDay();
      show();
      if (hasDay_) fetchListIfDue();
    });
    show();  // "Verbinden met wifi..."
  }

  void show() {
    const bool done = over();
    noClock_->visible = !hasDay_;
    if (noClock_->button()) noClock_->button()->visible = !clock_.busy();
    if (noClock_->hint()) noClock_->hint()->setText(clock_.hint(kNoClockHint));
    game_->visible = hasDay_;
    keys_->visible = hasDay_ && !done;
    done_->visible = done;
    keys_->marks = game::keyMarks(answer_, guesses_);
    if (done) {
      const game::Stats s = loadStats(ctx().store);
      if (won()) {
        doneTitle_->setText("Geraden in " + triesText(static_cast<int>(guesses_.size())));
        doneLine_->setText("Reeks: " + daysText(game::streakOn(s, day_)));
      } else {
        doneTitle_->setText("Niet geraden");
        doneLine_->setText("Het woord was: " + upper(answer_));
      }
    }
    if (root_->host()) root_->host()->relayout();
  }

  void key(char ch) {
    if (!hasDay_ || over()) return;
    const int row = static_cast<int>(guesses_.size());
    if (ch == KeyPad::kEnter) {
      enter();
    } else if (ch == KeyPad::kBack) {
      if (typed_.empty()) return;
      typed_.pop_back();
      board_->invalidateRect(board_->tileRect(row, static_cast<int>(typed_.size())), Hint::Feedback);
    } else if (typed_.size() < static_cast<size_t>(kLen)) {
      typed_ += ch;
      board_->invalidateRect(board_->tileRect(row, static_cast<int>(typed_.size()) - 1), Hint::Feedback);
    }
  }

  void enter() {
    if (typed_.size() < static_cast<size_t>(kLen)) {
      ctx().toast("Te weinig letters");
      return;
    }
    if (!known(typed_)) {
      ctx().toast("Onbekend woord");  // the row is not used up
      return;
    }
    const int row = static_cast<int>(guesses_.size());
    guesses_.push_back(typed_);
    typed_.clear();
    save();
    if (over()) {
      Store& st = ctx().store;
      if (!st.get("recorded", false)) {
        game::Stats s = loadStats(st);
        game::record(s, day_, won(), static_cast<int>(guesses_.size()));
        saveStats(st, s);
        st.set("recorded", true);
        st.save();
      }
      show();
      return;
    }
    board_->invalidateRect(board_->rowRect(row), Hint::Feedback);
    // The keys a moment later, in their own push: with the row they would be one rectangle from the row down to the keys.
    ctx().timer(30, [this] {
      keys_->marks = game::keyMarks(answer_, guesses_);
      keys_->invalidate(Hint::Feedback);
    });
  }

  bool known(const std::string& word) const { return game::isWord(word) || game::inPacked(extra_, word); }

  // The fetched words from the cache, read on the worker (about 30 KB). Until they are in, only the built-in words count.
  void loadList() {
    listStamp_ = ctx().store.get("listFetched", 0.0);
    const Cache cache = ctx().cache;  // a copy: the job may run after the app is gone
    ctx().async(
        [cache] {
          std::string packed = cache.read(kListFile);
          return game::validPacked(packed) ? packed : std::string();
        },
        [this](std::string packed) {
          if (!packed.empty()) extra_ = std::move(packed);
        });
  }

  // About once a week (and a day after a failed try): the list from the web. Wi-Fi comes up for it by itself; nothing is
  // shown, and a failure leaves the words that are there.
  void fetchListIfDue() {
    Store& st = ctx().store;
    if (!game::listDue(ctx().clock.now(), static_cast<int64_t>(st.get("listFetched", 0.0)), static_cast<int64_t>(st.get("listTried", 0.0)))) return;
    fetchList(ctx(), [this](std::string packed) {
      if (packed.empty()) return;
      extra_ = std::move(packed);
      listStamp_ = ctx().store.get("listFetched", 0.0);
    });
  }

  void copyScore() { apps::copyScore(ctx(), game::shareText(game::puzzleNumber(day_), answer_, guesses_)); }

  ui::Column* root_ = nullptr;
  ui::EmptyState* noClock_ = nullptr;
  apps::ClockSetter clock_;
  ui::Column *game_ = nullptr, *done_ = nullptr;
  Board* board_ = nullptr;
  KeyPad* keys_ = nullptr;
  ui::Label *doneTitle_ = nullptr, *doneLine_ = nullptr;

  bool hasDay_ = false;
  int day_ = 0;
  std::string answer_;
  std::vector<std::string> guesses_;
  std::string typed_;  // the letters of the row being filled; not kept when the app is left
  std::string extra_;  // fetched words accepted as a guess, packed and sorted; "" until loaded or when there are none
  double listStamp_ = -1;  // the store's listFetched that extra_ belongs to
  static GameScreen* current_;
};
GameScreen* GameScreen::current_ = nullptr;

// The app was called "vijfje" (its id, so its store and cache names) until 2026-11-16. A device that ran it then keeps its
// saved game, statistics and fetched words: they are moved to the new names once, before the store is read. Remove this
// when no device with the old names is left.
void adoptOldData() {
  const std::string oldStore = "/data/apps/vijfje.json", newStore = "/data/apps/wordgame.json";
  const std::string oldCache = "/data/cache/vijfje", newCache = "/data/cache/wordgame";
  if (fs::exists(oldStore)) {
    if (fs::exists(newStore) || !fs::rename(oldStore, newStore)) fs::removeAll(oldStore);
  }
  if (fs::exists(oldCache)) {
    const std::string file = std::string("/") + kListFile;
    if (fs::exists(oldCache + file) && !fs::exists(newCache + file) && fs::mkdirs(newCache)) fs::rename(oldCache + file, newCache + file);
    fs::removeAll(oldCache);
  }
}

class WordGameApp : public App {
 public:
  void onStart(AppContext& ctx) override {
    adoptOldData();
    ctx.nav.push<GameScreen>();
  }
  bool sleepInfo(SleepInfo& info) override {
    GameScreen* s = GameScreen::current();
    return s && s->sleepInfo(info);
  }
};

POS_REGISTER_APP(WordGameApp, {
    .id = "wordgame",
    .name = game::kName,  // the launcher tile follows the one name constant
    .icon = Icon::Type,
    .order = 96,  // after Somdoku
});

}  // namespace pos::apps::wordgame_app
