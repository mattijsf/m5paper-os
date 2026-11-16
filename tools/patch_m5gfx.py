# PlatformIO pre-build script (platformio.ini: extra_scripts): patches M5GFX 0.3.0 in .pio/libdeps for the device.
# Idempotent; fails the build if the library text is not what the patch was written against (a new M5GFX version).
# Why and what: docs/DISPLAY.md "Tap latency", docs/DECISIONS.md D24. Every patch keeps the panel's pixels identical.
import os
import sys

Import("env")  # noqa: F821 (provided by SCons)

PATCHES = []

# 1. Panel_EPD::writeImage with the panel rotated by 90 or 270 degrees (our portrait UI on the landscape panel): the stock
#    code writes one source row at a time, which on the rotated panel is one 4-bit read-modify-write every ~480 bytes of
#    PSRAM (a cache miss per pixel). While a waveform runs (the update task streams ~1 MB of PSRAM per frame) a page-sized
#    push took 450-530 ms instead of 88. Here 16 source rows are read first, then each panel row gets its 16 pixels as
#    consecutive bytes. Same dithering (Bayer, same formulas), same frame buffer content.
WRITE_IMAGE_OLD = """  void Panel_EPD::writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t* param, bool use_dma)
  {
    uint_fast16_t xs = x, xe = x + w - 1;
    uint_fast16_t ys = y, ye = y + h - 1;
    _update_transferred_rect(xs, ys, xe, ye);
"""
WRITE_IMAGE_NEW = """  // PAPEROS PATCH 3 (tools/patch_m5gfx.py): the quantise-and-write of a tile batch on both cores. A helper task on the
  // other core (priority 1: the EPD update task, priority 2, always goes first) takes chunks of 16 panel rows from an atomic
  // counter, and so does the caller; the caller never waits for a helper that did not take part, so it is never slower than
  // one core. Rows never overlap: the result is the same bytes (">set epd-selftest on").
  struct paperos_tile_job {
    const grayscale_t* blk; int w; int nb; int x; int px0; bool flipx; bool flipy; int pw; int ph; int stride;
    uint8_t* buf; const uint8_t* qlut;
    std::atomic<int> next{0};
    int chunks = 0;
    std::atomic<uint32_t> gen{0}, helper_in{0}, helper_out{0};
  };
  static paperos_tile_job paperos_job;
  static TaskHandle_t paperos_helper = nullptr;
  static constexpr int PAPEROS_CHUNK = 16;

  static void paperos_tile_columns(const paperos_tile_job& j, int i0, int i1)
  {
    for (int i = i0; i < i1; ++i)
    {
      int px = j.px0;       // panel x of b = 0
      int py = j.x + i;     // panel y
      if (j.flipx) { px = j.pw - 1 - px; }
      if (j.flipy) { py = j.ph - 1 - py; }
      const int nb = j.nb;
      const int lo = j.flipx ? px - (nb - 1) : px;  // leftmost panel x of the run
      const uint8_t* bay = &Bayer[(py & 3) << 2];
      const uint8_t* lq[4] = { &j.qlut[bay[0] << 8], &j.qlut[bay[1] << 8], &j.qlut[bay[2] << 8], &j.qlut[bay[3] << 8] };
      uint8_t q[33];
      if (j.flipx) { for (int k = 0; k < nb; ++k) { q[k] = lq[(lo + k) & 3][j.blk[(nb - 1 - k) * j.w + i].get()]; } }
      else         { for (int k = 0; k < nb; ++k) { q[k] = lq[(lo + k) & 3][j.blk[k * j.w + i].get()]; } }
      uint8_t* row = j.buf + py * j.stride;
      int k = 0;
      int qx = lo;
      if (qx & 1) { uint8_t& byte = row[qx >> 1]; byte = (byte & 0xF0) | q[0]; k = 1; ++qx; }
      for (; k + 1 < nb; k += 2, qx += 2) { row[qx >> 1] = (q[k] << 4) | q[k + 1]; }
      if (k < nb) { uint8_t& byte = row[qx >> 1]; byte = (byte & 0x0F) | (q[k] << 4); }
    }
  }

  static void paperos_tile_take(paperos_tile_job& j)
  {
    int c;
    while ((c = j.next.fetch_add(1)) < j.chunks)
    {
      const int i0 = c * PAPEROS_CHUNK;
      paperos_tile_columns(j, i0, std::min(j.w, i0 + PAPEROS_CHUNK));
    }
  }

  static void paperos_helper_task(void*)
  {
    for (;;)
    {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      const uint32_t g = paperos_job.gen.load();
      paperos_job.helper_in.store(g);   // before taking: the caller then knows to wait for helper_out
      paperos_tile_take(paperos_job);
      paperos_job.helper_out.store(g);
    }
  }

  static void paperos_tile_run(paperos_tile_job& j)
  {
    j.chunks = (j.w + PAPEROS_CHUNK - 1) / PAPEROS_CHUNK;
    if (!paperos_helper)
    {
      xTaskCreatePinnedToCore(paperos_helper_task, "epdtile", 3072, nullptr, 1, &paperos_helper, xPortGetCoreID() ^ 1);
    }
    const uint32_t g = j.gen.load() + 1;
    j.gen.store(g);
    j.next.store(0);
    if (paperos_helper && j.chunks > 1) { xTaskNotifyGive(paperos_helper); }
    paperos_tile_take(j);
    if (j.helper_in.load() == g)
    {
      while (j.helper_out.load() != g) { taskYIELD(); }  // it took a chunk of this batch: let it finish
    }
  }

  void Panel_EPD::writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t* param, bool use_dma)
  {
    uint_fast16_t xs = x, xe = x + w - 1;
    uint_fast16_t ys = y, ye = y + h - 1;
    _update_transferred_rect(xs, ys, xe, ye);
    // PAPEROS PATCH 1 (tools/patch_m5gfx.py): tiled write for a rotated panel.
    // B = 32 source rows: each panel row then gets 16 consecutive bytes, so a PSRAM cache line fetched for writing is
    // mostly used (16 rows: 28-35 ms for 540 x 240; measured with ">set epd-selftest on").
    constexpr int B = 32;
    constexpr int MAXW = 560;  // the portrait width (540) and a little
    if ((_internal_rotation & 1) && h >= 2 && w <= MAXW)
    {
      static grayscale_t* blk = nullptr;  // B rows of the source rect (internal RAM: read back column-wise below)
      if (!blk) blk = (grayscale_t*)heap_caps_malloc(B * MAXW * sizeof(grayscale_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (blk)
      {
        const uint_fast8_t r = _internal_rotation;
        const uint_fast8_t rb = 1 << r;
        const bool flipx = rb & 0b11000110;
        const bool flipy = rb & 0b10011100;
        const int pw = _cfg.panel_width;
        const int stride = (pw + 1) >> 1;
        const bool fast = _epd_mode == epd_mode_t::epd_fast || _epd_mode == epd_mode_t::epd_fastest;
        // Quantisation by table: qlut[bayer << 8 | value], the stock formulas of _draw_pixels for this mode (4 KB).
        static uint8_t* qlut = nullptr;
        static int qlut_fast = -1;
        if (!qlut) qlut = (uint8_t*)heap_caps_malloc(16 * 256, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!qlut) { goto stock; }
        if (qlut_fast != (int)fast)
        {
          for (int bv = 0; bv < 16; ++bv)
            for (int v = 0; v < 256; ++v)
              qlut[bv << 8 | v] = fast ? ((v + (bv << 4)) < 248 ? 0 : 0xF) : (uint8_t)std::min(15, std::max(0, (v + bv - 8) >> 4));
          qlut_fast = fast;
        }
        const auto sx = param->src_x32;
        const auto sy0 = param->src_y;
        bool ok = true;
        for (uint_fast16_t y0 = 0; y0 < h; y0 += B)
        {
          const int nb = std::min<int>(B, h - y0);
          for (int b = 0; b < nb; ++b)
          {
            if (param->fp_copy(&blk[b * w], 0, w, param) != (uint32_t)w) { ok = false; break; }
            param->src_x32 = sx;
            param->src_y++;
          }
          if (!ok)
          {
            // A source that skips pixels (transparency; pushSprite of an opaque sprite never does): the stock code takes
            // over from the first row of this block.
            param->src_x32 = sx;
            param->src_y = sy0 + y0;
            y += y0;
            h -= y0;
            break;
          }
          // Source pixel (x + i, y + y0 + b): with r & 1 the panel coordinates swap, then flip as in _draw_pixels.
          // The nb pixels of one panel row are consecutive: quantise them first, then write whole bytes (two pixels)
          // and read-modify-write only a half-used byte at either end.
          // Columns i (= panel rows) are shared between this core and a helper on the other core (PAPEROS PATCH 3).
          paperos_tile_job& j = paperos_job;
          j.blk = blk; j.w = w; j.nb = nb; j.x = x; j.px0 = y + y0; j.flipx = flipx; j.flipy = flipy;
          j.pw = _cfg.panel_width; j.ph = _cfg.panel_height; j.stride = stride; j.buf = _buf; j.qlut = qlut;
          paperos_tile_run(j);
        }
        if (ok) { return; }
      }
    }
    stock:
"""
PATCHES.append(("src/lgfx/v1/platforms/esp32/Panel_EPD.inl", WRITE_IMAGE_OLD, WRITE_IMAGE_NEW, "PAPEROS PATCH 1"))

