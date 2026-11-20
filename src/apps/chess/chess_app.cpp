// Schaken: chess against the Lichess computer (online, the user's token) or against the Paper (offline, a small engine).
// Rules and engine: chess.h; Lichess: lichess.h. docs/apps/chess.md, D41.
//
// One screen, two views. Setup: the opponent (Lichess | De Paper), the level, the colour, Begin (and Verder when a game is
// kept). Board: whose turn it is, the board (60 px squares, the user's side at the bottom), the last moves, Opgeven /
// Nieuwe partij. Tap a piece, then its square (dots show where it may go); a pawn that reaches the end becomes a queen.
#include <string>
#include <vector>

#include "apps/chess/chess.h"
#include "apps/chess/lichess.h"
#include "core/app.h"
#include "core/strings.h"
#include "ui/inputs.h"
#include "ui/root.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace pos::apps::chess_app {

namespace ch = ::pos::chess;

namespace {

constexpr uint32_t kPollMs = 2500;   // Lichess: ask for its move this often
constexpr int kPollMax = 48;         // two minutes, then say it does not answer
constexpr uint32_t kEngineNodes = 60000;

const Color kDark = gray(10);   // dark squares: visible on the panel, black pieces stand out on them
const Color kLight = ui::paper;

// ---- pieces, drawn as shapes in a 60-unit square ---------------------------------------------------------------------

struct Pen {
  Canvas& c;
  float x0, y0, u;      // the square's corner and the size of one unit
  int dx = 0, dy = 0;  // a shift in pixels: the white piece's outline is its silhouette drawn around it
  Color col;
  int X(float x) const { return static_cast<int>(x0 + x * u + 0.5f) + dx; }
  int Y(float y) const { return static_cast<int>(y0 + y * u + 0.5f) + dy; }
  int R(float r) const { return std::max(1, static_cast<int>(r * u + 0.5f)); }
  void tri(float ax, float ay, float bx, float by, float qx, float qy) { c.fillTriangle(X(ax), Y(ay), X(bx), Y(by), X(qx), Y(qy), col); }
  void rect(float l, float t, float r, float b) { c.fillRect(X(l), Y(t), std::max(1, X(r) - X(l)), std::max(1, Y(b) - Y(t)), col); }
  void circle(float x, float y, float r) { c.fillCircle(X(x), Y(y), R(r), col); }
  void ellipse(float x, float y, float rx, float ry) { c.fillEllipse(X(x), Y(y), R(rx), R(ry), col); }
};

void shape(Pen& p, int kind) {
  p.rect(14, 47, 46, 53);  // the foot
  switch (kind) {
    case ch::Pawn:
      p.tri(19, 47, 41, 47, 30, 24);
      p.ellipse(30, 29, 8, 3);
      p.circle(30, 19, 7);
      break;
    case ch::Rook:
      p.rect(20, 22, 40, 47);
      p.rect(16, 11, 44, 22);
      p.rect(17, 41, 43, 47);
      break;
    case ch::Bishop:
      p.tri(20, 47, 40, 47, 30, 34);
      p.ellipse(30, 29, 9, 13);
      p.circle(30, 12, 3.5f);
      break;
    case ch::Knight:
      p.tri(17, 47, 43, 47, 41, 22);
      p.tri(17, 47, 41, 22, 25, 30);
      p.tri(41, 22, 34, 8, 23, 16);
      p.tri(41, 22, 23, 16, 25, 30);
      p.tri(23, 16, 11, 30, 25, 30);
      p.tri(11, 30, 15, 36, 25, 30);
      break;
    case ch::Queen:
      p.tri(17, 47, 43, 47, 39, 24);
      p.tri(17, 47, 39, 24, 21, 24);
      p.tri(19, 26, 13, 12, 27, 24);
      p.tri(25, 24, 22, 9, 31, 24);
      p.tri(29, 24, 30, 7, 35, 24);
      p.tri(33, 24, 38, 9, 37, 24);
      p.tri(35, 24, 47, 12, 41, 26);
      p.circle(13, 12, 2.6f);
      p.circle(22, 9, 2.6f);
      p.circle(30, 7, 2.6f);
      p.circle(38, 9, 2.6f);
      p.circle(47, 12, 2.6f);
      break;
    case ch::King:
      p.tri(17, 47, 43, 47, 37, 21);
      p.tri(17, 47, 37, 21, 23, 21);
      p.ellipse(30, 21, 9, 4);
      p.rect(28, 4, 32, 18);
      p.rect(23, 8, 37, 12);
      break;
  }
}

// What a black piece shows in paper and a white one in ink (the white one's outline is its own).
void details(Pen& p, int kind, Color bg, bool white) {
  switch (kind) {
    case ch::Rook: {  // battlements: two notches cut into the top (a white rook's notch keeps an ink edge)
      const Color edge = p.col;
      for (float l : {21.0f, 35.0f}) {
        const int x0 = p.X(l), x1 = p.X(l + 4), y0 = p.Y(11) - (white ? 3 : 0), y1 = p.Y(15);
        p.c.fillRect(x0, y0, x1 - x0, y1 - y0, bg);
        if (white) {
          p.c.fillRect(x0 - 2, y0, 2, y1 - y0 + 2, edge);
          p.c.fillRect(x1, y0, 2, y1 - y0 + 2, edge);
          p.c.fillRect(x0 - 2, y1, x1 - x0 + 4, 2, edge);
        }
      }
      break;
    }
    case ch::Bishop:
      p.c.drawLine(p.X(26), p.Y(23), p.X(33), p.Y(30), p.col);
      p.c.drawLine(p.X(27), p.Y(23), p.X(34), p.Y(30), p.col);
      break;
    case ch::Knight:
      p.circle(28, 19, 1.6f);
      break;
    default:
      break;
  }
}

void drawPiece(Canvas& c, int piece, int x, int y, int size, Color bg) {
  const int kind = std::abs(piece);
  Pen p{c, static_cast<float>(x), static_cast<float>(y), size / 60.0f};
  p.col = ui::ink;
  const bool white = piece > 0;
  if (white) {  // white: the silhouette in ink, 2 px all round, then the piece in paper on it
    static const int kRing[8][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (const auto& d : kRing) {
      p.dx = d[0];
      p.dy = d[1];
      shape(p, kind);
    }
    p.dx = p.dy = 0;
    p.col = ui::paper;
    shape(p, kind);
    p.col = ui::ink;
  } else {
    shape(p, kind);
    p.col = ui::paper;
  }
  details(p, kind, bg, white);
}

// ---- the board -------------------------------------------------------------------------------------------------------

class Board : public ui::Widget {
 public:
  static constexpr int kSq = 60;
  const ch::Position* pos = nullptr;
  bool whiteBottom = true;
  int selected = -1;
  std::vector<int> targets;  // where the selected piece may go
  int lastFrom = -1, lastTo = -1;
  bool live = false;
  std::function<void(int)> onSquare;

  ui::Size measure(int availW) override { return {availW, 8 * kSq + 4}; }
  int left() const { return frame.x + (frame.w - 8 * kSq) / 2; }
  int top() const { return frame.y + 2; }
  // The square drawn at column `col`, row `row` from the top-left.
  int squareAt(int col, int row) const { return whiteBottom ? (7 - row) * 8 + col : row * 8 + (7 - col); }
  void cellOf(int sq, int& col, int& row) const {
    col = whiteBottom ? ch::fileOf(sq) : 7 - ch::fileOf(sq);
    row = whiteBottom ? 7 - ch::rankOf(sq) : ch::rankOf(sq);
  }

  void draw(Canvas& c) override {
    if (!pos) return;
    for (int row = 0; row < 8; ++row) {
      for (int col = 0; col < 8; ++col) {
        const int s = squareAt(col, row);
        const int x = left() + col * kSq, y = top() + row * kSq;
        const bool dark = (ch::fileOf(s) + ch::rankOf(s)) % 2 == 0;
        const Color bg = dark ? kDark : kLight;
        c.fillRect(x, y, kSq, kSq, bg);
        if (s == lastFrom || s == lastTo)  // the last move: a frame in both squares
          for (int t = 0; t < 3; ++t) c.drawRect(x + t, y + t, kSq - 2 * t, kSq - 2 * t, ui::secondary);
        if (pos->board[s]) drawPiece(c, pos->board[s], x, y, kSq, bg);
        if (s == selected)
          for (int t = 0; t < 5; ++t) c.drawRect(x + t, y + t, kSq - 2 * t, kSq - 2 * t, ui::ink);
      }
    }
    for (int s : targets) {
      int col, row;
      cellOf(s, col, row);
      const int x = left() + col * kSq + kSq / 2, y = top() + row * kSq + kSq / 2;
      if (pos->board[s]) {  // a capture: a ring round the piece's square
        for (int t = 0; t < 3; ++t) c.drawCircle(x, y, kSq / 2 - 2 - t, ui::ink);
      } else {
        c.fillCircle(x, y, 8, ui::ink);
        c.fillCircle(x, y, 4, ui::paper);
      }
    }
    for (int t = 0; t < 2; ++t) c.drawRect(left() - 1 - t, top() - 1 - t, 8 * kSq + 2 + 2 * t, 8 * kSq + 2 + 2 * t, ui::ink);
  }

  bool onTouch(const Gesture& g) override {
    if (!live) return false;
    const int cx = g.x - left(), cy = g.y - top();
    const bool inside = cx >= 0 && cy >= 0 && cx < 8 * kSq && cy < 8 * kSq;
    if (g.type == Gesture::Down) return inside;
    if (g.type != Gesture::Tap) return false;
    if (onSquare) onSquare(inside ? squareAt(cx / kSq, cy / kSq) : -1);
    return true;
  }
};

const char* resultText(ch::Result r, bool userWhite, bool vsLichess) {
  const bool userWins = (r == ch::Result::WhiteMates && userWhite) || (r == ch::Result::BlackMates && !userWhite);
  switch (r) {
    case ch::Result::WhiteMates:
    case ch::Result::BlackMates:
      return userWins ? "Schaakmat. Jij wint!" : vsLichess ? "Schaakmat. Lichess wint." : "Schaakmat. De Paper wint.";
    case ch::Result::Stalemate: return "Pat: remise.";
    case ch::Result::FiftyMoves: return "Remise: vijftig zetten zonder slaan.";
    case ch::Result::Material: return "Remise: te weinig stukken om mat te zetten.";
    case ch::Result::Repetition: return "Remise: drie keer dezelfde stelling.";
    default: return "";
  }
}

}  // namespace

class ChessScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    root_ = &root;
    current_ = this;
    setTitle("Schaken");
    hasToken_ = ctx().secrets.present("LICHESS_TOKEN");

