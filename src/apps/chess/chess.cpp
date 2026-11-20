#include "apps/chess/chess.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace pos::chess {

namespace {

constexpr int kKnight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int kKing[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int kDiag[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr int kLine[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

inline bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
inline int sq(int f, int r) { return r * 8 + f; }
inline bool isWhite(int p) { return p > 0; }

// Squares whose castling right is lost when a piece leaves or arrives there.
uint8_t rightsLostAt(int s) {
  switch (s) {
    case 0: return WQ;
    case 7: return WK;
    case 4: return WK | WQ;
    case 56: return BQ;
    case 63: return BK;
    case 60: return BK | BQ;
    default: return 0;
  }
}

const char kLetters[] = " PNBRQK";  // FEN letters (white upper case)

}  // namespace

std::string squareName(int s) {
  if (s < 0 || s > 63) return "-";
  return std::string(1, static_cast<char>('a' + fileOf(s))) + static_cast<char>('1' + rankOf(s));
}

int squareFrom(const std::string& s) {
  if (s.size() < 2 || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
  return sq(s[0] - 'a', s[1] - '1');
}

Position Position::start() {
  Position p;
  Position::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", p);
  return p;
}

bool Position::fromFen(const std::string& fen, Position& out) {
  Position p;
  size_t i = 0;
  int r = 7, f = 0;
  for (; i < fen.size() && fen[i] != ' '; ++i) {
    const char ch = fen[i];
    if (ch == '/') {
      if (f != 8 || r == 0) return false;
      --r;
      f = 0;
    } else if (ch >= '1' && ch <= '8') {
      f += ch - '0';
      if (f > 8) return false;
    } else {
      const char* at = std::strchr(kLetters, ch >= 'a' ? ch - 32 : ch);
      if (!at || at == kLetters || f > 7) return false;
      const int piece = static_cast<int>(at - kLetters);
      p.board[sq(f, r)] = static_cast<int8_t>(ch >= 'a' ? -piece : piece);
      ++f;
    }
  }
  if (r != 0 || f != 8) return false;
  // The other fields, each optional.
  auto field = [&](std::string& outField) {
    while (i < fen.size() && fen[i] == ' ') ++i;
    outField.clear();
    while (i < fen.size() && fen[i] != ' ') outField += fen[i++];
    return !outField.empty();
  };
  std::string side, castle, ep, half, full;
  if (field(side)) p.whiteToMove = side != "b";
  if (field(castle)) {
    for (char c : castle) p.castling |= c == 'K' ? WK : c == 'Q' ? WQ : c == 'k' ? BK : c == 'q' ? BQ : 0;
  } else {
    if (p.board[4] == King && p.board[7] == Rook) p.castling |= WK;
    if (p.board[4] == King && p.board[0] == Rook) p.castling |= WQ;
    if (p.board[60] == -King && p.board[63] == -Rook) p.castling |= BK;
    if (p.board[60] == -King && p.board[56] == -Rook) p.castling |= BQ;
  }
  if (field(ep)) p.ep = static_cast<int8_t>(squareFrom(ep));
  if (field(half)) p.halfmove = std::atoi(half.c_str());
  if (field(full)) p.fullmove = std::max(1, std::atoi(full.c_str()));
  out = p;
  return true;
}

std::string Position::placement() const {
  std::string s;
  for (int r = 7; r >= 0; --r) {
    int empty = 0;
    for (int f = 0; f < 8; ++f) {
      const int p = board[sq(f, r)];
      if (!p) {
        ++empty;
        continue;
      }
      if (empty) s += static_cast<char>('0' + empty);
      empty = 0;
      const char ch = kLetters[std::abs(p)];
      s += p > 0 ? ch : static_cast<char>(ch + 32);
    }
    if (empty) s += static_cast<char>('0' + empty);
    if (r) s += '/';
  }
  return s;
}

std::string Position::fen() const {
  std::string c;
  if (castling & WK) c += 'K';
  if (castling & WQ) c += 'Q';
  if (castling & BK) c += 'k';
  if (castling & BQ) c += 'q';
  return placement() + (whiteToMove ? " w " : " b ") + (c.empty() ? "-" : c) + " " + squareName(ep) + " " +
         std::to_string(halfmove) + " " + std::to_string(fullmove);
}

bool Position::attacked(int s, bool byWhite) const {
  const int f = fileOf(s), r = rankOf(s);
  const int sign = byWhite ? 1 : -1;
  // Pawns: a white pawn attacks upwards, so it stands one rank below.
  const int pr = r - sign;
  for (int df : {-1, 1})
    if (onBoard(f + df, pr) && board[sq(f + df, pr)] == sign * Pawn) return true;
  for (const auto& d : kKnight)
    if (onBoard(f + d[0], r + d[1]) && board[sq(f + d[0], r + d[1])] == sign * Knight) return true;
  for (const auto& d : kKing)
    if (onBoard(f + d[0], r + d[1]) && board[sq(f + d[0], r + d[1])] == sign * King) return true;
  for (const auto& d : kDiag) {
    for (int k = 1;; ++k) {
      const int x = f + d[0] * k, y = r + d[1] * k;
      if (!onBoard(x, y)) break;
      const int p = board[sq(x, y)];
      if (!p) continue;
      if (p == sign * Bishop || p == sign * Queen) return true;
      break;
    }
  }
  for (const auto& d : kLine) {
    for (int k = 1;; ++k) {
      const int x = f + d[0] * k, y = r + d[1] * k;
      if (!onBoard(x, y)) break;
      const int p = board[sq(x, y)];
      if (!p) continue;
      if (p == sign * Rook || p == sign * Queen) return true;
      break;
    }
  }
  return false;
}

bool Position::inCheck(bool white) const {
  const int8_t king = white ? King : -King;
  for (int s = 0; s < 64; ++s)
    if (board[s] == king) return attacked(s, !white);
  return false;
}

std::vector<Move> Position::legalMoves() const {
  std::vector<Move> out;
  out.reserve(48);
  const bool w = whiteToMove;
  const int sign = w ? 1 : -1;
  int king = -1;  // found once: after a move it is there, or where the king went (no search of the board per move)
  for (int s = 0; s < 64 && king < 0; ++s)
    if (board[s] == sign * King) king = s;
  auto add = [&](int from, int to, int promo = 0) {
    Move m;
    m.from = static_cast<int8_t>(from);
    m.to = static_cast<int8_t>(to);
    m.promo = static_cast<int8_t>(promo);
    Position next = *this;
    next.play(m);
    const int k = from == king ? to : king;
    if (k < 0 || !next.attacked(k, !w)) out.push_back(m);
  };
  auto own = [&](int p) { return p != 0 && isWhite(p) == w; };
  for (int s = 0; s < 64; ++s) {
    const int p = board[s];
    if (!own(p)) continue;
    const int kind = std::abs(p), f = fileOf(s), r = rankOf(s);
    if (kind == Pawn) {
      const int r1 = r + sign, last = w ? 7 : 0, home = w ? 1 : 6;
      auto pawnTo = [&](int to) {
        if (rankOf(to) == last)
          for (int pr : {Queen, Rook, Bishop, Knight}) add(s, to, pr);
        else
          add(s, to);
      };
      if (onBoard(f, r1) && !board[sq(f, r1)]) {
        pawnTo(sq(f, r1));
        if (r == home && !board[sq(f, r1 + sign)]) add(s, sq(f, r1 + sign));
      }
      for (int df : {-1, 1}) {
        if (!onBoard(f + df, r1)) continue;
        const int to = sq(f + df, r1);
        const int t = board[to];
        if ((t && !own(t)) || to == ep) pawnTo(to);
      }
    } else if (kind == Knight || kind == King) {
      const auto& dirs = kind == Knight ? kKnight : kKing;
      for (const auto& d : dirs) {
        const int x = f + d[0], y = r + d[1];
        if (onBoard(x, y) && !own(board[sq(x, y)])) add(s, sq(x, y));
      }
      if (kind == King && !inCheck(w)) {
        const int base = w ? 0 : 56;
        if ((castling & (w ? WK : BK)) && !board[base + 5] && !board[base + 6] && board[base + 7] == sign * Rook &&
            !attacked(base + 5, !w))
          add(s, base + 6);
        if ((castling & (w ? WQ : BQ)) && !board[base + 3] && !board[base + 2] && !board[base + 1] &&
            board[base] == sign * Rook && !attacked(base + 3, !w))
          add(s, base + 2);
      }
    } else {
      auto slide = [&](const int (*dirs)[2]) {
        for (int k = 0; k < 4; ++k) {
          for (int n = 1;; ++n) {
            const int x = f + dirs[k][0] * n, y = r + dirs[k][1] * n;
            if (!onBoard(x, y)) break;
            const int t = board[sq(x, y)];
            if (own(t)) break;
            add(s, sq(x, y));
            if (t) break;
          }
        }
      };
      if (kind == Bishop || kind == Queen) slide(kDiag);
      if (kind == Rook || kind == Queen) slide(kLine);
    }
  }
  return out;
}

int Position::play(const Move& m) {
  const int p = board[m.from];
  const int kind = std::abs(p);
  const int sign = p > 0 ? 1 : -1;
  int captured = board[m.to];
  // En passant: the pawn taken stands beside the target square.
  if (kind == Pawn && m.to == ep && !captured) {
    const int taken = m.to - 8 * sign;
    captured = board[taken];
    board[taken] = 0;
  }
  board[m.to] = static_cast<int8_t>(m.promo ? sign * m.promo : p);
  board[m.from] = 0;
  // Castling: the rook jumps too.
  if (kind == King && std::abs(m.to - m.from) == 2) {
    const int base = m.from - 4;
    if (m.to > m.from) {
      board[base + 5] = board[base + 7];
      board[base + 7] = 0;
    } else {
      board[base + 3] = board[base];
      board[base] = 0;
    }
  }
  castling &= static_cast<uint8_t>(~(rightsLostAt(m.from) | rightsLostAt(m.to)));
  ep = kind == Pawn && std::abs(m.to - m.from) == 16 ? static_cast<int8_t>((m.from + m.to) / 2) : -1;
  halfmove = kind == Pawn || captured ? 0 : halfmove + 1;
  if (!whiteToMove) ++fullmove;
  whiteToMove = !whiteToMove;
  return captured;
}

Position Game::now() const {
  Position p = startPos;
  for (const Move& m : moves) p.play(m);
  return p;
}

bool Game::play(const Move& m) {
  const Position p = now();
  for (const Move& l : p.legalMoves())
    if (l == m) {
      moves.push_back(m);
      return true;
    }
  return false;
}

Result Game::result() const {
  Position p = startPos;
  std::vector<std::string> seen;
  auto key = [](const Position& q) { return q.placement() + (q.whiteToMove ? "w" : "b") + std::to_string(q.castling) + std::to_string(q.ep); };
  seen.push_back(key(p));
  for (const Move& m : moves) {
    p.play(m);
    seen.push_back(key(p));
  }
  if (p.legalMoves().empty()) return p.inCheck(p.whiteToMove) ? (p.whiteToMove ? Result::BlackMates : Result::WhiteMates) : Result::Stalemate;
  if (p.halfmove >= 100) return Result::FiftyMoves;
  if (std::count(seen.begin(), seen.end(), seen.back()) >= 3) return Result::Repetition;
  // Too little to mate: kings alone, or a king with one bishop or knight against a king.
  int minors = 0, others = 0;
  for (int s = 0; s < 64; ++s) {
    const int k = std::abs(p.board[s]);
    if (k == Bishop || k == Knight) ++minors;
    else if (k && k != King) ++others;
  }
  if (!others && minors <= 1) return Result::Material;
  return Result::Ongoing;
}

std::string uci(const Move& m) {
  if (!m.valid()) return "";
  std::string s = squareName(m.from) + squareName(m.to);
  if (m.promo) s += "  nbrq"[m.promo];
  return s;
}

Move fromUci(const Position& p, const std::string& s) {
  if (s.size() < 4) return {};
  const int from = squareFrom(s.substr(0, 2)), to = squareFrom(s.substr(2, 2));
  int promo = 0;
  if (s.size() >= 5) {
    const char* at = std::strchr("  nbrq", s[4]);
    promo = at && s[4] != ' ' ? static_cast<int>(at - "  nbrq") : 0;
  }
  for (const Move& m : p.legalMoves())
    if (m.from == from && m.to == to && m.promo == promo) return m;
  return {};
}

std::string moveText(const Position& before, const Move& m) {
  static const char* kDutch[] = {"", "", "P", "L", "T", "D", "K"};  // pion, Paard, Loper, Toren, Dame, Koning
  const int p = before.board[m.from];
  const int kind = std::abs(p);
  std::string s;
  if (kind == King && std::abs(m.to - m.from) == 2) {
    s = m.to > m.from ? "O-O" : "O-O-O";
  } else {
    const bool capture = before.board[m.to] != 0 || (kind == Pawn && m.to == before.ep);
    s = std::string(kDutch[kind]) + squareName(m.from) + (capture ? "x" : "-") + squareName(m.to);
    if (m.promo) s += kDutch[m.promo];
  }
  Position after = before;
  after.play(m);
  if (after.inCheck(after.whiteToMove)) s += after.legalMoves().empty() ? "#" : "+";
  return s;
}

uint64_t perft(const Position& p, int depth) {
  if (depth == 0) return 1;
  const std::vector<Move> moves = p.legalMoves();
  if (depth == 1) return moves.size();
  uint64_t n = 0;
  for (const Move& m : moves) {
    Position q = p;
    q.play(m);
    n += perft(q, depth - 1);
  }
  return n;
}

// ---- the engine --------------------------------------------------------------------------------------------------------

namespace {

constexpr int kValue[7] = {0, 100, 320, 330, 500, 900, 0};

// Piece-square tables from white's side, a8 first (as written), so index with the mirrored square for white.
constexpr int8_t kPst[7][64] = {
    {},
    {0,  0,  0,  0,  0,  0,  0,  0,  50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10, 5,  5,  10, 25, 25, 10, 5,  5,
     0,  0,  0,  20, 20, 0,  0,  0,  5,  -5, -10, 0, 0,  -10, -5, 5, 5,  10, 10, -20, -20, 10, 10, 5, 0,  0,  0,  0,  0,  0,  0,  0},
    {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0, 0, 0, 0, -20, -40, -30, 0, 10, 15, 15, 10, 0, -30, -30, 5, 15, 20, 20, 15, 5, -30,
     -30, 0, 15, 20, 20, 15, 0, -30, -30, 5, 10, 15, 15, 10, 5, -30, -40, -20, 0, 5, 5, 0, -20, -40, -50, -40, -30, -30, -30, -30, -40, -50},
    {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 10, 10, 5, 0, -10, -10, 5, 5, 10, 10, 5, 5, -10,
     -10, 0, 10, 10, 10, 10, 0, -10, -10, 10, 10, 10, 10, 10, 10, -10, -10, 5, 0, 0, 0, 0, 5, -10, -20, -10, -10, -10, -10, -10, -10, -20},
    {0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 10, 10, 10, 10, 10, 5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5,
     -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 5, 5, 0, 0, 0},
    {-20, -10, -10, -5, -5, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 5, 5, 5, 0, -10, -5, 0, 5, 5, 5, 5, 0, -5,
     0, 0, 5, 5, 5, 5, 0, -5, -10, 5, 5, 5, 5, 5, 0, -10, -10, 0, 5, 0, 0, 0, 0, -10, -20, -10, -10, -5, -5, -10, -10, -20},
    {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
     -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10, 20, 20, 0, 0, 0, 0, 20, 20, 20, 30, 10, 0, 0, 10, 30, 20},
};

// From the side to move's view.
int evaluate(const Position& p) {
  int score = 0;
  for (int s = 0; s < 64; ++s) {
    const int pc = p.board[s];
    if (!pc) continue;
    const int k = std::abs(pc);
    const int idx = pc > 0 ? (7 - rankOf(s)) * 8 + fileOf(s) : s;  // the table is written from white's side, a8 first
    const int v = kValue[k] + kPst[k][idx];
    score += pc > 0 ? v : -v;
  }
  return p.whiteToMove ? score : -score;
}

struct Search {
  uint32_t nodes = 0, limit = 0;
  bool stopped = false;
};

// Captures first, the most valuable victim by the least valuable attacker (MVV-LVA).
void order(const Position& p, std::vector<Move>& ms) {
  auto score = [&](const Move& m) {
    const int victim = std::abs(p.board[m.to]);
    return (victim ? 10 * kValue[victim] - kValue[std::abs(p.board[m.from])] : 0) + (m.promo ? 800 : 0);
  };
  std::stable_sort(ms.begin(), ms.end(), [&](const Move& a, const Move& b) { return score(a) > score(b); });
}

int quiesce(const Position& p, int alpha, int beta, Search& s, int depth = 0) {
  if (++s.nodes > s.limit) s.stopped = true;
  const int stand = evaluate(p);
  if (stand >= beta || s.stopped || depth > 6) return stand;
  alpha = std::max(alpha, stand);
  std::vector<Move> ms = p.legalMoves();
  order(p, ms);
  for (const Move& m : ms) {
    if (!p.board[m.to] && !m.promo) continue;
    Position q = p;
    q.play(m);
    const int v = -quiesce(q, -beta, -alpha, s, depth + 1);
    if (v >= beta) return v;
    alpha = std::max(alpha, v);
  }
  return alpha;
}

constexpr int kMate = 100000;

int negamax(const Position& p, int depth, int alpha, int beta, int ply, Search& s) {
  if (++s.nodes > s.limit) s.stopped = true;
  if (s.stopped) return evaluate(p);
  std::vector<Move> ms = p.legalMoves();
  if (ms.empty()) return p.inCheck(p.whiteToMove) ? -kMate + ply : 0;
  if (p.halfmove >= 100) return 0;
  if (depth <= 0) return quiesce(p, alpha, beta, s);
  order(p, ms);
  int best = -kMate * 2;
  for (const Move& m : ms) {
    Position q = p;
    q.play(m);
    const int v = -negamax(q, depth - 1, -beta, -alpha, ply + 1, s);
    best = std::max(best, v);
    alpha = std::max(alpha, v);
    if (alpha >= beta) break;
  }
  return best;
}

}  // namespace

Move bestMove(const Position& p, int level, uint32_t seed, uint32_t nodeLimit, uint32_t* nodesOut) {
  std::vector<Move> ms = p.legalMoves();
  if (ms.empty()) return {};
  level = std::max(1, std::min(4, level));
  order(p, ms);
  // Iterative deepening up to the level's depth; a depth cut short by the node limit keeps the last full answer.
  Search s;
  s.limit = nodeLimit;
  Move best = ms.front();
  std::vector<int> scores(ms.size(), 0);
  for (int depth = 1; depth <= level; ++depth) {
    std::vector<int> now(ms.size(), -kMate * 2);
    int alpha = -kMate * 2;
    Move pick = ms.front();
    for (size_t i = 0; i < ms.size(); ++i) {
      Position q = p;
      q.play(ms[i]);
      now[i] = -negamax(q, depth - 1, -kMate * 2, -alpha, 1, s);
      if (s.stopped) break;
      if (now[i] > alpha) {
        alpha = now[i];
        pick = ms[i];
      }
    }
    if (s.stopped && depth > 1) break;
    best = pick;
    scores = now;
    // Search the best move first at the next depth.
    const long i = std::find(ms.begin(), ms.end(), best) - ms.begin();
    std::rotate(ms.begin(), ms.begin() + i, ms.begin() + i + 1);
    std::rotate(scores.begin(), scores.begin() + i, scores.begin() + i + 1);
  }
  // Level 1: a random move among those within a pawn and a half of the best (a beginner's opponent).
  if (level == 1) {
    const int top = *std::max_element(scores.begin(), scores.end());
    std::vector<Move> ok;
    for (size_t i = 0; i < ms.size(); ++i)
      if (scores[i] >= top - 150) ok.push_back(ms[i]);
    uint32_t x = seed * 2654435761u + 12345u;
    x ^= x >> 13;
    best = ok[x % ok.size()];
  }
  if (nodesOut) *nodesOut = s.nodes;
  return best;
}

}  // namespace pos::chess
