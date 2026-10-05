# Graphics bring-up

M5 is in progress. The first boundary is native Apple ANGLE; actual Android
Activity construction, ART integration, window composition and touch dispatch
remain required. A native color surface alone does not complete M5.

## Source and build

`third_party/angle/sources.json` selects unchanged ANGLE from AOSP's named
`android-15.0.0_r1` tag, commit `ba7c7168f85732bc9fe478b730505ef2265a666e`.
The Metal-only recipe in `metal-build.json` retains the original GN source
groups and explicit Apple additions. Python prepares hash-checked sources on
any host; Xcode compiles native arm64 Mac and iOS 15 frameworks. No Chromium
checkout, depot_tools, GN, Vulkan, SwiftShader or software CPU JIT is needed.

The small Chromium compression helper uses the Chromium revision declared by
that ANGLE DEPS file. Its original implementation calls public system zlib.
ANGLE, xxHash, Chromium and zlib notices accompany the source and frameworks.
The build is original ARTBox glue; upstream implementation files are unchanged.
This focused recipe must be reviewed again when changing the upstream pin.

```
python -B scripts/build_angle.py --prepare
# On macOS with Xcode:
python3 -B scripts/build_angle.py --jobs 2
```

ANGLE runs as native platform code, outside Bionic. Future guest EGL/GLES
adapters need explicit C boundaries and handle ownership; no C++ object or
allocator ownership may cross the guest/Apple boundary. Native frameworks are
compiled and signed before packaging. Internal and application GPU shaders use
ANGLE's GLSL-to-MSL translator and public Metal shader compilation. This does
not generate CPU instructions or request executable anonymous memory. First-use
shader compilation can still cause latency; it must be measured separately.

`ANGLE_ENABLE_METAL_OWNERSHIP_IDENTITY` is absent. Upstream's corresponding SPI
header is an empty placeholder, and its private resource API is conditionally
disabled. The framework uses public Apple SDKs. Symbol and entitlement evidence
is retained; this narrow review is not an App Store acceptance determination.

The generated serialized-program identifier hashes the entire pinned source
catalog and recipe. It intentionally differs from upstream's multi-backend GN
identifier, preventing cross-build program-cache reuse. It is a cache identity,
not executable-code signing or a claim of upstream binary compatibility.

## Acceptance boundaries

The native probe requires two valid GLES shaders to produce Metal source and
an invalid shader to fail with diagnostics. It queries a real Metal device. If
one exists, a specifically selected Metal EGL display must clear an RGBA8
32-by-32 pbuffer red, then green; all 2,048 pixels must match readback and the
context/surface must be destroyed successfully. No alternative backend is used.

A runner with no Metal device records `metal_available=false` and zero verified
pixels. Shader translation remains mandatory there; GPU execution remains
unverified and cannot satisfy the later M5 screenshot/touch gate. CI compilation
and this probe do not claim an Activity, a composed window or physical execution.

The next integration needs original AOSP Activity/Looper/MessageQueue support,
a Metal-backed window with lifetime tests, UIKit-to-MotionEvent translation,
and compatible ownership of ART and retained Binder services. Launcher polish
remains deferred.
