# UI system ("Ink")

One visual language for every app: black on white, large type, line icons, generous spacing,
no motion. Built for a 540x960 portrait panel at 235 ppi (1 mm = 9.25 px) and for the refresh
rules in `DISPLAY.md`.

## Foundations

### Grid and spacing

- Base unit 8 px. Spacing tokens: `s1`=8, `s2`=16, `s3`=24, `s4`=32, `s6`=48.
- Screen margins: 24 px left and right. Content width 492 px.
- Minimum touch target: **72 x 72 px** (7.8 mm). Preferred row height 96 px.
- Corner radius: 16 px for tiles and buttons, 8 px for small chips. Stroke width 2 px.

### Colour

16 grays, `gray(0)` black to `gray(15)` white (`core/gfx.h`). Tokens in `ui/theme.h`:

| Token | Value | Use |
|---|---|---|
| `ink` | gray(0) | text, icons, strokes, filled buttons |
| `paper` | gray(15) | background |
| `secondary` | gray(3) | **metadata only**: times, "Updated 20:21", counts, captions, hints (was gray(5): too light on the panel) |
| `disabled` | gray(5) | disabled text and icons (was gray(9), then gray(7)) |
| `divider` | gray(9) | 1 px separators (was gray(11): barely visible on the panel) |
| `fill` | gray(13) | icon wells, quiet backgrounds |

Anything that changes on touch (pressed, selected, on/off) uses only `ink` and `paper`.

**Limits measured by a person's eyes on the panel (Display lab, Text pattern, 2026-11-15)**: caption size (18 px): gray(3) is the lightest that
reads, anything lighter is hard; body size (22 px): gray(5) is the lightest that reads; 1 px lines: gray(13) is invisible, gray(11) "very thin, too
thin to be usable"; Read S (24 px) is a comfortable reading size. So: caption text is never lighter than gray(3), body text never lighter than gray(5),
1 px lines never lighter than gray(9).

