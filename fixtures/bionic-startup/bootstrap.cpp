// Original ARTBox integration code, MIT. Built with the pinned Bionic headers.
// This controlled fixture supplies the shared object normally owned by linker64.
#include <new>
#include <errno.h>
#include <unistd.h>
#include "pthread_internal.h"
#include "artbox/threads.h"
#include "private/bionic_globals.h"
#include "private/bionic_tls.h"
#include "private/KernelArgumentBlock.h"
#include "gwp_asan/platform_specific/guarded_pool_allocator_tls.h"
#include "gwp_asan/common.h"

static libc_shared_globals shared;
static bionic_tcb bootstrap_tcb;
alignas(gwp_asan::ThreadLocalPackedVariables)
static unsigned char gwp_storage[sizeof(gwp_asan::ThreadLocalPackedVariables)];
extern "C" uint64_t artbox_bootstrap_stage;
uint64_t artbox_bootstrap_stage;
static uint64_t guarded_samples;

extern "C" libc_shared_globals* __loader_shared_globals() { return &shared; }
extern "C" uint64_t artbox_bootstrap_gwp_enabled() { return shared.gwp_asan_state != nullptr; }
extern "C" uint64_t artbox_bootstrap_guarded_samples() { return guarded_samples; }
extern "C" uint64_t artbox_bootstrap_is_guarded(const void* p) {
  return shared.gwp_asan_state && shared.gwp_asan_state->pointerIsMine(p);
}
extern "C" void artbox_bootstrap_note_allocation(const void* p) {
  if (shared.gwp_asan_state && shared.gwp_asan_state->pointerIsMine(p)) ++guarded_samples;
}

// No stack protector: Bionic reseeds its guard while this frame is active.
// No guest ELF TLS templates exist in this deliberately bounded fixture.
extern "C" int artbox_bootstrap_main(void* raw_args) {
  if (artbox_bootstrap_stage) return -114;
  artbox_bootstrap_stage = 1;
  KernelArgumentBlock args(raw_args);
  shared.init_environ = args.envp;
  shared.init_progname = args.argv[0];
  __libc_init_main_thread_early(args, &bootstrap_tcb);
  auto* gwp = new (gwp_storage) gwp_asan::ThreadLocalPackedVariables;
  bootstrap_tcb.tls_slot(TLS_SLOT_NATIVE_BRIDGE_GUEST_STATE) = gwp;
  artbox_bootstrap_stage = 2;
  shared.static_tls_layout.reserve_exe_segment_and_tcb(nullptr, args.argv[0]);
  shared.static_tls_layout.reserve_bionic_tls();
  shared.static_tls_layout.finish_layout();
  artbox_bootstrap_stage = 3;
  __libc_init_main_thread_late();
  artbox_bootstrap_stage = 4;
  __libc_init_main_thread_final();
  artbox_bootstrap_stage = 5;
  return 0;
}


// This bridge is intentionally limited to Bionic's own pthread clone call.
// The private layouts remain on the Android side of the fixed-word ABI.
extern "C" int64_t artbox_host_pthread_clone(const artbox_thread_start*);
extern "C" pid_t __bionic_clone(uint32_t flags, void* child_stack, int* parent_tid,
                                void* tls, int* child_tid, int (*fn)(void*), void* arg) {
  if (flags != ARTBOX_PTHREAD_CLONE_FLAGS) { errno = ENOSYS; return -1; }
  if (!tls || !arg || !fn) { errno = EINVAL; return -1; }
  auto* slots = static_cast<void**>(tls);
  auto* thread = static_cast<pthread_internal_t*>(slots[TLS_SLOT_THREAD_ID]);
  if (thread != arg || parent_tid != &thread->tid || child_tid != &thread->tid ||
      reinterpret_cast<uintptr_t>(child_stack) != thread->stack_top) {
    errno = EINVAL; return -1;
  }
  const uint64_t page = getpagesize();
  uint64_t base = reinterpret_cast<uintptr_t>(thread->attr.stack_base);
  // Bionic includes the low guard in its automatic-stack attributes. A caller
  // supplied stack lives outside the separate TLS mapping and has no such guard.
  if (thread->attr.stack_base == thread->mmap_base) base += thread->attr.guard_size;
  const uint64_t top = thread->stack_top & ~(page - 1);
  if (base >= top || base % page) { errno = EINVAL; return -1; }
  artbox_thread_start start{flags, base, top - base, reinterpret_cast<uintptr_t>(tls),
      reinterpret_cast<uintptr_t>(parent_tid), reinterpret_cast<uintptr_t>(child_tid),
      reinterpret_cast<uintptr_t>(fn), reinterpret_cast<uintptr_t>(arg)};
  int64_t result = artbox_host_pthread_clone(&start);
  if (result < 0) { errno = static_cast<int>(-result); return -1; }
  return static_cast<pid_t>(result);
}
extern "C" int artbox_bootstrap_thread(void* entry, void* argument) {
  // This guest stack frame survives until the final thread-exit bridge. Its
  // GWP state never crosses a host struct layout or host TLS ABI boundary.
  gwp_asan::ThreadLocalPackedVariables gwp;
  __get_tls()[TLS_SLOT_NATIVE_BRIDGE_GUEST_STATE] = &gwp;
  return reinterpret_cast<int (*)(void*)>(entry)(argument);
}
