#include "artbox/runtime.h"

#include <stdio.h>

static void console_log(void *context, const char *message, size_t length) {
    FILE *stream = context;
    (void)fwrite(message, 1, length, stream);
    (void)fputc('\n', stream);
}

int main(void) {
    const artbox_host host = {console_log, stdout};
    if (artbox_start(&host) != ARTBOX_OK) {
        return 1;
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}
