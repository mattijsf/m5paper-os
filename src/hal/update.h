// HAL: writing a new firmware image to the slot that is not running, and what the boot after it was (docs/apps/settings.md
// "Update over Wi-Fi", DECISIONS.md D37). The device uses ESP-IDF's OTA slots and the bootloader's rollback; the simulator
// writes the image to a file so the path can be tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pos::hal {

class Updater {
 public:
  virtual ~Updater() = default;
  // Bytes an image may have (the size of the other slot); 0 where there is no second slot.
  virtual uint32_t slotSize() = 0;
  // Starts an image of `size` bytes in the slot that is not running. False with `error` set if it cannot.
  virtual bool begin(uint32_t size, std::string& error) = 0;
  virtual bool write(const uint8_t* data, size_t n) = 0;
  // The image is complete: it is checked (the chip's own image check) and boots at the next restart. False with `error`.
  virtual bool finish(std::string& error) = 0;
  // Gives an image that was begun up; the running firmware stays the one that boots.
  virtual void abort() = 0;

  // True in the first boot of a new image: until confirm() the bootloader goes back to the firmware from before at the
  // next reset (a crash, the reset button, a power cut).
  virtual bool pendingVerify() = 0;
  virtual void confirm() = 0;
  // True when the last update was given up by the bootloader: this is the firmware from before it.
  virtual bool rolledBack() = 0;
  // "app0" or "app1": the slot this firmware runs from ("" where there are none).
  virtual std::string runningSlot() = 0;
};

}  // namespace pos::hal
