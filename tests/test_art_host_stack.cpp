#include "artbox_host_stack.h"
#include <climits>
#include <cstdio>
#include <limits>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d failed\n", __LINE__); return 1; } } while (0)

int main() {
  size_t size = 123;
  CHECK(!artbox::host_stack_size(1, 0, nullptr));
  CHECK(!artbox::host_stack_size(0, 32768, &size) && size == 123);
  CHECK(!artbox::host_stack_size(-1, 32768, &size) && size == 123);
  CHECK(!artbox::host_stack_size(LONG_MIN, 32768, &size) && size == 123);
  CHECK(artbox::host_stack_size(4096, 32768, &size) && size == 32768);
  CHECK(artbox::host_stack_size(65536, 32768, &size) && size == 65536);
  CHECK(artbox::host_stack_size(1, 0, &size) && size == 1);
  CHECK(artbox::host_stack_size(1, std::numeric_limits<size_t>::max(), &size) &&
        size == std::numeric_limits<size_t>::max());
  if (static_cast<uintmax_t>(LONG_MAX) <= std::numeric_limits<size_t>::max()) {
    CHECK(artbox::host_stack_size(LONG_MAX, 0, &size) &&
          size == static_cast<size_t>(LONG_MAX));
  } else {
    CHECK(!artbox::host_stack_size(LONG_MAX, 0, &size) &&
          size == std::numeric_limits<size_t>::max());
  }
  std::puts("ART host stack minima: 9 cases passed");
  return 0;
}