    // Setup.
    setup_ = root.add<ui::Column>(14);
    setup_->add<ui::Spacer>(8);
    resume_ = setup_->add<ui::Button>("Verder met de partij", [this] { showBoard(); });
    setup_->add<ui::Label>("Tegenstander", ui::Type::Caption, ui::secondary);
    opponent_ = setup_->add<ui::Segmented>(std::vector<std::string>{"Lichess", "De Paper"}, hasToken_ ? 0 : 1);
    opponent_->onChange = [this](int) { setupChanged(); };
    setup_->add<ui::Label>("Niveau", ui::Type::Caption, ui::secondary);
    level_ = setup_->add<ui::Stepper>(3, 1, 8, 1, [this](int v) { return levelText(v); });
    setup_->add<ui::Label>("Jouw kleur", ui::Type::Caption, ui::secondary);
    color_ = setup_->add<ui::Segmented>(std::vector<std::string>{"Wit", "Zwart"}, 0);
    setup_->add<ui::Spacer>(8);
    begin_ = setup_->add<ui::Button>("Begin", [this] { begin(); });
    setupNote_ = setup_->add<ui::Label>("", ui::Type::Body, ui::secondary);
    setupNote_->maxLines = 4;

    // Board.
    play_ = root.add<ui::Column>(10);
    status_ = play_->add<ui::Label>("", ui::Type::Title);
    status_->align = ui::TextAlign::Center;
    board_ = play_->add<Board>();
    board_->onSquare = [this](int s) { tapSquare(s); };
    moves_ = play_->add<ui::Label>("", ui::Type::Body, ui::secondary);
    moves_->align = ui::TextAlign::Center;
    resign_ = play_->add<ui::Button>("Opgeven", [this] { resign(); }, ui::Button::Kind::Secondary);
    again_ = play_->add<ui::Button>("Nieuwe partij", [this] { showSetup(); });
    retry_ = play_->add<ui::Button>("Opnieuw vragen", [this] { retry(); }, ui::Button::Kind::Secondary);

