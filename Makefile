# PaperOS developer commands. `make help` lists them.
# PlatformIO is installed as a uv tool: uv tool install --python 3.12 platformio

PIO   ?= $(shell command -v pio || echo $(HOME)/.local/bin/pio)
SIM   := .pio/build/sim/program
PORT  ?= $(firstword $(wildcard /dev/cu.usbmodem*))

.PHONY: help emoji sim run shot test ui-test ui-accept gallery fw flash ota ota-phone ota-key monitor device-shot view cam-shot cam-video cam-compare cam-target backup books clean check

help:
	@echo "make sim      build the simulator"
	@echo "make run      build + open the simulator window (mouse = touch, S = screenshot, Q = quit)"
	@echo "make shot     build + render one headless frame to out/shot.png"
	@echo "make test      host unit tests (tests/unit, doctest)"
	@echo "make ui-test   run tests/ui/*.script headless, compare screenshots with tests/ui/golden"
	@echo "make ui-accept accept out/ui/*.png as the new goldens (look at them first)"
	@echo "make gallery   out/gallery.html: every UI test screenshot"
	@echo "make fw       build the device firmware"
	@echo "make flash    build + flash the device (PORT=$(PORT))"
	@echo "make ota      build + send the firmware over Wi-Fi (Settings > Developer > Wireless development on; HOST=address)"
	@echo "make ota-phone  build + sign the firmware and put it in the phone's Downloads (adb), for the companion app's Firmware update"
	@echo "make ota-key  once: make the key that signs updates, then make flash"
	@echo "make monitor  serial log from the device"
	@echo "make device-shot  dump the frame from the device over USB to out/device.png (tools/device.py)"
	@echo "make view         live view of the device in a browser, click = tap (tools/view.py; ARGS=--sim, ARGS=--lan)"
	@echo "make cam-shot     webcam still of the panel to out/cam.png (tools/cam.py; CAMERA=name or index)"
	@echo "make cam-video    webcam video to out/cam.mp4 (SECONDS=5, 60 fps if the camera can)"
	@echo "make cam-compare  device-shot + cam-shot, then the ghosting number and out/cam-diff.png"
	@echo "make cam-target   the calibration screen on the device + a still: where the panel is, lens, light, grays, sharpness"
	@echo "make backup   read the whole device flash to backup/flash-16MB.bin"
	@echo "make emoji    copy the emoji atlases to sim-data/ and push them to the device's card (tools/push.py)"
	@echo "make books    download public-domain test books into sim-data/sd/books/ and push them to the device (tools/push.py)"
	@echo "make check    what must pass before a task is done: sim build, unit tests, UI golden tests, firmware build"

sim:
	$(PIO) run -e sim

run: sim
	$(SIM)

shot: sim
	@mkdir -p out
	$(SIM) --headless --shot out/shot.png

fw:
	$(PIO) run -e papers3

flash:
	$(PIO) run -e papers3 -t upload --upload-port $(PORT)

# Firmware update over Wi-Fi (tools/ota.py, DECISIONS.md D37, D39): Settings > Developer > Wireless development on.
# HOST=192.168.1.23 names it; without it the last address, then this Mac's /24, is tried.
ota: fw
	uv run --script tools/ota.py send $(if $(HOST),--host $(HOST)) $(ARGS)

# Once: the signing key (kept outside the repository) and its public half in src/core/update_key.h. Then make flash.
# The signed package for the companion app's Maintenance > Firmware update (D40): out/paperos-<build>.posota, pushed to
# the phone's Downloads over adb when a phone is attached.
ota-phone: fw
	uv run --script tools/ota.py pack --out out/paperos.posota
	@if adb get-state >/dev/null 2>&1; then adb push out/paperos.posota /sdcard/Download/paperos.posota; \
	else echo "no phone on adb: copy out/paperos.posota to the phone yourself"; fi

ota-key:
	uv run --script tools/ota.py keygen

monitor:
	$(PIO) device monitor -p $(PORT) -b 115200

device-shot:
	@mkdir -p out
	$(HOME)/.platformio/penv/bin/python tools/device.py shot out/device.png

# Live view of the device in a browser, click = tap (ARGS=--sim: the simulator, ARGS=--lan: also from a phone)
view:
	$(HOME)/.platformio/penv/bin/python tools/view.py $(ARGS)

CAM     := uv run --script tools/cam.py $(if $(CAMERA),--camera "$(CAMERA)")
SECONDS ?= 5

cam-shot:
	@mkdir -p out
	$(CAM) shot --out out/cam.png

cam-video:
	@mkdir -p out
	$(CAM) video --seconds $(SECONDS) --out out/cam.mp4

cam-compare: device-shot cam-shot
	$(CAM) compare --image out/cam.png --frame out/device.png

# What the camera set-up is worth today (docs/DISPLAY.md "Calibration target"): run it after `cam.py lock`.
cam-target:
	@mkdir -p out
	$(CAM) target

backup:
	@mkdir -p backup
	$(HOME)/.platformio/penv/bin/esptool --port $(PORT) --baud 921600 read-flash 0 0x1000000 backup/flash-16MB.bin

# The emoji atlases (docs/UI.md "Emoji"): into the simulator's card and onto the device's card (tools/push.py).
emoji:
	mkdir -p sim-data/sd/paperos/emoji
	cp assets/emoji/*.bin sim-data/sd/paperos/emoji/
	$(HOME)/.platformio/penv/bin/python tools/push.py --to /sd/paperos/emoji/ assets/emoji/*.bin

books:
	tools/fetch-books.sh
	$(HOME)/.platformio/penv/bin/python tools/push.py sim-data/sd/books/*.epub sim-data/sd/books/*.txt

TEST := .pio/build/test/program

# Builds the host test program with PlatformIO, then runs it directly (its exit code is the result).
test:
	$(PIO) test -e test --without-testing
	$(TEST)

ui-test: sim
	tools/ui-test.sh

ui-accept: sim
	tools/ui-test.sh --accept

gallery:
	tools/gallery.sh

check: sim test ui-test fw

clean:
	$(PIO) run -t clean
