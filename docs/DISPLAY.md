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

Thresholding for the Fast pass happens in `hal::Display` on the device (the panel does it) and
in the emulation in the simulator. The canvas always holds the full-gray frame.

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
- **Screen with flat gray areas (2026-11-16)**: a Fast pass thresholds gray to black or white, so gray *areas* (the launcher's and
  Home's gray(13) icon wells, the keyboard) were missing from the Fast pass and arrived only with the settle pass 0.6 s later: a second
  refresh and on the panel a dark sweep. Now the diff of a
  Screen pass also counts changed pixels of flat gray (levels 1..14 with the same level 2 px away on all four sides, so anti-aliased text
  edges and 1 px dividers do not count; white or black 8-byte runs are skipped; it stops at the limit). At `grayAreaMin` (2000 px, a
  disc of radius 25; `DisplayParams`, not in `config.json`) the pass is **one Text push** of the changed box (`why=screen-gray`): the final
  screen at once, no settle afterwards, settles inside the box dropped. Ghost score: half of what the Fast pass would add (what Fast then
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
| reader page turn | 516 ms | **199 ms** (176 ms when the next page was prefetched) | fast text, M5GFX patch 1, prefetch, `Hint::Page` |
| open a screen (Settings row) | 807 ms (246 idle) | **306 ms** (247 idle) | fast text, M5GFX patches, 64 KB data cache (D25) |
| small control (stepper) | 52 ms | **6 ms** | config written 1 s later, not on the tap |

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
- **What remains**: that collision still costs ~60 ms (306 vs 247 idle); next levers in D25 "Next candidates" and fewer
  full-screen settle passes.

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
- **Lock at the start of a session**: `cam.py lock` puts exposure (what auto exposure chose, at most 33 ms), white balance and focus
  to fixed values through `uvc-util` (IOKit, no camera permission needed) and writes them to `out/cam-lock.json`. Focus: a sweep over
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
| `around tap X Y` / `long X Y` / `swipe X1 Y1 X2 Y2` | frame dump, video, the input through the serial port, the serial log read line by line, `>refresh`, frame dump after. Each push in the refresh log gets a host time (its `[disp] push` line arrives when the push call returns), and the video says per push when its rect starts and stops changing on the panel. Writes `out/cam-timing.png` (change per frame, push markers), `out/cam-frames.png` (frames with the most change), `out/cam-around.mp4`, and `compare` of the last frame against the frame dump after. Refuses a serial port that another process holds. |
| `compare --ref IMG` | ghost relative to a camera image of the same page after Clean (Quality) in the same session: the uneven light cancels out. Use this for ghost numbers. |
| `selftest` | synthetic camera images (perspective, uneven light, blur, noise): registration within 0.5 px, the ghost of Checker under White found in the right region and rising with the amount, timing of a synthetic Fast push within a frame. |

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

`--source FILE` replays a video in real time instead of a camera (tests of the pipeline without a camera). The panel can show private
messages: the images stay in `out/` (gitignored); never commit, upload or paste them, and do not record screens that show secrets.

