# Teletekst

NOS Teletekst, page by page (task P7-12, D32). Read only; nothing about the
user or the device is sent anywhere.

Code: `src/apps/teletekst/` (`teletekst_model.*`: the page JSON into a grid of cells, page references, the link a touch
means, the saved copy; unit tested in `tests/unit/teletekst_test.cpp`. `teletekst_app.cpp`: the page view, the buttons, the
number pad). Launcher tile "Teletekst", icon `tv`, order 99 (after the weather: the tenth tile of page 1).

## The screen

```
+--------------------------------------+
| <  Teletekst               [refresh] |  title bar
+--------------------------------------+
|                     NOS Teletekst 104|
|  the page: 40 x 24 characters,       |  520 x 624 px (13 x 26 a character)
|  10 px left and right                |
|              Updated 13:34           |  status line
| [nieuws] [binnenl.] [buitenl.] [sport]  the page's four coloured links
| [103]   <   [ 104 ]   >   [105]      |  previous page, subpage arrows, number pad, next page
+--------------------------------------+
```

- **The page** is the grid a television shows, rows 0 to 23. Row 24 holds the four coloured links (red, green, yellow,
  cyan on a remote); they are the four buttons below with the same words, so the row is not drawn.
- **A tap on the page** opens a link: the link in the touched cell, else the link of that row nearest to the finger, else
  the nearest in the row above or below (`linkNear`; a row is 26 px, a finger is not that exact). On an index page a tap
  anywhere on a headline opens it. A tap where no link is near does nothing.
- **A swipe** left or right over the page: the next or previous subpage while there is one, else the next or previous page.
- **The row of four** opens the page's own coloured links (`fastTextLinks`); a page with fewer leaves buttons empty.
- **The bottom row**: the previous and next page by number (empty when there is none); between them the page on screen as
  a filled button, "702/2" on a subpage; a tap on it opens the **number pad**. The two arrows beside it are the previous and
  next subpage: drawn only on a page that has subpages, gray at the first and last one.
- **Number pad**: a sheet with 0-9, Delete and Cancel. The third digit opens the page; a first digit of 0 or 9 is not
  taken (pages are 100 to 899).
- **Refresh** (title bar) reads the page again.
- **Screen::sideMargin()** is 10 px for this screen (the grid needs 520 of the 540 px); every other screen keeps 24.

## Colours

The panel has no colour and flat gray costs a second refresh pass (DISPLAY.md "Gray areas cost a flicker"), so the page is
ink and paper only, and the black background of a television is paper:

| On a television | Here |
|---|---|
| text in any colour on black | ink on paper |
| text on a blue, red, magenta or green background (title bars) | paper on an ink bar |
| text on a white, yellow or cyan background | ink on paper |
| text in the colour of its own background (hidden) | nothing |
| mosaic blocks in white, yellow, cyan, blue | solid ink |
| mosaic blocks in red, magenta | two of every three pixel rows ink |
| mosaic blocks in green | every other pixel row ink (Germany and Belgium on the weather map; the Netherlands itself is black there, so paper, inside a cyan coast line) |
| mosaic blocks on an ink bar | paper |

Characters are drawn with `Type::Mono` (JetBrains Mono Medium 22, every character 13 px wide; UI.md "Type") and then cut
to ink or paper at a threshold (`kInkBelow`), like a television's bitmap letters: the Fast pass shows the page exactly as
it is, nothing is dithered and nothing settles. A new page is pushed as `Hint::Page` (one Fast pass, a Quality clean every
tenth page like the reader). Double height, flashing and separated mosaics are not in the service's JSON and not drawn.

## Data

`GET https://teletekst-data.nos.nl/json/<page>` or `/json/<page>-<subpage>` (the JSON the NOS Teletekst site itself
reads; not a documented API, no key, checked on 2026-11-18). No headers of our own, nothing in the address but the page.

