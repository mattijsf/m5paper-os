# Read (e-book reader)

Reads EPUB and plain-text books from the SD card (`/sd/books/`). The most used app: it sets the
bar for refresh quality and battery life.

## Screens

### Library

- Title "Library". Rows (`ListItem`, 96 px): title, author, progress ("42%" trailing), sorted by
  last opened. 7 per page with `Pager`. Covers are not shown in the list (gray images are slow).
- The most recent book is shown as a larger card at the top of page 1: cover thumbnail
  (`Hint::Image`, 120x180), title, author, progress bar, "Continue".
- Empty state: "No books yet. Copy .epub or .txt files to the books folder on the SD card."
- First open of a book: "Preparing book..." while the worker indexes it (below), then the page.

### Page

```
(no StatusBar, no TitleBar: the whole 540x960 is the page)
margins: 40 px left/right (normal), 48 top, 56 bottom
footer (caption, gray(5)):   chapter title ............ 123 / 450      battery icon if < 20%
```

Touch zones:

| Zone | Action |
|---|---|
| right 40% of the width, or swipe left | next page |
| left 25%, or swipe right | previous page |
| centre 35% | toggle the menu overlay |

Menu overlay (`Sheet` from the bottom + a TitleBar at the top with back): font size (Segmented
S/M/L/XL), contents (chapter list screen), "Go to" (LevelBar by 10% + Stepper by page),
"Clean screen". Changing the font size re-paginates the current chapter and keeps the position
(character offset).

### Refresh behaviour

Page turn = `Hint::Content` over the text area: Fast pass at once (~0.5 s), Text settle after
600 ms of no input, Quality clean when the ghost budget says so (about every 8 to 10 pages),
done at a page turn, never mid-page. Requirements checked by `tests/ui/reader.script`:
`expect-no-quality` across 5 page turns.

## Formats

| Format | Support |
|---|---|
| `.txt` | UTF-8 (Latin-1 fallback). Blank line = paragraph. One "chapter". Task P4-1. |
| `.epub` (2 and 3) | Unencrypted. Text, paragraphs, headings (h1-h3), bold, italic, line breaks, block quotes, lists as indented paragraphs, horizontal rule. Cover image. Task P4-3/4. |
| not supported in v1 | DRM, images inside chapters (skipped with a `[image]` mark), tables (rows flattened to lines), CSS, footnote popups, right-to-left |

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
index and the current chapter.

Memory: one chapter at a time. A chapter larger than 512 KB of text is split at paragraph
boundaries into virtual chapters.

## Cache (`/sd/books/.paperos/<id>/`)

`<id>` = first 12 hex chars of a hash of file name + size + mtime.

| File | Content |
|---|---|
| `meta.json` | title, author, chapter list (title, source href), total pages per layout key |
| `ch<N>.txt` | chapter N as our own compact text format: one paragraph per line, style marks as control bytes (0x01 bold on, 0x02 bold off, 0x03 italic on, 0x04 italic off, 0x05 + level = heading) |
| `pages-<layoutKey>.idx` | binary: for each page, chapter index (u16) and byte offset into `chN.txt` (u32). `layoutKey` = font size + margins + line spacing |
| `cover.jpg` / `.png` | extracted cover file as-is (decoded by M5GFX when drawn) |
| `progress.json` | `{ "chapter": 3, "offset": 18234, "percent": 42, "opened": 1763200000 }` |

Position is stored as chapter + byte offset (not page number) so it survives font changes.
Progress is saved on every 5th page turn and in `onStop()`.

## Tests

- Unit (`tests/unit/`): zip reader on a small fixture EPUB; XHTML tokenizer (entities, nested
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

## Later (not v1)

Hyphenation, images in chapters, bookmarks and highlights, Wi-Fi book upload page (task P7-4),
dictionary lookup, reading statistics.
