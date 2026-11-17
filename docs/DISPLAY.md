# Display: rendering for e-ink

Goal: updates feel quick, the screen almost never flashes, and ghosting stays invisible.
Everything that reaches the panel goes through `core/display_manager` (task P1-3). Apps and
widgets never call the panel.

## Measured on our device (2026-11-15, spike firmware, M5GFX 0.3.0 default waveforms)

| Update | Mode | Time |
|---|---|---|
| Full screen 540x960 | Quality | 1363 ms |
| Full screen | Text | 1371 ms |
| One row 540x96 | Text | 871 ms |
| Box 200x200 | Fast | 268 ms |
| Full screen | Fast | 504 ms |

Times are push + waveform, measured with `waitDisplay()`. Consequences:

- **Text mode is too slow for interaction** (0.9 to 1.4 s). Use it to *settle*, not to respond.
- **Fast mode is the interactive mode** (0.27 to 0.5 s) but shows only black and white.
- Quality costs the same as Text, so when grays must be exact and a flash is acceptable there
  is no speed reason to avoid it.

## Pipeline

```
widgets --draw--> Canvas (8-bit gray, 540x960)
                     |
            DisplayManager.commit()
                     |  1. union of invalidated rects, clipped to screen
                     |  2. diff against shadow (what the panel shows): shrink each rect to
                     |     the pixels that really changed; drop rects with no change
                     |  3. choose a mode per rect (below)
                     |  4. hal::Display.push(canvas, rect, mode); update shadow
                     |  5. account ghosting; schedule settle / clean passes
                     v
            panel (device)  or  SDL window + refresh log (simulator)
```

`shadow` is a copy of the last pushed frame quantised to 16 levels (4 bits per pixel, 259 KB).
Comparison is done on quantised values, so sub-level anti-aliasing noise never causes an update.

## Refresh classes

Widgets do not pick panel modes. They invalidate with a **hint** that states intent:

```cpp
enum class Hint : uint8_t {
  Feedback,   // immediate response to a touch: pressed state, toggle, cursor
  Content,    // new content in place: list rows, values
  Page,       // a new page of text (reader): Fast, no settle; every Nth page a Quality clean
  Screen,     // whole screen changed: navigation, app switch
  Image,      // grayscale picture: book cover, photo
};
widget.invalidate(Hint::Content);
```

The DisplayManager maps hints to panel modes:

| Hint | First pass | Settle pass | Notes |
|---|---|---|---|
| Feedback | Fast, immediately (a pressed look: after 120 ms of holding, see "Navigation speed") | none if the *changed pixels* are pure black/white, else Text | the result of a tap must reach the panel within 100 ms of the Up |
| Content | Fast | Text after 600 ms without further changes in the region | "fast then settle" |
| Page | Fast | **none** (drops older settles inside the page) | the reader's page turns. Every `pageCleanEvery`-th (10) is one full-screen **Quality** pass instead, which also brings the 16 grays back. Decided: the Text settle is a dark sweep over the whole text area 0.6 s after every turn (camera: the paper at ~60 % brightness for ~0.3 s); black/white text between cleans is preferred |
| Screen | Fast full screen; **one Text pass instead** if the new pixels hold flat gray areas (`grayAreaMin`, below) | Text after 600 ms (none after the Text pass); **Quality instead** if the ghost budget is spent | navigation is the natural place to hide a clean |
| Image | Quality on the image rect | none | only place a flash is expected |

**Fast then settle.** A Fast pass shows the new content thresholded to black/white right away
(text is readable, edges slightly rough). When the user stops interacting, a Text pass over the
same region restores the 16-level anti-aliasing without a flash. If new input arrives before the
settle timer fires, the timer restarts, so rapid page turns stay at Fast speed.

The canvas always holds the full-gray frame; black and white for the Fast pass is made in `hal::Display`: on the device by
the panel driver, in the simulator by the emulation. **It is an ordered dither, not a threshold** (found 2026-11-16 in the
driver source, M5GFX `Panel_EPD`, `epd_fast`): a pixel is black if `value + Bayer4x4 * 16 < 248`. So grays are in the Fast
pass as dots: a gray(9) divider has 6 of 16 pixels black, gray(13) 2 of 16, gray(3) 13 of 16; anti-aliased edges are speckled.
The settle pass then turns the dots into the even gray. The simulator used a threshold at 50 % until then, which showed
gray(8) and lighter **not at all** until the settle pass (dividers, gray icon wells, the word game's gray frames: "first the text,
then the lines", user in the simulator window); it now uses the driver's formula (`platform/sim/display.cpp`; the pattern
is indexed by logical x, y, the driver indexes it in panel coordinates). **Confirmed on the panel 2026-11-16** (camera session, Display lab "Fast" after a logged `mode=F ... why=page` push, `cam.py compare`): lightness per band, 0 = ink, 1 = white: g0 .01, g1 .03, g2 .13, g3 .23, g4 .27, g5 .30, g6 .38, g7 .44, g8 .54, g9 .57, g10 .61, g11 .64, g12 .71, g13 .81, g14 .94, g15 1.04: a steady rise that follows the predicted white share (g3 .19, g7 .44, g9 .62, g13 .88); a threshold would give two flat groups, and the real-gray ramp bunches at both ends instead. A reader page was tried first and showed nothing: at 1.3 camera pixels per panel pixel the 4 x 4 pattern on text edges blurs away. Text and Quality modes also dither between neighbouring levels in the driver (`(value + Bayer - 8) >> 4`); the
simulator still shows the exact level there.