| Field | Use |
|---|---|
| `content` | 25 lines of 40 cells: text, `<span class="yellow bg-blue ">` (text colour, background), `<a class="yellow" href="#104">` or `href="#702/2"` (a link, with a subpage), mosaics as `&#xF020;`..`&#xF07f;` (the teletext code in the low 7 bits, a bit per block), named and numbered character references |
| `prevPage`, `nextPage` | the neighbours that exist (`"103"`), `""` if none |
| `prevSubPage`, `nextSubPage` | `"702-2"`, `""` if none; a page with either has subpages |
| `fastTextLinks` | `[{"title":"nieuws","page":"101"}, ...]`, four |

A page that does not exist answers 404 with an empty body. The JSON does not name its own page: the app keeps what it asked
for; a page asked without a subpage that turns out to have them is subpage 1.

## Saved copies and reads

- Every page read is kept in `/data/cache/teletekst/p<page>[-<sub>]` (the time of the read on the first line, then the
  JSON as it came; 3-9 KB), the last 16 pages (`saved` in `/data/apps/teletekst.json`; older files are deleted).
- **Opening the app**: the page that was open if the app was left less than an hour ago (`page`, `left`), else page 100.
  Its saved copy is drawn in the first frame; it is read again unless the copy is under two minutes old.
- **Going to a page**: a copy under two minutes old is shown without a read (back and forth costs no request). Otherwise
  the number button shows the new number and the status line "Loading 105..." at once, and the page changes when the
  answer is there: one change of the page per tap.
- **A read that fails**: the page's saved copy is shown if there is one, with "Saved 13:34: No Wi-Fi: ..." under it. Without
  a copy the page on screen stays and a toast says why ("Page 199 does not exist", "No Wi-Fi: ..."). With nothing on
  screen at all (first open, no network) the reason takes the page's place with a Retry button.
- Nothing is read in the background, on a timer or from the launcher. Wi-Fi comes up for a read by itself (`ctx.http`).

## Tests

- Fixtures: `tests/fixtures/make-teletekst-fixtures.py` writes pages 100, 101, 104, 702 (two subpages) and the 404 of 199
  in the service's shape. **The text is made up**; no page of the real service is in the repository. The map of 702 is
  our own drawing of the Netherlands (corner points in the script), coloured as the service colours its map: cyan coast
  line and inland water, green neighbours, red numbers on black land.
- A look at real pages without the device: fetch them with curl into a directory outside the repository under the
  fixture names (`GET_teletekst-data.nos.nl_json_702.json`) and run a script with `--fixtures <that directory>`.
- UI scripts: `teletekst` (index, a headline tap, the number pad, the map with its subpage, a page that does not exist, a
  coloured link), `teletekst-offline` (nothing saved, no network), `teletekst-saved` (saved copies without Wi-Fi, the page
  that was open comes back, page 100 after an hour).
- Unit tests: references, markup into cells (colours, links, mosaics, character references, too long, broken), the
  fixture pages, what is not a page, the saved copy, the mono font's 13 px for every character.

## Memory

A parsed page (`Page`) is 8 KB: 1000 cells of 8 bytes. It lives on the heap (`std::shared_ptr<const Page>`, one on screen
and one being parsed at most), never as a local: the UI thread's stack on the device is 8 KB (APPS.md "Threads").
`parsePage` therefore fills its result field by field instead of assigning a fresh `Page{}`.

## On the device

Flashed and run on 2026-11-18 with injected taps, frame dumps and the log (STATUS.md Log):

| | |
|---|---|
| TLS to the service | works (its chain ends at USERTrust RSA, among the firmware's 44 roots, D31) |
| a read, Wi-Fi up | 0.7 s the first request, 38-164 ms after it on the kept connection |
| render of a page | 92-138 ms from the network, 36-38 ms from a saved copy |
| tap to the page pushed | ~0.4-0.5 s from the network, 130 ms from a saved copy |
| internal heap, lowest | 79 KB (idle 83 KB); nothing stays allocated after leaving |
| UI thread stack left | 2.9 KB of 8 |
| frame of page 702 | the simulator's, pixel for pixel (only the time in the status line differs) |

Not seen yet: the panel itself (STATUS.md "Pending device checks"): how the bitmap letters and the 1 px stripes of the
map look, ghosting after pages with ink bars.
