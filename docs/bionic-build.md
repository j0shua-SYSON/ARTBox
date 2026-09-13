# Bionic source build

`python scripts/build_bionic.py` compiles the pinned Android 15 sources selected
in `third_party/bionic/m2-objects.json`. The current selection includes 23
pthread translation units, three libc initialization units, errno accessors,
futex/clone wrappers, open and stat wrappers. It produces 33 AArch64 objects and
combines them with a relocatable link. It does not produce `libc.so` or package
Android code into the app. `--ndk-root`, `--build-dir` and `--jobs` are configurable;
source/cache locations follow the shared Python environment settings.

The selected sources compile unchanged. Bionic's `libc_defaults` uses
`stl: "none"`, so the compiler uses `-nostdinc++` and Bionic's minimal
`libstdc++/include` instead of NDK libc++. The pinned [Soong compiler
configuration](https://android.googlesource.com/platform/build/soong/+/refs/tags/android-15.0.0_r1/cc/config/global.go)
selects `gnu++20` and suppresses `-Wnon-c-typedef-for-linkage`. Both settings are
needed with the pinned NDK compiler: C++17 rejects `constinit` in libc
initialization, and omitting the existing Soong warning exception breaks
`-Werror` on Bionic's allocator header. ARTBox retains warnings as errors.

The compiler reserves x18, x27 and x28 for the native runtime and disables
emulated ELF TLS. These flags alone do not adapt handwritten assembly or TLS
access. The build records a disassembly inventory and all undefined symbols
instead of replacing missing dependencies with placeholders.

## Current evidence and next boundaries

The initial build combines all 33 units successfully. Its partial object has
111 undefined dependencies, 58 `TPIDR_EL0` reads, no `TPIDR_EL0` writes and no
`svc` instructions. One instruction in `__init_additional_stacks` writes x18
for Android's shadow call stack despite `-ffixed-x18`. This object cannot run
safely on Darwin. Syscall assembly and the remaining libc/allocator/loader
components are absent from this source selection, so the inventory makes no
claim about the rest of Bionic.

Next adaptations must provide guest TLS without touching Darwin's thread
pointer, handle compiler-generated stack-guard loads, remove Android's x18
shadow-stack ownership, and resolve the remaining source and platform
dependencies. Compiler-generated outlined atomic helpers also appear in the
undefined-symbol inventory and need an explicit build/runtime choice.

`m2-bionic-objects.json` records the source pin, compiler flags, selected-source
and object hashes, unresolved symbols and instruction counts. CI keeps the
partial object with the hash-verified complete Bionic libc notice. Those are
compile and inspection results, not M2's multithreaded runtime acceptance.
