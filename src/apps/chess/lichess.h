// The Lichess part of the chess app (P7-23, D41): the requests of a game against the Lichess computer and what their answers
// mean. Platform-free; the app sends them with ctx.http. The token is LICHESS_TOKEN from the card (scopes board:play and
// challenge:write); it goes only into the Authorization header and is never logged or shown. docs/apps/chess.md.
//
// A game: challengeAi starts it (Lichess's computer at level 1..8, the user's colour); after each of the user's moves
// (`move`) the app asks `playing` every few seconds until it is the user's turn again, and reads Lichess's move from
// lastMove. A game that is no longer in `playing` has ended: `game` says how.
#pragma once

#include <string>

#include "hal/net.h"

namespace pos::lichess {

constexpr const char* kBase = "https://lichess.org";

hal::HttpRequest challengeAi(const std::string& token, int level, bool white);
hal::HttpRequest playing(const std::string& token);
hal::HttpRequest move(const std::string& token, const std::string& gameId, const std::string& uci);
hal::HttpRequest resign(const std::string& token, const std::string& gameId);
hal::HttpRequest game(const std::string& token, const std::string& gameId);

// challengeAi's answer: the new game's id, "" if none.
std::string gameIdOf(const std::string& body);

// One game in `playing`'s answer.
struct Playing {
  bool found = false;   // the game is still being played
  std::string fen;      // the position (Lichess sends the placement)
  std::string lastMove; // UCI, "" before the first move
  bool myTurn = false;
};
// False if the body cannot be read (then nothing is known).
bool parsePlaying(const std::string& body, const std::string& gameId, Playing& out);

// A finished game: Lichess's status ("mate", "resign", "stalemate", "draw", "outoftime", ...) and the winner ("white",
// "black", "" for none). False if the body cannot be read.
struct Ended {
  std::string status, winner;
};
bool parseGame(const std::string& body, Ended& out);

// The error text Lichess sends ({"error": "..."}), or "".
std::string errorOf(const std::string& body);

// What to tell the user for a failed request: no token, a refused token, no network, Lichess's own words.
std::string problemText(const hal::HttpResponse& r);

}  // namespace pos::lichess
