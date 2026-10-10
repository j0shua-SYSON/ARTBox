# Android framework bring-up

M5 needs original Activity and event-loop code executing in ART, followed by
managed input and a Metal-backed window. Native window rendering already has
its own acceptance; it does not establish framework execution.

The first reproducible framework input is the unchanged Android 15
`MessageQueue.java` and `android_os_MessageQueue.cpp`, plus their four JNI
headers. `third_party/framework/sources.json` pins these eight files, including
the upstream notice and OS flag declarations, at `android-15.0.0_r1`.
The queue uses the matching AP3A release's read-only tail-tracking value.
The generated accessor is constant for the process lifetime; mutable device
configuration is not introduced for this fixed release flag.

`scripts/build_framework_queue.py` accepts an existing AOSP class-library build,
JDK 17 and NDK r28c. Paths remain configurable. It compiles the original Java
file against the AOSP boot classes and a pinned, compile-only `android-all`
reference. Only the five queue classes and generated `android.os.Flags` enter
the six-class DEX. The reference JAR is never packaged or executed. Every
source, compiler input, generated class and DEX is recorded by hash; an explicit
class allowlist rejects unrelated payload entries. JNI compilation retains the
existing ARM64 instruction audit and emits an unresolved-import inventory for
the original unit and both Android-side acceptance callers.

```
python -B scripts/build_framework_queue.py --classlib-dir build/m3/classlib --fetch-jdk
```

The reference is an AOSP implementation publication from Robolectric, declared
Apache-2.0 in its pinned Maven POM. It is not a runtime substitute for original
framework source. Its build is not established as the same AOSP tag, so the
current queue input does not establish framework resource compatibility. A
broader Activity build must review generated resources and constants rather
than treating this reference as matching runtime resources.

The producer establishes compilation and provenance only. The required signed
Mac test links a sixteenth image containing those JNI/caller objects and the
existing ten original libutils/Looper objects. Its Android-only callback runs
after M3's managed checks and before the same interpreter-policy and VM-shutdown
checks. The existing M3 entry retains its original classpath and acceptance.
An extension failure still shuts down the VM before reporting failure.

The test registers the original six JNI methods in the actual ART VM, constructs
the original Java queue, observes polling and wake across attached threads,
exercises descriptor removal and disposes native/managed owners. An omitted-wake
control must time out and release both owners. Another process removes the
framework DEX and requires a class-lookup failure, followed by checked shutdown.
Missing console output and missing hello DEX remain independent M3 controls.
These paths are implemented but runtime acceptance remains unverified until CI
executes and preserves their actual observations. Then Looper/Handler
dispatch, Activity construction, resources, lifecycle and MotionEvent support
can build on that evidence. No SDK stub classes or mocked JVM satisfy this gate.