## Ghost budget

Fast updates leave faint remains; many of them also build DC bias on the panel. The manager
keeps a score:

- every Fast pass adds `changedPixels / screenPixels` (so a full-screen Fast = 1.0, a small
  button = 0.01);
- a Text settle pass subtracts half of what its region added;
- a Quality pass over the full screen resets the score to 0.

When the score reaches **`ghostLimit` (default 8.0)**, the next `Hint::Screen` commit uses
Quality for the whole screen (one flash, during navigation where it is least noticed). If no
navigation happens and the score reaches `2 x ghostLimit`, the manager cleans on the next idle
moment (2 s without input).

**Page turns have their own cadence (`Hint::Page`, 2026-11-16)**: every `pageCleanEvery`-th page (default 10, `config.json`
`display.pageCleanEvery`, 0 = off) is one full-screen Quality pass; the others are Fast without a settle pass. Why: measured with the
camera, a page turn as `Content` added about 0.13 net to the score (a Fast of ~0.26 of the screen, the settle took half back), the
reader never commits `Screen`, so only the idle clean at 2 x `ghostLimit` = 16 was left, after roughly 120 turns: 30 turns gave no
clean, and the ghost grew to 2 % of contrast. On the device after the change: nine `why=page` Fast pushes, then `why=page-clean`
Quality; ghost after 1 page 1.57 %, after 9 pages 1.21 % (within the noise of the old 1.23 % / 1.54 %), the turn finished on the panel
about 0.67 s after the tap instead of 1.6 s, and no dark sweep.

`ghostLimit`, the settle delay and the idle delay live in `config.json` under `display` so they
can be tuned on the device without rebuilding (task P7-1).

## As implemented (`core/display_manager.{h,cpp}`, P1-3)

- API: `invalidate(Rect, Hint)`, `commit()`, `tick(nowMs)`, `nextDeadlineMs()`, `forceClean()`,
  `ghostScore()`, `refreshLogTail()`, `shadowPacked()`. Parameters in `DisplayParams`
  (`ghostLimit` 8, `settleMs` 600, `idleCleanMs` 2000, `pageCleanEvery` 10). Unit tests: `tests/unit/display_manager_test.cpp`.
