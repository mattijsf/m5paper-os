// Test vectors for the PaperOS port of Somdoku (src/apps/somdoku): runs the web game's own code and writes what it
// computes, so the C++ unit tests can check that the device gives the same puzzle, number and share text.
//
//   node tools/somdoku-vectors.mjs path/to/somdoku > tests/unit/data/somdoku_vectors.json
//
// Reads the somdoku repo only (src/lib/puzzle.js, src/lib/game.js); writes nothing there.
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const repo = resolve(process.argv[2] || `${process.env.HOME}/Development/somdoku`);
const lib = (file) => import(pathToFileURL(`${repo}/src/lib/${file}`).href);
const { hash, rng } = await lib('puzzle.js');
const game = await lib('game.js');

const DAYS = 500;
const days = [];
for (let i = 0; i < DAYS; i++) {
  const day = new Date(Date.UTC(2026, 10, 11 + i)).toISOString().slice(0, 10);
  const p = game.dailyPuzzle(day);
  days.push({
    day,
    number: game.puzzleNumber(day),
    level: game.levelOf(day),
    passes: p.passes,
    attempt: p.attempt,
    rows: p.rows,
    cols: p.cols,
    solution: p.solution.flat(),
  });
}

// rng / hash on their own, for a quick first failure.
const texts = ['', 'a', 'sommetje 2026-11-11', 'niveau 2026-11-11', 'café ☕'];
const hashes = texts.map((text) => ({ text, hash: hash(text) }));
const random = rng(hash('sommetje 2026-11-11'));
const randoms = Array.from({ length: 8 }, () => random());

// Share texts: a trail is replayed as moves [cell, from, to, slip] on an empty 5x5 game.
const E = game.EMPTY;
const plays = [
  { name: 'Somdoku 1', seconds: 222, moves: [[0, E, 1, false], [1, E, 2, false], [2, E, 0, false]] },
  { name: 'Somdoku 12', seconds: 3.9, moves: [[3, E, 4, false], [3, 4, 2, true], [5, E, 1, false], [3, 2, 3, false]] },
  { name: 'Somdoku 99', seconds: 3725, moves: Array.from({ length: 25 }, (_, c) => [c, E, (c % 4) + 1, false]) },
  { name: 'Somdoku 7', seconds: 61, moves: [[10, E, 2, false], [10, 2, E, false], [10, E, 3, false], [11, E, 0, false], [11, 0, 1, true]] },
];
const shares = plays.map(({ name, seconds, moves }) => {
  const trail = game.freshTrail(25);
  for (const [cell, from, to, slip] of moves) game.noteMove(trail, cell, from, to, slip);
  return { name, seconds, moves, marks: game.marks(trail).join(''), text: game.shareText({ name, seconds, trail, n: 5 }) };
});

const clocks = [0, 5, 59.9, 60, 222, 3599, 3600, 3725, 86399].map((s) => ({ seconds: s, text: game.clock(s) }));
const streakTexts = [0, 1, 2, 10].map((n) => ({ n, text: game.streakText(n) }));

process.stdout.write(`${JSON.stringify({ firstDay: game.FIRST_DAY, levels: game.LEVELS, hashes, randoms, days, shares, clocks, streakTexts })}\n`);
