# Hacker News

Y Combinator's Hacker News: the story lists, a story's comments and the article it links to as text (task P7-14, D34; the article after "we can't show a reader/text mode
version of the site (via third party service)?"). Read only: no account, no votes, no replies.

Code: `src/apps/hackernews/` (`hackernews_model.*`: the two APIs' answers into stories and a comment tree in reading
order, comment HTML into text, the thread as paragraphs, the saved copy, the reads; unit tested in
`tests/unit/hackernews_test.cpp`. `hackernews_app.cpp`: the list, the story screen, the article screen). Launcher tile "Hacker News", icon
`Icon::Hackernews` (drawn: a Y in a filled square, "Icon" below), order 100 (after Teletekst).

## The list

```
+--------------------------------------+
| <  Hacker News             [refresh] |  title bar
+--------------------------------------+
| [ Top ][ New ][ Ask ][ Show ]        |  which list
|  1 A file system that fits on a      |
|    postcard                          |  six stories a page, 106 px each:
|    412 points · 14 comments · 3 h ·  |  place, title (up to two lines),
|    example.com                       |  points · comments · age · site
|  ...                                 |
|  <            1 / 5              >   |  pager (also a swipe)
|            Updated 13:34             |  status line
+--------------------------------------+
```

- **Thirty stories** of the chosen list, in the site's order: one page of the site, five pages here. Job adverts are
  left out ("Data").
- **Top / New / Ask / Show**: the choice is kept (`section` in `/data/apps/hackernews.json`).
- **A tap on a story** opens its comments. **Refresh** (title bar) reads the list again.
- Opening the app shows the saved copy of the chosen list in the first frame, then reads it again unless the copy is
  under a quarter of an hour old (a read moves the rows under the reader's eyes three or four seconds after the list
  appeared; Refresh is there for sooner). Another list: its copy at once, a read if that is old.
- What changes together is pushed as one region: a list switch (the switcher, the rows, the status line) and the rows
  of a list that was read again are one pass on the panel, not one per row ("Speed and flicker").
- **A read that fails**: the copy stays, the status line says "Saved 13:34: No Wi-Fi: ...". Without a copy the reason
  takes the list's place, with Retry (a Wi-Fi sign for no Wi-Fi, a warning sign for an error of the service).

## A story

```
+--------------------------------------+
| <  Hacker News             [refresh] |
+--------------------------------------+
| A file system that fits on a postcard|  title (up to three lines)
| 412 points · by marit · 3 h · exam...|
|--------------------------------------|
| duty_cycle · 2 h · 2 replies         |  a comment: who, how old, replies below it
| We ship something close to this ...  |
|   golden_shot · 2 h · 1 reply        |  a reply: 16 px further right a level,
|   How do you test the rotation ...   |  up to eight levels
| [Article]  Page 1 of 4  [Next thread]|
+--------------------------------------+
```

- **The text** is the story's own text if it has one (Ask HN), then every comment: answers to the story in the site's
  order (the best first), replies under what they answer, oldest first. Set in the interface's font (Inter 22, bylines
  semibold), about 16 lines a page; a byline never stands last on a page. Code blocks keep their lines and what a line is
  moved in by (less the two blanks the site wants before every line of code).
- **A tap** on the right two thirds or a swipe to the left turns the page, a tap on the left third or a swipe to the
  right turns back (`ui::TextBlock`). A new page is `Hint::Page`: one Fast pass, a Quality clean every tenth page, like
  the reader (DISPLAY.md). The page number under it is ink and goes in the same pass.
- **Next thread** goes to the page of the next answer to the story that starts after this page: the way past a long
  exchange of replies. Gray when there is none.
- **Article** opens the page the story links to as text ("The article"). A story without a link (Ask HN) has **Link**
  there instead: its page on the site (`news.ycombinator.com/item?id=...`) as a QR code for the phone.
