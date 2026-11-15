# PaperOS developer commands. `make help` lists them.
# PlatformIO is installed as a uv tool: uv tool install --python 3.12 platformio

PIO   ?= $(shell command -v pio || echo $(HOME)/.local/bin/pio)
SIM   := .pio/build/sim/program
PORT  ?= $(firstword $(wildcard /dev/cu.usbmodem*))

.PHONY: help sim run shot fw flash monitor device-shot backup books clean check

help:
	@echo "make sim      build the simulator"
	@echo "make run      build + open the simulator window (mouse = touch, S = screenshot, Q = quit)"
	@echo "make shot     build + render one headless frame to out/shot.png"
	@echo "make fw       build the device firmware"
	@echo "make flash    build + flash the device (PORT=$(PORT))"
	@echo "make monitor  serial log from the device"
	@echo "make device-shot  dump the frame from the device over USB to out/device.png (tools/device.py)"
	@echo "make backup   read the whole device flash to backup/flash-16MB.bin"
	@echo "make books    download public-domain test books into sim-data/sd/books/"
	@echo "make check    what must pass before a task is done: sim build, headless shot, firmware build"

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

monitor:
	$(PIO) device monitor -p $(PORT) -b 115200

device-shot:
	@mkdir -p out
	$(HOME)/.platformio/penv/bin/python tools/device.py shot out/device.png

backup:
	@mkdir -p backup
	$(HOME)/.platformio/penv/bin/esptool --port $(PORT) --baud 921600 read-flash 0 0x1000000 backup/flash-16MB.bin

books:
	tools/fetch-books.sh

check: shot fw

clean:
	$(PIO) run -t clean
