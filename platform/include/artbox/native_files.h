#ifndef ARTBOX_NATIVE_FILES_H
#define ARTBOX_NATIVE_FILES_H
#include "artbox/vfs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_native_files artbox_native_files;
/* Open a trusted existing root with public POSIX descriptor-relative APIs.
 * No path below it may traverse a symlink. Windows currently returns ENOTSUP;
 * its portable core tests use an injected filesystem provider. */
int artbox_native_files_open(const char *root, artbox_native_files **files);
artbox_file_ops artbox_native_files_ops(artbox_native_files *files);
/* Fresh zero-filled temporary backing in a trusted private root, unlinked
 * before publication. The returned handle uses native_files_ops' close/map
 * callbacks. Output is unchanged on failure. No global temporary directory. */
int artbox_native_files_temporary(void *files, size_t length, void **file);
int artbox_native_files_close(artbox_native_files *files);
#ifdef __cplusplus
}
#endif
#endif
