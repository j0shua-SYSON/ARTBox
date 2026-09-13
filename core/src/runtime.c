#include "artbox/runtime.h"

artbox_result artbox_start(const artbox_host *host) {
    static const char ready[] = "ARTBox ready";
    if (host == NULL || host->log == NULL) {
        return ARTBOX_INVALID_ARGUMENT;
    }
    host->log(host->context, ready, sizeof(ready) - 1);
    return ARTBOX_OK;
}
