/* Real Linux Binder reference, private binderfs device. SPDX-License-Identifier: MIT */
#include "../fixtures/binder-device/check.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/android/binderfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int open_device(void *context) {
    int fd = open(context, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    return fd < 0 ? -errno : fd;
}
static int close_device(void *context, int fd) {
    (void)context;
    return close(fd) ? -errno : 0;
}
static int64_t call_device(void *context, int fd, uint32_t request, uint64_t argument) {
    (void)context;
    int result = ioctl(fd, (unsigned long)request, (unsigned long)argument);
    return result < 0 ? -errno : result;
}
static void pause_device(void *context) {
    (void)context;
    const struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
int main(int argc, char **argv) {
    if (argc != 3) return 2; /* control path, expected newly-created device path */
    int control = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (control < 0) { perror("binder-control"); return 2; }
    struct binderfs_device device = {0};
    strcpy(device.name, "artbox");
    if (ioctl(control, BINDER_CTL_ADD, &device)) { perror("BINDER_CTL_ADD"); return 2; }
    if (close(control)) return 2;
    const artbox_binder_device_ops ops = {open_device, close_device, call_device, pause_device};
    int cases = artbox_binder_device_check(argv[2], &ops);
    if (cases < 0) return 1;
    printf("{\"protocol\":8,\"cases\":%d,\"fresh_binderfs_context\":true,\"passed\":true}\n", cases);
    return 0;
}
