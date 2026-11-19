// Firmware update on the device (hal/update.h, DECISIONS.md D37): ESP-IDF's two app slots. The image is written to the
// slot that is not running as it arrives; the bootloader's rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) goes back to
// the firmware from before when a new image is reset before it was confirmed.
#include <cstdio>
#include <cstring>

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "hal/system.h"
#include "platform/device/device.h"

// The Arduino core confirms a new image at once in initArduino() unless this says to wait: PaperOS confirms it itself,
// after the firmware has run for a while (Os::loopOnce).
extern "C" bool verifyRollbackLater() { return true; }

namespace pos::hal {

std::string buildId() {
  // (esp_app_get_elf_sha256() gives CONFIG_APP_RETRIEVE_LEN_ELF_SHA digits, 9 in this build: the description has all.)
  const esp_app_desc_t* d = esp_app_get_description();
  char hex[13];
  for (int i = 0; i < 6; ++i) snprintf(hex + 2 * i, 3, "%02x", d->app_elf_sha256[i]);
  return hex;
}

}  // namespace pos::hal

namespace pos::device {

uint32_t Updater::slotSize() {
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  return next ? next->size : 0;
}

bool Updater::begin(uint32_t size, std::string& error) {
  abort();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next || size > next->size) {
    error = "no slot for it";
    return false;
  }
  esp_ota_handle_t h = 0;
  // Sequential writes: the flash is erased as the image arrives (erasing 2.5 MB first would stall the upload for seconds).
  const esp_err_t e = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &h);
  if (e != ESP_OK) {
    error = "esp_ota_begin " + std::to_string(e);
    return false;
  }
  handle_ = h;
  open_ = true;
  return true;
}

// Through a buffer in internal RAM: what arrives over USB lands in PSRAM (pollSerial's buffer, D27), and a flash write stops
// the cache PSRAM is read through.
bool Updater::write(const uint8_t* data, size_t n) {
  static uint8_t buf[4096];
  while (open_ && n > 0) {
    const size_t take = n < sizeof buf ? n : sizeof buf;
    std::memcpy(buf, data, take);
    if (esp_ota_write(handle_, buf, take) != ESP_OK) return false;
    data += take;
    n -= take;
  }
  return open_;
}

bool Updater::finish(std::string& error) {
  if (!open_) {
    error = "nothing was begun";
    return false;
  }
  open_ = false;
  esp_err_t e = esp_ota_end(handle_);  // checks the image (its header, segments and checksum)
  if (e != ESP_OK) {
    error = "not a firmware image for this chip (" + std::to_string(e) + ")";
    return false;
  }
  e = esp_ota_set_boot_partition(esp_ota_get_next_update_partition(nullptr));
  if (e != ESP_OK) {
    error = "esp_ota_set_boot_partition " + std::to_string(e);
    return false;
  }
  return true;
}

void Updater::abort() {
  if (!open_) return;
  open_ = false;
  esp_ota_abort(handle_);
}

bool Updater::pendingVerify() {
  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}

void Updater::confirm() { esp_ota_mark_app_valid_cancel_rollback(); }

bool Updater::rolledBack() { return esp_ota_get_last_invalid_partition() != nullptr; }

// A power-off or restart the firmware does itself shows it runs: without this a new firmware that switches the device
// off within its first seconds (a scheduled refresh) would be taken for one that does not start, and be rolled back.
void confirmUpdateIfPending() {
  Updater u;
  if (u.pendingVerify()) u.confirm();
}

std::string Updater::runningSlot() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? p->label : "";
}

}  // namespace pos::device