- **Refresh** reads the story again; the place in the text stays (by comment, "Opened again").
- While it loads, a line of ink stands in the place of the text ("Loading comments...": nothing gray, so the screen's
  first pass already shows it as it is); when it has no comments or the read fails, a message with a picture (and Retry
  for a failure). A failure while comments are on screen is a toast.
- **The first page does not wait for the page count**: where the pages of 700 paragraphs start takes the worker 1.5 s to
  find; the first page needs none of that and is shown as soon as the comments are read. "Page 1 of 116" and "Next
  thread" follow when the count is there (pages turn from then on).
- A deleted comment that was answered shows as "[deleted]"; one nobody answered is left out. Beyond 1000 comments the
  text ends with "The first 1000 of N comments are shown."

## The article

(The request and the Markdown conversion below are also what the Read app uses for links shared from the phone: apps/reader.md
"Articles", P7-18. They still live in `hackernews_model`: moving them to `apps/common` is a note in STATUS.md.)

```
+--------------------------------------+
| <  example.com             [refresh] |  the site
+--------------------------------------+
| A file system that fits on a postcard|  headings: Inter 28 semibold
| By Marit, January 2026               |
| Most file systems are described in   |  text: Inter 22, **bold** as semibold
| books. This one is described on a    |
| ...                                  |
| [Link]        Page 1 of 4            |
+--------------------------------------+
```

- The page a story links to, as text from a **reader service**: `POST https://r.jina.ai/` with `{"url": "<the link>"}`
  (Jina Reader, no key; headers `X-Retain-Images: none`, `X-Md-Link-Style: discarded`, `Accept: text/plain`). It fetches
  the page and answers a few lines about it (`Title:`, `URL Source:`, sometimes `Warning:`) and the page as Markdown.
  **What leaves the device is the article's address, to that service**, and only when Article is tapped; nothing about
  the user. D34 says why a third party.
- `articleMarked` turns the Markdown into the reader's marked text: headings (`#` and `##` level 2, deeper ones level 3;
  a line underlined with `=` or `-`), paragraphs, list items with a bullet or their number (a list in a list moved in),
  code blocks and table rows as lines that stay lines, `**bold**`; the marks of emphasis, inline code, escapes and
  lines across are dropped. The page's title is put first when the text does not start with a heading; a `Warning:`
  of the service ("Target URL returned error 403") stands above it.
