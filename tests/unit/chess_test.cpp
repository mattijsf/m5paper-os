#include <doctest.h>

#include <string>

#include "apps/chess/chess.h"

using namespace pos::chess;

namespace {
Position fen(const char* f) {
  Position p;
  REQUIRE(Position::fromFen(f, p));
  return p;
}
}  // namespace

TEST_CASE("chess: move generation matches the standard perft counts") {
  // https://www.chessprogramming.org/Perft_Results
  const Position start = Position::start();
  CHECK(perft(start, 1) == 20);
  CHECK(perft(start, 2) == 400);
  CHECK(perft(start, 3) == 8902);
  CHECK(perft(start, 4) == 197281);
  CHECK(perft(fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"), 3) == 97862);  // Kiwipete
  CHECK(perft(fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"), 4) == 43238);
  CHECK(perft(fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"), 3) == 9467);
  CHECK(perft(fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"), 3) == 62379);
}

TEST_CASE("chess: FEN both ways, UCI, the text on the panel") {
  const Position s = Position::start();
  CHECK(s.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  Position p;
  CHECK_FALSE(Position::fromFen("rnbqkbnr/ppp", p));
  CHECK(Position::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR", p));  // placement only (Lichess)
  CHECK(p.castling == (WK | WQ | BK | BQ));
  const Move e4 = fromUci(s, "e2e4");
  REQUIRE(e4.valid());
  CHECK(uci(e4) == "e2e4");
  CHECK_FALSE(fromUci(s, "e2e5").valid());
  CHECK(moveText(s, e4) == "e2-e4");
  CHECK(moveText(s, fromUci(s, "g1f3")) == "Pg1-f3");
  const Position promo = fen("8/4P3/8/8/8/8/k7/4K3 w - - 0 1");
  const Move q = fromUci(promo, "e7e8q");
  REQUIRE(q.valid());
  CHECK(uci(q) == "e7e8q");
  CHECK(moveText(promo, q) == "e7-e8D");
  const Position castle = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  CHECK(moveText(castle, fromUci(castle, "e1g1")) == "O-O");
  CHECK(moveText(castle, fromUci(castle, "e1c1")) == "O-O-O");
}

TEST_CASE("chess: the end of a game") {
  Game g;
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) REQUIRE(g.play(fromUci(g.now(), m)));
  CHECK(g.result() == Result::BlackMates);  // fool's mate
  CHECK(moveText(fen("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2"), fromUci(fen("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2"), "d8h4")) ==
        "Dd8-h4#");
  Game st;
  st.startPos = fen("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
  REQUIRE(st.play(fromUci(st.now(), "f7f6")));
  CHECK(st.result() == Result::Ongoing);
  Game st2;
  st2.startPos = fen("7k/8/5QK1/8/8/8/8/8 w - - 0 1");
  REQUIRE(st2.play(fromUci(st2.now(), "f6f7")));
  CHECK(st2.result() == Result::Stalemate);
  Game kk;
  kk.startPos = fen("8/8/8/4k3/8/8/3nK3/8 w - - 0 1");
  REQUIRE(kk.play(fromUci(kk.now(), "e2d2")));  // the king takes the knight: kings alone
  CHECK(kk.result() == Result::Material);
  Game rep;
  for (int i = 0; i < 2; ++i)
    for (const char* m : {"g1f3", "g8f6", "f3g1", "f6g8"}) REQUIRE(rep.play(fromUci(rep.now(), m)));
  CHECK(rep.result() == Result::Repetition);
}

TEST_CASE("chess: the engine takes a free piece, mates in one, and stays within its node limit") {
  const Position hang = fen("4k3/8/8/3q4/8/8/8/3QK3 w - - 0 1");
  CHECK(uci(bestMove(hang, 2, 1)) == "d1d5");
  const Position mate = fen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
  CHECK(uci(bestMove(mate, 2, 1)) == "a1a8");
  uint32_t nodes = 0;
  const Move m = bestMove(Position::start(), 4, 7, 20000, &nodes);
  CHECK(m.valid());
  CHECK(nodes <= 20000 + 200);
  // Level 1 varies with the seed but never gives the queen away.
  for (uint32_t seed = 0; seed < 8; ++seed) CHECK(uci(bestMove(hang, 1, seed)) == "d1d5");
}

#include "apps/chess/lichess.h"

TEST_CASE("lichess: requests carry the token only in the Authorization header") {
  const auto r = pos::lichess::challengeAi("lip_test", 9, false);
  CHECK(r.method == "POST");
  CHECK(r.url == "https://lichess.org/api/challenge/ai");
  CHECK(r.body == "level=8&color=black");  // the level is kept within 1..8
  bool auth = false;
  for (const auto& h : r.headers) auth = auth || (h.first == "Authorization" && h.second == "Bearer lip_test");
  CHECK(auth);
  CHECK(r.url.find("lip_test") == std::string::npos);
  CHECK(pos::lichess::move("t", "AbC123de", "e7e8q").url == "https://lichess.org/api/board/game/AbC123de/move/e7e8q");
  CHECK(pos::lichess::playing("t").method == "GET");
}

TEST_CASE("lichess: the answers") {
  CHECK(pos::lichess::gameIdOf(R"({"id":"q7ZvsdUF","variant":{"key":"standard"}})") == "q7ZvsdUF");
  CHECK(pos::lichess::gameIdOf(R"({"id":"../../x"})") == "");
  CHECK(pos::lichess::gameIdOf("not json") == "");

  const std::string body = R"({"nowPlaying":[{"gameId":"other123","fen":"8/8/8/8/8/8/8/8","isMyTurn":true},
    {"gameId":"q7ZvsdUF","fullId":"q7ZvsdUFxxxx","color":"white","fen":"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR",
     "hasMoved":true,"isMyTurn":true,"lastMove":"e7e5","source":"ai","speed":"correspondence"}]})";
  pos::lichess::Playing p;
  REQUIRE(pos::lichess::parsePlaying(body, "q7ZvsdUF", p));
  CHECK(p.found);
  CHECK(p.myTurn);
  CHECK(p.lastMove == "e7e5");
  CHECK(p.fen == "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR");
  REQUIRE(pos::lichess::parsePlaying(R"({"nowPlaying":[]})", "q7ZvsdUF", p));
  CHECK_FALSE(p.found);
  CHECK_FALSE(pos::lichess::parsePlaying("<html>", "q7ZvsdUF", p));

  pos::lichess::Ended e;
  REQUIRE(pos::lichess::parseGame(R"({"id":"q7ZvsdUF","status":"mate","winner":"black"})", e));
  CHECK(e.status == "mate");
  CHECK(e.winner == "black");
  REQUIRE(pos::lichess::parseGame(R"({"status":"stalemate"})", e));
  CHECK(e.winner == "");

  pos::hal::HttpResponse r;
  r.status = 401;
  CHECK(pos::lichess::problemText(r).find("LICHESS_TOKEN") != std::string::npos);
  r.status = 400;
  r.body = R"({"error":"Not your turn, or game already over"})";
  CHECK(pos::lichess::problemText(r) == "Lichess: Not your turn, or game already over");
}
