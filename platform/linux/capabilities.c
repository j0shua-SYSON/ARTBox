// Original ARTBox host bridge. MIT. Forward to the real Linux kernel ABI.
#include <sys/capability.h>
#include <sys/syscall.h>
#include <unistd.h>

int capget(cap_user_header_t header, cap_user_data_t data) {
  return (int)syscall(SYS_capget, header, data);
}

int capset(cap_user_header_t header, const cap_user_data_t data) {
  return (int)syscall(SYS_capset, header, data);
}
