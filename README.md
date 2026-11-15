# PaperOS

A small operating environment for the M5Stack M5PaperS3 (ESP32-S3, 4.7" e-ink, touch):
launcher, e-book reader, Home Assistant remote, WhatsApp and mail inbox. Built for long battery
life and calm, quick e-ink rendering. Runs on the device and in a desktop simulator.

Status: Phase 0 (foundation) done. See `docs/STATUS.md` and `docs/ROADMAP.md`.

## Quick start (macOS)

```bash
brew install sdl2-compat uv
uv tool install --python 3.12 platformio
make shot      # builds the simulator, writes out/shot.png
make run       # simulator window
make flash     # build and flash a connected PaperS3
```

Documentation is in `docs/` (start with `docs/STATUS.md`).
