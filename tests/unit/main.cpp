// doctest runner for the host unit tests (pio test -e test, make test).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "hal/platform.h"
#include "hal/system.h"

// Log sink for the tests: silent unless POS_TEST_LOG is set.
#include <cstdio>
#include <cstdlib>
namespace pos::hal {
void logWrite(const char* line) {
  if (std::getenv("POS_TEST_LOG")) std::fputs(line, stderr);
}
StorageInfo storageInfo(const char*) { return StorageInfo{true, 1000000, 400000}; }
MemoryInfo memoryInfo() { return MemoryInfo{1, 2, 3, 4}; }
void restart() {}
void chime() {}  // core/sound.h counts them
std::string boardName() { return "test"; }
std::string buildId() { return "testbuild"; }
std::string macAddress() { return "00:11:22:33:44:55"; }
}  // namespace pos::hal

int main(int argc, char** argv) {
  doctest::Context ctx;
  ctx.applyCommandLine(argc, argv);
  return ctx.run();
}
