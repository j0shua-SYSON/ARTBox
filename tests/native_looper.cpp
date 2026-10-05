// SPDX-License-Identifier: MIT
#include "../fixtures/looper/check.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    unsigned mutation = 0;
    if (argc == 2 && std::strcmp(argv[1], "retain-callback") == 0) mutation = 1;
    else if (argc == 2 && std::strcmp(argv[1], "omit-wake") == 0) mutation = 2;
    else if (argc != 1) return 2;
    artbox_looper_result result{};
    const int status = artbox_native_looper_check(mutation, &result);
    std::printf("{\"status\":%d,\"cases\":%u,\"failure\":%u,\"wake_threads\":%u,"
                "\"message_calls\":%u,\"fd_callbacks\":%u,\"timer_callbacks\":%u}\n",
                status, result.cases, result.failure, result.wake_threads,
                result.message_calls, result.fd_callbacks, result.timer_callbacks);
    return status == 0 ? 0 : 1;
}
