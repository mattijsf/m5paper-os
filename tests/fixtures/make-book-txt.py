#!/usr/bin/env python3
"""Writes the reader's fixture book (a plain text file, deterministic) so UI goldens never depend on downloads.

  python3 tests/fixtures/make-book-txt.py "tests/ui/data/reader/sd/books/The Fixture Book - Ann Example.txt"

Content: hard-wrapped paragraphs (like Project Gutenberg), CHAPTER headings, _italic_ words, accents,
a long word, a short line. Not literature: it only has to paginate the same way every time.
"""
import sys, textwrap

WORDS = ("the quiet river runs past old stone houses where children play and the baker opens his shop before dawn "
         "she said that café lights flicker over the müller bridge señor nobody knew why the clock stopped at noon "
         "a long road winds through hills of heather and gorse while the wind carries distant bells").split()

def lcg(seed):
    while True:
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        yield seed >> 8

def paragraph(rng, sentences):
    out = []
    for _ in range(sentences):
        n = 6 + next(rng) % 14
        words = [WORDS[next(rng) % len(WORDS)] for _ in range(n)]
        if next(rng) % 5 == 0:
            words[1] = "_" + words[1] + "_"
        s = " ".join(words)
        out.append(s[0].upper() + s[1:] + ".")
    return " ".join(out)

def main(path):
    rng = lcg(7)
    lines = ["*** START OF THE PROJECT GUTENBERG EBOOK THE FIXTURE BOOK ***", ""]
    for ch in range(1, 5):
        lines += ["CHAPTER %s." % ["I", "II", "III", "IV"][ch - 1], ""]
        for p in range(14):
            text = paragraph(rng, 2 + next(rng) % 5)
            if ch == 2 and p == 3:
                text += " Supercalifragilisticexpialidocious" + "x" * 60 + " ends here."
            lines += textwrap.wrap(text, 70) + [""]
        if ch == 3:
            lines += ["A short line, alone.", ""]
    lines += ["*** END OF THE PROJECT GUTENBERG EBOOK THE FIXTURE BOOK ***"]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

main(sys.argv[1])
