# Somdoku

The user's own daily sum puzzle (live at https://somdoku.nl/) on the panel. Same puzzle,
number, level and share text as the web game on the same day, so a score from the device can be shared in the user's
WhatsApp group next to scores from phones. Task P7-8; added on 2026-11-15 (games + clipboard, D21).

## Source of truth

The web game's `src/lib/puzzle.js` and `src/lib/game.js`. The C++ port is `src/apps/somdoku/somdoku.{h,cpp}` (platform-free):
mulberry32, FNV-1a over UTF-16 code units, the line-by-line solver and its pass count, the generator with the web game's
order of random draws, `FIRST_DAY` 2026-11-11, `puzzleNumber`, `levelOf`, `dailyPuzzle`, streak, clock text, the trail
(green kept, yellow changed later, a slip fixed before touching another cell does not count) and `shareText`.

**Checked against the web code itself**: `tools/somdoku-vectors.mjs` runs the web game's modules with node and writes
`tests/unit/data/somdoku_vectors.json` (500 days from 2026-11-11: day, number, level, passes, attempt, sums, solution; hash
and rng values; share texts for replayed trails; clock and streak texts). `tests/unit/somdoku_test.cpp` compares every value;
all 500 days match. **If the web game's generator changes, every day's puzzle changes: regenerate the vectors**
(`node tools/somdoku-vectors.mjs path/to/somdoku > tests/unit/data/somdoku_vectors.json`) and port the change until the
test passes again.

The day is the device's local date (`net.timezone`). With the clock never set (no Wi-Fi since the battery ran out) the app
says so and shows no puzzle.

## Screens (one Screen, three views)

- **Ready** (also the pause screen): "Somdoku 6", "maandag 16 november", "Reeks: 3 dagen", Start / Verder, a hint. The puzzle
  is generated on the worker (`ctx.async`; at most a few hundred grids, well under a second on the device: measure).
- **Play**: the 5x5 grid with the row sums left and the column sums stacked above; a note ("Nog 22 vakjes leeg.", "Alles
  ingevuld, maar het klopt nog niet."); keys 1 2 3 / 4 Zwart Wis. Tap a cell, then a key; the same value again empties the
  cell; tapping the selected cell or beside the grid lets it go. A slip (the cell filled last changed again before another
  cell is touched) does not turn yellow; letting the cell go or pausing does not end it, only selecting another cell does
  (the web game's `justFilled`; `debugState` shows the marks, `marks=ygg;`). Filled digit = ink Headline digit, black cell = solid ink,
  selected cell = thick ink frame (paper on a black cell). The keys never change (no key shows the selected cell's value: selecting or filling refreshes only the board). The
  title bar's pause button (only while playing) goes back to Ready with the clock stopped.
- **Done**: "Opgelost in 3:42", the streak, the timeline as tiles (kept = solid ink, changed = hatched square) and one
  button, "Kopieer score" (to `core/clipboard`, toast "Score gekopieerd"). **No send button**. To share: paste the score into a reply in the inbox (the
  reply field's long press > Paste; the phone sends it) or on the phone.

As on the web: no checking, no hints, the game ends by itself when the grid equals the solution.

## Clock

The score is the time the puzzle was on screen in the Play view: from Start / Verder until pause, leaving the app (back,
home, power-off: `App::onStop`), another screen on top (`onHide`), or the solve. Light sleep between touches counts (the
puzzle stays visible). Not shown while playing.

## State

The app store (`/data/apps/somdoku.json`): `day`, `sums` (the puzzle's sums: a saved game of another puzzle is dropped),
`cells`, `order`, `changed`, `edits`, `started`, `finished`, `activeMs`, `solved` (the solved days, newest 400, for the
streak). Saved after every move. After a power-off the game comes back paused.

## Score squares in text

The share text holds 🟩 (U+1F7E9) and 🟨 (U+1F7E8). The fonts have no emoji, so `ui::drawText` / `measureText` draw score
squares as boxes of the text's height: 🟩 solid, 🟨 hatched, ⬛ ⬜ 🟥 🟦 🟧 outlined (Woordle uses ⬛). The text itself keeps the
real characters (clipboard, a pasted reply). `ui::Label::keepLineBreaks` keeps a score's lines (the inbox send confirmation).

## Not built

The web game's long-press-and-slide value picker, haptics, the coloured solved grid, the rules page. Woordle (woordle.nl) is
not the user's game and is out of scope.
