// ARTBox host strlcpy feature probe and copy/truncation contract. MIT.
#include <cstdio>
#include <cstring>
#ifdef ARTBOX_TEST_ART_STRLCPY
#include "base/strlcpy.h"
#endif

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d failed\n", __LINE__); return 1; } } while (0)

int main() {
  char buffer[8] = "xxxxxxx";
  CHECK(strlcpy(buffer, "abc", sizeof(buffer)) == 3 && std::strcmp(buffer, "abc") == 0);
  CHECK(strlcpy(buffer, "abcdef", 4) == 6 && std::strcmp(buffer, "abc") == 0 && buffer[4] == 'x');
  CHECK(strlcpy(buffer, "abc", 1) == 3 && buffer[0] == '\0' && buffer[1] == 'b');
  buffer[0] = 'x';
  CHECK(strlcpy(buffer, "abc", 0) == 3 && buffer[0] == 'x');
  CHECK(strlcpy(buffer, "", sizeof(buffer)) == 0 && buffer[0] == '\0');
  std::puts("ART host strlcpy: 5 cases passed");
  return 0;
}
