// SPDX-License-Identifier: MIT
#include "artbox/native_art.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
static pthread_mutex_t console_lock = PTHREAD_MUTEX_INITIALIZER;
static char console_text[65536];
static size_t console_used;
static int console_overflow;
static void capture(void *context, const char *text, size_t length) {
    if(context) return; // Negative control: drop actual guest output at the sink.
    pthread_mutex_lock(&console_lock);
    if(length >= sizeof(console_text)-console_used) console_overflow=1;
    else {
        memcpy(console_text+console_used,text,length); console_used+=length;
        console_text[console_used]=0;
    }
    pthread_mutex_unlock(&console_lock);
}
static void result(void *context, const char *text, size_t length) {
    (void)context;
    fwrite(text, 1, length, stdout); putchar('\n');
}
int main(int argc, char **argv) {
    if (argc != 32) return 2;
    artbox_libcore_input input = {{0}, {0}, argv[31]};
    for (unsigned i = 0; i < 15; ++i) {
        input.frameworks[i] = argv[1 + 2*i]; input.elfs[i] = argv[2 + 2*i];
    }
    const artbox_host host = {result, NULL};
    const artbox_host console = {capture, getenv("ARTBOX_TEST_DROP_CONSOLE")};
    int status=artbox_run_native_art_runtime_logged(&input, &host, &console);
    if(status) return status;
    if(console_overflow || !strstr(console_text,"hello from ARTBox ART\n") ||
       !strstr(console_text,"ARTBox: signed ART lifecycle checks passed\n")) {
        fputs("ARTBox console: missing guest output\n",stderr);
        return 3;
    }
    fputs("ARTBox console: hello and lifecycle observed\n",stderr);
    return 0;
}