# 2. Panel_EPD::task_update: every waveform frame converted all 540 rows of the 1 MB step buffer (PSRAM) to drive data,
#    finished rows included. blit_dmabuf writes an all-zero line ("drive nothing") for a row whose pixels are all
#    finished, and such a row cannot change until an update touches it. So a row that reports finished is remembered and
#    gets a zero line without reading PSRAM until the next update covering it. Less PSRAM traffic while a waveform runs:
#    the UI's pushes, diff and render stop crawling. Guard: the first finished line must really be zero, else this patch
#    switches itself off for good.
TASK_A_OLD = """    bool remain = false;

    for (;;) {
      me->_display_busy = remain;"""
TASK_A_NEW = """    bool remain = false;
    // PAPEROS PATCH 2 (tools/patch_m5gfx.py): rows whose pixels all finished their waveform.
    uint8_t* row_idle = (magni_h == 1) ? (uint8_t*)heap_caps_calloc(mh, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : nullptr;
    bool idle_checked = false;

    for (;;) {
      me->_display_busy = remain;"""
TASK_B_OLD = """          size_t panel_idx = ((new_data.x + new_data.y * panel_w) >> 1);"""
TASK_B_NEW = """          // PAPEROS PATCH 2b: an update makes its rows active again.
          if (row_idle && new_data.y < mh) { memset(row_idle + new_data.y, 0, std::min<size_t>(new_data.h, mh - new_data.y)); }
          size_t panel_idx = ((new_data.x + new_data.y * panel_w) >> 1);"""