    load();
    if (!game_.moves.empty() || !gameId_.empty() || !over_.empty())
      showBoard();
    else
      showSetup();
    if (vsLichess_ && !gameId_.empty() && over_.empty() && !userToMove()) poll(0);  // a game left while Lichess thought
    if (!vsLichess_ && over_.empty() && started_ && !userToMove()) think();
  }

  void onShow() override { current_ = this; }
  ~ChessScreen() override {
    if (current_ == this) current_ = nullptr;
  }
  static ChessScreen* current() { return current_; }

  std::string debugState() const override {
    return std::string("view=") + (play_->visible ? "board" : "setup") + " vs=" + (vsLichess_ ? "lichess" : "paper") +
           " white=" + (userWhite_ ? "1" : "0") + " moves=" + movesText() + " selected=" + ch::squareName(board_->selected) +
           " waiting=" + (waiting_ ? "1" : "0") + " over=" + (over_.empty() ? "0" : "1") + " board=" +
           std::to_string(board_->left()) + "," + std::to_string(board_->top()) + ";";
  }

  bool sleepInfo(SleepInfo& info) const {
    info.title = "Schaken";
    info.subtitle = over_.empty() ? (game_.moves.empty() ? "" : status_->text()) : over_;
    return true;
  }

 private:
  // ---- views ---------------------------------------------------------------------------------------------------------

  static std::string levelText(int v) { return std::to_string(v) + (v == 1 ? " (makkelijk)" : ""); }

  void setupChanged() {
    const bool lichess = opponent_->selected() == 0;
    level_->setRange(1, lichess ? 8 : 4);
    if (lichess && !hasToken_)
      setupNote_->setText("Voor Lichess is een token nodig: LICHESS_TOKEN op de kaart (docs/apps/chess.md).");
    else
      setupNote_->setText(lichess ? "Tegen de computer van Lichess, via wifi. De partij staat ook op lichess.org."
                                  : "Tegen de Paper zelf, zonder wifi. Niveau 4 denkt een paar seconden.");
    begin_->enabled = !(lichess && !hasToken_);
    begin_->invalidate(Hint::Feedback);
  }

  void showSetup() {
    setup_->visible = true;
    play_->visible = false;
    resume_->visible = over_.empty() && (!game_.moves.empty() || !gameId_.empty());
    setupChanged();
    if (root_->host()) root_->host()->relayout();
  }

  void showBoard() {
    setup_->visible = false;
    play_->visible = true;
    refresh();
  }

  // Everything on the board view from the state.
  void refresh() {
    const ch::Position p = game_.now();
    pos_ = p;
    board_->pos = &pos_;
    board_->whiteBottom = userWhite_;
    board_->live = over_.empty() && userToMove() && !waiting_;
    if (!game_.moves.empty()) {
      board_->lastFrom = game_.moves.back().from;
      board_->lastTo = game_.moves.back().to;
    } else {
      board_->lastFrom = board_->lastTo = -1;
    }
    std::string s;
    if (!over_.empty())
      s = over_;
    else if (!problem_.empty())
      s = problem_;
    else if (!userToMove())
      s = vsLichess_ ? "Lichess denkt na..." : "De Paper denkt na...";
    else
      s = p.inCheck(p.whiteToMove) ? "Schaak! Jij bent aan zet." : "Jij bent aan zet.";
    status_->setText(s);
    moves_->setText(lastMoves());
    resign_->visible = over_.empty() && (!game_.moves.empty() || !gameId_.empty());
    again_->visible = !over_.empty();
    retry_->visible = !problem_.empty() && over_.empty();
    if (root_->host()) root_->host()->relayout();
  }

  // "12. Pg1-f3  Lb8-c6": the last full move (or the last half).
  std::string lastMoves() const {
    if (game_.moves.empty()) return userWhite_ ? "Jij begint met wit." : "Je speelt met zwart.";
    ch::Position p = game_.startPos;
    std::vector<std::string> texts;
    for (const ch::Move& m : game_.moves) {
      texts.push_back(ch::moveText(p, m));
      p.play(m);
    }
    const size_t n = texts.size();
    const size_t moveNo = (n + 1) / 2;
    if (n % 2 == 1) return std::to_string(moveNo) + ". " + texts[n - 1];
    return std::to_string(moveNo) + ". " + texts[n - 2] + "  " + texts[n - 1];
  }

  std::string movesText() const {
    std::string s;
    for (const ch::Move& m : game_.moves) s += (s.empty() ? "" : ",") + ch::uci(m);
    return s;
  }

  bool userToMove() const { return game_.now().whiteToMove == userWhite_; }

  // ---- the user's move ------------------------------------------------------------------------------------------------

  void tapSquare(int s) {
    if (!board_->live) return;
    const int piece = s >= 0 ? pos_.board[s] : 0;
    if (board_->selected >= 0 && std::find(board_->targets.begin(), board_->targets.end(), s) != board_->targets.end()) {
      ch::Move chosen;
      for (const ch::Move& m : pos_.legalMoves())
        if (m.from == board_->selected && m.to == s && (m.promo == 0 || m.promo == ch::Queen)) chosen = m;  // promotion: a queen
      select(-1);
      if (chosen.valid()) userMove(chosen);
      return;
    }
    const bool own = piece != 0 && (piece > 0) == userWhite_;
    select(own && s != board_->selected ? s : -1);
  }

  void select(int s) {
    board_->selected = s;
    board_->targets.clear();
    if (s >= 0)
      for (const ch::Move& m : pos_.legalMoves())
        if (m.from == s && std::find(board_->targets.begin(), board_->targets.end(), m.to) == board_->targets.end())
          board_->targets.push_back(m.to);
    board_->invalidate(Hint::Feedback);
  }

  void userMove(const ch::Move& m) {
    game_.play(m);
    problem_.clear();
    save();
    const ch::Result r = game_.result();
    if (r != ch::Result::Ongoing) {
      finish(resultText(r, userWhite_, vsLichess_));
      if (vsLichess_) sendMove(m, false);  // Lichess ends its game too
      return;
    }
    refresh();
    if (vsLichess_)
      sendMove(m, true);
    else
      think();
  }

  // ---- against the Paper -------------------------------------------------------------------------------------------------

  void think() {
    const ch::Position p = game_.now();
    const int level = level_->value() > 4 ? 4 : level_->value();
    const uint32_t seed = static_cast<uint32_t>(game_.moves.size() * 7919u + ctx().clock.millis());
    waiting_ = true;
    refresh();
    ctx().async([p, level, seed] { return ch::bestMove(p, level, seed, kEngineNodes); },
                [this](ch::Move m) {
                  waiting_ = false;
                  if (m.valid()) theirMove(m);
                  else refresh();
                });
  }

  void theirMove(const ch::Move& m) {
    if (!game_.play(m)) return;
    save();
    const ch::Result r = game_.result();
    if (r != ch::Result::Ongoing) {
      finish(resultText(r, userWhite_, vsLichess_));
      return;
    }
    refresh();
  }

  // ---- against Lichess ---------------------------------------------------------------------------------------------------

  std::string token() const { return ctx().secrets.get("LICHESS_TOKEN"); }

  void sendMove(const ch::Move& m, bool thenPoll) {
    waiting_ = true;
    refresh();
    const std::string sent = ch::uci(m);
    ctx().http(lichess::move(token(), gameId_, sent), [this, thenPoll](hal::HttpResponse r) {
      waiting_ = false;
      if (r.status != 200) {
        problem_ = lichess::problemText(r);
        ctx().log("move refused: %d", r.status);
        refresh();
        return;
      }
      if (thenPoll) poll(0);
      else refresh();
    });
  }

  // Asks Lichess whether it moved; again every kPollMs until it did (or the game ended, or two minutes passed).
  void poll(int tries) {
    if (gameId_.empty()) return;
    waiting_ = true;
    refresh();
    ctx().http(lichess::playing(token()), [this, tries](hal::HttpResponse r) {
      lichess::Playing p;
      if (r.status != 200 || !lichess::parsePlaying(r.body, gameId_, p)) {
        waiting_ = false;
        problem_ = lichess::problemText(r);
        refresh();
        return;
      }
      if (!p.found) {  // over: ask how
        ended();
        return;
      }
      if (!p.myTurn) {
        if (tries + 1 >= kPollMax) {
          waiting_ = false;
          problem_ = "Lichess antwoordt niet.";
          refresh();
          return;
        }
        ctx().timer(kPollMs, [this, tries] { poll(tries + 1); });
        return;
      }
      waiting_ = false;
      apply(p);
    });
  }

  // Lichess's move: the last move, played on our copy of the game. If our copy and Lichess's position disagree after it
  // (a move made on lichess.org), the board is taken from Lichess's position.
  void apply(const lichess::Playing& p) {
    const ch::Position before = game_.now();
    const std::string ours = game_.moves.empty() ? "" : ch::uci(game_.moves.back());
    if (!p.lastMove.empty() && p.lastMove != ours) {
      const ch::Move m = ch::fromUci(before, p.lastMove);
      if (m.valid()) game_.play(m);
    }
    if (!p.fen.empty() && game_.now().placement() != p.fen.substr(0, p.fen.find(' '))) {
      ch::Position fresh;
      if (ch::Position::fromFen(p.fen, fresh)) {
        fresh.whiteToMove = userWhite_;  // it is the user's turn
        game_ = ch::Game{};
        game_.startPos = fresh;
        ctx().log("board taken from Lichess");
      }
    }
    problem_.clear();
    save();
    const ch::Result r = game_.result();
    if (r != ch::Result::Ongoing) {
      finish(resultText(r, userWhite_, true));
      return;
    }
    refresh();
  }

  void ended() {
    ctx().http(lichess::game(token(), gameId_), [this](hal::HttpResponse r) {
      waiting_ = false;
      lichess::Ended e;
      if (r.status == 200 && lichess::parseGame(r.body, e)) {
        const bool userWins = (e.winner == "white") == userWhite_ && !e.winner.empty();
        if (e.status == "mate") finish(userWins ? "Schaakmat. Jij wint!" : "Schaakmat. Lichess wint.");
        else if (e.status == "resign") finish(userWins ? "Lichess geeft op. Jij wint!" : "Je hebt opgegeven.");
        else if (e.winner.empty()) finish("Remise.");
        else finish(userWins ? "Jij wint." : "Lichess wint.");
      } else {
        finish("De partij is afgelopen.");
      }
    });
  }

  void retry() {
    problem_.clear();
    if (vsLichess_ && !gameId_.empty()) poll(0);
    else refresh();
  }

  // ---- start, stop -------------------------------------------------------------------------------------------------------

  void begin() {
    vsLichess_ = opponent_->selected() == 0;
    userWhite_ = color_->selected() == 0;
    game_ = ch::Game{};
    gameId_.clear();
    over_.clear();
    problem_.clear();
    started_ = true;
    save();
    showBoard();
    if (!vsLichess_) {
      if (!userWhite_) think();
      return;
    }
    waiting_ = true;
    status_->setText("Partij aanmaken bij Lichess...");
    board_->live = false;
    ctx().http(lichess::challengeAi(token(), level_->value(), userWhite_), [this](hal::HttpResponse r) {
      waiting_ = false;
      const std::string id = r.status == 200 || r.status == 201 ? lichess::gameIdOf(r.body) : "";
      if (id.empty()) {  // no game: back to the setup, with the reason
        started_ = false;
        save();
        showSetup();
        setupNote_->setText(lichess::problemText(r));
        return;
      }
      gameId_ = id;
      ctx().log("lichess game %s", id.c_str());
      save();
      if (!userWhite_) poll(0);  // Lichess opens
      else refresh();
    });
  }

  void resign() {
    if (!resignArmed_) {  // a second tap within 4 s, so a slip does not end the game
      resignArmed_ = true;
      resign_->setText("Tik nog eens om op te geven");
      ctx().timer(4000, [this] {
        resignArmed_ = false;
        resign_->setText("Opgeven");
      });
      return;
    }
    resignArmed_ = false;
    resign_->setText("Opgeven");
    if (vsLichess_ && !gameId_.empty()) ctx().http(lichess::resign(token(), gameId_), [](hal::HttpResponse) {});
    finish("Je hebt opgegeven.");
  }

  void finish(const std::string& text) {
    over_ = text;
    waiting_ = false;
    select(-1);
    save();
    refresh();
  }

  // ---- the store ---------------------------------------------------------------------------------------------------------

  void load() {
    Store& st = ctx().store;
    vsLichess_ = st.get("vs", std::string(hasToken_ ? "lichess" : "paper")) == "lichess";  // a card with a token: Lichess first
    userWhite_ = st.get("white", true);
    gameId_ = st.get("game", std::string());
    over_ = st.get("over", std::string());
    started_ = st.get("started", false);
    level_->setValue(st.get("level", 3));
    opponent_->setSelected(vsLichess_ && hasToken_ ? 0 : 1, Hint::Content);
    color_->setSelected(userWhite_ ? 0 : 1, Hint::Content);
    ch::Position start;
    if (!ch::Position::fromFen(st.get("start", std::string()), start)) start = ch::Position::start();
    game_ = ch::Game{};
    game_.startPos = start;
    for (const std::string& u : split(st.get("moves", std::string()), ',')) {
      const ch::Move m = ch::fromUci(game_.now(), u);
      if (!m.valid() || !game_.play(m)) break;
    }
  }

  void save() {
    Store& st = ctx().store;
    st.set("vs", std::string(vsLichess_ ? "lichess" : "paper"));
    st.set("white", userWhite_);
    st.set("game", gameId_);
    st.set("over", over_);
    st.set("started", started_);
    st.set("level", level_->value());
    st.set("start", game_.startPos.fen());
    st.set("moves", movesText());
    st.save();
  }

  ui::Column* root_ = nullptr;
  ui::Column *setup_ = nullptr, *play_ = nullptr;
  ui::Button *resume_ = nullptr, *begin_ = nullptr, *resign_ = nullptr, *again_ = nullptr, *retry_ = nullptr;
  ui::Segmented *opponent_ = nullptr, *color_ = nullptr;
  ui::Stepper* level_ = nullptr;
  ui::Label *setupNote_ = nullptr, *status_ = nullptr, *moves_ = nullptr;
  Board* board_ = nullptr;

  bool hasToken_ = false;
  bool vsLichess_ = false, userWhite_ = true, started_ = false, waiting_ = false, resignArmed_ = false;
  ch::Game game_;
  ch::Position pos_;
  std::string gameId_, over_, problem_;
  static ChessScreen* current_;
};
ChessScreen* ChessScreen::current_ = nullptr;

class ChessApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<ChessScreen>(); }
  bool sleepInfo(SleepInfo& info) override {
    ChessScreen* s = ChessScreen::current();
    return s && s->sleepInfo(info);
  }
};

POS_REGISTER_APP(ChessApp, {
    .id = "chess",
    .name = "Schaken",
    .icon = Icon::ChessKnight,
    .order = 102,  // after Pixelpuzzel: the tiles of the other apps keep their places
    .stayOpen = true,  // a game: thinking is not leaving
});

}  // namespace pos::apps::chess_app
