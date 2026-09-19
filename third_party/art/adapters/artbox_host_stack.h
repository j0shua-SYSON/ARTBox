// ARTBox host-build adapter. MIT; see the repository LICENSE.
#ifndef ARTBOX_HOST_STACK_H
#define ARTBOX_HOST_STACK_H

#include <cstddef>
#include <cstdint>
#include <limits>

namespace artbox {
inline bool host_stack_size(long system_minimum, size_t requested, size_t* result) {
  if (result == nullptr || system_minimum <= 0) return false;
  const uintmax_t minimum = static_cast<uintmax_t>(system_minimum);
  if (minimum > std::numeric_limits<size_t>::max()) return false;
  const size_t converted = static_cast<size_t>(minimum);
  *result = requested < converted ? converted : requested;
  return true;
}
}  // namespace artbox
#endif
