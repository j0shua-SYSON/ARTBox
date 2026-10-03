// SPDX-License-Identifier: MIT
#include "thread_state.h"
#include "runtime.h"
#include "thread-current-inl.h"
#include "gc/heap.h"

extern "C" bool artbox_art_thread_state(void* expected_jni, uintptr_t* thread,
                                       size_t** sampler_slot) {
  auto* runtime = art::Runtime::Current();
  if (!runtime || !runtime->GetHeap() || !thread || !sampler_slot) return false;
  auto* self = art::Thread::Current();
  void* actual_jni = self ? static_cast<void*>(self->GetJniEnv()) : nullptr;
  if (actual_jni != expected_jni) return false;
  auto& sampler = runtime->GetHeap()->GetHeapSampler();
  // The fixture changes a counter only while sampling is disabled.
  if (sampler.IsEnabled()) return false;
  *thread = reinterpret_cast<uintptr_t>(self);
  *sampler_slot = sampler.GetBytesUntilSample();
  return *sampler_slot != nullptr;
}
