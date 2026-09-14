# M3 ART execution contract

M2 is merged and tagged `m2` at `a84b9a0`; both Bionic profiles pass 328/328,
and its merged host/Linux and integrated iOS build checks pass. M3 must execute
a real DEX method through the AOSP ART interpreter and expose its string in
the iOS console. Metadata inspection or a replacement DEX interpreter does not
satisfy this milestone. Physical checks remain optional under ADR 0004.

## Source and build

Use the matching AOSP ART `android-15.0.0_r1` revision
`bebbc3cc49f2d9d5420197df0a336fbc3fcbea40`, reviewed under its Apache-2.0 notice.
Pin each imported dependency and retain its own license. Select the runtime
and required class-library subset before expanding the source checkout.
The [upstream build](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/runtime/Android.bp)
separates `libart-runtime` from the compiler library and includes the C++ switch
interpreter. Prefer this runtime target for the initial interpreter-only build.
No ART source is compiled by the first address-space probe.

Build the guest ARM64 runtime ahead of time with reproducible source adaptations,
preserve original ELF inputs, and package native instructions in signed Mach-O.
Native ARM64 macOS acceptance and an ordinary signed arm64 iOS 15 build must use
the same runtime sources and DEX. Keep the portable core's Windows/Linux checks.

## Required execution evidence

- Start real ART with a pinned, reviewed boot class path and the original
  hello-world DEX. Verify class loading, method invocation and exact log text.
- Force C++ switch interpretation initially. Assert at runtime that no JIT,
  profiling code cache, runtime dex2oat process or generated executable mapping
  exists. Record native code/data mapping permissions and emitted-code attempts.
- Exercise a Java allocation, explicit null/bounds exception, method call,
  managed collection and attached-thread lifecycle before claiming usable ART.
  Errors must be observable; an aborted runtime cannot count as a pass.
- Preserve a native upstream Linux reference for the same DEX and selected
  interpreter behavior before introducing compatibility changes.
- Include the shared ART execution entry, DEX and required signed libraries in
  the iOS console; verify all copied payloads, signatures and notices.

Record startup and invocation timings, managed/native memory and concrete
unsupported runtime paths. Interpreter-only M3 acceptance is permitted; it
does not establish AOT/OAT support. Host dex2oat and signed OAT integration follow
as separately measured coverage, without introducing a runtime JIT.

## Early compatibility questions

ART's managed references store 32-bit addresses. The current iOS app has a
4 GiB `__PAGEZERO`, preventing the low-address heap expected by unmodified ART.
Before changing the mapper or reference representation, compare the normal
signed executable layout with a reduced null guard using public linker options.
Reserve non-executable memory with hints only, verify independent ranges,
demand-zero pages, 32-bit pointer round trips and cleanup. Never replace an
existing image or mapping to make a probe pass. A successful Mac probe and
iOS link/signature check do not prove that the heap is available on a device.

[Runtime startup](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/runtime/runtime.cc)
can create a JIT cache for profiling even when compilation is disabled.
`Runtime::Start` also rejects `NoSigChain`. Switching off JIT is therefore only
one part of the boundary: explicit checks, cooperative suspension, stack walking,
native entrypoints and signal behavior each need tests before adaptation.
Generated-code assumptions in nterp and quick entrypoints remain unvalidated.
