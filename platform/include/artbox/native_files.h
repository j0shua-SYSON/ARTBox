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
int artbox_native_files_close(artbox_native_files *files);
#ifdef __cplusplus
}
#endif
#endif
