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
ANGLE, xxHash, Chromium, zlib, Khronos and generated-parser notices accompany
the source and frameworks. Both generated parser files retain their Bison
output exception verbatim, alongside the complete GPL-3.0 text.
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

## Native window ownership

`platform/include/artbox/native_surface.h` is the C boundary for native Apple
window ownership. It retains a supplied `CAMetalLayer` and owns its EGL surface
and GLES2 context. A single native ANGLE display remains initialized until the
last owner is destroyed; destroying one window leaves other contexts usable.
Creation does not change the current context. Callers bind before native GLES
commands and explicitly present afterwards. No EGL handle, Objective-C object
ownership or C++ allocation crosses into guest code through this interface.

The first implementation confines these operations and layer geometry changes
to the main thread. It rejects duplicate layer ownership and use from worker
threads. This simplifies UIKit lifetime ordering at the cost of restricting
render-thread scheduling; a later guest EGL adapter will need explicit context
transfer. Suspend unbinds and destroys the drawable surface while retaining the
context and its textures. Resume creates a new surface at the layer's current
bounds and scale. The UI owner must suspend before background rendering becomes
unavailable; this API alone does not implement iOS application lifecycle hooks.

The native window test requires Metal and an AppKit window server. It checks
7,296 pixels across seven frames in two windows, ordinary and scaled resize,
retained texture bytes after suspend/resume, six worker-thread rejections, and
display recreation after final teardown. Its signed executable and both native
surface static libraries accompany the ANGLE artifact. At `94523db`, this
test passes on the Apple Paravirtual Metal device, and the iOS 15 adapter
compiles. Independent artifact checks verify 926 source inputs, 13 project
inputs, both surface libraries, four signed Mach-O images and 18 notices.
The window contract takes 298.853 ms, after a separate 6,762.175 ms shader and
pbuffer probe. This includes UI/context setup and teardown and benefits from
the preceding graphics work; it is not a frame-rate measurement. Neither test
is a screenshot or an Android Activity test.
Probe processes have a 120-second timeout: the pinned upstream drawable
acquisition can otherwise retry without a timeout when no drawable is available.
The initial window candidate compiled its platform adapter but failed compiling
the probe because `CATransaction` was not imported. Adding the explicit public
header resolved that failure; no upstream source or check was disabled.

At `01e6e05`, the native Apple job passes and its artifact independently verifies
924 source inputs, ten project files, both signed framework images and the
signed probe. The Mac runner exposes an Apple Paravirtual Metal device: both
shader translations, malformed-shader rejection, all 2,048 pixel checks and
surface/context teardown pass. The complete cold probe takes 5,641.618 ms;
this includes initialization and is not frame throughput. The iOS 15 framework
is 7,760,016 bytes and has no entitlements or writable executable segments.
Physical graphics execution and the complete M5 Activity test remain pending.

The first candidate stopped at a probe-only default-display type error. The
second compiled all selected units but failed linking because an upstream
implementation unit was grouped under `libangle_headers`. The source recipe
now includes that group and rejects disagreement between its reviewed groups
and compilation units. No upstream implementation changes were needed.