**Content is ink** (rule from the user's first look at the panel, 2026-11-15: "the light gray text is a bit hard to see"). What the user came
to read is always `ink`: message preview lines, mail subjects and excerpts, senders, book authors in the library, values (a room's temperature),
results of an action, the launcher's summary line. `secondary` is only for metadata: times, "Updated 20:21", counts, captions, hints; state lines
such as "On - 51%" may stay secondary. **No text lighter than gray(3); no content text in gray at all.** A content line is also not set in the
caption size (18 px) when the row has room: `ListItem::setSubtitleIsContent(true)` draws the subtitle in ink at the body size (22 px). Where this is
applied: inbox and mail list rows (preview / subject), library rows and the continue card (author), mail detail sender (body size), Home room tiles
(the value), Settings results (Wi-Fi status, tests), the launcher summary line (body size), the sleep screen's author, the reader's "Preparing
book..." line. Still secondary: the reader footer, "x% read", times, status lines ("Updated ..."), Home state lines, Settings row hints, sheet hints,
empty-state hints, captions above controls.

### Type

Fonts are baked to 4-bit anti-aliased bitmaps by `tools/gen-assets.sh` (task P1-6) using
`lv_font_conv`; M5GFX loads that format (`loadFont(data, ft_lvgl)`).

| Token | Font | Size px | Use |
|---|---|---|---|
| `caption` | Inter Regular | 18 | timestamps, hints, status bar |
| `body` | Inter Regular | 22 | list subtitles, paragraphs in apps |
| `bodyStrong` | Inter SemiBold | 22 | list titles, button labels |
| `title` | Inter SemiBold | 28 | screen titles, tile labels |
| `headline` | Inter SemiBold | 40 | big numbers (temperature), launcher clock |
| `read-S/M/L/XL` | Literata Regular | 24 / 28 / 32 / 38 | book text (plus Italic and Bold at M only; other sizes fake them) |

Glyph ranges: U+0020-007E, U+00A0-00FF, U+0100-017F, U+2010-2027, U+20AC. Unknown code points
draw as `?`; emoji come from the emoji atlases (below).
Both fonts are under the SIL Open Font License; their licence files are in `assets/fonts/`
(also `Lucide-LICENSE.txt`, ISC). API: `ui/text.h` (`drawText`, `measureText`, `lineHeight`,
`ellipsize`, `sanitize`, `drawIcon`), tokens: `ui/theme.h`. Pitfalls found in P1-6:

- **Pass the background colour.** M5GFX blends glyph edges between the text colour and an explicit
  background colour (`setTextColor(fg, bg)`); it cannot read the canvas. Without `bg` every glyph
  pixel is solid ink (no anti-aliasing, looks bold). `drawText(..., Anchor, bg)` defaults to paper;
  text on `fill` (icon wells) or on ink needs that colour passed. Text over pictures is not supported.
- **Literata lacks some code points** in U+2010-2027 (it has the dashes, quotes, bullet and
  ellipsis); missing ones draw nothing or `?`.
- Each canvas holds one font: `ui/text.cpp` reloads only when the font changes.
- The specimen sheet (`program --app fonts`, golden `fonts-sheet`) shows every token.

### Icons

Lucide line icons (ISC licence), baked from the Lucide icon font at **32 px** (rows, bars) and
**64 px** (launcher tiles, empty states) through the same font pipeline, so an icon is a glyph:
`ui::icon(canvas, Icon::Sofa, x, y, 32)`. `assets/icons.txt` lists the icon names to include (66 icons on 2026-11-16, `Icon::BookOpen` etc.);
the generator writes `src/gen/icons.h` (`enum class Icon`) and the two icon fonts.
Initial set: arrow-left, chevron-left, chevron-right, house, book-open, library, lightbulb,
lightbulb-off, sofa, utensils, blocks, cooking-pot, lamp-wall-down, lamp-wall-up, car, bed, thermometer,
droplets, fan, wind, message-circle, mail, mail-open, settings, wifi, wifi-off, battery,
battery-low, battery-charging, refresh-cw, moon, power, sun, check, x, plus, minus, clock,
folder, file-text, type, info, triangle-alert, door-open, air-vent.
Weather conditions (P7-11, apps/weather.md): sun, moon, cloud, cloud-sun, cloud-moon, cloud-rain, cloud-rain-wind, cloud-snow,
cloud-lightning, cloud-fog, cloud-hail, wind, umbrella, droplets, sunrise, sunset.
The Home app reuses the names of the icons in the user's Home Assistant dashboard so rooms look familiar.

## Screen anatomy

```
+--------------------------------------+  y=0
| 12:34            wifi  battery 87%   |  StatusBar  56 px (always, drawn by the system)
+--------------------------------------+  y=56
| <  Title                    [action] |  TitleBar   72 px (apps; the launcher has none)
+--------------------------------------+  y=128
|                                      |
|            content area              |  540 x 736 (or 832 without TabBar)
|                                      |
+--------------------------------------+  y=864
|   tab        tab        tab          |  TabBar     96 px (optional)
+--------------------------------------+  y=960
```

- The back arrow (top left, 72x72 target) pops the screen stack; on an app's first screen it
  returns to the launcher. Long-press on it always goes to the launcher.
- There is no hardware navigation (the side button belongs to the power controller).
- The title bar may carry a small picture left of the action (`Screen::setTitleAccessory(width, paint)`, no touches;
  `refreshTitleAccessory()` redraws only it): Somdoku's score tiles. Keep it ink and paper so its push needs no settle pass,
  and redraw it in its own loop pass (a short `ctx.timer`), or it merges with other Feedback rects into one tall rectangle.

## Widgets (`src/ui/`, tasks P2-1 to P2-3)

All derive from `ui::Widget`:

```cpp
class Widget {
 public:
  Rect frame;                                     // set by the parent's layout
  virtual Size measure(int availW) { return {}; } // preferred size
  virtual void layout() {}                        // position children inside frame
  virtual void draw(Canvas& c) = 0;               // draw self (children drawn by container)
  virtual bool onTouch(const Gesture& g) { return false; }   // true = consumed
  void invalidate(Hint h = Hint::Content);        // marks frame dirty in the DisplayManager
};
```

| Widget | Purpose | Notes |
|---|---|---|
| `Column`, `Row`, `Stack`, `Spacer`, `Padding` | layout | `Column` stacks children top to bottom with a gap; a child with `flex=1` takes leftover space |
| `Label` | text, one or more lines | token for style; `maxLines`, ellipsis; `keepLineBreaks` (a line feed starts a new line) |
| `IconView` | icon glyph | 32 or 64 |
| `Button` | primary (filled ink) / secondary (2 px outline) / text | height 72; pressed = inverted |
| `IconButton` | 72x72 target with a 32 px icon | |
| `Tile` | outlined square: 64 px icon + label, optional value and badge; ink and paper only (no gray well: DISPLAY.md "Gray areas cost a flicker"). `Size::Large` (two to a row, 196 high, title-size label: Home) and `Size::Small` (three to a row, 160 high, body-size label: the launcher) | pressed = 4 px border |
| `ListItem` | 96 px row: leading icon, title, subtitle, trailing text / chevron / toggle / badge | divider below |
| `Toggle` | on/off pill 88x48 | on = filled ink with paper knob; pure black/white |
| `LevelBar` | 10 segments, tap a segment to set 10..100% | replaces sliders: dragging is poor on e-ink |
| `Stepper` | minus, value, plus | temperatures, font size |
| `Segmented` | 2 to 4 options | selected = filled ink |
| `Pager` | "3 / 12" with previous/next targets; also handles horizontal swipes | every list longer than one screen uses it |
| `Badge` | filled ink circle/pill with count | unread counts |
| `Sheet` | bottom sheet over the untouched screen, 2 px ink top edge, max 60% height; a tap outside or a swipe down closes it | detail controls, confirmations |
| `Toast` | one line over the status bar for 3 s: an ink band, edge to edge | errors like "No Wi-Fi", "Score gekopieerd" |
| `EmptyState` | 64 px icon + title + hint + optional button | empty lists, errors, loading |
| `Keyboard` | QWERTY, 3 layers (abc / ABC / 123#), key 54x72 | Wi-Fi password, search |
| `TextField` | one line (72 px) or `setLines(n)` wrapped lines; long press = Paste / Copy all / Clear | text input with the keyboard |
| `TextBlock` | paginated rich text (paragraphs, bold/italic) | reader and message/mail detail |

Loading is shown with `EmptyState` ("Loading..." + icon), never a spinner. Stale cached data is
shown immediately with a "Updated 12:30" caption, then replaced when the fetch finishes.

## As implemented (P2-1)

`ui::Root` (a `core::Surface`; not the app-level `pos::Screen` of `core/app.h`) owns the widget tree: `setRoot()` lays it out, `render()` redraws
only dirty widgets (a dirty ancestor covers its children; each is cleared to `bg()`, clipped to its
frame, drawn) and passes the rectangles to the DisplayManager with the hint given to
`invalidate()`. The Os feeds gestures to it. `Widget::bg()` is the colour behind the content
(own, else the parent's, else paper): text is anti-aliased against it. Layout: `Column`/`Row`
(`gap`, cross-axis `Align`), `Stack`, `Spacer(size, flex)`, `Padding`; `flex` on any child shares
the leftover main-axis space in proportion, no pixel lost. `Label` wraps at spaces, breaks long
words, ends the last allowed line with an ellipsis; `IconView` is a 32 or 64 px glyph. Test
screens: `apps/uitest` (`program --app widgets1`).

Gesture details beyond the table: every touch emits Down, then Cancel as soon as the finger is 24 px
away (a pressed widget lets go), then at most one of Tap / LongPress / Swipe*. Down is delivered
to the deepest widget under the finger and bubbles up until a widget consumes it; that widget
gets Tap, LongPress and Cancel; swipes bubble from it (a Pager above a Button sees them).

P2-2 widgets (`ui/controls`, `ui/list`, `ui/tile`): `Pressable` base (pressed on Down with a Feedback
invalidate, `onClick` on Tap, nothing on Cancel; disabled ones swallow the touch), `Button`
(Primary/Secondary/Text, 72 high, min 96 wide; pressed = inverted), `IconButton`, `Toggle` (88 x 48
pill in an 88 x 72 target; pressed = thicker border; Tap flips and calls `onChange`), `Badge`
(`99+` cap), `ListItem` (96 px; leading icon, title, subtitle, trailing text / chevron / Toggle /
Badge; the trailing Toggle is a child widget, so tapping it does not tap the row; pressed = inverted
row), `Tile` (234 x 196 in a 2-column grid; 64 px icon in a `fill` well, label, optional value and
badge; pressed = 4 px border), `Pager` (container: content children above a 72 px `PagerBar`;
arrows and horizontal swipes anywhere in the content call `onPage(page)`; arrows at the ends are
disabled). A Fast pass shows the shrunk changed rectangle only: a Toggle tap pushes about 2 x 4000
pixels. Test screens: `widgets2`, `widgets2rows`, `widgets2list` (`--app`, or `>set screen NAME`).

P2-3 widgets (`ui/inputs`, `ui/panels`): `Segmented` (2-4 options, 72 high; a Down already shows the option
under the finger as selected, Tap commits), `Stepper` (two 72 px IconButtons that grey out at the
limits around the value text; optional formatter), `LevelBar` (10 segments, 10..100 %, tap sets
(k+1) x 10), `EmptyState` (a Column: 64 px icon, title, hint, optional button), `Sheet`, `Toast`.
Overlays: `Root::addOverlay(widget)` puts a widget above the root (it gets touches first; its
frame starts as the full screen and `layout()` may shrink it); `closeOverlay` redraws the area
below (`Root::invalidateRect`) and is safe to call from the overlay's own button handlers (the
widget is destroyed on the next pass). `Widget::transparent` skips the clear-to-bg before drawing
(overlays draw over the screen), `Widget::touchable = false` lets touches fall through (Toast).
`Sheet`: modal widget whose frame is just the panel at the bottom (max 60% of the height, 2 px ink
top edge, grab handle); the screen behind stays untouched (no dimming: a hatch would repaint the whole
panel twice and cost ghost budget). `Widget::modal` makes the Screen send every touch that hits nothing
else to the sheet, so a tap outside closes it without reaching the widgets below; a swipe down closes
it too. Only the sheet's rectangle is invalidated on open and on close. Put controls into
`sheet->content()`. `Root::showToast(text)`: an ink band over the status bar (y 0..56, paper text at the body size, regular weight), 3 s, then the
screen below is restored pixel for pixel (checked: `widgets3-toast-gone` == `widgets3-base`).

P2-7 (`ui/keyboard`, `ui/textblock`):
`Keyboard`: 540 x 288, four rows of 54 x 72 keys; layers abc, ABC (one letter, then back to abc) and 123#;
rows: letters, letters (offset half a key), shift + letters + backspace, 123/abc + comma + space + period +
enter. Events through `onKey(Kind, text)`; `bindKeyboard(kb, field, onEnter)` wires it to a `TextField`
(72 px, optional masking with bullets, shows the tail of long text, caret). A key's pressed look (ink) appears
only after a 120 ms hold, and only the key's rectangle is redrawn (`Widget::invalidateRect`): a quick tap
pushes just the changed text-field pixels (every character key of the 12-character password in
`keyboard.script` stays under 6000 px; shift / layer switches repaint all keys and are not size-checked).
`TextBlock` + pure pagination (`layoutPage`, `paginate`, `pageIndexOf`, `parseMarked`): a position is
(paragraph, byte offset), so it survives a change of font size or margins. Greedy line breaking at spaces
(a word is the pieces that touch, so a style change or punctuation in its own run never opens a gap), long
words are cut by characters, first-line indent for body paragraphs after body paragraphs, extra gap above
headings, a heading is never split across pages or left alone at the bottom, a page too small for a line
still advances. Bold and italic exist as faces at ReadM only; other sizes fake bold with a double strike and
show italic as regular. Layout runs on `TextMetrics` (a fake in the tests, `CanvasMetrics` on the device),
so a whole book can be paginated on the worker; `TextBlock::setPageStarts()` hands such an index over. Touch:
tap on the left third = back, elsewhere forward, horizontal swipes. `setInsets(l, t, r, b)` keeps the text inside margins while
the widget (and its touch area) fills the frame; `setChapter(paras, starts, pos)` swaps text, page starts and position in one Content refresh.
The reader derives `PageView` from it to use its own touch zones. Test screens `widgets4` (keyboard), `widgets5`
(text pages); script commands `type TEXT` and `type-max N TEXT`.

Clipboard (`core/clipboard`, DECISIONS.md D21): one piece of text for the whole OS (4 KB, kept in `/data/clipboard.txt`, never logged).
**Every `TextField` opens a sheet on a long press**: "Paste" (disabled while the clipboard is empty), "Copy all" (not offered in a masked
field), "Clear" (both disabled while the field is empty); a caption above them says how many characters the clipboard holds, never its text.
Paste appends at the caret (always the end) and stops at the field's limit (`setMaxChars`, in characters, cut at a character boundary); a
one-line field turns line feeds into spaces. `setLines(n)` makes a multi-line field (body size, 32 px lines, line feeds kept, the last n
lines shown; Enter of a bound keyboard types a line feed when no Enter action is given); `onChange` runs after every change.
With a paired phone that shares the clipboard (D23, `clipboard::syncing()`), the sheet's caption first ends in ", checking your
phone..." while the relay is read (`clipboard::pull`), then shows the count again ("Clipboard from your phone: N characters" when the
phone's text arrived) and enables Paste if there is something; every `clipboard::set` by anything but the phone is sent to the phone.
UI script `textfield-phone`.
`clipboardMenu = false` turns the sheet off. Test screen `widgets6`, script `textfield-menu` (and `keyboard-menu-masked` in `keyboard`).

## Gestures (`core/gesture`, task P2-1)

| Gesture | Definition |
|---|---|
| Tap | down and up within 600 ms, moved less than 24 px |
| Long press | held 600 ms, moved less than 24 px (fires while still down) |
| Swipe left / right / up / down | moved more than 80 px along one axis, within 800 ms |

On `Down`, the widget under the finger shows its pressed state with `Hint::Feedback`. The action
fires on `Up` inside the widget. Swipes are used only for paging (left/right), for the quick
panel (swipe down from the status bar: Wi-Fi toggle, sleep, power off) and for the **clean gesture**:
a swipe up that starts in the bottom 60 px (`kCleanEdgeH`) cleans the whole screen with one Quality pass, on every
screen including full-screen reader pages (`Shell::onGesture`, 2026-11-16, chosen by the user). The side button cannot do
this: the firmware cannot read it (HARDWARE.md). No app may use a swipe up from the bottom edge.

## Patterns

- **Lists**: `ListItem` rows of 96 px, 7 per page with TitleBar and Pager. Never scroll.
- **Detail**: tap a row to push a detail screen (full text) or open a `Sheet` (quick controls).
- **Grids**: the launcher: 3 columns of small `Tile`, 4 rows per page, 16 px between (twelve apps to a page; until
  2026-11-17 2 x 3 large tiles). Home: 2 columns of large `Tile`.
- **Confirmation**: destructive actions use a `Sheet` with a primary and a secondary button: `ui::confirmSheet(from, title,
  body, okText, onOk)`. Any other sheet: `ui::openSheet(from, [](ui::Sheet& s) { ... })`, with `s.addButton(text, action,
  kind)` for buttons that close it first (`s.closing(action)` for a button in a row of its own). Do not build a sheet by hand.
- **Lists longer than a screen**: `ui::PagedList(rowsPerPage, fill)` + `setCount(n)`: the rows of the page on screen are
  filled by `fill(row, index)`, the bar shows from the second page on. A plain `Pager` only where a page is not just rows
  (the launcher's tiles, the library's first page with its card, the agenda's day headers).
- **Errors**: inline `EmptyState` for a failed first load; `Toast` when cached data is on screen.
- **A toast is a message, not a control**: until 2026-11-16 it was an ink pill of the primary button's size at the bottom, often
  exactly on a button ("The button / notification looks the same? bad system ui design", user). The band over the status bar
  cannot be taken for a button and covers no content, no button and no keyboard; the clock is hidden for its three seconds.

## Reviewing UI work

Every UI task ends with a headless screenshot that the author looks at:
`make shot` or a `tests/ui/*.script` run (`SIMULATOR.md`). Check: margins 24 px, nothing closer
than 8 px to another element, targets at least 72 px, text not clipped, only theme tokens used,
and the refresh log shows no Quality push outside `Hint::Image` and budget cleans.

### Emoji

Every text drawn with `drawText` / `measureText` / `ellipsize` shows emoji. Source: **Microsoft Fluent Emoji, "High Contrast" style** (MIT, `assets/fonts/FluentEmoji-LICENSE.txt`): drawn
for black and white, bold filled shapes that survive 16 grays and small sizes. Chosen by the user on 2026-11-16 over Noto
Emoji ("These are ugly. Especially the grandma and grandpa"); the comparison of Noto, Fluent High Contrast, OpenMoji and
gray-converted Fluent Flat and Twemoji is `tools/gen-emoji.py`'s history (colour sets turn yellow faces into pale gray, which
the panel prints lighter still; OpenMoji's lines are too thin). The 36 single emoji Fluent lacks come from **Noto Emoji** (SIL
OFL 1.1, `assets/fonts/NotoEmoji.ttf`, weight 600). **Flags are letters**: the country code in an outlined box like the
keycaps (🇳🇱 -> `NL`, England, Scotland, Wales -> `ENG`, `SCO`, `WAL`: a gray flag is mostly stripes; Twemoji's flags in gray were tried first). `tools/gen-emoji.py` (needs the
Fluent SVGs fetched first, see its top) renders 2308 emoji as 4-bit alpha bitmaps: 1405 single code points, 224 sequences
(ZWJ sequences like 👩‍💻 and ❤️‍🔥, keycaps) and 679 flags (every pair of letters, used or not, and the three tag flags);
one atlas per size: 18 (caption), 22 (body, bodyStrong), 24 (read-S), 28 (title, read-M and up, headline),
`assets/emoji/emoji-<px>.bin`, 2.5 MB together. A Fluent emoji's 32 x 32
box is the font size, 0.86 of it above the baseline, with px/8 (at least 2 px) on each side.

- **On the card, not in the firmware**: one size alone is 260-600 KB, more than the firmware's 3 MB budget had left when they
  were added (about 250 KB; about 720 KB since D31). The atlases live in `/sd/paperos/emoji/` (`make emoji` copies them into `sim-data/` and pushes them to the
  device; `tools/ui-test.sh` links them into every test card). `ui/emoji` keeps only each atlas's index (16 bytes per
  emoji) and reads a bitmap the first time a text needs it.
- **Fast**: a read bitmap goes into the same glyph cache as the letters (`ui/text`), so after the first use an emoji line
  draws like a line of letters (host: 7 us against 6 us per line, `emoji_test`); widths, ellipsis and line wrap include
  emoji. Each emoji and size is read from the card once per boot.
- **Placement**: on the font's baseline (the bottom of its "x"), at the font's size, advance as the font gives it.
- **Sequences**: each atlas sequence has an id from U+F0000 (private use); `ui::sanitize` replaces a sequence it finds by its
  id (`emoji::matchSequence`, longest match, skin tones and selectors inside skipped), so drawing stays the one-code-point
  fast path. A digit, # or * is looked up only when U+FE0F or U+20E3 follows. Families written as joined people
  ("👨‍👩‍👧"; Fluent has no family sequences) show 👪; another unknown ZWJ sequence shows its first emoji; leftover skin tones
  and selectors are dropped (a black-and-white set has no skin colour). `core/text_clean` keeps emoji and sequences whole.
- **No card / no atlas**: a few emoji get a text form (`:)`, `<3`, `(+1)`), the rest disappear (never `?`).
- **Score squares** keep their own drawing (below): they must stay apart on the gray panel.
- **Test screen** `emoji` (`--app emoji` in the simulator, `tools/device.py set screen emoji` on the device): every size, a
  group name, faces, objects, sequences and a wrapped paragraph; golden `emoji`.

### Score squares

`drawText` / `measureText` draw the score emoji (🟩 solid, 🟨 hatched, ⬛ ⬜ 🟥 🟦 🟧 outlined) as boxes of the text's height:
the fonts have no emoji. The text keeps the real characters. docs/apps/somdoku.md.
