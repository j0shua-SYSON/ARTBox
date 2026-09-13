#include <stdint.h>

static const uint64_t constant = UINT64_C(0x123456789abcdef0);
static const uint64_t* volatile relocated_pointer = &constant;
static uint64_t constructor_count;
static volatile uint64_t zero_fill[128];
const char artbox_dynamic_message[] = "hello from dynamic Bionic\n";

__attribute__((constructor)) static void initialize_probe(void) {
    ++constructor_count;
    zero_fill[127] = *relocated_pointer;
}

uint64_t artbox_dynamic_probe(void) {
    return constructor_count == 1 && zero_fill[0] == 0 ? zero_fill[127] : 0;
}
