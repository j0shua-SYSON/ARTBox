# Signed ART JavaVM execution

At `68400feb1b39a4a5b97b0d53b2cffecf14bb4c96`, real AOSP ART executes the hello
DEX through signed native frameworks on macOS ARM64. All
[17 host jobs](https://github.com/j0shua-SYSON/ARTBox/actions/runs/37186348661) and
the [iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/37186348665)
pass. This is managed execution, beyond the earlier constructor/bootstrap checks.

The run loads 15 images and executes 38 constructors. A real Bionic worker
creates the JavaVM with switch interpretation, no JIT and no profiling cache.
It prints `hello from ARTBox ART`, preserves a managed graph with checksum 6496
across explicit collection, exercises three exceptions and four native
attachment cycles, verifies thread-local isolation, destroys the VM and reaps
eight native threads. A separate process starts ART without hello.dex and fails
the intended class lookup. No checks are skipped to obtain a passing result.

| Single Mac correctness run | Observation |
| --- | ---: |
| JavaVM creation | 73.569 ms |
| Load and relocation | 71.803 ms |
| Entire bootstrap/runtime phase | 79.224 ms |
| Managed bytes before shutdown | 636,072 |
| Peak process RSS | 66,977,792 bytes |
| Collections before / after explicit GC | 0 / 1 |

The bootstrap phase includes JavaVM creation; these rows must not be summed as
independent phases. Timings include a traced acceptance workload, not sustained
interpreter throughput or device performance. AOT/OAT and host dex2oat remain
future coverage.

Independent verification checks the runtime artifact digest
`5b5118db5d719380f1f0ed0719ea4f9908d7c576f2517f1bd8b0f26fdde7c7e1`,
88 corresponding project sources, the ARM64 Mach-O runner, two boot DEX files,
the managed fixture, hello and ICU data. The complete ART link separately
verifies 465 object inputs, 212 canonical project sources and 44 notices.
Its 2,061,968-instruction boundary contains no SVC, direct thread-pointer access
or unknown instruction; three x18 mentions are approved read-only snapshots.
The same 21,502,096-byte libart ELF has verified Mac and iOS signed layouts.

The fix enabling this run is plain private futex requeue, used by unchanged ART
condition variables. Its 18-case NDK caller passes both signed Bionic modes and
both Linux syscall paths. Native Linux additionally compares actual blocked
waiter movement, masks, isolation, timeouts and 128 races against the kernel.
The earlier worker-stack, signal-34 and virtual cwd prerequisites remain tested.

M3 remains open until the app embeds the runtime and its actual stdout reaches
the console through the shared entry. The verified IPA at this checkpoint still
contains M1/M2 diagnostics. Physical-device execution is waived and unverified;
successful Mac execution or signed packaging does not establish iPhone behavior.