- Set like the comments (Inter 22), headings in Inter 28 semibold; pages turn as in the comments. **Link** shows the
  address as a QR code for the phone, with "Copy link" (the clipboard, so also the phone's when it is shared: D23); a
  link too long for the code (over about 210 characters) gives the story's page on the site.
- **Failures**: the service's own sentence when it gives one ("Domain 'x' could not be resolved"), else "The reader
  answered 503", "No text on the page", "The page is too long" (over 400 KB), or the network's reason; with Retry.
- The article is not kept: without a network "Article not loaded".

## Opened again

The app follows the idle return (D33: a minute without a touch and the launcher is back; it is not exempt, the user
named books and games only). So the story that was open comes back: an app left with a story open (idle return, the
launcher, power-off) and opened again within the hour shows that story at the page it was left at, without the list in
between; back from it gives the list. The place is kept as a comment and how far into it (`open`, `at` in the app's
store), so it is still right when comments came or the order changed meanwhile. Left with the article open: the
article again at its page (`article`), back from it the comments, back again the list. Left from the list: the list.

## Data

Two public services for the site itself, no key, nothing about the user or the device in a request (the article's
reader service is a third: "The article"):

| What | Request |
|---|---|
| the ids of a list, in the site's order | `GET https://hacker-news.firebaseio.com/v0/{top,new,ask,show}stories.json` (the site's own API; up to 500 ids, 4.5 KB) |
| those stories | `GET https://hn.algolia.com/api/v1/search?tags=(story,poll),(story_<id>,...)&hitsPerPage=<n>&attributesToRetrieve=title,url,author,points,num_comments,created_at_i&attributesToHighlight=none` (the site's search service; 34 ids asked, 8 KB) |
| one story: its own text, its direct comments in the site's order | `GET https://hacker-news.firebaseio.com/v0/item/<id>.json` |
| every comment of a story | `GET https://hn.algolia.com/api/v1/search?tags=comment,story_<id>&hitsPerPage=1000&attributesToRetrieve=author,comment_text,parent_id,created_at_i&attributesToHighlight=none` (flat: `objectID`, `parent_id`; 290 KB for 780 comments) |

Why two (D34): the first knows the order but answers one item a request (thirty stories would be 31 requests, a thread
of 200 comments 201); the second answers many at once but in its own order. So a list is two requests and a story is
two, each pair one job on the worker (`ctx.transport()`), one change of the screen.

- **One handshake a story, not two**: the device keeps one TLS connection, and a request to another host is a new
  handshake (0.9 s). A story's two requests do not need each other's answer, so the first goes to the service the last
  request went to (after a list: the search, so the comments first, then the item).

- A **job advert** is in the list of ids but unknown to the search: 34 ids are asked so that thirty stories remain.
- **The tree** is made from the flat hits without recursion (`parseComments`): a thread can be fifty replies deep and the
  worker's stack is 16 KB. A comment whose parent is not among the hits is left out.
- **Comment text** is HTML (`<p>`, `<i>`, `<a>`, `<pre><code>`, character references): `markedText` makes it the reader's
  marked text (a paragraph a line), cleaned by `core/text_clean` (only what the fonts can draw, long words cut).
- **Limits**: a list answer over 64 KB or a comment answer over 500 KB is refused ("Unexpected answer"; the device takes
  512 KB at most).

## Saved copies

- Each list read is kept in `/data/cache/hackernews/<top|new|ask|show>` (the time, then per story id, title, link,
  author, points, comments, time; about 6 KB), written on the worker.
- **Comments are not kept** (a thread is 20 to 300 KB): without a network a story shows "Comments not loaded" with Retry.
- Nothing is read in the background, on a timer or from the launcher.

## Memory and threads

- Parsing, building the tree and the paragraphs, and finding the pages (`ui::paginate` with its own metrics canvas, as the
  reader does) run on the worker; the screen gets the paragraphs and the page starts in one step (`TextBlock::setChapter`).
- The JSON document of the comments is freed before the tree is built; the paragraphs move into the text block.

## Icon

`assets/icons-drawn/hackernews.txt`: a Y in a filled square with round corners on a 64 x 64 grid (the site's mark: an
orange square with a white Y), made by a few lines of Python (a rounded square minus three thick line segments) and kept
as text like the Teletekst logo (UI.md "Icons"). The site's mark: for this private device only.

## Tests

- Fixtures: `tests/fixtures/make-hackernews-fixtures.py` writes four lists, the story search answers, four items and
  three comment answers in the services' shapes. **Stories, names and comments are made up**; nothing of the real site is
  in the repository. Times count back from the moment the UI scripts start, so the ages on the screenshots stay.
- UI scripts: `hackernews` (the list and its pages, a story's pages, Next thread, the article and its pages, the link
  sheet, a story with its own text, one without comments, one whose comments and article fail, the other lists),
  `hackernews-offline` (nothing saved, no network), `hackernews-saved` (saved lists without Wi-Fi),
  `hackernews-resume` (the story and the article that were open come back).
- Unit tests: addresses, ids, stories in the order of the ids, an item, comment HTML, the tree (order, depth, replies,
  deleted, orphans, a thousand deep), ages and lines, paragraphs and margins, a place that survives a new read, the saved
  copy, the reads through a transport that answers from the fixtures and one that fails, the order of a story's two
  requests, Markdown to marked text, the reader's answer, request and errors.
- A look at the real site without the device: `.pio/build/sim/program --app hackernews --fixtures none` (libcurl).

## Speed and flicker (camera session, 2026-11-18)

Measured with the firmware's
`[lat]` line (tap to the update handed to the panel driver) and `tools/cam.py around` (what the panel shows, DISPLAY.md
"Camera on the panel"); numbers of one session.

| Tap | first build | now | what changed |
|---|---|---|---|
| list page turn | 415 ms, three passes (the arrow pressed, the bar, the rows) | **105 ms, one pass** | the pager's pressed look waits like a button's; text is measured from the glyph cache; the ellipsis cut is found by halving |
| another list (Top / New) | 330 ms, three passes (one of them in Text mode: a gray icon) | **120 ms, one pass** | the switcher's highlight waits like a button's; one region; no rebuild of the whole screen |
| open the app | 340 ms | 270 ms (cold glyphs) | measuring |
| open a story | 350 ms to "Loading comments...", its settle visible | 160 ms, settle not visible | a line of ink instead of a gray icon |
| a story's first page of comments | 6.4 s (237 comments, Wi-Fi off: join 1.9 s, both requests with two handshakes 3.0 s, the page count 1.5 s) | **2.1 s** with Wi-Fi up, **4.3 s** with Wi-Fi off (22 and 26 comments; join 1.4 s, a handshake 0.7 s, the requests 0.9 and 1.0 s): all of it network | the first page does not wait for the page count; after a list a story costs one handshake, not two. Not the same stories before and after: the parts are what compare |
| comment / article page turn | 85-100 ms, two passes (page, then the page number with a settle) | **110 ms, one pass**, no settle | the page number is ink and goes with the page |

On the panel (camera): a page turn is visible 0.38 s after the tap and finished at 0.51 s (the camera's own delay of
about 0.2 s included), one Fast pass, the white around the text does not darken (under 1 % of contrast); the settle
passes after a list or screen change show no visible change. Where the time went in the first build:

- **A pressed look in flight**: the pager's arrow (and the switcher's highlight) were drawn on the finger going down;
  the page that followed 60 ms later had to wait for that small waveform: 150 ms. Both now wait `kPressDelayMs` like a
  `Pressable`, so a quick tap goes straight to its result (`ui/list.cpp`, `ui/inputs.cpp`: every paged list and every
  `Segmented` in the system).
