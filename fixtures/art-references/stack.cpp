// SPDX-License-Identifier: MIT
// Exercise the actual AOSP compilation unit; do not duplicate its register-copy logic.
#include "interpreter/interpreter_common.cc"

extern "C" __attribute__((visibility("default")))
int artbox_art_stack_check(void* first, void* second, uint32_t first_bits,
                          uint32_t second_bits, uint32_t* observed, void* storage) {
  using namespace art;
  using namespace art::interpreter;
  int cases = 0;
#define REQUIRE(x) do { ++cases; if (!(x)) return -cases; } while (0)
  REQUIRE(ShadowFrame::ComputeSize(8) <= 256);
  alignas(ShadowFrame) uint8_t frames[512];
  auto* caller = ShadowFrame::CreateShadowFrameImpl(8, nullptr, 0, frames);
  auto* callee = ShadowFrame::CreateShadowFrameImpl(8, nullptr, 0, frames + 256);
  (void)storage;
  auto* a = static_cast<mirror::Object*>(first);
  auto* b = static_cast<mirror::Object*>(second);
  REQUIRE(caller->NumberOfVRegs() == 8 && callee->NumberOfVRegs() == 8);
  caller->SetVRegReference(0, a);
  caller->SetVReg(1, 0x12345678);
  caller->SetVRegReference(2, nullptr);
  caller->SetVRegReference(3, b);
  REQUIRE(static_cast<uint32_t>(caller->GetVReg(0)) == first_bits &&
          caller->GetVRegReference<kVerifyNone>(0) == a);
  const uint32_t args[Instruction::kMaxVarArgRegs] = {3, 1, 0, 2, 0};
  CopyRegisters<false>(*caller, callee, args, 0, 2, 4);
  REQUIRE(callee->GetVRegReference<kVerifyNone>(2) == b);
  REQUIRE(static_cast<uint32_t>(callee->GetVReg(2)) == second_bits);
  REQUIRE(callee->GetVReg(3) == 0x12345678 &&
          callee->GetVRegReference<kVerifyNone>(3) == nullptr);
  REQUIRE(callee->GetVRegReference<kVerifyNone>(4) == a);
  REQUIRE(static_cast<uint32_t>(callee->GetVReg(4)) == first_bits);
  REQUIRE(callee->GetVReg(5) == 0 && callee->GetVRegReference<kVerifyNone>(5) == nullptr);
  CopyRegisters<true>(*caller, callee, args, 0, 0, 4);
  REQUIRE(callee->GetVRegReference<kVerifyNone>(0) == a &&
          callee->GetVRegReference<kVerifyNone>(3) == b);
  REQUIRE(callee->GetVReg(1) == 0x12345678 &&
          callee->GetVRegReference<kVerifyNone>(1) == nullptr);
  // A non-moving collector can leave a stale reference beside a primitive vreg.
  *caller->GetVRegAddr(0) = 0x11223344;
  REQUIRE(caller->GetVRegReference<kVerifyNone>(0) == a);
  AssignRegister(callee, *caller, 3, 0);
  REQUIRE(callee->GetVReg(3) == 0x11223344 &&
          callee->GetVRegReference<kVerifyNone>(3) == nullptr);
  caller->SetVRegReference(0, b);
  AssignRegister(callee, *caller, 3, 0);
  REQUIRE(callee->GetVRegReference<kVerifyNone>(3) == b &&
          static_cast<uint32_t>(callee->GetVReg(3)) == second_bits);
  caller->SetVRegReference(4, a);
  caller->SetVRegReference(5, b);
  caller->SetVRegLong(4, INT64_C(0x123456789abcdef0));
  REQUIRE(caller->GetVRegLong(4) == INT64_C(0x123456789abcdef0) &&
          caller->GetVRegReference<kVerifyNone>(4) == nullptr &&
          caller->GetVRegReference<kVerifyNone>(5) == nullptr);
  callee->SetVRegReference(4, a);
  callee->SetVRegReference(5, b);
  CopyRegisters<true>(*caller, callee, args, 4, 4, 2);
  REQUIRE(callee->GetVRegLong(4) == INT64_C(0x123456789abcdef0) &&
          callee->GetVRegReference<kVerifyNone>(4) == nullptr &&
          callee->GetVRegReference<kVerifyNone>(5) == nullptr);
  *reinterpret_cast<volatile uint64_t*>(callee->GetVRegReference<kVerifyNone>(0)) =
      UINT64_C(0x123456789abcdef0);
  REQUIRE(*static_cast<volatile uint64_t*>(first) == UINT64_C(0x123456789abcdef0));
  *reinterpret_cast<volatile uint64_t*>(callee->GetVRegReference<kVerifyNone>(3)) =
      UINT64_C(0xfedcba9876543210);
  REQUIRE(*static_cast<volatile uint64_t*>(second) == UINT64_C(0xfedcba9876543210));
  observed[0] = kPoisonHeapReferences;
  observed[1] = first_bits;
  observed[2] = second_bits;
  observed[3] = cases;
  callee->~ShadowFrame();
  caller->~ShadowFrame();
  return cases;
#undef REQUIRE
}
