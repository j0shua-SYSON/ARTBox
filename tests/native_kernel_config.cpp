// SPDX-License-Identifier: MIT
#include <cstdint>
#include <cstdio>
extern "C" int32_t artbox_kernel_config_check(unsigned);
int main() {
    const int32_t cases = artbox_kernel_config_check(0);
    const int32_t comments = artbox_kernel_config_check(1);
    const int32_t relaxed = artbox_kernel_config_check(2);
    std::printf("{\"cases\":%d,\"comments_control\":%d,\"relaxed_control\":%d}\n", cases, comments, relaxed);
    return cases == 89 && comments == -104 && relaxed == -105 ? 0 : 1;
}
