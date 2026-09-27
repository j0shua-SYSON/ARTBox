/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <unistd.h>

/* Only the translated guest executes this caller. It must never create a child. */
int artbox_vfork_rejection_check(void) {
    pid_t before = getpid();
    if (before <= 0) return -1;
    errno = 0;
    if (vfork() != -1 || errno != ENOSYS) return -2;
    if (getpid() != before) return -3;
    return 2;
}
