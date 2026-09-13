// Original ARTBox integration code, MIT. Built with the pinned Bionic headers.
// This controlled fixture supplies the shared object normally owned by linker64.
#include <new>
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
