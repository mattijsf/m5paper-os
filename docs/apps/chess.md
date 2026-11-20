# Schaken

Chess on the panel. Task P7-23, D41. App id `chess`, tile "Schaken" (icon `chess-knight`), after Pixelpuzzel:
the thirteenth tile, so it is on the launcher's second page.

## Setup

- **Tegenstander**: Lichess | De Paper. Lichess is chosen first when the card has `LICHESS_TOKEN`; without it the
  setup says what is missing and Begin stays off for Lichess.
- **Niveau**: 1..8 for Lichess (Stockfish's levels on Lichess), 1..4 against the Paper.
- **Jouw kleur**: Wit | Zwart. **Begin**; **Verder met de partij** while a game is kept.

## The board

- 60 px squares (480 px), the user's side at the bottom; light squares paper, dark ones a diagonal ink hatch (one line
  in five pixels, as in a printed diagram). The pieces are drawn as shapes (`drawPiece` in `chess_app.cpp`, no font):
  black pieces solid ink, white ones paper with a 2 px ink outline (the silhouette drawn eight times around, then the
  piece in paper on it). The last move is framed in ink (2 px) in both squares.
- **Every pixel of the board is pure black or white**: a
  selection or a move is one Fast push (`Hint::Feedback`) with no Text settle after it (DISPLAY.md: no settle when the
  changed pixels are black/white). The view is laid out again only when a button comes or goes (Opgeven shows from the
  start of a game), not on every move. On the device: a selection 51 ms from the tap to the push; a move 194 ms to its
  last push; each Fast push 248 ms of waveform, the bands of one move overlapping in the driver. Only the two lines of
  text (whose turn, the last move) get a small Text settle.
- **Tap a piece**, then where it goes: the piece gets a thick ink frame, its squares a dot (a ring for a capture). Tap it
  again, or another of your pieces, to change your mind. A pawn that reaches the last rank becomes a queen.
- Above the board: whose turn it is ("Jij bent aan zet.", "Schaak! ...", "Lichess denkt na...", "De Paper denkt
  na...", or the result). Under it the last full move in long notation with Dutch letters ("1. e2-e4 Pb8-c6"; K D T L P,
  a pawn without a letter, `x` a capture, `+` check, `#` mate, `O-O`).
- **Opgeven** asks once ("Tik nog eens om op te geven", 4 s), then ends the game (on Lichess too). At the end:
  **Nieuwe partij** (back to the setup).

## Against the Paper

`chess::bestMove` (`chess.h`): alpha-beta with quiescence (captures only) over material and piece-square tables,
iterative deepening to the level's depth (1..4), moves ordered captures first (MVV-LVA), at most 60,000 nodes so a move
cannot take long on the device; level 1 picks at random among the moves within a pawn and a half of the best. It runs on
the worker (`ctx.async`). The game ends by mate, stalemate, the fifty-move rule, threefold repetition or too little material
(`Game::result`).

## Against Lichess (`lichess.h`)

The user's personal token, `LICHESS_TOKEN=` in `/sd/paperos_config` (read at boot; scopes "board:play" and
"challenge:write"; made at lichess.org/account/oauth/token/create). It goes only into the `Authorization` header.

| Step | Request |
|---|---|
| Begin | `POST /api/challenge/ai` with `level` and `color`: the game id |
| Your move | `POST /api/board/game/{id}/move/{uci}` |
| Its move | `GET /api/account/playing` every 2.5 s until `isMyTurn` (at most two minutes, then "Lichess antwoordt niet." and **Opnieuw vragen**): `lastMove` is played on the Paper's copy of the game; if the position then differs from Lichess's `fen` (a move made on lichess.org), the board is taken from Lichess |
| The end | the game is no longer in `playing`: `GET /game/export/{id}` (JSON) gives `status` and `winner` |
| Opgeven | `POST /api/board/game/{id}/resign` |

`/api/account/playing` is asked instead of the Board API's game stream: one answer per request, as every other request on
the Paper (ctx.http), and Wi-Fi only while a reply is awaited. Failures in words (`lichess::problemText`): no connection,
a token Lichess does not know (401), a token without the scope (403), "wait a minute" (429), or Lichess's own error text.
A game that could not be created goes back to the setup with the reason.

## State

The app store (`/data/apps/chess.json`): `vs` (lichess | paper), `white`, `level`, `game` (the Lichess id), `start` (FEN),
`moves` (UCI, comma separated), `over` (the result text), `started`. A game left while it was the opponent's turn goes on
when the app opens again (the Paper thinks again; Lichess is asked again).

## Tests

`tests/unit/chess_test.cpp`: move generation against the standard perft counts (start position to depth 4, Kiwipete and
positions 3, 4, 5), FEN, UCI, the panel's notation, mate, stalemate, the material and repetition draws, the engine (takes a
hanging queen, mates in one, keeps to its node limit), Lichess's requests (the token only in the header) and answers.
UI scripts: `chess` (against the Paper: setup, a selection with its dots, a move and the answer, resigning, a new game),
`chess-mate` (a kept game with black: the flipped board, mate in one), `chess-lichess` (fixtures and a fake token:
creating the game, a move, Lichess's answer, resigning).
