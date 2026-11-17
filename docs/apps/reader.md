# Read (e-book reader)

Reads EPUB and plain-text books from the SD card (`/sd/books/`). The most used app: it sets the
bar for refresh quality and battery life.

## Screens

### Library

- Title "Library". Rows (`ListItem`, 96 px): title, author, progress ("42%" trailing), sorted by
  last opened. 7 per page with `Pager`. Covers are not shown in the list (gray images are slow).
- The most recently opened book is shown as a larger card at the top of page 1 (not repeated in the list; no card
  while nothing was opened): cover thumbnail (`Hint::Image`, 120x180; a framed book icon until P4-5), title, author,
  progress bar, "Continue". Page 1 then holds 5 rows, the other pages 7. The list is rebuilt from a scan on the
  worker each time the screen is shown (so progress and order are current after reading).
  **One worker, so order matters (P4-6, device)**: when the app resumes into the open book, the library under it is not scanned
  until it is shown (the scan and the cover decode held the book's open back: 2.45 s instead of 0.14 s); the card's cover is
  decoded once per book, not on every return (the decode of a 220-270 KB JPEG kept the worker busy for 2 to 3 s, and a book
  tapped meanwhile opened that much later); after a first open the cover is written to the cache after the first page's
  chapter is loaded (written before it, the page came 1.9 s late and taps were dropped meanwhile).
- Title and author come from the book's cache (`meta.json`, once it was opened) or from the file name
  "Title - Author.ext".
- Empty state: "No books yet. Copy .epub or .txt files to the books folder on the SD card."
- First open of a book: "Preparing book..." while the worker indexes it (below), then the page.

### Page

```
(no StatusBar, no TitleBar: the whole 540x960 is the page)
margins: 40 px left/right (normal), 48 top, 56 bottom
footer (caption, ink, kept by the user; goes out with the page, Hint::Page, 2026-11-16):   chapter title ............ 123 / 450      battery icon if < 20%
```

Touch zones:

| Zone | Action |
|---|---|
| right 40% of the width, or swipe left | next page |
| left 25%, or swipe right | previous page |
| centre 35% | toggle the menu overlay |

Menu overlay (a `Sheet` from the bottom; there is no TitleBar over a full-screen page, the way out is the
"Library" button): book title, "Text size" (Segmented S/M/L/XL), "Go to" (LevelBar by 10%, with 10 20 ... 100 printed under the ten boxes, + Stepper by page, "Page 21 of 34"),
and a row of three buttons: Contents (a chapter list screen: seven rows per Pager page, "reading now" on the current chapter,
the page where each chapter starts; a tap jumps), Clean (forces the Quality clean pass) and Library. The page screen is `fullScreen()` (docs/APPS.md): 540 x 960, no chrome. Changing the font size saves the position, paginates the whole book again for the new size (instant when that
index is cached; "Preparing book..." otherwise) and opens on the page that holds the saved position. The size is
remembered (`ctx.store` key `font`, 0..3).

### Refresh behaviour

Page turn = `Hint::Content` over the text area: Fast pass at once (~0.5 s), Text settle after
600 ms of no input, Quality clean when the ghost budget says so (about every 8 to 10 pages),
done at a page turn, never mid-page. Requirements checked by `tests/ui/reader.script`:
`expect-no-quality` across 5 page turns.

## Formats

| Format | Support |
|---|---|
| `.txt` | UTF-8 (Windows-1252/Latin-1 fallback). Blank line = paragraph, hard-wrapped lines are joined. `_word_` is italic, a short line starting with CHAPTER / BOOK / PART ... is a heading. Project Gutenberg header and licence are cut. One "chapter" (parts of at most 512 KB for huge files); title and author from "Title - Author.txt", else the Gutenberg header. Task P4-1 (done). |
| `.epub` (2 and 3) | Unencrypted. Text, paragraphs, headings (h1-h3), bold, italic, line breaks, block quotes, lists as indented paragraphs, horizontal rule. Cover image. Task P4-3/4. |
| not supported in v1 | DRM, images inside chapters (not built: they leave no placeholder, only real alt text such as a caption stays; inline pictures are a later feature), tables (each cell is a line, the lines of a row stay together), CSS, footnote popups, right-to-left |

## Pipeline

```
book.epub (zip)
  1. zip directory        core/zip: read the central directory, inflate entries with tinfl
  2. META-INF/container.xml -> OPF: title, author, cover item, spine (chapter order), TOC (nav/ncx)
  3. per chapter: XHTML -> token stream        apps/reader/xhtml: tiny SAX-style tokenizer,
                                                entity decoding (&amp; &#8217; &nbsp; ...)
  4. tokens -> paragraphs with style runs      {text, bold, italic, heading level, indent}
  5. paragraphs -> lines -> pages              apps/reader/paginate: greedy line breaking on
                                                font metrics; no hyphenation in v1
  6. draw the page                             ui::TextBlock
```

Steps 1-5 for the **whole book** run once on the worker when a book is first opened (or its
font settings change) and produce the cache below. Opening a cached book reads only the page
index and the current chapter. While it runs the page screen says "Preparing book..." (with a percentage after
1.5 s); leaving the screen cancels the job (the worker checks a flag between chapters).

What the XHTML converter (`apps/reader/xhtml`) does: block elements (p, div, h1-h6, li, blockquote, table rows,
...) start paragraphs; `<br>` also starts one (a `<br>` makes a "soft" paragraph, marked 0x06 in the cache format: no first-line indent and no gap above, so verse, addresses and title pages stay together);
b/strong = bold, i/em/cite = italic; h1 = level 1, h2 = 2, h3-h6 = 3; lists get a bullet or "1." prefix;
`<hr>` = "* * *"; images and svg leave nothing (real alt text stays as text, so a cover page has no text and is dropped); each table cell is its own soft line;
style, script, head are ignored; white space collapses; control bytes are dropped so a book cannot forge marks.
Chapters follow the table of contents: a spine document with several contents entries that point at anchors (`#id`) inside
it is cut at those anchors (the converter records where each `id` / `name` lands), text before the first anchor is a
"Beginning" chapter; a document with one entry (or none) is one chapter. A chapter is stored as a byte range of its converted
document (`from`, `to` in meta.json). Documents over 512 KB of text are split into "Title (2)", ... Chapters with no text are
dropped. The footer shows the contents title of the chapter; chapters named only from their first heading (no contents entry)
and the "Beginning" part show the book title.

Memory: one chapter at a time. A chapter larger than 512 KB of text is split at paragraph
boundaries into virtual chapters.

## Cache (`/sd/books/.paperos/<id>/`)

`<id>` = first 12 hex chars of a hash of file name + size + mtime.

| File | Content |
|---|---|
| `meta.json` | title, author, chapter list (title, and for EPUB the source href and part), cover path inside the zip |
| `ch<N>.txt` | **TXT books only**: chapter N as our own compact text format: one paragraph per line, style marks as control bytes (0x01 bold on, 0x02 bold off, 0x03 italic on, 0x04 italic off, 0x05 + level = heading, 0x06 at the start of a line = continues the previous block after a `<br>`). An EPUB's chapters are converted from the EPUB itself each time one is opened (about 0.1 s) and while paginating: the card writes at only about 90 KB/s, so caching 1 MB of text cost 8 s of a 26 s first open. |
| `pages-<layoutKey>.idx` | binary: for each page, chapter index (u16) and byte offset into `chN.txt` (u32). `layoutKey` = font size + margins + line spacing |
| `cover.jpg` / `.png` | the cover copied out of the EPUB as it is (decoded by M5GFX when drawn); written in the background after the book is open |
| `progress.json` | `{ "chapter": 3, "offset": 18234, "percent": 42, "opened": 1763200000 }` |

Position is stored as chapter + paragraph + byte offset inside the paragraph (`TextPos`, not a page number) so it
survives font changes; `progress.json` is `{ "chapter", "para", "byte", "percent", "opened" }`. Progress is saved
on every 5th page turn and when the page screen is destroyed (back, home, power-off).
The page index file is `"PIX1"`, a u32 page count, then per page u16 chapter, u32 paragraph, u32 byte
(little endian). `layoutKey` = face, column width and height, line spacing, indent, paragraph gap.

## Tests

- Unit (`tests/unit/`): zip reader on the small fixture EPUBs (`tests/fixtures/make-epub.py` builds `fixture3.epub`: EPUB 3 with nav, cover,
  deflated chapters, %20 in an href; `fixture2.epub`: EPUB 2 with NCX, stored chapter), damaged zips (cut, bit flips, wrong sizes and CRC, random damage); XHTML tokenizer (entities, nested
  tags, malformed input must not crash); pagination (no line wider than the column, page offsets
  strictly increasing, last page ends at chapter end, re-pagination keeps position).
- UI: `tests/ui/reader.script` runs against a **generated** fixture book (built by a script in
  `tests/unit/fixtures/`, committed), so goldens never depend on downloads. Shots: library,
  first page, after 3 turns, menu.
- Real books for manual testing: `tools/fetch-books.sh` downloads five public-domain EPUBs and
  one TXT from Project Gutenberg into `sim-data/sd/books/` (not committed): Pride and
  Prejudice, Sherlock Holmes, Frankenstein, Alice in Wonderland (EPUB3), Max Havelaar (Dutch,
  accented characters), The Time Machine (TXT). Every one of them must open, paginate to the
  end without a crash, and show its title and author (task P4-4 acceptance).
- Getting books onto the device card: `tools/push.py` over USB (task P4-0), or take the card
  out and copy the files into `/books/` with a computer.
- Device check: page turn time (target under 0.6 s to readable text), ghosting after 10 turns,
  open time for a cached 1 MB EPUB (target under 1.5 s).
  **Measured 2026-11-16 (P4-6, firmware with the three changes above)**: page turn 109 ms from the tap to the panel driver (median of
  30, slowest 150 ms: the Quality clean of every 10th page), on the panel about 0.5 s after the tap (camera, DISPLAY.md). Cached
  open 30-230 ms, the page's text on screen 0.7-1.4 s after the tap in the library (all six books; the largest is 609 KB, no 1 MB
  EPUB is on the card). First open (convert + paginate, "Preparing book..." with a percentage): Pride and Prejudice 609 KB, 1577
  pages 12.5 s; Frankenstein 440 KB, 922 pages 9.8 s; Max Havelaar 405 KB, 1667 pages 15.7 s; Sherlock Holmes 312 KB, 1240 pages
  9.8 s: about 6-9 ms per page, most of it layout (text measuring is 40-55 % of that), so a 1 MB EPUB would take 20-25 s once.
  The page's text follows 0.3-0.4 s after the book is ready. Ghosting: DISPLAY.md "Reader ghosting".

## Later (not v1)

Hyphenation, images in chapters, bookmarks and highlights, Wi-Fi book upload page (task P7-4),
dictionary lookup, reading statistics.

## Books in folders (2026-11-16, Files over Wi-Fi)

The library looks into the folders inside `/sd/books`, three levels deep (names starting with a dot are skipped), and shows
one flat list; a book's `file` is its path inside the books folder ("Classics/Alice.epub"). A book's id comes from its name
without the folder (and its size and time), so a book moved into or out of a folder keeps its cache and reading position; the
cached meta takes the new place when it opens. Unit test "books in folders are found ...".
