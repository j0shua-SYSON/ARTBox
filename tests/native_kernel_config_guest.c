// SPDX-License-Identifier: MIT
#include "artbox/native_art.h"
#include <stdio.h>
static void result(void *context, const char *text, size_t length) {
    (void)context;
    fwrite(text, 1, length, stdout); putchar('\n');
}
int main(int argc, char **argv) {
    if (argc != 12) return 2;
    artbox_kernel_config_input input = {{0}, {0}, argv[11]};
    for (unsigned i = 0; i < 5; ++i) {
        input.frameworks[i] = argv[1 + 2*i]; input.elfs[i] = argv[2 + 2*i];
    }
    const artbox_host host = {result, NULL};
    return artbox_run_native_kernel_config(&input, &host);
}
