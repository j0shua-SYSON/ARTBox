// SPDX-License-Identifier: MIT
#include <stddef.h>
#include <stdio.h>

// ART's class linker supplies alloca storage to BitVector without clearing it.
// Exercise both automatic arrays and the compiler's dynamic-allocation policy.
static_assert(sizeof(unsigned) == 4);

__attribute__((noinline)) static unsigned automatic_words() {
  volatile unsigned words[3];
  return words[0] | words[1] | words[2];
}

__attribute__((noinline)) static unsigned dynamic_words(size_t count) {
  volatile unsigned* words = static_cast<unsigned*>(__builtin_alloca(count * sizeof(unsigned)));
  unsigned bits = 0;
  for (size_t i = 0; i < count; ++i) bits |= words[i];
  return bits;
}

int main() {
  unsigned zero_cases = automatic_words() == 0 ? 1u : 0u;
  // 255 words is the pinned String linker's stack-allocation size.
  const size_t sizes[] = {3, 255, 256, 1024};
  for (size_t i = 0; i < 4; ++i) {
    if (dynamic_words(sizes[i]) == 0) zero_cases |= 1u << (i + 1);
  }
  if (zero_cases == 31) {
    puts("stack initialization: 5/5 zero");
    return 0;
  }
  if (zero_cases == 0) {
    puts("stack initialization: 5/5 nonzero");
    return 42;
  }
  fprintf(stderr, "inconsistent stack initialization: zero mask %u\n", zero_cases);
  return 43;
}
