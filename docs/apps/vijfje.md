# Vijfje

A daily five-letter word game in Dutch: one word a day, six tries. PaperOS's own game in the genre of the daily word
puzzles. Task P7-9; with a handover text of that web game.

## What it is not (DECISIONS.md D26)

It is **not woordle.nl**. The request was that game with the PaperOS look; what was built keeps the genre and leaves out
what belongs to that game: its name, its list of daily words in its order (so its puzzle of the day and its puzzle
number), its texts, its colours. Consequences the user should know:

- The word of the day is **not** the one the web game has that day, and the number in the title is Vijfje's own.
- A shared score has the same shape as the genre's (`<name> <number> <tries>/6`, an empty line, rows of 🟩 🟨 ⬛), but the
  name is "Vijfje": woordscore.nl will not file it under the web game.
- The name is one constant (`vijfje::kName`); the app id and the store file are `vijfje`.

## Rules

- Guess the word of five letters in six tries. Type with the keys, Enter plays the row, Backspace removes a letter.
- A guess must be in the word list: otherwise a toast "Onbekend woord" and the row is not used up. Fewer than five
  letters: "Te weinig letters".
- After each guess every letter gets a mark: right place, in the word at another place, not in the word. A letter that
  is in the word once is marked once: right places first, then other places from left to right while the word has that
  letter left (`marksOf`, unit-tested: `label` against `fabel` marks the first L as not in the word).
- The keys show the best mark each letter got so far.
- Won on the guess that is the word; lost after the sixth. Then the result, "Kopieer score" and "Statistieken".
- No hard mode, no settings.

## Look (no colours on this panel)

| Mark | Tile | Key |
|---|---|---|
| right place | solid ink, paper letter | solid ink, paper letter |
| another place | hatched ring around a paper middle, ink letter | the same, smaller |
| not in the word | gray(9) frame, gray(5) letter | 1 px gray(9) outline, gray(5) letter |
| typed, not played | 2 px ink frame, ink letter | |
| empty / not tried | gray(9) frame | 2 px ink outline |

Solid = 🟩, hatched = 🟨, outline = ⬛: the same three looks `ui::drawText` uses for score squares in text. All three are ink
and paper only, so the Fast pass shows the final look. A legend line under the board says what they mean (it never
changes, so it is never refreshed).

## Screens

- **Game** (`GameScreen`): title "Vijfje 431" (the puzzle number), the board of 6 x 5 tiles (72 px), the legend, the keys
  (three rows, QWERTY, Enter and Backspace in the last row as icons; 45 x 72 px keys, a tap in a gap goes to the nearer
  key). A typed letter refreshes its tile only; a played row refreshes the row, and 30 ms later the keys in their own push.
  When the game is over the keys make way for "Geraden in 3 beurten" + the streak, or "Niet geraden" + "Het woord was:
  SPONS", and the buttons "Kopieer score" (to `core/clipboard`, toast "Score gekopieerd") and "Statistieken".
  With the clock never set: "De klok is nog niet gezet", no board.
- **Statistieken** (`StatsScreen`, also the info button in the title bar): played, % guessed, streak, longest streak;
  games per number of tries as bars (1 to 6 and X for not guessed; today's game is the solid bar); the rules in three lines
  and the legend.

## The day

The day is the device's local date (`net.timezone`): a new word at local midnight, nothing comes from a server.
`dayOf(date)` = whole days since 2026-11-16 (day 0, puzzle 1); `answerOf(day)` = the answers list at `day` modulo its
length (it wraps after 688 days and works for days before the first). The screen checks the date again each time it is
shown (back from the statistics); an app left open over midnight keeps yesterday's board until then.

## Words

`assets/vijfje/answers.txt` (688 common words: these can be the word of the day) and `assets/vijfje/extra.txt` (1566
more words that are accepted as a guess), both written for PaperOS, not taken from another game or a dictionary file.
`tools/gen-vijfje-words.py` checks them (a-z, five letters, no answer twice) and writes `src/apps/vijfje/words_data.cpp`:
all 2254 words sorted in one string (binary search, about 11 KB of flash) and the answers in a fixed shuffled order as
indices into it (so the source does not show next week's words to someone reading it).

- Adding a word to `extra.txt` is always safe. **Adding or removing an answer changes which word falls on which day**
  (the shuffle is over the sorted list), also for today's saved game: the saved game is dropped when its word is not
  today's any more.
- **The list is small.** A Dutch five-letter dictionary with inflections has several thousand more words, so a real word
  can get "Onbekend woord". The cure is a bigger `extra.txt`, for instance from an open word list (OpenTaal); nothing was
  downloaded for this (it needs the owner's go-ahead).
- The handover named `https://woordscore.nl/woordlijst.txt` (844 answers + 5616 guesses). On 2026-11-16 that address
  answered 241 bytes: 10 + 30 words. Not used.

## State

The app store (`/data/apps/vijfje.json`): `day`, `answer`, `guesses` (comma-separated), `recorded` (today's result is in
the statistics), and the statistics `played`, `won`, `streak`, `maxStreak`, `lastWon` (day number), `turns` (six counts).
Saved after every guess; the letters typed in a row that was not played are not kept. The streak counts days won in a
row: a lost or a missed day ends it (`streakOn`).

## Tests

- `tests/unit/vijfje_test.cpp`: marks (double letters), key marks, days and numbers, the word list (sorted, a-z, every
  answer a word, each answer once per round, wrap), the share text byte for byte, statistics and streak.
- `tests/ui/vijfje.script` (a win in three with all marks, unknown word, too few letters, copy, statistics, restart),
  `vijfje-lost.script` (resume after a restart, six wrong guesses), `vijfje-launcher.script` (page 2 of the launcher).
  The scripts run on 2028-01-20 and -21 (days 430 and 431), so no golden shows a word of the coming year.
  Key positions in a script come from `debugState()` (`keys=q49,708 ...`; `+` is Enter, `-` is Backspace).

## Not built

A countdown to the next word (the panel does not tick), colour themes, high contrast, a large keyboard, hard mode.