TASK_C_OLD = """        if (blit_dmabuf(dma_buf, &me->_step_framebuf[(y * memory_w >> 1) * 2], me->_lut_2pixel, w)) {
          remain = true;
        }"""
TASK_C_NEW = """        // PAPEROS PATCH 2c: finished rows get a zero line without reading the step buffer.
        if (row_idle && row_idle[y]) {
          memset(dma_buf, 0, w * sizeof(uint32_t));  // what blit_dmabuf writes for a row with nothing left to drive
        } else if (blit_dmabuf(dma_buf, &me->_step_framebuf[(y * memory_w >> 1) * 2], me->_lut_2pixel, w)) {
          remain = true;
        } else if (row_idle) {
          if (!idle_checked) {
            idle_checked = true;
            for (int i = 0; i < w; ++i) {
              if (dma_buf[i] != 0) { heap_caps_free(row_idle); row_idle = nullptr; break; }  // not understood: stock behaviour
            }
          }
          if (row_idle) { row_idle[y] = 1; }
        }"""
PATCHES.append(("src/lgfx/v1/platforms/esp32/Panel_EPD.inl", TASK_A_OLD, TASK_A_NEW, "PAPEROS PATCH 2"))
PATCHES.append(("src/lgfx/v1/platforms/esp32/Panel_EPD.inl", TASK_B_OLD, TASK_B_NEW, "PAPEROS PATCH 2b"))
PATCHES.append(("src/lgfx/v1/platforms/esp32/Panel_EPD.inl", TASK_C_OLD, TASK_C_NEW, "PAPEROS PATCH 2c"))


def apply(libdir):
    for rel, old, new, marker in PATCHES:
        path = os.path.join(libdir, rel)
        with open(path) as f:
            text = f.read()
        if marker in text:
            continue
        if text.count(old) != 1:
            sys.exit(f"tools/patch_m5gfx.py: {rel} does not contain the expected code (M5GFX version changed?). Update the patch.")
        with open(path, "w") as f:
            f.write(text.replace(old, new))
        print(f"tools/patch_m5gfx.py: applied {marker} to {rel}")


libdeps = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")  # noqa: F821
m5gfx = os.path.join(libdeps, "M5GFX")
if os.path.isdir(m5gfx):
    apply(m5gfx)
else:
    print("tools/patch_m5gfx.py: M5GFX not installed yet; it is patched on the next build")
