// Chess rules for the chess app (P7-23, D41): a position, its legal moves, playing them, the end of the game, FEN and UCI
// text, and a small engine for the game against the Paper. Platform-free (no Arduino, no SDL). docs/apps/chess.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pos::chess {

// Pieces: positive white, negative black, 0 empty.
enum Piece : int8_t { None = 0, Pawn = 1, Knight = 2, Bishop = 3, Rook = 4, Queen = 5, King = 6 };

// Squares 0..63: a1 = 0, h1 = 7, a8 = 56.
inline int fileOf(int sq) { return sq & 7; }
inline int rankOf(int sq) { return sq >> 3; }
std::string squareName(int sq);  // "e4"
int squareFrom(const std::string& s);  // -1 if not a square

struct Move {
  int8_t from = -1, to = -1;
  int8_t promo = 0;  // Knight..Queen (unsigned) for a promotion, else 0
  bool operator==(const Move& o) const { return from == o.from && to == o.to && promo == o.promo; }
  bool valid() const { return from >= 0; }
};

enum Castle : uint8_t { WK = 1, WQ = 2, BK = 4, BQ = 8 };

struct Position {
  int8_t board[64] = {};
  bool whiteToMove = true;
  uint8_t castling = 0;
  int8_t ep = -1;  // the square a pawn passed over (en passant target), -1 none
  int halfmove = 0;  // since the last capture or pawn move (the 50-move rule)
  int fullmove = 1;

  static Position start();
  // A FEN; with only the placement field the rest defaults (white to move unless `whiteToMove` says otherwise, castling
  // from kings and rooks on their home squares, no en passant). False if it cannot be read.
  static bool fromFen(const std::string& fen, Position& out);
  std::string fen() const;
  std::string placement() const;  // FEN's first field

  bool inCheck(bool white) const;
  bool attacked(int sq, bool byWhite) const;
  std::vector<Move> legalMoves() const;
  // Plays a move (assumed legal). Returns the captured piece (0 none).
  int play(const Move& m);
};

enum class Result : uint8_t { Ongoing, WhiteMates, BlackMates, Stalemate, FiftyMoves, Material, Repetition };

// The game: the start position and the moves since, with the repetition rule.
struct Game {
  Position startPos = Position::start();
  std::vector<Move> moves;
  Position now() const;
  Result result() const;
  bool play(const Move& m);  // false if not legal
};

std::string uci(const Move& m);  // "e2e4", "e7e8q"
// The legal move in `p` that UCI text names, or an invalid move.
Move fromUci(const Position& p, const std::string& s);
// Long notation with Dutch piece letters for the panel: "Pg1-f3", "e4xd5", "O-O", "e7-e8D", "+" for check, "#" for mate.
std::string moveText(const Position& before, const Move& m);

// The engine (D41): alpha-beta with quiescence over material and piece-square tables. `level` 1..4: depth 1 with some
// randomness .. depth 4. `nodeLimit` bounds the time on the device; `seed` makes the weak levels vary.
Move bestMove(const Position& p, int level, uint32_t seed, uint32_t nodeLimit = 60000, uint32_t* nodes = nullptr);

// For tests: the number of leaf positions at `depth` (perft).
uint64_t perft(const Position& p, int depth);

}  // namespace pos::chess
