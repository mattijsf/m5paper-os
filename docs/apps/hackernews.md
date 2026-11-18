# Hacker News

Y Combinator's Hacker News: the story lists and a story's comments (task P7-14, D34). Read only: no account, no votes, no replies.

Code: `src/apps/hackernews/` (`hackernews_model.*`: the two APIs' answers into stories and a comment tree in reading
order, comment HTML into text, the thread as paragraphs, the saved copy, the reads; unit tested in
`tests/unit/hackernews_test.cpp`. `hackernews_app.cpp`: the list and the story screen). Launcher tile "Hacker News", icon
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
  under five minutes old. Another list: its copy at once, a read if that is old.
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
| [Link]     Page 1 of 5  [Next thread]|
+--------------------------------------+
```

- **The text** is the story's own text if it has one (Ask HN), then every comment: answers to the story in the site's
  order (the best first), replies under what they answer, oldest first. Set in the interface's font (Inter 22, bylines
  semibold), about 16 lines a page; a byline never stands last on a page. Code blocks keep their lines and what a line is
  moved in by.
- **A tap** on the right two thirds or a swipe to the left turns the page, a tap on the left third or a swipe to the
  right turns back (`ui::TextBlock`). A new page is `Hint::Page`: one Fast pass, a Quality clean every tenth page, like
  the reader (DISPLAY.md).
- **Next thread** goes to the page of the next answer to the story that starts after this page: the way past a long
  exchange of replies. Gray when there is none.
- **Link** shows the story's address as a QR code for the phone, with "Copy link" (the clipboard, so also the phone's
  when it is shared: D23). A story without a link, and one whose link is too long for the code (over about 210
  characters), gives its page on the site (`news.ycombinator.com/item?id=...`).
- **Refresh** reads the story again; the place in the text stays (by comment, "Opened again").
- While it loads, when it has no comments and when the read fails, a message stands in the place of the text (with Retry
  for a failure). A failure while comments are on screen is a toast.
- A deleted comment that was answered shows as "[deleted]"; one nobody answered is left out. Beyond 1000 comments the
  text ends with "The first 1000 of N comments are shown."

## Opened again

The app follows the idle return (D33: a minute without a touch and the launcher is back; it is not exempt, the user
named books and games only). So the story that was open comes back: an app left with a story open (idle return, the
launcher, power-off) and opened again within the hour shows that story at the page it was left at, without the list in
between; back from it gives the list. The place is kept as a comment and how far into it (`open`, `at` in the app's
store), so it is still right when comments came or the order changed meanwhile. Left from the list: the list.

## Data

Two public services, no key, nothing about the user or the device in a request:

| What | Request |
|---|---|
| the ids of a list, in the site's order | `GET https://hacker-news.firebaseio.com/v0/{top,new,ask,show}stories.json` (the site's own API; up to 500 ids, 4.5 KB) |
| those stories | `GET https://hn.algolia.com/api/v1/search?tags=(story,poll),(story_<id>,...)&hitsPerPage=<n>&attributesToRetrieve=title,url,author,points,num_comments,created_at_i&attributesToHighlight=none` (the site's search service; 34 ids asked, 8 KB) |
| one story: its own text, its direct comments in the site's order | `GET https://hacker-news.firebaseio.com/v0/item/<id>.json` |
| every comment of a story | `GET https://hn.algolia.com/api/v1/search?tags=comment,story_<id>&hitsPerPage=1000&attributesToRetrieve=author,comment_text,parent_id,created_at_i&attributesToHighlight=none` (flat: `objectID`, `parent_id`; 290 KB for 780 comments) |

Why two (D34): the first knows the order but answers one item a request (thirty stories would be 31 requests, a thread
of 200 comments 201); the second answers many at once but in its own order. So a list is two requests and a story is
two, each pair one job on the worker (`ctx.transport()`), one change of the screen.

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
- UI scripts: `hackernews` (the list and its pages, a story's pages, Next thread, the link sheet, a story with its own
  text, one without comments, one whose comments fail, the other lists), `hackernews-offline` (nothing saved, no
  network), `hackernews-saved` (saved lists without Wi-Fi), `hackernews-resume` (the story that was open comes back).
- Unit tests: addresses, ids, stories in the order of the ids, an item, comment HTML, the tree (order, depth, replies,
  deleted, orphans, a thousand deep), ages and lines, paragraphs and margins, a place that survives a new read, the saved
  copy, the reads through a transport that answers from the fixtures and one that fails.
- A look at the real site without the device: `.pio/build/sim/program --app hackernews --fixtures none` (libcurl).

## On the device

Flashed and run on 2026-11-18 with injected taps, frame dumps (`out/dev-hn/`) and the log:

| | |
|---|---|
| TLS to both services | works (both chains end at Google Trust Services roots, among the firmware's 44, D31) |
| the Top list | thirty live stories in the frame dump |
| a story with 236 comments | 710 paragraphs, 116 pages; the item 0.92 s (a new TLS connection: the other host), the comments 1.14 s (0.25 s of it the body) |
| internal heap | 115 KB free with the story open, lowest 79 KB; 115 KB after leaving (nothing kept) |
| stacks left | UI thread 2.8 KB of 8, worker 10.5 KB of 16 |
