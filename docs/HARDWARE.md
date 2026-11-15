# Hardware: M5Stack M5PaperS3 (SKU C139)

Facts the software depends on. "Source" says where each fact comes from, so it can be re-checked:
**docs** = https://docs.m5stack.com/en/core/papers3, **M5Unified** / **M5GFX** = library source at
version 0.3.0 (in `.pio/libdeps/papers3/`), **measured** = read from our own device (see
`docs/STATUS.md` for the log).

## Specs

| Item | Value | Source |
|---|---|---|
| SoC | ESP32-S3R8, dual core LX7 240 MHz, Wi-Fi 2.4 GHz, BLE | docs |
| Flash | 16 MB quad SPI | docs, measured (`esptool flash-id`) |
| PSRAM | 8 MB octal (must be enabled, OPI mode) | docs |
| Display | 4.7" e-ink ED047TC1, 960x540, 16 gray levels, ~235 ppi, driven directly by the ESP32 (no controller chip) | docs |
| Touch | GT911 capacitive, 2 points, I2C | docs |
| RTC | BM8563, I2C 0x51, alarm can power the board on | docs |
| IMU | BMI270, I2C 0x68 | docs |
| Battery | 3.7 V 1800 mAh LiPo, charger LGS4056H (~330 mA), no fuel gauge: voltage via ADC only | docs |
| Storage | microSD slot (SPI) | docs |
| Sound | passive buzzer | docs |
| Power controller | PMS150G microcontroller: handles the side button, power on/off, download mode | docs |
| USB | USB-C, native USB serial/JTAG (shows up as `/dev/cu.usbmodem*`, VID:PID 303A:1001) | measured |
| Size | 121.5 x 67.7 x 7.7 mm, 89 g | docs |

## Pin map

| Function | GPIO | Source |
|---|---|---|
| EPD data DB0..DB7 | 6, 14, 7, 12, 9, 11, 8, 10 | docs |
| EPD XSTL / XLE / SPV / CKV | 13 / 15 / 17 / 18 | docs |
| EPD power enable | 45 | docs |
| I2C SDA / SCL (GT911, BM8563, BMI270) | 41 / 42 | docs, M5Unified |
| Touch interrupt (active low) | 48 | M5Unified |
| microSD SCK / MOSI / MISO / CS | 39 / 38 / 40 / 47 | docs, M5Unified |
| Battery voltage ADC | 3 | docs |
| Charge status (low = charging) | 4 | M5Unified `Power_Class.inl` |
| USB power detect (>0.2 V = plugged) | 5 | docs |
| Power-off pulse to PMS150G | 44 | M5Unified |
| Buzzer PWM | 21 | docs |
| Status LED | 0 | M5Unified |
| Grove port (yellow / white) | 1 / 2 | docs |

We never drive these pins by hand: M5Unified (`M5.begin()`) detects the board and configures the
display, touch, RTC, IMU, power and SD pins. Our device HAL calls M5Unified.

## Power behaviour (important for the power design, see POWER.md)

- **Side button**: owned by the PMS150G, not a normal GPIO. One click powers on (or resets when
  on); double click powers off; hold while plugging USB = download mode (red LED blinks).
  Whether the ESP32 can read a click while running is **not verified** (task P3-1).
- **Power off from software**: `M5.Power.powerOff()` sends 5 pulses on GPIO 44. In this state the
  board draws ~9 uA (docs). Only the side button, or an RTC alarm (`M5.Power.timerSleep(sec)`),
  brings it back, and that is a full boot.
- **Touch cannot wake from deep sleep or power-off**: GPIO 48 is not an RTC IO on the ESP32-S3.
  M5Unified logs `deepSleep: GPIO48 cannot be used as a deep sleep wakeup source`.
- **Touch can wake from light sleep**: `M5.Power.lightSleep(us, true)` arms a low-level GPIO
  wake on GPIO 48. The program continues after the call. This is our main idle state.
- Documented currents at 4.2 V: 154 mA running (Wi-Fi on), 950 uA "standby" (main power off,
  IMU on), 9.3 uA off. Light-sleep current is not documented: measure it (task P3-5).
- The e-ink image stays visible with no power at all.

## Display driver facts (M5GFX `Panel_EPD`)

- Native panel orientation is landscape 960x540. PaperOS runs portrait 540x960 (rotation is set
  in the device HAL; M5GFX reports 540x960 by default for this board).
- The driver keeps its own frame buffer and runs a background FreeRTOS task (`"epd"`) that plays
  the waveform frames. `M5.Display.display(x,y,w,h)` queues a region; drawing between
  `startWrite()` and `endWrite()` batches into one update; `displayBusy()` / `waitDisplay()`
  report / wait for the task.
- Modes (`M5.Display.setEpdMode(...)`), waveform lengths from `Panel_EPD.inl`:

  | Mode | Frames | Grays | Flash | Use |
  |---|---|---|---|---|
  | `epd_quality` | 15 | 16 | yes (goes through black/white) | images, cleaning ghosts |
  | `epd_text` | 12 | 16 | no | text pages, lists |
  | `epd_fast` | 8 | 2 (black/white) | no | touch feedback, toggles |
  | `epd_fastest` | 5 | 2 | no | drag feedback only; ghosts most |

  The M5Stack docs give reference times for a similar panel (PaperMono): quality 4.7 s (full
  screen clean), text 0.45 s, fast 0.34 s, fastest 0.07 s. Our own numbers: `docs/STATUS.md`.
- Custom waveforms are possible: `Panel_EPD::config_detail_t` takes replacement LUTs
  (`lut_quality`, `lut_text`, `lut_fast`, `lut_fastest`). Only touch this in task P7-1.
- M5Stack warns: long runs of fast partial updates without a full refresh build up DC bias and
  can damage the panel. The DisplayManager's ghost budget (DISPLAY.md) exists for this reason too.

## Toolchain facts (verified on this Mac, 2026-11-15)

- PlatformIO Core 6.2.0, installed with `uv tool install --python 3.12 platformio`
  (binary: `~/.local/bin/pio`).
- Device platform: pioarduino `55.03.312-1` (Arduino core 3.x on ESP-IDF 5.x). The official
  `espressif32` platform is stuck on Arduino core 2.x; do not switch to it.
- Board definition: `esp32-s3-devkitc-1` with overrides in `platformio.ini` (16 MB flash, OPI
  PSRAM, native USB CDC). There is no `m5stack-papers3` board in PlatformIO.
- `esptool` lives at `~/.platformio/penv/bin/esptool`.
- A full image of the factory firmware is in `backup/factory-16MB.bin` (not in git). Restore:
  `~/.platformio/penv/bin/esptool --port /dev/cu.usbmodem101 write-flash 0 backup/factory-16MB.bin`.
