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
| `disabled` | gray(7) | disabled text and icons (was gray(9)) |
| `divider` | gray(11) | 1 px separators |
| `fill` | gray(13) | icon wells, quiet backgrounds |

Anything that changes on touch (pressed, selected, on/off) uses only `ink` and `paper`.

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
draw as `?`. Emoji in messages are replaced by `?` in v1 (task P6-4 notes an improvement).
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
`ui::icon(canvas, Icon::Sofa, x, y, 32)`. `assets/icons.txt` lists the icon names to include (45 icons, `Icon::BookOpen` etc.);
the generator writes `src/gen/icons.h` (`enum class Icon`) and the two icon fonts.
Initial set: arrow-left, chevron-left, chevron-right, house, book-open, library, lightbulb,
lightbulb-off, sofa, utensils, blocks, cooking-pot, lamp-wall-down, lamp-wall-up, car, bed, thermometer,
droplets, fan, wind, message-circle, mail, mail-open, settings, wifi, wifi-off, battery,
battery-low, battery-charging, refresh-cw, moon, power, sun, check, x, plus, minus, clock,
folder, file-text, type, info, triangle-alert, door-open, air-vent.
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
| `Label` | text, one or more lines | token for style; `maxLines`, ellipsis |
| `IconView` | icon glyph | 32 or 64 |
| `Button` | primary (filled ink) / secondary (2 px outline) / text | height 72; pressed = inverted |
| `IconButton` | 72x72 target with a 32 px icon | |
| `Tile` | launcher / Home app square: 64 px icon well + label, optional value and badge | pressed = 4 px border |
| `ListItem` | 96 px row: leading icon, title, subtitle, trailing text / chevron / toggle / badge | divider below |
| `Toggle` | on/off pill 88x48 | on = filled ink with paper knob; pure black/white |
| `LevelBar` | 10 segments, tap a segment to set 10..100% | replaces sliders: dragging is poor on e-ink |
| `Stepper` | minus, value, plus | temperatures, font size |
| `Segmented` | 2 to 4 options | selected = filled ink |
| `Pager` | "3 / 12" with previous/next targets; also handles horizontal swipes | every list longer than one screen uses it |
| `Badge` | filled ink circle/pill with count | unread counts |
| `Sheet` | bottom sheet over the untouched screen, 2 px ink top edge, max 60% height; a tap outside or a swipe down closes it | detail controls, confirmations |
| `Toast` | one line at the bottom for 3 s | errors like "No Wi-Fi" |
| `EmptyState` | 64 px icon + title + hint + optional button | empty lists, errors, loading |
| `Keyboard` | QWERTY, 3 layers (abc / ABC / 123#), key 54x72 | Wi-Fi password, search |
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
`sheet->content()`. `Root::showToast(text)`: ink pill at the bottom (y 864..936), 3 s, then the
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

## Gestures (`core/gesture`, task P2-1)

| Gesture | Definition |
|---|---|
| Tap | down and up within 600 ms, moved less than 24 px |
| Long press | held 600 ms, moved less than 24 px (fires while still down) |
| Swipe left / right / up / down | moved more than 80 px along one axis, within 800 ms |

On `Down`, the widget under the finger shows its pressed state with `Hint::Feedback`. The action
fires on `Up` inside the widget. Swipes are used only for paging (left/right) and for the quick
panel (swipe down from the status bar: Wi-Fi toggle, sleep, power off).

## Patterns

- **Lists**: `ListItem` rows of 96 px, 7 per page with TitleBar and Pager. Never scroll.
- **Detail**: tap a row to push a detail screen (full text) or open a `Sheet` (quick controls).
- **Grids**: 2 columns of `Tile`, 3 rows per page (as in the Phase 0 mock, `out/spike.png`).
- **Confirmation**: destructive actions use a `Sheet` with a primary and a secondary button.
- **Errors**: inline `EmptyState` for a failed first load; `Toast` when cached data is on screen.

## Reviewing UI work

Every UI task ends with a headless screenshot that the author looks at:
`make shot` or a `tests/ui/*.script` run (`SIMULATOR.md`). Check: margins 24 px, nothing closer
than 8 px to another element, targets at least 72 px, text not clipped, only theme tokens used,
and the refresh log shows no Quality push outside `Hint::Image` and budget cleans.
