// The CLI and iOS app execute the same signed Bionic acceptance runner.
#include "artbox/native_bionic.h"
#include <stdio.h>
#include <string.h>
static void result(void *context, const char *text, size_t length) {
    (void)context;
    fwrite(text, 1, length, stdout); putchar('\n');
}
int main(int argc, char **argv) {
    if (argc != 10 && (argc != 11 || strcmp(argv[10], "--sampled"))) return 2;
    artbox_bionic_input input = {{0}, {0}, argv[9], argc == 11};
    for (unsigned i = 0; i < 4; ++i) { input.frameworks[i] = argv[1+2*i]; input.elfs[i] = argv[2+2*i]; }
    const artbox_host host = {result, NULL};
    return artbox_run_native_bionic(&input, &host);
}