- One pending rect per hint (invalidations of the same hint are united). `commit()` handles
  Feedback, Page, Content, Screen, Image in that order (Page before Content: the footer of a page turn is Content and must not make
  the page wait for its push: measured, the page's push call took 322 ms behind the footer, 82 ms in front of it); later rects diff against the shadow the
  earlier ones updated.
- The shadow is the quantised canvas of everything pushed (not the thresholded Fast image).
  `forceClean()` at boot makes it valid; before that every pixel counts as changed.
- A settle region merges with any overlapping or touching Fast region and restarts its timer
  (500 ms later change in the same area = new 600 ms from then). Unrelated regions keep their timers.
- Screen at or over `ghostLimit`: Quality over the whole screen, drops pending settles.
- **Screen with flat gray areas (2026-11-16)**: (written when the simulator thresholded: on the device a gray(13) well is in the
  Fast pass as sparse dots, 2 of 16 pixels, not missing; whether one Text push is still the better trade there is for the camera.)
  A Fast pass thresholds gray to black or white, so gray *areas* (the launcher's and
  Home's gray(13) icon wells, the keyboard) were missing from the Fast pass and arrived only with the settle pass 0.6 s later: a second
  refresh and on the panel a dark sweep. Now the diff of a
  Screen pass also counts changed pixels of flat gray (levels 1..14 with the same level 2 px away on all four sides, so anti-aliased text
  edges and 1 px dividers do not count; white or black 8-byte runs are skipped; it stops at the limit). At `grayAreaMin` (2000 px, a
  disc of radius 25; `DisplayParams`, not in `config.json`) the pass is **one Text push** of the changed box (`why=screen-gray`): the final
  screen at once, no settle afterwards, settles inside the box dropped (one that sticks out keeps only the strip outside the box, when
  that is one rectangle: the Agenda's first frame was 2 px wider than its next and got a second Text pass over 494x882). Ghost score: half of what the Fast pass would add (what Fast then
  settle nets). Screens without gray areas (text, lists) keep Fast then settle. The Text push takes longer on the panel than a Fast one
  (full screen 1.37 s against 0.5 s), but ends with the final look; one waveform instead of two.
  Image: Quality on the changed box only, does not touch the score.
- Idle clean (`score >= 2 x ghostLimit`, 2 s without a push): `tick()` runs `forceClean()`.
- Clean on demand: a swipe up from the bottom edge (UI.md "Gestures") runs `forceClean()` (`why=clean`). On the device
  (2026-11-16, camera): one Quality push, 0.66 s visible on the panel, it removed 1.5 % of ghost from a reader page.
- Every push is logged (`POS_LOG("disp", ...)` and the refresh log: `t= mode= rect= changed= why=`).
- **`hal::Display::busy()` on the device is only "M5GFX update queue full"**: M5GFX cannot say
  whether a waveform is running. So the "coalesce while busy" rule rarely triggers on the device;
  M5GFX queues up to 8 updates itself and merges overlapping ones. `>set epd-timing on` makes the
  device wait after each push and log `[epd] ... push+waveform <ms> done t=<ms>` (blocks the UI:
  debug only).

## Unchanged pixels are left alone (2026-11-17, `tools/patch_m5gfx.py` patch 4; measured on the panel the same day)

Measured in the simulator
(refresh log of a walk through Settings): every navigation is a Fast pass over 78 % of the screen (the content area,
492 x 823) and, 0.6 s later, a Text settle over the same 78 %; no Quality pass in 16 navigations. Why that flashes is in
the driver (M5GFX `Panel_EPD.inl`, `step_fast` / `step_text`): the update task keeps per pixel pair the *request* it last
got, and a request carries its mode. So

- a **Text settle after a Fast pass** drives every pair of its rectangle again, the white background too (it was "white,
  asked for by Fast", now it is "white, asked for by Text"): through the erase steps and then the Text waveform, which for
  white is five frames toward black and back. The whole region darkens and returns: the "dark sweep" the camera saw;
- a **Fast pass after a Text or Quality pass** does the same in small: every pair of its box gets the Fast waveform, for
  white two frames toward black first;
- a Text pass drives every pair that is not white again, changed or not (unchanged text flickers under a `screen-gray` push).

The launcher and Home change screens with one Text pass after another (`screen-gray`), where white on white is skipped, so
they flash less; list screens (Settings) are Fast then settle, and get two full-area flashes per navigation.

Patch 4: a pair whose two pixel values are what was last requested is not driven again, whatever mode asked (a Fast
request holds 0 or 15 per pixel, so an equal value after it is black or white, where Fast left it; an equal gray was put
there by Text or Quality). Only what changes is driven: the settle touches the anti-aliased edges and gray lines, a
navigation the pixels that differ. Quality is untouched (a clean drives everything). `>set epd-keep off` gives the stock
rule back while the device runs, `on` the patch: for a comparison with the camera.

**On the panel (2026-11-17, camera locked, 640x480 at 30 fps, `cam.py around tap` on Settings > Display and back; the
stock rule and the patch in one session, switched with `>set epd-keep`)**. The number is how much the white that stays
white (white in the frame dump before and after, away from ink) darkens while a push runs, in % of that video's
white-to-ink contrast (`cam.py around` prints it per push since this day):

| | Fast pass of the navigation | Text settle 0.6 s later |
|---|---|---|
| stock rule (`epd-keep off`) | 25-31 % darker, visible 236-241 ms | **66-82 % darker**, visible 327-367 ms |
| patch 4 (`epd-keep on`) | 0.3-0.4 %, visible 73-106 ms (what changed) | 0.5 % or less, "no visible change in its rect" |

So the background nearly reached ink during the settle and a quarter of the way during the Fast pass, twice per
navigation; with the patch neither shows. The screen is final about 1.17 s after the tap instead of 1.34 s.
The settle still does its work: Display lab, Check (a Fast pass) then Ramp (Fast, then the Text settle): the 16 levels read
the same with the patch as with the stock rule (every level within 0.01: g0 0.03, g7 0.41, g12 0.74, g15 1.02). Ghost after
20 navigations from a cleaned Settings list, against the same page after a Clean: **0.75 %** with the patch (p95 3.5 %),
1.09 % with the stock rule (p95 4.3 %): one run each, so "not worse" is what it says; the ghost budget stays as it is.
`>set epd-selftest on` after the flash: "identical" for Fast and Quality. (In that ramp g11 read 0.49 between g10 0.70 and
g12 0.74, with and without the patch: not from this change; the set-up was moved since the ramp of 2026-11-16.)
Also changed: a toast is a Content update, not a Screen change (it could trigger the full-screen Quality clean).

## Navigation speed (2026-11-15)

Measured on the device with `tools/device.py tap` and `refresh` (injected taps, so Down and Up are
60 ms apart), opening a screen from a row or tile:

| | before | after |
|---|---|---|
| tap to the new screen's Fast pass done (epd timing mode) | 925 ms (Read tile), ~1 s (row) | **605 ms** (tile and back arrow; row alike) |
| `diff` of the new screen against the shadow | 107-229 ms | 21-29 ms |
| layout + drawing the new screen (`[perf] render`) | 116 ms | 56-60 ms |
| `hal::Display::push` call of the screen pass | 531 ms | 88 ms |

What was slow: (1) the pressed look of the tapped row was pushed on Down and its waveform (~250 ms) was
still running when the screen was built and pushed. **While an EPD waveform runs, every access to PSRAM is
several times slower** (the EPD task streams frame buffers from PSRAM): the diff, the drawing and the
`pushSprite` conversion of the next screen all crawled, and the push call even waited ~0.5 s. (2) The diff
looked at every pixel of the 400 000 changed ones.

Fixes: (1) **Press feedback delay**: `Pressable` shows its pressed look only if the finger is still down
after `ui::kPressDelayMs` = 120 ms; a quick tap goes straight to its result, one Fast pass, no waveform
running while the next screen is built (and half the Fast passes, so less ghost budget). The 100 ms
Feedback promise now applies to what the tap *does*, not to a pressed look for taps shorter than 120 ms.
Segmented, LevelBar and TabBar still preview on Down (their result is the preview). (2) **Cheaper diff**: the
shadow is an exact 8-bit copy of the last pushed canvas; per row 8 bytes at a time find the first and last
changed pixel (sub-level noise trimmed at the ends), `changed` counts the changed span of each row (the
ghost score is a hair high when pixels inside a span are unchanged), and the black/white check only runs for
Feedback regions. The refresh log now prints `diff=<ms>` and `call=<ms>` per push, and the loop logs
`[perf] render took <ms>` for passes of 30 ms or more.

Remaining cost of a screen change: Up (60-150 ms) + render 57 + diff 25 + push call 88 + the Fast waveform
(~400 ms for a 492 x 824 area; fixed by the panel). Further savings would need a smaller changed area or a
cheaper layout pass, not different scheduling.

## Tap latency (2026-11-16)

Measured with `tools/latency.py` (injected taps, the firmware's `[lat]` line per tap: event -> gesture -> render -> commit, with
the diff and push-call times of the push) and `tools/cam.py around` (when the panel changes). Medians, finger up to the update
handed to the panel driver; the waveform follows (Fast ~0.13-0.17 s visible).

| Tap | before | after | what changed |
|---|---|---|---|
| reader page turn | 516 ms | **112 ms** | fast text, M5GFX patch 1, prefetch, `Hint::Page` |
| open a screen (Settings row) | 807 ms (246 idle) | **150 ms** | fast text, M5GFX patches (two cores), 64 KB data cache, 120 MHz flash + PSRAM (D25), diff updates the shadow |
| small control (stepper) | 52 ms | **5 ms** | config written 1 s later, not on the tap |

On the panel (camera, page turn): first change 0.53 s -> **0.38 s** after the tap, turn finished 0.67 s -> **0.55 s** (both include
the camera's own ~0.2 s delay; at the start of the day, with the settle sweep, the turn ended at 1.6 s).

Where the time went and what was done:
- **Text drawing** (`ui/text.cpp` "Fast text path"): LovyanGFX decoded every BFF glyph with a malloc and wrote each pixel as a
  1x1 fill: 10-20 ms per reader line, 155-361 ms per page. Glyphs are now rasterised once by LovyanGFX on a scratch canvas (exact:
  the 16 alpha levels come back from black-on-white; the true glyph box from `drawChar` with the background fill turned off) and
  blended straight into the 8-bit canvas with LovyanGFX's own positioning, fill and integer blend. Byte-identical
  (`tests/unit/text_fast_test.cpp`, every font, anchor, colour pair, clipping, icons, the fake-bold strike over overlapping
  glyphs). Page render 362 -> ~100 ms, screen render 121 -> 60 ms.
- **Frame buffer write** (M5GFX patch 1, `tools/patch_m5gfx.py`, D24): our portrait UI on the landscape panel made M5GFX write
  one 4-bit pixel per PSRAM byte, column-wise; while a waveform ran (the update task streams ~1 MB of PSRAM per frame) a page push
  took 450-530 ms. Now 32 source rows per tile, quantised by table (Bayer and formulas unchanged) and written as whole bytes:
  page push ~87 ms, also while a waveform runs. Identical frame buffer (`>set epd-selftest on`).
- **Update task** (M5GFX patch 2): rows whose waveform finished get a zero line without reading the step buffer.
- **Prefetch** (`TextBlock::prefetch`, reader): 600 ms after a page shows, the next page is drawn into a spare canvas; the turn
  copies it in. **Skipping the diff for pages was tried and reverted**: a page turned under the open menu sheet then re-pushed the
  sheet's unchanged gray text in Fast mode.
- **Config writes**: Settings write `config.json` about a second after the last change (`Os::saveConfigSoon`; flushed before
  power-off); a flash write on the tap cost 25-50 ms before the redraw.
- **Data cache** (D25, the core built from source): 64 KB with 64-byte lines instead of 32 KB / 32 B. A screen change during
  the previous one's settle waveform 408 -> 306 ms.
- **120 MHz flash and PSRAM** (D25): PSRAM 51 -> 64 MB/s, and code and fonts (in flash) run faster: page turn 199 -> 140 ms,
  screen change 306 -> 190 ms (the collision with a running settle no longer shows in the median).
- **Both cores for the frame buffer write** (M5GFX patch 3): a helper task on the other core (priority 1, below the EPD update
  task) and the caller take 16-row chunks from one atomic counter; the caller waits only for a chunk the helper took. Page
  push call 87 -> 61 ms; the self-test rect 28 -> 22 ms (still identical).
- **Diff and shadow in one pass**: the diff copies each row's raw changed span into the shadow as it goes (rows with only
  sub-level noise too); the second pass over the box after every push is gone. Unit tests and goldens unchanged.
- **Reading position written a second after the turn** (every 5th turn), not on the tap: the flash write cost ~36 ms there.
- Now (2026-11-16): page turn 112 ms, screen change 150 ms, small tap 5 ms.
- **Chapter boundaries**: the reader loads the next chapter on the worker as soon as it starts within the next two pages, and
  the turn into it uses that. Loading takes 129 ms at book open but ~930 ms while reading (the worker shares core 0 with the
  panel's waveform task after each turn), so one page of lead was not always enough: a turn could still wait ~1 s. 60 turns
  over several boundaries: median 105 ms, slowest 159 ms (boundary turns ~130 ms: the new chapter's layout).
- **Frame dumps no longer block the UI**: `Remote` queues its replies and sends what the USB buffer takes on each loop pass
  (16 KB TX buffer); a `>shot` held the UI loop ~1.07 s, now the 259 KB stream between passes, 0.5 s per dump.

## Rules for UI code (enforced in review, see UI.md)

1. **No animation, no scrolling.** Lists paginate. Progress is a static label.
2. **Interactive state is pure black/white.** Pressed, selected, on/off must be drawable with
   black and white only, so the Fast pass shows the final look. Grays are for secondary text,
   dividers and images.
3. **Invalidate the smallest widget.** A toggling switch invalidates itself, not its row.
4. **Never invalidate from a timer faster than once per minute** (clock in the status bar).
5. **One commit per event.** Handle the event, change state, let the loop commit once.
6. Coalesce: while the panel is busy (`display.busy()`), further commits merge their rects and
   wait, except `Hint::Feedback` which may queue immediately (M5GFX handles overlapping updates).

## Device implementation notes (`platform/device/display.cpp`, task P1-2)

```cpp
void push(const Canvas& frame, Rect r, Refresh mode) {
  M5.Display.setEpdMode(toEpd(mode));         // Quality->epd_quality, Text->epd_text, Fast->epd_fast
  M5.Display.setClipRect(r.x, r.y, r.w, r.h);
  frame.pushSprite(&M5.Display, 0, 0);        // only pixels inside the clip are transferred
  M5.Display.clearClipRect();                 // endWrite inside pushSprite queues the update
}
```

- This code is `src/platform/device/display.cpp` (the pushSprite call needs a `const_cast`: it
  is not const in LovyanGFX). The same code ran in the spike and produced the numbers above. A cheaper transfer for small rects (push only the sub-rectangle with `pushImage` from
  the canvas buffer) is an optimisation for task P7-1, not before.
- `M5.Display.waitDisplay()` before light sleep and before power-off, or the image is left
  half-driven.
- `epd_fastest` is not used by default. Reserve it for future drag feedback.

## Simulator emulation (`platform/sim/display.cpp`, task P1-4)

The simulator keeps a `panel` buffer (what a real panel would show) separate from the canvas:

| Pushed mode | Effect on `panel` |
|---|---|
| Quality | region = canvas quantised to 16 levels; ghost residue cleared; counts as a flash |
| Text | region = canvas quantised to 16 levels; residue halved |
| Fast | region = canvas thresholded at 50% to black/white; where a pixel changed, 6% of the old value is left as residue |

Implementation notes (P1-4): `panel` holds 16-level values; the residue is a separate float layer
(offset in levels). Screenshots, `>shot` and `snapshot4()` show `panel` **without** residue (stable
goldens; the device frame matches after a settle pass); key `G` in the window overlays the residue,
exaggerated 8x. A pixel counts as `changed` when its panel value changed. Headless one-shot runs
use a fake clock (the log shows t=0); `--headless --remote` and the window use real time.

Every push is appended to a **refresh log**: `t=<ms> mode=<Q|T|F> rect=x,y,w,h changed=<pixels>`.
Screenshots come from `panel` (so they show thresholding and ghost residue). UI tests assert on
the log, for example "a page turn causes no Quality push" or "tapping a toggle pushes at most
120x80 pixels". This is how rendering quality is tested without hardware. The emulation is a
model, not physics: final tuning happens on the device (task P7-1).

## Test patterns

Settings > Developer > Display lab (task P7-1) shows: the 16-step gray ramp, a ghosting test
(checkerboard then white), a fine-text sample, and counters for pushes per mode. Use it to tune
`ghostLimit` and the settle delay, and to compare custom waveforms.

**Fast (2026-11-16)**: a fifth pattern, the ramp upside down (white at the top, so it differs from Ramp in every band and
is pushed also when coming from Ramp: with the same ramp the diff found nothing and the panel kept the Text-mode image)
pushed as `Hint::Page`: one Fast pass and no settle pass, so the panel
keeps what the driver's Fast mode makes of 16 grays. An ordered dither (what the driver source says, "Fast then settle" above)
shows 16 bands of rising brightness; a threshold would show black bands and white bands only. For the camera: a still of the
whole ramp is enough, no pixel detail needed. Check the counters line or `tools/device.py refresh` for an `F ... why=page`
push: a Page push can be the reader's every-10th Quality clean, and then the ramp is in real grays. The segment "Check" is
the checkerboard (five labels: "Checker" did not fit).

**As built (2026-11-15, `DisplayLabPage` in `apps/settings/settings_app.cpp`)**: four patterns behind a Segmented control.
**Ramp**: 16 labelled bars gray(0) to gray(15). **Checker** and **White**: pure black and white, shown with one Fast pass each, so Checker then
White shows what a Fast pass leaves behind; "Clean (Quality)" is the cleaning pass. **Text**: the caption size in gray 0/3/5/7/9, the body size in
gray 0/3/5/7, the four reader sizes, and 1 px lines in gray 0/7/9/11/13. A line below shows the pushes per mode and the ghost score against the
limit. Goldens `lab-*`. It is a tool for a person's eyes: nothing was tuned with it yet (frame dumps show the frame, not the panel).

## What the panel does to grays

Frame dumps and simulator screenshots cannot show this. On the physical panel the white is itself gray, and the mid-grays read **lighter**
than on a monitor; thin anti-aliased gray text (the 18 px caption in gray(5)) was the first thing to become hard to read. Rules that follow
(tokens and the content rule are in UI.md "Colour"): no text lighter than gray(3); no content text in gray at all (content is `ink`); a content
line is not set in the caption size when the row has room. Measured by the user with the Display lab (Text pattern) the same day: caption text reads down to gray(3), body text down to gray(5), a 1 px line in gray(11) is
barely visible and in gray(13) invisible; Read S 24 px is fine. Tokens now: `secondary` gray(3), `disabled` gray(5), `divider` gray(9). Not yet judged: the gray(9)
dividers in a real list, the gray(13) icon wells (an area, not a line).

## Camera on the panel (P7-7, `tools/cam.py`)

A webcam pointed at the device sees what frame dumps cannot: ghosting, the Fast-then-settle sequence, the flash of a Quality pass, how
grays and thin lines come out. It is the tool for those checks; a person's eyes are asked only for what the camera cannot tell (touch feel,
whether text is comfortable to read, ghosting too faint for the camera), batched as before.

**The set-up is not stable**: the camera can be moved, daylight changes over the day. So:

- Every run finds the panel again: a SIFT match of the camera image against the frame dump of the same moment (`make device-shot`).
  The outline is saved in `out/cam-corners.json` (gitignored) and reused only within the hour (`--max-age`), for pages without
  features (a blank white page). Four ArUco markers (4x4_50, ids 0-3) stuck on the bezel are optional: if the calibration sees them, it
  stores their panel coordinates and later images of the session are registered by the markers.
- Every number is normalised by the same image: the light is a smooth surface fitted through the white areas, black and contrast are a
  robust fit of camera value against the frame's gray. A number is "% of this image's white-to-black contrast".
- Numbers are compared only within one session (before and after a change, A against B). No camera goldens, no thresholds in `make check`.
- **Lock at the start of a session**: `cam.py lock` puts exposure, gain, white balance and focus
  to fixed values through `uvc-util` (IOKit, no camera permission needed) and writes them to `out/cam-lock.json`.
  Exposure and gain are **measured** since 2026-11-17: 33 ms (the most 30 fps allows) and the gain that brings the
  panel's white to 185 of 255. Before, lock took "what auto exposure chose" from the camera, but the C922 reports its
  defaults in automatic mode (25 ms, gain 0), so the lock froze a dark picture: white at 44-63 of 255 in the 3D printer
  the device sits in (about the light there). Now gain 160, white 166-195. Focus: a sweep over
  0..250 on the page shown (text works best), sharpest inside the panel. 2026-11-16: exposure 29.5 ms, 4000 K, focus 45 (sharpness 173
  against 4-6 out of focus). The settings stay in the camera until it is unplugged; `cam.py unlock` gives the automatics back.
  `uvc-util`: github.com/jtfrey/uvc-util (commit 8110da7), source read before building (IOKit and Foundation only), built with
  `clang -O2 -o uvc-util -framework IOKit -framework Foundation uvc-util.m UVCController.m UVCType.m UVCValue.m` (no ARC) in
  `~/.local/src/uvc-util/src`, installed to `~/.local/bin/uvc-util`.
- **Camera**: Logitech C922 Pro Stream Webcam (since 2026-11-16), above the device, the panel turned 90 degrees in the image, about
  300x540 camera pixels at 1920x1080. **Frame rate**: avfoundation gets uncompressed frames only (MJPEG cannot be chosen), and USB 2 carries
  about 20 MB/s of them: 640x480 runs at 30 fps, 864x480 at 15, 1280x720 and 1920x1080 at 10 (measured 2026-11-16, exposure locked).
  So stills use 1920x1080 and videos and `around` the largest mode that fits 30 fps (640x480: the panel is about 160x290 camera pixels,
  enough for timing and for the gray levels, which matched the 1080p still within 0.02). Timings are good to about 33 ms.
  Autofocus hunted on low-contrast pages (a blurred Ramp) until `lock` fixed the focus.
- **Camera permission**: macOS gives the camera per app. An editor that never asks for the camera gets no Camera prompt (it does not appear in
  System Settings > Privacy & Security > Camera), so ffmpeg started from its shell opens the camera and receives no frames. `cam.py`
  therefore runs ffmpeg through a **capture server in one Terminal window** (`cam.py serve`, started by itself the first time, Unix
  socket `out/cam-run/serve.sock` readable by the owner only, runs nothing but ffmpeg, quits after 30 idle minutes). Terminal has the
  permission. From a terminal app (`TERM_PROGRAM` set) ffmpeg runs directly; `--via direct|terminal` overrides.
- avfoundation timestamps do not advance reliably for this camera (`-t` never ends): stills and videos are stopped on the wall clock.

Commands (`uv run --script tools/cam.py ...`; every command has a run limit and prints progress, like `tools/device.py`):

| Command | What it does |
|---|---|
| `devices` | cameras and their modes. Screen-capture devices are never used; the built-in camera only with `--camera`. |
| `make cam-shot` | `shot`: one still to `out/cam.png` after 2 s of warm-up. |
| `make cam-video SECONDS=5` | `video`: `out/cam.mp4`, 60 fps if the camera offers it, else its best rate. |
| `make cam-compare` | `device-shot` + `cam-shot` + `compare`: warp to 540x960, residual against the frame dump. Prints the **ghost number** (mean, p95) overall and per region (3 columns x 4 rows), the measured lightness per gray level on flat areas (the Display lab Ramp), and writes `out/cam-diff.png` (camera, frame dump, ghost heat map). |
| `calibrate` | registers `out/cam.png` against `out/device.png`, saves the outline, draws it into `out/cam-calibrated.png`. |
| `around tap X Y` / `long X Y` / `swipe X1 Y1 X2 Y2` | frame dump, video, the input through the serial port, the serial log read line by line, `>refresh`, frame dump after. Per push also "white that stays white ... darkest N % of contrast": the flash a pass shows on pixels it does not change. Each push in the refresh log gets a host time (its `[disp] push` line arrives when the push call returns), and the video says per push when its rect starts and stops changing on the panel. Writes `out/cam-timing.png` (change per frame, push markers), `out/cam-frames.png` (frames with the most change), `out/cam-around.mp4`, and `compare` of the last frame against the frame dump after. Refuses a serial port that another process holds. |
| `compare --ref IMG` | ghost relative to a camera image of the same page after Clean (Quality) in the same session: the uneven light cancels out. Use this for ghost numbers. |
| `make cam-target` (`target [--keep]`) | shows the calibration screen (`>set screen camcal`), takes a still, reports what the set-up is worth (below), saves the outline for pages without features, puts the launcher back. `--image` / `--frame` analyse saved files. |
| `selftest` | synthetic camera images (perspective, uneven light, blur, noise): registration within 0.5 px, the ghost of Checker under White found in the right region and rising with the amount, timing of a synthetic Fast push within a frame. |

**Calibration target (2026-11-17)**. The screen `camcal` (`apps/uitest`, golden `camcal`; one Quality pass): a lattice of ink
dots every 60 px over the whole panel, between its rows the 16 grays three times (rising, falling, shifted by eight: each
level in three places), black and white bars, line pairs of 1 to 4 px running down and across, checkerboards, lines of
text (the unique features for the first match), a 2 px frame on the outermost pixels. `cam.py target` reads the layout
from the frame dump (it knows only the dot size and where the line pairs are) and reports:

- **where the panel is**: the feature match, then a least-squares fit to where the camera sees the 144 dots, error in panel
  pixels; the fitted outline is saved (`out/cam-corners.json`, used within the hour for pages without features);
- **lens**: the same fit with one radial term; it says whether the picture is bent enough to matter;
- **light**: how uneven it is, and whether the picture is too dark or near the camera's limit;
- **reflections**: what ink reads at every dot is a map of stray light (the light is divided out, so ink that reads
  lighter somewhere is light lying on top of the picture); the map is taken out of the gray readings;
- **grays**: each level's lightness as the mean of its three places, how far the places differ (what a gray number is
  worth), which neighbours are not told apart, the largest step;
- **sharpness**: the contrast left in the line pairs: from which line width on the camera shows detail.

Self-test 3 (`cam.py selftest`) runs the analysis on the golden through a synthetic camera, straight and with a bent
lens (k1 0.04 found as 0.039).

**On the panel (2026-11-17; the device lies in a 3D printer, the light cannot be changed)**. First run, with the old lock
(exposure 25 ms, gain 0): white at 44-63 of 255, too dark to trust. After the lock was fixed (33 ms, gain 160, focus 45):
144 of 144 dots, 1.14 camera pixels per panel pixel; the panel found to 0.56 px by the feature match and 0.20 px
(worst 0.43) by the dots; the lens term takes 0.04 px off: **no lens correction needed**. Light: the darkest part gets
85 % of the brightest. **Black is far from even**: ink reads 28 to 67 % of the white around it, lightest at the lower
middle of the left edge (a reflection, or the side the light comes from). So one black for the whole panel is wrong by
a lot: the same gray differed by up to 0.23 between its three places. With the dots' map as each place's own black:
0.12-0.13. Grays (mean of three places, repeatable to 0.02 between two runs): g0 -0.03, g1 -0.02, g2 0.06, g3 0.15, g4
0.23, g5 0.30, g6 0.38, g7 0.53, g8 0.57, g9 0.63, g10 0.69, g11 0.78, g12 0.91, g13 0.96, g14 1.01: in order, g0 and
g1 not told apart, the largest step g6 to g7. What is left between places is not the light, the black or the camera's
tone curve (a fitted power curve took 0.02 off and ran to the end of its range: not kept); it changes with what the
panel showed before (from the launcher the dark half read lighter toward the top; from a cleaned white screen it did
not), so part of it is the panel's own. A single gray patch is good to about 0.06, the mean of three to 0.02. Lines: 4
px 0.86 / 0.88 (down / across), 3 px 0.69 / 0.69, 2 px 0.48 / 0.45, 1 px 0: detail from 3 panel pixels on.
**The map is used**: `compare` and `around` read the black map of the last target run (an hour) for their gray levels
("each place with its own black"): the Display lab ramp then reads in order (g11 0.73; that morning, with one black,
0.49 between g10 0.70 and g12 0.74). Ghost numbers do not use it (white areas only), so they compare with earlier runs.

What "ghost" measures: pixels the frame dump says are white and at least 3 px away from any ink, and how much darker than expected the
camera sees them. The known page for it is the Display lab: Checker, then White (one Fast pass each), then `make cam-compare`; "Clean
(Quality)", then `make cam-compare` again gives the floor of the same session. On a reader page the previous page's lines sit in the
same rows as the current ones, so a reader ghost shows mostly in the word gaps and margins: compare turns within one session.

**First results (2026-11-16, C922)**: the registration matched every page with text (147-222 SIFT inliers). Display lab Checker then White:
the checkerboard is plainly visible on the panel (`out/cam-diff-white.png`). Without a reference the numbers mix ghost and light
(about 1-2 % in the lower and middle regions even after a Clean); `around tap 140 917` (Clean) with the same page before and after reports
the ghost the Clean removed: **0.8-1.1 % of contrast in the checker area, 0.3-0.6 % in header and footer** (`out/cam-diff-removed.png`).
The Quality push of the Clean shows on the panel from about +0.4 s to +1.5 s after the push call (dark frames at +0.65 to +0.94 s): about
1.1 s of visible waveform, the refresh log's push call returns after 104 ms. The first Ramp shot was blurred (autofocus) and
showed ghosts of earlier screens. **After `lock` and a Clean (in focus, repeated at 640x480 within 0.02)**, lightness on this image's
scale (ink 0, white 1): g0 0.15, g1 0.15, g2 0.17, g3 0.19, g4 0.21, g5 0.23, g6 0.25, g7 0.33, g8 0.40, g9 0.42, g10 0.50, g11 0.65,
g12 0.89, g13 0.95, g14 1.01, g15 1.04: monotonic, but the ends crowd: g0..g6 lie within 0.10 of each other and g12..g15 within 0.15
of white, while g11 to g12 is a jump of 0.24. Consequences for UI tokens (UI.md) and the custom waveform (P7-1): gray(13) wells read
almost white, gray(1)..gray(4) are hard to tell from ink. A faint vertical line shows in the dark bars at the same place in every shot
(x about 410 of 540): a panel or driver artefact, not a ghost. At 30 fps the Clean's Quality push shows on the panel from +0.27 s to
+1.23 s after the push call (0.96 s).

**Reader ghosting (2026-11-16, camera locked, Alice; each block starts from a cleaned page, the ghost is measured against
the same page after the menu's Clean)**: M after 1 turn 1.23 %, after 10 turns 1.54 %, after 30 turns 1.99 % (p95 4.9 / 5.8 /
7.2 %); after 10 turns S 1.36 %, L 1.54 %, XL 1.71 %. No automatic Quality clean in any block (see "Ghost budget"). At 30 turns
the ghost sits mostly in a rectangle over the upper half of the text (2.2-3.2 % per region), the lower middle stays at 0.2-0.8 %
(`out/rg/M-30-diff.png`). One page turn at 30 fps (`around tap 400 500`): first visible change about 0.5 s after the tap, the Fast
pass visible for about 0.17 s, the settle pass done on the panel about 1.6 s after the tap (before `Hint::Page`: see "Ghost budget"). Start times include the camera's own
latency (about 0.2-0.25 s, the same offset appeared on the Clean); durations do not.

**Reader ghosting with `Hint::Page` (P4-6, 2026-11-16 15:40, a new camera session at dusk: exposure 26 ms, focus 45; Alice at M from
page 108, each block: menu Clean, N turns, still, menu Clean on the same page as the reference)**: 1 turn 1.27 %, 9 turns 1.15 %,
10 turns 1.11 % (the 10th is the Quality clean), 29 turns 1.22 % (two cleans on the way, 9 turns since the last), 30 turns 1.14 %
(the 30th is a clean); p95 4.5-5.5 %. The page right after a clean reads 1.1 %: that is this session's floor, so the ghost does
not grow with the number of pages any more (before `Hint::Page`: 1.23 / 1.54 / 1.99 % after 1 / 10 / 30). The refresh log showed
what the cadence promises: 9 Fast, then `page-clean`. Worst region 1.8 % on a short page (4 lines) after a full one: the previous
page's lines are faintly there in the normalised image (`out/rg2/29-diff.png`). `ghostLimit`, `pageCleanEvery` and the settle
defaults stay as they are.

`--source FILE` replays a video in real time instead of a camera (tests of the pipeline without a camera). The panel can show private
messages: the images stay in `out/` (gitignored); never commit, upload or paste them, and do not record screens that show secrets.