- **Measuring text**: wrapping a title measures it a dozen times, and `ui::ellipsize` measured a line that does not fit
  once per character (30 ms for one line under a title). `ui::measureText` now sums the glyph cache's metrics, the same
  sum LovyanGFX makes (every baked character in every face compared in `tests/unit/text_fast_test.cpp`), and the cut
  is found by halving. Row drawing 140 ms -> 35 ms a page.
- **Several regions**: three regions that change together are three pushes that wait for each other (280 ms); as one
  region 70 ms.
- **The network** is what is left of opening a story: Wi-Fi join 1.4-1.7 s when it was off (it is, 20 s after the last
  read: POWER.md), a TLS handshake 0.8-0.9 s a host, the requests 0.8-1.1 s each.

Found on the way, in `ui/textblock` (the gap around a bold word in a line of Inter): LovyanGFX measured a glyph's
fractional advance rounded and drew it cut off, about half a pixel a character. First worked around in the text block
(`ui::penAdvance`, gone again), then fixed for every Inter string by M5GFX patch 5 (P7-15, D24, UI.md "Type"): Inter
is now set as wide as the font means, so the fixture thread is five pages where it was four.

## On the device

Flashed and run on 2026-11-18 with injected taps, frame dumps (`out/dev-hn/`), the log and the camera:

| | |
|---|---|
| TLS to the three services | works (all chains end at Google Trust Services roots, among the firmware's 44, D31) |
| the Top list | thirty live stories in the frame dump |
| a story with 236 comments | 710 paragraphs, 116 pages; the item 0.92 s, the comments 1.14 s (0.25 s of it the body) |
| an article | 145 paragraphs, the reader service 1.6 s |
| internal heap | 115 KB free with the story open, lowest 79 KB; 115 KB after leaving (nothing kept) |
| stacks left | UI thread 2.8 KB of 8, worker 10.5 KB of 16 |
