// doctest runner for the host unit tests (pio test -e test, make test).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "hal/platform.h"

// Log sink for the tests: silent unless POS_TEST_LOG is set.
#include <cstdio>
#include <cstdlib>
namespace pos::hal {
void logWrite(const char* line) {
  if (std::getenv("POS_TEST_LOG")) std::fputs(line, stderr);
}
}  // namespace pos::hal

int main(int argc, char** argv) {
  doctest::Context ctx;
  ctx.applyCommandLine(argc, argv);
  return ctx.run();
}
