# Pixelpuzzel

A daily picture puzzle (a nonogram) on the panel. Task P7-22, D41. App id `pixelpuzzle`, tile "Pixelpuzzel" (icon `blocks`), after Hacker
News in the launcher.

## The puzzle

- 10 x 10 cells. Left of each row and above each column the lengths of its runs of filled cells, in order ("2 2" = two
  cells, a gap, two cells; "0" = an empty line). Fill the cells so every row and column has its numbers; a picture
  appears. A row or column that has its numbers already shows them in gray (`ui::disabled`).
- **Solved = every line has its clue**, not "equals the drawing": a grid that fits all clues counts (crosses are empty
  cells). No checking or hints while playing, as Somdoku.
- **The pictures are drawn for PaperOS** (`src/apps/pixelpuzzle/pixelpuzzle.cpp`, 24 of them: a heart, a house, a cat,
  a sailboat, a mitre and a shoe for Sinterklaas, ...). `'#'` filled, `'.'` empty, `'1'..'9'` empty but drawn in gray
  when solved (1 = gray(11) .. 9 = gray(3); lighter does not show on the panel): sky, water, ground. **Every picture, and
  its mirror image, is solvable line by line without guessing**: `tests/unit/pixelpuzzle_test.cpp` runs the line solver
  on each (a new picture that fails needs redrawing: two corners the clues cannot tell apart are the usual cause).
- **The daily choice**: day 1 is 2026-11-20 (`kFirstDay`); the 24 pictures in a fixed order that steps through the list
  (step 7, coprime with 24: neighbours in the list are alike), then all of them mirrored, and so on: a picture comes back
  after 24 days, mirrored, and as drawn after 48.

## Screens (one Screen, three views, as Somdoku)

- **Ready** (also the pause screen): "Pixelpuzzel 3", the day, the streak, Start / Verder, a two-line explanation.
- **Play**: the grid (40 px cells, 4.3 mm on the panel; the screen's side margins are 10 px so the row clues fit, as
  Teletekst), the note ("Nog 7 regels."), and Vullen | Kruisje. The clue area above the grid is as tall as the day's
  longest column clue.
  - **Tap** a cell: the mark of the mode (filled, or a gray cross), or empty if it had that mark.
  - **Swipe** along a row or column (from a cell, at least 80 px): the same for every cell of the stretch, decided by the
    first cell. One refresh for many cells: on e-ink that is the fast way to fill a line.
  - **Long press**: the other mark (a cross while filling, and the other way round).
  - The pause button in the title bar stops the clock.
- **Done**: the picture in 28 px cells with its grays, its name ("Een zeilboot"), "Opgelost in 4:12", the streak, and
  Kopieer score ("Pixelpuzzel 5: Een zeilboot in 4:12", to the clipboard; no send button, as the other games).

## Clock, streak, state

As Somdoku (apps/somdoku.md "Clock"): the time counts only while the grid is on screen. The date, streak and clock text
helpers are Somdoku's (`somdoku::parseDay`, `streak`, `streakText`, `clock`). Without a set clock: "Klok zetten"
(`apps::ClockSetter`). Store `/data/apps/pixelpuzzle.json`: `day`, `name` (a saved game of another picture is dropped),
`cells` (a digit per cell: 0 empty, 1 filled, 2 crossed), `started`, `finished`, `activeMs`, `solved` (newest 400 days).
Saved after every move; after a power-off the game comes back paused.

## Tests

`tests/unit/pixelpuzzle_test.cpp` (clues, every picture solvable, the daily order, the check, the score text);
`tests/ui/pixelpuzzle.script` (day 1, the heart: taps, swipes along rows and a column, crosses, a long press, emptying a
cell, pause and Verder, the solve, Kopieer score); `tests/ui/pixelpuzzle-reveal.script` (day 5 solved earlier, a preset
store: the sailboat with its grays).
