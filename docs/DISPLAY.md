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
  Content,    // new content in place: list rows, values, a page of text
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
| Screen | Fast full screen | Text after 600 ms; **Quality instead** if the ghost budget is spent | navigation is the natural place to hide a clean |
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
moment (2 s without input). The reader counts page turns the same way; with defaults that is
roughly one clean flash every 8 to 10 pages, never in the middle of reading a page.

`ghostLimit`, the settle delay and the idle delay live in `config.json` under `display` so they
can be tuned on the device without rebuilding (task P7-1).

## As implemented (`core/display_manager.{h,cpp}`, P1-3)

- API: `invalidate(Rect, Hint)`, `commit()`, `tick(nowMs)`, `nextDeadlineMs()`, `forceClean()`,
  `ghostScore()`, `refreshLogTail()`, `shadowPacked()`. Parameters in `DisplayParams`
  (`ghostLimit` 8, `settleMs` 600, `idleCleanMs` 2000). Unit tests: `tests/unit/display_manager_test.cpp`.
- One pending rect per hint (invalidations of the same hint are united). `commit()` handles
  Feedback, Content, Screen, Image in that order; later rects diff against the shadow the
  earlier ones updated.
- The shadow is the quantised canvas of everything pushed (not the thresholded Fast image).
  `forceClean()` at boot makes it valid; before that every pixel counts as changed.
- A settle region merges with any overlapping or touching Fast region and restarts its timer
  (500 ms later change in the same area = new 600 ms from then). Unrelated regions keep their timers.
- Screen at or over `ghostLimit`: Quality over the whole screen, drops pending settles.
  Image: Quality on the changed box only, does not touch the score.
- Idle clean (`score >= 2 x ghostLimit`, 2 s without a push): `tick()` runs `forceClean()`.
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
- Exposure, focus and white balance: macOS's avfoundation cannot lock them; `uvc-util` (not installed) could for a UVC camera. The
  normalisation makes stills robust to auto exposure; in a video a big flash can make auto exposure hunt for a few frames.
- **Camera**: Logitech C922 Pro Stream Webcam (since 2026-11-16), above the device, the panel turned 90 degrees in the image, about
  300x540 camera pixels at 1920x1080. **Measured limits (2026-11-16, room light in the morning)**: about 10 fps whatever is asked (auto
  exposure lengthens the frame time in dim light; at 1080p and 720p alike), so timings are good to about 100 ms; autofocus hunts on pages
  with little contrast (the Ramp came out blurred, text pages are sharp). More light, or locking exposure and focus (`uvc-util`), would fix both.
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
1.1 s of visible waveform, the refresh log's push call returns after 104 ms. The Ramp: g0..g15 measured monotonic except g11 darker than
g10 and g5 = g6 (a blurred shot with ghosts of earlier screens visible: a vertical line and a wavy band; repeat after a Clean, in focus).

`--source FILE` replays a video in real time instead of a camera (tests of the pipeline without a camera). The panel can show private
messages: the images stay in `out/` (gitignored); never commit, upload or paste them, and do not record screens that show secrets.

